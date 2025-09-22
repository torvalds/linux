%{
/*	$OpenBSD: grammar.y,v 1.24 2024/04/08 02:51:14 jsg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/pfvar.h>

#include <net80211/ieee80211.h>

#include <stdio.h>
#include <string.h>

#include "pcap-int.h"

#include "gencode.h"
#include <pcap-namedb.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#define QSET(q, p, d, a) (q).proto = (p),\
			 (q).dir = (d),\
			 (q).addr = (a)

int n_errors = 0;

static struct qual qerr = { Q_UNDEF, Q_UNDEF, Q_UNDEF, Q_UNDEF };

static void
yyerror(char *msg)
{
	++n_errors;
	bpf_error("%s", msg);
	/* NOTREACHED */
}

#ifndef YYBISON
int yyparse(void);

int
pcap_parse(void)
{
	return (yyparse());
}
#endif

%}

%union {
	int i;
	bpf_u_int32 h;
	u_char *e;
	char *s;
	struct stmt *stmt;
	struct arth *a;
	struct {
		struct qual q;
		struct block *b;
	} blk;
	struct block *rblk;
}

%type	<blk>	expr id nid pid term rterm qid
%type	<blk>	head
%type	<i>	pqual dqual aqual ndaqual
%type	<a>	arth narth
%type	<i>	byteop pname pnum relop irelop
%type	<blk>	and or paren not null prog
%type	<rblk>	other pfvar p80211

%token  DST SRC HOST GATEWAY
%token  NET MASK PORT LESS GREATER PROTO PROTOCHAIN BYTE
%token  ARP RARP IP TCP UDP ICMP IGMP IGRP PIM
%token  ATALK DECNET LAT SCA MOPRC MOPDL STP
%token  TK_BROADCAST TK_MULTICAST
%token  NUM INBOUND OUTBOUND
%token  PF_IFNAME PF_RSET PF_RNR PF_SRNR PF_REASON PF_ACTION
%token	TYPE SUBTYPE DIR ADDR1 ADDR2 ADDR3 ADDR4
%token  LINK
%token	GEQ LEQ NEQ
%token	ID EID HID HID6
%token	LSH RSH
%token  LEN RND SAMPLE
%token  IPV6 ICMPV6 AH ESP
%token	VLAN MPLS

%type	<s> ID
%type	<e> EID
%type	<s> HID HID6
%type	<i> NUM action reason type subtype dir

%left OR AND
%nonassoc  '!'
%left '|'
%left '&'
%left LSH RSH
%left '+' '-'
%left '*' '/'
%nonassoc UMINUS
%%
prog:	  null expr
{
	finish_parse($2.b);
}
	| null
	;
null:	  /* null */		{ $$.q = qerr; }
	;
expr:	  term
	| expr and term		{ gen_and($1.b, $3.b); $$ = $3; }
	| expr and id		{ gen_and($1.b, $3.b); $$ = $3; }
	| expr or term		{ gen_or($1.b, $3.b); $$ = $3; }
	| expr or id		{ gen_or($1.b, $3.b); $$ = $3; }
	;
and:	  AND			{ $$ = $<blk>0; }
	;
or:	  OR			{ $$ = $<blk>0; }
	;
id:	  nid
	| pnum			{ $$.b = gen_ncode(NULL, (bpf_u_int32)$1,
						   $$.q = $<blk>0.q); }
	| paren pid ')'		{ $$ = $2; }
	;
nid:	  ID			{ $$.b = gen_scode($1, $$.q = $<blk>0.q); }
	| HID '/' NUM		{ $$.b = gen_mcode($1, NULL, $3,
				    $$.q = $<blk>0.q); }
	| HID MASK HID		{ $$.b = gen_mcode($1, $3, 0,
				    $$.q = $<blk>0.q); }
	| HID			{
				  /* Decide how to parse HID based on proto */
				  $$.q = $<blk>0.q;
				  switch ($$.q.proto) {
				  case Q_DECNET:
					$$.b = gen_ncode($1, 0, $$.q);
					break;
				  default:
					$$.b = gen_ncode($1, 0, $$.q);
					break;
				  }
				}
	| HID6 '/' NUM		{
#ifdef INET6
				  $$.b = gen_mcode6($1, NULL, $3,
				    $$.q = $<blk>0.q);
#else
				  bpf_error("'ip6addr/prefixlen' not supported "
					"in this configuration");
#endif /*INET6*/
				}
	| HID6			{
#ifdef INET6
				  $$.b = gen_mcode6($1, 0, 128,
				    $$.q = $<blk>0.q);
#else
				  bpf_error("'ip6addr' not supported "
					"in this configuration");
#endif /*INET6*/
				}
	| EID			{ $$.b = gen_ecode($1, $$.q = $<blk>0.q); }
	| not id		{ gen_not($2.b); $$ = $2; }
	;
