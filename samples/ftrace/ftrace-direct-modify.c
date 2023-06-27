// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/ftrace.h>
#include <asm/asm-offsets.h>

extern void my_direct_func1(void);
extern void my_direct_func2(void);

void my_direct_func1(void)
{
	trace_printk("my direct func1\n");
}

void my_direct_func2(void)
{
	trace_printk("my direct func2\n");
}

extern void my_tramp1(void *);
extern void my_tramp2(void *);

static unsigned long my_ip = (unsigned long)schedule;

#ifdef CONFIG_X86_64

#include <asm/ibt.h>
#include <asm/nospec-branch.h>

asm (
"	.pushsection    .text, \"ax\", @progbits\n"
"	.type		my_tramp1, @function\n"
"	.globl		my_tramp1\n"
"   my_tramp1:"
	ASM_ENDBR
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
	CALL_DEPTH_ACCOUNT
"	call my_direct_func1\n"
"	leave\n"
"	.size		my_tramp1, .-my_tramp1\n"
	ASM_RET

"	.type		my_tramp2, @function\n"
"	.globl		my_tramp2\n"
"   my_tramp2:"
	ASM_ENDBR
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
	CALL_DEPTH_ACCOUNT
"	call my_direct_func2\n"
"	leave\n"
	ASM_RET
"	.size		my_tramp2, .-my_tramp2\n"
"	.popsection\n"
);

#endif /* CONFIG_X86_64 */

#ifdef CONFIG_S390

asm (
"	.pushsection	.text, \"ax\", @progbits\n"
"	.type		my_tramp1, @function\n"
"	.globl		my_tramp1\n"
"   my_tramp1:"
"	lgr		%r1,%r15\n"
"	stmg		%r0,%r5,"__stringify(__SF_GPRS)"(%r15)\n"
"	stg		%r14,"__stringify(__SF_GPRS+8*8)"(%r15)\n"
"	aghi		%r15,"__stringify(-STACK_FRAME_OVERHEAD)"\n"
"	stg		%r1,"__stringify(__SF_BACKCHAIN)"(%r15)\n"
"	brasl		%r14,my_direct_func1\n"
"	aghi		%r15,"__stringify(STACK_FRAME_OVERHEAD)"\n"
"	lmg		%r0,%r5,"__stringify(__SF_GPRS)"(%r15)\n"
"	lg		%r14,"__stringify(__SF_GPRS+8*8)"(%r15)\n"
"	lgr		%r1,%r0\n"
"	br		%r1\n"
"	.size		my_tramp1, .-my_tramp1\n"
"	.type		my_tramp2, @function\n"
"	.globl		my_tramp2\n"
"   my_tramp2:"
"	lgr		%r1,%r15\n"
"	stmg		%r0,%r5,"__stringify(__SF_GPRS)"(%r15)\n"
"	stg		%r14,"__stringify(__SF_GPRS+8*8)"(%r15)\n"
"	aghi		%r15,"__stringify(-STACK_FRAME_OVERHEAD)"\n"
"	stg		%r1,"__stringify(__SF_BACKCHAIN)"(%r15)\n"
"	brasl		%r14,my_direct_func2\n"
"	aghi		%r15,"__stringify(STACK_FRAME_OVERHEAD)"\n"
"	lmg		%r0,%r5,"__stringify(__SF_GPRS)"(%r15)\n"
"	lg		%r14,"__stringify(__SF_GPRS+8*8)"(%r15)\n"
"	lgr		%r1,%r0\n"
"	br		%r1\n"
"	.size		my_tramp2, .-my_tramp2\n"
"	.popsection\n"
);

#endif /* CONFIG_S390 */

#ifdef CONFIG_LOONGARCH

asm (
"	.pushsection    .text, \"ax\", @progbits\n"
"	.type		my_tramp1, @function\n"
"	.globl		my_tramp1\n"
"   my_tramp1:\n"
"	addi.d	$sp, $sp, -16\n"
"	st.d	$t0, $sp, 0\n"
"	st.d	$ra, $sp, 8\n"
"	bl	my_direct_func1\n"
"	ld.d	$t0, $sp, 0\n"
"	ld.d	$ra, $sp, 8\n"
"	addi.d	$sp, $sp, 16\n"
"	jr	$t0\n"
"	.size		my_tramp1, .-my_tramp1\n"

"	.type		my_tramp2, @function\n"
"	.globl		my_tramp2\n"
"   my_tramp2:\n"
"	addi.d	$sp, $sp, -16\n"
"	st.d	$t0, $sp, 0\n"
"	st.d	$ra, $sp, 8\n"
"	bl	my_direct_func2\n"
"	ld.d	$t0, $sp, 0\n"
"	ld.d	$ra, $sp, 8\n"
"	addi.d	$sp, $sp, 16\n"
"	jr	$t0\n"
"	.size		my_tramp2, .-my_tramp2\n"
"	.popsection\n"
);

#endif /* CONFIG_LOONGARCH */

static struct ftrace_ops direct;

static unsigned long my_tramp = (unsigned long)my_tramp1;
static unsigned long tramps[2] = {
	(unsigned long)my_tramp1,
	(unsigned long)my_tramp2,
};

static int simple_thread(void *arg)
{
	static int t;
	int ret = 0;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2 * HZ);

		if (ret)
			continue;
		t ^= 1;
		ret = modify_ftrace_direct(&direct, tramps[t]);
		if (!ret)
			my_tramp = tramps[t];
		WARN_ON_ONCE(ret);
	}

	return 0;
}

static struct task_struct *simple_tsk;

static int __init ftrace_direct_init(void)
{
	int ret;

	ftrace_set_filter_ip(&direct, (unsigned long) my_ip, 0, 0);
	ret = register_ftrace_direct(&direct, my_tramp);

	if (!ret)
		simple_tsk = kthread_run(simple_thread, NULL, "event-sample-fn");
	return ret;
}

static void __exit ftrace_direct_exit(void)
{
	kthread_stop(simple_tsk);
	unregister_ftrace_direct(&direct, my_tramp, true);
}

module_init(ftrace_direct_init);
module_exit(ftrace_direct_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("Example use case of using modify_ftrace_direct()");
MODULE_LICENSE("GPL");
