/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_MPTCP_H
#define _UAPI_MPTCP_H

#include <linux/const.h>
#include <linux/types.h>

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

enum {
	MPTCP_PM_CMD_UNSPEC,

	MPTCP_PM_CMD_ADD_ADDR,
	MPTCP_PM_CMD_DEL_ADDR,
	MPTCP_PM_CMD_GET_ADDR,
	MPTCP_PM_CMD_FLUSH_ADDRS,
	MPTCP_PM_CMD_SET_LIMITS,
	MPTCP_PM_CMD_GET_LIMITS,
	MPTCP_PM_CMD_SET_FLAGS,

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
 * MPTCP_EVENT_SUB_ESTABLISHED: token, family, saddr4 | saddr6,
 *                              daddr4 | daddr6, sport, dport, backup,
 *                              if_idx [, error]
 * A new subflow has been established. 'error' should not be set.
 *
 * MPTCP_EVENT_SUB_CLOSED: token, family, saddr4 | saddr6, daddr4 | daddr6,
 *                         sport, dport, backup, if_idx [, error]
 * A subflow has been closed. An error (copy of sk_err) could be set if an
 * error has been detected for this subflow.
 *
 * MPTCP_EVENT_SUB_PRIORITY: token, family, saddr4 | saddr6, daddr4 | daddr6,
 *                           sport, dport, backup, if_idx [, error]
 *       The priority of a subflow has changed. 'error' should not be set.
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

#endif /* _UAPI_MPTCP_H */
