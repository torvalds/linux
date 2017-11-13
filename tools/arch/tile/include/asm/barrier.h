/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_TILE_BARRIER_H
#define _TOOLS_LINUX_ASM_TILE_BARRIER_H
/*
 * FIXME: This came from tools/perf/perf-sys.h, where it was first introduced
 * in 620830b6954913647b7c7f68920cf48eddf6ad92, more work needed to make it
 * more closely follow the Linux kernel arch/tile/include/asm/barrier.h file.
 * Probably when we continue work on tools/ Kconfig support to have all the
 * CONFIG_ needed for properly doing that.
 */

#define mb()		asm volatile ("mf" ::: "memory")
#define wmb()		mb()
#define rmb()		mb()

#endif /* _TOOLS_LINUX_ASM_TILE_BARRIER_H */
