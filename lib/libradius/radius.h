/*	$OpenBSD: radius.h,v 1.7 2024/06/29 11:50:31 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _RADIUS_H
#define _RADIUS_H

#define RADIUS_DEFAULT_PORT		1812
#define RADIUS_ACCT_DEFAULT_PORT	1813
#define RADIUS_DAE_DEFAULT_PORT		3799

/* RADIUS codes */
#define RADIUS_CODE_ACCESS_REQUEST             1
#define RADIUS_CODE_ACCESS_ACCEPT              2
#define RADIUS_CODE_ACCESS_REJECT              3
#define RADIUS_CODE_ACCOUNTING_REQUEST         4
#define RADIUS_CODE_ACCOUNTING_RESPONSE        5
#define RADIUS_CODE_ACCESS_CHALLENGE          11
#define RADIUS_CODE_STATUS_SERVER             12
#define RADIUS_CODE_STATUS_CLIENT             13

#define RADIUS_CODE_DISCONNECT_REQUEST        40
#define RADIUS_CODE_DISCONNECT_ACK            41
#define RADIUS_CODE_DISCONNECT_NAK            42
#define RADIUS_CODE_COA_REQUEST               43
#define RADIUS_CODE_COA_ACK                   44
#define RADIUS_CODE_COA_NAK                   45

