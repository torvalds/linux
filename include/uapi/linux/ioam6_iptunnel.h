/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  IPv6 IOAM Lightweight Tunnel API
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#ifndef _UAPI_LINUX_IOAM6_IPTUNNEL_H
#define _UAPI_LINUX_IOAM6_IPTUNNEL_H

enum {
	IOAM6_IPTUNNEL_UNSPEC,
	IOAM6_IPTUNNEL_TRACE,		/* struct ioam6_trace_hdr */
	__IOAM6_IPTUNNEL_MAX,
};

#define IOAM6_IPTUNNEL_MAX (__IOAM6_IPTUNNEL_MAX - 1)

#endif /* _UAPI_LINUX_IOAM6_IPTUNNEL_H */
