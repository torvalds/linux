/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic arch dependent fprobe macros.
 */
#ifndef __ASM_GENERIC_FPROBE_H__
#define __ASM_GENERIC_FPROBE_H__

#include <linux/bits.h>

#ifdef CONFIG_64BIT
/*
 * Encoding the size and the address of fprobe into one 64bit entry.
 * The 32bit architectures should use 2 entries to store those info.
 */

#define ARCH_DEFINE_ENCODE_FPROBE_HEADER

#define FPROBE_HEADER_MSB_SIZE_SHIFT (BITS_PER_LONG - FPROBE_DATA_SIZE_BITS)
#define FPROBE_HEADER_MSB_MASK					\
	GENMASK(FPROBE_HEADER_MSB_SIZE_SHIFT - 1, 0)

/*
 * By default, this expects the MSBs in the address of kprobe is 0xf.
 * If any arch needs another fixed pattern (e.g. s390 is zero filled),
 * override this.
 */
#define FPROBE_HEADER_MSB_PATTERN				\
	GENMASK(BITS_PER_LONG - 1, FPROBE_HEADER_MSB_SIZE_SHIFT)

#define arch_fprobe_header_encodable(fp)			\
	(((unsigned long)(fp) & ~FPROBE_HEADER_MSB_MASK) ==	\
	 FPROBE_HEADER_MSB_PATTERN)

#define arch_encode_fprobe_header(fp, size)			\
	(((unsigned long)(fp) & FPROBE_HEADER_MSB_MASK) |	\
	 ((unsigned long)(size) << FPROBE_HEADER_MSB_SIZE_SHIFT))

#define arch_decode_fprobe_header_size(val)			\
	((unsigned long)(val) >> FPROBE_HEADER_MSB_SIZE_SHIFT)

#define arch_decode_fprobe_header_fp(val)					\
	((struct fprobe *)(((unsigned long)(val) & FPROBE_HEADER_MSB_MASK) |	\
			   FPROBE_HEADER_MSB_PATTERN))
#endif /* CONFIG_64BIT */

#endif /* __ASM_GENERIC_FPROBE_H__ */
