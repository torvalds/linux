/*
 * iSCSI User/Kernel Shares (Defines, Constants, Protocol definitions, etc)
 *
 * Copyright (C) 2005 Dmitry Yusupov
 * Copyright (C) 2005 Alex Aizman
 * maintained by open-iscsi@googlegroups.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 */

#ifndef ISCSI_IF_H
#define ISCSI_IF_H

#include <scsi/iscsi_proto.h>
#include <linux/in.h>
#include <linux/in6.h>

#define ISCSI_NL_GRP_ISCSID	1
#define ISCSI_NL_GRP_UIP	2

#define UEVENT_BASE			10
#define KEVENT_BASE			100
#define ISCSI_ERR_BASE			1000

enum iscsi_uevent_e {
	ISCSI_UEVENT_UNKNOWN		= 0,

	/* down events */
	ISCSI_UEVENT_CREATE_SESSION	= UEVENT_BASE + 1,
	ISCSI_UEVENT_DESTROY_SESSION	= UEVENT_BASE + 2,
	ISCSI_UEVENT_CREATE_CONN	= UEVENT_BASE + 3,
	ISCSI_UEVENT_DESTROY_CONN	= UEVENT_BASE + 4,
	ISCSI_UEVENT_BIND_CONN		= UEVENT_BASE + 5,
	ISCSI_UEVENT_SET_PARAM		= UEVENT_BASE + 6,
	ISCSI_UEVENT_START_CONN		= UEVENT_BASE + 7,
	ISCSI_UEVENT_STOP_CONN		= UEVENT_BASE + 8,
	ISCSI_UEVENT_SEND_PDU		= UEVENT_BASE + 9,
	ISCSI_UEVENT_GET_STATS		= UEVENT_BASE + 10,
	ISCSI_UEVENT_GET_PARAM		= UEVENT_BASE + 11,

	ISCSI_UEVENT_TRANSPORT_EP_CONNECT	= UEVENT_BASE + 12,
	ISCSI_UEVENT_TRANSPORT_EP_POLL		= UEVENT_BASE + 13,
	ISCSI_UEVENT_TRANSPORT_EP_DISCONNECT	= UEVENT_BASE + 14,

	ISCSI_UEVENT_TGT_DSCVR		= UEVENT_BASE + 15,
	ISCSI_UEVENT_SET_HOST_PARAM	= UEVENT_BASE + 16,
	ISCSI_UEVENT_UNBIND_SESSION	= UEVENT_BASE + 17,
	ISCSI_UEVENT_CREATE_BOUND_SESSION		= UEVENT_BASE + 18,
	ISCSI_UEVENT_TRANSPORT_EP_CONNECT_THROUGH_HOST	= UEVENT_BASE + 19,

	ISCSI_UEVENT_PATH_UPDATE	= UEVENT_BASE + 20,

	/* up events */
	ISCSI_KEVENT_RECV_PDU		= KEVENT_BASE + 1,
	ISCSI_KEVENT_CONN_ERROR		= KEVENT_BASE + 2,
	ISCSI_KEVENT_IF_ERROR		= KEVENT_BASE + 3,
	ISCSI_KEVENT_DESTROY_SESSION	= KEVENT_BASE + 4,
	ISCSI_KEVENT_UNBIND_SESSION	= KEVENT_BASE + 5,
	ISCSI_KEVENT_CREATE_SESSION	= KEVENT_BASE + 6,

	ISCSI_KEVENT_PATH_REQ		= KEVENT_BASE + 7,
	ISCSI_KEVENT_IF_DOWN		= KEVENT_BASE + 8,
};

enum iscsi_tgt_dscvr {
	ISCSI_TGT_DSCVR_SEND_TARGETS	= 1,
	ISCSI_TGT_DSCVR_ISNS		= 2,
	ISCSI_TGT_DSCVR_SLP		= 3,
};

struct iscsi_uevent {
	uint32_t type; /* k/u events type */
	uint32_t iferror; /* carries interface or resource errors */
	uint64_t transport_handle;

