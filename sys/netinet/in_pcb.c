/*	$OpenBSD: in_pcb.c,v 1.320 2025/07/14 21:53:46 bluhm Exp $	*/
/*	$NetBSD: in_pcb.c,v 1.25 1996/02/13 23:41:53 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1991, 1993
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
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/pool.h>
#include <sys/proc.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/pfvar.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/in_pcb.h>
#ifdef IPSEC
#include <netinet/ip_esp.h>
#endif /* IPSEC */

#include "stoeplitz.h"
#if NSTOEPLITZ > 0
#include <net/toeplitz.h>
#endif

/*
 * Locks used to protect data:
 *	a	atomic
 */

const struct in_addr zeroin_addr;
const union inpaddru zeroin46_addr;

/*
 * These configure the range of local port addresses assigned to
 * "unspecified" outgoing connections/packets/whatever.
 */
int ipport_firstauto = IPPORT_RESERVED;		/* [a] */
int ipport_lastauto = IPPORT_USERRESERVED;	/* [a] */
int ipport_hifirstauto = IPPORT_HIFIRSTAUTO;	/* [a] */
int ipport_hilastauto = IPPORT_HILASTAUTO;	/* [a] */

struct baddynamicports baddynamicports;
struct baddynamicports rootonlyports;
struct pool inpcb_pool;

void	in_pcbhash_insert(struct inpcb *);
struct inpcb *in_pcbhash_lookup(struct inpcbtable *, uint64_t, u_int,
    const struct in_addr *, u_short, const struct in_addr *, u_short);
int	in_pcbresize(struct inpcbtable *, int);

#define	INPCBHASH_LOADFACTOR(_x)	(((_x) * 3) / 4)

uint64_t in_pcbhash(struct inpcbtable *, u_int,
    const struct in_addr *, u_short, const struct in_addr *, u_short);
uint64_t in_pcblhash(struct inpcbtable *, u_int, u_short);

struct inpcb *in_pcblookup_lock(struct inpcbtable *, struct in_addr, u_int,
    struct in_addr, u_int, u_int, int);
int	in_pcbaddrisavail_lock(const struct inpcb *, struct sockaddr_in *, int,
    struct proc *, int);
int	in_pcbpickport(u_int16_t *, const void *, int, const struct inpcb *,
    struct proc *);

/*
 * in_pcb is used for inet and inet6.  in6_pcb only contains special
 * IPv6 cases.  So the internet initializer is used for both domains.
 */
void
in_init(void)
{
	pool_init(&inpcb_pool, sizeof(struct inpcb), 0,
	    IPL_SOFTNET, 0, "inpcb", NULL);
}

uint64_t
in_pcbhash(struct inpcbtable *table, u_int rdomain,
    const struct in_addr *faddr, u_short fport,
    const struct in_addr *laddr, u_short lport)
{
	SIPHASH_CTX ctx;
	u_int32_t nrdom = htonl(rdomain);

	SipHash24_Init(&ctx, &table->inpt_key);
	SipHash24_Update(&ctx, &nrdom, sizeof(nrdom));
	SipHash24_Update(&ctx, faddr, sizeof(*faddr));
	SipHash24_Update(&ctx, &fport, sizeof(fport));
	SipHash24_Update(&ctx, laddr, sizeof(*laddr));
	SipHash24_Update(&ctx, &lport, sizeof(lport));
	return SipHash24_End(&ctx);
}

uint64_t
in_pcblhash(struct inpcbtable *table, u_int rdomain, u_short lport)
{
	SIPHASH_CTX ctx;
	u_int32_t nrdom = htonl(rdomain);

	SipHash24_Init(&ctx, &table->inpt_lkey);
	SipHash24_Update(&ctx, &nrdom, sizeof(nrdom));
	SipHash24_Update(&ctx, &lport, sizeof(lport));
	return SipHash24_End(&ctx);
}

void
in_pcbinit(struct inpcbtable *table, int hashsize)
{
	mtx_init(&table->inpt_mtx, IPL_SOFTNET);
	TAILQ_INIT(&table->inpt_queue);
	table->inpt_hashtbl = hashinit(hashsize, M_PCB, M_WAITOK,
	    &table->inpt_mask);
	table->inpt_lhashtbl = hashinit(hashsize, M_PCB, M_WAITOK,
	    &table->inpt_lmask);
	table->inpt_count = 0;
	table->inpt_size = hashsize;
	arc4random_buf(&table->inpt_key, sizeof(table->inpt_key));
	arc4random_buf(&table->inpt_lkey, sizeof(table->inpt_lkey));
}

/*
 * Check if the specified port is invalid for dynamic allocation.
 */
int
in_baddynamic(u_int16_t port, u_int16_t proto)
{
	switch (proto) {
	case IPPROTO_TCP:
		return (DP_ISSET(baddynamicports.tcp, port));
	case IPPROTO_UDP:
#ifdef IPSEC
		/* Cannot preset this as it is a sysctl */
		if (port == atomic_load_int(&udpencap_port))
			return (1);
#endif
		return (DP_ISSET(baddynamicports.udp, port));
	default:
		return (0);
	}
}

int
in_rootonly(u_int16_t port, u_int16_t proto)
{
	switch (proto) {
	case IPPROTO_TCP:
		return (port < IPPORT_RESERVED ||
		    DP_ISSET(rootonlyports.tcp, port));
	case IPPROTO_UDP:
		return (port < IPPORT_RESERVED ||
		    DP_ISSET(rootonlyports.udp, port));
	default:
		return (0);
	}
}

