/*	$OpenBSD: ofp.h,v 1.15 2023/04/11 00:45:09 jsg Exp $	*/

/*
 * Copyright (c) 2013-2016 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2016 Kazuya GODA <goda@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_OFP_H_
#define _NET_OFP_H_

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#define OFP_IFNAMSIZ	16	/* on-wire (not IF_NAMSIZE) */
#define OFP_ALIGNMENT	8	/* OFP alignment */
#define OFP_ALIGN(_x)	(((_x) + (OFP_ALIGNMENT - 1)) & ~(OFP_ALIGNMENT - 1))

struct ofp_header {
	uint8_t		 oh_version;		/* OpenFlow version */
	uint8_t		 oh_type;		/* message type */
	uint16_t	 oh_length;		/* message length */
	uint32_t	 oh_xid;		/* transaction Id */
} __packed;

/* OpenFlow version */
#define OFP_V_0				0x00	/* OpenFlow 0.0 */
#define OFP_V_1_0			0x01	/* OpenFlow 1.0 */
#define OFP_V_1_1			0x02	/* OpenFlow 1.1 */
#define OFP_V_1_2			0x03	/* OpenFlow 1.2 */
#define OFP_V_1_3			0x04	/* OpenFlow 1.3 */
#define OFP_V_1_4			0x05	/* OpenFlow 1.4 */
#define OFP_V_1_5			0x06	/* OpenFlow 1.5 */

/* OpenFlow message type */
#define OFP_T_HELLO			0	/* Hello */
#define OFP_T_ERROR			1	/* Error */
#define OFP_T_ECHO_REQUEST		2	/* Echo Request */
#define OFP_T_ECHO_REPLY		3	/* Echo Reply */
#define OFP_T_EXPERIMENTER		4	/* Vendor/Experimenter */
#define OFP_T_FEATURES_REQUEST		5	/* Features Request (switch) */
#define OFP_T_FEATURES_REPLY		6	/* Features Reply (switch) */
#define OFP_T_GET_CONFIG_REQUEST	7	/* Get Config Request (switch) */
#define OFP_T_GET_CONFIG_REPLY		8	/* Get Config Reply (switch) */
#define OFP_T_SET_CONFIG		9	/* Set Config (switch) */
#define OFP_T_PACKET_IN			10	/* Packet In (async) */
#define OFP_T_FLOW_REMOVED		11	/* Flow Removed (async) */
#define OFP_T_PORT_STATUS		12	/* Port Status (async) */
#define OFP_T_PACKET_OUT		13	/* Packet Out (controller) */
#define OFP_T_FLOW_MOD			14	/* Flow Mod (controller) */
#define OFP_T_GROUP_MOD			15	/* Group Mod (controller) */
#define OFP_T_PORT_MOD			16	/* Port Mod (controller) */
#define OFP_T_TABLE_MOD			17	/* Table Mod (controller) */
#define OFP_T_MULTIPART_REQUEST		18	/* Multipart Message Request */
#define OFP_T_MULTIPART_REPLY		19	/* Multipart Message Request */
#define OFP_T_BARRIER_REQUEST		20	/* Barrier Request */
#define OFP_T_BARRIER_REPLY		21	/* Barrier Reply */
#define OFP_T_QUEUE_GET_CONFIG_REQUEST	22	/* Queue Get Config Request */
#define OFP_T_QUEUE_GET_CONFIG_REPLY	23	/* Queue Get Config Reply */
#define OFP_T_ROLE_REQUEST		24	/* Role Request */
#define OFP_T_ROLE_REPLY		25	/* Role Reply */
#define OFP_T_GET_ASYNC_REQUEST		26	/* Get Async Request */
#define OFP_T_GET_ASYNC_REPLY		27	/* Get Async Reply */
#define OFP_T_SET_ASYNC			28	/* Set Async */
#define OFP_T_METER_MOD			29	/* Meter Mod */
#define OFP_T_TYPE_MAX			30

/* OpenFlow Hello Message */
struct ofp_hello_element_header {
	uint16_t	he_type;
	uint16_t	he_length;
} __packed;

#define OFP_HELLO_T_VERSION_BITMAP	1	/* Supported version bitmap */

struct ofp_hello_element_versionbitmap {
	uint16_t	hev_type;
	uint16_t	hev_length;
} __packed;

/* Ports */
#define OFP_PORT_MAX		0xffffff00	/* Maximum number of physical ports */
#define	OFP_PORT_INPUT		0xfffffff8	/* Send back to input port */
#define OFP_PORT_FLOWTABLE	0xfffffff9	/* Perform actions in flow table */
#define OFP_PORT_NORMAL		0xfffffffa	/* Let switch decide */
#define OFP_PORT_FLOOD		0xfffffffb	/* All non-block ports except input */
#define OFP_PORT_ALL		0xfffffffc	/* All ports except input */
#define OFP_PORT_CONTROLLER	0xfffffffd	/* Send to controller */
#define OFP_PORT_LOCAL		0xfffffffe	/* Local virtual OpenFlow port */
#define OFP_PORT_ANY		0xffffffff	/* No port */

/* Switch Config Message (reply) */
struct ofp_switch_config {
	struct ofp_header	cfg_oh;		/* OpenFlow header */
	uint16_t		cfg_flags;	/* Configuration flags */
	uint16_t		cfg_miss_send_len; /* Max bytes from datapath */
} __packed;

/* Switch Config */
#define OFP_CONFIG_FRAG_NORMAL	0x0000		/* No special frag handling */
#define OFP_CONFIG_FRAG_DROP	0x0001		/* Drop fragments */
#define OFP_CONFIG_FRAG_REASM	0x0002		/* Reassemble fragments */
#define OFP_CONFIG_FRAG_MASK	0x0003		/* Fragment mask */

