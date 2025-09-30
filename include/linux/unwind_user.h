/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNWIND_USER_H
#define _LINUX_UNWIND_USER_H

#include <linux/unwind_user_types.h>
#include <asm/unwind_user.h>

#ifndef ARCH_INIT_USER_FP_FRAME
 #define ARCH_INIT_USER_FP_FRAME
#endif

int unwind_user(struct unwind_stacktrace *trace, unsigned int max_entries);

#endif /* _LINUX_UNWIND_USER_H */