/* RADIUS attributes */
#define RADIUS_TYPE_USER_NAME                  1
#define RADIUS_TYPE_USER_PASSWORD              2
#define RADIUS_TYPE_CHAP_PASSWORD              3
#define RADIUS_TYPE_NAS_IP_ADDRESS             4
#define RADIUS_TYPE_NAS_PORT                   5
#define RADIUS_TYPE_SERVICE_TYPE               6
#define RADIUS_TYPE_FRAMED_PROTOCOL            7
#define RADIUS_TYPE_FRAMED_IP_ADDRESS          8
#define RADIUS_TYPE_FRAMED_IP_NETMASK          9
#define RADIUS_TYPE_FRAMED_ROUTING            10
#define RADIUS_TYPE_FILTER_ID                 11
#define RADIUS_TYPE_FRAMED_MTU                12
#define RADIUS_TYPE_FRAMED_COMPRESSION        13
#define RADIUS_TYPE_LOGIN_IP_HOST             14
#define RADIUS_TYPE_LOGIN_SERVICE             15
#define RADIUS_TYPE_LOGIN_TCP_PORT            16
/*      unassigned                            17 */
#define RADIUS_TYPE_REPLY_MESSAGE             18
#define RADIUS_TYPE_CALLBACK_NUMBER           19
#define RADIUS_TYPE_CALLBACK_ID               20
/*      unassigned                            21 */
#define RADIUS_TYPE_FRAMED_ROUTE              22
#define RADIUS_TYPE_FRAMED_IPX_NETWORK        23
#define RADIUS_TYPE_STATE                     24
#define RADIUS_TYPE_CLASS                     25
#define RADIUS_TYPE_VENDOR_SPECIFIC           26
#define RADIUS_TYPE_SESSION_TIMEOUT           27
#define RADIUS_TYPE_IDLE_TIMEOUT              28
#define RADIUS_TYPE_TERMINATION_ACTION        29
#define RADIUS_TYPE_CALLED_STATION_ID         30
#define RADIUS_TYPE_CALLING_STATION_ID        31
#define RADIUS_TYPE_NAS_IDENTIFIER            32
#define RADIUS_TYPE_PROXY_STATE               33
#define RADIUS_TYPE_LOGIN_LAT_SERVICE         34
#define RADIUS_TYPE_LOGIN_LAT_NODE            35
#define RADIUS_TYPE_LOGIN_LAT_GROUP           36
#define RADIUS_TYPE_FRAMED_APPLETALK_LINK     37
#define RADIUS_TYPE_FRAMED_APPLETALK_NETWORK  38
#define RADIUS_TYPE_FRAMED_APPLETALK_ZONE     39
#define RADIUS_TYPE_ACCT_STATUS_TYPE          40
#define RADIUS_TYPE_ACCT_DELAY_TIME           41
#define RADIUS_TYPE_ACCT_INPUT_OCTETS         42
#define RADIUS_TYPE_ACCT_OUTPUT_OCTETS        43
#define RADIUS_TYPE_ACCT_SESSION_ID           44
#define RADIUS_TYPE_ACCT_AUTHENTIC            45
#define RADIUS_TYPE_ACCT_SESSION_TIME         46
#define RADIUS_TYPE_ACCT_INPUT_PACKETS        47
#define RADIUS_TYPE_ACCT_OUTPUT_PACKETS       48
#define RADIUS_TYPE_ACCT_TERMINATE_CAUSE      49
#define RADIUS_TYPE_ACCT_MULTI_SESSION_ID     50
#define RADIUS_TYPE_ACCT_LINK_COUNT           51
#define RADIUS_TYPE_ACCT_INPUT_GIGAWORDS      52
#define RADIUS_TYPE_ACCT_OUTPUT_GIGAWORDS     53
/*      unassigned (for accounting)           54 */
#define RADIUS_TYPE_EVENT_TIMESTAMP           55
/*      unassigned (for accounting)           56 */
/*      unassigned (for accounting)           57 */
/*      unassigned (for accounting)           58 */
/*      unassigned (for accounting)           59 */
#define RADIUS_TYPE_CHAP_CHALLENGE            60
#define RADIUS_TYPE_NAS_PORT_TYPE             61
#define RADIUS_TYPE_PORT_LIMIT                62
#define RADIUS_TYPE_LOGIN_LAT_PORT            63
#define RADIUS_TYPE_TUNNEL_TYPE               64
#define RADIUS_TYPE_TUNNEL_MEDIUM_TYPE        65
#define RADIUS_TYPE_TUNNEL_CLIENT_ENDPOINT    66
#define RADIUS_TYPE_TUNNEL_SERVER_ENDPOINT    67
#define RADIUS_TYPE_ACCT_TUNNEL_CONNECTION    68
#define RADIUS_TYPE_TUNNEL_PASSWORD           69
#define RADIUS_TYPE_ARAP_PASSWORD             70
#define RADIUS_TYPE_ARAP_FEATURES             71
#define RADIUS_TYPE_ARAP_ZONE_ACCESS          72
#define RADIUS_TYPE_ARAP_SECURITY             73
#define RADIUS_TYPE_ARAP_SECURITY_DATA        74
#define RADIUS_TYPE_PASSWORD_RETRY            75
#define RADIUS_TYPE_PROMPT                    76
#define RADIUS_TYPE_CONNECT_INFO              77
#define RADIUS_TYPE_CONFIGURATION_TOKEN       78
#define RADIUS_TYPE_EAP_MESSAGE               79
#define RADIUS_TYPE_MESSAGE_AUTHENTICATOR     80
#define RADIUS_TYPE_TUNNEL_PRIVATE_GROUP_ID   81
#define RADIUS_TYPE_TUNNEL_ASSIGNMENT_ID      82
#define RADIUS_TYPE_TUNNEL_PREFERENCE         83
#define RADIUS_TYPE_ARAP_CHALLENGE_RESPONSE   84
#define RADIUS_TYPE_ACCT_INTERIM_INTERVAL     85
#define RADIUS_TYPE_ACCT_TUNNEL_PACKETS_LOST  86
#define RADIUS_TYPE_NAS_PORT_ID               87
#define RADIUS_TYPE_FRAMED_POOL               88
/*      unassigned                            89 */
#define RADIUS_TYPE_TUNNEL_CLIENT_AUTH_ID     90
#define RADIUS_TYPE_TUNNEL_SERVER_AUTH_ID     91
/*	unassigned                            92-94 */
#define RADIUS_TYPE_NAS_IPV6_ADDRESS          95
#define RADIUS_TYPE_FRAMED_INTERFACE_ID       96
#define RADIUS_TYPE_FRAMED_IPV6_PREFIX        97
#define RADIUS_TYPE_LOGIN_IPV6_HOST           98
#define RADIUS_TYPE_FRAMED_IPV6_ROUTE         99
#define RADIUS_TYPE_FRAMED_IPV6_POOL         100

/* RFC 5176 3.5. Error-Cause */
#define RADIUS_TYPE_ERROR_CAUSE              101

