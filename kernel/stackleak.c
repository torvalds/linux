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
#include <linux/sysctl.h>

static DEFINE_STATIC_KEY_FALSE(stack_erasing_bypass);

int stack_erasing_sysctl(struct ctl_table *table, int write,
			void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = 0;
	int state = !static_branch_unlikely(&stack_erasing_bypass);
	int prev_state = state;

	table->data = &state;
	table->maxlen = sizeof(int);
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	state = !!state;
	if (ret || !write || state == prev_state)
		return ret;

	if (state)
		static_branch_disable(&stack_erasing_bypass);
	else
		static_branch_enable(&stack_erasing_bypass);

	pr_warn("stackleak: kernel stack erasing is %s\n",
					state ? "enabled" : "disabled");
	return ret;
}

#define skip_erasing()	static_branch_unlikely(&stack_erasing_bypass)
#else
#define skip_erasing()	false
#endif /* CONFIG_STACKLEAK_RUNTIME_DISABLE */

asmlinkage void noinstr stackleak_erase(void)
{
	/* It would be nice not to have 'kstack_ptr' and 'boundary' on stack */
	unsigned long kstack_ptr = current->lowest_stack;
	unsigned long boundary = (unsigned long)end_of_stack(current);
	unsigned int poison_count = 0;
	const unsigned int depth = STACKLEAK_SEARCH_DEPTH / sizeof(unsigned long);

	if (skip_erasing())
		return;

	/* Check that 'lowest_stack' value is sane */
	if (unlikely(kstack_ptr - boundary >= THREAD_SIZE))
		kstack_ptr = boundary;

	/* Search for the poison value in the kernel stack */
	while (kstack_ptr > boundary && poison_count <= depth) {
		if (*(unsigned long *)kstack_ptr == STACKLEAK_POISON)
			poison_count++;
		else
			poison_count = 0;

		kstack_ptr -= sizeof(unsigned long);
	}

	/*
	 * One 'long int' at the bottom of the thread stack is reserved and
	 * should not be poisoned (see CONFIG_SCHED_STACK_END_CHECK=y).
	 */
	if (kstack_ptr == boundary)
		kstack_ptr += sizeof(unsigned long);

#ifdef CONFIG_STACKLEAK_METRICS
	current->prev_lowest_stack = kstack_ptr;
#endif

	/*
	 * Now write the poison value to the kernel stack. Start from
	 * 'kstack_ptr' and move up till the new 'boundary'. We assume that
	 * the stack pointer doesn't change when we write poison.
	 */
	if (on_thread_stack())
		boundary = current_stack_pointer;
	else
		boundary = current_top_of_stack();

	while (kstack_ptr < boundary) {
		*(unsigned long *)kstack_ptr = STACKLEAK_POISON;
		kstack_ptr += sizeof(unsigned long);
	}

	/* Reset the 'lowest_stack' value for the next syscall */
	current->lowest_stack = current_top_of_stack() - THREAD_SIZE/64;
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
	    sp >= (unsigned long)task_stack_page(current) +
						sizeof(unsigned long)) {
		current->lowest_stack = sp;
	}
}
EXPORT_SYMBOL(stackleak_track_stack);
