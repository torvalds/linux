/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol sk_state field.
 */
#ifndef _LINUX_TCP_STATES_H
#define _LINUX_TCP_STATES_H

enum {
	TCP_ESTABLISHED = 1,
	TCP_SYN_SENT,
	TCP_SYN_RECV,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_TIME_WAIT,
	TCP_CLOSE,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_LISTEN,
	TCP_CLOSING,	/* Now a valid state */
	TCP_NEW_SYN_RECV,

	TCP_MAX_STATES	/* Leave at the end! */
};

#define TCP_STATE_MASK	0xF

#define TCP_ACTION_FIN	(1 << TCP_CLOSE)

enum {
	TCPF_ESTABLISHED = (1 << TCP_ESTABLISHED),
	TCPF_SYN_SENT	 = (1 << TCP_SYN_SENT),
	TCPF_SYN_RECV	 = (1 << TCP_SYN_RECV),
	TCPF_FIN_WAIT1	 = (1 << TCP_FIN_WAIT1),
	TCPF_FIN_WAIT2	 = (1 << TCP_FIN_WAIT2),
	TCPF_TIME_WAIT	 = (1 << TCP_TIME_WAIT),
	TCPF_CLOSE	 = (1 << TCP_CLOSE),
	TCPF_CLOSE_WAIT	 = (1 << TCP_CLOSE_WAIT),
	TCPF_LAST_ACK	 = (1 << TCP_LAST_ACK),
	TCPF_LISTEN	 = (1 << TCP_LISTEN),
	TCPF_CLOSING	 = (1 << TCP_CLOSING),
	TCPF_NEW_SYN_RECV = (1 << TCP_NEW_SYN_RECV),
};

#endif	/* _LINUX_TCP_STATES_H */