int
in_pcballoc(struct socket *so, struct inpcbtable *table, int wait)
{
	struct inpcb *inp;

	inp = pool_get(&inpcb_pool, (wait == M_WAIT ? PR_WAITOK : PR_NOWAIT) |
	    PR_ZERO);
	if (inp == NULL)
		return (ENOBUFS);
	inp->inp_table = table;
	inp->inp_socket = soref(so);
	refcnt_init_trace(&inp->inp_refcnt, DT_REFCNT_IDX_INPCB);
	inp->inp_seclevel.sl_auth = IPSEC_AUTH_LEVEL_DEFAULT;
	inp->inp_seclevel.sl_esp_trans = IPSEC_ESP_TRANS_LEVEL_DEFAULT;
	inp->inp_seclevel.sl_esp_network = IPSEC_ESP_NETWORK_LEVEL_DEFAULT;
	inp->inp_seclevel.sl_ipcomp = IPSEC_IPCOMP_LEVEL_DEFAULT;
	inp->inp_rtableid = curproc->p_p->ps_rtableid;
	inp->inp_hops = -1;
#ifdef INET6
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET6:
		inp->inp_flags = INP_IPV6;
		break;
	case PF_INET:
		/* inp->inp_flags is initialized to 0 */
		break;
	default:
		unhandled_af(so->so_proto->pr_domain->dom_family);
	}
	inp->inp_cksum6 = -1;
#endif /* INET6 */

	mtx_enter(&table->inpt_mtx);
	if (table->inpt_count++ > INPCBHASH_LOADFACTOR(table->inpt_size))
		(void)in_pcbresize(table, table->inpt_size * 2);
	TAILQ_INSERT_HEAD(&table->inpt_queue, inp, inp_queue);
	in_pcbhash_insert(inp);
	mtx_leave(&table->inpt_mtx);

	so->so_pcb = inp;

	return (0);
}

int
in_pcbbind_locked(struct inpcb *inp, struct mbuf *nam, const void *laddr,
    struct proc *p)
{
	struct socket *so = inp->inp_socket;
	u_int16_t lport = 0;
	int wild = 0;
	int error;

	if (inp->inp_lport)
		return (EINVAL);

	if ((so->so_options & (SO_REUSEADDR|SO_REUSEPORT)) == 0 &&
	    ((so->so_proto->pr_flags & PR_CONNREQUIRED) == 0 ||
	     (so->so_options & SO_ACCEPTCONN) == 0))
		wild = INPLOOKUP_WILDCARD;

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		if (!IN6_IS_ADDR_UNSPECIFIED(&inp->inp_laddr6))
			return (EINVAL);
		wild |= INPLOOKUP_IPV6;

		if (nam) {
			struct sockaddr_in6 *sin6;

			if ((error = in6_nam2sin6(nam, &sin6)))
				return (error);
			if ((error = in6_pcbaddrisavail_lock(inp, sin6, wild,
			    p, IN_PCBLOCK_HOLD)))
				return (error);
			laddr = &sin6->sin6_addr;
			lport = sin6->sin6_port;
		}
	} else
#endif
	{
		if (inp->inp_laddr.s_addr != INADDR_ANY)
			return (EINVAL);

		if (nam) {
			struct sockaddr_in *sin;

			if ((error = in_nam2sin(nam, &sin)))
				return (error);
			if ((error = in_pcbaddrisavail_lock(inp, sin, wild,
			    p, IN_PCBLOCK_HOLD)))
				return (error);
			laddr = &sin->sin_addr;
			lport = sin->sin_port;
		}
	}

	if (lport == 0) {
		if ((error = in_pcbpickport(&lport, laddr, wild, inp, p)))
			return (error);
	} else {
		if (in_rootonly(ntohs(lport), so->so_proto->pr_protocol) &&
		    suser(p) != 0)
			return (EACCES);
	}
	if (nam) {
#ifdef INET6
		if (ISSET(inp->inp_flags, INP_IPV6))
			inp->inp_laddr6 = *(struct in6_addr *)laddr;
		else
#endif
			inp->inp_laddr = *(struct in_addr *)laddr;
	}
	inp->inp_lport = lport;
	in_pcbrehash(inp);

	return (0);
}

int
in_pcbbind(struct inpcb *inp, struct mbuf *nam, struct proc *p)
{
	struct inpcbtable *table = inp->inp_table;
	int error;

	/* keep lookup, modification, and rehash in sync */
	mtx_enter(&table->inpt_mtx);
	error = in_pcbbind_locked(inp, nam, &zeroin46_addr, p);
	mtx_leave(&table->inpt_mtx);

	return error;
}

