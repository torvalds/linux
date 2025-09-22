/*	$OpenBSD: parse.y,v 1.32 2025/09/09 04:15:53 yasuoka Exp $ */

/*
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
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

%{
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netdb.h>
#include <event.h>

#include <stdbool.h>
#include "npppd_auth.h"
#include "npppd.h"
#ifdef USE_NPPPD_RADIUS
#include "radius_req.h"
#endif
#include "privsep.h"
#include "log.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *);
int		 popfile(void);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
int		 findeol(void);

static void		 tunnconf_init (struct tunnconf *, int);
static void		 tunnconf_fini (struct tunnconf *);
static struct tunnconf	*tunnconf_find (const char *);
static void		 authconf_init (struct authconf *);
static void		 authconf_fini (struct authconf *);
static void		 radconf_fini (struct radconf *);
static struct authconf	*authconf_find (const char *);
static void		 ipcpconf_init (struct ipcpconf *);
static void		 ipcpconf_fini (struct ipcpconf *);
static struct ipcpconf	*ipcpconf_find (const char *);
static struct iface	*iface_find (const char *);
static void		 sa_set_in_addr_any(struct sockaddr *);

struct npppd_conf	*conf;
struct ipcpconf		*curr_ipcpconf;
struct tunnconf		*curr_tunnconf;
struct authconf		*curr_authconf;
struct radconf		*curr_radconf;

typedef struct {
	union {
		int64_t			 number;
		char			*string;
		struct sockaddr_storage  address;
		struct in_addr           in4_addr;
		bool                     yesno;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	SET MAX_SESSION USER_MAX_SESSION
%token	TUNNEL LISTEN ON PROTOCOL
%token	MRU
%token	IP LCP PAP CHAP EAP MPPE CCP MSCHAPV2 STATEFUL STATELESS REQUIRED
%token	YES NO
%token	L2TP PPTP PPPOE L2TP_HOSTNAME L2TP_VENDOR_NAME L2TP_DATA_USE_SEQ
%token	L2TP_REQUIRE_IPSEC L2TP_LCP_RENEGOTIATION L2TP_FORCE_LCP_RENEGOTIATION
%token	L2TP_CTRL_IN_PKTDUMP L2TP_CTRL_OUT_PKTDUMP L2TP_DATA_IN_PKTDUMP
%token	L2TP_DATA_OUT_PKTDUMP PPTP_HOSTNAME
%token	PPTP_VENDOR_NAME PPTP_ECHO_INTERVAL PPTP_ECHO_TIMEOUT
%token	PPTP_CTRL_IN_PKTDUMP PPTP_CTRL_OUT_PKTDUMP PPTP_DATA_IN_PKTDUMP
%token	PPTP_DATA_OUT_PKTDUMP
%token	PPPOE_SERVICE_NAME PPPOE_ACCEPT_ANY_SERVICE PPPOE_AC_NAME
%token	PPPOE_DESC_IN_PKTDUMP PPPOE_DESC_OUT_PKTDUMP PPPOE_SESSION_IN_PKTDUMP
%token	PPPOE_SESSION_OUT_PKTDUMP
%token	LCP_TIMEOUT LCP_MAX_CONFIGURE LCP_MAX_TERMINATE LCP_MAX_NAK_LOOP
%token	LCP_KEEPALIVE LCP_KEEPALIVE_INTERVAL LCP_KEEPALIVE_RETRY_INTERVAL
%token	LCP_KEEPALIVE_MAX_RETRIES AUTHENTICATION_METHOD CHAP_NAME
%token	IPCP_TIMEOUT IPCP_MAX_CONFIGURE IPCP_MAX_TERMINATE IPCP_MAX_NAK_LOOP
%token	CCP_TIMEOUT CCP_MAX_CONFIGURE CCP_MAX_TERMINATE CCP_MAX_NAK_LOOP
%token	L2TP_HELLO_INTERVAL L2TP_HELLO_TIMEOUT L2TP_ACCEPT_DIALIN
%token	MPPE MPPE_KEY_LENGTH MPPE_KEY_STATE
%token	IDLE_TIMEOUT TCP_MSS_ADJUST INGRESS_FILTER CALLNUM_CHECK
%token	PIPEX DEBUG_DUMP_PKTIN DEBUG_DUMP_PKTOUT
%token	AUTHENTICATION TYPE LOCAL USERNAME_SUFFIX USERNAME_PREFIX EAP_CAPABLE
%token	STRIP_NT_DOMAIN STRIP_ATMARK_REALM USERS_FILE
%token	RADIUS AUTHENTICATION_SERVER ACCOUNTING_SERVER PORT
%token	X_TIMEOUT MAX_TRIES MAX_FAILOVERS SECRET
%token  POOL_ADDRESS DNS_SERVERS NBNS_SERVERS FOR STATIC DYNAMIC
%token  RESOLVER ALLOW_USER_SELECTED_ADDRESS
%token  INTERFACE ADDRESS IPCP
%token	BIND FROM AUTHENTICATED BY TO
%token	ERROR
%token	DAE CLIENT NAS_ID
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.yesno>		yesno
%type	<v.address>		address
%type	<v.address>		addressport
%type	<v.number>		optport
%type	<v.in4_addr>		in4_addr
%type	<v.number>		tunnelproto
%type	<v.number>		mppeyesno
%type	<v.number>		mppekeylen
%type	<v.number>		mppekeylen_l
%type	<v.number>		mppekeystate
%type	<v.number>		mppekeystate_l
%type	<v.number>		protobit
%type	<v.number>		protobit_l
%type	<v.number>		authtype
%type	<v.number>		authmethod
%type	<v.number>		authmethod_l
%type	<v.number>		ipcppooltype

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar set '\n'
		| grammar tunnel '\n'
		| grammar authentication '\n'
		| grammar ipcp '\n'
		| grammar interface '\n'
		| grammar bind '\n'
		| grammar radius '\n'
		| grammar error '\n'		{ file->errors++; }
		;


set		: SET MAX_SESSION NUMBER        { conf->max_session = $3; }
		| SET USER_MAX_SESSION NUMBER   { conf->user_max_session = $3; }
		;

/*
 * tunnel { }
 */
