/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FRAME_H
#define _LINUX_FRAME_H

#ifdef CONFIG_STACK_VALIDATION
/*
 * This macro marks the given function's stack frame as "non-standard", which
 * tells objtool to ignore the function when doing stack metadata validation.
 * It should only be used in special cases where you're 100% sure it won't
 * affect the reliability of frame pointers and kernel stack traces.
 *
 * For more information, see tools/objtool/Documentation/stack-validation.txt.
 */
#define STACK_FRAME_NON_STANDARD(func) \
	static void __used __section(.discard.func_stack_frame_non_standard) \
		*__func_stack_frame_non_standard_##func = func

#else /* !CONFIG_STACK_VALIDATION */

#define STACK_FRAME_NON_STANDARD(func)

#endif /* CONFIG_STACK_VALIDATION */

#endif /* _LINUX_FRAME_H */
