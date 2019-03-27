/*
 * ng_l2cap.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_l2cap.h,v 1.2 2003/04/27 00:52:26 max Exp $
 * $FreeBSD$
 */

/*
 * This file contains everything that application needs to know about
 * Link Layer Control and Adaptation Protocol (L2CAP). All information 
 * was obtained from Bluetooth Specification Book v1.1.
 *
 * This file can be included by both kernel and userland applications.
 */

#ifndef _NETGRAPH_L2CAP_H_
#define _NETGRAPH_L2CAP_H_

/**************************************************************************
 **************************************************************************
 **     Netgraph node hook name, type name and type cookie and commands
 **************************************************************************
 **************************************************************************/

/* Netgraph node hook names */
#define NG_L2CAP_HOOK_HCI		"hci"	/* HCI   <-> L2CAP */
#define NG_L2CAP_HOOK_L2C		"l2c"	/* L2CAP <-> Upper */
#define NG_L2CAP_HOOK_CTL		"ctl"	/* L2CAP <-> User  */ 

/* Node type name and type cookie */
#define NG_L2CAP_NODE_TYPE		"l2cap"
#define NGM_L2CAP_COOKIE		1000774185

/**************************************************************************
 **************************************************************************
 **                   Common defines and types (L2CAP)
 **************************************************************************
 **************************************************************************/

/*
 * Channel IDs are assigned relative to the instance of L2CAP node, i.e.
 * relative to the unit. So the total number of channels that unit can have
 * open at the same time is 0xffff - 0x0040 = 0xffbf (65471). This number
 * does not depend on number of connections.
 */

#define NG_L2CAP_NULL_CID	0x0000	/* DO NOT USE THIS CID */
#define NG_L2CAP_SIGNAL_CID	0x0001	/* signaling channel ID */
#define NG_L2CAP_CLT_CID	0x0002	/* connectionless channel ID */
#define NG_L2CAP_A2MP_CID	0x0003  
#define NG_L2CAP_ATT_CID	0x0004  
#define NG_L2CAP_LESIGNAL_CID	0x0005
#define NG_L2CAP_SMP_CID	0x0006
	/* 0x0007 - 0x003f Reserved */
#define NG_L2CAP_FIRST_CID	0x0040	/* dynamically alloc. (start) */
#define NG_L2CAP_LAST_CID	0xffff	/* dynamically alloc. (end) */
#define NG_L2CAP_LELAST_CID	0x007f


/* L2CAP MTU */
#define NG_L2CAP_MTU_LE_MINIMAM		23
#define NG_L2CAP_MTU_MINIMUM		48
#define NG_L2CAP_MTU_DEFAULT		672
#define NG_L2CAP_MTU_MAXIMUM		0xffff

/* L2CAP flush and link timeouts */
#define NG_L2CAP_FLUSH_TIMO_DEFAULT	0xffff /* always retransmit */
#define NG_L2CAP_LINK_TIMO_DEFAULT	0xffff

/* L2CAP Command Reject reasons */
#define NG_L2CAP_REJ_NOT_UNDERSTOOD	0x0000
#define NG_L2CAP_REJ_MTU_EXCEEDED	0x0001
#define NG_L2CAP_REJ_INVALID_CID	0x0002
/* 0x0003 - 0xffff - reserved for future use */

/* Protocol/Service Multioplexor (PSM) values */
#define NG_L2CAP_PSM_ANY		0x0000	/* Any/Invalid PSM */
#define NG_L2CAP_PSM_SDP		0x0001	/* Service Discovery Protocol */
#define NG_L2CAP_PSM_RFCOMM		0x0003	/* RFCOMM protocol */
#define NG_L2CAP_PSM_TCP		0x0005	/* Telephony Control Protocol */
/* 0x0006 - 0x1000 - reserved for future use */

/* L2CAP Connection response command result codes */
#define NG_L2CAP_SUCCESS		0x0000
#define NG_L2CAP_PENDING		0x0001
#define NG_L2CAP_PSM_NOT_SUPPORTED	0x0002
#define NG_L2CAP_SEQUIRY_BLOCK		0x0003
#define NG_L2CAP_NO_RESOURCES		0x0004
#define NG_L2CAP_TIMEOUT		0xeeee
#define NG_L2CAP_UNKNOWN		0xffff
/* 0x0005 - 0xffff - reserved for future use */

