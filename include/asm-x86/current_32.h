#ifndef _I386_CURRENT_H
#define _I386_CURRENT_H

#include <linux/compiler.h>
#include <asm/percpu.h>

struct task_struct;

DECLARE_PER_CPU(struct task_struct *, current_task);
static __always_inline struct task_struct *get_current(void)
{
	return x86_read_percpu(current_task);
}

#define current get_current()

#endif /* !(_I386_CURRENT_H) */
