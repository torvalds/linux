// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <linux/mm.h> /* for handle_mm_fault() */
#include <linux/ftrace.h>

void my_direct_func(struct vm_area_struct *vma,
			unsigned long address, unsigned int flags)
{
	trace_printk("handle mm fault vma=%p address=%lx flags=%x\n",
		     vma, address, flags);
}

extern void my_tramp(void *);

asm (
"	.pushsection    .text, \"ax\", @progbits\n"
"   my_tramp:"
"	pushq %rbp\n"
"	movq %rsp, %rbp\n"
"	pushq %rdi\n"
"	pushq %rsi\n"
"	pushq %rdx\n"
"	call my_direct_func\n"
"	popq %rdx\n"
"	popq %rsi\n"
"	popq %rdi\n"
"	leave\n"
"	ret\n"
"	.popsection\n"
);


static int __init ftrace_direct_init(void)
{
	return register_ftrace_direct((unsigned long)handle_mm_fault,
				     (unsigned long)my_tramp);
}

static void __exit ftrace_direct_exit(void)
{
	unregister_ftrace_direct((unsigned long)handle_mm_fault,
				 (unsigned long)my_tramp);
}

module_init(ftrace_direct_init);
module_exit(ftrace_direct_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("Another example use case of using register_ftrace_direct()");
MODULE_LICENSE("GPL");
