/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

/* Define states of a socket to tracking messages sending to and from the
 * socket.
 *
 * These states are based on rfc9293 with some modifications to support
 * tracking of messages sent out from a socket. For example, when a SYN is
 * received, a new socket is transiting to the SYN_RECV state defined in
 * rfc9293. But, we put it in SYN_RECV_SENDING_SYN_ACK state and when
 * SYN-ACK is sent out, it moves to SYN_RECV state. With this modification,
 * we can track the message sent out from a socket.
 */

#ifndef __CGROUP_TCP_SKB_H__
#define __CGROUP_TCP_SKB_H__

enum {
	INIT,
	CLOSED,
	SYN_SENT,
	SYN_RECV_SENDING_SYN_ACK,
	SYN_RECV,
	ESTABLISHED,
	FIN_WAIT1,
	FIN_WAIT2,
	CLOSE_WAIT_SENDING_ACK,
	CLOSE_WAIT,
	CLOSING,
	LAST_ACK,
	TIME_WAIT_SENDING_ACK,
	TIME_WAIT,
};

#endif /* __CGROUP_TCP_SKB_H__ */