not:	  '!'			{ $$ = $<blk>0; }
	;
paren:	  '('			{ $$ = $<blk>0; }
	;
pid:	  nid
	| qid and id		{ gen_and($1.b, $3.b); $$ = $3; }
	| qid or id		{ gen_or($1.b, $3.b); $$ = $3; }
	;
qid:	  pnum			{ $$.b = gen_ncode(NULL, (bpf_u_int32)$1,
						   $$.q = $<blk>0.q); }
	| pid
	;
term:	  rterm
	| not term		{ gen_not($2.b); $$ = $2; }
	;
head:	  pqual dqual aqual	{ QSET($$.q, $1, $2, $3); }
	| pqual dqual		{ QSET($$.q, $1, $2, Q_DEFAULT); }
	| pqual aqual		{ QSET($$.q, $1, Q_DEFAULT, $2); }
	| pqual PROTO		{ QSET($$.q, $1, Q_DEFAULT, Q_PROTO); }
	| pqual PROTOCHAIN	{ QSET($$.q, $1, Q_DEFAULT, Q_PROTOCHAIN); }
	| pqual ndaqual		{ QSET($$.q, $1, Q_DEFAULT, $2); }
	;
rterm:	  head id		{ $$ = $2; }
	| paren expr ')'	{ $$.b = $2.b; $$.q = $1.q; }
	| pname			{ $$.b = gen_proto_abbrev($1); $$.q = qerr; }
	| arth relop arth	{ $$.b = gen_relation($2, $1, $3, 0);
				  $$.q = qerr; }
	| arth irelop arth	{ $$.b = gen_relation($2, $1, $3, 1);
				  $$.q = qerr; }
	| other			{ $$.b = $1; $$.q = qerr; }
	;
/* protocol level qualifiers */
pqual:	  pname
	|			{ $$ = Q_DEFAULT; }
	;
/* 'direction' qualifiers */
dqual:	  SRC			{ $$ = Q_SRC; }
	| DST			{ $$ = Q_DST; }
	| SRC OR DST		{ $$ = Q_OR; }
	| DST OR SRC		{ $$ = Q_OR; }
	| SRC AND DST		{ $$ = Q_AND; }
	| DST AND SRC		{ $$ = Q_AND; }
	| ADDR1			{ $$ = Q_ADDR1; }
	| ADDR2			{ $$ = Q_ADDR2; }
	| ADDR3			{ $$ = Q_ADDR3; }
	| ADDR4			{ $$ = Q_ADDR4; }
	;

/* address type qualifiers */
aqual:	  HOST			{ $$ = Q_HOST; }
	| NET			{ $$ = Q_NET; }
	| PORT			{ $$ = Q_PORT; }
	;
/* non-directional address type qualifiers */
ndaqual:  GATEWAY		{ $$ = Q_GATEWAY; }
	;
pname:	  LINK			{ $$ = Q_LINK; }
	| IP			{ $$ = Q_IP; }
	| ARP			{ $$ = Q_ARP; }
	| RARP			{ $$ = Q_RARP; }
	| TCP			{ $$ = Q_TCP; }
	| UDP			{ $$ = Q_UDP; }
	| ICMP			{ $$ = Q_ICMP; }
	| IGMP			{ $$ = Q_IGMP; }
	| IGRP			{ $$ = Q_IGRP; }
	| PIM			{ $$ = Q_PIM; }
	| ATALK			{ $$ = Q_ATALK; }
	| DECNET		{ $$ = Q_DECNET; }
	| LAT			{ $$ = Q_LAT; }
	| SCA			{ $$ = Q_SCA; }
	| MOPDL			{ $$ = Q_MOPDL; }
	| MOPRC			{ $$ = Q_MOPRC; }
	| IPV6			{ $$ = Q_IPV6; }
	| ICMPV6		{ $$ = Q_ICMPV6; }
	| AH			{ $$ = Q_AH; }
	| ESP			{ $$ = Q_ESP; }
	| STP			{ $$ = Q_STP; }
	;