/* RFC 6911 3. Attributes */
#define RADIUS_TYPE_FRAMED_IPV6_ADDRESS      168
#define RADIUS_TYPE_DNS_SERVER_IPV6_ADDRESS  169
#define RADIUS_TYPE_ROUTE_IPV6_INFORMATION   170
#define RADIUS_TYPE_DELEGATED_IPV6_PREFIX_POOL 171
#define RADIUS_TYPE_STATEFUL_IPV6_ADDRESS_POOL 172


/* RFC 2865 5.7. Framed-Protocol */
#define RADIUS_FRAMED_PROTOCOL_PPP	1	/* PPP */
#define RADIUS_FRAMED_PROTOCOL_SLIP	2	/* SLIP */
#define RADIUS_FRAMED_PROTOCOL_ARAP	3	/* AppleTalk Remote Access
						 * Protocol (ARAP) */
#define RADIUS_FRAMED_PROTOCOL_GANDALF	4	/* Gandalf proprietary
						 * SingleLink/MultiLink
						 * protocol */
#define RADIUS_FRAMED_PROTOCOL_XYLOGICS	5	/* Xylogics proprietary
						 * IPX/SLIP */
#define RADIUS_FRAMED_PROTOCOL_X75	6	/* X.75 Synchronous */


/* RFC 2865 5.6. Service-Type */
#define RADIUS_SERVICE_TYPE_LOGIN             1
#define RADIUS_SERVICE_TYPE_FRAMED            2
#define RADIUS_SERVICE_TYPE_CB_LOGIN          3
#define RADIUS_SERVICE_TYPE_CB_FRAMED         4
#define RADIUS_SERVICE_TYPE_OUTBOUND          5
#define RADIUS_SERVICE_TYPE_ADMINISTRATIVE    6
#define RADIUS_SERVICE_TYPE_NAS_PROMPT        7
#define RADIUS_SERVICE_TYPE_AUTHENTICAT_ONLY  8
#define RADIUS_SERVICE_TYPE_CB_NAS_PROMPT     9
#define RADIUS_SERVICE_TYPE_CALL_CHECK        10
#define RADIUS_SERVICE_TYPE_CB_ADMINISTRATIVE 11


/* Microsoft vendor specific attributes: see RFC2548*/
#define RADIUS_VENDOR_MICROSOFT              311
#define RADIUS_VTYPE_MS_CHAP_RESPONSE          1
#define RADIUS_VTYPE_MS_CHAP_ERROR             2
#define RADIUS_VTYPE_MS_CHAP_PW_1              3
#define RADIUS_VTYPE_MS_CHAP_PW_2              4
#define RADIUS_VTYPE_MS_CHAP_LM_ENC_PW         5
#define RADIUS_VTYPE_MS_CHAP_NT_ENC_PW         6
#define RADIUS_VTYPE_MPPE_ENCRYPTION_POLICY    7
#define RADIUS_VTYPE_MPPE_ENCRYPTION_TYPES     8
#define RADIUS_VTYPE_MS_RAS_VENDOR             9
#define RADIUS_VTYPE_MS_CHAP_CHALLENGE        11
#define RADIUS_VTYPE_MS_CHAP_MPPE_KEYS        12
#define RADIUS_VTYPE_MS_BAP_USAGE             13
#define RADIUS_VTYPE_MS_LINK_UTILIZATION_THRESHOLD 14
#define RADIUS_VTYPE_MS_LINK_DROP_TIME_LIMIT  15
#define RADIUS_VTYPE_MPPE_SEND_KEY            16
#define RADIUS_VTYPE_MPPE_RECV_KEY            17
#define RADIUS_VTYPE_MS_RAS_VERSION           18
#define RADIUS_VTYPE_MS_OLD_ARAP_PASSWORD     19
#define RADIUS_VTYPE_MS_NEW_ARAP_PASSWORD     20
#define RADIUS_VTYPE_MS_ARAP_PASSWORD_CHANGE_REASON 21
#define RADIUS_VTYPE_MS_FILTER                22
#define RADIUS_VTYPE_MS_ACCT_AUTH_TYPE        23
#define RADIUS_VTYPE_MS_ACCT_EAP_TYPE         24
#define RADIUS_VTYPE_MS_CHAP2_RESPONSE        25
#define RADIUS_VTYPE_MS_CHAP2_SUCCESS         26
#define RADIUS_VTYPE_MS_CHAP2_PW              27
#define RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER    28
#define RADIUS_VTYPE_MS_SECONDARY_DNS_SERVER  29
#define RADIUS_VTYPE_MS_PRIMARY_NBNS_SERVER   30
#define RADIUS_VTYPE_MS_SECONDARY_NBNS_SERVER 31
/*      unassigned?                           32 */
#define RADIUS_VTYPE_MS_ARAP_CHALLENGE        33


