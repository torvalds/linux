/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ONCE_H
#define _LINUX_ONCE_H

#include <linux/types.h>
#include <linux/jump_label.h>

bool __do_once_start(bool *done, unsigned long *flags);
void __do_once_done(bool *done, struct static_key *once_key,
		    unsigned long *flags);

/* Call a function exactly once. The idea of DO_ONCE() is to perform
 * a function call such as initialization of random seeds, etc, only
 * once, where DO_ONCE() can live in the fast-path. After @func has
 * been called with the passed arguments, the static key will patch
 * out the condition into a nop. DO_ONCE() guarantees type safety of
 * arguments!
 *
 * Not that the following is not equivalent ...
 *
 *   DO_ONCE(func, arg);
 *   DO_ONCE(func, arg);
 *
 * ... to this version:
 *
 *   void foo(void)
 *   {
 *     DO_ONCE(func, arg);
 *   }
 *
 *   foo();
 *   foo();
 *
 * In case the one-time invocation could be triggered from multiple
 * places, then a common helper function must be defined, so that only
 * a single static key will be placed there!
 */
#define DO_ONCE(func, ...)						     \
	({								     \
		bool ___ret = false;					     \
		static bool ___done = false;				     \
		static struct static_key ___once_key = STATIC_KEY_INIT_TRUE; \
		if (static_key_true(&___once_key)) {			     \
			unsigned long ___flags;				     \
			___ret = __do_once_start(&___done, &___flags);	     \
			if (unlikely(___ret)) {				     \
				func(__VA_ARGS__);			     \
				__do_once_done(&___done, &___once_key,	     \
					       &___flags);		     \
			}						     \
		}							     \
		___ret;							     \
	})

#define get_random_once(buf, nbytes)					     \
	DO_ONCE(get_random_bytes, (buf), (nbytes))
#define get_random_once_wait(buf, nbytes)                                    \
	DO_ONCE(get_random_bytes_wait, (buf), (nbytes))                      \

#endif /* _LINUX_ONCE_H */