	union {
		/* messages u -> k */
		struct msg_create_session {
			uint32_t	initial_cmdsn;
			uint16_t	cmds_max;
			uint16_t	queue_depth;
		} c_session;
		struct msg_create_bound_session {
			uint64_t	ep_handle;
			uint32_t	initial_cmdsn;
			uint16_t	cmds_max;
			uint16_t	queue_depth;
		} c_bound_session;
		struct msg_destroy_session {
			uint32_t	sid;
		} d_session;
		struct msg_create_conn {
			uint32_t	sid;
			uint32_t	cid;
		} c_conn;
		struct msg_bind_conn {
			uint32_t	sid;
			uint32_t	cid;
			uint64_t	transport_eph;
			uint32_t	is_leading;
		} b_conn;
		struct msg_destroy_conn {
			uint32_t	sid;
			uint32_t	cid;
		} d_conn;
		struct msg_send_pdu {
			uint32_t	sid;
			uint32_t	cid;
			uint32_t	hdr_size;
			uint32_t	data_size;
		} send_pdu;
		struct msg_set_param {
			uint32_t	sid;
			uint32_t	cid;
			uint32_t	param; /* enum iscsi_param */
			uint32_t	len;
		} set_param;
		struct msg_start_conn {
			uint32_t	sid;
			uint32_t	cid;
		} start_conn;
		struct msg_stop_conn {
			uint32_t	sid;
			uint32_t	cid;
			uint64_t	conn_handle;
			uint32_t	flag;
		} stop_conn;
		struct msg_get_stats {
			uint32_t	sid;
			uint32_t	cid;
		} get_stats;
		struct msg_transport_connect {
			uint32_t	non_blocking;
		} ep_connect;
		struct msg_transport_connect_through_host {
			uint32_t	host_no;
			uint32_t	non_blocking;
		} ep_connect_through_host;
		struct msg_transport_poll {
			uint64_t	ep_handle;
			uint32_t	timeout_ms;
		} ep_poll;
		struct msg_transport_disconnect {
			uint64_t	ep_handle;
		} ep_disconnect;
		struct msg_tgt_dscvr {
			enum iscsi_tgt_dscvr	type;
			uint32_t	host_no;
			/*
 			 * enable = 1 to establish a new connection
			 * with the server. enable = 0 to disconnect
			 * from the server. Used primarily to switch
			 * from one iSNS server to another.
			 */
			uint32_t	enable;
		} tgt_dscvr;
		struct msg_set_host_param {
			uint32_t	host_no;
			uint32_t	param; /* enum iscsi_host_param */
			uint32_t	len;
		} set_host_param;
		struct msg_set_path {
			uint32_t	host_no;
		} set_path;
	} u;
	union {
		/* messages k -> u */
		int			retcode;
		struct msg_create_session_ret {
			uint32_t	sid;
			uint32_t	host_no;
		} c_session_ret;
		struct msg_create_conn_ret {
			uint32_t	sid;
			uint32_t	cid;
		} c_conn_ret;
		struct msg_unbind_session {
			uint32_t	sid;
			uint32_t	host_no;
		} unbind_session;
		struct msg_recv_req {
			uint32_t	sid;
			uint32_t	cid;
			uint64_t	recv_handle;
		} recv_req;
		struct msg_conn_error {
			uint32_t	sid;
			uint32_t	cid;
			uint32_t	error; /* enum iscsi_err */
		} connerror;
		struct msg_session_destroyed {
			uint32_t	host_no;
			uint32_t	sid;
		} d_session;
		struct msg_transport_connect_ret {
			uint64_t	handle;
		} ep_connect_ret;
		struct msg_req_path {
			uint32_t	host_no;
		} req_path;
		struct msg_notify_if_down {
			uint32_t	host_no;
		} notify_if_down;
	} r;
} __attribute__ ((aligned (sizeof(uint64_t))));

/*
 * To keep the struct iscsi_uevent size the same for userspace code
 * compatibility, the main structure for ISCSI_UEVENT_PATH_UPDATE and
 * ISCSI_KEVENT_PATH_REQ is defined separately and comes after the
 * struct iscsi_uevent in the NETLINK_ISCSI message.
 */
struct iscsi_path {
	uint64_t	handle;
	uint8_t		mac_addr[6];
	uint8_t		mac_addr_old[6];
	uint32_t	ip_addr_len;	/* 4 or 16 */
	union {
		struct in_addr	v4_addr;
		struct in6_addr	v6_addr;
	} src;
	union {
		struct in_addr	v4_addr;
		struct in6_addr	v6_addr;
	} dst;
	uint16_t	vlan_id;
	uint16_t	pmtu;
} __attribute__ ((aligned (sizeof(uint64_t))));