int
in_pcbaddrisavail_lock(const struct inpcb *inp, struct sockaddr_in *sin,
    int wild, struct proc *p, int lock)
{
	struct socket *so = inp->inp_socket;
	struct inpcbtable *table = inp->inp_table;
	u_int16_t lport = sin->sin_port;
	int reuseport = (so->so_options & SO_REUSEPORT);

	if (IN_MULTICAST(sin->sin_addr.s_addr)) {
		/*
		 * Treat SO_REUSEADDR as SO_REUSEPORT for multicast;
		 * allow complete duplication of binding if
		 * SO_REUSEPORT is set, or if SO_REUSEADDR is set
		 * and a multicast address is bound on both
		 * new and duplicated sockets.
		 */
		if (so->so_options & (SO_REUSEADDR|SO_REUSEPORT))
			reuseport = SO_REUSEADDR|SO_REUSEPORT;
	} else if (sin->sin_addr.s_addr != INADDR_ANY) {
		/*
		 * we must check that we are binding to an address we
		 * own except when:
		 * - SO_BINDANY is set or
		 * - we are binding a UDP socket to 255.255.255.255 or
		 * - we are binding a UDP socket to one of our broadcast
		 *   addresses
		 */
		if (!ISSET(so->so_options, SO_BINDANY) &&
		    !(so->so_type == SOCK_DGRAM &&
		    sin->sin_addr.s_addr == INADDR_BROADCAST) &&
		    !(so->so_type == SOCK_DGRAM &&
		    in_broadcast(sin->sin_addr, inp->inp_rtableid))) {
			struct ifaddr *ia;

			sin->sin_port = 0;
			memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
			ia = ifa_ifwithaddr(sintosa(sin), inp->inp_rtableid);
			sin->sin_port = lport;

			if (ia == NULL)
				return (EADDRNOTAVAIL);
		}
	}
	if (lport) {
		struct inpcb *t;
		int error = 0;

		if (so->so_euid && !IN_MULTICAST(sin->sin_addr.s_addr)) {
			t = in_pcblookup_local_lock(table, &sin->sin_addr,
			    lport, INPLOOKUP_WILDCARD, inp->inp_rtableid, lock);
			if (t && (so->so_euid != t->inp_socket->so_euid))
				error = EADDRINUSE;
			if (lock == IN_PCBLOCK_GRAB)
				in_pcbunref(t);
			if (error)
				return (error);
		}
		t = in_pcblookup_local_lock(table, &sin->sin_addr, lport,
		    wild, inp->inp_rtableid, lock);
		if (t && (reuseport & t->inp_socket->so_options) == 0)
			error = EADDRINUSE;
		if (lock == IN_PCBLOCK_GRAB)
			in_pcbunref(t);
		if (error)
			return (error);
	}

	return (0);
}

int
in_pcbaddrisavail(const struct inpcb *inp, struct sockaddr_in *sin,
    int wild, struct proc *p)
{
	return in_pcbaddrisavail_lock(inp, sin, wild, p, IN_PCBLOCK_GRAB);
}

int
in_pcbpickport(u_int16_t *lport, const void *laddr, int wild,
    const struct inpcb *inp, struct proc *p)
{
	struct socket *so = inp->inp_socket;
	struct inpcbtable *table = inp->inp_table;
	struct inpcb *t;
	u_int16_t first, last, lower, higher, candidate, localport;
	int count;

	MUTEX_ASSERT_LOCKED(&table->inpt_mtx);

	if (inp->inp_flags & INP_HIGHPORT) {
		first = atomic_load_int(&ipport_hifirstauto);	/* sysctl */
		last = atomic_load_int(&ipport_hilastauto);
	} else if (inp->inp_flags & INP_LOWPORT) {
		if (suser(p))
			return (EACCES);
		first = IPPORT_RESERVED-1; /* 1023 */
		last = 600;		   /* not IPPORT_RESERVED/2 */
	} else {
		first = atomic_load_int(&ipport_firstauto);	/* sysctl */
		last = atomic_load_int(&ipport_lastauto);
	}
	if (first < last) {
		lower = first;
		higher = last;
	} else {
		lower = last;
		higher = first;
	}

	/*
	 * Simple check to ensure all ports are not used up causing
	 * a deadlock here.
	 */

	count = higher - lower;
	candidate = lower + arc4random_uniform(count);

	do {
		do {
			if (count-- < 0)	/* completely used? */
				return (EADDRNOTAVAIL);
			++candidate;
			if (candidate < lower || candidate > higher)
				candidate = lower;
			localport = htons(candidate);
		} while (in_baddynamic(candidate, so->so_proto->pr_protocol));
		t = in_pcblookup_local_lock(table, laddr, localport, wild,
		    inp->inp_rtableid, IN_PCBLOCK_HOLD);
	} while (t != NULL);
	*lport = localport;

	return (0);
}

/*
 * Connect from a socket to a specified address.
 * Both address and port must be specified in argument sin.
 * If don't have a local address for this socket yet,
 * then pick one.
 */
int
in_pcbconnect(struct inpcb *inp, struct mbuf *nam)
{
	struct inpcbtable *table = inp->inp_table;
	struct in_addr ina;
	struct sockaddr_in *sin;
	struct inpcb *t;
	int error;

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6))
		return (in6_pcbconnect(inp, nam));
#endif

	if ((error = in_nam2sin(nam, &sin)))
		return (error);
	if (sin->sin_port == 0)
		return (EADDRNOTAVAIL);
	error = in_pcbselsrc(&ina, sin, inp);
	if (error)
		return (error);

	/* keep lookup, modification, and rehash in sync */
	mtx_enter(&table->inpt_mtx);

	t = in_pcblookup_lock(inp->inp_table, sin->sin_addr, sin->sin_port,
	    ina, inp->inp_lport, inp->inp_rtableid, IN_PCBLOCK_HOLD);
	if (t != NULL) {
		mtx_leave(&table->inpt_mtx);
		return (EADDRINUSE);
	}

	KASSERT(inp->inp_laddr.s_addr == INADDR_ANY || inp->inp_lport);

	if (inp->inp_laddr.s_addr == INADDR_ANY) {
		if (inp->inp_lport == 0) {
			error = in_pcbbind_locked(inp, NULL, &ina, curproc);
			if (error) {
				mtx_leave(&table->inpt_mtx);
				return (error);
			}
			t = in_pcblookup_lock(inp->inp_table, sin->sin_addr,
			    sin->sin_port, ina, inp->inp_lport,
			    inp->inp_rtableid, IN_PCBLOCK_HOLD);
			if (t != NULL) {
				inp->inp_lport = 0;
				mtx_leave(&table->inpt_mtx);
				return (EADDRINUSE);
			}
		}
		inp->inp_laddr = ina;
	}
	inp->inp_faddr = sin->sin_addr;
	inp->inp_fport = sin->sin_port;
	in_pcbrehash(inp);

	mtx_leave(&table->inpt_mtx);

