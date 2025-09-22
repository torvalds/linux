/*	$OpenBSD: print-sunrpc.c,v 1.23 2023/09/06 05:54:07 jsg Exp $	*/

/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
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
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <rpc/rpc.h>
#ifdef HAVE_RPC_RPCENT_H
#include <rpc/rpcent.h>
#endif
#include <rpc/pmap_prot.h>

#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "privsep.h"

static struct tok proc2str[] = {
	{ PMAPPROC_NULL,	"null" },
	{ PMAPPROC_SET,		"set" },
	{ PMAPPROC_UNSET,	"unset" },
	{ PMAPPROC_GETPORT,	"getport" },
	{ PMAPPROC_DUMP,	"dump" },
	{ PMAPPROC_CALLIT,	"call" },
	{ 0,			NULL }
};

/* Forwards */
static char *progstr(u_int32_t);

void
sunrpcrequest_print(const u_char *bp, u_int length, const u_char *bp2)
{
	const struct rpc_msg *rp;
	u_int32_t x;

	rp = (struct rpc_msg *)bp;

	printf("xid 0x%x %d", (u_int32_t)ntohl(rp->rm_xid), length);
	printf(" %s", tok2str(proc2str, " proc #%u",
	    (u_int32_t)ntohl(rp->rm_call.cb_proc)));
	x = ntohl(rp->rm_call.cb_rpcvers);
	if (x != 2)
		printf(" [rpcver %u]", x);

	switch (ntohl(rp->rm_call.cb_proc)) {

	case PMAPPROC_SET:
	case PMAPPROC_UNSET:
	case PMAPPROC_GETPORT:
	case PMAPPROC_CALLIT:
		x = ntohl(rp->rm_call.cb_prog);
		if (!nflag)
			printf(" %s", progstr(x));
		else
			printf(" %u", x);
		printf(".%u", (u_int32_t)ntohl(rp->rm_call.cb_vers));
		break;
	}
}

static char *
progstr(u_int32_t prog)
{
	char progname[32];
	static char buf[32];
	static int lastprog = 0;

	if (lastprog != 0 && prog == lastprog)
		return (buf);
	lastprog = prog;
        if (priv_getrpcbynumber(prog, progname, sizeof(progname)) == 0)
		snprintf(buf, sizeof(buf), "#%u", prog);
	else
		strlcpy(buf, progname, sizeof(buf));
	return (buf);
}