other:	  pqual TK_BROADCAST	{ $$ = gen_broadcast($1); }
	| pqual TK_MULTICAST	{ $$ = gen_multicast($1); }
	| LESS NUM		{ $$ = gen_less($2); }
	| GREATER NUM		{ $$ = gen_greater($2); }
	| BYTE NUM byteop NUM	{ $$ = gen_byteop($3, $2, $4); }
	| INBOUND		{ $$ = gen_inbound(0); }
	| OUTBOUND		{ $$ = gen_inbound(1); }
	| VLAN pnum		{ $$ = gen_vlan($2); }
	| VLAN			{ $$ = gen_vlan(-1); }
	| MPLS pnum		{ $$ = gen_mpls($2); }
	| MPLS			{ $$ = gen_mpls(-1); }
	| pfvar			{ $$ = $1; }
	| pqual p80211		{ $$ = $2; }
	| SAMPLE NUM		{ $$ = gen_sample($2); }
	;

pfvar:	  PF_IFNAME ID		{ $$ = gen_pf_ifname($2); }
	| PF_RSET ID		{ $$ = gen_pf_ruleset($2); }
	| PF_RNR NUM		{ $$ = gen_pf_rnr($2); }
	| PF_SRNR NUM		{ $$ = gen_pf_srnr($2); }
	| PF_REASON reason	{ $$ = gen_pf_reason($2); }
	| PF_ACTION action	{ $$ = gen_pf_action($2); }
	;

reason:	  NUM			{ $$ = $1; }
	| ID			{ const char *reasons[] = PFRES_NAMES;
				  int i;
				  for (i = 0; reasons[i]; i++) {
					  if (strcasecmp($1, reasons[i]) == 0) {
						  $$ = i;
						  break;
					  }
				  }
				  if (reasons[i] == NULL)
					  bpf_error("unknown PF reason");
				}
	;

action:	  ID			{ if (strcasecmp($1, "pass") == 0 ||
				      strcasecmp($1, "accept") == 0)
					$$ = PF_PASS;
				  else if (strcasecmp($1, "drop") == 0 ||
				      strcasecmp($1, "block") == 0)
					$$ = PF_DROP;
				  else if (strcasecmp($1, "match") == 0)
					$$ = PF_MATCH;
				  else if (strcasecmp($1, "rdr") == 0)
				  	$$ = PF_RDR;
				  else if (strcasecmp($1, "nat") == 0)
				  	$$ = PF_NAT;
				  else if (strcasecmp($1, "binat") == 0)
				  	$$ = PF_BINAT;
				  else if (strcasecmp($1, "scrub") == 0)
				  	$$ = PF_SCRUB;
				  else
					  bpf_error("unknown PF action");
				}
	;

p80211:   TYPE type SUBTYPE subtype
				{ $$ = gen_p80211_type($2 | $4,
					IEEE80211_FC0_TYPE_MASK |
					IEEE80211_FC0_SUBTYPE_MASK);
				}
	| TYPE type		{ $$ = gen_p80211_type($2,
					IEEE80211_FC0_TYPE_MASK); }
	| SUBTYPE subtype	{ $$ = gen_p80211_type($2,
					IEEE80211_FC0_SUBTYPE_MASK); }
	| DIR dir		{ $$ = gen_p80211_fcdir($2); }
	;