/* RFC 2865 5.41. NAS-Port-Type */
#define RADIUS_NAS_PORT_TYPE_ASYNC		0	/* Async */
#define RADIUS_NAS_PORT_TYPE_SYNC		1	/* Sync */
#define RADIUS_NAS_PORT_TYPE_ISDN_SYNC		2	/* ISDN Sync */
#define RADIUS_NAS_PORT_TYPE_ISDN_ASYNC_V120	3	/* ISDN Async V.120 */
#define RADIUS_NAS_PORT_TYPE_ISDN_ASYNC_V110	4	/* ISDN Async V.110 */
#define RADIUS_NAS_PORT_TYPE_VIRTUAL		5	/* Virtual */
#define RADIUS_NAS_PORT_TYPE_PIAFS		6	/* PIAFS */
#define RADIUS_NAS_PORT_TYPE_HDLC_CLEAR_CHANNEL	7	/* HDLC Clear Channel */
#define RADIUS_NAS_PORT_TYPE_X_25		8	/* X.25 */
#define RADIUS_NAS_PORT_TYPE_X_75		9	/* X.75 */
#define RADIUS_NAS_PORT_TYPE_G3_FAX		10	/* G.3 Fax */
#define RADIUS_NAS_PORT_TYPE_SDSL		11	/* SDSL - Symmetric DSL */
#define RADIUS_NAS_PORT_TYPE_ADSL_CAP		12	/* ADSL-CAP - Asymmetric
							 * DSL, Carrierless
							 * Amplitude Phase
							 * Modulation */
#define RADIUS_NAS_PORT_TYPE_ADSL_DMT		13	/* ADSL-DMT - Asymmetric
							 * DSL, Discrete
							 * Multi-Tone */
#define RADIUS_NAS_PORT_TYPE_IDSL		14	/* IDSL - ISDN Digital
							 * Subscriber Line */
#define RADIUS_NAS_PORT_TYPE_ETHERNET		15	/* Ethernet */
#define RADIUS_NAS_PORT_TYPE_XDSL		16	/* xDSL - Digital
							 * Subscriber Line of
							 * unknown type */
#define RADIUS_NAS_PORT_TYPE_CABLE		17	/* Cable */
#define RADIUS_NAS_PORT_TYPE_WIRELESS		18	/* Wireless - Other */
#define RADIUS_NAS_PORT_TYPE_WIRELESS_802_11	19	/* Wireless - IEEE
							 * 802.11 */


/* RFC 2866 5.1.  Acct-Status-Type */
#define RADIUS_ACCT_STATUS_TYPE_START		1	/* Start */
#define RADIUS_ACCT_STATUS_TYPE_STOP		2	/* Stop */
#define RADIUS_ACCT_STATUS_TYPE_INTERIM_UPDATE	3	/* Interim-Update */
#define RADIUS_ACCT_STATUS_TYPE_ACCT_ON		7	/* Accounting-On */
#define RADIUS_ACCT_STATUS_TYPE_ACCT_OFF	8	/* Accounting-Off */


/* RFC 2866 5.6.  Acct-Authentic */
#define RADIUS_ACCT_AUTHENTIC_RADIUS		1	/* RADIUS */
#define RADIUS_ACCT_AUTHENTIC_LOCAL		2	/* Local */
#define RADIUS_ACCT_AUTHENTIC_REMOTE		3	/* Remote */


