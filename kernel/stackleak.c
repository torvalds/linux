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

asmlinkage void stackleak_erase(void)
{
	/* It would be nice not to have 'kstack_ptr' and 'boundary' on stack */
	unsigned long kstack_ptr = current->lowest_stack;
	unsigned long boundary = (unsigned long)end_of_stack(current);
	unsigned int poison_count = 0;
	const unsigned int depth = STACKLEAK_SEARCH_DEPTH / sizeof(unsigned long);

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

