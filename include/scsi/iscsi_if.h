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
	ISCSI_UEVENT_SET_IFACE_PARAMS	= UEVENT_BASE + 21,
	ISCSI_UEVENT_PING		= UEVENT_BASE + 22,
	ISCSI_UEVENT_GET_CHAP		= UEVENT_BASE + 23,
	ISCSI_UEVENT_DELETE_CHAP	= UEVENT_BASE + 24,
	ISCSI_UEVENT_SET_FLASHNODE_PARAMS	= UEVENT_BASE + 25,
	ISCSI_UEVENT_NEW_FLASHNODE	= UEVENT_BASE + 26,
	ISCSI_UEVENT_DEL_FLASHNODE	= UEVENT_BASE + 27,
	ISCSI_UEVENT_LOGIN_FLASHNODE	= UEVENT_BASE + 28,
	ISCSI_UEVENT_LOGOUT_FLASHNODE	= UEVENT_BASE + 29,
	ISCSI_UEVENT_LOGOUT_FLASHNODE_SID	= UEVENT_BASE + 30,
	ISCSI_UEVENT_SET_CHAP		= UEVENT_BASE + 31,
	ISCSI_UEVENT_GET_HOST_STATS	= UEVENT_BASE + 32,

	/* up events */
	ISCSI_KEVENT_RECV_PDU		= KEVENT_BASE + 1,
	ISCSI_KEVENT_CONN_ERROR		= KEVENT_BASE + 2,
	ISCSI_KEVENT_IF_ERROR		= KEVENT_BASE + 3,
	ISCSI_KEVENT_DESTROY_SESSION	= KEVENT_BASE + 4,
	ISCSI_KEVENT_UNBIND_SESSION	= KEVENT_BASE + 5,
	ISCSI_KEVENT_CREATE_SESSION	= KEVENT_BASE + 6,

	ISCSI_KEVENT_PATH_REQ		= KEVENT_BASE + 7,
	ISCSI_KEVENT_IF_DOWN		= KEVENT_BASE + 8,
	ISCSI_KEVENT_CONN_LOGIN_STATE   = KEVENT_BASE + 9,
	ISCSI_KEVENT_HOST_EVENT		= KEVENT_BASE + 10,
	ISCSI_KEVENT_PING_COMP		= KEVENT_BASE + 11,
};

enum iscsi_tgt_dscvr {
	ISCSI_TGT_DSCVR_SEND_TARGETS	= 1,
	ISCSI_TGT_DSCVR_ISNS		= 2,
	ISCSI_TGT_DSCVR_SLP		= 3,
};

enum iscsi_host_event_code {
	ISCSI_EVENT_LINKUP		= 1,
	ISCSI_EVENT_LINKDOWN,
	/* must always be last */
	ISCSI_EVENT_MAX,
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
		struct msg_set_iface_params {
			uint32_t	host_no;
			uint32_t	count;
		} set_iface_params;
		struct msg_iscsi_ping {
			uint32_t        host_no;
			uint32_t        iface_num;
			uint32_t        iface_type;
			uint32_t        payload_size;
			uint32_t	pid;	/* unique ping id associated
						   with each ping request */
		} iscsi_ping;
		struct msg_get_chap {
			uint32_t	host_no;
			uint32_t	num_entries; /* number of CHAP entries
						      * on request, number of
						      * valid CHAP entries on
						      * response */
			uint16_t	chap_tbl_idx;
		} get_chap;
		struct msg_delete_chap {
		       uint32_t        host_no;
		       uint16_t        chap_tbl_idx;
		} delete_chap;
		struct msg_set_flashnode_param {
			uint32_t	host_no;
			uint32_t	flashnode_idx;
			uint32_t	count;
		} set_flashnode;
		struct msg_new_flashnode {
			uint32_t	host_no;
			uint32_t	len;
		} new_flashnode;
		struct msg_del_flashnode {
			uint32_t	host_no;
			uint32_t	flashnode_idx;
		} del_flashnode;
		struct msg_login_flashnode {
			uint32_t	host_no;
			uint32_t	flashnode_idx;
		} login_flashnode;
		struct msg_logout_flashnode {
			uint32_t	host_no;
			uint32_t	flashnode_idx;
		} logout_flashnode;
		struct msg_logout_flashnode_sid {
			uint32_t	host_no;
			uint32_t	sid;
		} logout_flashnode_sid;
		struct msg_get_host_stats {
			uint32_t host_no;
		} get_host_stats;
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
		struct msg_conn_login {
			uint32_t        sid;
			uint32_t        cid;
			uint32_t        state; /* enum iscsi_conn_state */
		} conn_login;
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
		struct msg_host_event {
			uint32_t	host_no;
			uint32_t	data_size;
			enum iscsi_host_event_code code;
		} host_event;
		struct msg_ping_comp {
			uint32_t        host_no;
			uint32_t        status; /* enum
						 * iscsi_ping_status_code */
			uint32_t	pid;	/* unique ping id associated
						   with each ping request */
			uint32_t        data_size;
		} ping_comp;
		struct msg_new_flashnode_ret {
			uint32_t	flashnode_idx;
		} new_flashnode_ret;
	} r;
} __attribute__ ((aligned (sizeof(uint64_t))));