#if NSTOEPLITZ > 0
	inp->inp_flowid = stoeplitz_ip4port(inp->inp_faddr.s_addr,
	    inp->inp_laddr.s_addr, inp->inp_fport, inp->inp_lport);
#endif
	return (0);
}

void
in_pcbdisconnect(struct inpcb *inp)
{
#if NPF > 0
	pf_remove_divert_state(inp);
	pf_inp_unlink(inp);
#endif
	inp->inp_flowid = 0;
	if (inp->inp_socket->so_state & SS_NOFDREF)
		in_pcbdetach(inp);
}

void
in_pcbdetach(struct inpcb *inp)
{
	struct socket *so = inp->inp_socket;
	struct inpcbtable *table = inp->inp_table;

	soassertlocked(so);

	so->so_pcb = NULL;
	sofree(so, 1);
	if (inp->inp_route.ro_rt) {
		rtfree(inp->inp_route.ro_rt);
		inp->inp_route.ro_rt = NULL;
	}
#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		ip6_freepcbopts(inp->inp_outputopts6);
		ip6_freemoptions(inp->inp_moptions6);
	} else
#endif
	{
		m_freem(inp->inp_options);
		ip_freemoptions(inp->inp_moptions);
	}
#if NPF > 0
	pf_remove_divert_state(inp);
	pf_inp_unlink(inp);
#endif
	mtx_enter(&table->inpt_mtx);
	LIST_REMOVE(inp, inp_lhash);
	LIST_REMOVE(inp, inp_hash);
	TAILQ_REMOVE(&table->inpt_queue, inp, inp_queue);
	table->inpt_count--;
	mtx_leave(&table->inpt_mtx);

	in_pcbunref(inp);
}

struct socket *
in_pcbsolock(struct inpcb *inp)
{
	struct socket *so = inp->inp_socket;

	NET_ASSERT_LOCKED();

	if (so == NULL)
		return NULL;
	rw_enter_write(&so->so_lock);
	if (so->so_pcb == NULL) {
		rw_exit_write(&so->so_lock);
		return NULL;
	}
	KASSERT(inp->inp_socket == so && sotoinpcb(so) == inp);
	return so;
}

void
in_pcbsounlock(struct inpcb *inp, struct socket *so)
{
	if (so == NULL)
		return;
	if (inp != NULL && so->so_pcb != NULL)
		KASSERT(inp->inp_socket == so && sotoinpcb(so) == inp);
	rw_exit_write(&so->so_lock);
}

struct inpcb *
in_pcbref(struct inpcb *inp)
{
	if (inp == NULL)
		return NULL;
	refcnt_take(&inp->inp_refcnt);
	return inp;
}

void
in_pcbunref(struct inpcb *inp)
{
	if (inp == NULL)
		return;
	if (refcnt_rele(&inp->inp_refcnt) == 0)
		return;
	sorele(inp->inp_socket);
	KASSERT((LIST_NEXT(inp, inp_hash) == NULL) ||
	    (LIST_NEXT(inp, inp_hash) == _Q_INVALID));
	KASSERT((LIST_NEXT(inp, inp_lhash) == NULL) ||
	    (LIST_NEXT(inp, inp_lhash) == _Q_INVALID));
	KASSERT((TAILQ_NEXT(inp, inp_queue) == NULL) ||
	    (TAILQ_NEXT(inp, inp_queue) == _Q_INVALID));
	pool_put(&inpcb_pool, inp);
}

struct inpcb *
in_pcb_iterator(struct inpcbtable *table, struct inpcb *inp,
    struct inpcb_iterator *iter)
{
	struct inpcb *tmp;

	MUTEX_ASSERT_LOCKED(&table->inpt_mtx);

	if (inp)
		tmp = TAILQ_NEXT((struct inpcb *)iter, inp_queue);
	else
		tmp = TAILQ_FIRST(&table->inpt_queue);

	while (tmp && tmp->inp_table == NULL)
		tmp = TAILQ_NEXT(tmp, inp_queue);

	if (inp) {
		TAILQ_REMOVE(&table->inpt_queue, (struct inpcb *)iter,
		    inp_queue);
		in_pcbunref(inp);
	}
	if (tmp) {
		TAILQ_INSERT_AFTER(&table->inpt_queue, tmp,
		    (struct inpcb *)iter, inp_queue);
		in_pcbref(tmp);
	}

	return tmp;
}

void
in_pcb_iterator_abort(struct inpcbtable *table, struct inpcb *inp,
    struct inpcb_iterator *iter)
{
	MUTEX_ASSERT_LOCKED(&table->inpt_mtx);

	if (inp) {
		TAILQ_REMOVE(&table->inpt_queue, (struct inpcb *)iter,
		    inp_queue);
		in_pcbunref(inp);
	}
}

void
in_setsockaddr(struct inpcb *inp, struct mbuf *nam)
{
	struct sockaddr_in *sin;

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		in6_setsockaddr(inp, nam);
		return;
	}
