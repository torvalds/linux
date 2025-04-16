/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * if_addrlabel.h - netlink interface for address labels
 *
 * Copyright (C)2007 USAGI/WIDE Project,  All Rights Reserved.
 *
 * Authors:
 *	YOSHIFUJI Hideaki @ USAGI/WIDE <yoshfuji@linux-ipv6.org>
 */

#ifndef _UAPI__LINUX_IF_ADDRLABEL_H
#define _UAPI__LINUX_IF_ADDRLABEL_H

#include <linux/types.h>

struct ifaddrlblmsg {
	__u8		ifal_family;		/* Address family */
	__u8		__ifal_reserved;	/* Reserved */
	__u8		ifal_prefixlen;		/* Prefix length */
	__u8		ifal_flags;		/* Flags */
	__u32		ifal_index;		/* Link index */
	__u32		ifal_seq;		/* sequence number */
};

enum {
	IFAL_ADDRESS = 1,
	IFAL_LABEL = 2,
	__IFAL_MAX
};

#define IFAL_MAX	(__IFAL_MAX - 1)

#endif