enum iscsi_param_type {
	ISCSI_PARAM,		/* iscsi_param (session, conn, target, LU) */
	ISCSI_HOST_PARAM,	/* iscsi_host_param */
	ISCSI_NET_PARAM,	/* iscsi_net_param */
	ISCSI_FLASHNODE_PARAM,	/* iscsi_flashnode_param */
	ISCSI_CHAP_PARAM,	/* iscsi_chap_param */
	ISCSI_IFACE_PARAM,	/* iscsi_iface_param */
};

/* structure for minimalist usecase */
struct iscsi_param_info {
	uint32_t len;		/* Actual length of the param value */
	uint16_t param;		/* iscsi param */
	uint8_t value[0];	/* length sized value follows */
} __packed;

struct iscsi_iface_param_info {
	uint32_t iface_num;	/* iface number, 0 - n */
	uint32_t len;		/* Actual length of the param */
	uint16_t param;		/* iscsi param value */
	uint8_t iface_type;	/* IPv4 or IPv6 */
	uint8_t param_type;	/* iscsi_param_type */
	uint8_t value[0];	/* length sized value follows */
} __packed;

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

/* iscsi iface enabled/disabled setting */
#define ISCSI_IFACE_DISABLE	0x01
#define ISCSI_IFACE_ENABLE	0x02

/* ipv4 bootproto */
#define ISCSI_BOOTPROTO_STATIC		0x01
#define ISCSI_BOOTPROTO_DHCP		0x02

/* ipv6 addr autoconfig type */
#define ISCSI_IPV6_AUTOCFG_DISABLE		0x01
#define ISCSI_IPV6_AUTOCFG_ND_ENABLE		0x02
#define ISCSI_IPV6_AUTOCFG_DHCPV6_ENABLE	0x03

/* ipv6 link local addr type */
#define ISCSI_IPV6_LINKLOCAL_AUTOCFG_ENABLE	0x01
#define ISCSI_IPV6_LINKLOCAL_AUTOCFG_DISABLE	0x02

/* ipv6 router addr type */
#define ISCSI_IPV6_ROUTER_AUTOCFG_ENABLE	0x01
#define ISCSI_IPV6_ROUTER_AUTOCFG_DISABLE	0x02

#define ISCSI_IFACE_TYPE_IPV4		0x01
#define ISCSI_IFACE_TYPE_IPV6		0x02

#define ISCSI_MAX_VLAN_ID		4095
#define ISCSI_MAX_VLAN_PRIORITY		7

/* iscsi vlan enable/disabled setting */
#define ISCSI_VLAN_DISABLE	0x01
#define ISCSI_VLAN_ENABLE	0x02

/* iscsi generic enable/disabled setting for various features */
#define ISCSI_NET_PARAM_DISABLE		0x01
#define ISCSI_NET_PARAM_ENABLE		0x02

