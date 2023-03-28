/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* DO NOT USE in new code! This is solely for MEI due to legacy reasons */
/*
 * MEI UUID definition
 *
 * Copyright (C) 2010, Intel Corp.
 *	Huang Ying <ying.huang@intel.com>
 */

#ifndef _UAPI_LINUX_UUID_H_
#define _UAPI_LINUX_UUID_H_

#include <linux/types.h>

typedef struct {
	__u8 b[16];
} uuid_le;

#define UUID_LE(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)		\
((uuid_le)								\
{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
   (b) & 0xff, ((b) >> 8) & 0xff,					\
   (c) & 0xff, ((c) >> 8) & 0xff,					\
   (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

#define NULL_UUID_LE							\
	UUID_LE(0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00,	\
	     0x00, 0x00, 0x00, 0x00)

#endif /* _UAPI_LINUX_UUID_H_ */