tunnel		: TUNNEL STRING PROTOCOL tunnelproto {
			struct tunnconf *n;

			if (tunnconf_find($2) != NULL) {
				yyerror("tunnel name = %s is already in use.",
				    $2);
				free($2);
				YYERROR;
			}

			if ((n = malloc(sizeof(struct tunnconf))) == NULL) {
				yyerror("out of memory");
				free($2);
				YYERROR;
			}
			tunnconf_init(n, $4);
			switch ($4) {
			case NPPPD_TUNNEL_L2TP:
				strlcpy(n->proto.l2tp.name, $2,
				    sizeof(n->proto.l2tp.name));
				n->name = n->proto.l2tp.name;
				break;
			case NPPPD_TUNNEL_PPTP:
				strlcpy(n->proto.pptp.name, $2,
				    sizeof(n->proto.pptp.name));
				n->name = n->proto.pptp.name;
				break;
			case NPPPD_TUNNEL_PPPOE:
				strlcpy(n->proto.pppoe.name, $2,
				    sizeof(n->proto.pppoe.name));
				n->name = n->proto.pppoe.name;
				break;
			}
			free($2);
			n->protocol = $4;
			curr_tunnconf = n;
		} tunnopts {
			TAILQ_INSERT_TAIL(&conf->tunnconfs, curr_tunnconf,
			    entry);
			switch (curr_tunnconf->protocol) {
#ifdef USE_NPPPD_L2TP
			case NPPPD_TUNNEL_L2TP:
				if (TAILQ_EMPTY(
				    &curr_tunnconf->proto.l2tp.listen)) {
					struct l2tp_listen_addr *addr;

					if ((addr = malloc(sizeof(struct
					    l2tp_listen_addr))) == NULL) {
						free(curr_tunnconf);
						yyerror("out of memory");
						YYERROR;
					}
					sa_set_in_addr_any(
					    (struct sockaddr *)&addr->addr);
					TAILQ_INSERT_TAIL(&curr_tunnconf->proto.
					    l2tp.listen, addr, entry);
				}
				TAILQ_INSERT_TAIL(&conf->l2tp_confs,
				    &curr_tunnconf->proto.l2tp, entry);
				break;
#endif
#ifdef USE_NPPPD_PPTP
			case NPPPD_TUNNEL_PPTP:
				if (TAILQ_EMPTY(
				    &curr_tunnconf->proto.pptp.listen)) {
					struct pptp_listen_addr *addr;

					if ((addr = malloc(sizeof(struct
					    pptp_listen_addr))) == NULL) {
						free(curr_tunnconf);
						yyerror("out of memory");
						YYERROR;
					}
					sa_set_in_addr_any(
					    (struct sockaddr *)&addr->addr);
					TAILQ_INSERT_TAIL(&curr_tunnconf->proto.
					    pptp.listen, addr, entry);
				}
				TAILQ_INSERT_TAIL(&conf->pptp_confs,
				    &curr_tunnconf->proto.pptp, entry);
				break;
#endif
#ifdef USE_NPPPD_PPPOE
			case NPPPD_TUNNEL_PPPOE:
				TAILQ_INSERT_TAIL(&conf->pppoe_confs,
				    &curr_tunnconf->proto.pppoe, entry);
				break;
#endif
			default:
				yyerror("%s is not enabled.",
				    npppd_tunnel_protocol_name(
					    curr_tunnconf->protocol));
				tunnconf_fini(curr_tunnconf);
				free(curr_tunnconf);
				YYERROR;
			}
			curr_tunnconf = NULL;
		}
		;


tunnopts	:
		| '{' optnl tunnopt_l '}'
		;

tunnopt_l	: /* empty */
		| tunnopt_l tunnopt nl
		| tunnopt optnl
		;