/* L2CAP Connection response status codes */
#define NG_L2CAP_NO_INFO		0x0000
#define NG_L2CAP_AUTH_PENDING		0x0001
#define NG_L2CAP_AUTZ_PENDING		0x0002
/* 0x0003 - 0xffff - reserved for future use */

/* L2CAP Configuration response result codes */
#define NG_L2CAP_UNACCEPTABLE_PARAMS	0x0001
#define NG_L2CAP_REJECT			0x0002
#define NG_L2CAP_UNKNOWN_OPTION		0x0003
/* 0x0003 - 0xffff - reserved for future use */

/* L2CAP Configuration options */
#define NG_L2CAP_OPT_CFLAG_BIT		0x0001
#define NG_L2CAP_OPT_CFLAG(flags)	((flags) & NG_L2CAP_OPT_CFLAG_BIT)
#define NG_L2CAP_OPT_HINT_BIT		0x80
#define NG_L2CAP_OPT_HINT(type)		((type) & NG_L2CAP_OPT_HINT_BIT)
#define NG_L2CAP_OPT_HINT_MASK		0x7f
#define NG_L2CAP_OPT_MTU		0x01
#define NG_L2CAP_OPT_MTU_SIZE		sizeof(u_int16_t)
#define NG_L2CAP_OPT_FLUSH_TIMO		0x02
#define NG_L2CAP_OPT_FLUSH_TIMO_SIZE	sizeof(u_int16_t)
#define NG_L2CAP_OPT_QOS		0x03
#define NG_L2CAP_OPT_QOS_SIZE		sizeof(ng_l2cap_flow_t)
/* 0x4 - 0xff - reserved for future use */

/* L2CAP Information request type codes */
#define NG_L2CAP_CONNLESS_MTU		0x0001
/* 0x0002 - 0xffff - reserved for future use */

/* L2CAP Information response codes */
#define NG_L2CAP_NOT_SUPPORTED		0x0001
/* 0x0002 - 0xffff - reserved for future use */

/* L2CAP flow control */
typedef struct {
	u_int8_t	flags;             /* reserved for future use */
	u_int8_t	service_type;      /* service type */
	u_int32_t	token_rate;        /* bytes per second */
	u_int32_t	token_bucket_size; /* bytes */
	u_int32_t	peak_bandwidth;    /* bytes per second */
	u_int32_t	latency;           /* microseconds */
	u_int32_t	delay_variation;   /* microseconds */
} __attribute__ ((packed)) ng_l2cap_flow_t;
typedef ng_l2cap_flow_t *	ng_l2cap_flow_p;

/**************************************************************************
 **************************************************************************
 **                 Link level defines, headers and types
 **************************************************************************
 **************************************************************************/

/* L2CAP header */
typedef struct {
	u_int16_t	length;	/* payload size */
	u_int16_t	dcid;	/* destination channel ID */
} __attribute__ ((packed)) ng_l2cap_hdr_t;

/* L2CAP ConnectionLess Traffic (CLT) (if destination cid == 0x2) */
typedef struct {
	u_int16_t	psm; /* Protocol/Service Multiplexor */
} __attribute__ ((packed)) ng_l2cap_clt_hdr_t;

#define NG_L2CAP_CLT_MTU_MAXIMUM \
	(NG_L2CAP_MTU_MAXIMUM - sizeof(ng_l2cap_clt_hdr_t))

/* L2CAP command header */
typedef struct {
	u_int8_t	code;   /* command OpCode */
	u_int8_t	ident;  /* identifier to match request and response */
	u_int16_t	length; /* command parameters length */
} __attribute__ ((packed)) ng_l2cap_cmd_hdr_t;

/* L2CAP Command Reject */
#define NG_L2CAP_CMD_REJ	0x01
typedef struct {
	u_int16_t	reason; /* reason to reject command */
/*	u_int8_t	data[]; -- optional data (depends on reason) */
} __attribute__ ((packed)) ng_l2cap_cmd_rej_cp;