/* Switch port description */
struct ofp_switch_port {
	uint32_t	swp_number;		/* Switch port number */
	uint8_t		swp_pad[4];		/* Padding */
	uint8_t		swp_macaddr[ETHER_ADDR_LEN]; /* Port MAC address */
	uint8_t		swp_pad2[2];		/* Padding */
	char		swp_name[OFP_IFNAMSIZ];	/* Switch port name */
	uint32_t	swp_config;		/* Configuration flags */
	uint32_t	swp_state;		/* State flags */
	uint32_t	swp_cur;		/* Current features */
	uint32_t	swp_advertised;		/* Advertised by the port */
	uint32_t	swp_supported;		/* Supported by the port */
	uint32_t	swp_peer;		/* Advertised by peer */
	uint32_t	swp_cur_speed;		/* Current port speed in Kbps*/
	uint32_t	swp_max_speed;		/* Support port max speed in Kbps*/
} __packed;

/* Physical port configuration */
#define OFP_PORTCONFIG_PORT_DOWN	0x1	/* Port is down */
#define OFP_PORTCONFIG_NO_STP		0x2	/* Disable STP on port */
#define OFP_PORTCONFIG_NO_RECV		0x4	/* Drop everything except STP */
#define OFP_PORTCONFIG_NO_RECV_STP	0x8	/* Drop received STP */
#define OFP_PORTCONFIG_NO_FLOOD		0x10	/* Do not flood to this port */
#define OFP_PORTCONFIG_NO_FWD		0x20	/* Drop packets to port */
#define OFP_PORTCONFIG_NO_PACKET_IN	0x40	/* NO PACKET_IN on port */

/* Physical port state */
#define OFP_PORTSTATE_LINK_DOWN		0x1	/* Link not active */
#define OFP_PORTSTATE_STP_LISTEN	0x000	/* Not learning or forwarding */
#define OFP_PORTSTATE_STP_LEARN		0x100	/* Learning but not forwarding */
#define OFP_PORTSTATE_STP_FORWARD	0x200	/* Learning and forwarding */
#define OFP_PORTSTATE_STP_BLOCK		0x300	/* Not part of spanning tree */
#define OFP_PORTSTATE_STP_MASK		0x300	/* Spanning tree values */

/* Physical port media types */
#define OFP_PORTMEDIA_10MB_HD		0x1	/* 10 Mb half-duplex */
#define OFP_PORTMEDIA_10MB_FD		0x2	/* 10 Mb full-duplex */
#define OFP_PORTMEDIA_100MB_HD		0x4	/* 100 Mb half-duplex */
#define OFP_PORTMEDIA_100MB_FD		0x8	/* 100 Mb full-duplex */
#define OFP_PORTMEDIA_1GB_HD		0x10    /* 1 Gb half-duplex */
#define OFP_PORTMEDIA_1GB_FD		0x20    /* 1 Gb full-duplex */
#define OFP_PORTMEDIA_10GB_FD		0x40    /* 10 Gb full-duplex */
#define OFP_PORTMEDIA_COPPER		0x80    /* Copper */
#define OFP_PORTMEDIA_FIBER		0x100   /* Fiber */
#define OFP_PORTMEDIA_AUTONEG		0x200   /* Auto-negotiation */
#define OFP_PORTMEDIA_PAUSE		0x400   /* Pause */
#define OFP_PORTMEDIA_PAUSE_ASYM	0x800	/* Asymmetric pause */

/* Switch Features Message (reply) */
struct ofp_switch_features {
	struct ofp_header	swf_oh;		/* OpenFlow header */
	uint64_t		swf_datapath_id; /* Datapath unique ID */
	uint32_t		swf_nbuffers;	/* Max packets buffered */
	uint8_t			swf_ntables;	/* Number of supported tables */
	uint8_t			swf_aux_id;	/* Identify auxiliary connections */
	uint8_t			swf_pad[2];	/* Align to 64 bits */
	uint32_t		swf_capabilities; /* Capability flags */
	uint32_t		swf_actions;	/* Supported action flags */
} __packed;

/* Switch capabilities */
#define OFP_SWCAP_FLOW_STATS		0x1	/* Flow statistics */
#define OFP_SWCAP_TABLE_STATS		0x2	/* Table statistics */
#define OFP_SWCAP_PORT_STATS		0x4	/* Port statistics */
#define OFP_SWCAP_GROUP_STATS		0x8	/* Group statistics */
#define OFP_SWCAP_IP_REASM		0x20	/* Can reassemble IP frags */
#define OFP_SWCAP_QUEUE_STATS		0x40	/* Queue statistics */
#define OFP_SWCAP_ARP_MATCH_IP		0x80	/* Match IP addresses in ARP pkts */
#define OFP_SWCAP_PORT_BLOCKED		0x100	/* Switch will block ports */

/* Flow matching */
struct ofp_match {
	uint16_t	om_type;
	uint16_t	om_length;
} __packed;

/* Flow matching type */
#define OFP_MATCH_STANDARD		0	/* Standard match deprecated */
#define OFP_MATCH_OXM			1	/* OpenFlow Extensible Match */

/* Packet-In Message */
struct ofp_packet_in {
	struct ofp_header	pin_oh;		/* OpenFlow header */
	uint32_t		pin_buffer_id;
	uint16_t		pin_total_len;
	uint8_t			pin_reason;
	uint8_t			pin_table_id;
	uint64_t		pin_cookie;
	struct ofp_match	pin_match;
} __packed;

/* Reason */
#define	OFP_PKTIN_REASON_NO_MATCH	0	/* No matching flow */
#define	OFP_PKTIN_REASON_ACTION		1	/* Explicit output */
#define	OFP_PKTIN_REASON_TTL		2	/* Packet has invalid TTL */

/* Flow Instruction */
struct ofp_instruction {
	uint16_t	i_type;
	uint16_t	i_len;
} __packed;

