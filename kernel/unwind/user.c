// SPDX-License-Identifier: GPL-2.0
/*
* Generic interfaces for unwinding user space
*/
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/unwind_user.h>
#include <linux/uaccess.h>

static const struct unwind_user_frame fp_frame = {
	ARCH_INIT_USER_FP_FRAME
};

#define for_each_user_frame(state) \
	for (unwind_user_start(state); !(state)->done; unwind_user_next(state))

static int unwind_user_next_fp(struct unwind_user_state *state)
{
	const struct unwind_user_frame *frame = &fp_frame;
	unsigned long cfa, fp, ra;
	unsigned int shift;

	if (frame->use_fp) {
		if (state->fp < state->sp)
			return -EINVAL;
		cfa = state->fp;
	} else {
		cfa = state->sp;
	}

	/* Get the Canonical Frame Address (CFA) */
	cfa += frame->cfa_off;

	/* stack going in wrong direction? */
	if (cfa <= state->sp)
		return -EINVAL;

	/* Make sure that the address is word aligned */
	shift = sizeof(long) == 4 ? 2 : 3;
	if (cfa & ((1 << shift) - 1))
		return -EINVAL;

	/* Find the Return Address (RA) */
	if (get_user(ra, (unsigned long *)(cfa + frame->ra_off)))
		return -EINVAL;

	if (frame->fp_off && get_user(fp, (unsigned long __user *)(cfa + frame->fp_off)))
		return -EINVAL;

	state->ip = ra;
	state->sp = cfa;
	if (frame->fp_off)
		state->fp = fp;
	return 0;
}

static int unwind_user_next(struct unwind_user_state *state)
{
	unsigned long iter_mask = state->available_types;
	unsigned int bit;

	if (state->done)
		return -EINVAL;

	for_each_set_bit(bit, &iter_mask, NR_UNWIND_USER_TYPE_BITS) {
		enum unwind_user_type type = BIT(bit);

		state->current_type = type;
		switch (type) {
		case UNWIND_USER_TYPE_FP:
			if (!unwind_user_next_fp(state))
				return 0;
			continue;
		default:
			WARN_ONCE(1, "Undefined unwind bit %d", bit);
			break;
		}
		break;
	}

	/* No successful unwind method. */
	state->current_type = UNWIND_USER_TYPE_NONE;
	state->done = true;
	return -EINVAL;
}

static int unwind_user_start(struct unwind_user_state *state)
{
	struct pt_regs *regs = task_pt_regs(current);

	memset(state, 0, sizeof(*state));

	if ((current->flags & PF_KTHREAD) || !user_mode(regs)) {
		state->done = true;
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_HAVE_UNWIND_USER_FP))
		state->available_types |= UNWIND_USER_TYPE_FP;

	state->ip = instruction_pointer(regs);
	state->sp = user_stack_pointer(regs);
	state->fp = frame_pointer(regs);

	return 0;
}

int unwind_user(struct unwind_stacktrace *trace, unsigned int max_entries)
{
	struct unwind_user_state state;

	trace->nr = 0;

	if (!max_entries)
		return -EINVAL;

	if (current->flags & PF_KTHREAD)
		return 0;

	for_each_user_frame(&state) {
		trace->entries[trace->nr++] = state.ip;
		if (trace->nr >= max_entries)
			break;
	}

	return 0;
}