/* CommandReject data */
typedef union {
 	/* NG_L2CAP_REJ_MTU_EXCEEDED */
	struct {
		u_int16_t	mtu; /* actual signaling MTU */
	} __attribute__ ((packed)) mtu;
	/* NG_L2CAP_REJ_INVALID_CID */
	struct {
		u_int16_t	scid; /* local CID */
		u_int16_t	dcid; /* remote CID */
	} __attribute__ ((packed)) cid;
} ng_l2cap_cmd_rej_data_t;
typedef ng_l2cap_cmd_rej_data_t * ng_l2cap_cmd_rej_data_p;

/* L2CAP Connection Request */
#define NG_L2CAP_CON_REQ	0x02
typedef struct {
	u_int16_t	psm;  /* Protocol/Service Multiplexor (PSM) */
	u_int16_t	scid; /* source channel ID */
} __attribute__ ((packed)) ng_l2cap_con_req_cp;

/* L2CAP Connection Response */
#define NG_L2CAP_CON_RSP	0x03
typedef struct {
	u_int16_t	dcid;   /* destination channel ID */
	u_int16_t	scid;   /* source channel ID */
	u_int16_t	result; /* 0x00 - success */
	u_int16_t	status; /* more info if result != 0x00 */
} __attribute__ ((packed)) ng_l2cap_con_rsp_cp;

/* L2CAP Configuration Request */
#define NG_L2CAP_CFG_REQ	0x04
typedef struct {
	u_int16_t	dcid;  /* destination channel ID */
	u_int16_t	flags; /* flags */
/*	u_int8_t	options[] --  options */
} __attribute__ ((packed)) ng_l2cap_cfg_req_cp;

/* L2CAP Configuration Response */
#define NG_L2CAP_CFG_RSP	0x05
typedef struct {
	u_int16_t	scid;   /* source channel ID */
	u_int16_t	flags;  /* flags */
	u_int16_t	result; /* 0x00 - success */
/*	u_int8_t	options[] -- options */
} __attribute__ ((packed)) ng_l2cap_cfg_rsp_cp;

/* L2CAP configuration option */
typedef struct {
	u_int8_t	type;
	u_int8_t	length;
/*	u_int8_t	value[] -- option value (depends on type) */
} __attribute__ ((packed)) ng_l2cap_cfg_opt_t;
typedef ng_l2cap_cfg_opt_t * ng_l2cap_cfg_opt_p;

/* L2CAP configuration option value */
typedef union {
	u_int16_t		mtu;		/* NG_L2CAP_OPT_MTU */
	u_int16_t		flush_timo;	/* NG_L2CAP_OPT_FLUSH_TIMO */
	ng_l2cap_flow_t		flow;		/* NG_L2CAP_OPT_QOS */
	uint16_t		encryption;
} ng_l2cap_cfg_opt_val_t;
typedef ng_l2cap_cfg_opt_val_t * ng_l2cap_cfg_opt_val_p;

/* L2CAP Disconnect Request */
#define NG_L2CAP_DISCON_REQ	0x06
typedef struct {
	u_int16_t	dcid; /* destination channel ID */
	u_int16_t	scid; /* source channel ID */
} __attribute__ ((packed)) ng_l2cap_discon_req_cp;

/* L2CAP Disconnect Response */
#define NG_L2CAP_DISCON_RSP	0x07
typedef ng_l2cap_discon_req_cp	ng_l2cap_discon_rsp_cp;

/* L2CAP Echo Request */
#define NG_L2CAP_ECHO_REQ	0x08
/* No command parameters, only optional data */

/* L2CAP Echo Response */
#define NG_L2CAP_ECHO_RSP	0x09
#define NG_L2CAP_MAX_ECHO_SIZE \
	(NG_L2CAP_MTU_MAXIMUM - sizeof(ng_l2cap_cmd_hdr_t))
/* No command parameters, only optional data */

/* L2CAP Information Request */
#define NG_L2CAP_INFO_REQ	0x0a
typedef struct {
	u_int16_t	type; /* requested information type */
} __attribute__ ((packed)) ng_l2cap_info_req_cp;