/* Instruction types */
#define OFP_INSTRUCTION_T_GOTO_TABLE	1	/* Goto-Table */
#define OFP_INSTRUCTION_T_WRITE_META	2	/* Write-Metadata */
#define OFP_INSTRUCTION_T_WRITE_ACTIONS	3	/* Write-Actions */
#define OFP_INSTRUCTION_T_APPLY_ACTIONS	4	/* Apply-Actions */
#define OFP_INSTRUCTION_T_CLEAR_ACTIONS	5	/* Clear-Actions */
#define OFP_INSTRUCTION_T_METER		6	/* Meter */
#define OFP_INSTRUCTION_T_EXPERIMENTER	0xffff	/* Experimenter */

/* Write-Metadata instruction */
struct ofp_instruction_write_metadata {
	uint16_t	iwm_type;
	uint16_t	iwm_len;
	uint8_t		iwm_pad[4];
	uint64_t	iwm_metadata;
	uint64_t	iwm_metadata_mask;
} __packed;

/* Goto-Table instruction */
struct ofp_instruction_goto_table {
	uint16_t	igt_type;
	uint16_t	igt_len;
	uint8_t		igt_table_id;
	uint8_t		igt_pad[3];
} __packed;

/* Apply-Actions instruction */
struct ofp_instruction_actions {
	uint16_t	ia_type;
	uint16_t	ia_len;
	uint8_t		ia_pad[4];
} __packed;

/* Meter instruction */
struct ofp_instruction_meter {
	uint16_t	im_type;
	uint16_t	im_len;
	uint32_t	im_meter_id;
} __packed;

/* Experimenter instruction */
struct ofp_instruction_experimenter {
	uint16_t	ie_type;
	uint16_t	ie_len;
	uint32_t	ie_experimenter;
} __packed;

/* Actions */
#define OFP_ACTION_OUTPUT		0	/* Output to switch port */
#define OFP_ACTION_COPY_TTL_OUT		11	/* Copy TTL outwards */
#define OFP_ACTION_COPY_TTL_IN		12	/* Copy TTL inwards */
#define OFP_ACTION_SET_MPLS_TTL		15	/* MPLS TTL */
#define OFP_ACTION_DEC_MPLS_TTL		16	/* Decrement MPLS TTL */
#define OFP_ACTION_PUSH_VLAN		17	/* Push a new VLAN tag */
#define OFP_ACTION_POP_VLAN		18	/* Pop the outer VLAN tag */
#define OFP_ACTION_PUSH_MPLS		19	/* Push a new MPLS tag */
#define OFP_ACTION_POP_MPLS		20	/* Pop the outer MPLS tag */
#define OFP_ACTION_SET_QUEUE		21	/* Set queue id when outputting to a port */
#define OFP_ACTION_GROUP		22	/* Apply group */
#define OFP_ACTION_SET_NW_TTL		23	/* Set IP TTL */
#define OFP_ACTION_DEC_NW_TTL		24	/* Decrement IP TTL */
#define OFP_ACTION_SET_FIELD		25	/* Set a header field using OXM TLV format */
#define OFP_ACTION_PUSH_PBB		26	/* Push a new PBB service tag (I-TAG) */
#define OFP_ACTION_POP_PBB		27	/* Pop the outer PBB service tag (I-TAG) */
#define OFP_ACTION_EXPERIMENTER		0xffff	/* Vendor-specific action */

/* Action Header */
struct ofp_action_header {
	uint16_t	ah_type;
	uint16_t	ah_len;
	uint32_t	ah_pad;
} __packed;

/* Output Action */
struct ofp_action_output {
	uint16_t	ao_type;
	uint16_t	ao_len;
	uint32_t	ao_port;
	uint16_t	ao_max_len;
	uint8_t		ao_pad[6];
} __packed;

/* Buffer configuration */
#define OFP_CONTROLLER_MAXLEN_MAX	0xffe5	/* maximum buffer length */
#define OFP_CONTROLLER_MAXLEN_NO_BUFFER	0xffff	/* don't do any buffering */

struct ofp_action_mpls_ttl {
	uint16_t	amt_type;
	uint16_t	amt_len;
	uint8_t		amt_ttl;
	uint8_t		amt_pad[3];
} __packed;

struct ofp_action_push {
	uint16_t	ap_type;
	uint16_t	ap_len;
	uint16_t	ap_ethertype;
	uint8_t		ap_pad[2];
} __packed;

struct ofp_action_pop_mpls {
	uint16_t	apm_type;
	uint16_t	apm_len;
	uint16_t	apm_ethertype;
	uint8_t		apm_pad[2];
} __packed;

struct ofp_action_group {
	uint16_t	ag_type;
	uint16_t	ag_len;
	uint32_t	ag_group_id;
} __packed;

struct ofp_action_nw_ttl {
	uint16_t	ant_type;
	uint16_t	ant_len;
	uint8_t		ant_ttl;
	uint8_t		ant_pad[3];
} __packed;

struct ofp_action_set_field {
	uint16_t	asf_type;
	uint16_t	asf_len;
	uint8_t		asf_field[4];
} __packed;

struct ofp_action_set_queue {
	uint16_t	asq_type;
	uint16_t	asq_len;
	uint32_t	asq_queue_id;
} __packed;

/* Packet-Out Message */
struct ofp_packet_out {
	struct ofp_header	pout_oh;	/* OpenFlow header */
	uint32_t		pout_buffer_id;
	uint32_t		pout_in_port;
	uint16_t		pout_actions_len;
	uint8_t			pout_pad[6];
	struct ofp_action_header pout_actions[0];
	/* Followed by optional packet data if buffer_id == 0xffffffff */
} __packed;

/* Special buffer id */
#define OFP_PKTOUT_NO_BUFFER		0xffffffff	/* No buffer id */

