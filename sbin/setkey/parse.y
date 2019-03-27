/*	$FreeBSD$	*/
/*	$KAME: parse.y,v 1.83 2004/05/18 08:48:23 sakane Exp $	*/

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

%{
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <net/route.h>
#include <netinet/in.h>
#include <net/pfkeyv2.h>
#include <netipsec/key_var.h>
#include <netipsec/ipsec.h>
#include <arpa/inet.h>

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>

#include "libpfkey.h"
#include "vchar.h"

#define ATOX(c) \
  (isdigit(c) ? (c - '0') : (isupper(c) ? (c - 'A' + 10) : (c - 'a' + 10)))

u_int32_t p_spi;
u_int p_ext, p_alg_enc, p_alg_auth, p_replay, p_mode;
u_int32_t p_reqid;
u_int p_key_enc_len, p_key_auth_len;
caddr_t p_key_enc, p_key_auth;
time_t p_lt_hard, p_lt_soft;

static int p_aiflags = 0, p_aifamily = PF_UNSPEC;

static struct addrinfo *parse_addr(char *, char *);
static int fix_portstr(vchar_t *, vchar_t *, vchar_t *);
static int setvarbuf(char *, int *, struct sadb_ext *, int, caddr_t, int);
void parse_init(void);
void free_buffer(void);

int setkeymsg0(struct sadb_msg *, unsigned int, unsigned int, size_t);
static int setkeymsg_spdaddr(unsigned int, unsigned int, vchar_t *,
	struct addrinfo *, int, struct addrinfo *, int);
static int setkeymsg_addr(unsigned int, unsigned int,
	struct addrinfo *, struct addrinfo *, int);
static int setkeymsg_add(unsigned int, unsigned int,
	struct addrinfo *, struct addrinfo *);
extern int setkeymsg(char *, size_t *);
extern int sendkeymsg(char *, size_t);

extern int yylex(void);
extern void yyfatal(const char *);
extern void yyerror(const char *);
%}

%union {
	int num;
	unsigned long ulnum;
	vchar_t val;
	struct addrinfo *res;
}

%token EOT SLASH BLCL ELCL
%token ADD GET DELETE DELETEALL FLUSH DUMP
%token PR_ESP PR_AH PR_IPCOMP PR_TCP
%token F_PROTOCOL F_AUTH F_ENC F_REPLAY F_COMP F_RAWCPI
%token F_MODE MODE F_REQID
%token F_EXT EXTENSION NOCYCLICSEQ
%token ALG_AUTH ALG_AUTH_NOKEY
%token ALG_ENC ALG_ENC_NOKEY ALG_ENC_DESDERIV ALG_ENC_DES32IV ALG_ENC_OLD
%token ALG_ENC_SALT
%token ALG_COMP
%token F_LIFETIME_HARD F_LIFETIME_SOFT
%token DECSTRING QUOTEDSTRING HEXSTRING STRING ANY
	/* SPD management */
%token SPDADD SPDDELETE SPDDUMP SPDFLUSH
%token F_POLICY PL_REQUESTS
%token F_AIFLAGS
%token TAGGED

%type <num> prefix protocol_spec upper_spec
%type <num> ALG_ENC ALG_ENC_DESDERIV ALG_ENC_DES32IV ALG_ENC_OLD ALG_ENC_NOKEY
%type <num> ALG_ENC_SALT
%type <num> ALG_AUTH ALG_AUTH_NOKEY
%type <num> ALG_COMP
%type <num> PR_ESP PR_AH PR_IPCOMP PR_TCP
%type <num> EXTENSION MODE
%type <ulnum> DECSTRING
%type <val> PL_REQUESTS portstr key_string
%type <val> policy_requests
%type <val> QUOTEDSTRING HEXSTRING STRING
%type <val> F_AIFLAGS
%type <val> upper_misc_spec policy_spec
%type <res> ipaddr

%%
commands
	:	/*NOTHING*/
	|	commands command
		{
			free_buffer();
			parse_init();
		}
	;

command
	:	add_command
	|	get_command
	|	delete_command
	|	deleteall_command
	|	flush_command
	|	dump_command
	|	spdadd_command
	|	spddelete_command
	|	spddump_command
	|	spdflush_command
	;
	/* commands concerned with management, there is in tail of this file. */

	/* add command */
add_command
	:	ADD ipaddropts ipaddr ipaddr protocol_spec spi extension_spec algorithm_spec EOT
		{
			int status;

			status = setkeymsg_add(SADB_ADD, $5, $3, $4);
			if (status < 0)
				return -1;
		}
	;

	/* delete */
