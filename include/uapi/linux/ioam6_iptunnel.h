/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  IPv6 IOAM Lightweight Tunnel API
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#ifndef _UAPI_LINUX_IOAM6_IPTUNNEL_H
#define _UAPI_LINUX_IOAM6_IPTUNNEL_H

/* Encap modes:
 *  - inline: direct insertion
 *  - encap: ip6ip6 encapsulation
 *  - auto: inline for local packets, encap for in-transit packets
 */
enum {
	__IOAM6_IPTUNNEL_MODE_MIN,

	IOAM6_IPTUNNEL_MODE_INLINE,
	IOAM6_IPTUNNEL_MODE_ENCAP,
	IOAM6_IPTUNNEL_MODE_AUTO,

	__IOAM6_IPTUNNEL_MODE_MAX,
};

#define IOAM6_IPTUNNEL_MODE_MIN (__IOAM6_IPTUNNEL_MODE_MIN + 1)
#define IOAM6_IPTUNNEL_MODE_MAX (__IOAM6_IPTUNNEL_MODE_MAX - 1)

enum {
	IOAM6_IPTUNNEL_UNSPEC,

	/* Encap mode */
	IOAM6_IPTUNNEL_MODE,		/* u8 */

	/* Tunnel dst address.
	 * For encap,auto modes.
	 */
	IOAM6_IPTUNNEL_DST,		/* struct in6_addr */

	/* IOAM Trace Header */
	IOAM6_IPTUNNEL_TRACE,		/* struct ioam6_trace_hdr */

	/* Insertion frequency:
	 * "k over n" packets (0 < k <= n)
	 * [0.0001% ... 100%]
	 */
#define IOAM6_IPTUNNEL_FREQ_MIN 1
#define IOAM6_IPTUNNEL_FREQ_MAX 1000000
	IOAM6_IPTUNNEL_FREQ_K,		/* u32 */
	IOAM6_IPTUNNEL_FREQ_N,		/* u32 */

	__IOAM6_IPTUNNEL_MAX,
};

#define IOAM6_IPTUNNEL_MAX (__IOAM6_IPTUNNEL_MAX - 1)

#endif /* _UAPI_LINUX_IOAM6_IPTUNNEL_H */