/* iSCSI network params */
enum iscsi_net_param {
	ISCSI_NET_PARAM_IPV4_ADDR		= 1,
	ISCSI_NET_PARAM_IPV4_SUBNET,
	ISCSI_NET_PARAM_IPV4_GW,
	ISCSI_NET_PARAM_IPV4_BOOTPROTO,
	ISCSI_NET_PARAM_MAC,
	ISCSI_NET_PARAM_IPV6_LINKLOCAL,
	ISCSI_NET_PARAM_IPV6_ADDR,
	ISCSI_NET_PARAM_IPV6_ROUTER,
	ISCSI_NET_PARAM_IPV6_ADDR_AUTOCFG,
	ISCSI_NET_PARAM_IPV6_LINKLOCAL_AUTOCFG,
	ISCSI_NET_PARAM_IPV6_ROUTER_AUTOCFG,
	ISCSI_NET_PARAM_IFACE_ENABLE,
	ISCSI_NET_PARAM_VLAN_ID,
	ISCSI_NET_PARAM_VLAN_PRIORITY,
	ISCSI_NET_PARAM_VLAN_ENABLED,
	ISCSI_NET_PARAM_VLAN_TAG,
	ISCSI_NET_PARAM_IFACE_TYPE,
	ISCSI_NET_PARAM_IFACE_NAME,
	ISCSI_NET_PARAM_MTU,
	ISCSI_NET_PARAM_PORT,
	ISCSI_NET_PARAM_IPADDR_STATE,
	ISCSI_NET_PARAM_IPV6_LINKLOCAL_STATE,
	ISCSI_NET_PARAM_IPV6_ROUTER_STATE,
	ISCSI_NET_PARAM_DELAYED_ACK_EN,
	ISCSI_NET_PARAM_TCP_NAGLE_DISABLE,
	ISCSI_NET_PARAM_TCP_WSF_DISABLE,
	ISCSI_NET_PARAM_TCP_WSF,
	ISCSI_NET_PARAM_TCP_TIMER_SCALE,
	ISCSI_NET_PARAM_TCP_TIMESTAMP_EN,
	ISCSI_NET_PARAM_CACHE_ID,
	ISCSI_NET_PARAM_IPV4_DHCP_DNS_ADDR_EN,
	ISCSI_NET_PARAM_IPV4_DHCP_SLP_DA_EN,
	ISCSI_NET_PARAM_IPV4_TOS_EN,
	ISCSI_NET_PARAM_IPV4_TOS,
	ISCSI_NET_PARAM_IPV4_GRAT_ARP_EN,
	ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID_EN,
	ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID,
	ISCSI_NET_PARAM_IPV4_DHCP_REQ_VENDOR_ID_EN,
	ISCSI_NET_PARAM_IPV4_DHCP_USE_VENDOR_ID_EN,
	ISCSI_NET_PARAM_IPV4_DHCP_VENDOR_ID,
	ISCSI_NET_PARAM_IPV4_DHCP_LEARN_IQN_EN,
	ISCSI_NET_PARAM_IPV4_FRAGMENT_DISABLE,
	ISCSI_NET_PARAM_IPV4_IN_FORWARD_EN,
	ISCSI_NET_PARAM_IPV4_TTL,
	ISCSI_NET_PARAM_IPV6_GRAT_NEIGHBOR_ADV_EN,
	ISCSI_NET_PARAM_IPV6_MLD_EN,
	ISCSI_NET_PARAM_IPV6_FLOW_LABEL,
	ISCSI_NET_PARAM_IPV6_TRAFFIC_CLASS,
	ISCSI_NET_PARAM_IPV6_HOP_LIMIT,
	ISCSI_NET_PARAM_IPV6_ND_REACHABLE_TMO,
	ISCSI_NET_PARAM_IPV6_ND_REXMIT_TIME,
	ISCSI_NET_PARAM_IPV6_ND_STALE_TMO,
	ISCSI_NET_PARAM_IPV6_DUP_ADDR_DETECT_CNT,
	ISCSI_NET_PARAM_IPV6_RTR_ADV_LINK_MTU,
	ISCSI_NET_PARAM_REDIRECT_EN,
};

enum iscsi_ipaddress_state {
	ISCSI_IPDDRESS_STATE_UNCONFIGURED,
	ISCSI_IPDDRESS_STATE_ACQUIRING,
	ISCSI_IPDDRESS_STATE_TENTATIVE,
	ISCSI_IPDDRESS_STATE_VALID,
	ISCSI_IPDDRESS_STATE_DISABLING,
	ISCSI_IPDDRESS_STATE_INVALID,
	ISCSI_IPDDRESS_STATE_DEPRECATED,
};

enum iscsi_router_state {
	ISCSI_ROUTER_STATE_UNKNOWN,
	ISCSI_ROUTER_STATE_ADVERTISED,
	ISCSI_ROUTER_STATE_MANUAL,
	ISCSI_ROUTER_STATE_STALE,
};