#endif

	nam->m_len = sizeof(*sin);
	sin = mtod(nam, struct sockaddr_in *);
	memset(sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_port = inp->inp_lport;
	sin->sin_addr = inp->inp_laddr;
}

void
in_setpeeraddr(struct inpcb *inp, struct mbuf *nam)
{
	struct sockaddr_in *sin;

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		in6_setpeeraddr(inp, nam);
		return;
	}
#endif

	nam->m_len = sizeof(*sin);
	sin = mtod(nam, struct sockaddr_in *);
	memset(sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_port = inp->inp_fport;
	sin->sin_addr = inp->inp_faddr;
}

int
in_sockaddr(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	in_setsockaddr(inp, nam);

	return (0);
}

int
in_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	in_setpeeraddr(inp, nam);

	return (0);
}

/*
 * Pass some notification to all connections of a protocol
 * associated with address dst.  The "usual action" will be
 * taken, depending on the ctlinput cmd.  The caller must filter any
 * cmds that are uninteresting (e.g., no error in the map).
 * Call the protocol specific routine (if any) to report
 * any errors for each matching socket.
 */
void
in_pcbnotifyall(struct inpcbtable *table, const struct sockaddr_in *dst,
    u_int rtable, int errno, void (*notify)(struct inpcb *, int))
{
	struct inpcb_iterator iter = { .inp_table = NULL };
	struct inpcb *inp = NULL;
	u_int rdomain;

	if (dst->sin_addr.s_addr == INADDR_ANY)
		return;
	if (notify == NULL)
		return;

	rdomain = rtable_l2(rtable);
	mtx_enter(&table->inpt_mtx);
	while ((inp = in_pcb_iterator(table, inp, &iter)) != NULL) {
		struct socket *so;

		KASSERT(!ISSET(inp->inp_flags, INP_IPV6));

		if (inp->inp_faddr.s_addr != dst->sin_addr.s_addr ||
		    rtable_l2(inp->inp_rtableid) != rdomain) {
			continue;
		}
		mtx_leave(&table->inpt_mtx);
		so = in_pcbsolock(inp);
		if (so != NULL)
			(*notify)(inp, errno);
		in_pcbsounlock(inp, so);
		mtx_enter(&table->inpt_mtx);
	}
	mtx_leave(&table->inpt_mtx);
}

/*
 * Check for alternatives when higher level complains
 * about service problems.  For now, invalidate cached
 * routing information.  If the route was created dynamically
 * (by a redirect), time to try a default gateway again.
 */
void
in_losing(struct inpcb *inp)
{
	struct rtentry *rt = inp->inp_route.ro_rt;

	if (rt) {
		inp->inp_route.ro_rt = NULL;

		if (rt->rt_flags & RTF_DYNAMIC) {
			struct ifnet *ifp;

			ifp = if_get(rt->rt_ifidx);
			/*
			 * If the interface is gone, all its attached
			 * route entries have been removed from the table,
			 * so we're dealing with a stale cache and have
			 * nothing to do.
			 */
			if (ifp != NULL)
				rtdeletemsg(rt, ifp, inp->inp_rtableid);
			if_put(ifp);
		}
		/*
		 * A new route can be allocated
		 * the next time output is attempted.
		 * rtfree() needs to be called in anycase because the inp
		 * is still holding a reference to rt.
		 */
		rtfree(rt);
	}
}

/*
 * After a routing change, flush old routing
 * and allocate a (hopefully) better one.
 */
void
in_pcbrtchange(struct inpcb *inp, int errno)
{
	soassertlocked(inp->inp_socket);

	if (inp->inp_route.ro_rt) {
		rtfree(inp->inp_route.ro_rt);
		inp->inp_route.ro_rt = NULL;
		/*
		 * A new route can be allocated the next time
		 * output is attempted.
		 */
	}
}

struct inpcb *
in_pcblookup_local_lock(struct inpcbtable *table, const void *laddrp,
    u_int lport_arg, int flags, u_int rtable, int lock)
{
	struct inpcb *inp, *match = NULL;
	int matchwild = 3, wildcard;
	u_int16_t lport = lport_arg;
	const struct in_addr laddr = *(const struct in_addr *)laddrp;
#ifdef INET6
	const struct in6_addr *laddr6 = (const struct in6_addr *)laddrp;
#endif
	struct inpcbhead *head;
	uint64_t lhash;
	u_int rdomain;

	rdomain = rtable_l2(rtable);
	lhash = in_pcblhash(table, rdomain, lport);

	if (lock == IN_PCBLOCK_GRAB) {
		mtx_enter(&table->inpt_mtx);
	} else {
		KASSERT(lock == IN_PCBLOCK_HOLD);
		MUTEX_ASSERT_LOCKED(&table->inpt_mtx);
	}
	head = &table->inpt_lhashtbl[lhash & table->inpt_lmask];
	LIST_FOREACH(inp, head, inp_lhash) {
		if (rtable_l2(inp->inp_rtableid) != rdomain)
			continue;
		if (inp->inp_lport != lport)
			continue;
		wildcard = 0;
#ifdef INET6
		if (ISSET(flags, INPLOOKUP_IPV6)) {
			KASSERT(ISSET(inp->inp_flags, INP_IPV6));

			if (!IN6_IS_ADDR_UNSPECIFIED(&inp->inp_faddr6))
				wildcard++;

			if (!IN6_ARE_ADDR_EQUAL(&inp->inp_laddr6, laddr6)) {
				if (IN6_IS_ADDR_UNSPECIFIED(&inp->inp_laddr6) ||
				    IN6_IS_ADDR_UNSPECIFIED(laddr6))
					wildcard++;
				else
					continue;
			}

		} else
#endif /* INET6 */
		{
			KASSERT(!ISSET(inp->inp_flags, INP_IPV6));

			if (inp->inp_faddr.s_addr != INADDR_ANY)
				wildcard++;

			if (inp->inp_laddr.s_addr != laddr.s_addr) {
				if (inp->inp_laddr.s_addr == INADDR_ANY ||
				    laddr.s_addr == INADDR_ANY)
					wildcard++;
				else
					continue;
			}

		}
		if ((!wildcard || (flags & INPLOOKUP_WILDCARD)) &&
		    wildcard < matchwild) {
			match = inp;
			if ((matchwild = wildcard) == 0)
				break;
		}
	}
	if (lock == IN_PCBLOCK_GRAB) {
		in_pcbref(match);
		mtx_leave(&table->inpt_mtx);
	}

	return (match);
}