delete_command
	:	DELETE ipaddropts ipaddr ipaddr protocol_spec spi extension_spec EOT
		{
			int status;

			if ($3->ai_next || $4->ai_next) {
				yyerror("multiple address specified");
				return -1;
			}
			if (p_mode != IPSEC_MODE_ANY)
				yyerror("WARNING: mode is obsolete");

			status = setkeymsg_addr(SADB_DELETE, $5, $3, $4, 0);
			if (status < 0)
				return -1;
		}
	;

	/* deleteall command */
deleteall_command
	:	DELETEALL ipaddropts ipaddr ipaddr protocol_spec EOT
		{
			int status;

			status = setkeymsg_addr(SADB_DELETE, $5, $3, $4, 1);
			if (status < 0)
				return -1;
		}
	;

	/* get command */
get_command
	:	GET ipaddropts ipaddr ipaddr protocol_spec spi extension_spec EOT
		{
			int status;

			if (p_mode != IPSEC_MODE_ANY)
				yyerror("WARNING: mode is obsolete");

			status = setkeymsg_addr(SADB_GET, $5, $3, $4, 0);
			if (status < 0)
				return -1;
		}
	;

	/* flush */
flush_command
	:	FLUSH protocol_spec EOT
		{
			struct sadb_msg msg;
			setkeymsg0(&msg, SADB_FLUSH, $2, sizeof(msg));
			sendkeymsg((char *)&msg, sizeof(msg));
		}
	;

	/* dump */
dump_command
	:	DUMP protocol_spec EOT
		{
			struct sadb_msg msg;
			setkeymsg0(&msg, SADB_DUMP, $2, sizeof(msg));
			sendkeymsg((char *)&msg, sizeof(msg));
		}
	;

protocol_spec
	:	/*NOTHING*/
		{
			$$ = SADB_SATYPE_UNSPEC;
		}
	|	PR_ESP
		{
			$$ = SADB_SATYPE_ESP;
			if ($1 == 1)
				p_ext |= SADB_X_EXT_OLD;
			else
				p_ext &= ~SADB_X_EXT_OLD;
		}
	|	PR_AH
		{
			$$ = SADB_SATYPE_AH;
			if ($1 == 1)
				p_ext |= SADB_X_EXT_OLD;
			else
				p_ext &= ~SADB_X_EXT_OLD;
		}
	|	PR_IPCOMP
		{
			$$ = SADB_X_SATYPE_IPCOMP;
		}
	|	PR_TCP
		{
			$$ = SADB_X_SATYPE_TCPSIGNATURE;
		}
	;

spi
	:	DECSTRING { p_spi = $1; }
	|	HEXSTRING
		{
			char *ep;
			unsigned long v;

			ep = NULL;
			v = strtoul($1.buf, &ep, 16);
			if (!ep || *ep) {
				yyerror("invalid SPI");
				return -1;
			}
			if (v & ~0xffffffff) {
				yyerror("SPI too big.");
				return -1;
			}

			p_spi = v;
		}
	;

algorithm_spec
	:	esp_spec
	|	ah_spec
	|	ipcomp_spec
	;

esp_spec
	:	F_ENC enc_alg F_AUTH auth_alg
	|	F_ENC enc_alg
	;

ah_spec
	:	F_AUTH auth_alg
	;

