/*	$KAME: parser.y,v 1.8 2000/11/08 03:03:34 jinmei Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 *
 * $FreeBSD$
 */

%{
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/queue.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/icmp6.h>

#include <limits.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

#include "rrenumd.h"

struct config_is_set {
	u_short cis_dest : 1;
} cis;

struct dst_list *dl_head;
struct payload_list *pl_head, ple_cur;
u_int retry;
char errbuf[LINE_MAX];

extern int lineno;
extern void yyerror(const char *s);
extern int yylex(void);
static struct payload_list * pllist_lookup(int seqnum);
static void pllist_enqueue(struct payload_list *pl_entry);

#define MAX_RETRYNUM 10 /* upper limit of retry in this rrenumd program */
#define MAX_SEQNUM 256 /* upper limit of seqnum in this rrenumd program */
#define NOSPEC	-1

%}

%union {
	u_long num;
	struct {
		char *cp;
		int len;
	} cs;
	struct in_addr addr4;
	struct in6_addr addr6;
	struct {
		struct in6_addr addr;
		u_char plen;
	} prefix;
	struct dst_list *dl;
	struct payload_list *pl;
	struct sockaddr *sa;
}

%token <num> ADD CHANGE SETGLOBAL
%token DEBUG_CMD DEST_CMD RETRY_CMD SEQNUM_CMD
%token MATCH_PREFIX_CMD MAXLEN_CMD MINLEN_CMD
%token USE_PREFIX_CMD KEEPLEN_CMD
%token VLTIME_CMD PLTIME_CMD
%token RAF_ONLINK_CMD RAF_AUTO_CMD RAF_DECRVALID_CMD RAF_DECRPREFD_CMD
%token <num> DAYS HOURS MINUTES SECONDS INFINITY
%token <num> ON OFF
%token BCL ECL EOS ERROR
%token <cs> NAME HOSTNAME QSTRING DECSTRING
%token <addr4> IPV4ADDR
%token <addr6> IPV6ADDR
%token <num> PREFIXLEN

%type <num> retrynum seqnum rrenum_cmd
%type <num> prefixlen maxlen minlen keeplen vltime pltime
%type <num> lifetime days hours minutes seconds
%type <num> decstring
%type <num> raf_onlink raf_auto raf_decrvalid raf_decrprefd flag
%type <dl> dest_addrs dest_addr sin sin6
%type <pl> rrenum_statement
%type <cs> ifname
%type <prefix> prefixval

%%
config:
		/* empty */
	| 	statements
	;

statements:
		statement
	| 	statements statement
	;

statement:
		debug_statement
	|	destination_statement
	|	rrenum_statement_without_seqnum
	|	rrenum_statement_with_seqnum
	|	error EOS
		{
			yyerrok;
		}
	|	EOS
	;

debug_statement:
		DEBUG_CMD flag EOS
		{
#ifdef YYDEBUG
			yydebug = $2;
#endif /* YYDEBUG */
		}
	;

destination_statement:
		DEST_CMD dest_addrs retrynum EOS
		{
			dl_head = $2;
			retry = $3;
		}
	;

dest_addrs:
		dest_addr
	|	dest_addrs dest_addr
		{
			$2->dl_next = $1;
			$$ = $2;
		}
	;

dest_addr :
		sin
		{
			with_v4dest = 1;
		}
	|	sin6
		{
			with_v6dest = 1;
		}
	|	sin6 ifname
		{
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)$1->dl_dst;
			sin6->sin6_scope_id = if_nametoindex($2.cp);
			with_v6dest = 1;
			$$ = $1;
		}
	|	HOSTNAME
		{
			struct sockaddr_storage *ss;
			struct addrinfo hints, *res;
			int error;

			memset(&hints, 0, sizeof(hints));
			hints.ai_flags = AI_CANONNAME;
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_RAW;
			hints.ai_protocol = 0;
			error = getaddrinfo($1.cp, 0, &hints, &res);
			if (error) {
				snprintf(errbuf, sizeof(errbuf),
				    "name resolution failed for %s:%s",
				    $1.cp, gai_strerror(error));
				yyerror(errbuf);
			}
			ss = (struct sockaddr_storage *)malloc(sizeof(*ss));
			memset(ss, 0, sizeof(*ss));
			memcpy(ss, res->ai_addr, res->ai_addr->sa_len);
			freeaddrinfo(res);

			$$ = (struct dst_list *)
			     malloc(sizeof(struct dst_list));
			memset($$, 0, sizeof(struct dst_list));
			$$->dl_dst = (struct sockaddr *)ss;
		}
	;

