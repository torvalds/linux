/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_MPTCP_H
#define _UAPI_MPTCP_H

#ifndef __KERNEL__
#include <netinet/in.h>		/* for sockaddr_in and sockaddr_in6	*/
#include <sys/socket.h>		/* for struct sockaddr			*/
#endif

#include <linux/const.h>
#include <linux/types.h>
#include <linux/in.h>		/* for sockaddr_in			*/
#include <linux/in6.h>		/* for sockaddr_in6			*/
#include <linux/socket.h>	/* for sockaddr_storage and sa_family	*/

#define MPTCP_SUBFLOW_FLAG_MCAP_REM		_BITUL(0)
#define MPTCP_SUBFLOW_FLAG_MCAP_LOC		_BITUL(1)
#define MPTCP_SUBFLOW_FLAG_JOIN_REM		_BITUL(2)
#define MPTCP_SUBFLOW_FLAG_JOIN_LOC		_BITUL(3)
#define MPTCP_SUBFLOW_FLAG_BKUP_REM		_BITUL(4)
#define MPTCP_SUBFLOW_FLAG_BKUP_LOC		_BITUL(5)
#define MPTCP_SUBFLOW_FLAG_FULLY_ESTABLISHED	_BITUL(6)
#define MPTCP_SUBFLOW_FLAG_CONNECTED		_BITUL(7)
#define MPTCP_SUBFLOW_FLAG_MAPVALID		_BITUL(8)

enum {
	MPTCP_SUBFLOW_ATTR_UNSPEC,
	MPTCP_SUBFLOW_ATTR_TOKEN_REM,
	MPTCP_SUBFLOW_ATTR_TOKEN_LOC,
	MPTCP_SUBFLOW_ATTR_RELWRITE_SEQ,
	MPTCP_SUBFLOW_ATTR_MAP_SEQ,
	MPTCP_SUBFLOW_ATTR_MAP_SFSEQ,
	MPTCP_SUBFLOW_ATTR_SSN_OFFSET,
	MPTCP_SUBFLOW_ATTR_MAP_DATALEN,
	MPTCP_SUBFLOW_ATTR_FLAGS,
	MPTCP_SUBFLOW_ATTR_ID_REM,
	MPTCP_SUBFLOW_ATTR_ID_LOC,
	MPTCP_SUBFLOW_ATTR_PAD,
	__MPTCP_SUBFLOW_ATTR_MAX
};

#define MPTCP_SUBFLOW_ATTR_MAX (__MPTCP_SUBFLOW_ATTR_MAX - 1)

/* netlink interface */
#define MPTCP_PM_NAME		"mptcp_pm"
#define MPTCP_PM_CMD_GRP_NAME	"mptcp_pm_cmds"
#define MPTCP_PM_EV_GRP_NAME	"mptcp_pm_events"
#define MPTCP_PM_VER		0x1

/*
 * ATTR types defined for MPTCP
 */
enum {
	MPTCP_PM_ATTR_UNSPEC,

	MPTCP_PM_ATTR_ADDR,				/* nested address */
	MPTCP_PM_ATTR_RCV_ADD_ADDRS,			/* u32 */
	MPTCP_PM_ATTR_SUBFLOWS,				/* u32 */
	MPTCP_PM_ATTR_TOKEN,				/* u32 */
	MPTCP_PM_ATTR_LOC_ID,				/* u8 */
	MPTCP_PM_ATTR_ADDR_REMOTE,			/* nested address */

	__MPTCP_PM_ATTR_MAX
};

#define MPTCP_PM_ATTR_MAX (__MPTCP_PM_ATTR_MAX - 1)

enum {
	MPTCP_PM_ADDR_ATTR_UNSPEC,

	MPTCP_PM_ADDR_ATTR_FAMILY,			/* u16 */
	MPTCP_PM_ADDR_ATTR_ID,				/* u8 */
	MPTCP_PM_ADDR_ATTR_ADDR4,			/* struct in_addr */
	MPTCP_PM_ADDR_ATTR_ADDR6,			/* struct in6_addr */
	MPTCP_PM_ADDR_ATTR_PORT,			/* u16 */
	MPTCP_PM_ADDR_ATTR_FLAGS,			/* u32 */
	MPTCP_PM_ADDR_ATTR_IF_IDX,			/* s32 */

	__MPTCP_PM_ADDR_ATTR_MAX
};

#define MPTCP_PM_ADDR_ATTR_MAX (__MPTCP_PM_ADDR_ATTR_MAX - 1)