/* L2CAP Information Response */
#define NG_L2CAP_INFO_RSP	0x0b
typedef struct {
	u_int16_t	type;   /* requested information type */
	u_int16_t	result; /* 0x00 - success */
/*	u_int8_t	info[]  -- info data (depends on type)
 *
 * NG_L2CAP_CONNLESS_MTU - 2 bytes connectionless MTU
 */
} __attribute__ ((packed)) ng_l2cap_info_rsp_cp;
typedef union {
 	/* NG_L2CAP_CONNLESS_MTU */
	struct {
		u_int16_t	mtu;
	} __attribute__ ((packed)) mtu;
} ng_l2cap_info_rsp_data_t;
typedef ng_l2cap_info_rsp_data_t *	ng_l2cap_info_rsp_data_p;

#define NG_L2CAP_CMD_PARAM_UPDATE_REQUEST	0x12

typedef struct {
  uint16_t interval_min;
  uint16_t interval_max;
  uint16_t slave_latency;
  uint16_t timeout_mpl;
} __attribute__ ((packed)) ng_l2cap_param_update_req_cp;

#define NG_L2CAP_CMD_PARAM_UPDATE_RESPONSE	0x13
#define NG_L2CAP_UPDATE_PARAM_ACCEPT 0
#define NG_L2CAP_UPDATE_PARAM_REJECT 1

//typedef uint16_t update_response;
/**************************************************************************
 **************************************************************************
 **        Upper layer protocol interface. L2CA_xxx messages 
 **************************************************************************
 **************************************************************************/

/*
 * NOTE! NOTE! NOTE!
 *
 * Bluetooth specification says that L2CA_xxx request must block until
 * response is ready. We are not allowed to block in Netgraph, so we 
 * need to queue request and save some information that can be used 
 * later and help match request and response.
 *
 * The idea is to use "token" field from Netgraph message header. The
 * upper layer protocol _MUST_ populate "token". L2CAP will queue request
 * (using L2CAP command descriptor) and start processing. Later, when
 * response is ready or timeout has occur L2CAP layer will create new 
 * Netgraph message, set "token" and RESP flag and send the message to
 * the upper layer protocol. 
 *
 * L2CA_xxx_Ind messages _WILL_NOT_ populate "token" and _WILL_NOT_
 * set RESP flag. There is no reason for this, because they are just
 * notifications and do not require acknowlegment.
 *
 * NOTE: This is _NOT_ what NG_MKRESPONSE and NG_RESPOND_MSG do, however
 *       it is somewhat similar.
 */

/* L2CA data packet header */
typedef struct {
	u_int32_t	token;	/* token to use in L2CAP_L2CA_WRITE */
	u_int16_t	length;	/* length of the data */
	u_int16_t	lcid;	/* local channel ID */
	uint16_t	idtype;
} __attribute__ ((packed)) ng_l2cap_l2ca_hdr_t;
#define NG_L2CAP_L2CA_IDTYPE_BREDR 0
#define NG_L2CAP_L2CA_IDTYPE_ATT  1
#define NG_L2CAP_L2CA_IDTYPE_LE  2
#define NG_L2CAP_L2CA_IDTYPE_SMP  3
/* L2CA_Connect */
#define NGM_L2CAP_L2CA_CON		0x80
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	psm;    /* Protocol/Service Multiplexor */
	bdaddr_t	bdaddr;	/* remote unit address */
	uint8_t		linktype;
	uint8_t		idtype;
} ng_l2cap_l2ca_con_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	lcid;   /* local channel ID */
	uint16_t	idtype; /*ID type*/
	u_int16_t	result; /* 0x00 - success */
	u_int16_t	status; /* if result != 0x00 */
	uint8_t 	encryption;
} ng_l2cap_l2ca_con_op;

/* L2CA_ConnectInd */
#define NGM_L2CAP_L2CA_CON_IND		0x81
/* L2CAP -> Upper */
typedef struct {
	bdaddr_t	bdaddr; /* remote unit address */
	u_int16_t	lcid;   /* local channel ID */
	u_int16_t	psm;    /* Procotol/Service Multiplexor */
	u_int8_t	ident;  /* identifier */
	u_int8_t	linktype; /* link type*/
} ng_l2cap_l2ca_con_ind_ip;
/* No output parameters */

