/*	$KAME: policy_parse.y,v 1.14 2003/06/27 03:39:20 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * IN/OUT bound policy configuration take place such below:
 *	in <policy>
 *	out <policy>
 *
 * <policy> is one of following:
 *	"discard", "none", "ipsec <requests>", "entrust", "bypass",
 *
 * The following requests are accepted as <requests>:
 *
 *	protocol/mode/src-dst/level
 *	protocol/mode/src-dst		parsed as protocol/mode/src-dst/default
 *	protocol/mode/src-dst/		parsed as protocol/mode/src-dst/default
 *	protocol/transport		parsed as protocol/mode/any-any/default
 *	protocol/transport//level	parsed as protocol/mode/any-any/level
 *
 * You can concatenate these requests with either ' '(single space) or '\n'.
 */

%{
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netipsec/ipsec.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>

#include "ipsec_strerror.h"

#define ATOX(c) \
  (isdigit(c) ? (c - '0') : (isupper(c) ? (c - 'A' + 10) : (c - 'a' + 10) ))

static caddr_t pbuf = NULL;		/* sadb_x_policy buffer */
static int tlen = 0;			/* total length of pbuf */
static int offset = 0;			/* offset of pbuf */
static int p_dir, p_type, p_protocol, p_mode, p_level, p_reqid;
static struct sockaddr *p_src = NULL;
static struct sockaddr *p_dst = NULL;

struct _val;
extern void yyerror(char *msg);
static struct sockaddr *parse_sockaddr(struct _val *buf);
static int rule_check(void);
static int init_x_policy(void);
static int set_x_request(struct sockaddr *src, struct sockaddr *dst);
static int set_sockaddr(struct sockaddr *addr);
static void policy_parse_request_init(void);
static caddr_t policy_parse(char *msg, int msglen);

extern void __policy__strbuffer__init__(char *msg);
extern void __policy__strbuffer__free__(void);
extern int yylex(void);

extern char *__libipsecyytext;	/*XXX*/

%}

%union {
	u_int num;
	struct _val {
		int len;
		char *buf;
	} val;
}

%token DIR ACTION PROTOCOL MODE LEVEL LEVEL_SPECIFY
%token IPADDRESS
%token ME ANY
%token SLASH HYPHEN
%type <num> DIR ACTION PROTOCOL MODE LEVEL
%type <val> IPADDRESS LEVEL_SPECIFY

%%
policy_spec
	:	DIR ACTION
		{
			p_dir = $1;
			p_type = $2;

			if (init_x_policy())
				return -1;
		}
		rules
	|	DIR
		{
			p_dir = $1;
			p_type = 0;	/* ignored it by kernel */

			if (init_x_policy())
				return -1;
		}
	;

rules
	:	/*NOTHING*/
	|	rules rule {
			if (rule_check() < 0)
				return -1;

			if (set_x_request(p_src, p_dst) < 0)
				return -1;

			policy_parse_request_init();
		}
	;

rule
	:	protocol SLASH mode SLASH addresses SLASH level
	|	protocol SLASH mode SLASH addresses SLASH
	|	protocol SLASH mode SLASH addresses
	|	protocol SLASH mode SLASH
	|	protocol SLASH mode SLASH SLASH level
	|	protocol SLASH mode
	|	protocol SLASH {
			__ipsec_errcode = EIPSEC_FEW_ARGUMENTS;
			return -1;
		}
	|	protocol {
			__ipsec_errcode = EIPSEC_FEW_ARGUMENTS;
			return -1;
		}
	;

protocol
	:	PROTOCOL { p_protocol = $1; }
	;

mode
	:	MODE { p_mode = $1; }
	;

level
	:	LEVEL {
			p_level = $1;
			p_reqid = 0;
		}
	|	LEVEL_SPECIFY {
			p_level = IPSEC_LEVEL_UNIQUE;
			p_reqid = atol($1.buf);	/* atol() is good. */
		}
	;

addresses
	:	IPADDRESS {
			p_src = parse_sockaddr(&$1);
			if (p_src == NULL)
				return -1;
		}
		HYPHEN
		IPADDRESS {
			p_dst = parse_sockaddr(&$4);
			if (p_dst == NULL)
				return -1;
		}
	|	ME HYPHEN ANY {
			if (p_dir != IPSEC_DIR_OUTBOUND) {
				__ipsec_errcode = EIPSEC_INVAL_DIR;
				return -1;
			}
		}
	|	ANY HYPHEN ME {
			if (p_dir != IPSEC_DIR_INBOUND) {
				__ipsec_errcode = EIPSEC_INVAL_DIR;
				return -1;
			}
		}
		/*
	|	ME HYPHEN ME
		*/
	;

%%

void
yyerror(msg)
	char *msg;
{
	fprintf(stderr, "libipsec: %s while parsing \"%s\"\n",
		msg, __libipsecyytext);

	return;
}

static struct sockaddr *
parse_sockaddr(buf)
	struct _val *buf;
{
	struct addrinfo hints, *res;
	char *serv = NULL;
	int error;
	struct sockaddr *newaddr = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST;
	error = getaddrinfo(buf->buf, serv, &hints, &res);
	if (error != 0) {
		yyerror("invalid IP address");
		__ipsec_set_strerror(gai_strerror(error));
		return NULL;
	}