/* Flow match fields for basic class */
#define OFP_XM_T_IN_PORT		0	/* Switch input port */
#define OFP_XM_T_IN_PHY_PORT		1	/* Switch physical input port */
#define OFP_XM_T_META			2	/* Metadata passed between tables */
#define OFP_XM_T_ETH_DST		3	/* Ethernet destination address */
#define OFP_XM_T_ETH_SRC		4	/* Ethernet source address */
#define OFP_XM_T_ETH_TYPE		5	/* Ethernet frame type */
#define OFP_XM_T_VLAN_VID		6	/* VLAN id */
#define OFP_XM_T_VLAN_PCP		7	/* VLAN priority */
#define OFP_XM_T_IP_DSCP		8	/* IP DSCP (6 bits in ToS field) */
#define OFP_XM_T_IP_ECN			9	/* IP ECN (2 bits in ToS field) */
#define OFP_XM_T_IP_PROTO		10	/* IP protocol */
#define OFP_XM_T_IPV4_SRC		11	/* IPv4 source address */
#define OFP_XM_T_IPV4_DST		12	/* IPv4 destination address */
#define OFP_XM_T_TCP_SRC		13	/* TCP source port */
#define OFP_XM_T_TCP_DST		14	/* TCP destination port */
#define OFP_XM_T_UDP_SRC		15	/* UDP source port */
#define OFP_XM_T_UDP_DST		16	/* UDP destination port */
#define OFP_XM_T_SCTP_SRC		17	/* SCTP source port */
#define OFP_XM_T_SCTP_DST		18	/* SCTP destination port */
#define OFP_XM_T_ICMPV4_TYPE		19	/* ICMP type */
#define OFP_XM_T_ICMPV4_CODE		20	/* ICMP code */
#define OFP_XM_T_ARP_OP			21	/* ARP opcode */
#define OFP_XM_T_ARP_SPA		22	/* ARP source IPv4 address */
#define OFP_XM_T_ARP_TPA		23	/* ARP target IPv4 address */
#define OFP_XM_T_ARP_SHA		24	/* ARP source hardware address */
#define OFP_XM_T_ARP_THA		25	/* ARP target hardware address */
#define OFP_XM_T_IPV6_SRC		26	/* IPv6 source address */
#define OFP_XM_T_IPV6_DST		27	/* IPv6 destination address */
#define OFP_XM_T_IPV6_FLABEL		28	/* IPv6 Flow Label */
#define OFP_XM_T_ICMPV6_TYPE		29	/* ICMPv6 type */
#define OFP_XM_T_ICMPV6_CODE		30	/* ICMPv6 code */
#define OFP_XM_T_IPV6_ND_TARGET		31	/* Target address for ND */
#define OFP_XM_T_IPV6_ND_SLL		32	/* Source link-layer for ND */
#define OFP_XM_T_IPV6_ND_TLL		33	/* Target link-layer for ND */
#define OFP_XM_T_MPLS_LABEL		34	/* MPLS label */
#define OFP_XM_T_MPLS_TC		35	/* MPLS TC */
#define OFP_XM_T_MPLS_BOS		36	/* MPLS BoS bit */
#define OFP_XM_T_PBB_ISID		37	/* PBB I-SID */
#define OFP_XM_T_TUNNEL_ID		38	/* Logical Port Metadata */
#define OFP_XM_T_IPV6_EXTHDR		39	/* IPv6 Extension Header pseudo-field */
#define OFP_XM_T_MAX			40

/* Flow match fields for nxm1 class */
#define OFP_XM_NXMT_TUNNEL_ID		38	/* Tunnel Logical Port Metadata */
#define OFP_XM_NXMT_TUNNEL_IPV4_SRC	31	/* Tunnel IPv4 source address */
#define OFP_XM_NXMT_TUNNEL_IPV4_DST	32	/* Tunnel IPv4 destination address */
#define OFP_XM_NXMT_TUNNEL_IPV6_SRC	109	/* Tunnel IPv6 source address */
#define OFP_XM_NXMT_TUNNEL_IPV6_DST	110	/* Tunnel IPv6 destination address */

/* OXM class */
#define OFP_OXM_C_NXM_0			0x0000	/* NXM 0 */
#define OFP_OXM_C_NXM_1			0x0001	/* NXM 1 */
#define OFP_OXM_C_OPENFLOW_BASIC	0x8000	/* OpenFlow Basic */
#define OFP_OXM_C_OPENFLOW_EXPERIMENTER	0xffff	/* OpenFlow Experimenter */

/* VLAN matching flag */
#define OFP_XM_VID_PRESENT		0x1000	/* VLAN ID present */
#define OFP_XM_VID_NONE			0x0000	/* No VLAN ID */

/* IPv6 Extension header pseudo-field flags */
#define OFP_XM_IPV6_EXTHDR_NONEXT	0x0001 /* "No next header" encountered */
#define OFP_XM_IPV6_EXTHDR_ESP		0x0002 /* Encrypted Sec Payload header present */
#define OFP_XM_IPV6_EXTHDR_AUTH		0x0004 /* Authentication header present. */
#define OFP_XM_IPV6_EXTHDR_DEST		0x0008 /* 1 or 2 dest headers present. */
#define OFP_XM_IPV6_EXTHDR_FRAG		0x0010 /* Fragment header present. */
#define OFP_XM_IPV6_EXTHDR_ROUTER	0x0020 /* Router header present. */
#define OFP_XM_IPV6_EXTHDR_HOP		0x0040 /* Hop-by-hop header present. */
#define OFP_XM_IPV6_EXTHDR_UNREP	0x0080 /* Unexpected repeats encountered. */
#define OFP_XM_IPV6_EXTHDR_UNSEQ	0x0100 /* Unexpected sequencing encountered. */

struct ofp_ox_match {
	uint16_t	oxm_class;
	uint8_t		oxm_fh;
	uint8_t		oxm_length;
	uint8_t		oxm_value[0];
} __packed;

#define OFP_OXM_GET_FIELD(o)	(((o)->oxm_fh) >> 1)
#define OFP_OXM_GET_HASMASK(o)	(((o)->oxm_fh) & 0x1)
#define OFP_OXM_SET_FIELD(o, t)	(((o)->oxm_fh) = ((t) << 1))
#define OFP_OXM_SET_HASMASK(o)	(((o)->oxm_fh) |= 0x1)

