/*	$KAME: ip_encap.c,v 1.41 2001/03/15 08:35:08 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (c) 2018 Andrey V. Elsukov <ae@FreeBSD.org>
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
 * My grandfather said that there's a devil inside tunnelling technology...
 *
 * We have surprisingly many protocols that want packets with IP protocol
 * #4 or #41.  Here's a list of protocols that want protocol #41:
 *	RFC1933 configured tunnel
 *	RFC1933 automatic tunnel
 *	RFC2401 IPsec tunnel
 *	RFC2473 IPv6 generic packet tunnelling
 *	RFC2529 6over4 tunnel
 *	mobile-ip6 (uses RFC2473)
 *	RFC3056 6to4 tunnel
 *	isatap tunnel
 * Here's a list of protocol that want protocol #4:
 *	RFC1853 IPv4-in-IPv4 tunnelling
 *	RFC2003 IPv4 encapsulation within IPv4
 *	RFC2344 reverse tunnelling for mobile-ip4
 *	RFC2401 IPsec tunnel
 * Well, what can I say.  They impose different en/decapsulation mechanism
 * from each other, so they need separate protocol handler.  The only one
 * we can easily determine by protocol # is IPsec, which always has
 * AH/ESP/IPComp header right after outer IP header.
 *
 * So, clearly good old protosw does not work for protocol #4 and #41.
 * The code will let you match protocol via src/dst address pair.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/ip_encap.h>

#ifdef INET6
#include <netinet6/ip6_var.h>
#endif

static MALLOC_DEFINE(M_NETADDR, "encap_export_host",
    "Export host address structure");

struct encaptab {
	CK_LIST_ENTRY(encaptab) chain;
	int		proto;
	int		min_length;
	int		exact_match;
	void		*arg;

	encap_lookup_t	lookup;
	encap_check_t	check;
	encap_input_t	input;
};

struct srcaddrtab {
	CK_LIST_ENTRY(srcaddrtab) chain;

	encap_srcaddr_t	srcaddr;
	void		*arg;
};

CK_LIST_HEAD(encaptab_head, encaptab);
CK_LIST_HEAD(srcaddrtab_head, srcaddrtab);
#ifdef INET
static struct encaptab_head ipv4_encaptab = CK_LIST_HEAD_INITIALIZER();
static struct srcaddrtab_head ipv4_srcaddrtab = CK_LIST_HEAD_INITIALIZER();
#endif
#ifdef INET6
static struct encaptab_head ipv6_encaptab = CK_LIST_HEAD_INITIALIZER();
static struct srcaddrtab_head ipv6_srcaddrtab = CK_LIST_HEAD_INITIALIZER();
#endif

static struct mtx encapmtx, srcaddrmtx;
MTX_SYSINIT(encapmtx, &encapmtx, "encapmtx", MTX_DEF);
MTX_SYSINIT(srcaddrmtx, &srcaddrmtx, "srcaddrmtx", MTX_DEF);
#define	ENCAP_WLOCK()		mtx_lock(&encapmtx)
#define	ENCAP_WUNLOCK()		mtx_unlock(&encapmtx)
#define	ENCAP_RLOCK_TRACKER	struct epoch_tracker encap_et
#define	ENCAP_RLOCK()		\
    epoch_enter_preempt(net_epoch_preempt, &encap_et)
#define	ENCAP_RUNLOCK()		\
    epoch_exit_preempt(net_epoch_preempt, &encap_et)
#define	ENCAP_WAIT()		epoch_wait_preempt(net_epoch_preempt)

#define	SRCADDR_WLOCK()		mtx_lock(&srcaddrmtx)
#define	SRCADDR_WUNLOCK()	mtx_unlock(&srcaddrmtx)
#define	SRCADDR_RLOCK_TRACKER	struct epoch_tracker srcaddr_et
#define	SRCADDR_RLOCK()		\
    epoch_enter_preempt(net_epoch_preempt, &srcaddr_et)
#define	SRCADDR_RUNLOCK()	\
    epoch_exit_preempt(net_epoch_preempt, &srcaddr_et)
#define	SRCADDR_WAIT()		epoch_wait_preempt(net_epoch_preempt)

/*
 * ifaddr_event_ext handler.
 *
 * Tunnelling interfaces may request the kernel to notify when
 * some interface addresses appears or disappears. Usually tunnelling
 * interface must use an address configured on the local machine as
 * ingress address to be able receive datagramms and do not send
 * spoofed packets.
 */
