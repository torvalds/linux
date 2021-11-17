/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  IPv6 IOAM implementation
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#ifndef _UAPI_LINUX_IOAM6_H
#define _UAPI_LINUX_IOAM6_H

#include <asm/byteorder.h>
#include <linux/types.h>

#define IOAM6_U16_UNAVAILABLE U16_MAX
#define IOAM6_U32_UNAVAILABLE U32_MAX
#define IOAM6_U64_UNAVAILABLE U64_MAX

#define IOAM6_DEFAULT_ID (IOAM6_U32_UNAVAILABLE >> 8)
#define IOAM6_DEFAULT_ID_WIDE (IOAM6_U64_UNAVAILABLE >> 8)
#define IOAM6_DEFAULT_IF_ID IOAM6_U16_UNAVAILABLE
#define IOAM6_DEFAULT_IF_ID_WIDE IOAM6_U32_UNAVAILABLE

/*
 * IPv6 IOAM Option Header
 */
struct ioam6_hdr {
	__u8 opt_type;
	__u8 opt_len;
	__u8 :8;				/* reserved */
#define IOAM6_TYPE_PREALLOC 0
	__u8 type;
} __attribute__((packed));

/*
 * IOAM Trace Header
 */
struct ioam6_trace_hdr {
	__be16	namespace_id;

#if defined(__LITTLE_ENDIAN_BITFIELD)

	__u8	:1,				/* unused */
		:1,				/* unused */
		overflow:1,
		nodelen:5;

	__u8	remlen:7,
		:1;				/* unused */

	union {
		__be32 type_be32;

		struct {
			__u32	bit7:1,
				bit6:1,
				bit5:1,
				bit4:1,
				bit3:1,
				bit2:1,
				bit1:1,
				bit0:1,
				bit15:1,	/* unused */
				bit14:1,	/* unused */
				bit13:1,	/* unused */
				bit12:1,	/* unused */
				bit11:1,
				bit10:1,
				bit9:1,
				bit8:1,
				bit23:1,	/* reserved */
				bit22:1,
				bit21:1,	/* unused */
				bit20:1,	/* unused */
				bit19:1,	/* unused */
				bit18:1,	/* unused */
				bit17:1,	/* unused */
				bit16:1,	/* unused */
				:8;		/* reserved */
		} type;
	};

#elif defined(__BIG_ENDIAN_BITFIELD)

	__u8	nodelen:5,
		overflow:1,
		:1,				/* unused */
		:1;				/* unused */

	__u8	:1,				/* unused */
		remlen:7;

	union {
		__be32 type_be32;

		struct {
			__u32	bit0:1,
				bit1:1,
				bit2:1,
				bit3:1,
				bit4:1,
				bit5:1,
				bit6:1,
				bit7:1,
				bit8:1,
				bit9:1,
				bit10:1,
				bit11:1,
				bit12:1,	/* unused */
				bit13:1,	/* unused */
				bit14:1,	/* unused */
				bit15:1,	/* unused */
				bit16:1,	/* unused */
				bit17:1,	/* unused */
				bit18:1,	/* unused */
				bit19:1,	/* unused */
				bit20:1,	/* unused */
				bit21:1,	/* unused */
				bit22:1,
				bit23:1,	/* reserved */
				:8;		/* reserved */
		} type;
	};

#else
#error "Please fix <asm/byteorder.h>"
#endif

#define IOAM6_TRACE_DATA_SIZE_MAX 244
	__u8	data[0];
} __attribute__((packed));

#endif /* _UAPI_LINUX_IOAM6_H */