/* Flow modification commands */
#define OFP_FLOWCMD_ADD			0	/* Add new flow */
#define OFP_FLOWCMD_MODIFY		1	/* Modify flow */
#define OFP_FLOWCMD_MODIFY_STRICT	2	/* Modify flow w/o wildcard */
#define OFP_FLOWCMD_DELETE		3	/* Delete flow */
#define OFP_FLOWCMD_DELETE_STRICT	4	/* Delete flow w/o wildcard */

/* Flow modification flags */
#define OFP_FLOWFLAG_SEND_FLOW_REMOVED	0x0001	/* Send flow removed message */
#define OFP_FLOWFLAG_CHECK_OVERLAP	0x0002	/* Check flow overlap first */
#define OFP_FLOWFLAG_RESET_COUNTS	0x0004	/* Reset flow packet and byte counters */
#define OFP_FLOWFLAG_NO_PACKET_COUNTS	0x0008	/* Don't keep track of packet count */
#define OFP_FLOWFLAG_NO_BYTE_COUNTS	0x0010	/* Don't keep track of byte count */

/* Flow modification message */
struct ofp_flow_mod {
	struct ofp_header	fm_oh;		/* OpenFlow header */
	uint64_t		fm_cookie;
	uint64_t		fm_cookie_mask;
	uint8_t			fm_table_id;
	uint8_t			fm_command;
	uint16_t		fm_idle_timeout;
	uint16_t		fm_hard_timeout;
	uint16_t		fm_priority;
	uint32_t		fm_buffer_id;
	uint32_t		fm_out_port;
	uint32_t		fm_out_group;
	uint16_t		fm_flags;
	uint8_t			fm_pad[2];
	struct ofp_match	fm_match;
} __packed;

/* Flow removed reasons */
#define OFP_FLOWREM_REASON_IDLE_TIMEOUT	0	/* Flow idle time exceeded idle_timeout */
#define OFP_FLOWREM_REASON_HARD_TIMEOUT	1	/* Time exceeded hard_timeout */
#define OFP_FLOWREM_REASON_DELETE	2	/* Evicted by a DELETE flow mod */
#define OFP_FLOWREM_REASON_GROUP_DELETE	3	/* Group was removed */

/* Flow removed message */
struct ofp_flow_removed {
	struct ofp_header	fr_oh;
	uint64_t		fr_cookie;
	uint16_t		fr_priority;
	uint8_t			fr_reason;
	uint8_t			fr_table_id;
	uint32_t		fr_duration_sec;
	uint32_t		fr_duration_nsec;
	uint16_t		fr_idle_timeout;
	uint16_t		fr_hard_timeout;
	uint64_t		fr_packet_count;
	uint64_t		fr_byte_count;
	struct ofp_match	fr_match;
} __packed;

/* Error message */
struct ofp_error {
	struct ofp_header	err_oh;
	uint16_t		err_type;
	uint16_t		err_code;
	uint8_t			err_data[0];
	/* Followed by optional data */
} __packed;
#define OFP_ERRDATA_MAX			64

/* Error types */
#define OFP_ERRTYPE_HELLO_FAILED	0	/* Hello protocol failed */
#define OFP_ERRTYPE_BAD_REQUEST		1	/* Request was not understood */
#define OFP_ERRTYPE_BAD_ACTION		2	/* Error in action */
#define OFP_ERRTYPE_BAD_INSTRUCTION	3	/* Error in instruction list */
#define OFP_ERRTYPE_BAD_MATCH		4	/* Error in match */
#define OFP_ERRTYPE_FLOW_MOD_FAILED	5	/* Problem modifying flow */
#define OFP_ERRTYPE_GROUP_MOD_FAILED	6	/* Problem modifying group */
#define OFP_ERRTYPE_PORT_MOD_FAILED	7	/* Port mod request failed */
#define OFP_ERRTYPE_TABLE_MOD_FAILED	8	/* Port mod request failed */
#define OFP_ERRTYPE_QUEUE_OP_FAILED	9	/* Queue operation failed */
#define OFP_ERRTYPE_SWITCH_CFG_FAILED	10	/* Switch Config request failed */
#define OFP_ERRTYPE_ROLE_REQUEST_FAILED	11	/* Controller role request failed */
#define OFP_ERRTYPE_METER_MOD_FAILED	12	/* Error in meter */
#define OFP_ERRTYPE_TABLE_FEATURES_FAILED 13	/* Setting table features failed */
#define OFP_ERRTYPE_EXPERIMENTER	0xffff	/* Experimenter error message */

/* HELLO error codes */
#define OFP_ERRHELLO_INCOMPATIBLE	0	/* No compatible version */
#define OFP_ERRHELLO_EPERM		1	/* Permissions error */

/* REQUEST error codes */
#define OFP_ERRREQ_VERSION		0	/* Version not supported  */
#define OFP_ERRREQ_TYPE			1	/* Type not supported  */
#define OFP_ERRREQ_MULTIPART		2	/* Multipart type not supported */
#define OFP_ERRREQ_EXPERIMENTER		3	/* Experimenter id not supported */
#define OFP_ERRREQ_EXP_TYPE		4	/* Experimenter type not supported  */
#define OFP_ERRREQ_EPERM		5	/* Permission error */
#define OFP_ERRREQ_BAD_LEN		6	/* Wrong request length for type */
#define OFP_ERRREQ_BUFFER_EMPTY		7	/* Specified buffer has already been used */
#define OFP_ERRREQ_BUFFER_UNKNOWN	8	/* Specified buffer does not exist */
#define OFP_ERRREQ_TABLE_ID		9	/* Specified table-id invalid or does not exit */
#define OFP_ERRREQ_IS_SLAVE		10	/* Denied because controller is slave */
#define OFP_ERRREQ_PORT			11	/* Invalid port */
#define OFP_ERRREQ_PACKET		12	/* Invalid packet in packet-out */
#define OFP_ERRREQ_MULTIPART_OVERFLOW	13	/* Multipart overflowed the assigned buffer */