/* L2CA_ConnectRsp */
#define NGM_L2CAP_L2CA_CON_RSP		0x82
/* Upper -> L2CAP */
typedef struct {
	bdaddr_t	bdaddr; /* remote unit address */
	u_int8_t	ident;  /* "ident" from L2CAP_ConnectInd event */
	u_int8_t	linktype; /*link type */
	u_int16_t	lcid;   /* local channel ID */
	u_int16_t	result; /* 0x00 - success */ 
	u_int16_t	status; /* if response != 0x00 */ 
} ng_l2cap_l2ca_con_rsp_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result; /* 0x00 - success */
} ng_l2cap_l2ca_con_rsp_op;

/* L2CA_Config */
#define NGM_L2CAP_L2CA_CFG		0x83
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	lcid;        /* local channel ID */
	u_int16_t	imtu;        /* receiving MTU for the local channel */
	ng_l2cap_flow_t	oflow;       /* out flow */
	u_int16_t	flush_timo;  /* flush timeout (msec) */
	u_int16_t	link_timo;   /* link timeout (msec) */
} ng_l2cap_l2ca_cfg_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result;      /* 0x00 - success */
	u_int16_t	imtu;        /* sending MTU for the remote channel */
	ng_l2cap_flow_t	oflow;       /* out flow */
	u_int16_t	flush_timo;  /* flush timeout (msec) */
} ng_l2cap_l2ca_cfg_op;

/* L2CA_ConfigRsp */
#define NGM_L2CAP_L2CA_CFG_RSP		0x84
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	lcid;  /* local channel ID */
	u_int16_t	omtu;  /* sending MTU for the local channel */
	ng_l2cap_flow_t	iflow; /* in FLOW */
} ng_l2cap_l2ca_cfg_rsp_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result; /* 0x00 - sucsess */
} ng_l2cap_l2ca_cfg_rsp_op;

/* L2CA_ConfigInd */
#define NGM_L2CAP_L2CA_CFG_IND		0x85
/* L2CAP -> Upper */
typedef struct {
	u_int16_t	lcid;        /* local channel ID */
	u_int16_t	omtu;        /* outgoing MTU for the local channel */
	ng_l2cap_flow_t	iflow;       /* in flow */
	u_int16_t	flush_timo;  /* flush timeout (msec) */
} ng_l2cap_l2ca_cfg_ind_ip;
/* No output parameters */

/* L2CA_QoSViolationInd */
#define NGM_L2CAP_L2CA_QOS_IND		0x86
/* L2CAP -> Upper */
typedef struct {
	bdaddr_t	bdaddr;	/* remote unit address */
} ng_l2cap_l2ca_qos_ind_ip;
/* No output parameters */

/* L2CA_Disconnect */
#define NGM_L2CAP_L2CA_DISCON		0x87
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	lcid;  /* local channel ID */
	u_int16_t	idtype;
} ng_l2cap_l2ca_discon_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result; /* 0x00 - sucsess */
} ng_l2cap_l2ca_discon_op;

/* L2CA_DisconnectInd */
#define NGM_L2CAP_L2CA_DISCON_IND	0x88
/* L2CAP -> Upper */
typedef ng_l2cap_l2ca_discon_ip ng_l2cap_l2ca_discon_ind_ip;
/* No output parameters */

/* L2CA_Write response */
#define NGM_L2CAP_L2CA_WRITE		0x89
/* No input parameters */

/* L2CAP -> Upper */
typedef struct {
	int		result;	/* result (0x00 - success) */
	u_int16_t	length;	/* amount of data written */
	u_int16_t	lcid;	/* local channel ID */
	uint16_t 	idtype;
} ng_l2cap_l2ca_write_op;

/* L2CA_GroupCreate */
#define NGM_L2CAP_L2CA_GRP_CREATE	0x8a
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	psm;   /* Protocol/Service Multiplexor */
} ng_l2cap_l2ca_grp_create_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	lcid;  /* local group channel ID */
} ng_l2cap_l2ca_grp_create_op;

/* L2CA_GroupClose */
#define NGM_L2CAP_L2CA_GRP_CLOSE	0x8b
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	lcid;  /* local group channel ID */
} ng_l2cap_l2ca_grp_close_ip;

#if 0
/* L2CAP -> Upper */
 * typedef struct {
 * 	u_int16_t	result; /* 0x00 - success */
 * } ng_l2cap_l2ca_grp_close_op;
#endif

