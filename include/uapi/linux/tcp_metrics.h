/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* tcp_metrics.h - TCP Metrics Interface */

#ifndef _LINUX_TCP_METRICS_H
#define _LINUX_TCP_METRICS_H

#include <linux/types.h>

/* NETLINK_GENERIC related info
 */
#define TCP_METRICS_GENL_NAME		"tcp_metrics"
#define TCP_METRICS_GENL_VERSION	0x1

enum tcp_metric_index {
	TCP_METRIC_RTT,		/* in ms units */
	TCP_METRIC_RTTVAR,	/* in ms units */
	TCP_METRIC_SSTHRESH,
	TCP_METRIC_CWND,
	TCP_METRIC_REORDERING,

	TCP_METRIC_RTT_US,	/* in usec units */
	TCP_METRIC_RTTVAR_US,	/* in usec units */

	/* Always last.  */
	__TCP_METRIC_MAX,
};

#define TCP_METRIC_MAX	(__TCP_METRIC_MAX - 1)

enum {
	TCP_METRICS_ATTR_UNSPEC,
	TCP_METRICS_ATTR_ADDR_IPV4,		/* u32 */
	TCP_METRICS_ATTR_ADDR_IPV6,		/* binary */
	TCP_METRICS_ATTR_AGE,			/* msecs */
	TCP_METRICS_ATTR_TW_TSVAL,		/* u32, raw, rcv tsval */
	TCP_METRICS_ATTR_TW_TS_STAMP,		/* s32, sec age */
	TCP_METRICS_ATTR_VALS,			/* nested +1, u32 */
	TCP_METRICS_ATTR_FOPEN_MSS,		/* u16 */
	TCP_METRICS_ATTR_FOPEN_SYN_DROPS,	/* u16, count of drops */
	TCP_METRICS_ATTR_FOPEN_SYN_DROP_TS,	/* msecs age */
	TCP_METRICS_ATTR_FOPEN_COOKIE,		/* binary */
	TCP_METRICS_ATTR_SADDR_IPV4,		/* u32 */
	TCP_METRICS_ATTR_SADDR_IPV6,		/* binary */
	TCP_METRICS_ATTR_PAD,

	__TCP_METRICS_ATTR_MAX,
};

#define TCP_METRICS_ATTR_MAX	(__TCP_METRICS_ATTR_MAX - 1)

enum {
	TCP_METRICS_CMD_UNSPEC,
	TCP_METRICS_CMD_GET,
	TCP_METRICS_CMD_DEL,

	__TCP_METRICS_CMD_MAX,
};

#define TCP_METRICS_CMD_MAX	(__TCP_METRICS_CMD_MAX - 1)

#endif /* _LINUX_TCP_METRICS_H */