/* ACTION error codes */
#define OFP_ERRACTION_TYPE		0	/* Unknown or unsupported action type */
#define OFP_ERRACTION_LEN		1	/* Length problem in actions */
#define OFP_ERRACTION_EXPERIMENTER	2	/* Unknown experimenter id specified */
#define OFP_ERRACTION_EXP_TYPE		3	/* Unknown action for experimenter id */
#define OFP_ERRACTION_OUT_PORT		4	/* Problem validating output port */
#define OFP_ERRACTION_ARGUMENT		5	/* Bad action argument  */
#define OFP_ERRACTION_EPERM		6	/* Permission error */
#define OFP_ERRACTION_TOO_MANY		7	/* Can't handle this many actions */
#define OFP_ERRACTION_BAD_QUEUE		8	/* Problem validating output queue */
#define OFP_ERRACTION_BAD_OUT_GROUP	9	/* Invalid group id in forward action */
#define OFP_ERRACTION_MATCH_INCONSISTENT 10	/* Action can't apply or Set-Field failed */
#define OFP_ERRACTION_UNSUPPORTED_ORDER	11	/* Action order is unsupported for Apply-Actions */
#define OFP_ERRACTION_TAG		12	/* Actions uses an unsupported tag/encap */
#define OFP_ERRACTION_SET_TYPE		13	/* Unsupported type in SET_FIELD action */
#define OFP_ERRACTION_SET_LEN		14	/* Length problem in SET_FIELD action */
#define OFP_ERRACTION_SET_ARGUMENT	15	/* Bad argument in SET_FIELD action */

/* INSTRUCTION error codes */
#define OFP_ERRINST_UNKNOWN_INST	0	/* Unknown instruction */
#define OFP_ERRINST_UNSUPPORTED_INST	1	/* Switch or table does not support */
#define OFP_ERRINST_TABLE_ID		2	/* Invalid Table-ID specified */
#define OFP_ERRINST_UNSUPP_META		3	/* Metadata value unsupported by datapath */
#define OFP_ERRINST_UNSUPP_META_MASK	4	/* Metadata mask value unsupported by datapath */
#define OFP_ERRINST_BAD_EXPERIMENTER	5	/* Unknown experimenter id specified */
#define OFP_ERRINST_BAD_EXPERIMENTER_TYPE 6	/* Unknown instruction for experimenter id */
#define OFP_ERRINST_BAD_LEN		7	/* Length problem in instructions */
#define OFP_ERRINST_EPERM		8	/* Permissions error */

/* MATCH error codes */
#define OFP_ERRMATCH_BAD_TYPE		0	/* Unsupported match type */
#define OFP_ERRMATCH_BAD_LEN		1	/* Length problem in match */
#define OFP_ERRMATCH_BAD_TAG		2	/* Match uses an unsupported tag/encap */
#define OFP_ERRMATCH_BAD_DL_ADDR_MASK	3	/* Unsupported datalink addr mask */
#define OFP_ERRMATCH_BAD_NW_ADDR_MASK	4	/* Unsupported network addr mask */
#define OFP_ERRMATCH_BAD_WILDCARDS	5	/* Unsupported combination of fields */
#define OFP_ERRMATCH_BAD_FIELD		6	/* Unsupported field type in the match */
#define OFP_ERRMATCH_BAD_VALUE		7	/* Unsupported value in a match field */
#define OFP_ERRMATCH_BAD_MASK		8	/* Unsupported mask specified in match */
#define OFP_ERRMATCH_BAD_PREREQ		9	/* A prerequisite was not met */
#define OFP_ERRMATCH_DUP_FIELD		10	/* A field type was duplicated */
#define OFP_ERRMATCH_EPERM		11	/* Permissions error */

/* FLOW MOD error codes */
#define OFP_ERRFLOWMOD_UNKNOWN		0	/* Unknown */
#define OFP_ERRFLOWMOD_TABLE_FULL	1	/* Table is full */
#define OFP_ERRFLOWMOD_TABLE_ID		2	/* Invalid table id */
#define OFP_ERRFLOWMOD_OVERLAP		3	/* Overlapping flow */
#define OFP_ERRFLOWMOD_EPERM		4	/* Permissions error */
#define OFP_ERRFLOWMOD_BAD_TIMEOUT	5	/* non-zero idle/hard timeout */
#define OFP_ERRFLOWMOD_BAD_COMMAND	6	/* Unsupported or Unknown command */
#define OFP_ERRFLOWMOD_BAD_FLAGS	7	/* Unsupported or Unknown flags */

/* GROUP MOD error codes */
#define OFP_ERRGROUPMOD_GROUP_EXISTS	0	/* Already present group */
#define OFP_ERRGROUPMOD_INVALID_GROUP	1	/* Group specified is invalid */
#define OFP_ERRGROUPMOD_WEIGHT_UNSUPP	2	/* Switch does not support unequal load sharing */
#define OFP_ERRGROUPMOD_OUT_OF_GROUPS	3	/* The Group table is full */
#define OFP_ERRGROUPMOD_OUT_OF_BUCKETS	4	/* The maximum number of action buckets */
#define OFP_ERRGROUPMOD_CHAINING_UNSUPP	5	/* Switch does not support groups forwarding to groups */
#define OFP_ERRGROUPMOD_WATCH_UNSUPP	6	/* This group cannot watch the watch_port */
#define OFP_ERRGROUPMOD_LOOP		7	/* Group entry would cause a loop */
#define OFP_ERRGROUPMOD_UNKNOWN_GROUP	8	/* MODIFY attempted to modify a non-existent group */
#define OFP_ERRGROUPMOD_CHAINED_GROUP	9	/* Group not deleted because another group is forwarding to it */
#define OFP_ERRGROUPMOD_BAD_TYPE	10	/* Unsupported or unknown group type */
#define OFP_ERRGROUPMOD_BAD_COMMAND	11	/* Unsupported or unknown command */
#define OFP_ERRGROUPMOD_BAD_BUCKET	12	/* Error in bucket */
#define OFP_ERRGROUPMOD_BAD_WATCH	13	/* Error in watch port/group */
#define OFP_ERRGROUPMOD_EPERM		14	/* Permission error */