/* L2CA_GroupAddMember */
#define NGM_L2CAP_L2CA_GRP_ADD_MEMBER	0x8c
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	lcid;   /* local group channel ID */
	bdaddr_t	bdaddr; /* remote unit address */
} ng_l2cap_l2ca_grp_add_member_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result; /* 0x00 - success */
} ng_l2cap_l2ca_grp_add_member_op;

/* L2CA_GroupRemoveMember */
#define NGM_L2CAP_L2CA_GRP_REM_MEMBER	0x8d
/* Upper -> L2CAP */
typedef ng_l2cap_l2ca_grp_add_member_ip	ng_l2cap_l2ca_grp_rem_member_ip;

/* L2CAP -> Upper */
#if 0
 * typedef ng_l2cap_l2ca_grp_add_member_op	ng_l2cap_l2ca_grp_rem_member_op;
#endif

/* L2CA_GroupMembeship */
#define NGM_L2CAP_L2CA_GRP_MEMBERSHIP	0x8e
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	lcid;  /* local group channel ID */
} ng_l2cap_l2ca_grp_get_members_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result;   /* 0x00 - success */
	u_int16_t	nmembers; /* number of group members */
/*	bdaddr_t	members[] -- group memebers */
} ng_l2cap_l2ca_grp_get_members_op;

/* L2CA_Ping */
#define NGM_L2CAP_L2CA_PING		0x8f
/* Upper -> L2CAP */
typedef struct {
	bdaddr_t	bdaddr;    /* remote unit address */
	u_int16_t	echo_size; /* size of echo data in bytes */
/*	u_int8_t	echo_data[] -- echo data */
} ng_l2cap_l2ca_ping_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result;    /* 0x00 - success */
	bdaddr_t	bdaddr;    /* remote unit address */
	u_int16_t	echo_size; /* size of echo data in bytes */
/*	u_int8_t	echo_data[] -- echo data */
} ng_l2cap_l2ca_ping_op;

/* L2CA_GetInfo */
#define NGM_L2CAP_L2CA_GET_INFO		0x90
/* Upper -> L2CAP */
typedef struct {
	bdaddr_t	bdaddr;	   /* remote unit address */
	u_int16_t	info_type; /* info type */
	uint8_t		linktype;
	uint8_t	        unused;
} ng_l2cap_l2ca_get_info_ip;

/* L2CAP -> Upper */
typedef struct {
	u_int16_t	result;    /* 0x00 - success */
	u_int16_t	info_size; /* size of info data in bytes */
/*	u_int8_t	info_data[] -- info data */
} ng_l2cap_l2ca_get_info_op;

/* L2CA_EnableCLT/L2CA_DisableCLT */
#define NGM_L2CAP_L2CA_ENABLE_CLT	0x91
/* Upper -> L2CAP */
typedef struct {
	u_int16_t	psm;    /* Protocol/Service Multiplexor */
	u_int16_t	enable; /* 0x00 - disable */
} ng_l2cap_l2ca_enable_clt_ip;

#if 0
/* L2CAP -> Upper */
 * typedef struct {
 * 	u_int16_t	result; /* 0x00 - success */
 * } ng_l2cap_l2ca_enable_clt_op;
#endif
#define NGM_L2CAP_L2CA_ENC_CHANGE 0x92
typedef struct {
	uint16_t 	lcid;
	uint16_t	result;
	uint8_t 	idtype;
} ng_l2cap_l2ca_enc_chg_op;

/**************************************************************************
 **************************************************************************
 **                          L2CAP node messages
 **************************************************************************
 **************************************************************************/

/* L2CAP connection states */
#define NG_L2CAP_CON_CLOSED		0	/* connection closed */
#define NG_L2CAP_W4_LP_CON_CFM		1	/* waiting... */
#define NG_L2CAP_CON_OPEN		2	/* connection open */

/* L2CAP channel states */
#define NG_L2CAP_CLOSED			0	/* channel closed */
#define NG_L2CAP_W4_L2CAP_CON_RSP	1	/* wait for L2CAP resp. */
#define NG_L2CAP_W4_L2CA_CON_RSP	2	/* wait for upper resp. */
#define NG_L2CAP_CONFIG			3	/* L2CAP configuration */
#define NG_L2CAP_OPEN			4	/* channel open */
#define NG_L2CAP_W4_L2CAP_DISCON_RSP	5	/* wait for L2CAP discon. */
#define NG_L2CAP_W4_L2CA_DISCON_RSP	6	/* wait for upper discon. */

