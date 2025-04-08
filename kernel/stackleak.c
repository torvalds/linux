// SPDX-License-Identifier: GPL-2.0
/*
 * This code fills the used part of the kernel stack with a poison value
 * before returning to userspace. It's part of the STACKLEAK feature
 * ported from grsecurity/PaX.
 *
 * Author: Alexander Popov <alex.popov@linux.com>
 *
 * STACKLEAK reduces the information which kernel stack leak bugs can
 * reveal and blocks some uninitialized stack variable attacks.
 */

#include <linux/stackleak.h>
#include <linux/kprobes.h>

#ifdef CONFIG_STACKLEAK_RUNTIME_DISABLE
#include <linux/jump_label.h>
#include <linux/string_choices.h>
#include <linux/sysctl.h>
#include <linux/init.h>

static DEFINE_STATIC_KEY_FALSE(stack_erasing_bypass);

#ifdef CONFIG_SYSCTL
static int stack_erasing_sysctl(const struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = 0;
	int state = !static_branch_unlikely(&stack_erasing_bypass);
	int prev_state = state;
	struct ctl_table table_copy = *table;

	table_copy.data = &state;
	ret = proc_dointvec_minmax(&table_copy, write, buffer, lenp, ppos);
	state = !!state;
	if (ret || !write || state == prev_state)
		return ret;

	if (state)
		static_branch_disable(&stack_erasing_bypass);
	else
		static_branch_enable(&stack_erasing_bypass);

	pr_warn("stackleak: kernel stack erasing is %s\n",
					str_enabled_disabled(state));
	return ret;
}
static const struct ctl_table stackleak_sysctls[] = {
	{
		.procname	= "stack_erasing",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= stack_erasing_sysctl,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

static int __init stackleak_sysctls_init(void)
{
	register_sysctl_init("kernel", stackleak_sysctls);
	return 0;
}
late_initcall(stackleak_sysctls_init);
#endif /* CONFIG_SYSCTL */

#define skip_erasing()	static_branch_unlikely(&stack_erasing_bypass)
#else
#define skip_erasing()	false
#endif /* CONFIG_STACKLEAK_RUNTIME_DISABLE */

#ifndef __stackleak_poison
static __always_inline void __stackleak_poison(unsigned long erase_low,
					       unsigned long erase_high,
					       unsigned long poison)
{
	while (erase_low < erase_high) {
		*(unsigned long *)erase_low = poison;
		erase_low += sizeof(unsigned long);
	}
}
#endif

static __always_inline void __stackleak_erase(bool on_task_stack)
{
	const unsigned long task_stack_low = stackleak_task_low_bound(current);
	const unsigned long task_stack_high = stackleak_task_high_bound(current);
	unsigned long erase_low, erase_high;

	erase_low = stackleak_find_top_of_poison(task_stack_low,
						 current->lowest_stack);

#ifdef CONFIG_STACKLEAK_METRICS
	current->prev_lowest_stack = erase_low;
#endif

	/*
	 * Write poison to the task's stack between 'erase_low' and
	 * 'erase_high'.
	 *
	 * If we're running on a different stack (e.g. an entry trampoline
	 * stack) we can erase everything below the pt_regs at the top of the
	 * task stack.
	 *
	 * If we're running on the task stack itself, we must not clobber any
	 * stack used by this function and its caller. We assume that this
	 * function has a fixed-size stack frame, and the current stack pointer
	 * doesn't change while we write poison.
	 */
	if (on_task_stack)
		erase_high = current_stack_pointer;
	else
		erase_high = task_stack_high;

	__stackleak_poison(erase_low, erase_high, STACKLEAK_POISON);

	/* Reset the 'lowest_stack' value for the next syscall */
	current->lowest_stack = task_stack_high;
}

/*
 * Erase and poison the portion of the task stack used since the last erase.
 * Can be called from the task stack or an entry stack when the task stack is
 * no longer in use.
 */
asmlinkage void noinstr stackleak_erase(void)
{
	if (skip_erasing())
		return;

	__stackleak_erase(on_thread_stack());
}

/*
 * Erase and poison the portion of the task stack used since the last erase.
 * Can only be called from the task stack.
 */
asmlinkage void noinstr stackleak_erase_on_task_stack(void)
{
	if (skip_erasing())
		return;

	__stackleak_erase(true);
}

/*
 * Erase and poison the portion of the task stack used since the last erase.
 * Can only be called from a stack other than the task stack.
 */
asmlinkage void noinstr stackleak_erase_off_task_stack(void)
{
	if (skip_erasing())
		return;

	__stackleak_erase(false);
}

void __used __no_caller_saved_registers noinstr stackleak_track_stack(void)
{
	unsigned long sp = current_stack_pointer;

	/*
	 * Having CONFIG_STACKLEAK_TRACK_MIN_SIZE larger than
	 * STACKLEAK_SEARCH_DEPTH makes the poison search in
	 * stackleak_erase() unreliable. Let's prevent that.
	 */
	BUILD_BUG_ON(CONFIG_STACKLEAK_TRACK_MIN_SIZE > STACKLEAK_SEARCH_DEPTH);

	/* 'lowest_stack' should be aligned on the register width boundary */
	sp = ALIGN(sp, sizeof(unsigned long));
	if (sp < current->lowest_stack &&
	    sp >= stackleak_task_low_bound(current)) {
		current->lowest_stack = sp;
	}
}
EXPORT_SYMBOL(stackleak_track_stack);