/* RFC 2866 5.10.  Acct-Terminate-Cause */
#define RADIUS_TERMNATE_CAUSE_USER_REQUEST	1	/* User Request */
#define RADIUS_TERMNATE_CAUSE_LOST_CARRIER	2	/* Lost Carrier */
#define RADIUS_TERMNATE_CAUSE_LOST_SERVICE	3	/* Lost Service */
#define RADIUS_TERMNATE_CAUSE_IDLE_TIMEOUT	4	/* Idle Timeout */
#define RADIUS_TERMNATE_CAUSE_SESSION_TIMEOUT	5	/* Session Timeout */
#define RADIUS_TERMNATE_CAUSE_ADMIN_RESET	6	/* Admin Reset */
#define RADIUS_TERMNATE_CAUSE_ADMIN_REBOOT	7	/* Admin Reboot */
#define RADIUS_TERMNATE_CAUSE_PORT_ERROR	8	/* Port Error */
#define RADIUS_TERMNATE_CAUSE_NAS_ERROR		9	/* NAS Error */
#define RADIUS_TERMNATE_CAUSE_NAS_RESET		10	/* NAS Request */
#define RADIUS_TERMNATE_CAUSE_NAS_REBOOT	11	/* NAS Reboot */
#define RADIUS_TERMNATE_CAUSE_PORT_UNNEEDED	12	/* Port Unneeded */
#define RADIUS_TERMNATE_CAUSE_PORT_PREEMPTED	13	/* Port Preempted */
#define RADIUS_TERMNATE_CAUSE_PORT_SUSPENDED	14	/* Port Suspended */
#define RADIUS_TERMNATE_CAUSE_SERVICE_UNAVAIL	15	/* Service Unavailable */
#define RADIUS_TERMNATE_CAUSE_CALLBACK		16	/* Callback */
#define RADIUS_TERMNATE_CAUSE_USER_ERROR	17	/* User Error */
#define RADIUS_TERMNATE_CAUSE_HOST_REQUEST	18	/* Host Request */


/* RFC 2868 3.1. Tunnel-Type */
#define RADIUS_TUNNEL_TYPE_PPTP		1	/* Point-to-Point Tunneling
						 * Protocol (PPTP) */
#define RADIUS_TUNNEL_TYPE_L2F		2	/* Layer Two Forwarding (L2F) */
#define RADIUS_TUNNEL_TYPE_L2TP		3	/* Layer Two Tunneling
						 * Protocol (L2TP) */
#define RADIUS_TUNNEL_TYPE_ATMP		4	/* Ascend Tunnel Management
						 * Protocol (ATMP) */
#define RADIUS_TUNNEL_TYPE_VTP		5	/* Virtual Tunneling Protocol
						 * (VTP) */
#define RADIUS_TUNNEL_TYPE_AH		6	/* IP Authentication Header in
						 * the Tunnel-mode (AH) */
#define RADIUS_TUNNEL_TYPE_IP		7	/* IP-in-IP Encapsulation
						 * (IP-IP) */
#define RADIUS_TUNNEL_TYPE_MOBILE	8	/* Minimal IP-in-IP
						 * Encapsulation (MIN-IP-IP) */
#define RADIUS_TUNNEL_TYPE_ESP		9	/* IP Encapsulating Security
						 * Payload in the Tunnel-mode
						 * (ESP) */
#define RADIUS_TUNNEL_TYPE_GRE		10	/* Generic Route Encapsulation
						 * (GRE) */
#define RADIUS_TUNNEL_TYPE_VDS		11	/* Bay Dial Virtual Services
						 * (DVS) */
#define RADIUS_TUNNEL_TYPE_IPIP		12	/* IP-in-IP Tunneling */


/* RFC 2868 3.2. Tunnel-Medium-Type */
#define RADIUS_TUNNEL_MEDIUM_TYPE_IPV4		1	/* IPv4 (IP version 4) */
#define RADIUS_TUNNEL_MEDIUM_TYPE_IPV6		2	/* IPv6 (IP version 6) */
#define RADIUS_TUNNEL_MEDIUM_TYPE_NSAP		3	/* NSAP */
#define RADIUS_TUNNEL_MEDIUM_TYPE_HDLC		4	/* HDLC (8-bit
							 * multidrop) */