	if (res->ai_addr == NULL) {
		yyerror("invalid IP address");
		__ipsec_set_strerror(gai_strerror(error));
		return NULL;
	}

	newaddr = malloc(res->ai_addr->sa_len);
	if (newaddr == NULL) {
		__ipsec_errcode = EIPSEC_NO_BUFS;
		freeaddrinfo(res);
		return NULL;
	}
	memcpy(newaddr, res->ai_addr, res->ai_addr->sa_len);

	freeaddrinfo(res);

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return newaddr;
}

static int
rule_check()
{
	if (p_type == IPSEC_POLICY_IPSEC) {
		if (p_protocol == IPPROTO_IP) {
			__ipsec_errcode = EIPSEC_NO_PROTO;
			return -1;
		}

		if (p_mode != IPSEC_MODE_TRANSPORT
		 && p_mode != IPSEC_MODE_TUNNEL) {
			__ipsec_errcode = EIPSEC_INVAL_MODE;
			return -1;
		}

		if (p_src == NULL && p_dst == NULL) {
			 if (p_mode != IPSEC_MODE_TRANSPORT) {
				__ipsec_errcode = EIPSEC_INVAL_ADDRESS;
				return -1;
			}
		}
		else if (p_src->sa_family != p_dst->sa_family) {
			__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
			return -1;
		}
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

static int
init_x_policy()
{
	struct sadb_x_policy *p;

	tlen = sizeof(struct sadb_x_policy);

	pbuf = malloc(tlen);
	if (pbuf == NULL) {
		__ipsec_errcode = EIPSEC_NO_BUFS;
		return -1;
	}
	memset(pbuf, 0, tlen);
	p = (struct sadb_x_policy *)pbuf;
	p->sadb_x_policy_len = 0;	/* must update later */
	p->sadb_x_policy_exttype = SADB_X_EXT_POLICY;
	p->sadb_x_policy_type = p_type;
	p->sadb_x_policy_dir = p_dir;
	p->sadb_x_policy_id = 0;

	offset = tlen;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

static int
set_x_request(src, dst)
	struct sockaddr *src, *dst;
{
	struct sadb_x_ipsecrequest *p;
	int reqlen;

	reqlen = sizeof(*p)
		+ (src ? src->sa_len : 0)
		+ (dst ? dst->sa_len : 0);
	tlen += reqlen;		/* increment to total length */

	pbuf = realloc(pbuf, tlen);
	if (pbuf == NULL) {
		__ipsec_errcode = EIPSEC_NO_BUFS;
		return -1;
	}
	p = (struct sadb_x_ipsecrequest *)&pbuf[offset];
	p->sadb_x_ipsecrequest_len = reqlen;
	p->sadb_x_ipsecrequest_proto = p_protocol;
	p->sadb_x_ipsecrequest_mode = p_mode;
	p->sadb_x_ipsecrequest_level = p_level;
	p->sadb_x_ipsecrequest_reqid = p_reqid;
	offset += sizeof(*p);

	if (set_sockaddr(src) || set_sockaddr(dst))
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

static int
set_sockaddr(addr)
	struct sockaddr *addr;
{
	if (addr == NULL) {
		__ipsec_errcode = EIPSEC_NO_ERROR;
		return 0;
	}

	/* tlen has already incremented */

	memcpy(&pbuf[offset], addr, addr->sa_len);

	offset += addr->sa_len;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

static void
policy_parse_request_init()
{
	p_protocol = IPPROTO_IP;
	p_mode = IPSEC_MODE_ANY;
	p_level = IPSEC_LEVEL_DEFAULT;
	p_reqid = 0;
	if (p_src != NULL) {
		free(p_src);
		p_src = NULL;
	}
	if (p_dst != NULL) {
		free(p_dst);
		p_dst = NULL;
	}

	return;
}

static caddr_t
policy_parse(msg, msglen)
	char *msg;
	int msglen;
{
	int error;
	pbuf = NULL;
	tlen = 0;

	/* initialize */
	p_dir = IPSEC_DIR_INVALID;
	p_type = IPSEC_POLICY_DISCARD;
	policy_parse_request_init();
	__policy__strbuffer__init__(msg);

	error = yyparse();	/* it must be set errcode. */
	__policy__strbuffer__free__();

	if (error) {
		if (pbuf != NULL)
			free(pbuf);
		return NULL;
	}

	/* update total length */
	((struct sadb_x_policy *)pbuf)->sadb_x_policy_len = PFKEY_UNIT64(tlen);

	__ipsec_errcode = EIPSEC_NO_ERROR;

	return pbuf;
}

caddr_t
ipsec_set_policy(msg, msglen)
	char *msg;
	int msglen;
{
	caddr_t policy;

	policy = policy_parse(msg, msglen);
	if (policy == NULL) {
		if (__ipsec_errcode == EIPSEC_NO_ERROR)
			__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return NULL;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return policy;
}

