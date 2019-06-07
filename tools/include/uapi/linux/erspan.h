/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ERSPAN Tunnel Metadata
 *
 * Copyright (c) 2018 VMware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * Userspace API for metadata mode ERSPAN tunnel
 */
#ifndef _UAPI_ERSPAN_H
#define _UAPI_ERSPAN_H

#include <linux/types.h>	/* For __beXX in userspace */
#include <asm/byteorder.h>

/* ERSPAN version 2 metadata header */
struct erspan_md2 {
	__be32 timestamp;
	__be16 sgt;	/* security group tag */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	hwid_upper:2,
		ft:5,
		p:1;
	__u8	o:1,
		gra:2,
		dir:1,
		hwid:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8	p:1,
		ft:5,
		hwid_upper:2;
	__u8	hwid:4,
		dir:1,
		gra:2,
		o:1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
};

struct erspan_metadata {
	int version;
	union {
		__be32 index;		/* Version 1 (type II)*/
		struct erspan_md2 md2;	/* Version 2 (type III) */
	} u;
};

#endif /* _UAPI_ERSPAN_H */
