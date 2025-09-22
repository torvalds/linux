/*	$OpenBSD: dhcp.h,v 1.1 2017/04/19 15:38:32 reyk Exp $	*/

/* Protocol structures... */

/*
 * Copyright (c) 1995, 1996 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#define DHCP_UDP_OVERHEAD	(20 + /* IP header */		\
				 8)   /* UDP header */
#define DHCP_SNAME_LEN		64
#define DHCP_FILE_LEN		128
#define DHCP_FIXED_NON_UDP	236
#define DHCP_FIXED_LEN		(DHCP_FIXED_NON_UDP + DHCP_UDP_OVERHEAD)
						/* Everything but options. */
#define DHCP_MTU_MAX		1500
#define DHCP_OPTION_LEN		(DHCP_MTU_MAX - DHCP_FIXED_LEN)
/* The option/sub-option maximum length. */
#define DHCP_OPTION_MAXLEN	255
/* The option/sub-option header length. */
#define DHCP_OPTION_HDR_LEN	2

#define BOOTP_MIN_LEN		300

#define	SERVER_PORT		67
#define	CLIENT_PORT		68

struct dhcp_packet {
	u_int8_t  op;		/* Message opcode/type */
	u_int8_t  htype;	/* Hardware addr type (see net/if_types.h) */
	u_int8_t  hlen;		/* Hardware addr length */
	u_int8_t  hops;		/* Number of relay agent hops from client */
	u_int32_t xid;		/* Transaction ID */
	u_int16_t secs;		/* Seconds since client started looking */
	u_int16_t flags;	/* Flag bits */
	struct in_addr ciaddr;	/* Client IP address (if already in use) */
	struct in_addr yiaddr;	/* Client IP address */
	struct in_addr siaddr;	/* IP address of next server to talk to */
	struct in_addr giaddr;	/* DHCP relay agent IP address */
	unsigned char chaddr[16];	/* Client hardware address */
	char sname[DHCP_SNAME_LEN];	/* Server name */
	char file[DHCP_FILE_LEN];	/* Boot filename */
	unsigned char options[DHCP_OPTION_LEN];
				/* Optional parameters
				   (actual length dependent on MTU). */
};

/* BOOTP (rfc951) message types */
#define BOOTREQUEST	1
#define BOOTREPLY	2

/* Possible values for flags field... */
#define BOOTP_BROADCAST 32768L

/* Possible values for hardware type (htype) field... */
#define HTYPE_ETHER		1	/* Ethernet			*/
#define HTYPE_IEEE802		6	/* IEEE 802.2 Token Ring...	*/
#define HTYPE_FDDI		8	/* FDDI...			*/
#define HTYPE_IPSEC_TUNNEL	31	/* IPsec Tunnel (RFC3456)	*/

/* Magic cookie validating dhcp options field (and bootp vendor
   extensions field). */
#define DHCP_OPTIONS_COOKIE	"\143\202\123\143"
#define DHCP_OPTIONS_COOKIE_LEN	4

/* DHCP Option codes: */

#define DHO_PAD				0
#define DHO_SUBNET_MASK			1
#define DHO_TIME_OFFSET			2
#define DHO_ROUTERS			3
#define DHO_TIME_SERVERS		4
#define DHO_NAME_SERVERS		5
#define DHO_DOMAIN_NAME_SERVERS		6
#define DHO_LOG_SERVERS			7
#define DHO_COOKIE_SERVERS		8
#define DHO_LPR_SERVERS			9
#define DHO_IMPRESS_SERVERS		10
#define DHO_RESOURCE_LOCATION_SERVERS	11
#define DHO_HOST_NAME			12
#define DHO_BOOT_SIZE			13
#define DHO_MERIT_DUMP			14
#define DHO_DOMAIN_NAME			15
#define DHO_SWAP_SERVER			16
#define DHO_ROOT_PATH			17
#define DHO_EXTENSIONS_PATH		18
#define DHO_IP_FORWARDING		19
#define DHO_NON_LOCAL_SOURCE_ROUTING	20
#define DHO_POLICY_FILTER		21
#define DHO_MAX_DGRAM_REASSEMBLY	22
#define DHO_DEFAULT_IP_TTL		23
#define DHO_PATH_MTU_AGING_TIMEOUT	24
#define DHO_PATH_MTU_PLATEAU_TABLE	25
#define DHO_INTERFACE_MTU		26
#define DHO_ALL_SUBNETS_LOCAL		27
#define DHO_BROADCAST_ADDRESS		28
#define DHO_PERFORM_MASK_DISCOVERY	29
#define DHO_MASK_SUPPLIER		30
#define DHO_ROUTER_DISCOVERY		31
#define DHO_ROUTER_SOLICITATION_ADDRESS	32
#define DHO_STATIC_ROUTES		33
#define DHO_TRAILER_ENCAPSULATION	34
#define DHO_ARP_CACHE_TIMEOUT		35
#define DHO_IEEE802_3_ENCAPSULATION	36
#define DHO_DEFAULT_TCP_TTL		37
#define DHO_TCP_KEEPALIVE_INTERVAL	38
#define DHO_TCP_KEEPALIVE_GARBAGE	39
#define DHO_NIS_DOMAIN			40
#define DHO_NIS_SERVERS			41
#define DHO_NTP_SERVERS			42
#define DHO_VENDOR_ENCAPSULATED_OPTIONS	43
#define DHO_NETBIOS_NAME_SERVERS	44
#define DHO_NETBIOS_DD_SERVER		45
#define DHO_NETBIOS_NODE_TYPE		46
#define DHO_NETBIOS_SCOPE		47
#define DHO_FONT_SERVERS		48
#define DHO_X_DISPLAY_MANAGER		49
#define DHO_DHCP_REQUESTED_ADDRESS	50
#define DHO_DHCP_LEASE_TIME		51
#define DHO_DHCP_OPTION_OVERLOAD	52
#define DHO_DHCP_MESSAGE_TYPE		53
#define DHO_DHCP_SERVER_IDENTIFIER	54
#define DHO_DHCP_PARAMETER_REQUEST_LIST	55
#define DHO_DHCP_MESSAGE		56
#define DHO_DHCP_MAX_MESSAGE_SIZE	57
#define DHO_DHCP_RENEWAL_TIME		58
#define DHO_DHCP_REBINDING_TIME		59
#define DHO_DHCP_CLASS_IDENTIFIER	60
#define DHO_DHCP_CLIENT_IDENTIFIER	61
#define DHO_DHCP_USER_CLASS_ID		77
#define DHO_RELAY_AGENT_INFORMATION	82
#define DHO_END				255

/* DHCP message types. */
#define DHCPDISCOVER	1
#define DHCPOFFER	2
#define DHCPREQUEST	3
#define DHCPDECLINE	4
#define DHCPACK		5
#define DHCPNAK		6
#define DHCPRELEASE	7
#define DHCPINFORM	8

/* Relay Agent Information sub-options */
#define RAI_CIRCUIT_ID	1
#define RAI_REMOTE_ID	2
#define RAI_AGENT_ID	3
