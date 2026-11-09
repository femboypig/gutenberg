// The bridge keeps Android stack arguments in eight-byte slots on Darwin too.
#if defined(__APPLE__)
#define GUTENBERG_NATIVE_ENTRY "_gutenberg_call_native"
#else
#define GUTENBERG_NATIVE_ENTRY "gutenberg_call_native"
#endif

#if defined(__aarch64__) || defined(__arm64__)
asm(".text\n.p2align 4\n.globl " GUTENBERG_NATIVE_ENTRY "\n" GUTENBERG_NATIVE_ENTRY ":\n"
    R"(
    .cfi_startproc
    stp x29, x30, [sp, #-32]!
    .cfi_def_cfa_offset 32
    .cfi_offset x29, -32
    .cfi_offset x30, -24
    mov x29, sp
    .cfi_def_cfa_register x29
    stp x19, x20, [sp, #16]
    .cfi_offset x19, -16
    .cfi_offset x20, -8
    mov x20, x0
    mov x19, x1
    ldr x9, [x19, #136]
    lsl x10, x9, #3
    add x10, x10, #15
    and x10, x10, #-16
    sub sp, sp, x10
    ldr x11, [x19, #128]
    mov x12, sp
    cbz x9, 2f
1:
    ldr x13, [x11], #8
    str x13, [x12], #8
    subs x9, x9, #1
    b.ne 1b
2:
    ldp d0, d1, [x19, #64]
    ldp d2, d3, [x19, #80]
    ldp d4, d5, [x19, #96]
    ldp d6, d7, [x19, #112]
    ldp x0, x1, [x19]
    ldp x2, x3, [x19, #16]
    ldp x4, x5, [x19, #32]
    ldp x6, x7, [x19, #48]
    blr x20
    str x0, [x19, #144]
    str d0, [x19, #152]
    mov sp, x29
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    .cfi_def_cfa sp, 0
    ret
    .cfi_endproc
)");
#elif defined(__x86_64__)
asm(".text\n.p2align 4\n.globl " GUTENBERG_NATIVE_ENTRY "\n" GUTENBERG_NATIVE_ENTRY ":\n"
    R"(
    .cfi_startproc
    pushq %rbp
    .cfi_def_cfa_offset 16
    .cfi_offset %rbp, -16
    movq %rsp, %rbp
    .cfi_def_cfa_register %rbp
    pushq %r12
    .cfi_offset %r12, -24
    pushq %r13
    .cfi_offset %r13, -32
    movq %rdi, %r13
    movq %rsi, %r12
    movq 136(%r12), %rcx
    leaq 15(,%rcx,8), %rax
    andq $-16, %rax
    subq %rax, %rsp
    movq %rsp, %rdi
    movq 128(%r12), %rsi
    rep movsq
    movq 64(%r12), %xmm0
    movq 72(%r12), %xmm1
    movq 80(%r12), %xmm2
    movq 88(%r12), %xmm3
    movq 96(%r12), %xmm4
    movq 104(%r12), %xmm5
    movq 112(%r12), %xmm6
    movq 120(%r12), %xmm7
    movq 0(%r12), %rdi
    movq 8(%r12), %rsi
    movq 16(%r12), %rdx
    movq 24(%r12), %rcx
    movq 32(%r12), %r8
    movq 40(%r12), %r9
    movl $8, %eax
    call *%r13
    movq %rax, 144(%r12)
    movq %xmm0, 152(%r12)
    leaq -16(%rbp), %rsp
    popq %r13
    popq %r12
    popq %rbp
    .cfi_def_cfa %rsp, 8
    ret
    .cfi_endproc
)");
#endif
#undef GUTENBERG_NATIVE_ENTRY