ipcomp_spec
	:	F_COMP ALG_COMP
		{
			if ($2 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $2;
		}
	|	F_COMP ALG_COMP F_RAWCPI
		{
			if ($2 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $2;
			p_ext |= SADB_X_EXT_RAWCPI;
		}
	;

enc_alg
	:	ALG_ENC_NOKEY {
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $1;

			p_key_enc_len = 0;
			p_key_enc = NULL;
			if (ipsec_check_keylen(SADB_EXT_SUPPORTED_ENCRYPT,
			    p_alg_enc, PFKEY_UNUNIT64(p_key_enc_len)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	|	ALG_ENC key_string {
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $1;

			p_key_enc_len = $2.len;
			p_key_enc = $2.buf;
			if (ipsec_check_keylen(SADB_EXT_SUPPORTED_ENCRYPT,
			    p_alg_enc, PFKEY_UNUNIT64(p_key_enc_len)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	|	ALG_ENC_OLD {
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			yyerror("WARNING: obsolete algorithm");
			p_alg_enc = $1;

			p_key_enc_len = 0;
			p_key_enc = NULL;
			if (ipsec_check_keylen(SADB_EXT_SUPPORTED_ENCRYPT,
			    p_alg_enc, PFKEY_UNUNIT64(p_key_enc_len)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	|	ALG_ENC_DESDERIV key_string
		{
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $1;
			if (p_ext & SADB_X_EXT_OLD) {
				yyerror("algorithm mismatched");
				return -1;
			}
			p_ext |= SADB_X_EXT_DERIV;

			p_key_enc_len = $2.len;
			p_key_enc = $2.buf;
			if (ipsec_check_keylen(SADB_EXT_SUPPORTED_ENCRYPT,
			    p_alg_enc, PFKEY_UNUNIT64(p_key_enc_len)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	|	ALG_ENC_DES32IV key_string
		{
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $1;
			if (!(p_ext & SADB_X_EXT_OLD)) {
				yyerror("algorithm mismatched");
				return -1;
			}
			p_ext |= SADB_X_EXT_IV4B;

			p_key_enc_len = $2.len;
			p_key_enc = $2.buf;
			if (ipsec_check_keylen(SADB_EXT_SUPPORTED_ENCRYPT,
			    p_alg_enc, PFKEY_UNUNIT64(p_key_enc_len)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	|	ALG_ENC_SALT key_string
		{
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_enc = $1;

			p_key_enc_len = $2.len;

			p_key_enc = $2.buf;
			/*
			 * Salted keys include a 4 byte value that is
			 * not part of the key.
			 */
			if (ipsec_check_keylen(SADB_EXT_SUPPORTED_ENCRYPT,
			    p_alg_enc, PFKEY_UNUNIT64(p_key_enc_len - 4)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	;

auth_alg
	:	ALG_AUTH key_string {
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_auth = $1;

			p_key_auth_len = $2.len;
			p_key_auth = $2.buf;

			if (p_alg_auth == SADB_X_AALG_TCP_MD5) {
				if ((p_key_auth_len < 1) || (p_key_auth_len >
				    80))
					return -1;
			} else if (ipsec_check_keylen(SADB_EXT_SUPPORTED_AUTH,
			    p_alg_auth, PFKEY_UNUNIT64(p_key_auth_len)) < 0) {
				yyerror(ipsec_strerror());
				return -1;
			}
		}
	|	ALG_AUTH_NOKEY {
			if ($1 < 0) {
				yyerror("unsupported algorithm");
				return -1;
			}
			p_alg_auth = $1;

			p_key_auth_len = 0;
			p_key_auth = NULL;
		}
	;

key_string
	:	QUOTEDSTRING
		{
			$$ = $1;
		}
	|	HEXSTRING
		{
			caddr_t pp_key;
			caddr_t bp;
			caddr_t yp = $1.buf;
			int l;

			l = strlen(yp) % 2 + strlen(yp) / 2;
			if ((pp_key = malloc(l)) == 0) {
				yyerror("not enough core");
				return -1;
			}
			memset(pp_key, 0, l);

			bp = pp_key;
			if (strlen(yp) % 2) {
				*bp = ATOX(yp[0]);
				yp++, bp++;
			}
			while (*yp) {
				*bp = (ATOX(yp[0]) << 4) | ATOX(yp[1]);
				yp += 2, bp++;
			}

			$$.len = l;
			$$.buf = pp_key;
		}
	;

extension_spec
	:	/*NOTHING*/
	|	extension_spec extension
	;

extension
	:	F_EXT EXTENSION { p_ext |= $2; }
	|	F_EXT NOCYCLICSEQ { p_ext &= ~SADB_X_EXT_CYCSEQ; }
	|	F_MODE MODE { p_mode = $2; }
	|	F_MODE ANY { p_mode = IPSEC_MODE_ANY; }
	|	F_REQID DECSTRING { p_reqid = $2; }
	|	F_REPLAY DECSTRING
		{
			if ((p_ext & SADB_X_EXT_OLD) != 0) {
				yyerror("replay prevention cannot be used with "
				    "ah/esp-old");
				return -1;
			}
			p_replay = $2;
			if (p_replay > (UINT32_MAX - 32) >> 3)
				yyerror("replay window is too large");
		}
	|	F_LIFETIME_HARD DECSTRING { p_lt_hard = $2; }
	|	F_LIFETIME_SOFT DECSTRING { p_lt_soft = $2; }
	;

	/* definition about command for SPD management */
	/* spdadd */
spdadd_command
	:	SPDADD ipaddropts STRING prefix portstr STRING prefix portstr upper_spec upper_misc_spec policy_spec EOT
		{
			int status;
			struct addrinfo *src, *dst;

			/* fixed port fields if ulp is icmpv6 */
			if ($10.buf != NULL) {
				if ($9 != IPPROTO_ICMPV6)
					return -1;
				free($5.buf);
				free($8.buf);
				if (fix_portstr(&$10, &$5, &$8))
					return -1;
			}

			src = parse_addr($3.buf, $5.buf);
			dst = parse_addr($6.buf, $8.buf);
			if (!src || !dst) {
				/* yyerror is already called */
				return -1;
			}
			if (src->ai_next || dst->ai_next) {
				yyerror("multiple address specified");
				freeaddrinfo(src);
				freeaddrinfo(dst);
				return -1;
			}

			status = setkeymsg_spdaddr(SADB_X_SPDADD, $9, &$11,
			    src, $4, dst, $7);
			freeaddrinfo(src);
			freeaddrinfo(dst);
			if (status < 0)
				return -1;
		}
	|	SPDADD TAGGED QUOTEDSTRING policy_spec EOT
		{
			return -1;
		}
	;

spddelete_command
	:	SPDDELETE ipaddropts STRING prefix portstr STRING prefix portstr upper_spec upper_misc_spec policy_spec EOT
		{
			int status;
			struct addrinfo *src, *dst;

			/* fixed port fields if ulp is icmpv6 */
			if ($10.buf != NULL) {
				if ($9 != IPPROTO_ICMPV6)
					return -1;
				free($5.buf);
				free($8.buf);
				if (fix_portstr(&$10, &$5, &$8))
					return -1;
			}

			src = parse_addr($3.buf, $5.buf);
			dst = parse_addr($6.buf, $8.buf);
			if (!src || !dst) {
				/* yyerror is already called */
				return -1;
			}
			if (src->ai_next || dst->ai_next) {
				yyerror("multiple address specified");
				freeaddrinfo(src);
				freeaddrinfo(dst);
				return -1;
			}

			status = setkeymsg_spdaddr(SADB_X_SPDDELETE, $9, &$11,
			    src, $4, dst, $7);
			freeaddrinfo(src);
			freeaddrinfo(dst);
			if (status < 0)
				return -1;
		}
	;

spddump_command:
		SPDDUMP EOT
		{
			struct sadb_msg msg;
			setkeymsg0(&msg, SADB_X_SPDDUMP, SADB_SATYPE_UNSPEC,
			    sizeof(msg));
			sendkeymsg((char *)&msg, sizeof(msg));
		}
	;

spdflush_command:
		SPDFLUSH EOT
		{
			struct sadb_msg msg;
			setkeymsg0(&msg, SADB_X_SPDFLUSH, SADB_SATYPE_UNSPEC,
			    sizeof(msg));
			sendkeymsg((char *)&msg, sizeof(msg));
		}
	;

ipaddropts
	:	/* nothing */
	|	ipaddropts ipaddropt
	;

ipaddropt
	:	F_AIFLAGS
		{
			char *p;

			for (p = $1.buf + 1; *p; p++)
				switch (*p) {
				case '4':
					p_aifamily = AF_INET;
					break;
#ifdef INET6
				case '6':
					p_aifamily = AF_INET6;
					break;
#endif
				case 'n':
					p_aiflags = AI_NUMERICHOST;
					break;
				default:
					yyerror("invalid flag");
					return -1;
				}
		}
	;

ipaddr
	:	STRING
		{
			$$ = parse_addr($1.buf, NULL);
			if ($$ == NULL) {
				/* yyerror already called by parse_addr */
				return -1;
			}
		}
	;

prefix
	:	/*NOTHING*/ { $$ = -1; }
	|	SLASH DECSTRING { $$ = $2; }
	;

portstr
	:	/*NOTHING*/
		{
			$$.buf = strdup("0");
			if (!$$.buf) {
				yyerror("insufficient memory");
				return -1;
			}
			$$.len = strlen($$.buf);
		}
	|	BLCL ANY ELCL
		{
			$$.buf = strdup("0");
			if (!$$.buf) {
				yyerror("insufficient memory");
				return -1;
			}
			$$.len = strlen($$.buf);
		}
	|	BLCL DECSTRING ELCL
		{
			char buf[20];
			snprintf(buf, sizeof(buf), "%lu", $2);
			$$.buf = strdup(buf);
			if (!$$.buf) {
				yyerror("insufficient memory");
				return -1;
			}
			$$.len = strlen($$.buf);
		}
	|	BLCL STRING ELCL
		{
			$$ = $2;
		}
	;

upper_spec
	:	DECSTRING { $$ = $1; }
	|	ANY { $$ = IPSEC_ULPROTO_ANY; }
	|	PR_TCP { $$ = IPPROTO_TCP; }
	|	PR_ESP { $$ = IPPROTO_ESP; }
	|	STRING
		{
			struct protoent *ent;

			ent = getprotobyname($1.buf);
			if (ent)
				$$ = ent->p_proto;
			else {
				if (strcmp("icmp6", $1.buf) == 0) {
					$$ = IPPROTO_ICMPV6;
				} else if(strcmp("ip4", $1.buf) == 0) {
					$$ = IPPROTO_IPV4;
				} else {
					yyerror("invalid upper layer protocol");
					return -1;
				}
			}
			endprotoent();
		}
	;

upper_misc_spec
	:	/*NOTHING*/
		{
			$$.buf = NULL;
			$$.len = 0;
		}
	|	STRING
		{
			$$.buf = strdup($1.buf);
			if (!$$.buf) {
				yyerror("insufficient memory");
				return -1;
			}
			$$.len = strlen($$.buf);
		}
	;

policy_spec
	:	F_POLICY policy_requests
		{
			char *policy;

			policy = ipsec_set_policy($2.buf, $2.len);
			if (policy == NULL) {
				yyerror(ipsec_strerror());
				return -1;
			}

			$$.buf = policy;
			$$.len = ipsec_get_policylen(policy);
		}
	;

policy_requests
	:	PL_REQUESTS { $$ = $1; }
	;

%%

int
setkeymsg0(msg, type, satype, l)
	struct sadb_msg *msg;
	unsigned int type;
	unsigned int satype;
	size_t l;
{

	msg->sadb_msg_version = PF_KEY_V2;
	msg->sadb_msg_type = type;
	msg->sadb_msg_errno = 0;
	msg->sadb_msg_satype = satype;
	msg->sadb_msg_reserved = 0;
	msg->sadb_msg_seq = 0;
	msg->sadb_msg_pid = getpid();
	msg->sadb_msg_len = PFKEY_UNIT64(l);
	return 0;
}

/* XXX NO BUFFER OVERRUN CHECK! BAD BAD! */
static int
setkeymsg_spdaddr(type, upper, policy, srcs, splen, dsts, dplen)
	unsigned int type;
	unsigned int upper;
	vchar_t *policy;
	struct addrinfo *srcs;
	int splen;
	struct addrinfo *dsts;
	int dplen;
{
	struct sadb_msg *msg;
	char buf[BUFSIZ];
	int l, l0;
	struct sadb_address m_addr;
	struct addrinfo *s, *d;
	int n;
	int plen;
	struct sockaddr *sa;
	int salen;

	msg = (struct sadb_msg *)buf;

	if (!srcs || !dsts)
		return -1;

	/* fix up length afterwards */
	setkeymsg0(msg, type, SADB_SATYPE_UNSPEC, 0);
	l = sizeof(struct sadb_msg);

	memcpy(buf + l, policy->buf, policy->len);
	l += policy->len;

	l0 = l;
	n = 0;

	/* do it for all src/dst pairs */
	for (s = srcs; s; s = s->ai_next) {
		for (d = dsts; d; d = d->ai_next) {
			/* rewind pointer */
			l = l0;

			if (s->ai_addr->sa_family != d->ai_addr->sa_family)
				continue;
			switch (s->ai_addr->sa_family) {
			case AF_INET:
				plen = sizeof(struct in_addr) << 3;
				break;
#ifdef INET6
			case AF_INET6:
				plen = sizeof(struct in6_addr) << 3;
				break;
#endif
			default:
				continue;
			}

			/* set src */
			sa = s->ai_addr;
			salen = s->ai_addr->sa_len;
			m_addr.sadb_address_len = PFKEY_UNIT64(sizeof(m_addr) +
			    PFKEY_ALIGN8(salen));
			m_addr.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
			m_addr.sadb_address_proto = upper;
			m_addr.sadb_address_prefixlen =
			    (splen >= 0 ? splen : plen);
			m_addr.sadb_address_reserved = 0;

			setvarbuf(buf, &l, (struct sadb_ext *)&m_addr,
			    sizeof(m_addr), (caddr_t)sa, salen);

			/* set dst */
			sa = d->ai_addr;
			salen = d->ai_addr->sa_len;
			m_addr.sadb_address_len = PFKEY_UNIT64(sizeof(m_addr) +
			    PFKEY_ALIGN8(salen));
			m_addr.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
			m_addr.sadb_address_proto = upper;
			m_addr.sadb_address_prefixlen =
			    (dplen >= 0 ? dplen : plen);
			m_addr.sadb_address_reserved = 0;

			setvarbuf(buf, &l, (struct sadb_ext *)&m_addr,
			    sizeof(m_addr), (caddr_t)sa, salen);

			msg->sadb_msg_len = PFKEY_UNIT64(l);

			sendkeymsg(buf, l);

			n++;
		}
	}

	if (n == 0)
		return -1;
	else
		return 0;
}

/* XXX NO BUFFER OVERRUN CHECK! BAD BAD! */
static int
setkeymsg_addr(type, satype, srcs, dsts, no_spi)
	unsigned int type;
	unsigned int satype;
	struct addrinfo *srcs;
	struct addrinfo *dsts;
	int no_spi;
{
	struct sadb_msg *msg;
	char buf[BUFSIZ];
	int l, l0, len;
	struct sadb_sa m_sa;
	struct sadb_x_sa2 m_sa2;
	struct sadb_x_sa_replay m_replay;
	struct sadb_address m_addr;
	struct addrinfo *s, *d;
	int n;
	int plen;
	struct sockaddr *sa;
	int salen;

	msg = (struct sadb_msg *)buf;

	if (!srcs || !dsts)
		return -1;

	/* fix up length afterwards */
	setkeymsg0(msg, type, satype, 0);
	l = sizeof(struct sadb_msg);

	if (!no_spi) {
		len = sizeof(struct sadb_sa);
		m_sa.sadb_sa_len = PFKEY_UNIT64(len);
		m_sa.sadb_sa_exttype = SADB_EXT_SA;
		m_sa.sadb_sa_spi = htonl(p_spi);
		m_sa.sadb_sa_replay = p_replay > UINT8_MAX ? UINT8_MAX:
		    p_replay;
		m_sa.sadb_sa_state = 0;
		m_sa.sadb_sa_auth = p_alg_auth;
		m_sa.sadb_sa_encrypt = p_alg_enc;
		m_sa.sadb_sa_flags = p_ext;

		memcpy(buf + l, &m_sa, len);
		l += len;

		len = sizeof(struct sadb_x_sa2);
		m_sa2.sadb_x_sa2_len = PFKEY_UNIT64(len);
		m_sa2.sadb_x_sa2_exttype = SADB_X_EXT_SA2;
		m_sa2.sadb_x_sa2_mode = p_mode;
		m_sa2.sadb_x_sa2_reqid = p_reqid;

		memcpy(buf + l, &m_sa2, len);
		l += len;

		if (p_replay > UINT8_MAX) {
			len = sizeof(struct sadb_x_sa_replay);
			m_replay.sadb_x_sa_replay_len = PFKEY_UNIT64(len);
			m_replay.sadb_x_sa_replay_exttype =
			    SADB_X_EXT_SA_REPLAY;
			m_replay.sadb_x_sa_replay_replay = p_replay << 3;

			memcpy(buf + l, &m_replay, len);
			l += len;
		}
	}

	l0 = l;
	n = 0;

	/* do it for all src/dst pairs */
	for (s = srcs; s; s = s->ai_next) {
		for (d = dsts; d; d = d->ai_next) {
			/* rewind pointer */
			l = l0;

			if (s->ai_addr->sa_family != d->ai_addr->sa_family)
				continue;
			switch (s->ai_addr->sa_family) {
			case AF_INET:
				plen = sizeof(struct in_addr) << 3;
				break;
#ifdef INET6
			case AF_INET6:
				plen = sizeof(struct in6_addr) << 3;
				break;
#endif
			default:
				continue;
			}

			/* set src */
			sa = s->ai_addr;
			salen = s->ai_addr->sa_len;
			m_addr.sadb_address_len = PFKEY_UNIT64(sizeof(m_addr) +
			    PFKEY_ALIGN8(salen));
			m_addr.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
			m_addr.sadb_address_proto = IPSEC_ULPROTO_ANY;
			m_addr.sadb_address_prefixlen = plen;
			m_addr.sadb_address_reserved = 0;

			setvarbuf(buf, &l, (struct sadb_ext *)&m_addr,
			    sizeof(m_addr), (caddr_t)sa, salen);

			/* set dst */
			sa = d->ai_addr;
			salen = d->ai_addr->sa_len;
			m_addr.sadb_address_len = PFKEY_UNIT64(sizeof(m_addr) +
			    PFKEY_ALIGN8(salen));
			m_addr.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
			m_addr.sadb_address_proto = IPSEC_ULPROTO_ANY;
			m_addr.sadb_address_prefixlen = plen;
			m_addr.sadb_address_reserved = 0;

			setvarbuf(buf, &l, (struct sadb_ext *)&m_addr,
			    sizeof(m_addr), (caddr_t)sa, salen);

			msg->sadb_msg_len = PFKEY_UNIT64(l);

			sendkeymsg(buf, l);

			n++;
		}
	}

	if (n == 0)
		return -1;
	else
		return 0;
}

/* XXX NO BUFFER OVERRUN CHECK! BAD BAD! */
static int
setkeymsg_add(type, satype, srcs, dsts)
	unsigned int type;
	unsigned int satype;
	struct addrinfo *srcs;
	struct addrinfo *dsts;
{
	struct sadb_msg *msg;
	char buf[BUFSIZ];
	int l, l0, len;
	struct sadb_sa m_sa;
	struct sadb_x_sa2 m_sa2;
	struct sadb_address m_addr;
	struct sadb_x_sa_replay m_replay;
	struct addrinfo *s, *d;
	int n;
	int plen;
	struct sockaddr *sa;
	int salen;

	msg = (struct sadb_msg *)buf;

	if (!srcs || !dsts)
		return -1;

	/* fix up length afterwards */
	setkeymsg0(msg, type, satype, 0);
	l = sizeof(struct sadb_msg);

	/* set encryption algorithm, if present. */
	if (satype != SADB_X_SATYPE_IPCOMP && p_key_enc) {
		struct sadb_key m_key;

		m_key.sadb_key_len =
			PFKEY_UNIT64(sizeof(m_key)
				   + PFKEY_ALIGN8(p_key_enc_len));
		m_key.sadb_key_exttype = SADB_EXT_KEY_ENCRYPT;
		m_key.sadb_key_bits = p_key_enc_len * 8;
		m_key.sadb_key_reserved = 0;

		setvarbuf(buf, &l,
			(struct sadb_ext *)&m_key, sizeof(m_key),
			(caddr_t)p_key_enc, p_key_enc_len);
	}

	/* set authentication algorithm, if present. */
	if (p_key_auth) {
		struct sadb_key m_key;

		m_key.sadb_key_len =
			PFKEY_UNIT64(sizeof(m_key)
				   + PFKEY_ALIGN8(p_key_auth_len));
		m_key.sadb_key_exttype = SADB_EXT_KEY_AUTH;
		m_key.sadb_key_bits = p_key_auth_len * 8;
		m_key.sadb_key_reserved = 0;

		setvarbuf(buf, &l,
			(struct sadb_ext *)&m_key, sizeof(m_key),
			(caddr_t)p_key_auth, p_key_auth_len);
	}

	/* set lifetime for HARD */
	if (p_lt_hard != 0) {
		struct sadb_lifetime m_lt;
		u_int slen = sizeof(struct sadb_lifetime);

		m_lt.sadb_lifetime_len = PFKEY_UNIT64(slen);
		m_lt.sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
		m_lt.sadb_lifetime_allocations = 0;
		m_lt.sadb_lifetime_bytes = 0;
		m_lt.sadb_lifetime_addtime = p_lt_hard;
		m_lt.sadb_lifetime_usetime = 0;

		memcpy(buf + l, &m_lt, slen);
		l += slen;
	}

	/* set lifetime for SOFT */
	if (p_lt_soft != 0) {
		struct sadb_lifetime m_lt;
		u_int slen = sizeof(struct sadb_lifetime);

		m_lt.sadb_lifetime_len = PFKEY_UNIT64(slen);
		m_lt.sadb_lifetime_exttype = SADB_EXT_LIFETIME_SOFT;
		m_lt.sadb_lifetime_allocations = 0;
		m_lt.sadb_lifetime_bytes = 0;
		m_lt.sadb_lifetime_addtime = p_lt_soft;
		m_lt.sadb_lifetime_usetime = 0;

		memcpy(buf + l, &m_lt, slen);
		l += slen;
	}

	len = sizeof(struct sadb_sa);
	m_sa.sadb_sa_len = PFKEY_UNIT64(len);
	m_sa.sadb_sa_exttype = SADB_EXT_SA;
	m_sa.sadb_sa_spi = htonl(p_spi);
	m_sa.sadb_sa_replay = p_replay > UINT8_MAX ? UINT8_MAX: p_replay;
	m_sa.sadb_sa_state = 0;
	m_sa.sadb_sa_auth = p_alg_auth;
	m_sa.sadb_sa_encrypt = p_alg_enc;
	m_sa.sadb_sa_flags = p_ext;

	memcpy(buf + l, &m_sa, len);
	l += len;

	len = sizeof(struct sadb_x_sa2);
	m_sa2.sadb_x_sa2_len = PFKEY_UNIT64(len);
	m_sa2.sadb_x_sa2_exttype = SADB_X_EXT_SA2;
	m_sa2.sadb_x_sa2_mode = p_mode;
	m_sa2.sadb_x_sa2_reqid = p_reqid;

	memcpy(buf + l, &m_sa2, len);
	l += len;

	if (p_replay > UINT8_MAX) {
		len = sizeof(struct sadb_x_sa_replay);
		m_replay.sadb_x_sa_replay_len = PFKEY_UNIT64(len);
		m_replay.sadb_x_sa_replay_exttype = SADB_X_EXT_SA_REPLAY;
		m_replay.sadb_x_sa_replay_replay = p_replay << 3;

		memcpy(buf + l, &m_replay, len);
		l += len;
	}
	l0 = l;
	n = 0;

	/* do it for all src/dst pairs */
	for (s = srcs; s; s = s->ai_next) {
		for (d = dsts; d; d = d->ai_next) {
			/* rewind pointer */
			l = l0;

			if (s->ai_addr->sa_family != d->ai_addr->sa_family)
				continue;
			switch (s->ai_addr->sa_family) {
			case AF_INET:
				plen = sizeof(struct in_addr) << 3;
				break;
#ifdef INET6
			case AF_INET6:
				plen = sizeof(struct in6_addr) << 3;
				break;
#endif
			default:
				continue;
			}

			/* set src */
			sa = s->ai_addr;
			salen = s->ai_addr->sa_len;
			m_addr.sadb_address_len = PFKEY_UNIT64(sizeof(m_addr) +
			    PFKEY_ALIGN8(salen));
			m_addr.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
			m_addr.sadb_address_proto = IPSEC_ULPROTO_ANY;
			m_addr.sadb_address_prefixlen = plen;
			m_addr.sadb_address_reserved = 0;

			setvarbuf(buf, &l, (struct sadb_ext *)&m_addr,
			    sizeof(m_addr), (caddr_t)sa, salen);

			/* set dst */
			sa = d->ai_addr;
			salen = d->ai_addr->sa_len;
			m_addr.sadb_address_len = PFKEY_UNIT64(sizeof(m_addr) +
			    PFKEY_ALIGN8(salen));
			m_addr.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
			m_addr.sadb_address_proto = IPSEC_ULPROTO_ANY;
			m_addr.sadb_address_prefixlen = plen;
			m_addr.sadb_address_reserved = 0;

			setvarbuf(buf, &l, (struct sadb_ext *)&m_addr,
			    sizeof(m_addr), (caddr_t)sa, salen);

			msg->sadb_msg_len = PFKEY_UNIT64(l);

			sendkeymsg(buf, l);

			n++;
		}
	}

	if (n == 0)
		return -1;
	else
		return 0;
}

static struct addrinfo *
parse_addr(host, port)
	char *host;
	char *port;
{
	struct addrinfo hints, *res = NULL;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = p_aifamily;
	hints.ai_socktype = SOCK_DGRAM;		/*dummy*/
	hints.ai_protocol = IPPROTO_UDP;	/*dummy*/
	hints.ai_flags = p_aiflags;
	error = getaddrinfo(host, port, &hints, &res);
	if (error != 0) {
		yyerror(gai_strerror(error));
		return NULL;
	}
	return res;
}

static int
fix_portstr(spec, sport, dport)
	vchar_t *spec, *sport, *dport;
{
	char *p, *p2;
	u_int l;

	l = 0;
	for (p = spec->buf; *p != ',' && *p != '\0' && l < spec->len; p++, l++)
		;
	if (*p == '\0') {
		p2 = "0";
	} else {
		if (*p == ',') {
			*p = '\0';
			p2 = ++p;
		}
		for (p = p2; *p != '\0' && l < spec->len; p++, l++)
			;
		if (*p != '\0' || *p2 == '\0') {
			yyerror("invalid an upper layer protocol spec");
			return -1;
		}
	}

	sport->buf = strdup(spec->buf);
	if (!sport->buf) {
		yyerror("insufficient memory");
		return -1;
	}
	sport->len = strlen(sport->buf);
	dport->buf = strdup(p2);
	if (!dport->buf) {
		yyerror("insufficient memory");
		return -1;
	}
	dport->len = strlen(dport->buf);

	return 0;
}

static int
setvarbuf(buf, off, ebuf, elen, vbuf, vlen)
	char *buf;
	int *off;
	struct sadb_ext *ebuf;
	int elen;
	caddr_t vbuf;
	int vlen;
{
	memset(buf + *off, 0, PFKEY_UNUNIT64(ebuf->sadb_ext_len));
	memcpy(buf + *off, (caddr_t)ebuf, elen);
	memcpy(buf + *off + elen, vbuf, vlen);
	(*off) += PFKEY_ALIGN8(elen + vlen);

	return 0;
}

void
parse_init()
{
	p_spi = 0;

	p_ext = SADB_X_EXT_CYCSEQ;
	p_alg_enc = SADB_EALG_NONE;
	p_alg_auth = SADB_AALG_NONE;
	p_mode = IPSEC_MODE_ANY;
	p_reqid = 0;
	p_replay = 0;
	p_key_enc_len = p_key_auth_len = 0;
	p_key_enc = p_key_auth = 0;
	p_lt_hard = p_lt_soft = 0;

	p_aiflags = 0;
	p_aifamily = PF_UNSPEC;

	return;
}

void
free_buffer()
{
	/* we got tons of memory leaks in the parser anyways, leave them */

	return;
}
