/*	$OpenBSD: print-hsrp.c,v 1.5 2015/11/16 00:16:39 mmcc Exp $	*/

/*
 * Copyright (C) 2001 Julian Cowley
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

/* Cisco Hot Standby Router Protocol (HSRP). */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

#include <stdio.h>
#include <netinet/in.h>

#include "interface.h"
#include "addrtoname.h"

/* HSRP op code types. */
static const char *op_code_str[] = {
	"hello",
	"coup",
	"resign"
};

/* HSRP states and associated names. */
static struct tok states[] = {
	{  0, "initial" },
	{  1, "learn" },
	{  2, "listen" },
	{  4, "speak" },
	{  8, "standby" },
	{ 16, "active" },
	{  0, NULL }
};

/*
 * RFC 2281:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Version     |   Op Code     |     State     |   Hellotime   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Holdtime    |   Priority    |     Group     |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Authentication  Data                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Authentication  Data                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Virtual IP Address                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define HSRP_AUTH_SIZE	8

/* HSRP protocol header. */
struct hsrp {
	u_char		hsrp_version;
	u_char		hsrp_op_code;
	u_char		hsrp_state;
	u_char		hsrp_hellotime;
	u_char		hsrp_holdtime;
	u_char		hsrp_priority;
	u_char		hsrp_group;
	u_char		hsrp_reserved;
	u_char		hsrp_authdata[HSRP_AUTH_SIZE];
	struct in_addr	hsrp_virtaddr;
};

void
hsrp_print(const u_char *bp, u_int len)
{
	struct hsrp *hp = (struct hsrp *) bp;

	TCHECK(hp->hsrp_version);
	printf("HSRPv%d", hp->hsrp_version);
	if (hp->hsrp_version != 0)
		return;
	TCHECK(hp->hsrp_op_code);
	printf("-");
	if (hp->hsrp_op_code >= sizeof(op_code_str)/sizeof(*op_code_str))
		printf("unknown (%d) ", hp->hsrp_op_code);
	else
		printf("%s ", op_code_str[hp->hsrp_op_code]);
	printf("%d: ", len);
	TCHECK(hp->hsrp_state);
	printf("state=%s ", tok2str(states, "Unknown (%d)", hp->hsrp_state));
	TCHECK(hp->hsrp_group);
	printf("group=%d ", hp->hsrp_group);
	TCHECK(hp->hsrp_reserved);
	if (hp->hsrp_reserved != 0) {
		printf("[reserved=%d!] ", hp->hsrp_reserved);
	}
	TCHECK2(hp->hsrp_virtaddr, sizeof(hp->hsrp_virtaddr));
	printf("addr=%s", ipaddr_string(&hp->hsrp_virtaddr));
	if (vflag) {
		printf(" hellotime=");
		relts_print(hp->hsrp_hellotime);
		printf(" holdtime=");
		relts_print(hp->hsrp_holdtime);
		printf(" priority=%d", hp->hsrp_priority);
		printf(" auth=\"");
		fn_printn(hp->hsrp_authdata, sizeof(hp->hsrp_authdata), NULL);
		printf("\"");
	}
	return;
trunc:
	printf("[|hsrp]");
}
