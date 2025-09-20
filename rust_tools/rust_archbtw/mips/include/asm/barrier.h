/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_MIPS_BARRIER_H
#define _TOOLS_LINUX_ASM_MIPS_BARRIER_H
/*
 * FIXME: This came from tools/perf/perf-sys.h, where it was first introduced
 * in c1e028ef40b8d6943b767028ba17d4f2ba020edb, more work needed to make it
 * more closely follow the Linux kernel arch/mips/include/asm/barrier.h file.
 * Probably when we continue work on tools/ Kconfig support to have all the
 * CONFIG_ needed for properly doing that.
 */
#define mb()		asm volatile(					\
				".set	mips2\n\t"			\
				"sync\n\t"				\
				".set	mips0"				\
				: /* no output */			\
				: /* no input */			\
				: "memory")
#define wmb()	mb()
#define rmb()	mb()

#endif /* _TOOLS_LINUX_ASM_MIPS_BARRIER_H */