static void
srcaddr_change_event(void *arg __unused, struct ifnet *ifp,
    struct ifaddr *ifa, int event)
{
	SRCADDR_RLOCK_TRACKER;
	struct srcaddrtab_head *head;
	struct srcaddrtab *p;

	/* Support for old ifaddr_event. */
	EVENTHANDLER_INVOKE(ifaddr_event, ifp);

	switch (ifa->ifa_addr->sa_family) {
#ifdef INET
	case AF_INET:
		head = &ipv4_srcaddrtab;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		head = &ipv6_srcaddrtab;
		break;
#endif
	default:
		/* ignore event */
		return;
	}

	SRCADDR_RLOCK();
	CK_LIST_FOREACH(p, head, chain) {
		(*p->srcaddr)(p->arg, ifa->ifa_addr, event);
	}
	SRCADDR_RUNLOCK();
}
EVENTHANDLER_DEFINE(ifaddr_event_ext, srcaddr_change_event, NULL, 0);

static struct srcaddrtab *
encap_register_srcaddr(struct srcaddrtab_head *head, encap_srcaddr_t func,
    void *arg, int mflags)
{
	struct srcaddrtab *p, *tmp;

	if (func == NULL)
		return (NULL);
	p = malloc(sizeof(*p), M_NETADDR, mflags);
	if (p == NULL)
		return (NULL);
	p->srcaddr = func;
	p->arg = arg;

	SRCADDR_WLOCK();
	CK_LIST_FOREACH(tmp, head, chain) {
		if (func == tmp->srcaddr && arg == tmp->arg)
			break;
	}
	if (tmp == NULL)
		CK_LIST_INSERT_HEAD(head, p, chain);
	SRCADDR_WUNLOCK();

	if (tmp != NULL) {
		free(p, M_NETADDR);
		p = tmp;
	}
	return (p);
}

static int
encap_unregister_srcaddr(struct srcaddrtab_head *head,
    const struct srcaddrtab *cookie)
{
	struct srcaddrtab *p;

	SRCADDR_WLOCK();
	CK_LIST_FOREACH(p, head, chain) {
		if (p == cookie) {
			CK_LIST_REMOVE(p, chain);
			SRCADDR_WUNLOCK();
			SRCADDR_WAIT();
			free(p, M_NETADDR);
			return (0);
		}
	}
	SRCADDR_WUNLOCK();
	return (EINVAL);
}

static struct encaptab *
encap_attach(struct encaptab_head *head, const struct encap_config *cfg,
    void *arg, int mflags)
{
	struct encaptab *ep, *tmp;

	if (cfg == NULL || cfg->input == NULL ||
	    (cfg->check == NULL && cfg->lookup == NULL) ||
	    (cfg->lookup != NULL && cfg->exact_match != ENCAP_DRV_LOOKUP) ||
	    (cfg->exact_match == ENCAP_DRV_LOOKUP && cfg->lookup == NULL))
		return (NULL);

	ep = malloc(sizeof(*ep), M_NETADDR, mflags);
	if (ep == NULL)
		return (NULL);

	ep->proto = cfg->proto;
	ep->min_length = cfg->min_length;
	ep->exact_match = cfg->exact_match;
	ep->arg = arg;
	ep->lookup = cfg->exact_match == ENCAP_DRV_LOOKUP ? cfg->lookup: NULL;
	ep->check = cfg->exact_match != ENCAP_DRV_LOOKUP ? cfg->check: NULL;
	ep->input = cfg->input;

	ENCAP_WLOCK();
	CK_LIST_FOREACH(tmp, head, chain) {
		if (tmp->exact_match <= ep->exact_match)
			break;
	}
	if (tmp == NULL)
		CK_LIST_INSERT_HEAD(head, ep, chain);
	else
		CK_LIST_INSERT_BEFORE(tmp, ep, chain);
	ENCAP_WUNLOCK();
	return (ep);
}