sin:
		IPV4ADDR
		{
			struct sockaddr_in *sin;

			sin = (struct sockaddr_in *)malloc(sizeof(*sin));
			memset(sin, 0, sizeof(*sin));
			sin->sin_len = sizeof(*sin);
			sin->sin_family = AF_INET;
			sin->sin_addr = $1;

			$$ = (struct dst_list *)
			     malloc(sizeof(struct dst_list));
			memset($$, 0, sizeof(struct dst_list));
			$$->dl_dst = (struct sockaddr *)sin;
		}
	;

sin6:
		IPV6ADDR
		{
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)malloc(sizeof(*sin6));
			memset(sin6, 0, sizeof(*sin6));
			sin6->sin6_len = sizeof(*sin6);
			sin6->sin6_family = AF_INET6;
			sin6->sin6_addr = $1;

			$$ = (struct dst_list *)
			     malloc(sizeof(struct dst_list));
			memset($$, 0, sizeof(struct dst_list));
			$$->dl_dst = (struct sockaddr *)sin6;
		}

ifname:
		NAME
		{
			$$.cp = strdup($1.cp);
			$$.len = $1.len;
		}
	|	QSTRING
		{
			$1.cp[$1.len - 1] = 0;
			$$.cp = strdup(&$1.cp[1]);
			$$.len = $1.len - 2;
		}
	;

retrynum:
		/* empty */
		{
			$$ = 2;
		}
	|	RETRY_CMD decstring
		{
			if ($2 > MAX_RETRYNUM)
				$2 = MAX_RETRYNUM;
			$$ = $2;
		}
	;

rrenum_statement_with_seqnum:
		SEQNUM_CMD seqnum
		{
			if (pllist_lookup($2)) {
				snprintf(errbuf, sizeof(errbuf),
				    "duplicate seqnum %ld specified at %d",
				    $2, lineno);
				yyerror(errbuf);
			}
		}
		BCL rrenum_statement EOS ECL EOS
		{
			$5->pl_irr.rr_seqnum = $2;
			pllist_enqueue($5);
		}
	;

seqnum:
		/* empty */
		{
			$$ = 0;
		}
	|	decstring
		{
			if ($1 > MAX_SEQNUM) {
				snprintf(errbuf, sizeof(errbuf),
				    "seqnum %ld is illegal for this  program. "
				    "should be between 0 and %d",
				    $1, MAX_SEQNUM);
				yyerror(errbuf);
			}
			$$ = $1;
		}
	;

rrenum_statement_without_seqnum:
		rrenum_statement EOS
		{
			if (pllist_lookup(0)) {
				snprintf(errbuf, sizeof(errbuf),
				    "duplicate seqnum %d specified  at %d",
				    0, lineno);
				yyerror(errbuf);
			}
			$1->pl_irr.rr_seqnum = 0;
			pllist_enqueue($1);
		}
	;

rrenum_statement:
		match_prefix_definition use_prefix_definition
		{
			$$ = (struct payload_list *)
			     malloc(sizeof(struct payload_list));
			memcpy($$, &ple_cur, sizeof(ple_cur));
		}
	;

