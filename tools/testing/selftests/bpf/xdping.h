/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved. */

#define	XDPING_MAX_COUNT	10
#define	XDPING_DEFAULT_COUNT	4

struct pinginfo {
	__u64	start;
	__be16	seq;
	__u16	count;
	__u32	pad;
	__u64	times[XDPING_MAX_COUNT];
};