static int
encap_detach(struct encaptab_head *head, const struct encaptab *cookie)
{
	struct encaptab *ep;

	ENCAP_WLOCK();
	CK_LIST_FOREACH(ep, head, chain) {
		if (ep == cookie) {
			CK_LIST_REMOVE(ep, chain);
			ENCAP_WUNLOCK();
			ENCAP_WAIT();
			free(ep, M_NETADDR);
			return (0);
		}
	}
	ENCAP_WUNLOCK();
	return (EINVAL);
}

static int
encap_input(struct encaptab_head *head, struct mbuf *m, int off, int proto)
{
	ENCAP_RLOCK_TRACKER;
	struct encaptab *ep, *match;
	void *arg;
	int matchprio, ret;

	match = NULL;
	matchprio = 0;

	ENCAP_RLOCK();
	CK_LIST_FOREACH(ep, head, chain) {
		if (ep->proto >= 0 && ep->proto != proto)
			continue;
		if (ep->min_length > m->m_pkthdr.len)
			continue;
		if (ep->exact_match == ENCAP_DRV_LOOKUP)
			ret = (*ep->lookup)(m, off, proto, &arg);
		else
			ret = (*ep->check)(m, off, proto, ep->arg);
		if (ret <= 0)
			continue;
		if (ret > matchprio) {
			match = ep;
			if (ep->exact_match != ENCAP_DRV_LOOKUP)
				arg = ep->arg;
			/*
			 * No need to continue the search, we got the
			 * exact match.
			 */
			if (ret >= ep->exact_match)
				break;
			matchprio = ret;
		}
	}

	if (match != NULL) {
		/* found a match, "match" has the best one */
		ret = (*match->input)(m, off, proto, arg);
		ENCAP_RUNLOCK();
		MPASS(ret == IPPROTO_DONE);
		return (IPPROTO_DONE);
	}
	ENCAP_RUNLOCK();
	return (0);
}

#ifdef INET
const struct srcaddrtab *
ip_encap_register_srcaddr(encap_srcaddr_t func, void *arg, int mflags)
{

	return (encap_register_srcaddr(&ipv4_srcaddrtab, func, arg, mflags));
}

int
ip_encap_unregister_srcaddr(const struct srcaddrtab *cookie)
{

	return (encap_unregister_srcaddr(&ipv4_srcaddrtab, cookie));
}

const struct encaptab *
ip_encap_attach(const struct encap_config *cfg, void *arg, int mflags)
{

	return (encap_attach(&ipv4_encaptab, cfg, arg, mflags));
}

int
ip_encap_detach(const struct encaptab *cookie)
{

	return (encap_detach(&ipv4_encaptab, cookie));
}

int
encap4_input(struct mbuf **mp, int *offp, int proto)
{

	if (encap_input(&ipv4_encaptab, *mp, *offp, proto) != IPPROTO_DONE)
		return (rip_input(mp, offp, proto));
	return (IPPROTO_DONE);
}
#endif /* INET */

#ifdef INET6
const struct srcaddrtab *
ip6_encap_register_srcaddr(encap_srcaddr_t func, void *arg, int mflags)
{

	return (encap_register_srcaddr(&ipv6_srcaddrtab, func, arg, mflags));
}

int
ip6_encap_unregister_srcaddr(const struct srcaddrtab *cookie)
{

	return (encap_unregister_srcaddr(&ipv6_srcaddrtab, cookie));
}

const struct encaptab *
ip6_encap_attach(const struct encap_config *cfg, void *arg, int mflags)
{

	return (encap_attach(&ipv6_encaptab, cfg, arg, mflags));
}

int
ip6_encap_detach(const struct encaptab *cookie)
{

	return (encap_detach(&ipv6_encaptab, cookie));
}

int
encap6_input(struct mbuf **mp, int *offp, int proto)
{

	if (encap_input(&ipv6_encaptab, *mp, *offp, proto) != IPPROTO_DONE)
		return (rip6_input(mp, offp, proto));
	return (IPPROTO_DONE);
}
#endif /* INET6 */