#define MPTCP_PM_ADDR_FLAG_SIGNAL			(1 << 0)
#define MPTCP_PM_ADDR_FLAG_SUBFLOW			(1 << 1)
#define MPTCP_PM_ADDR_FLAG_BACKUP			(1 << 2)
#define MPTCP_PM_ADDR_FLAG_FULLMESH			(1 << 3)
#define MPTCP_PM_ADDR_FLAG_IMPLICIT			(1 << 4)

enum {
	MPTCP_PM_CMD_UNSPEC,

	MPTCP_PM_CMD_ADD_ADDR,
	MPTCP_PM_CMD_DEL_ADDR,
	MPTCP_PM_CMD_GET_ADDR,
	MPTCP_PM_CMD_FLUSH_ADDRS,
	MPTCP_PM_CMD_SET_LIMITS,
	MPTCP_PM_CMD_GET_LIMITS,
	MPTCP_PM_CMD_SET_FLAGS,
	MPTCP_PM_CMD_ANNOUNCE,
	MPTCP_PM_CMD_REMOVE,
	MPTCP_PM_CMD_SUBFLOW_CREATE,
	MPTCP_PM_CMD_SUBFLOW_DESTROY,

	__MPTCP_PM_CMD_AFTER_LAST
};

#define MPTCP_INFO_FLAG_FALLBACK		_BITUL(0)
#define MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED	_BITUL(1)

struct mptcp_info {
	__u8	mptcpi_subflows;
	__u8	mptcpi_add_addr_signal;
	__u8	mptcpi_add_addr_accepted;
	__u8	mptcpi_subflows_max;
	__u8	mptcpi_add_addr_signal_max;
	__u8	mptcpi_add_addr_accepted_max;
	__u32	mptcpi_flags;
	__u32	mptcpi_token;
	__u64	mptcpi_write_seq;
	__u64	mptcpi_snd_una;
	__u64	mptcpi_rcv_nxt;
	__u8	mptcpi_local_addr_used;
	__u8	mptcpi_local_addr_max;
	__u8	mptcpi_csum_enabled;
	__u32	mptcpi_retransmits;
	__u64	mptcpi_bytes_retrans;
	__u64	mptcpi_bytes_sent;
	__u64	mptcpi_bytes_received;
	__u64	mptcpi_bytes_acked;
};

/*
 * MPTCP_EVENT_CREATED: token, family, saddr4 | saddr6, daddr4 | daddr6,
 *                      sport, dport
 * A new MPTCP connection has been created. It is the good time to allocate
 * memory and send ADD_ADDR if needed. Depending on the traffic-patterns
 * it can take a long time until the MPTCP_EVENT_ESTABLISHED is sent.
 *
 * MPTCP_EVENT_ESTABLISHED: token, family, saddr4 | saddr6, daddr4 | daddr6,
 *			    sport, dport
 * A MPTCP connection is established (can start new subflows).
 *
 * MPTCP_EVENT_CLOSED: token
 * A MPTCP connection has stopped.
 *
 * MPTCP_EVENT_ANNOUNCED: token, rem_id, family, daddr4 | daddr6 [, dport]
 * A new address has been announced by the peer.
 *
 * MPTCP_EVENT_REMOVED: token, rem_id
 * An address has been lost by the peer.
 *
 * MPTCP_EVENT_SUB_ESTABLISHED: token, family, loc_id, rem_id,
 *                              saddr4 | saddr6, daddr4 | daddr6, sport,
 *                              dport, backup, if_idx [, error]
 * A new subflow has been established. 'error' should not be set.
 *
 * MPTCP_EVENT_SUB_CLOSED: token, family, loc_id, rem_id, saddr4 | saddr6,
 *                         daddr4 | daddr6, sport, dport, backup, if_idx
 *                         [, error]
 * A subflow has been closed. An error (copy of sk_err) could be set if an
 * error has been detected for this subflow.
 *
 * MPTCP_EVENT_SUB_PRIORITY: token, family, loc_id, rem_id, saddr4 | saddr6,
 *                           daddr4 | daddr6, sport, dport, backup, if_idx
 *                           [, error]
 * The priority of a subflow has changed. 'error' should not be set.
 *
 * MPTCP_EVENT_LISTENER_CREATED: family, sport, saddr4 | saddr6
 * A new PM listener is created.
 *
 * MPTCP_EVENT_LISTENER_CLOSED: family, sport, saddr4 | saddr6
 * A PM listener is closed.
 */