/* iSCSI specific settings params for iface */
enum iscsi_iface_param {
	ISCSI_IFACE_PARAM_DEF_TASKMGMT_TMO,
	ISCSI_IFACE_PARAM_HDRDGST_EN,
	ISCSI_IFACE_PARAM_DATADGST_EN,
	ISCSI_IFACE_PARAM_IMM_DATA_EN,
	ISCSI_IFACE_PARAM_INITIAL_R2T_EN,
	ISCSI_IFACE_PARAM_DATASEQ_INORDER_EN,
	ISCSI_IFACE_PARAM_PDU_INORDER_EN,
	ISCSI_IFACE_PARAM_ERL,
	ISCSI_IFACE_PARAM_MAX_RECV_DLENGTH,
	ISCSI_IFACE_PARAM_FIRST_BURST,
	ISCSI_IFACE_PARAM_MAX_R2T,
	ISCSI_IFACE_PARAM_MAX_BURST,
	ISCSI_IFACE_PARAM_CHAP_AUTH_EN,
	ISCSI_IFACE_PARAM_BIDI_CHAP_EN,
	ISCSI_IFACE_PARAM_DISCOVERY_AUTH_OPTIONAL,
	ISCSI_IFACE_PARAM_DISCOVERY_LOGOUT_EN,
	ISCSI_IFACE_PARAM_STRICT_LOGIN_COMP_EN,
	ISCSI_IFACE_PARAM_INITIATOR_NAME,
};

enum iscsi_conn_state {
	ISCSI_CONN_STATE_FREE,
	ISCSI_CONN_STATE_XPT_WAIT,
	ISCSI_CONN_STATE_IN_LOGIN,
	ISCSI_CONN_STATE_LOGGED_IN,
	ISCSI_CONN_STATE_IN_LOGOUT,
	ISCSI_CONN_STATE_LOGOUT_REQUESTED,
	ISCSI_CONN_STATE_CLEANUP_WAIT,
};

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
	ISCSI_ERR_NOP_TIMEDOUT		= ISCSI_ERR_BASE + 22,
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

	ISCSI_PARAM_CHAP_IN_IDX,
	ISCSI_PARAM_CHAP_OUT_IDX,

	ISCSI_PARAM_BOOT_ROOT,
	ISCSI_PARAM_BOOT_NIC,
	ISCSI_PARAM_BOOT_TARGET,

	ISCSI_PARAM_AUTO_SND_TGT_DISABLE,
	ISCSI_PARAM_DISCOVERY_SESS,
	ISCSI_PARAM_PORTAL_TYPE,
	ISCSI_PARAM_CHAP_AUTH_EN,
	ISCSI_PARAM_DISCOVERY_LOGOUT_EN,
	ISCSI_PARAM_BIDI_CHAP_EN,
	ISCSI_PARAM_DISCOVERY_AUTH_OPTIONAL,

	ISCSI_PARAM_DEF_TIME2WAIT,
	ISCSI_PARAM_DEF_TIME2RETAIN,
	ISCSI_PARAM_MAX_SEGMENT_SIZE,
	ISCSI_PARAM_STATSN,
	ISCSI_PARAM_KEEPALIVE_TMO,
	ISCSI_PARAM_LOCAL_PORT,
	ISCSI_PARAM_TSID,
	ISCSI_PARAM_DEF_TASKMGMT_TMO,

	ISCSI_PARAM_TCP_TIMESTAMP_STAT,
	ISCSI_PARAM_TCP_WSF_DISABLE,
	ISCSI_PARAM_TCP_NAGLE_DISABLE,
	ISCSI_PARAM_TCP_TIMER_SCALE,
	ISCSI_PARAM_TCP_TIMESTAMP_EN,
	ISCSI_PARAM_TCP_XMIT_WSF,
	ISCSI_PARAM_TCP_RECV_WSF,
	ISCSI_PARAM_IP_FRAGMENT_DISABLE,
	ISCSI_PARAM_IPV4_TOS,
	ISCSI_PARAM_IPV6_TC,
	ISCSI_PARAM_IPV6_FLOW_LABEL,
	ISCSI_PARAM_IS_FW_ASSIGNED_IPV6,

	ISCSI_PARAM_DISCOVERY_PARENT_IDX,
	ISCSI_PARAM_DISCOVERY_PARENT_TYPE,
	ISCSI_PARAM_LOCAL_IPADDR,
	/* must always be last */
	ISCSI_PARAM_MAX,
};