tunnopt		: LISTEN ON addressport {

			switch (curr_tunnconf->protocol) {
			case NPPPD_TUNNEL_L2TP:
			    {
				struct l2tp_listen_addr	*l_listen;

				if ((l_listen = malloc(sizeof(
				    struct l2tp_listen_addr))) == NULL) {
					yyerror("out of memory");
					YYERROR;
				}
				l_listen->addr = $3;
				TAILQ_INSERT_TAIL(&curr_tunnconf->proto
				    .l2tp.listen, l_listen, entry);
				break;
			    }
			case NPPPD_TUNNEL_PPTP:
				if ($3.ss_family == AF_INET6) {
					yyerror("listen on IPv6 address is not "
					    "supported by pptp tunnel");
					YYERROR;
				}
			    {
				struct pptp_listen_addr	*p_listen;

				if ((p_listen = malloc(sizeof(
				    struct pptp_listen_addr))) == NULL) {
					yyerror("out of memory");
					YYERROR;
				}
				p_listen->addr = $3;
				TAILQ_INSERT_TAIL(&curr_tunnconf->proto
				    .pptp.listen, p_listen, entry);
				break;
			    }
			default:
				yyerror("listen on address is not supported "
				    "for specified protocol.\n");
				YYERROR;
			}
		}
		| LISTEN ON INTERFACE STRING {
			switch (curr_tunnconf->protocol) {
			case NPPPD_TUNNEL_PPPOE:
				strlcpy(curr_tunnconf->proto.pppoe.if_name, $4,
				    sizeof(curr_tunnconf->proto.pppoe.if_name));
				free($4);
				break;
			default:
				free($4);
				yyerror("listen on interface is not supported "
				    "for specified protocol.\n");
				YYERROR;
			}
		}
		| LCP_TIMEOUT NUMBER {
			curr_tunnconf->lcp_timeout = $2;
		}
		| LCP_MAX_CONFIGURE NUMBER {
			curr_tunnconf->lcp_max_configure = $2;
		}
		| LCP_MAX_TERMINATE NUMBER {
			curr_tunnconf->lcp_max_terminate = $2;
		}
		| LCP_MAX_NAK_LOOP NUMBER {
			curr_tunnconf->lcp_max_nak_loop = $2;
		}
		| MRU NUMBER {
			curr_tunnconf->mru = $2;
		}
		| LCP_KEEPALIVE yesno {
			curr_tunnconf->lcp_keepalive = $2;
		}
		| LCP_KEEPALIVE_INTERVAL NUMBER {
			curr_tunnconf->lcp_keepalive_interval = $2;
		}
		| LCP_KEEPALIVE_RETRY_INTERVAL NUMBER {
			curr_tunnconf->lcp_keepalive_retry_interval = $2;
		}
		| LCP_KEEPALIVE_MAX_RETRIES NUMBER {
			curr_tunnconf->lcp_keepalive_max_retries = $2;
		}
		| AUTHENTICATION_METHOD authmethod_l {
			curr_tunnconf->auth_methods = $2;
		}
		| CHAP_NAME STRING {
			curr_tunnconf->chap_name = $2;
		}
		| IPCP_TIMEOUT NUMBER {
			curr_tunnconf->ipcp_timeout = $2;
		}
		| IPCP_MAX_CONFIGURE NUMBER {
			curr_tunnconf->ipcp_max_configure = $2;
		}
		| IPCP_MAX_TERMINATE NUMBER {
			curr_tunnconf->ipcp_max_terminate = $2;
		}
		| IPCP_MAX_NAK_LOOP NUMBER {
			curr_tunnconf->ipcp_max_nak_loop = $2;
		}
		| CCP_TIMEOUT NUMBER {
			curr_tunnconf->ccp_timeout = $2;
		}
		| CCP_MAX_CONFIGURE NUMBER {
			curr_tunnconf->ccp_max_configure = $2;
		}
		| CCP_MAX_TERMINATE NUMBER {
			curr_tunnconf->ccp_max_terminate = $2;
		}
		| CCP_MAX_NAK_LOOP NUMBER {
			curr_tunnconf->ccp_max_nak_loop = $2;
		}
		| L2TP_HOSTNAME STRING {
			curr_tunnconf->proto.l2tp.hostname = $2;
		}
		| L2TP_VENDOR_NAME STRING {
			curr_tunnconf->proto.l2tp.vendor_name = $2;
		}
		| L2TP_HELLO_INTERVAL NUMBER {
			curr_tunnconf->proto.l2tp.hello_interval = $2;
		}
		| L2TP_HELLO_TIMEOUT NUMBER {
			curr_tunnconf->proto.l2tp.hello_timeout = $2;
		}
		| L2TP_ACCEPT_DIALIN yesno {
			curr_tunnconf->proto.l2tp.accept_dialin = $2;
		}
		| L2TP_DATA_USE_SEQ yesno {
			curr_tunnconf->proto.l2tp.data_use_seq = $2;
		}
		| L2TP_REQUIRE_IPSEC yesno {
			curr_tunnconf->proto.l2tp.require_ipsec = $2;
		}
		| L2TP_LCP_RENEGOTIATION yesno {
			curr_tunnconf->proto.l2tp.lcp_renegotiation = $2;
		}
		| L2TP_FORCE_LCP_RENEGOTIATION yesno {
			curr_tunnconf->proto.l2tp.force_lcp_renegotiation = $2;
		}
		| L2TP_CTRL_IN_PKTDUMP yesno {
			curr_tunnconf->proto.l2tp.ctrl_in_pktdump = $2;
		}
		| L2TP_CTRL_OUT_PKTDUMP yesno {
			curr_tunnconf->proto.l2tp.ctrl_out_pktdump = $2;
		}
		| L2TP_DATA_IN_PKTDUMP yesno {
			curr_tunnconf->proto.l2tp.data_in_pktdump = $2;
		}
		| L2TP_DATA_OUT_PKTDUMP yesno {
			curr_tunnconf->proto.l2tp.data_out_pktdump = $2;
		}
		| PPTP_HOSTNAME STRING {
			curr_tunnconf->proto.pptp.hostname = $2;
		}
		| PPTP_VENDOR_NAME STRING {
			curr_tunnconf->proto.pptp.vendor_name = $2;
		}
		| PPTP_ECHO_INTERVAL NUMBER {
			curr_tunnconf->proto.pptp.echo_interval = $2;
		}
		| PPTP_ECHO_TIMEOUT NUMBER {
			curr_tunnconf->proto.pptp.echo_timeout = $2;
		}
		| PPTP_CTRL_IN_PKTDUMP yesno {
			curr_tunnconf->proto.pptp.ctrl_in_pktdump = $2;
		}
		| PPTP_CTRL_OUT_PKTDUMP yesno {
			curr_tunnconf->proto.pptp.ctrl_out_pktdump = $2;
		}
		| PPTP_DATA_IN_PKTDUMP yesno {
			curr_tunnconf->proto.pptp.data_in_pktdump = $2;
		}
		| PPTP_DATA_OUT_PKTDUMP yesno {
			curr_tunnconf->proto.pptp.data_out_pktdump = $2;
		}
		| PPPOE_SERVICE_NAME STRING {
			curr_tunnconf->proto.pppoe.service_name = $2;
		}
		| PPPOE_ACCEPT_ANY_SERVICE yesno {
			curr_tunnconf->proto.pppoe.accept_any_service = $2;
		}
		| PPPOE_AC_NAME STRING {
			curr_tunnconf->proto.pppoe.ac_name = $2;
		}
		| PPPOE_DESC_IN_PKTDUMP yesno {
			curr_tunnconf->proto.pppoe.desc_in_pktdump = $2;
		}
		| PPPOE_DESC_OUT_PKTDUMP yesno {
			curr_tunnconf->proto.pppoe.desc_out_pktdump = $2;
		}
		| PPPOE_SESSION_IN_PKTDUMP yesno {
			curr_tunnconf->proto.pppoe.session_in_pktdump = $2;
		}
		| PPPOE_SESSION_OUT_PKTDUMP yesno {
			curr_tunnconf->proto.pppoe.session_out_pktdump = $2;
		}
		| MPPE mppeyesno {
			curr_tunnconf->mppe_yesno = $2;
		}
		| MPPE_KEY_LENGTH mppekeylen_l {
			curr_tunnconf->mppe_keylen = $2;
		}
		| MPPE_KEY_STATE mppekeystate_l {
			curr_tunnconf->mppe_keystate = $2;
		}
		| TCP_MSS_ADJUST yesno {
			curr_tunnconf->tcp_mss_adjust = $2;
		}
		| IDLE_TIMEOUT NUMBER {
			curr_tunnconf->idle_timeout = $2;
		}
		| INGRESS_FILTER yesno {
			curr_tunnconf->ingress_filter = $2;
		}
		| CALLNUM_CHECK yesno {
			curr_tunnconf->callnum_check = $2;
		}
		| PIPEX yesno {
			curr_tunnconf->pipex = $2;
		}
		| DEBUG_DUMP_PKTIN protobit_l {
			curr_tunnconf->debug_dump_pktin = $2;
		}
		| DEBUG_DUMP_PKTOUT protobit_l {
			curr_tunnconf->debug_dump_pktout = $2;
		}
		;
