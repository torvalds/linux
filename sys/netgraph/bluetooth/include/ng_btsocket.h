/*
 * ng_btsocket.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: ng_btsocket.h,v 1.8 2003/04/26 22:32:10 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_BTSOCKET_H_
#define _NETGRAPH_BTSOCKET_H_

/*
 * Bluetooth protocols
 */

#define BLUETOOTH_PROTO_HCI	134	/* HCI protocol number */
#define BLUETOOTH_PROTO_L2CAP	135	/* L2CAP protocol number */
#define BLUETOOTH_PROTO_RFCOMM	136	/* RFCOMM protocol number */
#define BLUETOOTH_PROTO_SCO	137	/* SCO protocol number */

/*
 * Bluetooth version of struct sockaddr for raw HCI sockets
 */

struct sockaddr_hci {
	u_char		hci_len;	/* total length */
	u_char		hci_family;	/* address family */
	char		hci_node[32];	/* address (size == NG_NODESIZ ) */
};

/* Raw HCI socket options */
#define SOL_HCI_RAW		0x0802	/* socket options level */

#define SO_HCI_RAW_FILTER	1	/* get/set filter on socket */
#define SO_HCI_RAW_DIRECTION	2	/* turn on/off direction info */
#define SCM_HCI_RAW_DIRECTION	SO_HCI_RAW_DIRECTION /* cmsg_type  */

/*
 * Raw HCI socket filter.
 *
 * For packet mask use (1 << (HCI packet indicator - 1))
 * For event mask use (1 << (Event - 1))
 */

struct ng_btsocket_hci_raw_filter {
	bitstr_t	bit_decl(packet_mask, 32);
	bitstr_t	bit_decl(event_mask, (NG_HCI_EVENT_MASK_SIZE * 8));
};

/*
 * Raw HCI sockets ioctl's
 */

/* Get state */
struct ng_btsocket_hci_raw_node_state {
	ng_hci_node_state_ep	state;
};
#define SIOC_HCI_RAW_NODE_GET_STATE \
	_IOWR('b', NGM_HCI_NODE_GET_STATE, \
		struct ng_btsocket_hci_raw_node_state)

/* Initialize */
#define SIOC_HCI_RAW_NODE_INIT \
	_IO('b', NGM_HCI_NODE_INIT)

/* Get/Set debug level */
struct ng_btsocket_hci_raw_node_debug {
	ng_hci_node_debug_ep	debug;
};
#define SIOC_HCI_RAW_NODE_GET_DEBUG \
	_IOWR('b', NGM_HCI_NODE_GET_DEBUG, \
		struct ng_btsocket_hci_raw_node_debug)
#define SIOC_HCI_RAW_NODE_SET_DEBUG \
	_IOWR('b', NGM_HCI_NODE_SET_DEBUG, \
		struct ng_btsocket_hci_raw_node_debug)

/* Get buffer info */
struct ng_btsocket_hci_raw_node_buffer {
	ng_hci_node_buffer_ep	buffer;
};
#define SIOC_HCI_RAW_NODE_GET_BUFFER \
	_IOWR('b', NGM_HCI_NODE_GET_BUFFER, \
		struct ng_btsocket_hci_raw_node_buffer)

/* Get BD_ADDR */
struct ng_btsocket_hci_raw_node_bdaddr {
	bdaddr_t	bdaddr;
};
#define SIOC_HCI_RAW_NODE_GET_BDADDR \
	_IOWR('b', NGM_HCI_NODE_GET_BDADDR, \
		struct ng_btsocket_hci_raw_node_bdaddr)

/* Get features */
struct ng_btsocket_hci_raw_node_features {
	u_int8_t	features[NG_HCI_FEATURES_SIZE];
};
#define SIOC_HCI_RAW_NODE_GET_FEATURES \
	_IOWR('b', NGM_HCI_NODE_GET_FEATURES, \
		struct ng_btsocket_hci_raw_node_features)

/* Get stat */
struct ng_btsocket_hci_raw_node_stat {
	ng_hci_node_stat_ep	stat;
};
#define SIOC_HCI_RAW_NODE_GET_STAT \
	_IOWR('b', NGM_HCI_NODE_GET_STAT, \
		struct ng_btsocket_hci_raw_node_stat)

/* Reset stat */
#define SIOC_HCI_RAW_NODE_RESET_STAT \
	_IO('b', NGM_HCI_NODE_RESET_STAT)

/* Flush neighbor cache */
#define SIOC_HCI_RAW_NODE_FLUSH_NEIGHBOR_CACHE \
	_IO('b', NGM_HCI_NODE_FLUSH_NEIGHBOR_CACHE)

/* Get neighbor cache */
struct ng_btsocket_hci_raw_node_neighbor_cache {
	u_int32_t				 num_entries;
	ng_hci_node_neighbor_cache_entry_ep	*entries;
};
#define SIOC_HCI_RAW_NODE_GET_NEIGHBOR_CACHE \
	_IOWR('b', NGM_HCI_NODE_GET_NEIGHBOR_CACHE, \
		struct ng_btsocket_hci_raw_node_neighbor_cache)