#define RADIUS_TUNNEL_MEDIUM_TYPE_BBN1822	5	/* BBN 1822 */
#define RADIUS_TUNNEL_MEDIUM_TYPE_802		6	/* 802 (includes all 802
							 * media plus Ethernet
							 * "canonical format")*/
#define RADIUS_TUNNEL_MEDIUM_TYPE_E163		7	/* E.163 (POTS) */
#define RADIUS_TUNNEL_MEDIUM_TYPE_E164		8	/* E.164 (SMDS, Frame
							 * Relay, ATM) */
/* RFC 5167 3.5. Error-Cause */
/* Residual Session Context Removed */
#define RADIUS_ERROR_CAUSE_RESIDUAL_SESSION_REMOVED	201
/* Invalid EAP Packet (Ignored) */
#define RADIUS_ERROR_CAUSE_INVALID_EAP_PACKET		202
/* Unsupported Attribute */
#define RADIUS_ERROR_CAUSE_UNSUPPORTED_ATTRIBUTE	401
/* Missing Attribute */
#define RADIUS_ERROR_CAUSE_MISSING_ATTRIBUTE		402
/* NAS Identification Mismatch */
#define RADIUS_ERROR_CAUSE_NAS_IDENTIFICATION_MISMATCH	403
/* Invalid Request */
#define RADIUS_ERROR_CAUSE_INVALID_REQUEST		404
/* Unsupported Service */
#define RADIUS_ERROR_CAUSE_UNSUPPORTED_SERVICE		405
/* Unsupported Extension */
#define RADIUS_ERROR_CAUSE_UNSUPPORTED_EXTENSION	406
/* Invalid Attribute Valu */
#define RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE	407
/* Administratively Prohibited */
#define RADIUS_ERROR_CAUSE_ADMINISTRATIVELY_PROHIBITED	501
/* Request Not Routable (Proxy) */
#define RADIUS_ERROR_CAUSE_REQUEST_NOT_ROUTABLE		502
/* Session Context Not Found */
#define RADIUS_ERROR_CAUSE_SESSION_NOT_FOUND		503
/* Session Context Not Removable */
#define RADIUS_ERROR_CAUSE_SESSION_NOT_REMOVABLE 	504
/* Other Proxy Processing Error */
#define RADIUS_ERROR_CAUSE_OTHER_PROXY_PROCESSING_ERROR	505
/* Resources Unavailable */
#define RADIUS_ERROR_CAUSE_RESOURCES_UNAVAILABLE	506
/* Request Initiated */
#define RADIUS_ERROR_CAUSE_REQUEST_INITIATED		507
/* Multiple Session Selection Unsupported */
#define RADIUS_ERROR_CAUSE_MULTI_SELECTION_UNSUPPORTED	508

#include <sys/socket.h>
#include <sys/cdefs.h>

#include <stdbool.h>
#include <stdint.h>

struct in_addr;
struct in6_addr;

__BEGIN_DECLS

/******* packet manipulation support *******/

typedef struct _RADIUS_PACKET RADIUS_PACKET;

/* constructors */
RADIUS_PACKET	*radius_new_request_packet(uint8_t);
RADIUS_PACKET	*radius_new_response_packet(uint8_t, const RADIUS_PACKET *);
RADIUS_PACKET	*radius_convert_packet(const void *, size_t);

/* destructor */
int		 radius_delete_packet(RADIUS_PACKET *);

/* accessors - header values */
uint8_t		 radius_get_id(const RADIUS_PACKET *);
void		 radius_update_id(RADIUS_PACKET * packet);
void		 radius_set_id(RADIUS_PACKET *, uint8_t);
uint8_t		 radius_get_code(const RADIUS_PACKET *);
void		 radius_get_authenticator(const RADIUS_PACKET *, void *);
void		 radius_set_request_packet(RADIUS_PACKET *,
		    const RADIUS_PACKET *);
const RADIUS_PACKET *
		 radius_get_request_packet(const RADIUS_PACKET *);
int		 radius_check_response_authenticator(const RADIUS_PACKET *,
		    const char *);
int		 radius_check_accounting_request_authenticator(
		    const RADIUS_PACKET *, const char *);