/*
 * Common error codes
 */
enum iscsi_err {
	ISCSI_OK			= 0,

	ISCSI_ERR_DATASN		= ISCSI_ERR_BASE + 1,
	ISCSI_ERR_DATA_OFFSET		= ISCSI_ERR_BASE + 2,
	ISCSI_ERR_MAX_CMDSN		= ISCSI_ERR_BASE + 3,
	ISCSI_ERR_EXP_CMDSN		= ISCSI_ERR_BASE + 4,
	ISCSI_ERR_BAD_OPCODE		= ISCSI_ERR_BASE + 5,
	ISCSI_ERR_DATALEN		= ISCSI_ERR_BASE + 6,
	ISCSI_ERR_AHSLEN		= ISCSI_ERR_BASE + 7,
	ISCSI_ERR_PROTO			= ISCSI_ERR_BASE + 8,
	ISCSI_ERR_LUN			= ISCSI_ERR_BASE + 9,
	ISCSI_ERR_BAD_ITT		= ISCSI_ERR_BASE + 10,
	ISCSI_ERR_CONN_FAILED		= ISCSI_ERR_BASE + 11,
	ISCSI_ERR_R2TSN			= ISCSI_ERR_BASE + 12,
	ISCSI_ERR_SESSION_FAILED	= ISCSI_ERR_BASE + 13,
	ISCSI_ERR_HDR_DGST		= ISCSI_ERR_BASE + 14,
	ISCSI_ERR_DATA_DGST		= ISCSI_ERR_BASE + 15,
	ISCSI_ERR_PARAM_NOT_FOUND	= ISCSI_ERR_BASE + 16,
	ISCSI_ERR_NO_SCSI_CMD		= ISCSI_ERR_BASE + 17,
	ISCSI_ERR_INVALID_HOST		= ISCSI_ERR_BASE + 18,
	ISCSI_ERR_XMIT_FAILED		= ISCSI_ERR_BASE + 19,
	ISCSI_ERR_TCP_CONN_CLOSE	= ISCSI_ERR_BASE + 20,
	ISCSI_ERR_SCSI_EH_SESSION_RST	= ISCSI_ERR_BASE + 21,
};

/*
 * iSCSI Parameters (RFC3720)
 */
enum iscsi_param {
	/* passed in using netlink set param */
	ISCSI_PARAM_MAX_RECV_DLENGTH,
	ISCSI_PARAM_MAX_XMIT_DLENGTH,
	ISCSI_PARAM_HDRDGST_EN,
	ISCSI_PARAM_DATADGST_EN,
	ISCSI_PARAM_INITIAL_R2T_EN,
	ISCSI_PARAM_MAX_R2T,
	ISCSI_PARAM_IMM_DATA_EN,
	ISCSI_PARAM_FIRST_BURST,
	ISCSI_PARAM_MAX_BURST,
	ISCSI_PARAM_PDU_INORDER_EN,
	ISCSI_PARAM_DATASEQ_INORDER_EN,
	ISCSI_PARAM_ERL,
	ISCSI_PARAM_IFMARKER_EN,
	ISCSI_PARAM_OFMARKER_EN,
	ISCSI_PARAM_EXP_STATSN,
	ISCSI_PARAM_TARGET_NAME,
	ISCSI_PARAM_TPGT,
	ISCSI_PARAM_PERSISTENT_ADDRESS,
	ISCSI_PARAM_PERSISTENT_PORT,
	ISCSI_PARAM_SESS_RECOVERY_TMO,

	/* passed in through bind conn using transport_fd */
	ISCSI_PARAM_CONN_PORT,
	ISCSI_PARAM_CONN_ADDRESS,

	ISCSI_PARAM_USERNAME,
	ISCSI_PARAM_USERNAME_IN,
	ISCSI_PARAM_PASSWORD,
	ISCSI_PARAM_PASSWORD_IN,

	ISCSI_PARAM_FAST_ABORT,
	ISCSI_PARAM_ABORT_TMO,
	ISCSI_PARAM_LU_RESET_TMO,
	ISCSI_PARAM_HOST_RESET_TMO,

	ISCSI_PARAM_PING_TMO,
	ISCSI_PARAM_RECV_TMO,