struct rtentry *
in_pcbrtentry(struct inpcb *inp)
{
	soassertlocked(inp->inp_socket);

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6))
		return in6_pcbrtentry(inp);
#endif

	if (inp->inp_faddr.s_addr == INADDR_ANY)
		return (NULL);
	return (route_mpath(&inp->inp_route, &inp->inp_faddr, &inp->inp_laddr,
	    inp->inp_rtableid));
}

/*
 * Return an IPv4 address, which is the most appropriate for a given
 * destination.
 * If necessary, this function lookups the routing table and returns
 * an entry to the caller for later use.
 */
int
in_pcbselsrc(struct in_addr *insrc, const struct sockaddr_in *dstsock,
    struct inpcb *inp)
{
	const struct in_addr *dst = &dstsock->sin_addr;
	const struct in_addr *laddr = &inp->inp_laddr;
	struct rtentry *rt;
	struct ip_moptions *mopts = inp->inp_moptions;
	u_int rtableid = inp->inp_rtableid;
	struct sockaddr	*ip4_source = NULL;
	struct in_ifaddr *ia = NULL;

	/*
	 * If the socket(if any) is already bound, use that bound address
	 * unless it is INADDR_ANY or INADDR_BROADCAST.
	 */
	if (laddr->s_addr != INADDR_ANY &&
	    laddr->s_addr != INADDR_BROADCAST) {
		*insrc = *laddr;
		return (0);
	}

	/*
	 * If the destination address is multicast or limited
	 * broadcast (255.255.255.255) and an outgoing interface has
	 * been set as a multicast option, use the address of that
	 * interface as our source address.
	 */
	if ((IN_MULTICAST(dst->s_addr) || dst->s_addr == INADDR_BROADCAST) &&
	    mopts != NULL) {
		struct ifnet *ifp;

		ifp = if_get(mopts->imo_ifidx);
		if (ifp != NULL) {
			if (ifp->if_rdomain == rtable_l2(rtableid))
				IFP_TO_IA(ifp, ia);
			if (ia == NULL) {
				if_put(ifp);
				return (EADDRNOTAVAIL);
			}

			*insrc = ia->ia_addr.sin_addr;
			if_put(ifp);
			return (0);
		}
	}

	/*
	 * If route is known or can be allocated now,
	 * our src addr is taken from the i/f, else punt.
	 */
	rt = route_mpath(&inp->inp_route, dst, NULL, rtableid);

	/*
	 * If we found a route, use the address
	 * corresponding to the outgoing interface.
	 */
	if (rt != NULL)
		ia = ifatoia(rt->rt_ifa);

	/*
	 * Use preferred source address if :
	 * - destination is not onlink
	 * - preferred source address is set
	 * - output interface is UP
	 */
	if (rt != NULL && ISSET(rt->rt_flags, RTF_GATEWAY)) {
		ip4_source = rtable_getsource(rtableid, AF_INET);
		if (ip4_source != NULL) {
			struct ifaddr *ifa;
			if ((ifa = ifa_ifwithaddr(ip4_source, rtableid)) !=
			    NULL && ISSET(ifa->ifa_ifp->if_flags, IFF_UP)) {
				*insrc = satosin(ip4_source)->sin_addr;
				return (0);
			}
		}
	}

	if (ia == NULL)
		return (EADDRNOTAVAIL);

	*insrc = ia->ia_addr.sin_addr;
	return (0);
}

void
in_pcbrehash(struct inpcb *inp)
{
	LIST_REMOVE(inp, inp_lhash);
	LIST_REMOVE(inp, inp_hash);
	in_pcbhash_insert(inp);
}

void
in_pcbhash_insert(struct inpcb *inp)
{
	struct inpcbtable *table = inp->inp_table;
	struct inpcbhead *head;
	uint64_t hash, lhash;

	MUTEX_ASSERT_LOCKED(&table->inpt_mtx);

	lhash = in_pcblhash(table, inp->inp_rtableid, inp->inp_lport);
	head = &table->inpt_lhashtbl[lhash & table->inpt_lmask];
	LIST_INSERT_HEAD(head, inp, inp_lhash);
#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6))
		hash = in6_pcbhash(table, rtable_l2(inp->inp_rtableid),
		    &inp->inp_faddr6, inp->inp_fport,
		    &inp->inp_laddr6, inp->inp_lport);
	else
#endif
		hash = in_pcbhash(table, rtable_l2(inp->inp_rtableid),
		    &inp->inp_faddr, inp->inp_fport,
		    &inp->inp_laddr, inp->inp_lport);
	head = &table->inpt_hashtbl[hash & table->inpt_mask];
	LIST_INSERT_HEAD(head, inp, inp_hash);
}