/* iSCSI HBA params */
enum iscsi_host_param {
	ISCSI_HOST_PARAM_HWADDRESS,
	ISCSI_HOST_PARAM_INITIATOR_NAME,
	ISCSI_HOST_PARAM_NETDEV_NAME,
	ISCSI_HOST_PARAM_IPADDRESS,
	ISCSI_HOST_PARAM_PORT_STATE,
	ISCSI_HOST_PARAM_PORT_SPEED,
	ISCSI_HOST_PARAM_MAX,
};

/* portal type */
#define PORTAL_TYPE_IPV4	"ipv4"
#define PORTAL_TYPE_IPV6	"ipv6"

/* iSCSI Flash Target params */
enum iscsi_flashnode_param {
	ISCSI_FLASHNODE_IS_FW_ASSIGNED_IPV6,
	ISCSI_FLASHNODE_PORTAL_TYPE,
	ISCSI_FLASHNODE_AUTO_SND_TGT_DISABLE,
	ISCSI_FLASHNODE_DISCOVERY_SESS,
	ISCSI_FLASHNODE_ENTRY_EN,
	ISCSI_FLASHNODE_HDR_DGST_EN,
	ISCSI_FLASHNODE_DATA_DGST_EN,
	ISCSI_FLASHNODE_IMM_DATA_EN,
	ISCSI_FLASHNODE_INITIAL_R2T_EN,
	ISCSI_FLASHNODE_DATASEQ_INORDER,
	ISCSI_FLASHNODE_PDU_INORDER,
	ISCSI_FLASHNODE_CHAP_AUTH_EN,
	ISCSI_FLASHNODE_SNACK_REQ_EN,
	ISCSI_FLASHNODE_DISCOVERY_LOGOUT_EN,
	ISCSI_FLASHNODE_BIDI_CHAP_EN,
	/* make authentication for discovery sessions optional */
	ISCSI_FLASHNODE_DISCOVERY_AUTH_OPTIONAL,
	ISCSI_FLASHNODE_ERL,
	ISCSI_FLASHNODE_TCP_TIMESTAMP_STAT,
	ISCSI_FLASHNODE_TCP_NAGLE_DISABLE,
	ISCSI_FLASHNODE_TCP_WSF_DISABLE,
	ISCSI_FLASHNODE_TCP_TIMER_SCALE,
	ISCSI_FLASHNODE_TCP_TIMESTAMP_EN,
	ISCSI_FLASHNODE_IP_FRAG_DISABLE,
	ISCSI_FLASHNODE_MAX_RECV_DLENGTH,
	ISCSI_FLASHNODE_MAX_XMIT_DLENGTH,
	ISCSI_FLASHNODE_FIRST_BURST,
	ISCSI_FLASHNODE_DEF_TIME2WAIT,
	ISCSI_FLASHNODE_DEF_TIME2RETAIN,
	ISCSI_FLASHNODE_MAX_R2T,
	ISCSI_FLASHNODE_KEEPALIVE_TMO,
	ISCSI_FLASHNODE_ISID,
	ISCSI_FLASHNODE_TSID,
	ISCSI_FLASHNODE_PORT,
	ISCSI_FLASHNODE_MAX_BURST,
	ISCSI_FLASHNODE_DEF_TASKMGMT_TMO,
	ISCSI_FLASHNODE_IPADDR,
	ISCSI_FLASHNODE_ALIAS,
	ISCSI_FLASHNODE_REDIRECT_IPADDR,
	ISCSI_FLASHNODE_MAX_SEGMENT_SIZE,
	ISCSI_FLASHNODE_LOCAL_PORT,
	ISCSI_FLASHNODE_IPV4_TOS,
	ISCSI_FLASHNODE_IPV6_TC,
	ISCSI_FLASHNODE_IPV6_FLOW_LABEL,
	ISCSI_FLASHNODE_NAME,
	ISCSI_FLASHNODE_TPGT,
	ISCSI_FLASHNODE_LINK_LOCAL_IPV6,
	ISCSI_FLASHNODE_DISCOVERY_PARENT_IDX,
	ISCSI_FLASHNODE_DISCOVERY_PARENT_TYPE,
	ISCSI_FLASHNODE_TCP_XMIT_WSF,
	ISCSI_FLASHNODE_TCP_RECV_WSF,
	ISCSI_FLASHNODE_CHAP_IN_IDX,
	ISCSI_FLASHNODE_CHAP_OUT_IDX,
	ISCSI_FLASHNODE_USERNAME,
	ISCSI_FLASHNODE_USERNAME_IN,
	ISCSI_FLASHNODE_PASSWORD,
	ISCSI_FLASHNODE_PASSWORD_IN,
	ISCSI_FLASHNODE_STATSN,
	ISCSI_FLASHNODE_EXP_STATSN,
	ISCSI_FLASHNODE_IS_BOOT_TGT,

