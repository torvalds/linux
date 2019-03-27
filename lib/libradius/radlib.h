/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1998 Juniper Networks, Inc.
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
 *
 *	$FreeBSD$
 */

#ifndef _RADLIB_H_
#define _RADLIB_H_

#include <sys/types.h>
#include <netinet/in.h>

/* Limits */
#define RAD_MAX_ATTR_LEN		253

/* Message types */
#define RAD_ACCESS_REQUEST		1
#define RAD_ACCESS_ACCEPT		2
#define RAD_ACCESS_REJECT		3
#define RAD_ACCOUNTING_REQUEST		4
#define RAD_ACCOUNTING_RESPONSE		5
#define RAD_ACCESS_CHALLENGE		11
#define RAD_DISCONNECT_REQUEST		40
#define RAD_DISCONNECT_ACK		41
#define RAD_DISCONNECT_NAK		42
#define RAD_COA_REQUEST			43
#define RAD_COA_ACK			44
#define RAD_COA_NAK			45

/* Attribute types and values */
#define RAD_USER_NAME			1	/* String */
#define RAD_USER_PASSWORD		2	/* String */
#define RAD_CHAP_PASSWORD		3	/* String */
#define RAD_NAS_IP_ADDRESS		4	/* IP address */
#define RAD_NAS_PORT			5	/* Integer */
#define RAD_SERVICE_TYPE		6	/* Integer */
	#define RAD_LOGIN			1
	#define RAD_FRAMED			2
	#define RAD_CALLBACK_LOGIN		3
	#define RAD_CALLBACK_FRAMED		4
	#define RAD_OUTBOUND			5
	#define RAD_ADMINISTRATIVE		6
	#define RAD_NAS_PROMPT			7
	#define RAD_AUTHENTICATE_ONLY		8
	#define RAD_CALLBACK_NAS_PROMPT		9
#define RAD_FRAMED_PROTOCOL		7	/* Integer */
	#define RAD_PPP				1
	#define RAD_SLIP			2
	#define RAD_ARAP			3	/* Appletalk */
	#define RAD_GANDALF			4
	#define RAD_XYLOGICS			5
#define RAD_FRAMED_IP_ADDRESS		8	/* IP address */
#define RAD_FRAMED_IP_NETMASK		9	/* IP address */
#define RAD_FRAMED_ROUTING		10	/* Integer */
#define RAD_FILTER_ID			11	/* String */
#define RAD_FRAMED_MTU			12	/* Integer */
#define RAD_FRAMED_COMPRESSION		13	/* Integer */
	#define RAD_COMP_NONE			0
	#define RAD_COMP_VJ			1
	#define RAD_COMP_IPXHDR			2
#define RAD_LOGIN_IP_HOST		14	/* IP address */
#define RAD_LOGIN_SERVICE		15	/* Integer */
#define RAD_LOGIN_TCP_PORT		16	/* Integer */
     /* unassiged			17 */
#define RAD_REPLY_MESSAGE		18	/* String */
#define RAD_CALLBACK_NUMBER		19	/* String */
#define RAD_CALLBACK_ID			20	/* String */
     /* unassiged			21 */
#define RAD_FRAMED_ROUTE		22	/* String */
#define RAD_FRAMED_IPX_NETWORK		23	/* IP address */
#define RAD_STATE			24	/* String */
#define RAD_CLASS			25	/* Integer */
#define RAD_VENDOR_SPECIFIC		26	/* Integer */
#define RAD_SESSION_TIMEOUT		27	/* Integer */
#define RAD_IDLE_TIMEOUT		28	/* Integer */
#define RAD_TERMINATION_ACTION		29	/* Integer */
#define RAD_CALLED_STATION_ID		30	/* String */
#define RAD_CALLING_STATION_ID		31	/* String */
#define RAD_NAS_IDENTIFIER		32	/* String */
#define RAD_PROXY_STATE			33	/* Integer */
#define RAD_LOGIN_LAT_SERVICE		34	/* Integer */
#define RAD_LOGIN_LAT_NODE		35	/* Integer */
#define RAD_LOGIN_LAT_GROUP		36	/* Integer */
#define RAD_FRAMED_APPLETALK_LINK	37	/* Integer */
#define RAD_FRAMED_APPLETALK_NETWORK	38	/* Integer */
#define RAD_FRAMED_APPLETALK_ZONE	39	/* Integer */
     /* reserved for accounting		40-59 */
#define RAD_ACCT_INPUT_GIGAWORDS	52
#define RAD_ACCT_OUTPUT_GIGAWORDS	53

#define RAD_CHAP_CHALLENGE		60	/* String */
#define RAD_NAS_PORT_TYPE		61	/* Integer */
	#define RAD_ASYNC			0
	#define RAD_SYNC			1
	#define RAD_ISDN_SYNC			2
	#define RAD_ISDN_ASYNC_V120		3
	#define RAD_ISDN_ASYNC_V110		4
	#define RAD_VIRTUAL			5
	#define RAD_PIAFS			6
	#define RAD_HDLC_CLEAR_CHANNEL		7
	#define RAD_X_25			8
	#define RAD_X_75			9
	#define RAD_G_3_FAX			10
	#define RAD_SDSL			11
	#define RAD_ADSL_CAP			12
	#define RAD_ADSL_DMT			13
	#define RAD_IDSL			14
	#define RAD_ETHERNET			15
	#define RAD_XDSL			16
	#define RAD_CABLE			17
	#define RAD_WIRELESS_OTHER		18
	#define RAD_WIRELESS_IEEE_802_11	19
