/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SYNC_CORE_H
#define _LINUX_SYNC_CORE_H

#ifdef CONFIG_ARCH_HAS_SYNC_CORE_BEFORE_USERMODE
#include <asm/sync_core.h>
#else
/*
 * This is a dummy sync_core_before_usermode() implementation that can be used
 * on all architectures which return to user-space through core serializing
 * instructions.
 * If your architecture returns to user-space through non-core-serializing
 * instructions, you need to write your own functions.
 */
static inline void sync_core_before_usermode(void)
{
}
#endif

#ifdef CONFIG_ARCH_HAS_PREPARE_SYNC_CORE_CMD
#include <asm/sync_core.h>
#else
/*
 * This is a dummy prepare_sync_core_cmd() implementation that can be used on
 * all architectures which provide unconditional core serializing instructions
 * in switch_mm().
 * If your architecture doesn't provide such core serializing instructions in
 * switch_mm(), you may need to write your own functions.
 */
static inline void prepare_sync_core_cmd(struct mm_struct *mm)
{
}
#endif

#endif /* _LINUX_SYNC_CORE_H */