	ISCSI_PARAM_IFACE_NAME,
	ISCSI_PARAM_ISID,
	ISCSI_PARAM_INITIATOR_NAME,

	ISCSI_PARAM_TGT_RESET_TMO,
	ISCSI_PARAM_TARGET_ALIAS,
	/* must always be last */
	ISCSI_PARAM_MAX,
};

#define ISCSI_MAX_RECV_DLENGTH		(1ULL << ISCSI_PARAM_MAX_RECV_DLENGTH)
#define ISCSI_MAX_XMIT_DLENGTH		(1ULL << ISCSI_PARAM_MAX_XMIT_DLENGTH)
#define ISCSI_HDRDGST_EN		(1ULL << ISCSI_PARAM_HDRDGST_EN)
#define ISCSI_DATADGST_EN		(1ULL << ISCSI_PARAM_DATADGST_EN)
#define ISCSI_INITIAL_R2T_EN		(1ULL << ISCSI_PARAM_INITIAL_R2T_EN)
#define ISCSI_MAX_R2T			(1ULL << ISCSI_PARAM_MAX_R2T)
#define ISCSI_IMM_DATA_EN		(1ULL << ISCSI_PARAM_IMM_DATA_EN)
#define ISCSI_FIRST_BURST		(1ULL << ISCSI_PARAM_FIRST_BURST)
#define ISCSI_MAX_BURST			(1ULL << ISCSI_PARAM_MAX_BURST)
#define ISCSI_PDU_INORDER_EN		(1ULL << ISCSI_PARAM_PDU_INORDER_EN)
#define ISCSI_DATASEQ_INORDER_EN	(1ULL << ISCSI_PARAM_DATASEQ_INORDER_EN)
#define ISCSI_ERL			(1ULL << ISCSI_PARAM_ERL)
#define ISCSI_IFMARKER_EN		(1ULL << ISCSI_PARAM_IFMARKER_EN)
#define ISCSI_OFMARKER_EN		(1ULL << ISCSI_PARAM_OFMARKER_EN)
#define ISCSI_EXP_STATSN		(1ULL << ISCSI_PARAM_EXP_STATSN)
#define ISCSI_TARGET_NAME		(1ULL << ISCSI_PARAM_TARGET_NAME)
#define ISCSI_TPGT			(1ULL << ISCSI_PARAM_TPGT)
#define ISCSI_PERSISTENT_ADDRESS	(1ULL << ISCSI_PARAM_PERSISTENT_ADDRESS)
#define ISCSI_PERSISTENT_PORT		(1ULL << ISCSI_PARAM_PERSISTENT_PORT)
#define ISCSI_SESS_RECOVERY_TMO		(1ULL << ISCSI_PARAM_SESS_RECOVERY_TMO)
#define ISCSI_CONN_PORT			(1ULL << ISCSI_PARAM_CONN_PORT)
#define ISCSI_CONN_ADDRESS		(1ULL << ISCSI_PARAM_CONN_ADDRESS)
#define ISCSI_USERNAME			(1ULL << ISCSI_PARAM_USERNAME)
#define ISCSI_USERNAME_IN		(1ULL << ISCSI_PARAM_USERNAME_IN)
#define ISCSI_PASSWORD			(1ULL << ISCSI_PARAM_PASSWORD)
#define ISCSI_PASSWORD_IN		(1ULL << ISCSI_PARAM_PASSWORD_IN)
#define ISCSI_FAST_ABORT		(1ULL << ISCSI_PARAM_FAST_ABORT)
#define ISCSI_ABORT_TMO			(1ULL << ISCSI_PARAM_ABORT_TMO)
#define ISCSI_LU_RESET_TMO		(1ULL << ISCSI_PARAM_LU_RESET_TMO)
#define ISCSI_HOST_RESET_TMO		(1ULL << ISCSI_PARAM_HOST_RESET_TMO)
#define ISCSI_PING_TMO			(1ULL << ISCSI_PARAM_PING_TMO)
#define ISCSI_RECV_TMO			(1ULL << ISCSI_PARAM_RECV_TMO)
#define ISCSI_IFACE_NAME		(1ULL << ISCSI_PARAM_IFACE_NAME)
#define ISCSI_ISID			(1ULL << ISCSI_PARAM_ISID)
#define ISCSI_INITIATOR_NAME		(1ULL << ISCSI_PARAM_INITIATOR_NAME)
#define ISCSI_TGT_RESET_TMO		(1ULL << ISCSI_PARAM_TGT_RESET_TMO)
#define ISCSI_TARGET_ALIAS		(1ULL << ISCSI_PARAM_TARGET_ALIAS)

