/*
 * Copyright (c) 2008 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _LINUX_RDS_H
#define _LINUX_RDS_H

#include <linux/types.h>
#include <linux/socket.h>		/* For __kernel_sockaddr_storage. */

#define RDS_IB_ABI_VERSION		0x301

#define	SOL_RDS		276

/*
 * setsockopt/getsockopt for SOL_RDS
 */
#define RDS_CANCEL_SENT_TO      	1
#define RDS_GET_MR			2
#define RDS_FREE_MR			3
/* deprecated: RDS_BARRIER 4 */
#define RDS_RECVERR			5
#define RDS_CONG_MONITOR		6
#define RDS_GET_MR_FOR_DEST		7
#define SO_RDS_TRANSPORT		8

/* supported values for SO_RDS_TRANSPORT */
#define	RDS_TRANS_IB	0
#define	RDS_TRANS_IWARP	1
#define	RDS_TRANS_TCP	2
#define RDS_TRANS_COUNT	3
#define	RDS_TRANS_NONE	(~0)

/*
 * Control message types for SOL_RDS.
 *
 * CMSG_RDMA_ARGS (sendmsg)
 *	Request a RDMA transfer to/from the specified
 *	memory ranges.
 *	The cmsg_data is a struct rds_rdma_args.
 * RDS_CMSG_RDMA_DEST (recvmsg, sendmsg)
 *	Kernel informs application about intended
 *	source/destination of a RDMA transfer
 * RDS_CMSG_RDMA_MAP (sendmsg)
 *	Application asks kernel to map the given
 *	memory range into a IB MR, and send the
 *	R_Key along in an RDS extension header.
 *	The cmsg_data is a struct rds_get_mr_args,
 *	the same as for the GET_MR setsockopt.
 * RDS_CMSG_RDMA_STATUS (recvmsg)
 *	Returns the status of a completed RDMA operation.
 */
#define RDS_CMSG_RDMA_ARGS		1
#define RDS_CMSG_RDMA_DEST		2
#define RDS_CMSG_RDMA_MAP		3
#define RDS_CMSG_RDMA_STATUS		4
#define RDS_CMSG_CONG_UPDATE		5
#define RDS_CMSG_ATOMIC_FADD		6
#define RDS_CMSG_ATOMIC_CSWP		7
#define RDS_CMSG_MASKED_ATOMIC_FADD	8
#define RDS_CMSG_MASKED_ATOMIC_CSWP	9

#define RDS_INFO_FIRST			10000
#define RDS_INFO_COUNTERS		10000
#define RDS_INFO_CONNECTIONS		10001
/* 10002 aka RDS_INFO_FLOWS is deprecated */
#define RDS_INFO_SEND_MESSAGES		10003
#define RDS_INFO_RETRANS_MESSAGES       10004
#define RDS_INFO_RECV_MESSAGES          10005
#define RDS_INFO_SOCKETS                10006
#define RDS_INFO_TCP_SOCKETS            10007
#define RDS_INFO_IB_CONNECTIONS		10008
#define RDS_INFO_CONNECTION_STATS	10009
#define RDS_INFO_IWARP_CONNECTIONS	10010
#define RDS_INFO_LAST			10010

struct rds_info_counter {
	uint8_t	name[32];
	uint64_t	value;
} __attribute__((packed));

#define RDS_INFO_CONNECTION_FLAG_SENDING	0x01
#define RDS_INFO_CONNECTION_FLAG_CONNECTING	0x02
#define RDS_INFO_CONNECTION_FLAG_CONNECTED	0x04

#define TRANSNAMSIZ	16

struct rds_info_connection {
	uint64_t	next_tx_seq;
	uint64_t	next_rx_seq;
	__be32		laddr;
	__be32		faddr;
	uint8_t	transport[TRANSNAMSIZ];		/* null term ascii */
	uint8_t	flags;
} __attribute__((packed));

#define RDS_INFO_MESSAGE_FLAG_ACK               0x01
#define RDS_INFO_MESSAGE_FLAG_FAST_ACK          0x02

struct rds_info_message {
	uint64_t	seq;
	uint32_t	len;
	__be32		laddr;
	__be32		faddr;
	__be16		lport;
	__be16		fport;
	uint8_t	flags;
} __attribute__((packed));

struct rds_info_socket {
	uint32_t	sndbuf;
	__be32		bound_addr;
	__be32		connected_addr;
	__be16		bound_port;
	__be16		connected_port;
	uint32_t	rcvbuf;
	uint64_t	inum;
} __attribute__((packed));

