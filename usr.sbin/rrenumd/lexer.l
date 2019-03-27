/*	$KAME: lexer.l,v 1.7 2000/11/08 02:40:53 itojun Exp $	*/

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
#include <sys/queue.h>

#include <string.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/icmp6.h>

#include <arpa/inet.h>

#include "y.tab.h"

int lineno = 1;

#define LINEBUF_SIZE 1000
char linebuf[LINEBUF_SIZE];

int parse(FILE **);
void yyerror(const char *);
int yylex(void);
%}

%option nounput

/* common section */
nl		\n
ws		[ \t]+
digit		[0-9]
letter		[0-9A-Za-z]
hexdigit	[0-9A-Fa-f]
special		[()+\|\?\*,]
dot		\.
hyphen		\-
colon		\:
slash		\/
bcl		\{
ecl		\}
semi		\;
usec		{dot}{digit}{1,6}
comment		\#.*
qstring		\"[^"]*\"
decstring	{digit}+
hexpair		{hexdigit}{hexdigit}
hexstring	0[xX]{hexdigit}+
octetstring	{octet}({dot}{octet})+
ipv4addr	{digit}{1,3}({dot}{digit}{1,3}){0,3}
ipv6addr	{hexdigit}{0,4}({colon}{hexdigit}{0,4}){2,7}
ipaddrmask	{slash}{digit}{1,3}
keyword		{letter}{letter}+
name		{letter}(({letter}|{digit}|{hyphen})*({letter}|{digit}))*
hostname	{name}(({dot}{name})+{dot}?)?

timeval		{digit}{0,2}
days		d{timeval}
hours		h{timeval}
minutes		m{timeval}
seconds		s{timeval}

mprefix		match_prefix|match-prefix
uprefix		use_prefix|use-prefix

%%
	/* rrenumd keywords */
debug		{
			return(DEBUG_CMD);
		}
dest		{
			return(DEST_CMD);
		}
retry		{
			return(RETRY_CMD);
		}
seqnum		{
			return(SEQNUM_CMD);
		}
add		{
			yylval.num = RPM_PCO_ADD;
			return(ADD);
		}
change		{
			yylval.num = RPM_PCO_CHANGE;
			return(CHANGE);
		 }
setglobal	{
			yylval.num = RPM_PCO_SETGLOBAL;
			return(SETGLOBAL);
		}
{mprefix}	{
			return(MATCH_PREFIX_CMD);
		}
maxlen		{
			return(MAXLEN_CMD);
		}
minlen		{
			return(MINLEN_CMD);
		}
{uprefix}	{
			return(USE_PREFIX_CMD);
		}
keeplen		{
			return(KEEPLEN_CMD);
		}

vltime		{
			return(VLTIME_CMD);
		}
pltime		{
			return(PLTIME_CMD);
		}
raf_onlink	{
			return(RAF_ONLINK_CMD);
		}
raf_auto	{
			return(RAF_AUTO_CMD);
		}
rrf_decrvalid	{
			return(RAF_DECRVALID_CMD);
		}
rrf_decrprefd	{
			return(RAF_DECRPREFD_CMD);
		}
{days}		{
			yytext++;
			yylval.num = atoi(yytext);
			return(DAYS);
		}
{hours}		{
			yytext++;
			yylval.num = atoi(yytext);
			return(HOURS);
		}
{minutes}	{
			yytext++;
			yylval.num = atoi(yytext);
			return(MINUTES);
		}
{seconds}	{
			yytext++;
			yylval.num = atoi(yytext);
			return(SECONDS);
		}
infinity	{
			return(INFINITY);
		}

on		{
			yylval.num = 1;
			return(ON);
		}
off		{
			yylval.num = 0;
			return(OFF);
		}

	/* basic rules */
{ws}		;
{nl}		{
			lineno++;
		}
{semi}		{
			return EOS;
		}
{bcl}		{
			return BCL;
		}
{ecl}		{
			return ECL;
		}
{qstring}	{
			yylval.cs.cp = yytext;
			yylval.cs.len = yyleng;
			return QSTRING;
		}
{decstring}	{
			yylval.cs.cp = yytext;
			yylval.cs.len = yyleng;
			return DECSTRING;
		}
{name}		{
			yylval.cs.cp = yytext;
			yylval.cs.len = yyleng;
			return NAME;
		}
{ipv4addr}	{
			memset(&yylval.addr4, 0, sizeof(struct in_addr));
			if (inet_pton(AF_INET, yytext,
				      &yylval.addr4) == 1) {
				return IPV4ADDR;
			} else {
				return ERROR;
			}
		}
{ipv6addr}	{
			memset(&yylval.addr6, 0, sizeof(struct in6_addr));
			if (inet_pton(AF_INET6, yytext,
				      &yylval.addr6) == 1) {
				return IPV6ADDR;
			} else {
				return ERROR;
			}
		}
{ipaddrmask}	{
			yytext++;
			yylval.num = atoi(yytext);
			return(PREFIXLEN);
		}
{hostname}	{
			yylval.cs.cp = yytext;
			yylval.cs.len = yyleng;
			return HOSTNAME;
		}
%%

int parse(FILE **fp)
{
	extern int yyparse(void);

	yyin = *fp;

	if (yyparse())
		return(-1);

	return(0);

}

void
yyerror(const char *s)
{
	printf("%s: at %s in line %d\n", s, yytext, lineno);
}