	ISCSI_FLASHNODE_MAX,
};

struct iscsi_flashnode_param_info {
	uint32_t len;		/* Actual length of the param */
	uint16_t param;		/* iscsi param value */
	uint8_t value[0];	/* length sized value follows */
} __packed;

enum iscsi_discovery_parent_type {
	ISCSI_DISC_PARENT_UNKNOWN	= 0x1,
	ISCSI_DISC_PARENT_SENDTGT	= 0x2,
	ISCSI_DISC_PARENT_ISNS		= 0x3,
};

/* iSCSI port Speed */
enum iscsi_port_speed {
	ISCSI_PORT_SPEED_UNKNOWN	= 0x1,
	ISCSI_PORT_SPEED_10MBPS		= 0x2,
	ISCSI_PORT_SPEED_100MBPS	= 0x4,
	ISCSI_PORT_SPEED_1GBPS		= 0x8,
	ISCSI_PORT_SPEED_10GBPS		= 0x10,
	ISCSI_PORT_SPEED_25GBPS         = 0x20,
	ISCSI_PORT_SPEED_40GBPS         = 0x40,
};

/* iSCSI port state */
enum iscsi_port_state {
	ISCSI_PORT_STATE_DOWN		= 0x1,
	ISCSI_PORT_STATE_UP		= 0x2,
};

/* iSCSI PING status/error code */
enum iscsi_ping_status_code {
	ISCSI_PING_SUCCESS			= 0,
	ISCSI_PING_FW_DISABLED			= 0x1,
	ISCSI_PING_IPADDR_INVALID		= 0x2,
	ISCSI_PING_LINKLOCAL_IPV6_ADDR_INVALID	= 0x3,
	ISCSI_PING_TIMEOUT			= 0x4,
	ISCSI_PING_INVALID_DEST_ADDR		= 0x5,
	ISCSI_PING_OVERSIZE_PACKET		= 0x6,
	ISCSI_PING_ICMP_ERROR			= 0x7,
	ISCSI_PING_MAX_REQ_EXCEEDED		= 0x8,
	ISCSI_PING_NO_ARP_RECEIVED		= 0x9,
};

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
#define CAP_LOGIN_OFFLOAD	0x4000  /* offload session login */

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

enum chap_type_e {
	CHAP_TYPE_OUT,
	CHAP_TYPE_IN,
};

enum iscsi_chap_param {
	ISCSI_CHAP_PARAM_INDEX,
	ISCSI_CHAP_PARAM_CHAP_TYPE,
	ISCSI_CHAP_PARAM_USERNAME,
	ISCSI_CHAP_PARAM_PASSWORD,
	ISCSI_CHAP_PARAM_PASSWORD_LEN
};

#define ISCSI_CHAP_AUTH_NAME_MAX_LEN	256
#define ISCSI_CHAP_AUTH_SECRET_MAX_LEN	256
struct iscsi_chap_rec {
	uint16_t chap_tbl_idx;
	enum chap_type_e chap_type;
	char username[ISCSI_CHAP_AUTH_NAME_MAX_LEN];
	uint8_t password[ISCSI_CHAP_AUTH_SECRET_MAX_LEN];
	uint8_t password_length;
};

#define ISCSI_HOST_STATS_CUSTOM_MAX             32
#define ISCSI_HOST_STATS_CUSTOM_DESC_MAX        64
struct iscsi_host_stats_custom {
	char desc[ISCSI_HOST_STATS_CUSTOM_DESC_MAX];
	uint64_t value;
};