/* GROUP MOD message */
#define OFP_GROUPCMD_ADD		0	/* Add group */
#define OFP_GROUPCMD_MODIFY		1	/* Modify group */
#define OFP_GROUPCMD_DELETE		2	/* Delete group */

/* Group types */
#define OFP_GROUP_T_ALL			0	/* All (multicast/broadcast) group */
#define OFP_GROUP_T_SELECT		1	/* Select group */
#define OFP_GROUP_T_INDIRECT		2	/* Indirect group */
#define OFP_GROUP_T_FAST_FAILOVER	3	/* Fast failover group */

/* Special group identifiers */
#define OFP_GROUP_ID_MAX	0xffffff00	/* Last usable group number */
#define OFP_GROUP_ID_ALL	0xfffffffc	/* Represents all groups for delete command */
#define OFP_GROUP_ID_ANY	0xffffffff	/* Special wildcard: no group specified */

struct ofp_bucket {
	uint16_t		b_len;
	uint16_t		b_weight;
	uint32_t		b_watch_port;
	uint32_t		b_watch_group;
	uint8_t			b_pad[4];
	struct ofp_action_header b_actions[0];
} __packed;

struct ofp_group_mod {
	struct ofp_header	gm_oh;
	uint16_t		gm_command;
	uint8_t			gm_type;
	uint8_t			gm_pad;
	uint32_t		gm_group_id;
	struct ofp_bucket	gm_buckets[0];
} __packed;

struct ofp_multipart {
	struct ofp_header	mp_oh;
	uint16_t		mp_type;
	uint16_t		mp_flags;
	uint8_t			mp_pad[4];
} __packed;

#define OFP_MP_FLAG_REQ_MORE		1	/* More requests to follow */
#define OFP_MP_FLAG_REPLY_MORE		1	/* More replies to follow */

/* Multipart types */
#define OFP_MP_T_DESC			0	/* Description of the switch */
#define OFP_MP_T_FLOW			1	/* Individual flow statistics */
#define OFP_MP_T_AGGREGATE		2	/* Aggregate flow statistics */
#define OFP_MP_T_TABLE			3	/* Flow table statistics */
#define OFP_MP_T_PORT_STATS		4	/* Port statistics */
#define OFP_MP_T_QUEUE			5	/* Queue statistics for a port */
#define OFP_MP_T_GROUP			6	/* Group counter statistics */
#define OFP_MP_T_GROUP_DESC		7	/* Group description */
#define OFP_MP_T_GROUP_FEATURES		8	/* Group features */
#define OFP_MP_T_METER			9	/* Meter statistics */
#define OFP_MP_T_METER_CONFIG		10	/* Meter configuration */
#define OFP_MP_T_METER_FEATURES		11	/* Meter features */
#define OFP_MP_T_TABLE_FEATURES		12	/* Table features */
#define OFP_MP_T_PORT_DESC		13	/* Port description */
#define OFP_MP_T_EXPERIMENTER		0xffff	/* Experimenter extension */

#define OFP_DESC_STR_LEN		256
#define OFP_SERIAL_NUM_LEN		32

struct ofp_desc {
	char		d_mfr_desc[OFP_DESC_STR_LEN];
	char		d_hw_desc[OFP_DESC_STR_LEN];
	char		d_sw_desc[OFP_DESC_STR_LEN];
	char		d_serial_num[OFP_SERIAL_NUM_LEN];
	char		d_dp_desc[OFP_DESC_STR_LEN];
} __packed;

/* Flow stats request */
struct ofp_flow_stats_request {
	uint8_t			fsr_table_id;
	uint8_t			fsr_pad[3];
	uint32_t		fsr_out_port;
	uint32_t		fsr_out_group;
	uint8_t			fsr_pad2[4];
	uint64_t		fsr_cookie;
	uint64_t		fsr_cookie_mask;
	struct ofp_match	fsr_match;
} __packed;

/* Flow stats */
struct ofp_flow_stats {
	uint16_t		fs_length;
	uint8_t			fs_table_id;
	uint8_t			fs_pad;
	uint32_t		fs_duration_sec;
	uint32_t		fs_duration_nsec;
	uint16_t		fs_priority;
	uint16_t		fs_idle_timeout;
	uint16_t		fs_hard_timeout;
	uint16_t		fs_flags;
	uint8_t			fs_pad2[4];
	uint64_t		fs_cookie;
	uint64_t		fs_packet_count;
	uint64_t		fs_byte_count;
	struct ofp_match	fs_match;
} __packed;

/* Aggregate flow stats request */
struct ofp_aggregate_stats_request {
	uint8_t			asr_table_id;
	uint8_t			asr_pad[3];
	uint32_t		asr_out_port;
	uint32_t		asr_out_group;
	uint8_t			asr_pad2[4];
	uint64_t		asr_cookie;
	uint64_t		asr_cookie_mask;
	struct ofp_match	asr_match;
} __packed;

struct ofp_aggregate_stats {
	uint64_t	as_packet_count;
	uint64_t	as_byte_count;
	uint32_t	as_flow_count;
	uint8_t		as_pad[4];
} __packed;

/* Special table id */
#define OFP_TABLE_ID_MAX			0xfe	/* Last usable table */
#define OFP_TABLE_ID_ALL			0xff	/* Wildcard table */

struct ofp_table_stats {
	uint8_t		ts_table_id;
	uint8_t		ts_pad[3];
	uint32_t	ts_active_count;
	uint64_t	ts_lookup_count;
	uint64_t	ts_matched_count;
} __packed;

