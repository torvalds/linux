/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_RANDOMIZE_KSTACK_H
#define _LINUX_RANDOMIZE_KSTACK_H

#ifdef CONFIG_RANDOMIZE_KSTACK_OFFSET
#include <linux/kernel.h>
#include <linux/jump_label.h>
#include <linux/percpu-defs.h>
#include <linux/prandom.h>

DECLARE_STATIC_KEY_MAYBE(CONFIG_RANDOMIZE_KSTACK_OFFSET_DEFAULT,
			 randomize_kstack_offset);

/*
 * Do not use this anywhere else in the kernel. This is used here because
 * it provides an arch-agnostic way to grow the stack with correct
 * alignment. Also, since this use is being explicitly masked to a max of
 * 10 bits, stack-clash style attacks are unlikely. For more details see
 * "VLAs" in Documentation/process/deprecated.rst
 *
 * The normal __builtin_alloca() is initialized with INIT_STACK_ALL (currently
 * only with Clang and not GCC). Initializing the unused area on each syscall
 * entry is expensive, and generating an implicit call to memset() may also be
 * problematic (such as in noinstr functions). Therefore, if the compiler
 * supports it (which it should if it initializes allocas), always use the
 * "uninitialized" variant of the builtin.
 */
#if __has_builtin(__builtin_alloca_uninitialized)
#define __kstack_alloca __builtin_alloca_uninitialized
#else
#define __kstack_alloca __builtin_alloca
#endif

/*
 * Use, at most, 6 bits of entropy (on 64-bit; 8 on 32-bit). This cap is
 * to keep the "VLA" from being unbounded (see above). Additionally clear
 * the bottom 4 bits (on 64-bit systems, 2 for 32-bit), since stack
 * alignment will always be at least word size. This makes the compiler
 * code gen better when it is applying the actual per-arch alignment to
 * the final offset. The resulting randomness is reasonable without overly
 * constraining usable stack space.
 */
#ifdef CONFIG_64BIT
#define KSTACK_OFFSET_MAX(x)	((x) & 0b1111110000)
#else
#define KSTACK_OFFSET_MAX(x)	((x) & 0b1111111100)
#endif

DECLARE_PER_CPU(struct rnd_state, kstack_rnd_state);

static __always_inline u32 get_kstack_offset(void)
{
	struct rnd_state *state;
	u32 rnd;

	state = &get_cpu_var(kstack_rnd_state);
	rnd = prandom_u32_state(state);
	put_cpu_var(kstack_rnd_state);

	return rnd;
}

/**
 * add_random_kstack_offset - Increase stack utilization by a random offset.
 *
 * This should be used in the syscall entry path after user registers have been
 * stored to the stack. Preemption may be enabled. For testing the resulting
 * entropy, please see: tools/testing/selftests/lkdtm/stack-entropy.sh
 */
#define add_random_kstack_offset() do {					\
	if (static_branch_maybe(CONFIG_RANDOMIZE_KSTACK_OFFSET_DEFAULT,	\
				&randomize_kstack_offset)) {		\
		u32 offset = get_kstack_offset();			\
		u8 *ptr = __kstack_alloca(KSTACK_OFFSET_MAX(offset));	\
		/* Keep allocation even after "ptr" loses scope. */	\
		asm volatile("" :: "r"(ptr) : "memory");		\
	}								\
} while (0)

#else /* CONFIG_RANDOMIZE_KSTACK_OFFSET */
#define add_random_kstack_offset()		do { } while (0)
#endif /* CONFIG_RANDOMIZE_KSTACK_OFFSET */

#endif
