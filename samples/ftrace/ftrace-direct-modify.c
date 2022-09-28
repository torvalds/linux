// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/ftrace.h>

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

asm (
"	.pushsection    .text, \"ax\", @progbits\n"
"	.type		my_tramp1, @function\n"
"	.globl		my_tramp1\n"
"   my_tramp1:"
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
"	call my_direct_func1\n"
"	leave\n"
"	.size		my_tramp1, .-my_tramp1\n"
	ASM_RET
"	.type		my_tramp2, @function\n"
"	.globl		my_tramp2\n"
"   my_tramp2:"
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
"	call my_direct_func2\n"
"	leave\n"
	ASM_RET
"	.size		my_tramp2, .-my_tramp2\n"
"	.popsection\n"
);

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
		ret = modify_ftrace_direct(my_ip, my_tramp, tramps[t]);
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

	ret = register_ftrace_direct(my_ip, my_tramp);
	if (!ret)
		simple_tsk = kthread_run(simple_thread, NULL, "event-sample-fn");
	return ret;
}

static void __exit ftrace_direct_exit(void)
{
	kthread_stop(simple_tsk);
	unregister_ftrace_direct(my_ip, my_tramp);
}

module_init(ftrace_direct_init);
module_exit(ftrace_direct_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("Example use case of using modify_ftrace_direct()");
MODULE_LICENSE("GPL");