#define RAD_PORT_LIMIT			62	/* Integer */
#define RAD_LOGIN_LAT_PORT		63	/* Integer */
#define RAD_CONNECT_INFO		77	/* String */
#define RAD_EAP_MESSAGE			79	/* Octets */
#define RAD_MESSAGE_AUTHENTIC		80	/* Octets */
#define RAD_ACCT_INTERIM_INTERVAL	85	/* Integer */
#define RAD_NAS_IPV6_ADDRESS		95	/* IPv6 address */
#define RAD_FRAMED_INTERFACE_ID		96	/* 8 octets */
#define RAD_FRAMED_IPV6_PREFIX		97	/* Octets */
#define RAD_LOGIN_IPV6_HOST		98	/* IPv6 address */
#define RAD_FRAMED_IPV6_ROUTE		99	/* String */
#define RAD_FRAMED_IPV6_POOL		100	/* String */

/* Accounting attribute types and values */
#define RAD_ACCT_STATUS_TYPE		40	/* Integer */
	#define RAD_START			1
	#define RAD_STOP			2
	#define RAD_UPDATE			3
	#define RAD_ACCOUNTING_ON		7
	#define RAD_ACCOUNTING_OFF		8
#define RAD_ACCT_DELAY_TIME		41	/* Integer */
#define RAD_ACCT_INPUT_OCTETS		42	/* Integer */
#define RAD_ACCT_OUTPUT_OCTETS		43	/* Integer */
#define RAD_ACCT_SESSION_ID		44	/* String */
#define RAD_ACCT_AUTHENTIC		45	/* Integer */
	#define RAD_AUTH_RADIUS			1
	#define RAD_AUTH_LOCAL			2
	#define RAD_AUTH_REMOTE			3
#define RAD_ACCT_SESSION_TIME		46	/* Integer */
#define RAD_ACCT_INPUT_PACKETS		47	/* Integer */
#define RAD_ACCT_OUTPUT_PACKETS		48	/* Integer */
#define RAD_ACCT_TERMINATE_CAUSE	49	/* Integer */
        #define RAD_TERM_USER_REQUEST		1
        #define RAD_TERM_LOST_CARRIER		2
        #define RAD_TERM_LOST_SERVICE		3
        #define RAD_TERM_IDLE_TIMEOUT		4
        #define RAD_TERM_SESSION_TIMEOUT	5
        #define RAD_TERM_ADMIN_RESET		6
        #define RAD_TERM_ADMIN_REBOOT		7
        #define RAD_TERM_PORT_ERROR		8
        #define RAD_TERM_NAS_ERROR		9
        #define RAD_TERM_NAS_REQUEST		10
        #define RAD_TERM_NAS_REBOOT		11
        #define RAD_TERM_PORT_UNNEEDED		12
        #define RAD_TERM_PORT_PREEMPTED		13
        #define RAD_TERM_PORT_SUSPENDED		14
        #define RAD_TERM_SERVICE_UNAVAILABLE    15
        #define RAD_TERM_CALLBACK		16
        #define RAD_TERM_USER_ERROR		17
        #define RAD_TERM_HOST_REQUEST		18
#define	RAD_ACCT_MULTI_SESSION_ID	50	/* String */
#define	RAD_ACCT_LINK_COUNT		51	/* Integer */

#define	RAD_ERROR_CAUSE			101	/* Integer */

struct rad_handle;
struct timeval;

__BEGIN_DECLS
struct rad_handle	*rad_acct_open(void);
int			 rad_add_server(struct rad_handle *,
			    const char *, int, const char *, int, int);
int			 rad_add_server_ex(struct rad_handle *,
			    const char *, int, const char *, int, int,
			    int, struct in_addr *);
struct rad_handle	*rad_auth_open(void);
void			 rad_bind_to(struct rad_handle *, in_addr_t);
void			 rad_close(struct rad_handle *);
int			 rad_config(struct rad_handle *, const char *);
int			 rad_continue_send_request(struct rad_handle *, int,
			    int *, struct timeval *);
int			 rad_create_request(struct rad_handle *, int);
int			 rad_create_response(struct rad_handle *, int);
struct in_addr		 rad_cvt_addr(const void *);
struct in6_addr		 rad_cvt_addr6(const void *);
u_int32_t		 rad_cvt_int(const void *);
char			*rad_cvt_string(const void *, size_t);
int			 rad_get_attr(struct rad_handle *, const void **,
			    size_t *);
int			 rad_init_send_request(struct rad_handle *, int *,
			    struct timeval *);
struct rad_handle	*rad_open(void);  /* Deprecated, == rad_auth_open */
int			 rad_put_addr(struct rad_handle *, int, struct in_addr);
int			 rad_put_addr6(struct rad_handle *, int, struct in6_addr);
int			 rad_put_attr(struct rad_handle *, int,
			    const void *, size_t);
int			 rad_put_int(struct rad_handle *, int, u_int32_t);
int			 rad_put_string(struct rad_handle *, int,
			    const char *);
int			 rad_put_message_authentic(struct rad_handle *);
ssize_t			 rad_request_authenticator(struct rad_handle *, char *,
			    size_t);
int			 rad_receive_request(struct rad_handle *);
int			 rad_send_request(struct rad_handle *);
int			 rad_send_response(struct rad_handle *);
struct rad_handle	*rad_server_open(int fd);
const char		*rad_server_secret(struct rad_handle *);
const char		*rad_strerror(struct rad_handle *);
u_char			*rad_demangle(struct rad_handle *, const void *,
			    size_t);

__END_DECLS

#endif /* _RADLIB_H_ */