match_prefix_definition:
		rrenum_cmd MATCH_PREFIX_CMD prefixval maxlen minlen
		{
			struct icmp6_router_renum *irr;
			struct rr_pco_match *rpm;

			irr = &ple_cur.pl_irr;
			rpm = &ple_cur.pl_rpm;
			memset(rpm, 0, sizeof(*rpm));

			rpm->rpm_code = $1;
			rpm->rpm_prefix = $3.addr;
			rpm->rpm_matchlen = $3.plen;
			rpm->rpm_maxlen = $4;
			rpm->rpm_minlen = $5;
		}
	;

rrenum_cmd:
		/* empty */
		{
			$$ = RPM_PCO_ADD;
		}
	|	ADD
	|	CHANGE
	|	SETGLOBAL
	;

prefixval:
		IPV6ADDR prefixlen
		{
			$$.addr = $1;
			$$.plen = $2;
		}
	;

prefixlen:
		/* empty */
		{
			$$ = 64;
		}
	|	PREFIXLEN
	;

maxlen:
		/* empty */
		{
			$$ = 128;
		}
	|	MAXLEN_CMD decstring
		{
			if ($2 > 128)
				$2 = 128;
			$$ = $2;
		}
	;

minlen:
		/* empty */
		{
			$$ = 0;
		}
	|	MINLEN_CMD decstring
		{
			if ($2 > 128)
				$2 = 128;
			$$ = $2;
		}
	;

use_prefix_definition:
		/* empty */
		{
			struct icmp6_router_renum *irr;
			struct rr_pco_match *rpm;
			struct rr_pco_use *rpu;

			irr = (struct icmp6_router_renum *)&ple_cur.pl_irr;
			rpm = (struct rr_pco_match *)(irr + 1);
			rpu = (struct rr_pco_use *)(rpm + 1);
			memset(rpu, 0, sizeof(*rpu));
		}
	|	USE_PREFIX_CMD prefixval keeplen use_prefix_values
		{
			struct icmp6_router_renum *irr;
			struct rr_pco_match *rpm;
			struct rr_pco_use *rpu;

			irr = (struct icmp6_router_renum *)&ple_cur.pl_irr;
			rpm = (struct rr_pco_match *)(irr + 1);
			rpu = (struct rr_pco_use *)(rpm + 1);

			rpu->rpu_prefix = $2.addr;
			rpu->rpu_uselen = $2.plen;
			rpu->rpu_keeplen = $3;
		}
	;

use_prefix_values:
		/* empty */
		{
			struct icmp6_router_renum *irr;
			struct rr_pco_match *rpm;
			struct rr_pco_use *rpu;

			irr = (struct icmp6_router_renum *)&ple_cur.pl_irr;
			rpm = (struct rr_pco_match *)(irr + 1);
			rpu = (struct rr_pco_use *)(rpm + 1);
			memset(rpu, 0, sizeof(*rpu));

			rpu->rpu_vltime = htonl(DEF_VLTIME);
			rpu->rpu_pltime = htonl(DEF_PLTIME);
			rpu->rpu_ramask = 0;
			rpu->rpu_flags = 0;
		}
	|	BCL vltime pltime raf_onlink raf_auto raf_decrvalid raf_decrprefd ECL
		{
			struct icmp6_router_renum *irr;
			struct rr_pco_match *rpm;
			struct rr_pco_use *rpu;

			irr = (struct icmp6_router_renum *)&ple_cur.pl_irr;
			rpm = (struct rr_pco_match *)(irr + 1);
			rpu = (struct rr_pco_use *)(rpm + 1);
			memset(rpu, 0, sizeof(*rpu));

			rpu->rpu_vltime = $2;
			rpu->rpu_pltime = $3;
			if ($4 == NOSPEC) {
				rpu->rpu_ramask &=
				    ~ICMP6_RR_PCOUSE_RAFLAGS_ONLINK;
			} else {
				rpu->rpu_ramask |=
				    ICMP6_RR_PCOUSE_RAFLAGS_ONLINK;
				if ($4 == ON) {
					rpu->rpu_raflags |=
					    ICMP6_RR_PCOUSE_RAFLAGS_ONLINK;
				} else {
					rpu->rpu_raflags &=
					    ~ICMP6_RR_PCOUSE_RAFLAGS_ONLINK;
				}
			}
			if ($5 == NOSPEC) {
				rpu->rpu_ramask &=
				    ICMP6_RR_PCOUSE_RAFLAGS_AUTO;
			} else {
				rpu->rpu_ramask |=
				    ICMP6_RR_PCOUSE_RAFLAGS_AUTO;
				if ($5 == ON) {
					rpu->rpu_raflags |=
					    ICMP6_RR_PCOUSE_RAFLAGS_AUTO;
				} else {
					rpu->rpu_raflags &=
					    ~ICMP6_RR_PCOUSE_RAFLAGS_AUTO;
				}
			}
			rpu->rpu_flags = 0;
			if ($6 == ON) {
				rpu->rpu_flags |=
				    ICMP6_RR_PCOUSE_FLAGS_DECRVLTIME;
			}
			if ($7 == ON) {
				rpu->rpu_flags |=
				    ICMP6_RR_PCOUSE_FLAGS_DECRPLTIME;
			}
		}
	;