radius		: RADIUS NAS_ID STRING {
			if (strlcpy(conf->nas_id, $3, sizeof(conf->nas_id))
			    >= sizeof(conf->nas_id)) {
				yyerror("`radius nas-id' is too long.  use "
				    "less than %u chars.",
				    (unsigned)sizeof(conf->nas_id) - 1);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| RADIUS DAE CLIENT address SECRET STRING {
			struct radclientconf *client;
			int secretsiz;

			secretsiz = strlen($6) + 1;
			if ((client = calloc(1, offsetof(struct radclientconf,
			    secret[secretsiz]))) == NULL) {
				yyerror("%s", strerror(errno));
				free($6);
				YYERROR;
			}
			strlcpy(client->secret, $6, secretsiz);

			switch ($4.ss_family) {
			case AF_INET:
				memcpy(&client->addr, &$4,
				    sizeof(struct sockaddr_in));
				break;
			case AF_INET6:
				memcpy(&client->addr, &$4,
				    sizeof(struct sockaddr_in6));
				break;
			default:
				yyerror("address family %d not supported",
				    $4.ss_family);
				free($6);
				YYERROR;
				break;
			}
			TAILQ_INSERT_TAIL(&conf->raddaeclientconfs, client,
			    entry);
			free($6);
		}
		| RADIUS DAE LISTEN ON addressport {
			struct radlistenconf *listen;

			if (ntohs(((struct sockaddr_in *)&$5)->sin_port) == 0)
				((struct sockaddr_in *)&$5)->sin_port = htons(
				    RADIUS_DAE_DEFAULT_PORT);

			if ((listen = calloc(1, sizeof(*listen))) == NULL) {
				yyerror("%s", strerror(errno));
				YYERROR;
			}
			switch ($5.ss_family) {
			case AF_INET:
				memcpy(&listen->addr, &$5,
				    sizeof(struct sockaddr_in));
				break;
			case AF_INET6:
				memcpy(&listen->addr, &$5,
				    sizeof(struct sockaddr_in6));
				break;
			default:
				yyerror("address family %d not supported",
				    $5.ss_family);
				YYERROR;
				break;
			}
			TAILQ_INSERT_TAIL(&conf->raddaelistenconfs, listen,
			    entry);
		}
		;

tunnelproto	: L2TP                        { $$ = NPPPD_TUNNEL_L2TP; }
		| PPTP                        { $$ = NPPPD_TUNNEL_PPTP; }
		| PPPOE                       { $$ = NPPPD_TUNNEL_PPPOE; }
		;

mppeyesno	: YES                         { $$ = NPPPD_MPPE_ENABLED; }
		| NO                          { $$ = NPPPD_MPPE_DISABLED; }
		| REQUIRED                    { $$ = NPPPD_MPPE_REQUIRED; }
		;

address		: STRING {
			int              retval;
			struct addrinfo  hint, *res;

			memset(&hint, 0, sizeof(hint));
			hint.ai_family = PF_UNSPEC;
			hint.ai_socktype = SOCK_DGRAM;	/* dummy */
			hint.ai_flags = AI_NUMERICHOST;

			if ((retval = getaddrinfo($1, NULL, &hint, &res))
			    != 0) {
				yyerror("could not parse the address %s: %s",
				    $1, gai_strerror(retval));
				free($1);
				YYERROR;
			}
			free($1);

			if (res->ai_family != AF_INET &&
			    res->ai_family != AF_INET6) {
				yyerror("address family(%d) is not supported",
				    res->ai_family);
				freeaddrinfo(res);
				YYERROR;
			}
			memcpy(&($$), res->ai_addr, res->ai_addrlen);

			freeaddrinfo(res);
		}
		;

addressport	: address optport {
			$$ = $1;
			((struct sockaddr_in *)&($$))->sin_port = htons($2);
		}
		;

in4_addr	: STRING {
			if (inet_pton(AF_INET, $1, &($$)) != 1) {
				yyerror("could not parse the address %s", $1);
				free($1);
				YYERROR;
			}
		}
		;

authmethod_l	: authmethod                  { $$ = $1; }
		| authmethod_l authmethod     { $$ |= $2; }
		;

authmethod	: PAP                         { $$ = NPPPD_AUTH_METHODS_PAP; }
		| CHAP                        { $$ = NPPPD_AUTH_METHODS_CHAP; }
		| MSCHAPV2 {
		    $$ = NPPPD_AUTH_METHODS_MSCHAPV2;
		}
		;

mppekeylen_l    : mppekeylen                  { $$ = $1; }
		| mppekeylen_l mppekeylen     { $$ |= $2; }
		;

mppekeylen	: NUMBER {
			if ($1 == 40)       $$ = NPPPD_MPPE_40BIT;
			else if ($1 == 56)  $$ = NPPPD_MPPE_56BIT;
			else if ($1 == 128) $$ = NPPPD_MPPE_128BIT;
			else {
				yyerror("%"PRId64": unknown mppe key length",
				    $$);
				YYERROR;
			}
		}
		;

mppekeystate_l	: mppekeystate                { $$ = $1; }
		| mppekeystate_l mppekeystate { $$ |= $2; }
		;

mppekeystate	: STATEFUL                    { $$ = NPPPD_MPPE_STATEFUL; }
		| STATELESS                   { $$ = NPPPD_MPPE_STATELESS; }
		;

protobit_l	: protobit                    { $$ = $1; }
		| protobit_l protobit         { $$ |= $2; }
		;

protobit	: IP                          { $$ = NPPPD_PROTO_BIT_IP; }
		| LCP                         { $$ = NPPPD_PROTO_BIT_LCP; }
		| PAP                         { $$ = NPPPD_PROTO_BIT_PAP; }
		| CHAP                        { $$ = NPPPD_PROTO_BIT_CHAP; }
		| EAP                         { $$ = NPPPD_PROTO_BIT_EAP; }
		| MPPE                        { $$ = NPPPD_PROTO_BIT_MPPE; }
		| CCP                         { $$ = NPPPD_PROTO_BIT_CCP; }
		| IPCP                        { $$ = NPPPD_PROTO_BIT_IPCP; }
		;

/*
 * authentication { }
 */
authentication	: AUTHENTICATION STRING TYPE authtype {
			struct authconf *n;

			if (authconf_find($2) != NULL) {
				yyerror("authentication name %s is already in "
				    "use.", $2);
				free($2);
				YYERROR;
			}
			if ((n = malloc(sizeof(struct authconf))) == NULL) {
				yyerror("out of memory");
				free($2);
				YYERROR;
			}
			authconf_init(n);
			strlcpy(n->name, $2, sizeof(n->name));
			free($2);
			n->auth_type = $4;
			if ($4 == NPPPD_AUTH_TYPE_RADIUS) {
				TAILQ_INIT(&n->data.radius.auth.servers);
				TAILQ_INIT(&n->data.radius.acct.servers);
			}
			curr_authconf = n;
		} '{' optnl authopt_l '}' {
			TAILQ_INSERT_TAIL(&conf->authconfs, curr_authconf,
			    entry);
			curr_authconf = NULL;
		}
		;

authopt_l	: /* empty */
		| authopt_l authopt nl
		| authopt optnl
		;

authopt	: USERNAME_SUFFIX STRING {
			curr_authconf->username_suffix = $2;
		}
		| EAP_CAPABLE yesno {
			curr_authconf->eap_capable = $2;
		}
		| STRIP_NT_DOMAIN yesno {
			curr_authconf->strip_nt_domain = $2;
		}
		| STRIP_ATMARK_REALM yesno {
			curr_authconf->strip_atmark_realm = $2;
		}
		| USERS_FILE STRING {
			strlcpy(curr_authconf->users_file_path, $2,
			    sizeof(curr_authconf->users_file_path));
			free($2);
		}
		| USER_MAX_SESSION NUMBER {
			curr_authconf->user_max_session = $2;
		}
		| AUTHENTICATION_SERVER {
			if (curr_authconf->auth_type != NPPPD_AUTH_TYPE_RADIUS){
				yyerror("`authentication-server' can not be "
				    "used for this type.");
				YYERROR;
			}
			curr_radconf = &curr_authconf->data.radius.auth;
		} '{' optnl radopt_l '}'
		| ACCOUNTING_SERVER {
			if (curr_authconf->auth_type != NPPPD_AUTH_TYPE_RADIUS){
				yyerror("`accounting-server' can not be used "
				    "for this type.");
				YYERROR;
			}
			curr_radconf = &curr_authconf->data.radius.acct;
		} '{' optnl radopt_l '}'
		;

optport		: /* empty */                 { $$ = 0; }
		| PORT NUMBER                 { $$ = $2; }
		;

authtype	: LOCAL		              { $$ = NPPPD_AUTH_TYPE_LOCAL; }
		| RADIUS	              { $$ = NPPPD_AUTH_TYPE_RADIUS; }
		;

radopt_l	:
		| radopt_l radopt nl
		| radopt optnl
		;

radopt		: ADDRESS address optport SECRET STRING {
			int               cnt;
			struct radserver *n;

			if (strlen($5) > MAX_RADIUS_SECRET - 1) {
				yyerror("`secret' is too long.  "
				    "use less than %d chars.",
				    MAX_RADIUS_SECRET - 1);
				YYERROR;
			}
			cnt = 0;
			TAILQ_FOREACH(n, &curr_radconf->servers, entry) {
				cnt++;
			}
			if (cnt >= MAX_RADIUS_SERVERS) {
				yyerror("too many radius servers.  use less "
				    "than or equal to %d servers.",
				    MAX_RADIUS_SERVERS);
				YYERROR;
			}
			if ((n = malloc(sizeof(struct radserver))) == NULL) {
				yyerror("out of memory");
				YYERROR;
			}
			n->address = $2;
			((struct sockaddr_in *)&n->address)->sin_port =
			    htons($3);
			n->secret = $5;
			TAILQ_INSERT_TAIL(&curr_radconf->servers, n, entry);
		}
		| X_TIMEOUT NUMBER {
			curr_radconf->timeout = $2;
		}
		| MAX_TRIES NUMBER {
			curr_radconf->max_tries = $2;
		}
		| MAX_FAILOVERS NUMBER {
			curr_radconf->max_failovers = $2;
		}
		;
/*
 * ipcp { }
 */
ipcp		: IPCP STRING {
			int              cnt;
			struct ipcpconf *n;

			cnt = 0;
			/*
			TAILQ_FOREACH(n, &conf->ipcpconfs, entry) {
				cnt++;
			}
			if (cnt >= NPPPD_MAX_POOL) {
				yyerror("too many `ipcp' settings.  it must be "
				    "less than or euals to %d.",
				    NPPPD_MAX_POOL);
				YYERROR;
			}
			*/

			if (ipcpconf_find($2) != NULL) {
				yyerror("ipcp name %s is already in use.", $2);
				free($2);
				YYERROR;
			}
			if ((n = malloc(sizeof(struct ipcpconf))) == NULL) {
				yyerror("out of memory");
				free($2);
				YYERROR;
			}
			ipcpconf_init(n);
			strlcpy(n->name, $2, sizeof(n->name));
			free($2);
			curr_ipcpconf = n;
		} '{' optnl ipcpopt_l '}' {
			TAILQ_INSERT_TAIL(&conf->ipcpconfs, curr_ipcpconf,
			    entry);
			curr_ipcpconf = NULL;
		}
		;

ipcpopt_l	: /* empty */
		| ipcpopt_l ipcpopt nl
		| ipcpopt optnl
		;

ipcpopt		: POOL_ADDRESS STRING ipcppooltype {
			if ($3 != 1) {
				if (in_addr_range_list_add(
				    &curr_ipcpconf->dynamic_pool, $2) != 0) {
					yyerror("%s", strerror(errno));
					free($2);
					YYERROR;
				}
			}
			if (in_addr_range_list_add(
			    &curr_ipcpconf->static_pool, $2) != 0) {
				yyerror("%s", strerror(errno));
				free($2);
				YYERROR;
			}
			free($2);
		}
		| DNS_SERVERS RESOLVER {
			curr_ipcpconf->dns_use_resolver = true;
			curr_ipcpconf->dns_servers[0].s_addr = 0;
			curr_ipcpconf->dns_servers[1].s_addr = 0;
		}
		| DNS_SERVERS in4_addr in4_addr {
			curr_ipcpconf->dns_use_resolver  = false;
			curr_ipcpconf->dns_configured  = true;
			curr_ipcpconf->dns_servers[0] = $2;
			curr_ipcpconf->dns_servers[1] = $3;
		}
		| DNS_SERVERS in4_addr {
			curr_ipcpconf->dns_use_resolver  = false;
			curr_ipcpconf->dns_configured  = true;
			curr_ipcpconf->dns_servers[0] = $2;
			curr_ipcpconf->dns_servers[1].s_addr = 0;
		}
		| NBNS_SERVERS in4_addr in4_addr {
			curr_ipcpconf->nbns_configured  = true;
			curr_ipcpconf->nbns_servers[0] = $2;
			curr_ipcpconf->nbns_servers[1] = $3;
		}
		| NBNS_SERVERS in4_addr {
			curr_ipcpconf->nbns_configured  = true;
			curr_ipcpconf->nbns_servers[0] = $2;
			curr_ipcpconf->nbns_servers[1].s_addr = 0;
		}
		| ALLOW_USER_SELECTED_ADDRESS yesno {
			curr_ipcpconf->allow_user_select = $2;
		}
		| MAX_SESSION NUMBER {
			curr_ipcpconf->max_session = $2;
		}
		;

ipcppooltype	: /* empty */                 { $$ = 0; }
		| FOR DYNAMIC                 { $$ = 0; }
		| FOR STATIC                  { $$ = 1; }
		;


/*
 * interface
 */
interface	: INTERFACE STRING ADDRESS in4_addr IPCP STRING {
			int              cnt;
			struct iface    *n;
			struct ipcpconf *ipcp;

			cnt = 0;
			TAILQ_FOREACH(n, &conf->ifaces, entry) {
				cnt++;
			}
			if (cnt >= NPPPD_MAX_IFACE) {
				yyerror("too many interfaces.  use less than "
				    "or equal to %d", NPPPD_MAX_IFACE);
				YYERROR;
			}

			if ((ipcp = ipcpconf_find($6)) == NULL) {
				yyerror("ipcp %s is not found", $6);
				free($2);
				YYERROR;
			}
			if (iface_find($2) != NULL) {
				yyerror("interface %s is already in used.", $2);
				free($2);
				YYERROR;
			}

			if ((n = calloc(1, sizeof(struct iface))) == NULL) {
				yyerror("out of memory");
				free($2);
				YYERROR;
			}
			strlcpy(n->name, $2, sizeof(n->name));
			free($2);
			n->ip4addr = $4;
			if (strncmp(n->name, "pppx", 4) == 0)
				n->is_pppx = true;

			n->ipcpconf = ipcp;
			TAILQ_INSERT_TAIL(&conf->ifaces, n, entry);
		}
		;

/*
 * bind
 */
bind		: BIND TUNNEL FROM STRING AUTHENTICATED BY STRING TO STRING {
			struct authconf  *auth;
			struct tunnconf  *tunn;
			struct iface     *iface;
			struct confbind  *n;

			if ((tunn = tunnconf_find($4)) == NULL) {
				yyerror("tunnel %s is not found", $4);
				free($4);
				free($7);
				free($9);
				YYERROR;
			}
			if ((auth = authconf_find($7)) == NULL) {
				yyerror("authentication %s is not found", $7);
				free($4);
				free($7);
				free($9);
				YYERROR;
			}
			if ((iface = iface_find($9)) == NULL) {
				yyerror("interface %s is not found", $9);
				free($4);
				free($7);
				free($9);
				YYERROR;
			}
			if (tunn->pipex == 0 && iface->is_pppx) {
				yyerror("pipex should be enabled for"
				    " interface %s", $9);
				free($4);
				free($7);
				free($9);
				YYERROR;
			}
			if ((n = malloc(sizeof(struct confbind))) == NULL) {
				yyerror("out of memory");
				free($4);
				free($7);
				free($9);
				YYERROR;
			}
			n->tunnconf = tunn;
			n->authconf = auth;
			n->iface = iface;
			TAILQ_INSERT_TAIL(&conf->confbinds, n, entry);
		}
		;

yesno           : YES                         { $$ = true; }
		| NO                          { $$ = false; }
		;

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl
		;

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		*msg;

	file->errors++;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		fatalx("yyerror vasprintf");
	va_end(ap);
	logit(LOG_CRIT, "%s:%d: %s", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "accounting-server",            ACCOUNTING_SERVER},
		{ "address",                      ADDRESS},
		{ "allow-user-selected-address",  ALLOW_USER_SELECTED_ADDRESS},
		{ "authenticated",                AUTHENTICATED},
		{ "authentication",               AUTHENTICATION},
		{ "authentication-method",        AUTHENTICATION_METHOD},
		{ "authentication-server",        AUTHENTICATION_SERVER},
		{ "bind",                         BIND},
		{ "by",                           BY},
		{ "callnum-check",                CALLNUM_CHECK},
		{ "ccp",                          CCP},
		{ "ccp-max-configure",            CCP_MAX_CONFIGURE},
		{ "ccp-max-nak-loop",             CCP_MAX_NAK_LOOP},
		{ "ccp-max-terminate",            CCP_MAX_TERMINATE},
		{ "ccp-timeout",                  CCP_TIMEOUT},
		{ "chap",                         CHAP},
		{ "chap-name",                    CHAP_NAME},
		{ "client",                       CLIENT},
		{ "dae",                          DAE},
		{ "debug-dump-pktin",             DEBUG_DUMP_PKTIN},
		{ "debug-dump-pktout",            DEBUG_DUMP_PKTOUT},
		{ "dns-servers",                  DNS_SERVERS},
		{ "dynamic",                      DYNAMIC},
		{ "eap",                          EAP},
		{ "eap-capable",                  EAP_CAPABLE},
		{ "for",                          FOR},
		{ "from",                         FROM},
		{ "idle-timeout",                 IDLE_TIMEOUT},
		{ "ingress-filter",               INGRESS_FILTER},
		{ "interface",                    INTERFACE},
		{ "ip",                           IP},
		{ "ipcp",                         IPCP},
		{ "ipcp-max-configure",           IPCP_MAX_CONFIGURE},
		{ "ipcp-max-nak-loop",            IPCP_MAX_NAK_LOOP},
		{ "ipcp-max-terminate",           IPCP_MAX_TERMINATE},
		{ "ipcp-timeout",                 IPCP_TIMEOUT},
		{ "l2tp",                         L2TP},
		{ "l2tp-accept-dialin",           L2TP_ACCEPT_DIALIN},
		{ "l2tp-ctrl-in-pktdump",         L2TP_CTRL_IN_PKTDUMP},
		{ "l2tp-ctrl-out-pktdump",        L2TP_CTRL_OUT_PKTDUMP},
		{ "l2tp-data-in-pktdump",         L2TP_DATA_IN_PKTDUMP},
		{ "l2tp-data-out-pktdump",        L2TP_DATA_OUT_PKTDUMP},
		{ "l2tp-data-use-seq",            L2TP_DATA_USE_SEQ},
		{ "l2tp-force-lcp-renegotiation", L2TP_FORCE_LCP_RENEGOTIATION},
		{ "l2tp-hello-interval",          L2TP_HELLO_INTERVAL},
		{ "l2tp-hello-timeout",           L2TP_HELLO_TIMEOUT},
		{ "l2tp-hostname",                L2TP_HOSTNAME},
		{ "l2tp-lcp-renegotiation",       L2TP_LCP_RENEGOTIATION},
		{ "l2tp-require-ipsec",           L2TP_REQUIRE_IPSEC},
		{ "l2tp-vendor-name",             L2TP_VENDOR_NAME},
		{ "lcp",                          LCP},
		{ "lcp-keepalive",                LCP_KEEPALIVE},
		{ "lcp-keepalive-interval",       LCP_KEEPALIVE_INTERVAL},
		{ "lcp-keepalive-max-retries",    LCP_KEEPALIVE_MAX_RETRIES },
		{ "lcp-keepalive-retry-interval", LCP_KEEPALIVE_RETRY_INTERVAL},
		{ "lcp-max-configure",            LCP_MAX_CONFIGURE},
		{ "lcp-max-nak-loop",             LCP_MAX_NAK_LOOP},
		{ "lcp-max-terminate",            LCP_MAX_TERMINATE},
		{ "lcp-timeout",                  LCP_TIMEOUT},
		{ "listen",                       LISTEN},
		{ "local",                        LOCAL},
		{ "max-failovers",                MAX_FAILOVERS},
		{ "max-session",                  MAX_SESSION},
		{ "max-tries",                    MAX_TRIES},
		{ "mppe",                         MPPE},
		{ "mppe-key-length",              MPPE_KEY_LENGTH},
		{ "mppe-key-state",               MPPE_KEY_STATE},
		{ "mru",                          MRU},
		{ "mschapv2",                     MSCHAPV2},
		{ "nas-id",			  NAS_ID},
		{ "nbns-servers",                 NBNS_SERVERS},
		{ "no",                           NO},
		{ "on",                           ON},
		{ "pap",                          PAP},
		{ "pipex",                        PIPEX},
		{ "pool-address",                 POOL_ADDRESS},
		{ "port",                         PORT},
		{ "pppoe",                        PPPOE},
		{ "pppoe-ac-name",                PPPOE_AC_NAME},
		{ "pppoe-accept-any-service",     PPPOE_ACCEPT_ANY_SERVICE},
		{ "pppoe-desc-in-pktdump",        PPPOE_DESC_IN_PKTDUMP},
		{ "pppoe-desc-out-pktdump",       PPPOE_DESC_OUT_PKTDUMP},
		{ "pppoe-service-name",           PPPOE_SERVICE_NAME},
		{ "pppoe-session-in-pktdump",     PPPOE_SESSION_IN_PKTDUMP},
		{ "pppoe-session-out-pktdump",    PPPOE_SESSION_OUT_PKTDUMP},
		{ "pptp",                         PPTP},
		{ "pptp-ctrl-in-pktdump",         PPTP_CTRL_IN_PKTDUMP},
		{ "pptp-ctrl-out-pktdump",        PPTP_CTRL_OUT_PKTDUMP},
		{ "pptp-data-in-pktdump",         PPTP_DATA_IN_PKTDUMP},
		{ "pptp-data-out-pktdump",        PPTP_DATA_OUT_PKTDUMP},
		{ "pptp-echo-interval",           PPTP_ECHO_INTERVAL},
		{ "pptp-echo-timeout",            PPTP_ECHO_TIMEOUT},
		{ "pptp-hostname",                PPTP_HOSTNAME},
		{ "pptp-vendor-name",             PPTP_VENDOR_NAME},
		{ "protocol",                     PROTOCOL},
		{ "radius",                       RADIUS},
		{ "required",                     REQUIRED},
		{ "resolver",                     RESOLVER},
		{ "secret",                       SECRET},
		{ "set",                          SET},
		{ "stateful",                     STATEFUL},
		{ "stateless",                    STATELESS},
		{ "static",                       STATIC},
		{ "strip-atmark-realm",           STRIP_ATMARK_REALM},
		{ "strip-nt-domain",              STRIP_NT_DOMAIN},
		{ "tcp-mss-adjust",               TCP_MSS_ADJUST},
		{ "timeout",                      X_TIMEOUT},
		{ "to",                           TO},
		{ "tunnel",                       TUNNEL},
		{ "type",                         TYPE},
		{ "user-max-session",             USER_MAX_SESSION},
		{ "username-suffix",              USERNAME_SUFFIX},
		{ "users-file",                   USERS_FILE},
		{ "yes",                          YES}
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define MAXPUSHBACK	128

char	*parsebuf;
int	 parseindex;
char	 pushback_buffer[MAXPUSHBACK];
int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = (unsigned char)parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return ((unsigned char)pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index + 1 >= MAXPUSHBACK)
		return (EOF);
	pushback_buffer[pushback_index++] = c;
	return (c);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = (unsigned char)pushback_buffer[--pushback_index];
		else
			c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p;
	int	 quotec, next, c;
	int	 token;

	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || next == ' ' ||
				    next == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			fatal("yylex: strdup");
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && x != '<' && x != '>' && \
	x != '!' && x != '=' && x != '/' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_' || c == '*') {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				fatal("yylex: strdup");
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

struct file *
pushfile(const char *name)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		log_warn("%s", __func__);
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		log_warn("%s", __func__);
		free(nfile);
		return (NULL);
	}
#ifdef NO_PRIVSEP
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
#else
	if ((nfile->stream = priv_fopen(nfile->name)) == NULL) {
#endif
		log_warn("%s: %s", __func__, nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = 1;
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

int
npppd_conf_parse(struct npppd_conf *xconf, const char *filename)
{
	int  errors = 0;

	conf = xconf;

	if ((file = pushfile(filename)) == NULL) {
		return (-1);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	if (curr_tunnconf != NULL) {
		tunnconf_fini(curr_tunnconf);
		free(curr_tunnconf);
	}
	curr_tunnconf = NULL;
	if (curr_authconf != NULL) {
		authconf_fini(curr_authconf);
		free(curr_authconf);
	}
	curr_authconf = NULL;
	if (curr_ipcpconf != NULL) {
		ipcpconf_fini(curr_ipcpconf);
		free(curr_ipcpconf);
	}
	curr_ipcpconf = NULL;

	if (errors)
		npppd_conf_fini(xconf);

	return (errors ? -1 : 0);
}

void
npppd_conf_init(struct npppd_conf *xconf)
{
	memset(xconf, 0, sizeof(struct npppd_conf));
	TAILQ_INIT(&xconf->tunnconfs);
	TAILQ_INIT(&xconf->authconfs);
	TAILQ_INIT(&xconf->ipcpconfs);
	TAILQ_INIT(&xconf->ifaces);
	TAILQ_INIT(&xconf->confbinds);
	TAILQ_INIT(&xconf->l2tp_confs);
	TAILQ_INIT(&xconf->pptp_confs);
	TAILQ_INIT(&xconf->pppoe_confs);
	TAILQ_INIT(&xconf->raddaeclientconfs);
	TAILQ_INIT(&xconf->raddaelistenconfs);
	strlcpy(xconf->nas_id, "npppd", sizeof(xconf->nas_id));
}

void
npppd_conf_fini(struct npppd_conf *xconf)
{
	struct tunnconf *tunn, *tunn0;
	struct authconf *auth, *auth0;
	struct ipcpconf *ipcp, *ipcp0;
	struct iface    *iface, *iface0;
	struct confbind *confbind, *confbind0;
	struct radclientconf *radc, *radct;
	struct radlistenconf *radl, *radlt;

	TAILQ_FOREACH_SAFE(tunn, &xconf->tunnconfs, entry, tunn0) {
		tunnconf_fini(tunn);
	}
	TAILQ_FOREACH_SAFE(auth, &xconf->authconfs, entry, auth0) {
		authconf_fini(auth);
	}
	TAILQ_FOREACH_SAFE(ipcp, &xconf->ipcpconfs, entry, ipcp0) {
		ipcpconf_fini(ipcp);
	}
	TAILQ_FOREACH_SAFE(iface, &xconf->ifaces, entry, iface0) {
		free(iface);
	}
	TAILQ_FOREACH_SAFE(confbind, &xconf->confbinds, entry, confbind0) {
		free(confbind);
	}
	TAILQ_FOREACH_SAFE(radc, &xconf->raddaeclientconfs, entry, radct)
		free(radc);
	TAILQ_FOREACH_SAFE(radl, &xconf->raddaelistenconfs, entry, radlt)
		free(radl);
	TAILQ_INIT(&xconf->l2tp_confs);
	TAILQ_INIT(&xconf->pptp_confs);
	TAILQ_INIT(&xconf->pppoe_confs);
}

void
tunnconf_fini(struct tunnconf *tun)
{
	if (tun->chap_name != NULL)
		free(tun->chap_name);
	tun->chap_name = NULL;

	switch (tun->protocol) {
	case NPPPD_TUNNEL_L2TP:
	    {
		struct l2tp_listen_addr	*l_addr, *l_tmp;

		if (tun->proto.l2tp.hostname != NULL)
			free(tun->proto.l2tp.hostname);
		tun->proto.l2tp.hostname = NULL;
		if (tun->proto.l2tp.vendor_name != NULL)
			free(tun->proto.l2tp.vendor_name);
		tun->proto.l2tp.vendor_name = NULL;
		TAILQ_FOREACH_SAFE(l_addr, &tun->proto.l2tp.listen, entry,
		    l_tmp) {
			TAILQ_REMOVE(&tun->proto.l2tp.listen, l_addr, entry);
			free(l_addr);
		}
		break;
	    }
	case NPPPD_TUNNEL_PPTP:
	    {
		struct pptp_listen_addr	*p_addr, *p_tmp;

		if (tun->proto.pptp.hostname != NULL)
			free(tun->proto.pptp.hostname);
		tun->proto.pptp.hostname = NULL;
		if (tun->proto.pptp.vendor_name != NULL)
			free(tun->proto.pptp.vendor_name);
		tun->proto.pptp.vendor_name = NULL;
		TAILQ_FOREACH_SAFE(p_addr, &tun->proto.pptp.listen, entry,
		    p_tmp) {
			TAILQ_REMOVE(&tun->proto.pptp.listen, p_addr, entry);
			free(p_addr);
		}
		break;
	    }
	case NPPPD_TUNNEL_PPPOE:
		if (tun->proto.pppoe.service_name != NULL)
			free(tun->proto.pppoe.service_name);
		tun->proto.pppoe.service_name = NULL;
		if (tun->proto.pppoe.ac_name != NULL)
			free(tun->proto.pppoe.ac_name);
		tun->proto.pppoe.ac_name = NULL;
		break;
	}
}

void
tunnconf_init(struct tunnconf *tun, int protocol)
{
	extern struct tunnconf tunnconf_default_l2tp, tunnconf_default_pptp;
	extern struct tunnconf tunnconf_default_pppoe;

	switch (protocol) {
	case NPPPD_TUNNEL_L2TP:
		memcpy(tun, &tunnconf_default_l2tp, sizeof(struct tunnconf));
		TAILQ_INIT(&tun->proto.l2tp.listen);
		break;
	case NPPPD_TUNNEL_PPTP:
		memcpy(tun, &tunnconf_default_pptp, sizeof(struct tunnconf));
		TAILQ_INIT(&tun->proto.pptp.listen);
		break;
	case NPPPD_TUNNEL_PPPOE:
		memcpy(tun, &tunnconf_default_pppoe, sizeof(struct tunnconf));
		break;
	}
}

struct tunnconf *
tunnconf_find(const char *name)
{
	struct tunnconf *tunn;

	TAILQ_FOREACH(tunn, &conf->tunnconfs, entry) {
		if (strcmp(tunn->name, name) == 0)
			return tunn;
	}

	return NULL;
}

void
authconf_init(struct authconf *auth)
{
	memset(auth, 0, sizeof(struct authconf));
	auth->eap_capable = true;
	auth->strip_nt_domain = true;
	auth->strip_atmark_realm = false;
}

void
authconf_fini(struct authconf *auth)
{
	if (auth->username_suffix != NULL)
		free(auth->username_suffix);
	auth->username_suffix = NULL;

	switch (auth->auth_type) {
	case NPPPD_AUTH_TYPE_RADIUS:
		radconf_fini(&auth->data.radius.auth);
		radconf_fini(&auth->data.radius.acct);
		break;
	}
}

void
radconf_fini(struct radconf *radconf)
{
	struct radserver *server, *server0;

	TAILQ_FOREACH_SAFE(server, &radconf->servers, entry, server0) {
		if (server->secret != NULL)
			free(server->secret);
		server->secret = NULL;
	}
}

struct authconf *
authconf_find(const char *name)
{
	struct authconf *auth;

	TAILQ_FOREACH(auth, &conf->authconfs, entry) {
		if (strcmp(auth->name, name) == 0)
			return auth;
	}

	return NULL;
}

void
ipcpconf_init(struct ipcpconf *ipcp)
{
	memset(ipcp, 0, sizeof(struct ipcpconf));
}

void
ipcpconf_fini(struct ipcpconf *ipcp)
{
	if (ipcp->dynamic_pool != NULL)
		in_addr_range_list_remove_all(&ipcp->dynamic_pool);
	if (ipcp->static_pool != NULL)
		in_addr_range_list_remove_all(&ipcp->static_pool);
}

struct ipcpconf *
ipcpconf_find(const char *name)
{
	struct ipcpconf *ipcp;

	TAILQ_FOREACH(ipcp, &conf->ipcpconfs, entry) {
		if (strcmp(ipcp->name, name) == 0)
			return ipcp;
	}

	return NULL;
}

struct iface *
iface_find(const char *name)
{
	struct iface *iface;

	TAILQ_FOREACH(iface, &conf->ifaces, entry) {
		if (strcmp(iface->name, name) == 0)
			return iface;
	}

	return NULL;
}

void
sa_set_in_addr_any(struct sockaddr *sa)
{
	memset(sa, 0, sizeof(struct sockaddr_in));

	sa->sa_family = AF_INET,
	sa->sa_len = sizeof(struct sockaddr_in);
	((struct sockaddr_in *)sa)->sin_addr.s_addr = htonl(INADDR_ANY);
}