struct inpcb *
in_pcbhash_lookup(struct inpcbtable *table, uint64_t hash, u_int rdomain,
    const struct in_addr *faddr, u_short fport,
    const struct in_addr *laddr, u_short lport)
{
	struct inpcbhead *head;
	struct inpcb *inp;

	MUTEX_ASSERT_LOCKED(&table->inpt_mtx);

	head = &table->inpt_hashtbl[hash & table->inpt_mask];
	LIST_FOREACH(inp, head, inp_hash) {
		KASSERT(!ISSET(inp->inp_flags, INP_IPV6));

		if (inp->inp_fport == fport && inp->inp_lport == lport &&
		    inp->inp_faddr.s_addr == faddr->s_addr &&
		    inp->inp_laddr.s_addr == laddr->s_addr &&
		    rtable_l2(inp->inp_rtableid) == rdomain) {
			break;
		}
	}
	if (inp != NULL) {
		/*
		 * Move this PCB to the head of hash chain so that
		 * repeated accesses are quicker.  This is analogous to
		 * the historic single-entry PCB cache.
		 */
		if (inp != LIST_FIRST(head)) {
			LIST_REMOVE(inp, inp_hash);
			LIST_INSERT_HEAD(head, inp, inp_hash);
		}
	}
	return (inp);
}

int
in_pcbresize(struct inpcbtable *table, int hashsize)
{
	u_long nmask, nlmask;
	int osize;
	void *nhashtbl, *nlhashtbl, *ohashtbl, *olhashtbl;
	struct inpcb *inp;

	MUTEX_ASSERT_LOCKED(&table->inpt_mtx);

	ohashtbl = table->inpt_hashtbl;
	olhashtbl = table->inpt_lhashtbl;
	osize = table->inpt_size;

	nhashtbl = hashinit(hashsize, M_PCB, M_NOWAIT, &nmask);
	if (nhashtbl == NULL)
		return ENOBUFS;
	nlhashtbl = hashinit(hashsize, M_PCB, M_NOWAIT, &nlmask);
	if (nlhashtbl == NULL) {
		hashfree(nhashtbl, hashsize, M_PCB);
		return ENOBUFS;
	}
	table->inpt_hashtbl = nhashtbl;
	table->inpt_lhashtbl = nlhashtbl;
	table->inpt_mask = nmask;
	table->inpt_lmask = nlmask;
	table->inpt_size = hashsize;

	TAILQ_FOREACH(inp, &table->inpt_queue, inp_queue) {
		if (in_pcb_is_iterator(inp))
			continue;
		LIST_REMOVE(inp, inp_lhash);
		LIST_REMOVE(inp, inp_hash);
		in_pcbhash_insert(inp);
	}
	hashfree(ohashtbl, osize, M_PCB);
	hashfree(olhashtbl, osize, M_PCB);

	return (0);
}

#ifdef DIAGNOSTIC
int	in_pcbnotifymiss = 0;
#endif

/*
 * The in(6)_pcblookup functions are used to locate connected sockets
 * quickly:
 *     faddr.fport <-> laddr.lport
 * No wildcard matching is done so that listening sockets are not found.
 * If the functions return NULL in(6)_pcblookup_listen can be used to
 * find a listening/bound socket that may accept the connection.
 * After those two lookups no other are necessary.
 */
struct inpcb *
in_pcblookup_lock(struct inpcbtable *table, struct in_addr faddr,
    u_int fport, struct in_addr laddr, u_int lport, u_int rtable, int lock)
{
	struct inpcb *inp;
	uint64_t hash;
	u_int rdomain;

	rdomain = rtable_l2(rtable);
	hash = in_pcbhash(table, rdomain, &faddr, fport, &laddr, lport);

	if (lock == IN_PCBLOCK_GRAB) {
		mtx_enter(&table->inpt_mtx);
	} else {
		KASSERT(lock == IN_PCBLOCK_HOLD);
		MUTEX_ASSERT_LOCKED(&table->inpt_mtx);
	}
	inp = in_pcbhash_lookup(table, hash, rdomain,
	    &faddr, fport, &laddr, lport);
	if (lock == IN_PCBLOCK_GRAB) {
		in_pcbref(inp);
		mtx_leave(&table->inpt_mtx);
	}

#ifdef DIAGNOSTIC
	if (inp == NULL && in_pcbnotifymiss) {
		printf("%s: faddr=%08x fport=%d laddr=%08x lport=%d rdom=%u\n",
		    __func__, ntohl(faddr.s_addr), ntohs(fport),
		    ntohl(laddr.s_addr), ntohs(lport), rdomain);
	}
#endif
	return (inp);
}

struct inpcb *
in_pcblookup(struct inpcbtable *table, struct in_addr faddr,
    u_int fport, struct in_addr laddr, u_int lport, u_int rtable)
{
	return in_pcblookup_lock(table, faddr, fport, laddr, lport, rtable,
	    IN_PCBLOCK_GRAB);
}

/*
 * The in(6)_pcblookup_listen functions are used to locate listening
 * sockets quickly.  This are sockets with unspecified foreign address
 * and port:
 *		*.*     <-> laddr.lport
 *		*.*     <->     *.lport
 */
