/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace API for hardware time stamping of network packets
 *
 * Copyright (C) 2008,2009 Intel Corporation
 * Author: Patrick Ohly <patrick.ohly@intel.com>
 *
 */

#ifndef _NET_TIMESTAMPING_H
#define _NET_TIMESTAMPING_H

#include <linux/types.h>
#include <linux/socket.h>   /* for SO_TIMESTAMPING */

/* SO_TIMESTAMPING gets an integer bit field comprised of these values */
enum {
	SOF_TIMESTAMPING_TX_HARDWARE = (1<<0),
	SOF_TIMESTAMPING_TX_SOFTWARE = (1<<1),
	SOF_TIMESTAMPING_RX_HARDWARE = (1<<2),
	SOF_TIMESTAMPING_RX_SOFTWARE = (1<<3),
	SOF_TIMESTAMPING_SOFTWARE = (1<<4),
	SOF_TIMESTAMPING_SYS_HARDWARE = (1<<5),
	SOF_TIMESTAMPING_RAW_HARDWARE = (1<<6),
	SOF_TIMESTAMPING_OPT_ID = (1<<7),
	SOF_TIMESTAMPING_TX_SCHED = (1<<8),
	SOF_TIMESTAMPING_TX_ACK = (1<<9),
	SOF_TIMESTAMPING_OPT_CMSG = (1<<10),
	SOF_TIMESTAMPING_OPT_TSONLY = (1<<11),
	SOF_TIMESTAMPING_OPT_STATS = (1<<12),
	SOF_TIMESTAMPING_OPT_PKTINFO = (1<<13),
	SOF_TIMESTAMPING_OPT_TX_SWHW = (1<<14),

	SOF_TIMESTAMPING_LAST = SOF_TIMESTAMPING_OPT_TX_SWHW,
	SOF_TIMESTAMPING_MASK = (SOF_TIMESTAMPING_LAST - 1) |
				 SOF_TIMESTAMPING_LAST
};

/*
 * SO_TIMESTAMPING flags are either for recording a packet timestamp or for
 * reporting the timestamp to user space.
 * Recording flags can be set both via socket options and control messages.
 */
#define SOF_TIMESTAMPING_TX_RECORD_MASK	(SOF_TIMESTAMPING_TX_HARDWARE | \
					 SOF_TIMESTAMPING_TX_SOFTWARE | \
					 SOF_TIMESTAMPING_TX_SCHED | \
					 SOF_TIMESTAMPING_TX_ACK)

/**
 * struct hwtstamp_config - %SIOCGHWTSTAMP and %SIOCSHWTSTAMP parameter
 *
 * @flags:	no flags defined right now, must be zero for %SIOCSHWTSTAMP
 * @tx_type:	one of HWTSTAMP_TX_*
 * @rx_filter:	one of HWTSTAMP_FILTER_*
 *
 * %SIOCGHWTSTAMP and %SIOCSHWTSTAMP expect a &struct ifreq with a
 * ifr_data pointer to this structure.  For %SIOCSHWTSTAMP, if the
 * driver or hardware does not support the requested @rx_filter value,
 * the driver may use a more general filter mode.  In this case
 * @rx_filter will indicate the actual mode on return.
 */
struct hwtstamp_config {
	int flags;
	int tx_type;
	int rx_filter;
};

/* possible values for hwtstamp_config->tx_type */
enum hwtstamp_tx_types {
	/*
	 * No outgoing packet will need hardware time stamping;
	 * should a packet arrive which asks for it, no hardware
	 * time stamping will be done.
	 */
	HWTSTAMP_TX_OFF,

	/*
	 * Enables hardware time stamping for outgoing packets;
	 * the sender of the packet decides which are to be
	 * time stamped by setting %SOF_TIMESTAMPING_TX_SOFTWARE
	 * before sending the packet.
	 */
	HWTSTAMP_TX_ON,

	/*
	 * Enables time stamping for outgoing packets just as
	 * HWTSTAMP_TX_ON does, but also enables time stamp insertion
	 * directly into Sync packets. In this case, transmitted Sync
	 * packets will not received a time stamp via the socket error
	 * queue.
	 */
	HWTSTAMP_TX_ONESTEP_SYNC,
};

/* possible values for hwtstamp_config->rx_filter */
enum hwtstamp_rx_filters {
	/* time stamp no incoming packet at all */
	HWTSTAMP_FILTER_NONE,

	/* time stamp any incoming packet */
	HWTSTAMP_FILTER_ALL,

	/* return value: time stamp all packets requested plus some others */
	HWTSTAMP_FILTER_SOME,

	/* PTP v1, UDP, any kind of event packet */
	HWTSTAMP_FILTER_PTP_V1_L4_EVENT,
	/* PTP v1, UDP, Sync packet */
	HWTSTAMP_FILTER_PTP_V1_L4_SYNC,
	/* PTP v1, UDP, Delay_req packet */
	HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ,
	/* PTP v2, UDP, any kind of event packet */
	HWTSTAMP_FILTER_PTP_V2_L4_EVENT,
	/* PTP v2, UDP, Sync packet */
	HWTSTAMP_FILTER_PTP_V2_L4_SYNC,
	/* PTP v2, UDP, Delay_req packet */
	HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ,

	/* 802.AS1, Ethernet, any kind of event packet */
	HWTSTAMP_FILTER_PTP_V2_L2_EVENT,
	/* 802.AS1, Ethernet, Sync packet */
	HWTSTAMP_FILTER_PTP_V2_L2_SYNC,
	/* 802.AS1, Ethernet, Delay_req packet */
	HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ,

	/* PTP v2/802.AS1, any layer, any kind of event packet */
	HWTSTAMP_FILTER_PTP_V2_EVENT,
	/* PTP v2/802.AS1, any layer, Sync packet */
	HWTSTAMP_FILTER_PTP_V2_SYNC,
	/* PTP v2/802.AS1, any layer, Delay_req packet */
	HWTSTAMP_FILTER_PTP_V2_DELAY_REQ,

	/* NTP, UDP, all versions and packet modes */
	HWTSTAMP_FILTER_NTP_ALL,
};

/* SCM_TIMESTAMPING_PKTINFO control message */
struct scm_ts_pktinfo {
	__u32 if_index;
	__u32 pkt_length;
	__u32 reserved[2];
};

/*
 * SO_TXTIME gets a struct sock_txtime with flags being an integer bit
 * field comprised of these values.
 */
enum txtime_flags {
	SOF_TXTIME_DEADLINE_MODE = (1 << 0),
	SOF_TXTIME_REPORT_ERRORS = (1 << 1),

	SOF_TXTIME_FLAGS_LAST = SOF_TXTIME_REPORT_ERRORS,
	SOF_TXTIME_FLAGS_MASK = (SOF_TXTIME_FLAGS_LAST - 1) |
				 SOF_TXTIME_FLAGS_LAST
};

struct sock_txtime {
	__kernel_clockid_t	clockid;/* reference clockid */
	__u32			flags;	/* as defined by enum txtime_flags */
};

#endif /* _NET_TIMESTAMPING_H */
