/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_RISCV_HWPROBE_H
#define SELFTEST_RISCV_HWPROBE_H
#include <stddef.h>
#include <asm/hwprobe.h>

#if __BYTE_ORDER == __BIG_ENDIAN
# define le32_bswap(_x)				\
	((((_x) & 0x000000ffU) << 24) |		\
	 (((_x) & 0x0000ff00U) <<  8) |		\
	 (((_x) & 0x00ff0000U) >>  8) |		\
	 (((_x) & 0xff000000U) >> 24))
#else
# define le32_bswap(_x) (_x)
#endif

/*
 * Rather than relying on having a new enough libc to define this, just do it
 * ourselves.  This way we don't need to be coupled to a new-enough libc to
 * contain the call.
 */
long riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
		   size_t cpusetsize, unsigned long *cpus, unsigned int flags);

#endif
