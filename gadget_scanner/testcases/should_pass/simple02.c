int main(int argc, char *argv[])
{
    asm volatile("mov 0x20(%r13), %r14");
    asm volatile("mov 0x28(%r13), %r15");
    asm volatile("mov (%r15), %ax");
    asm volatile("add %r14, %rax");
    asm volatile("mov %r14, (%rax)");
    return 0;
}