/* Table features */
#define OFP_TABLE_FEATPROP_INSTRUCTION		0	/* Instruction property */
#define OFP_TABLE_FEATPROP_INSTRUCTION_MISS	1	/* Instruction for table-miss  */
#define OFP_TABLE_FEATPROP_NEXT_TABLES		2	/* Next table property */
#define OFP_TABLE_FEATPROP_NEXT_TABLES_MISS	3	/* Next table for table-miss */
#define OFP_TABLE_FEATPROP_WRITE_ACTIONS	4	/* Write actions property */
#define OFP_TABLE_FEATPROP_WRITE_ACTIONS_MISS	5	/* Write actions for table-miss */
#define OFP_TABLE_FEATPROP_APPLY_ACTIONS	6	/* Apply actions property */
#define OFP_TABLE_FEATPROP_APPLY_ACTIONS_MISS	7	/* Apply actions for table-miss */
#define OFP_TABLE_FEATPROP_MATCH		8	/* Match property */
#define OFP_TABLE_FEATPROP_WILDCARDS		10	/* Wildcards property */
#define OFP_TABLE_FEATPROP_WRITE_SETFIELD	12	/* Write set-field property */
#define OFP_TABLE_FEATPROP_WRITE_SETFIELD_MISS	13	/* Write set-field for table-miss */
#define OFP_TABLE_FEATPROP_APPLY_SETFIELD	14	/* Apply set-field property */
#define OFP_TABLE_FEATPROP_APPLY_SETFIELD_MISS	15	/* Apply set-field for table-miss */
#define OFP_TABLE_FEATPROP_EXPERIMENTER		0xfffe	/* Experimenter property */
#define OFP_TABLE_FEATPROP_EXPERIMENTER_MISS	0xffff	/* Experimenter for table-miss */

#define OFP_TABLE_MAX_NAME_LEN			32

struct ofp_table_features {
	uint16_t	tf_length;
	uint8_t		tf_tableid;
	uint8_t		tf_pad[5];
	char		tf_name[OFP_TABLE_MAX_NAME_LEN];
	uint64_t	tf_metadata_match;
	uint64_t	tf_metadata_write;
	uint32_t	tf_config;
	uint32_t	tf_max_entries;
} __packed;

struct ofp_table_feature_property {
	uint16_t	tp_type;
	uint16_t	tp_length;
} __packed;

struct ofp_table_feature_property_instruction {
	uint16_t		tpi_type;
	uint16_t		tpi_length;
	struct ofp_instruction	tpi_instructions[0];
} __packed;

struct ofp_table_feature_property_next_tables {
	uint16_t	tpnt_type;
	uint16_t	tpnt_length;
	uint8_t		tpnt_tables[0];
} __packed;

struct ofp_table_feature_property_actions {
	uint16_t			tpa_type;
	uint16_t			tpa_length;
	struct ofp_action_header	tpa_actions[0];
} __packed;

struct ofp_table_feature_property_oxm {
	uint16_t	tpoxm_type;
	uint16_t	tpoxm_length;
	uint32_t	tpoxm_oxm[0];
} __packed;

struct ofp_table_feature_property_experimenter {
	uint16_t	tfpexp_type;
	uint16_t	tfpexp_length;
	uint32_t	tfpexp_experimenter;
	uint32_t	tfpexp_exp_type;
	uint32_t	tfpexp_experimenter_data[0];
} __packed;

struct ofp_port_stats {
	uint32_t	pt_port_no;
	uint8_t		pt_pad[4];
	uint64_t	pt_rx_packets;
	uint64_t	pt_tx_packets;
	uint64_t	pt_rx_bytes;
	uint64_t	pt_tx_bytes;
	uint64_t	pt_rx_dropped;
	uint64_t	pt_tx_dropped;
	uint64_t	pt_rx_errors;
	uint64_t	pt_tx_errors;
	uint64_t	pt_rx_frame_err;
	uint64_t	pt_rx_over_err;
	uint64_t	pt_rx_crc_err;
	uint64_t	pt_collision;
	uint32_t	pt_duration_sec;
	uint32_t	pt_duration_nsec;
} __packed;

/* Groups stats request */
struct ofp_group_stats_request {
	uint32_t	gsr_group_id;
	uint8_t		gsr_pad[4];
} __packed;

struct ofp_bucket_counter {
	uint64_t	gs_packet_count;
	uint64_t	gs_byte_count;
} __packed;

/* Group stats */
struct ofp_group_stats {
	uint16_t		gs_length;
	uint8_t			gs_pad[2];
	uint32_t		gs_group_id;
	uint32_t		gs_ref_count;
	uint8_t			gs_pad2[4];
	uint64_t		gs_packet_count;
	uint64_t		gs_byte_count;
	uint32_t		gs_duration_sec;
	uint32_t		gs_duration_nsec;
	struct ofp_bucket_counter gs_bucket_stats[0];
} __packed;

/* Group description */
struct ofp_group_desc {
	uint16_t		gd_length;
	uint8_t			gd_type;
	uint8_t			gd_pad;
	uint32_t		gd_group_id;
	struct ofp_bucket	gd_buckets[0];
} __packed;

/*
 * Implementation-specific definitions that are not part of the spec
 */

/* OpenFlow finite state machine */
enum ofp_state {
	OFP_STATE_CLOSED,
	OFP_STATE_HELLO_WAIT,
	OFP_STATE_FEATURE_WAIT,
	OFP_STATE_ESTABLISHED
};

/* Used by the bpf for DLT_OPENFLOW */
struct dlt_openflow_hdr {
	uint32_t	of_direction;
	uint64_t	of_datapath_id;
} __packed;

#define DLT_OPENFLOW_TO_SWITCH		1
#define DLT_OPENFLOW_TO_CONTROLLER	2

#endif /* _NET_OFP_H_ */