/* Node flags */
#define NG_L2CAP_CLT_SDP_DISABLED	(1 << 0)      /* disable SDP CLT */
#define NG_L2CAP_CLT_RFCOMM_DISABLED	(1 << 1)      /* disable RFCOMM CLT */
#define NG_L2CAP_CLT_TCP_DISABLED	(1 << 2)      /* disable TCP CLT */

/* Debug levels */
#define NG_L2CAP_ALERT_LEVEL		1
#define NG_L2CAP_ERR_LEVEL		2
#define NG_L2CAP_WARN_LEVEL		3
#define NG_L2CAP_INFO_LEVEL		4

/* Get node flags (see flags above) */
#define	NGM_L2CAP_NODE_GET_FLAGS	0x400	/* L2CAP -> User */
typedef u_int16_t	ng_l2cap_node_flags_ep;

/* Get/Set debug level (see levels above) */
#define	NGM_L2CAP_NODE_GET_DEBUG	0x401	/* L2CAP -> User */
#define	NGM_L2CAP_NODE_SET_DEBUG	0x402	/* User -> L2CAP */
typedef u_int16_t	ng_l2cap_node_debug_ep;

#define NGM_L2CAP_NODE_HOOK_INFO	0x409	/* L2CAP -> Upper */
typedef struct {
	bdaddr_t addr;
}ng_l2cap_node_hook_info_ep;

#define NGM_L2CAP_NODE_GET_CON_LIST	0x40a	/* L2CAP -> User */
typedef struct {
	u_int32_t	num_connections; /* number of connections */
} ng_l2cap_node_con_list_ep;

/* Connection flags */
#define NG_L2CAP_CON_TX			(1 << 0) /* sending data */
#define NG_L2CAP_CON_RX			(1 << 1) /* receiving data */
#define NG_L2CAP_CON_OUTGOING		(1 << 2) /* outgoing connection */
#define NG_L2CAP_CON_LP_TIMO		(1 << 3) /* LP timeout */
#define NG_L2CAP_CON_AUTO_DISCON_TIMO	(1 << 4) /* auto discon. timeout */
#define NG_L2CAP_CON_DYING		(1 << 5) /* connection is dying */

typedef struct {
	u_int8_t	state;      /* connection state */
	u_int8_t	flags;      /* flags */
	int16_t		pending;    /* num. pending packets */
	u_int16_t	con_handle; /* connection handle */
	bdaddr_t	remote;     /* remote bdaddr */
} ng_l2cap_node_con_ep;

#define NG_L2CAP_MAX_CON_NUM \
	((0xffff - sizeof(ng_l2cap_node_con_list_ep))/sizeof(ng_l2cap_node_con_ep))

#define NGM_L2CAP_NODE_GET_CHAN_LIST	0x40b	/* L2CAP -> User */
typedef struct {
	u_int32_t	num_channels;	/* number of channels */
} ng_l2cap_node_chan_list_ep;

typedef struct {
	u_int32_t	state;		/* channel state */

	u_int16_t	scid;		/* source (local) channel ID */
	u_int16_t	dcid;		/* destination (remote) channel ID */

	u_int16_t	imtu;		/* incoming MTU */
	u_int16_t	omtu;		/* outgoing MTU */

	u_int16_t	psm;		/* PSM */
	bdaddr_t	remote;		/* remote bdaddr */
} ng_l2cap_node_chan_ep;

#define NG_L2CAP_MAX_CHAN_NUM \
	((0xffff - sizeof(ng_l2cap_node_chan_list_ep))/sizeof(ng_l2cap_node_chan_ep))

#define NGM_L2CAP_NODE_GET_AUTO_DISCON_TIMO 0x40c /* L2CAP -> User */
#define NGM_L2CAP_NODE_SET_AUTO_DISCON_TIMO 0x40d /* User -> L2CAP */
typedef u_int16_t	ng_l2cap_node_auto_discon_ep;

#endif /* ndef _NETGRAPH_L2CAP_H_ */