type:	  NUM
	| ID			{ if (strcasecmp($1, "data") == 0)
					$$ = IEEE80211_FC0_TYPE_DATA;
				  else if (strcasecmp($1, "mgt") == 0 ||
					strcasecmp($1, "management") == 0)
					$$ = IEEE80211_FC0_TYPE_MGT;
				  else if (strcasecmp($1, "ctl") == 0 ||
					strcasecmp($1, "control") == 0)
					$$ = IEEE80211_FC0_TYPE_CTL;
				  else
					  bpf_error("unknown 802.11 type");
				}
	;

subtype:  NUM
	| ID			{ if (strcasecmp($1, "assocreq") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
				  else if (strcasecmp($1, "assocresp") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_ASSOC_RESP;
				  else if (strcasecmp($1, "reassocreq") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_REASSOC_REQ;
				  else if (strcasecmp($1, "reassocresp") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_REASSOC_RESP;
				  else if (strcasecmp($1, "probereq") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_PROBE_REQ;
				  else if (strcasecmp($1, "proberesp") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_PROBE_RESP;
				  else if (strcasecmp($1, "beacon") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_BEACON;
				  else if (strcasecmp($1, "atim") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_ATIM;
				  else if (strcasecmp($1, "disassoc") == 0 ||
				      strcasecmp($1, "disassociation") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_DISASSOC;
				  else if (strcasecmp($1, "auth") == 0 ||
				      strcasecmp($1, "authentication") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_AUTH;
				  else if (strcasecmp($1, "deauth") == 0 ||
				      strcasecmp($1, "deauthentication") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_DEAUTH;
				  else if (strcasecmp($1, "data") == 0)
					$$ = IEEE80211_FC0_SUBTYPE_DATA;
				  else
					  bpf_error("unknown 802.11 subtype");
				}
	;

dir:	  NUM
	| ID			{ if (strcasecmp($1, "nods") == 0)
					$$ = IEEE80211_FC1_DIR_NODS;
				  else if (strcasecmp($1, "tods") == 0)
					$$ = IEEE80211_FC1_DIR_TODS;
				  else if (strcasecmp($1, "fromds") == 0)
					$$ = IEEE80211_FC1_DIR_FROMDS;
				  else if (strcasecmp($1, "dstods") == 0)
					$$ = IEEE80211_FC1_DIR_DSTODS;
				  else
					bpf_error("unknown 802.11 direction");
				}
	;

relop:	  '>'			{ $$ = BPF_JGT; }
	| GEQ			{ $$ = BPF_JGE; }
	| '='			{ $$ = BPF_JEQ; }
	;
irelop:	  LEQ			{ $$ = BPF_JGT; }
	| '<'			{ $$ = BPF_JGE; }
	| NEQ			{ $$ = BPF_JEQ; }
	;
arth:	  pnum			{ $$ = gen_loadi($1); }
	| narth
	;
narth:	  pname '[' arth ']'		{ $$ = gen_load($1, $3, 1); }
	| pname '[' arth ':' NUM ']'	{ $$ = gen_load($1, $3, $5); }
	| arth '+' arth			{ $$ = gen_arth(BPF_ADD, $1, $3); }
	| arth '-' arth			{ $$ = gen_arth(BPF_SUB, $1, $3); }
	| arth '*' arth			{ $$ = gen_arth(BPF_MUL, $1, $3); }
	| arth '/' arth			{ $$ = gen_arth(BPF_DIV, $1, $3); }
	| arth '&' arth			{ $$ = gen_arth(BPF_AND, $1, $3); }
	| arth '|' arth			{ $$ = gen_arth(BPF_OR, $1, $3); }
	| arth LSH arth			{ $$ = gen_arth(BPF_LSH, $1, $3); }
	| arth RSH arth			{ $$ = gen_arth(BPF_RSH, $1, $3); }
	| '-' arth %prec UMINUS		{ $$ = gen_neg($2); }
	| paren narth ')'		{ $$ = $2; }
	| LEN				{ $$ = gen_loadlen(); }
	| RND				{ $$ = gen_loadrnd(); }
	;
byteop:	  '&'			{ $$ = '&'; }
	| '|'			{ $$ = '|'; }
	| '<'			{ $$ = '<'; }
	| '>'			{ $$ = '>'; }
	| '='			{ $$ = '='; }
	;
pnum:	  NUM
	| paren pnum ')'	{ $$ = $2; }
	;
%%