keeplen:
		/* empty */
		{
			$$ = 0;
		}
	|	KEEPLEN_CMD decstring
		{
			if ($2 > 128)
				$2 = 128;
			$$ = $2;
		}
	;


vltime:
		/* empty */
		{
			$$ = htonl(DEF_VLTIME);
		}
	|	VLTIME_CMD lifetime
		{
			$$ = htonl($2);
		}
	;

pltime:
		/* empty */
		{
			$$ = htonl(DEF_PLTIME);
		}
	|	PLTIME_CMD lifetime
		{
			$$ = htonl($2);
		}

raf_onlink:
		/* empty */
		{
			$$ = NOSPEC;
		}
	|	RAF_ONLINK_CMD flag
		{
			$$ = $2;
		}
	;

raf_auto:
		/* empty */
		{
			$$ = NOSPEC;
		}
	|	RAF_AUTO_CMD flag
		{
			$$ = $2;
		}
	;

raf_decrvalid:
		/* empty */
		{
			$$ = NOSPEC;
		}
	|	RAF_DECRVALID_CMD flag
		{
			$$ = $2;
		}
	;

raf_decrprefd:
		/* empty */
		{
			$$ = NOSPEC;
		}
	|	RAF_DECRPREFD_CMD flag
		{
			$$ = $2;
		}
	;

flag:
		ON { $$ = ON; }
	|	OFF { $$ = OFF; }
	;

lifetime:
		decstring
	|	INFINITY
		{
			$$ = 0xffffffff;
		}
	|	days hours minutes seconds
		{
			int d, h, m, s;

			d = $1 * 24 * 60 * 60;
			h = $2 * 60 * 60;
			m = $3 * 60;
			s = $4;
			$$ = d + h + m + s;
		}
	;

days:
		/* empty */
		{
			$$ = 0;
		}
	|	DAYS
	;

hours:
		/* empty */
		{
			$$ = 0;
		}
	|	HOURS
	;

minutes:
		/* empty */
		{
			$$ = 0;
		}
	|	MINUTES
	;

seconds:
		/* empty */
		{
			$$ = 0;
		}
	|	SECONDS
	;

decstring:
		DECSTRING
		{
			int dval;

			dval = atoi($1.cp);
			$$ = dval;
		}
	;

%%

static struct payload_list *
pllist_lookup(int seqnum)
{
	struct payload_list *pl;
	for (pl = pl_head; pl && pl->pl_irr.rr_seqnum != seqnum;
	     pl = pl->pl_next)
		continue;
	return (pl);
}

static void
pllist_enqueue(struct payload_list *pl_entry)
{
	struct payload_list *pl, *pl_last;

	pl_last = NULL;
	for (pl = pl_head;
	     pl && pl->pl_irr.rr_seqnum < pl_entry->pl_irr.rr_seqnum;
	     pl_last = pl, pl = pl->pl_next)
		continue;
	if (pl_last)
		pl_last->pl_next = pl_entry;
	else
		pl_head = pl_entry;

	return;
}