enum mptcp_event_type {
	MPTCP_EVENT_UNSPEC = 0,
	MPTCP_EVENT_CREATED = 1,
	MPTCP_EVENT_ESTABLISHED = 2,
	MPTCP_EVENT_CLOSED = 3,

	MPTCP_EVENT_ANNOUNCED = 6,
	MPTCP_EVENT_REMOVED = 7,

	MPTCP_EVENT_SUB_ESTABLISHED = 10,
	MPTCP_EVENT_SUB_CLOSED = 11,

	MPTCP_EVENT_SUB_PRIORITY = 13,

	MPTCP_EVENT_LISTENER_CREATED = 15,
	MPTCP_EVENT_LISTENER_CLOSED = 16,
};

enum mptcp_event_attr {
	MPTCP_ATTR_UNSPEC = 0,

	MPTCP_ATTR_TOKEN,	/* u32 */
	MPTCP_ATTR_FAMILY,	/* u16 */
	MPTCP_ATTR_LOC_ID,	/* u8 */
	MPTCP_ATTR_REM_ID,	/* u8 */
	MPTCP_ATTR_SADDR4,	/* be32 */
	MPTCP_ATTR_SADDR6,	/* struct in6_addr */
	MPTCP_ATTR_DADDR4,	/* be32 */
	MPTCP_ATTR_DADDR6,	/* struct in6_addr */
	MPTCP_ATTR_SPORT,	/* be16 */
	MPTCP_ATTR_DPORT,	/* be16 */
	MPTCP_ATTR_BACKUP,	/* u8 */
	MPTCP_ATTR_ERROR,	/* u8 */
	MPTCP_ATTR_FLAGS,	/* u16 */
	MPTCP_ATTR_TIMEOUT,	/* u32 */
	MPTCP_ATTR_IF_IDX,	/* s32 */
	MPTCP_ATTR_RESET_REASON,/* u32 */
	MPTCP_ATTR_RESET_FLAGS, /* u32 */
	MPTCP_ATTR_SERVER_SIDE,	/* u8 */

	__MPTCP_ATTR_AFTER_LAST
};

#define MPTCP_ATTR_MAX (__MPTCP_ATTR_AFTER_LAST - 1)

/* MPTCP Reset reason codes, rfc8684 */
#define MPTCP_RST_EUNSPEC	0
#define MPTCP_RST_EMPTCP	1
#define MPTCP_RST_ERESOURCE	2
#define MPTCP_RST_EPROHIBIT	3
#define MPTCP_RST_EWQ2BIG	4
#define MPTCP_RST_EBADPERF	5
#define MPTCP_RST_EMIDDLEBOX	6

struct mptcp_subflow_data {
	__u32		size_subflow_data;		/* size of this structure in userspace */
	__u32		num_subflows;			/* must be 0, set by kernel */
	__u32		size_kernel;			/* must be 0, set by kernel */
	__u32		size_user;			/* size of one element in data[] */
} __attribute__((aligned(8)));

struct mptcp_subflow_addrs {
	union {
		__kernel_sa_family_t sa_family;
		struct sockaddr sa_local;
		struct sockaddr_in sin_local;
		struct sockaddr_in6 sin6_local;
		struct __kernel_sockaddr_storage ss_local;
	};
	union {
		struct sockaddr sa_remote;
		struct sockaddr_in sin_remote;
		struct sockaddr_in6 sin6_remote;
		struct __kernel_sockaddr_storage ss_remote;
	};
};

struct mptcp_subflow_info {
	__u32				id;
	struct mptcp_subflow_addrs	addrs;
};

struct mptcp_full_info {
	__u32		size_tcpinfo_kernel;	/* must be 0, set by kernel */
	__u32		size_tcpinfo_user;
	__u32		size_sfinfo_kernel;	/* must be 0, set by kernel */
	__u32		size_sfinfo_user;
	__u32		num_subflows;		/* must be 0, set by kernel (real subflow count) */
	__u32		size_arrays_user;	/* max subflows that userspace is interested in;
						 * the buffers at subflow_info/tcp_info
						 * are respectively at least:
						 *  size_arrays * size_sfinfo_user
						 *  size_arrays * size_tcpinfo_user
						 * bytes wide
						 */
	__aligned_u64		subflow_info;
	__aligned_u64		tcp_info;
	struct mptcp_info	mptcp_info;
};

/* MPTCP socket options */
#define MPTCP_INFO		1
#define MPTCP_TCPINFO		2
#define MPTCP_SUBFLOW_ADDRS	3
#define MPTCP_FULL_INFO		4

#endif /* _UAPI_MPTCP_H */