/* Get connection list */
struct ng_btsocket_hci_raw_con_list {
	u_int32_t		 num_connections;
	ng_hci_node_con_ep	*connections;
};
#define SIOC_HCI_RAW_NODE_GET_CON_LIST \
	_IOWR('b', NGM_HCI_NODE_GET_CON_LIST, \
		struct ng_btsocket_hci_raw_con_list)

/* Get/Set link policy settings mask */
struct ng_btsocket_hci_raw_node_link_policy_mask {
	ng_hci_node_link_policy_mask_ep	policy_mask;
};
#define SIOC_HCI_RAW_NODE_GET_LINK_POLICY_MASK \
	_IOWR('b', NGM_HCI_NODE_GET_LINK_POLICY_SETTINGS_MASK, \
		struct ng_btsocket_hci_raw_node_link_policy_mask)
#define SIOC_HCI_RAW_NODE_SET_LINK_POLICY_MASK \
	_IOWR('b', NGM_HCI_NODE_SET_LINK_POLICY_SETTINGS_MASK, \
		struct ng_btsocket_hci_raw_node_link_policy_mask)

/* Get/Set packet mask */
struct ng_btsocket_hci_raw_node_packet_mask {
	ng_hci_node_packet_mask_ep	packet_mask;
};
#define SIOC_HCI_RAW_NODE_GET_PACKET_MASK \
	_IOWR('b', NGM_HCI_NODE_GET_PACKET_MASK, \
		struct ng_btsocket_hci_raw_node_packet_mask)
#define SIOC_HCI_RAW_NODE_SET_PACKET_MASK \
	_IOWR('b', NGM_HCI_NODE_SET_PACKET_MASK, \
		struct ng_btsocket_hci_raw_node_packet_mask)

/* Get/Set role switch */
struct ng_btsocket_hci_raw_node_role_switch {
	ng_hci_node_role_switch_ep	role_switch;
};
#define SIOC_HCI_RAW_NODE_GET_ROLE_SWITCH \
	_IOWR('b', NGM_HCI_NODE_GET_ROLE_SWITCH, \
		struct ng_btsocket_hci_raw_node_role_switch)
#define SIOC_HCI_RAW_NODE_SET_ROLE_SWITCH \
	_IOWR('b', NGM_HCI_NODE_SET_ROLE_SWITCH, \
		struct ng_btsocket_hci_raw_node_role_switch)

/* Get list of HCI node names */
struct ng_btsocket_hci_raw_node_list_names {
	u_int32_t	 num_names;
	struct nodeinfo	*names;
};
#define SIOC_HCI_RAW_NODE_LIST_NAMES \
	_IOWR('b', NGM_HCI_NODE_LIST_NAMES, \
		struct ng_btsocket_hci_raw_node_list_names)

/*
 * XXX FIXME: probably does not belong here
 * Bluetooth version of struct sockaddr for SCO sockets (SEQPACKET)
 */

struct sockaddr_sco {
	u_char		sco_len;	/* total length */
	u_char		sco_family;	/* address family */
	bdaddr_t	sco_bdaddr;	/* address */
};

/* SCO socket options */
#define SOL_SCO		0x0209		/* socket options level */

#define SO_SCO_MTU	1		/* get sockets mtu */
#define SO_SCO_CONNINFO	2		/* get HCI connection handle */

/*
 * XXX FIXME: probably does not belong here
 * Bluetooth version of struct sockaddr for L2CAP sockets (RAW and SEQPACKET)
 */

struct sockaddr_l2cap_compat {
	u_char		l2cap_len;	/* total length */
	u_char		l2cap_family;	/* address family */
	u_int16_t	l2cap_psm;	/* PSM (Protocol/Service Multiplexor) */
	bdaddr_t	l2cap_bdaddr;	/* address */
};

struct sockaddr_l2cap {
	u_char		l2cap_len;	/* total length */
	u_char		l2cap_family;	/* address family */
	u_int16_t	l2cap_psm;	/* PSM (Protocol/Service Multiplexor) */
	bdaddr_t	l2cap_bdaddr;	/* address */
	u_int16_t	l2cap_cid;      /*cid*/
	u_int8_t	l2cap_bdaddr_type; /*address type*/
};


#if !defined(L2CAP_SOCKET_CHECKED) && !defined(_KERNEL)
#warning "Make sure new member of socket address initialized"
#endif


/* L2CAP socket options */
#define SOL_L2CAP		0x1609	/* socket option level */

#define SO_L2CAP_IMTU		1	/* get/set incoming MTU */
#define SO_L2CAP_OMTU		2	/* get outgoing (peer incoming) MTU */
#define SO_L2CAP_IFLOW		3	/* get incoming flow spec. */
#define SO_L2CAP_OFLOW		4	/* get/set outgoing flow spec. */
#define SO_L2CAP_FLUSH		5	/* get/set flush timeout */
#define SO_L2CAP_ENCRYPTED      6      /* get/set whether wait for encryptin on connect */
/*
 * Raw L2CAP sockets ioctl's
 */