/* struct iscsi_offload_host_stats: Host statistics,
 * Include statistics for MAC, IP, TCP & iSCSI.
 */
struct iscsi_offload_host_stats {
	/* MAC */
	uint64_t mactx_frames;
	uint64_t mactx_bytes;
	uint64_t mactx_multicast_frames;
	uint64_t mactx_broadcast_frames;
	uint64_t mactx_pause_frames;
	uint64_t mactx_control_frames;
	uint64_t mactx_deferral;
	uint64_t mactx_excess_deferral;
	uint64_t mactx_late_collision;
	uint64_t mactx_abort;
	uint64_t mactx_single_collision;
	uint64_t mactx_multiple_collision;
	uint64_t mactx_collision;
	uint64_t mactx_frames_dropped;
	uint64_t mactx_jumbo_frames;
	uint64_t macrx_frames;
	uint64_t macrx_bytes;
	uint64_t macrx_unknown_control_frames;
	uint64_t macrx_pause_frames;
	uint64_t macrx_control_frames;
	uint64_t macrx_dribble;
	uint64_t macrx_frame_length_error;
	uint64_t macrx_jabber;
	uint64_t macrx_carrier_sense_error;
	uint64_t macrx_frame_discarded;
	uint64_t macrx_frames_dropped;
	uint64_t mac_crc_error;
	uint64_t mac_encoding_error;
	uint64_t macrx_length_error_large;
	uint64_t macrx_length_error_small;
	uint64_t macrx_multicast_frames;
	uint64_t macrx_broadcast_frames;
	/* IP */
	uint64_t iptx_packets;
	uint64_t iptx_bytes;
	uint64_t iptx_fragments;
	uint64_t iprx_packets;
	uint64_t iprx_bytes;
	uint64_t iprx_fragments;
	uint64_t ip_datagram_reassembly;
	uint64_t ip_invalid_address_error;
	uint64_t ip_error_packets;
	uint64_t ip_fragrx_overlap;
	uint64_t ip_fragrx_outoforder;
	uint64_t ip_datagram_reassembly_timeout;
	uint64_t ipv6tx_packets;
	uint64_t ipv6tx_bytes;
	uint64_t ipv6tx_fragments;
	uint64_t ipv6rx_packets;
	uint64_t ipv6rx_bytes;
	uint64_t ipv6rx_fragments;
	uint64_t ipv6_datagram_reassembly;
	uint64_t ipv6_invalid_address_error;
	uint64_t ipv6_error_packets;
	uint64_t ipv6_fragrx_overlap;
	uint64_t ipv6_fragrx_outoforder;
	uint64_t ipv6_datagram_reassembly_timeout;
	/* TCP */
	uint64_t tcptx_segments;
	uint64_t tcptx_bytes;
	uint64_t tcprx_segments;
	uint64_t tcprx_byte;
	uint64_t tcp_duplicate_ack_retx;
	uint64_t tcp_retx_timer_expired;
	uint64_t tcprx_duplicate_ack;
	uint64_t tcprx_pure_ackr;
	uint64_t tcptx_delayed_ack;
	uint64_t tcptx_pure_ack;
	uint64_t tcprx_segment_error;
	uint64_t tcprx_segment_outoforder;
	uint64_t tcprx_window_probe;
	uint64_t tcprx_window_update;
	uint64_t tcptx_window_probe_persist;
	/* ECC */
	uint64_t ecc_error_correction;
	/* iSCSI */
	uint64_t iscsi_pdu_tx;
	uint64_t iscsi_data_bytes_tx;
	uint64_t iscsi_pdu_rx;
	uint64_t iscsi_data_bytes_rx;
	uint64_t iscsi_io_completed;
	uint64_t iscsi_unexpected_io_rx;
	uint64_t iscsi_format_error;
	uint64_t iscsi_hdr_digest_error;
	uint64_t iscsi_data_digest_error;
	uint64_t iscsi_sequence_error;
	/*
	 * iSCSI Custom Host Statistics support, i.e. Transport could
	 * extend existing host statistics with its own specific statistics
	 * up to ISCSI_HOST_STATS_CUSTOM_MAX
	 */
	uint32_t custom_length;
	struct iscsi_host_stats_custom custom[0]
		__aligned(sizeof(uint64_t));
};

#endif
