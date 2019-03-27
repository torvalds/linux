/*
 * $NetBSD: rpcb_stat.c,v 1.2 2000/07/04 20:27:40 matt Exp $
 * $FreeBSD$
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* #pragma ident   "@(#)rpcb_stat.c 1.7     94/04/25 SMI" */

/*
 * rpcb_stat.c
 * Allows for gathering of statistics
 *
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#include <netconfig.h>
#include <rpc/rpc.h>
#include <rpc/rpcb_prot.h>
#include <sys/stat.h>
#ifdef PORTMAP
#include <rpc/pmap_prot.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "rpcbind.h"

static rpcb_stat_byvers inf;

void
rpcbs_init(void)
{

}

void
rpcbs_procinfo(rpcvers_t rtype, rpcproc_t proc)
{
	switch (rtype + 2) {
#ifdef PORTMAP
	case PMAPVERS:		/* version 2 */
		if (proc > rpcb_highproc_2)
			return;
		break;
#endif
	case RPCBVERS:		/* version 3 */
		if (proc > rpcb_highproc_3)
			return;
		break;
	case RPCBVERS4:		/* version 4 */
		if (proc > rpcb_highproc_4)
			return;
		break;
	default: return;
	}
	inf[rtype].info[proc]++;
	return;
}

void
rpcbs_set(rpcvers_t rtype, bool_t success)
{
	if ((rtype >= RPCBVERS_STAT) || (success == FALSE))
		return;
	inf[rtype].setinfo++;
	return;
}

void
rpcbs_unset(rpcvers_t rtype, bool_t success)
{
	if ((rtype >= RPCBVERS_STAT) || (success == FALSE))
		return;
	inf[rtype].unsetinfo++;
	return;
}

void
rpcbs_getaddr(rpcvers_t rtype, rpcprog_t prog, rpcvers_t vers, char *netid,
	      char *uaddr)
{
	rpcbs_addrlist *al;
	struct netconfig *nconf;

	if (rtype >= RPCBVERS_STAT)
		return;
	for (al = inf[rtype].addrinfo; al; al = al->next) {

		if(al->netid == NULL)
			return;
		if ((al->prog == prog) && (al->vers == vers) &&
		    (strcmp(al->netid, netid) == 0)) {
			if ((uaddr == NULL) || (uaddr[0] == 0))
				al->failure++;
			else
				al->success++;
			return;
		}
	}
	nconf = rpcbind_get_conf(netid);
	if (nconf == NULL) {
		return;
	}
	al = (rpcbs_addrlist *) malloc(sizeof (rpcbs_addrlist));
	if (al == NULL) {
		return;
	}
	al->prog = prog;
	al->vers = vers;
	al->netid = nconf->nc_netid;
	if ((uaddr == NULL) || (uaddr[0] == 0)) {
		al->failure = 1;
		al->success = 0;
	} else {
		al->failure = 0;
		al->success = 1;
	}
	al->next = inf[rtype].addrinfo;
	inf[rtype].addrinfo = al;
}

void
rpcbs_rmtcall(rpcvers_t rtype, rpcproc_t rpcbproc, rpcprog_t prog,
	      rpcvers_t vers, rpcproc_t proc, char *netid, rpcblist_ptr rbl)
{
	rpcbs_rmtcalllist *rl;
	struct netconfig *nconf;

	if (rtype >= RPCBVERS_STAT)
		return;
	for (rl = inf[rtype].rmtinfo; rl; rl = rl->next) {

		if(rl->netid == NULL)
			return;

		if ((rl->prog == prog) && (rl->vers == vers) &&
		    (rl->proc == proc) &&
		    (strcmp(rl->netid, netid) == 0)) {
			if ((rbl == NULL) ||
			    (rbl->rpcb_map.r_vers != vers))
				rl->failure++;
			else
				rl->success++;
			if (rpcbproc == RPCBPROC_INDIRECT)
				rl->indirect++;
			return;
		}
	}
	nconf = rpcbind_get_conf(netid);
	if (nconf == NULL) {
		return;
	}
	rl = (rpcbs_rmtcalllist *) malloc(sizeof (rpcbs_rmtcalllist));
	if (rl == NULL) {
		return;
	}
	rl->prog = prog;
	rl->vers = vers;
	rl->proc = proc;
	rl->netid = nconf->nc_netid;
	if ((rbl == NULL) ||
		    (rbl->rpcb_map.r_vers != vers)) {
		rl->failure = 1;
		rl->success = 0;
	} else {
		rl->failure = 0;
		rl->success = 1;
	}
	rl->indirect = 1;
	rl->next = inf[rtype].rmtinfo;
	inf[rtype].rmtinfo = rl;
	return;
}

void *
rpcbproc_getstat(void *arg __unused, struct svc_req *req __unused,
    SVCXPRT *xprt __unused, rpcvers_t versnum __unused)
{
	return (void *)&inf;
}