/* iSCSI HBA params */
enum iscsi_host_param {
	ISCSI_HOST_PARAM_HWADDRESS,
	ISCSI_HOST_PARAM_INITIATOR_NAME,
	ISCSI_HOST_PARAM_NETDEV_NAME,
	ISCSI_HOST_PARAM_IPADDRESS,
	ISCSI_HOST_PARAM_MAX,
};

#define ISCSI_HOST_HWADDRESS		(1ULL << ISCSI_HOST_PARAM_HWADDRESS)
#define ISCSI_HOST_INITIATOR_NAME	(1ULL << ISCSI_HOST_PARAM_INITIATOR_NAME)
#define ISCSI_HOST_NETDEV_NAME		(1ULL << ISCSI_HOST_PARAM_NETDEV_NAME)
#define ISCSI_HOST_IPADDRESS		(1ULL << ISCSI_HOST_PARAM_IPADDRESS)

#define iscsi_ptr(_handle) ((void*)(unsigned long)_handle)
#define iscsi_handle(_ptr) ((uint64_t)(unsigned long)_ptr)

/*
 * These flags presents iSCSI Data-Path capabilities.
 */
#define CAP_RECOVERY_L0		0x1
#define CAP_RECOVERY_L1		0x2
#define CAP_RECOVERY_L2		0x4
#define CAP_MULTI_R2T		0x8
#define CAP_HDRDGST		0x10
#define CAP_DATADGST		0x20
#define CAP_MULTI_CONN		0x40
#define CAP_TEXT_NEGO		0x80
#define CAP_MARKERS		0x100
#define CAP_FW_DB		0x200
#define CAP_SENDTARGETS_OFFLOAD	0x400	/* offload discovery process */
#define CAP_DATA_PATH_OFFLOAD	0x800	/* offload entire IO path */
#define CAP_DIGEST_OFFLOAD	0x1000	/* offload hdr and data digests */
#define CAP_PADDING_OFFLOAD	0x2000	/* offload padding insertion, removal,
					 and verification */

/*
 * These flags describes reason of stop_conn() call
 */
#define STOP_CONN_TERM		0x1
#define STOP_CONN_SUSPEND	0x2
#define STOP_CONN_RECOVER	0x3

#define ISCSI_STATS_CUSTOM_MAX		32
#define ISCSI_STATS_CUSTOM_DESC_MAX	64
struct iscsi_stats_custom {
	char desc[ISCSI_STATS_CUSTOM_DESC_MAX];
	uint64_t value;
};

/*
 * struct iscsi_stats - iSCSI Statistics (iSCSI MIB)
 *
 * Note: this structure contains counters collected on per-connection basis.
 */
struct iscsi_stats {
	/* octets */
	uint64_t txdata_octets;
	uint64_t rxdata_octets;

	/* xmit pdus */
	uint32_t noptx_pdus;
	uint32_t scsicmd_pdus;
	uint32_t tmfcmd_pdus;
	uint32_t login_pdus;
	uint32_t text_pdus;
	uint32_t dataout_pdus;
	uint32_t logout_pdus;
	uint32_t snack_pdus;

	/* recv pdus */
	uint32_t noprx_pdus;
	uint32_t scsirsp_pdus;
	uint32_t tmfrsp_pdus;
	uint32_t textrsp_pdus;
	uint32_t datain_pdus;
	uint32_t logoutrsp_pdus;
	uint32_t r2t_pdus;
	uint32_t async_pdus;
	uint32_t rjt_pdus;

	/* errors */
	uint32_t digest_err;
	uint32_t timeout_err;

	/*
	 * iSCSI Custom Statistics support, i.e. Transport could
	 * extend existing MIB statistics with its own specific statistics
	 * up to ISCSI_STATS_CUSTOM_MAX
	 */
	uint32_t custom_length;
	struct iscsi_stats_custom custom[0]
		__attribute__ ((aligned (sizeof(uint64_t))));
};

#endif
