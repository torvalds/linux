// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <linux/mm.h> /* for handle_mm_fault() */
#include <linux/ftrace.h>
#ifndef CONFIG_ARM64
#include <asm/asm-offsets.h>
#endif

extern void my_direct_func(struct vm_area_struct *vma, unsigned long address,
			   unsigned int flags, struct pt_regs *regs);

void my_direct_func(struct vm_area_struct *vma, unsigned long address,
		    unsigned int flags, struct pt_regs *regs)
{
	trace_printk("handle mm fault vma=%p address=%lx flags=%x regs=%p\n",
		     vma, address, flags, regs);
}

extern void my_tramp(void *);

#ifdef CONFIG_RISCV
#include <asm/asm.h>

asm (
"       .pushsection    .text, \"ax\", @progbits\n"
"       .type           my_tramp, @function\n"
"       .globl          my_tramp\n"
"   my_tramp:\n"
"       addi	sp,sp,-5*"SZREG"\n"
"       "REG_S"	a0,0*"SZREG"(sp)\n"
"       "REG_S"	a1,1*"SZREG"(sp)\n"
"       "REG_S"	a2,2*"SZREG"(sp)\n"
"       "REG_S"	t0,3*"SZREG"(sp)\n"
"       "REG_S"	ra,4*"SZREG"(sp)\n"
"       call	my_direct_func\n"
"       "REG_L"	a0,0*"SZREG"(sp)\n"
"       "REG_L"	a1,1*"SZREG"(sp)\n"
"       "REG_L"	a2,2*"SZREG"(sp)\n"
"       "REG_L"	t0,3*"SZREG"(sp)\n"
"       "REG_L"	ra,4*"SZREG"(sp)\n"
"       addi	sp,sp,5*"SZREG"\n"
"       jr	t0\n"
"       .size           my_tramp, .-my_tramp\n"
"       .popsection\n"
);

#endif /* CONFIG_RISCV */

#ifdef CONFIG_X86_64

#include <asm/ibt.h>
#include <asm/nospec-branch.h>

asm (
"	.pushsection    .text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:"
	ASM_ENDBR
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
	CALL_DEPTH_ACCOUNT
"	pushq %rdi\n"
"	pushq %rsi\n"
"	pushq %rdx\n"
"	pushq %rcx\n"
"	call my_direct_func\n"
"	popq %rcx\n"
"	popq %rdx\n"
"	popq %rsi\n"
"	popq %rdi\n"
"	leave\n"
	ASM_RET
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_X86_64 */

#ifdef CONFIG_S390

asm (
"	.pushsection	.text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:"
"	lgr		%r1,%r15\n"
"	stmg		%r0,%r5,"__stringify(__SF_GPRS)"(%r15)\n"
"	stg		%r14,"__stringify(__SF_GPRS+8*8)"(%r15)\n"
"	aghi		%r15,"__stringify(-STACK_FRAME_OVERHEAD)"\n"
"	stg		%r1,"__stringify(__SF_BACKCHAIN)"(%r15)\n"
"	brasl		%r14,my_direct_func\n"
"	aghi		%r15,"__stringify(STACK_FRAME_OVERHEAD)"\n"
"	lmg		%r0,%r5,"__stringify(__SF_GPRS)"(%r15)\n"
"	lg		%r14,"__stringify(__SF_GPRS+8*8)"(%r15)\n"
"	lgr		%r1,%r0\n"
"	br		%r1\n"
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_S390 */

#ifdef CONFIG_ARM64

asm (
"	.pushsection	.text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:"
"	hint	34\n" // bti	c
"	sub	sp, sp, #48\n"
"	stp	x9, x30, [sp]\n"
"	stp	x0, x1, [sp, #16]\n"
"	stp	x2, x3, [sp, #32]\n"
"	bl	my_direct_func\n"
"	ldp	x30, x9, [sp]\n"
"	ldp	x0, x1, [sp, #16]\n"
"	ldp	x2, x3, [sp, #32]\n"
"	add	sp, sp, #48\n"
"	ret	x9\n"
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_ARM64 */

#ifdef CONFIG_LOONGARCH

asm (
"	.pushsection	.text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:\n"
"	addi.d	$sp, $sp, -48\n"
"	st.d	$a0, $sp, 0\n"
"	st.d	$a1, $sp, 8\n"
"	st.d	$a2, $sp, 16\n"
"	st.d	$t0, $sp, 24\n"
"	st.d	$ra, $sp, 32\n"
"	bl	my_direct_func\n"
"	ld.d	$a0, $sp, 0\n"
"	ld.d	$a1, $sp, 8\n"
"	ld.d	$a2, $sp, 16\n"
"	ld.d	$t0, $sp, 24\n"
"	ld.d	$ra, $sp, 32\n"
"	addi.d	$sp, $sp, 48\n"
"	jr	$t0\n"
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_LOONGARCH */

static struct ftrace_ops direct;

static int __init ftrace_direct_init(void)
{
	ftrace_set_filter_ip(&direct, (unsigned long) handle_mm_fault, 0, 0);

	return register_ftrace_direct(&direct, (unsigned long) my_tramp);
}

static void __exit ftrace_direct_exit(void)
{
	unregister_ftrace_direct(&direct, (unsigned long)my_tramp, true);
}

module_init(ftrace_direct_init);
module_exit(ftrace_direct_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("Another example use case of using register_ftrace_direct()");
MODULE_LICENSE("GPL");
