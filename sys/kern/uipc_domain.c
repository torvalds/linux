/*	$OpenBSD: uipc_domain.c,v 1.70 2025/06/12 20:37:58 deraadt Exp $	*/
/*	$NetBSD: uipc_domain.c,v 1.14 1996/02/09 19:00:44 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)uipc_domain.c	8.2 (Berkeley) 10/18/93
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/timeout.h>

#include "af_frame.h"
#include "bpfilter.h"
#include "pflow.h"

#if NAF_FRAME > 0
extern const struct domain framedomain;
#endif

const struct domain *const domains[] = {
#ifdef MPLS
	&mplsdomain,
#endif
#if defined (IPSEC) || defined (TCP_SIGNATURE)
	&pfkeydomain,
#endif
#ifdef INET6
	&inet6domain,
#endif /* INET6 */
	&inetdomain,
	&unixdomain,
	&routedomain,
#if NAF_FRAME > 0
	&framedomain,
#endif
	NULL
};

void		pffasttimo(void *);
void		pfslowtimo(void *);

void
domaininit(void)
{
	const struct domain *dp;
	const struct protosw *pr;
	static struct timeout pffast_timeout;
	static struct timeout pfslow_timeout;
	int i;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_init)
			(*dp->dom_init)();
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_init)
				(*pr->pr_init)();
	}

	/*
	 * max_linkhdr of 64 was chosen to encompass tunnelling
	 * traffic in IP payloads, eg, by etherip(4) or gif(4),
	 * without needing to prepend an mbuf to fit those
	 * headers.
	 */
	if (max_linkhdr < 64)
		max_linkhdr = 64;

	max_hdr = max_linkhdr + max_protohdr;
	timeout_set_flags(&pffast_timeout, pffasttimo, &pffast_timeout,
	    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);
	timeout_set_flags(&pfslow_timeout, pfslowtimo, &pfslow_timeout,
	    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);
	timeout_add(&pffast_timeout, 1);
	timeout_add(&pfslow_timeout, 1);
}

const struct domain *
pffinddomain(int family)
{
	const struct domain *dp;
	int i;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_family == family)
			return (dp);
	}
	return (NULL);
}

const struct protosw *
pffindtype(int family, int type)
{
	const struct domain *dp;
	const struct protosw *pr;

	dp = pffinddomain(family);
	if (dp == NULL)
		return (NULL);

	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		if (pr->pr_type && pr->pr_type == type)
			return (pr);
	return (NULL);
}

const struct protosw *
pffindproto(int family, int protocol, int type)
{
	const struct domain *dp;
	const struct protosw *pr;
	const struct protosw *maybe = NULL;

	if (family == PF_UNSPEC)
		return (NULL);

	dp = pffinddomain(family);
	if (dp == NULL)
		return (NULL);

	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
		if ((pr->pr_protocol == protocol) && (pr->pr_type == type))
			return (pr);

		if (type == SOCK_RAW && pr->pr_type == SOCK_RAW &&
		    pr->pr_protocol == 0 && maybe == NULL)
			maybe = pr;
	}
	return (maybe);
}

#ifndef SMALL_KERNEL
static int
net_link_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int node;
	int error;

	/*
	 * All sysctl names at this level are nonterminal.
	 */
	if (namelen < 2)
		return (EISDIR);		/* overloaded */
	node = name[0];

	namelen--;
	name++;

	switch (node) {
	case NET_LINK_IFRXQ:
		error = net_ifiq_sysctl(name, namelen, oldp, oldlenp,
		    newp, newlen);
		break;

	default:
		error = ENOPROTOOPT;
		break;
	}

	return (error);
}
#endif /* SMALL_KERNEL */

int
net_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	const struct domain *dp;
	const struct protosw *pr;
	int family, protocol;

	/*
	 * All sysctl names at this level are nonterminal.
	 * Usually: next two components are protocol family and protocol
	 *	number, then at least one addition component.
	 */
	if (namelen < 2)
		return (EISDIR);		/* overloaded */
	family = name[0];

	if (family == PF_UNSPEC)
		return (0);
#ifndef SMALL_KERNEL
	if (family == PF_LINK)
		return (net_link_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
	if (family == PF_UNIX)
		return (uipc_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#if NBPFILTER > 0
	if (family == PF_BPF)
		return (bpf_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif /* NBPFILTER > 0 */
#if NPFLOW > 0
	if (family == PF_PFLOW)
		return (pflow_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif /* NPFLOW > 0 */
#ifdef PIPEX
	if (family == PF_PIPEX)
		return (pipex_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif /* PIPEX */
#ifdef MPLS
	if (family == PF_MPLS)
		return (mpls_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif /* MPLS */
#endif /* SMALL_KERNEL */
	dp = pffinddomain(family);
	if (dp == NULL)
		return (ENOPROTOOPT);

	if (namelen < 3)
		return (EISDIR);		/* overloaded */
	protocol = name[1];
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		if (pr->pr_protocol == protocol && pr->pr_sysctl) {
			size_t savelen;
			int error;

			if ((pr->pr_flags & PR_MPSYSCTL) == 0) {
				savelen = *oldlenp;
				if ((error = sysctl_vslock(oldp, savelen)))
					return (error);
			}
			error = (*pr->pr_sysctl)(name + 2, namelen - 2,
			    oldp, oldlenp, newp, newlen);
			if ((pr->pr_flags & PR_MPSYSCTL) == 0)
				sysctl_vsunlock(oldp, savelen);

			return (error);
		}
	return (ENOPROTOOPT);
}

void
pfctlinput(int cmd, struct sockaddr *sa)
{
	const struct domain *dp;
	const struct protosw *pr;
	int i;

	NET_ASSERT_LOCKED();

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_ctlinput)
				(*pr->pr_ctlinput)(cmd, sa, 0, NULL);
	}
}

void
pfslowtimo(void *arg)
{
	struct timeout *to = (struct timeout *)arg;
	const struct domain *dp;
	const struct protosw *pr;
	int i;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_slowtimo)
				(*pr->pr_slowtimo)();
	}
	timeout_add_msec(to, 500);
}

void
pffasttimo(void *arg)
{
	struct timeout *to = (struct timeout *)arg;
	const struct domain *dp;
	const struct protosw *pr;
	int i;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_fasttimo)
				(*pr->pr_fasttimo)();
	}
	timeout_add_msec(to, 200);
}
