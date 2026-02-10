/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNWIND_USER_H
#define _LINUX_UNWIND_USER_H

#include <linux/unwind_user_types.h>
#include <asm/unwind_user.h>

#ifndef CONFIG_HAVE_UNWIND_USER_FP

#define ARCH_INIT_USER_FP_FRAME(ws)

#endif

#ifndef ARCH_INIT_USER_FP_ENTRY_FRAME
#define ARCH_INIT_USER_FP_ENTRY_FRAME(ws)
#endif

#ifndef unwind_user_at_function_start
static inline bool unwind_user_at_function_start(struct pt_regs *regs)
{
	return false;
}
#define unwind_user_at_function_start unwind_user_at_function_start
#endif

int unwind_user(struct unwind_stacktrace *trace, unsigned int max_entries);

#endif /* _LINUX_UNWIND_USER_H */