uint8_t		*radius_get_authenticator_retval(const RADIUS_PACKET *);
uint8_t		*radius_get_request_authenticator_retval(const RADIUS_PACKET *);
void		 radius_set_accounting_request_authenticator(RADIUS_PACKET *,
		    const char *);
void		 radius_set_response_authenticator(RADIUS_PACKET *,
		    const char *);
uint16_t	 radius_get_length(const RADIUS_PACKET *);
const void	*radius_get_data(const RADIUS_PACKET *);

int		 radius_get_raw_attr(const RADIUS_PACKET *, uint8_t, void *,
		    size_t *);
int		 radius_get_vs_raw_attr(const RADIUS_PACKET *, uint32_t,
		    uint8_t, void *, size_t *);
int		 radius_put_raw_attr(RADIUS_PACKET *, uint8_t, const void *,
		    size_t);
int		 radius_put_vs_raw_attr(RADIUS_PACKET *, uint32_t, uint8_t,
		    const void *, size_t);
int		 radius_get_raw_attr_ptr(const RADIUS_PACKET *, uint8_t,
		    const void **, size_t *);
int		 radius_get_vs_raw_attr_ptr(const RADIUS_PACKET *, uint32_t,
		    uint8_t, const void **, size_t *);
int		 radius_get_raw_attr_cat(const RADIUS_PACKET *, uint8_t,
		    void *, size_t *);
int		 radius_get_vs_raw_attr_cat(const RADIUS_PACKET *, uint32_t,
		    uint8_t, void *, size_t *);
int		 radius_put_raw_attr_cat(RADIUS_PACKET *, uint8_t,
		    const void *, size_t);
int		 radius_put_vs_raw_attr_cat(RADIUS_PACKET *, uint32_t, uint8_t,
		    const void *, size_t);
int		 radius_set_raw_attr(RADIUS_PACKET *, uint8_t, const void *,
		    size_t);
int		 radius_set_vs_raw_attr(RADIUS_PACKET *, uint32_t, uint8_t,
		    const void *, size_t);

int		 radius_del_attr_all(RADIUS_PACKET *, uint8_t);
int		 radius_del_vs_attr_all(RADIUS_PACKET *, uint32_t, uint8_t);

bool		 radius_has_attr(const RADIUS_PACKET *, uint8_t);
bool		 radius_has_vs_attr(const RADIUS_PACKET *, uint32_t, uint8_t);

/* typed attribute accessor (string) */
int		 radius_get_string_attr(const RADIUS_PACKET *, uint8_t, char *,
		    size_t);
int		 radius_get_vs_string_attr(const RADIUS_PACKET *, uint32_t,
		    uint8_t, char *, size_t);
int		 radius_put_string_attr(RADIUS_PACKET *, uint8_t, const char *);
int		 radius_put_vs_string_attr(RADIUS_PACKET *, uint32_t, uint8_t,
		    const char *);

/* typed attribute accessor (uint16_t) */
int		 radius_get_uint16_attr(const RADIUS_PACKET *,
		    uint8_t, uint16_t *);
int		 radius_get_vs_uint16_attr(const RADIUS_PACKET *,
		    uint32_t, uint8_t, uint16_t *);
int		 radius_put_uint16_attr(RADIUS_PACKET *,
		    uint8_t, const uint16_t);
int		 radius_put_vs_uint16_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const uint16_t);
int		 radius_set_uint16_attr(RADIUS_PACKET *,
		    uint8_t, const uint16_t);
int		 radius_set_vs_uint16_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const uint16_t);

/* typed attribute accessor (uint32_t) */
int		 radius_get_uint32_attr(const RADIUS_PACKET *,
		    uint8_t, uint32_t *);
int		 radius_get_vs_uint32_attr(const RADIUS_PACKET *,
		    uint32_t, uint8_t, uint32_t *);
int		 radius_put_uint32_attr(RADIUS_PACKET *,
		    uint8_t, const uint32_t);
int		 radius_put_vs_uint32_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const uint32_t);
int		 radius_set_uint32_attr(RADIUS_PACKET *,
		    uint8_t, const uint32_t);
int		 radius_set_vs_uint32_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const uint32_t);

