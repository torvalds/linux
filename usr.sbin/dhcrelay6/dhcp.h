/*	$OpenBSD: dhcp.h,v 1.1 2017/03/17 14:45:16 rzalamena Exp $	*/

/*
 * Copyright (c) 2017 Rafael Zalamena <rzalamena@openbsd.org>
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

#ifndef _DHCP_H_
#define _DHCP_H_

/* Maximum DHCP packet size. */
#define DHCP_MTU_MAX		1500

/*
 * Enterprise numbers can be found at:
 * http://www.iana.org/assignments/enterprise-numbers
 */
#define OPENBSD_ENTERPRISENO	30155
#define ENTERPRISENO_LEN	sizeof(uint32_t)

/*
 * DHCPv6 definitions and structures defined by the protocol
 * specification (RFC 3315).
 *
 * The relay part of DHCPv6 is specified in section 20 of RFC 3315 and
 * in the RFC 6221.
 */

/*
 * RFC 3315 Section 5.1 Multicast Addresses:
 * All_DHCP_Relay_Agents_and_Servers: FF02::1:2
 * All_DHCP_Servers: FF05::1:3
 */
#define DHCP6_ADDR_RELAYSERVER		"FF02::1:2"
#define DHCP6_ADDR_SERVER		"FF05::1:3"

/* DHCPv6 client/server ports */
#define DHCP6_CLIENT_PORT		546
#define DHCP6_CLIENT_PORT_STR		"546"
#define DHCP6_SERVER_PORT		547
#define DHCP6_SERVER_PORT_STR		"547"

/* DHCPv6 message types. */
#define DHCP6_MT_SOLICIT		1
#define DHCP6_MT_ADVERTISE		2
#define DHCP6_MT_REQUEST		3
#define DHCP6_MT_CONFIRM		4
#define DHCP6_MT_RENEW			5
#define DHCP6_MT_REBIND			6
#define DHCP6_MT_REPLY			7
#define DHCP6_MT_RELEASE		8
#define DHCP6_MT_DECLINE		9
#define DHCP6_MT_RECONFIGURE		10
#define DHCP6_MT_INFORMATIONREQUEST	11
#define DHCP6_MT_RELAYFORW		12
#define DHCP6_MT_RELAYREPL		13

/* Maximum amount of hops limit by default. */
#define DHCP6_HOP_LIMIT			32

/* DHCPv6 option definitions. */
#define DHCP6_OPT_CLIENTID		1
#define DHCP6_OPT_SERVERID		2
/* IANA - Identity Association for Non-temporary Address */
#define DHCP6_OPT_IANA			3
/* IATA - Identity Association for Temporary Address */
#define DHCP6_OPT_IATA			4
/* IA Addr - Identity Association Address */
#define DHCP6_OPT_IAADDR		5
/* ORO - Option Request Option */
#define DHCP6_OPT_ORO			6
#define DHCP6_OPT_PREFERENCE		7
#define DHCP6_OPT_ELAPSED_TIME		8
#define DHCP6_OPT_RELAY_MSG		9
#define DHCP6_OPT_AUTH			11
#define DHCP6_OPT_UNICAST		12
#define DHCP6_OPT_STATUSCODE		13
#define DHCP6_OPT_RAPIDCOMMIT		14
#define DHCP6_OPT_USERCLASS		15
#define DHCP6_OPT_VENDORCLASS		16
#define DHCP6_OPT_VENDOROPTS		17
#define DHCP6_OPT_INTERFACEID		18
#define DHCP6_OPT_RECONFMSG		19
#define DHCP6_OPT_RECONFACCEPT		20
/* Remote-ID is defined at RFC 4649. */
#define DHCP6_OPT_REMOTEID		37

/* DHCP option used in DHCP request/reply/relay packets. */
struct dhcp6_option {
	/* DHCP option code. */
	uint16_t		 dso_code;
	/* DHCP option payload length. */
	uint16_t		 dso_length;
	/* DHCP option data. */
	uint8_t			 dso_data[];
} __packed;

/* Client / Server message format. */
struct dhcp6_packet {
	uint8_t			 ds_msgtype;		/* message type */
	uint8_t			 ds_transactionid[3];	/* transaction id */
	struct dhcp6_option	 ds_options[];
} __packed;

/* Relay Agent/Server message format. */
struct dhcp6_relay_packet {
	/* Message type: DHCP6_MT_RELAYFORW or DHCP6_MT_RELAYREPL */
	uint8_t			 dsr_msgtype;

	/* Number of relay agents that relayed this message. */
	uint8_t			 dsr_hopcount;

	/*
	 * A global or site local address used by the server to Identify
	 * the client link.
	 */
	struct in6_addr		 dsr_linkaddr;

	/*
	 * The address of the client or relay agent from which the relayed
	 * message was received.
	 */
	struct in6_addr		 dsr_peer;

	/* Must include a relay message option. */
	struct dhcp6_option	 dsr_options[];
} __packed;

#endif /* _DHCP_H_ */
