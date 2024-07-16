/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  IPv6 RPL-SR implementation
 *
 *  Author:
 *  (C) 2020 Alexander Aring <alex.aring@gmail.com>
 */

#ifndef _UAPI_LINUX_RPL_H
#define _UAPI_LINUX_RPL_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/in6.h>

/*
 * RPL SR Header
 */
struct ipv6_rpl_sr_hdr {
	__u8	nexthdr;
	__u8	hdrlen;
	__u8	type;
	__u8	segments_left;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u32	cmpre:4,
		cmpri:4,
		reserved:4,
		pad:4,
		reserved1:16;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u32	cmpri:4,
		cmpre:4,
		pad:4,
		reserved:20;
#else
#error  "Please fix <asm/byteorder.h>"
#endif

	union {
		struct in6_addr addr[0];
		__u8 data[0];
	} segments;
} __attribute__((packed));

#define rpl_segaddr	segments.addr
#define rpl_segdata	segments.data

#endif