/* Ping */
struct ng_btsocket_l2cap_raw_ping {
	u_int32_t		 result;
	u_int32_t		 echo_size;
	u_int8_t		*echo_data;
};
#define SIOC_L2CAP_L2CA_PING \
	_IOWR('b', NGM_L2CAP_L2CA_PING, \
		struct ng_btsocket_l2cap_raw_ping)

/* Get info */
struct ng_btsocket_l2cap_raw_get_info {
	u_int32_t		 result;
	u_int32_t		 info_type;
	u_int32_t		 info_size;
	u_int8_t		*info_data;
};
#define SIOC_L2CAP_L2CA_GET_INFO \
	_IOWR('b', NGM_L2CAP_L2CA_GET_INFO, \
		struct ng_btsocket_l2cap_raw_get_info)

/* Get flags */
struct ng_btsocket_l2cap_raw_node_flags {
	ng_l2cap_node_flags_ep	flags;
};
#define SIOC_L2CAP_NODE_GET_FLAGS \
	_IOWR('b', NGM_L2CAP_NODE_GET_FLAGS, \
		struct ng_btsocket_l2cap_raw_node_flags)

/* Get/Set debug level */
struct ng_btsocket_l2cap_raw_node_debug {
	ng_l2cap_node_debug_ep	debug;
};
#define SIOC_L2CAP_NODE_GET_DEBUG \
	_IOWR('b', NGM_L2CAP_NODE_GET_DEBUG, \
		struct ng_btsocket_l2cap_raw_node_debug)
#define SIOC_L2CAP_NODE_SET_DEBUG \
	_IOWR('b', NGM_L2CAP_NODE_SET_DEBUG, \
		struct ng_btsocket_l2cap_raw_node_debug)

/* Get connection list */
struct ng_btsocket_l2cap_raw_con_list {
	u_int32_t		 num_connections;
	ng_l2cap_node_con_ep	*connections;
};
#define SIOC_L2CAP_NODE_GET_CON_LIST \
	_IOWR('b', NGM_L2CAP_NODE_GET_CON_LIST, \
		struct ng_btsocket_l2cap_raw_con_list)

/* Get channel list */
struct ng_btsocket_l2cap_raw_chan_list {
	u_int32_t		 num_channels;
	ng_l2cap_node_chan_ep	*channels;
};
#define SIOC_L2CAP_NODE_GET_CHAN_LIST \
	_IOWR('b', NGM_L2CAP_NODE_GET_CHAN_LIST, \
		struct ng_btsocket_l2cap_raw_chan_list)

/* Get/Set auto disconnect timeout */
struct ng_btsocket_l2cap_raw_auto_discon_timo
{
	ng_l2cap_node_auto_discon_ep	timeout;
};
#define SIOC_L2CAP_NODE_GET_AUTO_DISCON_TIMO \
	_IOWR('b', NGM_L2CAP_NODE_GET_AUTO_DISCON_TIMO, \
		struct ng_btsocket_l2cap_raw_auto_discon_timo)
#define SIOC_L2CAP_NODE_SET_AUTO_DISCON_TIMO \
	_IOWR('b', NGM_L2CAP_NODE_SET_AUTO_DISCON_TIMO, \
		struct ng_btsocket_l2cap_raw_auto_discon_timo)

/*
 * XXX FIXME: probably does not belong here
 * Bluetooth version of struct sockaddr for RFCOMM sockets (STREAM)
 */

struct sockaddr_rfcomm {
	u_char		rfcomm_len;	/* total length */
	u_char		rfcomm_family;	/* address family */
	bdaddr_t	rfcomm_bdaddr;	/* address */
	u_int8_t	rfcomm_channel;	/* channel */
};

/* Flow control information */
struct ng_btsocket_rfcomm_fc_info {
	u_int8_t	lmodem;		/* modem signals (local) */
	u_int8_t	rmodem;		/* modem signals (remote) */
	u_int8_t	tx_cred;	/* TX credits */
	u_int8_t	rx_cred;	/* RX credits */
	u_int8_t	cfc;		/* credit flow control */
	u_int8_t	reserved;
};

/* STREAM RFCOMM socket options */
#define SOL_RFCOMM		0x0816	/* socket options level */

#define SO_RFCOMM_MTU		1	/* get channel MTU */
#define SO_RFCOMM_FC_INFO	2	/* get flow control information */

/* 
 * Netgraph node type name and cookie 
 */

#define	NG_BTSOCKET_HCI_RAW_NODE_TYPE	"btsock_hci_raw"
#define	NG_BTSOCKET_L2CAP_RAW_NODE_TYPE	"btsock_l2c_raw"
#define	NG_BTSOCKET_L2CAP_NODE_TYPE	"btsock_l2c"
#define	NG_BTSOCKET_SCO_NODE_TYPE	"btsock_sco"

/*
 * Debug levels 
 */

#define NG_BTSOCKET_ALERT_LEVEL	1
#define NG_BTSOCKET_ERR_LEVEL	2
#define NG_BTSOCKET_WARN_LEVEL	3
#define NG_BTSOCKET_INFO_LEVEL	4

#endif /* _NETGRAPH_BTSOCKET_H_ */