/* typed attribute accessor (uint64_t) */
int		 radius_get_uint64_attr(const RADIUS_PACKET *,
		    uint8_t, uint64_t *);
int		 radius_get_vs_uint64_attr(const RADIUS_PACKET *,
		    uint32_t, uint8_t, uint64_t *);
int		 radius_put_uint64_attr(RADIUS_PACKET *,
		    uint8_t, const uint64_t);
int		 radius_put_vs_uint64_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const uint64_t);
int		 radius_set_uint64_attr(RADIUS_PACKET *,
		    uint8_t, const uint64_t);
int		 radius_set_vs_uint64_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const uint64_t);

/* typed attribute accessor (ipv4) */
int		 radius_get_ipv4_attr(const RADIUS_PACKET *,
		    uint8_t, struct in_addr *);
int		 radius_get_vs_ipv4_attr(const RADIUS_PACKET *,
		    uint32_t, uint8_t, struct in_addr *);
int		 radius_put_ipv4_attr(RADIUS_PACKET *,
		    uint8_t, const struct in_addr);
int		 radius_put_vs_ipv4_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const struct in_addr);
int		 radius_set_ipv4_attr(RADIUS_PACKET *,
		    uint8_t, const struct in_addr);
int		 radius_set_vs_ipv4_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const struct in_addr);

/* typed attribute accessor (ipv6) */
int		 radius_get_ipv6_attr(const RADIUS_PACKET *,
		    uint8_t, struct in6_addr *);
int		 radius_get_vs_ipv6_attr(const RADIUS_PACKET *,
		    uint32_t, uint8_t, struct in6_addr *);
int		 radius_put_ipv6_attr(RADIUS_PACKET *,
		    uint8_t, const struct in6_addr *);
int		 radius_put_vs_ipv6_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const struct in6_addr *);
int		 radius_set_ipv6_attr(RADIUS_PACKET *,
		    uint8_t, const struct in6_addr *);
int		 radius_set_vs_ipv6_attr(RADIUS_PACKET *,
		    uint32_t, uint8_t, const struct in6_addr *);

/* message authenticator */
int		 radius_put_message_authenticator(RADIUS_PACKET *,
		    const char *);
int		 radius_set_message_authenticator(RADIUS_PACKET *,
		    const char *);
int		 radius_check_message_authenticator(RADIUS_PACKET *,
		    const char *);

/* encryption */
int		 radius_encrypt_user_password_attr(void *, size_t *,
		    const char *, const void *, const char *);
int		 radius_decrypt_user_password_attr(char *, size_t,
		    const void *, size_t, const void *, const char *);
int		 radius_encrypt_mppe_key_attr(void *, size_t *,
		    const void *, size_t, const void *, const char *);
int		 radius_decrypt_mppe_key_attr(void *, size_t *, const void *,
		    size_t, const void *, const char *);

/* encrypted attribute */
int		 radius_get_user_password_attr(const RADIUS_PACKET *,
		    char *, size_t, const char *);
int		 radius_put_user_password_attr(RADIUS_PACKET *,
		    const char *, const char *);
int		 radius_get_mppe_send_key_attr(const RADIUS_PACKET *, void *,
		    size_t *, const char *);
int		 radius_put_mppe_send_key_attr(RADIUS_PACKET *,
		    const void *, size_t, const char *);
int		 radius_get_mppe_recv_key_attr(const RADIUS_PACKET *,
		    void *, size_t *, const char *);
int		 radius_put_mppe_recv_key_attr(RADIUS_PACKET *, const void *,
		    size_t, const char *);

int		 radius_get_eap_msk(const RADIUS_PACKET *, void *, size_t *,
		    const char *);

/* helpers */
RADIUS_PACKET	*radius_recvfrom(int, int, struct sockaddr *, socklen_t *);
int		 radius_sendto(int, const RADIUS_PACKET *, int flags,
		    const struct sockaddr *, socklen_t);
RADIUS_PACKET	*radius_recv(int, int);
int		 radius_send(int, const RADIUS_PACKET *, int);
RADIUS_PACKET	*radius_recvmsg(int, struct msghdr *, int);
int		 radius_sendmsg(int, const RADIUS_PACKET *,
		    const struct msghdr *, int);

__END_DECLS

#endif