struct inpcb *
in_pcblookup_listen(struct inpcbtable *table, struct in_addr laddr,
    u_int lport_arg, struct mbuf *m, u_int rtable)
{
	const struct in_addr *key1, *key2;
	struct inpcb *inp;
	uint64_t hash;
	u_int16_t lport = lport_arg;
	u_int rdomain;

	key1 = &laddr;
	key2 = &zeroin_addr;
#if NPF > 0
	if (m && m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
		struct pf_divert *divert;

		divert = pf_find_divert(m);
		KASSERT(divert != NULL);
		switch (divert->type) {
		case PF_DIVERT_TO:
			key1 = key2 = &divert->addr.v4;
			lport = divert->port;
			break;
		case PF_DIVERT_REPLY:
			return (NULL);
		default:
			panic("%s: unknown divert type %d, mbuf %p, divert %p",
			    __func__, divert->type, m, divert);
		}
	} else if (m && m->m_pkthdr.pf.flags & PF_TAG_TRANSLATE_LOCALHOST) {
		/*
		 * Redirected connections should not be treated the same
		 * as connections directed to 127.0.0.0/8 since localhost
		 * can only be accessed from the host itself.
		 * For example portmap(8) grants more permissions for
		 * connections to the socket bound to 127.0.0.1 than
		 * to the * socket.
		 */
		key1 = &zeroin_addr;
		key2 = &laddr;
	}
#endif

	rdomain = rtable_l2(rtable);
	hash = in_pcbhash(table, rdomain, &zeroin_addr, 0, key1, lport);

	mtx_enter(&table->inpt_mtx);
	inp = in_pcbhash_lookup(table, hash, rdomain,
	    &zeroin_addr, 0, key1, lport);
	if (inp == NULL && key1->s_addr != key2->s_addr) {
		hash = in_pcbhash(table, rdomain,
		    &zeroin_addr, 0, key2, lport);
		inp = in_pcbhash_lookup(table, hash, rdomain,
		    &zeroin_addr, 0, key2, lport);
	}
	in_pcbref(inp);
	mtx_leave(&table->inpt_mtx);

#ifdef DIAGNOSTIC
	if (inp == NULL && in_pcbnotifymiss) {
		printf("%s: laddr=%08x lport=%d rdom=%u\n",
		    __func__, ntohl(laddr.s_addr), ntohs(lport), rdomain);
	}
#endif
	return (inp);
}

int
in_pcbset_rtableid(struct inpcb *inp, u_int rtableid)
{
	struct inpcbtable *table = inp->inp_table;

	/* table must exist */
	if (!rtable_exists(rtableid))
		return (EINVAL);

	mtx_enter(&table->inpt_mtx);
	if (inp->inp_lport) {
		mtx_leave(&table->inpt_mtx);
		return (EBUSY);
	}
	inp->inp_rtableid = rtableid;
	in_pcbrehash(inp);
	mtx_leave(&table->inpt_mtx);

	return (0);
}

int
in_pcbset_addr(struct inpcb *inp, const struct sockaddr *fsa,
    const struct sockaddr *lsa, u_int rtableid)
{
	struct inpcbtable *table = inp->inp_table;
	const struct sockaddr_in *fsin, *lsin;
	struct inpcb *t;

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		KASSERT(fsa->sa_family == AF_INET6);
		KASSERT(lsa->sa_family == AF_INET6);
		return in6_pcbset_addr(inp, satosin6_const(fsa),
		    satosin6_const(lsa), rtableid);
	}
#endif
	KASSERT(fsa->sa_family == AF_INET);
	KASSERT(lsa->sa_family == AF_INET);
	fsin = satosin_const(fsa);
	lsin = satosin_const(lsa);

	mtx_enter(&table->inpt_mtx);

	t = in_pcblookup_lock(inp->inp_table, fsin->sin_addr, fsin->sin_port,
	    lsin->sin_addr, lsin->sin_port, rtableid, IN_PCBLOCK_HOLD);
	if (t != NULL) {
		mtx_leave(&table->inpt_mtx);
		return (EADDRINUSE);
	}

	inp->inp_rtableid = rtableid;
	inp->inp_laddr = lsin->sin_addr;
	inp->inp_lport = lsin->sin_port;
	inp->inp_faddr = fsin->sin_addr;
	inp->inp_fport = fsin->sin_port;
	in_pcbrehash(inp);

	mtx_leave(&table->inpt_mtx);

#if NSTOEPLITZ > 0
	inp->inp_flowid = stoeplitz_ip4port(inp->inp_faddr.s_addr,
	    inp->inp_laddr.s_addr, inp->inp_fport, inp->inp_lport);
#endif
	return (0);
}

void
in_pcbunset_faddr(struct inpcb *inp)
{
	struct inpcbtable *table = inp->inp_table;

	mtx_enter(&table->inpt_mtx);
#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6))
		inp->inp_faddr6 = in6addr_any;
	else
#endif
		inp->inp_faddr.s_addr = INADDR_ANY;
	inp->inp_fport = 0;
	in_pcbrehash(inp);
	mtx_leave(&table->inpt_mtx);
}

void
in_pcbunset_laddr(struct inpcb *inp)
{
	struct inpcbtable *table = inp->inp_table;

	mtx_enter(&table->inpt_mtx);
#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		inp->inp_faddr6 = in6addr_any;
		inp->inp_laddr6 = in6addr_any;
	} else
#endif
	{
		inp->inp_faddr.s_addr = INADDR_ANY;
		inp->inp_laddr.s_addr = INADDR_ANY;
	}
	inp->inp_fport = 0;
	in_pcbrehash(inp);
	mtx_leave(&table->inpt_mtx);
}