struct rds_info_tcp_socket {
	__be32          local_addr;
	__be16          local_port;
	__be32          peer_addr;
	__be16          peer_port;
	uint64_t       hdr_rem;
	uint64_t       data_rem;
	uint32_t       last_sent_nxt;
	uint32_t       last_expected_una;
	uint32_t       last_seen_una;
} __attribute__((packed));

#define RDS_IB_GID_LEN	16
struct rds_info_rdma_connection {
	__be32		src_addr;
	__be32		dst_addr;
	uint8_t		src_gid[RDS_IB_GID_LEN];
	uint8_t		dst_gid[RDS_IB_GID_LEN];

	uint32_t	max_send_wr;
	uint32_t	max_recv_wr;
	uint32_t	max_send_sge;
	uint32_t	rdma_mr_max;
	uint32_t	rdma_mr_size;
};

/*
 * Congestion monitoring.
 * Congestion control in RDS happens at the host connection
 * level by exchanging a bitmap marking congested ports.
 * By default, a process sleeping in poll() is always woken
 * up when the congestion map is updated.
 * With explicit monitoring, an application can have more
 * fine-grained control.
 * The application installs a 64bit mask value in the socket,
 * where each bit corresponds to a group of ports.
 * When a congestion update arrives, RDS checks the set of
 * ports that are now uncongested against the list bit mask
 * installed in the socket, and if they overlap, we queue a
 * cong_notification on the socket.
 *
 * To install the congestion monitor bitmask, use RDS_CONG_MONITOR
 * with the 64bit mask.
 * Congestion updates are received via RDS_CMSG_CONG_UPDATE
 * control messages.
 *
 * The correspondence between bits and ports is
 *	1 << (portnum % 64)
 */
#define RDS_CONG_MONITOR_SIZE	64
#define RDS_CONG_MONITOR_BIT(port)  (((unsigned int) port) % RDS_CONG_MONITOR_SIZE)
#define RDS_CONG_MONITOR_MASK(port) (1ULL << RDS_CONG_MONITOR_BIT(port))

/*
 * RDMA related types
 */

/*
 * This encapsulates a remote memory location.
 * In the current implementation, it contains the R_Key
 * of the remote memory region, and the offset into it
 * (so that the application does not have to worry about
 * alignment).
 */
typedef uint64_t	rds_rdma_cookie_t;

struct rds_iovec {
	uint64_t	addr;
	uint64_t	bytes;
};

struct rds_get_mr_args {
	struct rds_iovec vec;
	uint64_t	cookie_addr;
	uint64_t	flags;
};

struct rds_get_mr_for_dest_args {
	struct __kernel_sockaddr_storage dest_addr;
	struct rds_iovec 	vec;
	uint64_t		cookie_addr;
	uint64_t		flags;
};

struct rds_free_mr_args {
	rds_rdma_cookie_t cookie;
	uint64_t	flags;
};

struct rds_rdma_args {
	rds_rdma_cookie_t cookie;
	struct rds_iovec remote_vec;
	uint64_t	local_vec_addr;
	uint64_t	nr_local;
	uint64_t	flags;
	uint64_t	user_token;
};

struct rds_atomic_args {
	rds_rdma_cookie_t cookie;
	uint64_t 	local_addr;
	uint64_t 	remote_addr;
	union {
		struct {
			uint64_t	compare;
			uint64_t	swap;
		} cswp;
		struct {
			uint64_t	add;
		} fadd;
		struct {
			uint64_t	compare;
			uint64_t	swap;
			uint64_t	compare_mask;
			uint64_t	swap_mask;
		} m_cswp;
		struct {
			uint64_t	add;
			uint64_t	nocarry_mask;
		} m_fadd;
	};
	uint64_t	flags;
	uint64_t	user_token;
};

struct rds_rdma_notify {
	uint64_t	user_token;
	int32_t		status;
};

#define RDS_RDMA_SUCCESS	0
#define RDS_RDMA_REMOTE_ERROR	1
#define RDS_RDMA_CANCELED	2
#define RDS_RDMA_DROPPED	3
#define RDS_RDMA_OTHER_ERROR	4

/*
 * Common set of flags for all RDMA related structs
 */
#define RDS_RDMA_READWRITE	0x0001
#define RDS_RDMA_FENCE		0x0002	/* use FENCE for immediate send */
#define RDS_RDMA_INVALIDATE	0x0004	/* invalidate R_Key after freeing MR */
#define RDS_RDMA_USE_ONCE	0x0008	/* free MR after use */
#define RDS_RDMA_DONTWAIT	0x0010	/* Don't wait in SET_BARRIER */
#define RDS_RDMA_NOTIFY_ME	0x0020	/* Notify when operation completes */
#define RDS_RDMA_SILENT		0x0040	/* Do not interrupt remote */

#endif /* IB_RDS_H */
