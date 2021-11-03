// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <linux/mm.h> /* for handle_mm_fault() */
#include <linux/ftrace.h>
#include <linux/sched/stat.h>

void my_direct_func(unsigned long ip)
{
	trace_printk("ip %lx\n", ip);
}

extern void my_tramp(void *);

asm (
"	.pushsection    .text, \"ax\", @progbits\n"
"	.type		my_tramp, @function\n"
"	.globl		my_tramp\n"
"   my_tramp:"
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
"	pushq %rdi\n"
"	movq 8(%rbp), %rdi\n"
"	call my_direct_func\n"
"	popq %rdi\n"
"	leave\n"
"	ret\n"
"	.size		my_tramp, .-my_tramp\n"
"	.popsection\n"
);

static struct ftrace_ops direct;

static int __init ftrace_direct_multi_init(void)
{
	ftrace_set_filter_ip(&direct, (unsigned long) wake_up_process, 0, 0);
	ftrace_set_filter_ip(&direct, (unsigned long) schedule, 0, 0);

	return register_ftrace_direct_multi(&direct, (unsigned long) my_tramp);
}

static void __exit ftrace_direct_multi_exit(void)
{
	unregister_ftrace_direct_multi(&direct, (unsigned long) my_tramp);
}

module_init(ftrace_direct_multi_init);
module_exit(ftrace_direct_multi_exit);

MODULE_AUTHOR("Jiri Olsa");
MODULE_DESCRIPTION("Example use case of using register_ftrace_direct_multi()");
MODULE_LICENSE("GPL");
