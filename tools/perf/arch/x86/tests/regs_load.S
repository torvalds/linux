/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

#define AX	 0
#define BX	 1 * 8
#define CX	 2 * 8
#define DX	 3 * 8
#define SI	 4 * 8
#define DI	 5 * 8
#define BP	 6 * 8
#define SP	 7 * 8
#define IP	 8 * 8
#define FLAGS	 9 * 8
#define CS	10 * 8
#define SS	11 * 8
#define DS	12 * 8
#define ES	13 * 8
#define FS	14 * 8
#define GS	15 * 8
#define R8	16 * 8
#define R9	17 * 8
#define R10	18 * 8
#define R11	19 * 8
#define R12	20 * 8
#define R13	21 * 8
#define R14	22 * 8
#define R15	23 * 8

.text
#ifdef HAVE_ARCH_X86_64_SUPPORT
ENTRY(perf_regs_load)
	movq %rax, AX(%rdi)
	movq %rbx, BX(%rdi)
	movq %rcx, CX(%rdi)
	movq %rdx, DX(%rdi)
	movq %rsi, SI(%rdi)
	movq %rdi, DI(%rdi)
	movq %rbp, BP(%rdi)

	leaq 8(%rsp), %rax /* exclude this call.  */
	movq %rax, SP(%rdi)

	movq 0(%rsp), %rax
	movq %rax, IP(%rdi)

	movq $0, FLAGS(%rdi)
	movq $0, CS(%rdi)
	movq $0, SS(%rdi)
	movq $0, DS(%rdi)
	movq $0, ES(%rdi)
	movq $0, FS(%rdi)
	movq $0, GS(%rdi)

	movq %r8,  R8(%rdi)
	movq %r9,  R9(%rdi)
	movq %r10, R10(%rdi)
	movq %r11, R11(%rdi)
	movq %r12, R12(%rdi)
	movq %r13, R13(%rdi)
	movq %r14, R14(%rdi)
	movq %r15, R15(%rdi)
	ret
ENDPROC(perf_regs_load)
#else
ENTRY(perf_regs_load)
	push %edi
	movl 8(%esp), %edi
	movl %eax, AX(%edi)
	movl %ebx, BX(%edi)
	movl %ecx, CX(%edi)
	movl %edx, DX(%edi)
	movl %esi, SI(%edi)
	pop %eax
	movl %eax, DI(%edi)
	movl %ebp, BP(%edi)

	leal 4(%esp), %eax /* exclude this call.  */
	movl %eax, SP(%edi)

	movl 0(%esp), %eax
	movl %eax, IP(%edi)

	movl $0, FLAGS(%edi)
	movl $0, CS(%edi)
	movl $0, SS(%edi)
	movl $0, DS(%edi)
	movl $0, ES(%edi)
	movl $0, FS(%edi)
	movl $0, GS(%edi)
	ret
ENDPROC(perf_regs_load)
#endif

/*
 * We need to provide note.GNU-stack section, saying that we want
 * NOT executable stack. Otherwise the final linking will assume that
 * the ELF stack should not be restricted at all and set it RWX.
 */
.section .note.GNU-stack,"",@progbits
