// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <linux/mm.h> /* for handle_mm_fault() */
#include <linux/ftrace.h>
#include <linux/sched/stat.h>
#if !defined(CONFIG_ARM64) && !defined(CONFIG_PPC32)
#include <asm/asm-offsets.h>
#endif

extern void my_direct_func(unsigned long ip);

void my_direct_func(unsigned long ip)
{
	trace_printk("ip %lx\n", ip);
}

extern void my_tramp(void *);

#ifdef CONFIG_RISCV
#include <asm/asm.h>

asm (
"       .pushsection    .text, \"ax\", @progbits\n"
"       .type           my_tramp, @function\n"
"       .globl          my_tramp\n"
"   my_tramp:\n"
"       addi	sp,sp,-3*"SZREG"\n"
"       "REG_S"	a0,0*"SZREG"(sp)\n"
"       "REG_S"	t0,1*"SZREG"(sp)\n"
"       "REG_S"	ra,2*"SZREG"(sp)\n"
"       mv	a0,t0\n"
"       call	my_direct_func\n"
"       "REG_L"	a0,0*"SZREG"(sp)\n"
"       "REG_L"	t0,1*"SZREG"(sp)\n"
"       "REG_L"	ra,2*"SZREG"(sp)\n"
"       addi	sp,sp,3*"SZREG"\n"
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
"	movq 8(%rbp), %rdi\n"
"	call my_direct_func\n"
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
"	lgr		%r2,%r0\n"
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
"	sub	sp, sp, #32\n"
"	stp	x9, x30, [sp]\n"
"	str	x0, [sp, #16]\n"
"	mov	x0, x30\n"
"	bl	my_direct_func\n"
"	ldp	x30, x9, [sp]\n"
"	ldr	x0, [sp, #16]\n"
"	add	sp, sp, #32\n"
"	ret	x9\n"
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_ARM64 */

#ifdef CONFIG_LOONGARCH

#include <asm/asm.h>
asm (
"	.pushsection	.text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:\n"
"	addi.d	$sp, $sp, -32\n"
"	st.d	$a0, $sp, 0\n"
"	st.d	$t0, $sp, 8\n"
"	st.d	$ra, $sp, 16\n"
"	move	$a0, $t0\n"
"	bl	my_direct_func\n"
"	ld.d	$a0, $sp, 0\n"
"	ld.d	$t0, $sp, 8\n"
"	ld.d	$ra, $sp, 16\n"
"	addi.d	$sp, $sp, 32\n"
"	jr	$t0\n"
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_LOONGARCH */

#ifdef CONFIG_PPC
#include <asm/ppc_asm.h>

#ifdef CONFIG_PPC64
#define STACK_FRAME_SIZE 48
#else
#define STACK_FRAME_SIZE 24
#endif

#if defined(CONFIG_PPC64_ELF_ABI_V2) && !defined(CONFIG_PPC_KERNEL_PCREL)
#define PPC64_TOC_SAVE_AND_UPDATE			\
"	std		2, 24(1)\n"			\
"	bcl		20, 31, 1f\n"			\
"   1:	mflr		12\n"				\
"	ld		2, (99f - 1b)(12)\n"
#define PPC64_TOC_RESTORE				\
"	ld		2, 24(1)\n"
#define PPC64_TOC					\
"   99:	.quad		.TOC.@tocbase\n"
#else
#define PPC64_TOC_SAVE_AND_UPDATE ""
#define PPC64_TOC_RESTORE ""
#define PPC64_TOC ""
#endif

#ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
#define PPC_FTRACE_RESTORE_LR				\
	PPC_LL"		0, "__stringify(PPC_LR_STKOFF)"(1)\n"	\
"	mtlr		0\n"
#define PPC_FTRACE_RET					\
"	blr\n"
#define PPC_FTRACE_RECOVER_IP				\
"	lwz		8, 4(3)\n"			\
"	li		9, 6\n"				\
"	slw		8, 8, 9\n"			\
"	sraw		8, 8, 9\n"			\
"	add		3, 3, 8\n"			\
"	addi		3, 3, 4\n"
#else
#define PPC_FTRACE_RESTORE_LR				\
	PPC_LL"		0, "__stringify(PPC_LR_STKOFF)"(1)\n"	\
"	mtctr		0\n"
#define PPC_FTRACE_RET					\
"	mtlr		0\n"				\
"	bctr\n"
#define PPC_FTRACE_RECOVER_IP ""
#endif

asm (
"	.pushsection	.text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:\n"
	PPC_STL"	0, "__stringify(PPC_LR_STKOFF)"(1)\n"
	PPC_STLU"	1, -"__stringify(STACK_FRAME_MIN_SIZE)"(1)\n"
"	mflr		0\n"
	PPC_STL"	0, "__stringify(PPC_LR_STKOFF)"(1)\n"
	PPC_STLU"	1, -"__stringify(STACK_FRAME_SIZE)"(1)\n"
	PPC64_TOC_SAVE_AND_UPDATE
	PPC_STL"	3, "__stringify(STACK_FRAME_MIN_SIZE)"(1)\n"
"	mr		3, 0\n"
	PPC_FTRACE_RECOVER_IP
"	bl		my_direct_func\n"
	PPC_LL"		3, "__stringify(STACK_FRAME_MIN_SIZE)"(1)\n"
	PPC64_TOC_RESTORE
"	addi		1, 1, "__stringify(STACK_FRAME_SIZE)"\n"
	PPC_FTRACE_RESTORE_LR
"	addi		1, 1, "__stringify(STACK_FRAME_MIN_SIZE)"\n"
	PPC_LL"		0, "__stringify(PPC_LR_STKOFF)"(1)\n"
	PPC_FTRACE_RET
	PPC64_TOC
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

#endif /* CONFIG_PPC */

static struct ftrace_ops direct;

static int __init ftrace_direct_multi_init(void)
{
	ftrace_set_filter_ip(&direct, (unsigned long) wake_up_process, 0, 0);
	ftrace_set_filter_ip(&direct, (unsigned long) schedule, 0, 0);

	return register_ftrace_direct(&direct, (unsigned long) my_tramp);
}

static void __exit ftrace_direct_multi_exit(void)
{
	unregister_ftrace_direct(&direct, (unsigned long) my_tramp, true);
}

module_init(ftrace_direct_multi_init);
module_exit(ftrace_direct_multi_exit);

MODULE_AUTHOR("Jiri Olsa");
MODULE_DESCRIPTION("Example use case of using register_ftrace_direct_multi()");
MODULE_LICENSE("GPL");
