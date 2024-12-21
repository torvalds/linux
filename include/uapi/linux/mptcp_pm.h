/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/mptcp_pm.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_MPTCP_PM_H
#define _UAPI_LINUX_MPTCP_PM_H

#define MPTCP_PM_NAME	"mptcp_pm"
#define MPTCP_PM_VER	1

/**
 * enum mptcp_event_type
 * @MPTCP_EVENT_UNSPEC: unused event
 * @MPTCP_EVENT_CREATED: token, family, saddr4 | saddr6, daddr4 | daddr6,
 *   sport, dport, server-side A new MPTCP connection has been created. It is
 *   the good time to allocate memory and send ADD_ADDR if needed. Depending on
 *   the traffic-patterns it can take a long time until the
 *   MPTCP_EVENT_ESTABLISHED is sent.
 * @MPTCP_EVENT_ESTABLISHED: token, family, saddr4 | saddr6, daddr4 | daddr6,
 *   sport, dport, server-side A MPTCP connection is established (can start new
 *   subflows).
 * @MPTCP_EVENT_CLOSED: token A MPTCP connection has stopped.
 * @MPTCP_EVENT_ANNOUNCED: token, rem_id, family, daddr4 | daddr6 [, dport] A
 *   new address has been announced by the peer.
 * @MPTCP_EVENT_REMOVED: token, rem_id An address has been lost by the peer.
 * @MPTCP_EVENT_SUB_ESTABLISHED: token, family, loc_id, rem_id, saddr4 |
 *   saddr6, daddr4 | daddr6, sport, dport, backup, if_idx [, error] A new
 *   subflow has been established. 'error' should not be set.
 * @MPTCP_EVENT_SUB_CLOSED: token, family, loc_id, rem_id, saddr4 | saddr6,
 *   daddr4 | daddr6, sport, dport, backup, if_idx [, error] A subflow has been
 *   closed. An error (copy of sk_err) could be set if an error has been
 *   detected for this subflow.
 * @MPTCP_EVENT_SUB_PRIORITY: token, family, loc_id, rem_id, saddr4 | saddr6,
 *   daddr4 | daddr6, sport, dport, backup, if_idx [, error] The priority of a
 *   subflow has changed. 'error' should not be set.
 * @MPTCP_EVENT_LISTENER_CREATED: family, sport, saddr4 | saddr6 A new PM
 *   listener is created.
 * @MPTCP_EVENT_LISTENER_CLOSED: family, sport, saddr4 | saddr6 A PM listener
 *   is closed.
 */
enum mptcp_event_type {
	MPTCP_EVENT_UNSPEC,
	MPTCP_EVENT_CREATED,
	MPTCP_EVENT_ESTABLISHED,
	MPTCP_EVENT_CLOSED,
	MPTCP_EVENT_ANNOUNCED = 6,
	MPTCP_EVENT_REMOVED,
	MPTCP_EVENT_SUB_ESTABLISHED = 10,
	MPTCP_EVENT_SUB_CLOSED,
	MPTCP_EVENT_SUB_PRIORITY = 13,
	MPTCP_EVENT_LISTENER_CREATED = 15,
	MPTCP_EVENT_LISTENER_CLOSED,
};

enum {
	MPTCP_PM_ADDR_ATTR_UNSPEC,
	MPTCP_PM_ADDR_ATTR_FAMILY,
	MPTCP_PM_ADDR_ATTR_ID,
	MPTCP_PM_ADDR_ATTR_ADDR4,
	MPTCP_PM_ADDR_ATTR_ADDR6,
	MPTCP_PM_ADDR_ATTR_PORT,
	MPTCP_PM_ADDR_ATTR_FLAGS,
	MPTCP_PM_ADDR_ATTR_IF_IDX,

	__MPTCP_PM_ADDR_ATTR_MAX
};
#define MPTCP_PM_ADDR_ATTR_MAX (__MPTCP_PM_ADDR_ATTR_MAX - 1)

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

enum {
	MPTCP_PM_ENDPOINT_ADDR = 1,

	__MPTCP_PM_ENDPOINT_MAX
};
#define MPTCP_PM_ENDPOINT_MAX (__MPTCP_PM_ENDPOINT_MAX - 1)

enum {
	MPTCP_PM_ATTR_UNSPEC,
	MPTCP_PM_ATTR_ADDR,
	MPTCP_PM_ATTR_RCV_ADD_ADDRS,
	MPTCP_PM_ATTR_SUBFLOWS,
	MPTCP_PM_ATTR_TOKEN,
	MPTCP_PM_ATTR_LOC_ID,
	MPTCP_PM_ATTR_ADDR_REMOTE,

	__MPTCP_ATTR_AFTER_LAST
};
#define MPTCP_PM_ATTR_MAX (__MPTCP_ATTR_AFTER_LAST - 1)

enum mptcp_event_attr {
	MPTCP_ATTR_UNSPEC,
	MPTCP_ATTR_TOKEN,
	MPTCP_ATTR_FAMILY,
	MPTCP_ATTR_LOC_ID,
	MPTCP_ATTR_REM_ID,
	MPTCP_ATTR_SADDR4,
	MPTCP_ATTR_SADDR6,
	MPTCP_ATTR_DADDR4,
	MPTCP_ATTR_DADDR6,
	MPTCP_ATTR_SPORT,
	MPTCP_ATTR_DPORT,
	MPTCP_ATTR_BACKUP,
	MPTCP_ATTR_ERROR,
	MPTCP_ATTR_FLAGS,
	MPTCP_ATTR_TIMEOUT,
	MPTCP_ATTR_IF_IDX,
	MPTCP_ATTR_RESET_REASON,
	MPTCP_ATTR_RESET_FLAGS,
	MPTCP_ATTR_SERVER_SIDE,

	__MPTCP_ATTR_MAX
};
#define MPTCP_ATTR_MAX (__MPTCP_ATTR_MAX - 1)

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
#define MPTCP_PM_CMD_MAX (__MPTCP_PM_CMD_AFTER_LAST - 1)

#endif /* _UAPI_LINUX_MPTCP_PM_H */
