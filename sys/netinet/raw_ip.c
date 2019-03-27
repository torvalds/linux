/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.
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
 *	@(#)raw_ip.c	8.7 (Berkeley) 5/15/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <vm/uma.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_mroute.h>
#include <netinet/ip_icmp.h>

#include <netipsec/ipsec_support.h>

#include <machine/stdarg.h>
#include <security/mac/mac_framework.h>

VNET_DEFINE(int, ip_defttl) = IPDEFTTL;
SYSCTL_INT(_net_inet_ip, IPCTL_DEFTTL, ttl, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip_defttl), 0,
    "Maximum TTL on IP packets");

VNET_DEFINE(struct inpcbhead, ripcb);
VNET_DEFINE(struct inpcbinfo, ripcbinfo);

#define	V_ripcb			VNET(ripcb)
#define	V_ripcbinfo		VNET(ripcbinfo)

/*
 * Control and data hooks for ipfw, dummynet, divert and so on.
 * The data hooks are not used here but it is convenient
 * to keep them all in one place.
 */
VNET_DEFINE(ip_fw_chk_ptr_t, ip_fw_chk_ptr) = NULL;
VNET_DEFINE(ip_fw_ctl_ptr_t, ip_fw_ctl_ptr) = NULL;

int	(*ip_dn_ctl_ptr)(struct sockopt *);
int	(*ip_dn_io_ptr)(struct mbuf **, struct ip_fw_args *);
void	(*ip_divert_ptr)(struct mbuf *, bool);
int	(*ng_ipfw_input_p)(struct mbuf **, struct ip_fw_args *, bool);

#ifdef INET
/*
 * Hooks for multicast routing. They all default to NULL, so leave them not
 * initialized and rely on BSS being set to 0.
 */

/*
 * The socket used to communicate with the multicast routing daemon.
 */
VNET_DEFINE(struct socket *, ip_mrouter);

/*
 * The various mrouter and rsvp functions.
 */
int (*ip_mrouter_set)(struct socket *, struct sockopt *);
int (*ip_mrouter_get)(struct socket *, struct sockopt *);
int (*ip_mrouter_done)(void);
int (*ip_mforward)(struct ip *, struct ifnet *, struct mbuf *,
		   struct ip_moptions *);
int (*mrt_ioctl)(u_long, caddr_t, int);
int (*legal_vif_num)(int);
u_long (*ip_mcast_src)(int);

int (*rsvp_input_p)(struct mbuf **, int *, int);
int (*ip_rsvp_vif)(struct socket *, struct sockopt *);
void (*ip_rsvp_force_done)(struct socket *);
#endif /* INET */

extern	struct protosw inetsw[];

u_long	rip_sendspace = 9216;
SYSCTL_ULONG(_net_inet_raw, OID_AUTO, maxdgram, CTLFLAG_RW,
    &rip_sendspace, 0, "Maximum outgoing raw IP datagram size");

u_long	rip_recvspace = 9216;
SYSCTL_ULONG(_net_inet_raw, OID_AUTO, recvspace, CTLFLAG_RW,
    &rip_recvspace, 0, "Maximum space for incoming raw IP datagrams");

/*
 * Hash functions
 */

#define INP_PCBHASH_RAW_SIZE	256
#define INP_PCBHASH_RAW(proto, laddr, faddr, mask) \
        (((proto) + (laddr) + (faddr)) % (mask) + 1)

#ifdef INET
static void
rip_inshash(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbhead *pcbhash;
	int hash;

	INP_INFO_WLOCK_ASSERT(pcbinfo);
	INP_WLOCK_ASSERT(inp);
	
	if (inp->inp_ip_p != 0 &&
	    inp->inp_laddr.s_addr != INADDR_ANY &&
	    inp->inp_faddr.s_addr != INADDR_ANY) {
		hash = INP_PCBHASH_RAW(inp->inp_ip_p, inp->inp_laddr.s_addr,
		    inp->inp_faddr.s_addr, pcbinfo->ipi_hashmask);
	} else
		hash = 0;
	pcbhash = &pcbinfo->ipi_hashbase[hash];
	CK_LIST_INSERT_HEAD(pcbhash, inp, inp_hash);
}

static void
rip_delhash(struct inpcb *inp)
{

	INP_INFO_WLOCK_ASSERT(inp->inp_pcbinfo);
	INP_WLOCK_ASSERT(inp);

	CK_LIST_REMOVE(inp, inp_hash);
}
#endif /* INET */

/*
 * Raw interface to IP protocol.
 */

/*
 * Initialize raw connection block q.
 */
static void
rip_zone_change(void *tag)
{

	uma_zone_set_max(V_ripcbinfo.ipi_zone, maxsockets);
}

static int
rip_inpcb_init(void *mem, int size, int flags)
{
	struct inpcb *inp = mem;

	INP_LOCK_INIT(inp, "inp", "rawinp");
	return (0);
}

void
rip_init(void)
{

	in_pcbinfo_init(&V_ripcbinfo, "rip", &V_ripcb, INP_PCBHASH_RAW_SIZE,
	    1, "ripcb", rip_inpcb_init, IPI_HASHFIELDS_NONE);
	EVENTHANDLER_REGISTER(maxsockets_change, rip_zone_change, NULL,
	    EVENTHANDLER_PRI_ANY);
}

#ifdef VIMAGE
static void
rip_destroy(void *unused __unused)
{

	in_pcbinfo_destroy(&V_ripcbinfo);
}
VNET_SYSUNINIT(raw_ip, SI_SUB_PROTO_DOMAIN, SI_ORDER_FOURTH, rip_destroy, NULL);
#endif

#ifdef INET
static int
rip_append(struct inpcb *last, struct ip *ip, struct mbuf *n,
    struct sockaddr_in *ripsrc)
{
	int policyfail = 0;

	INP_LOCK_ASSERT(last);

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	/* check AH/ESP integrity. */
	if (IPSEC_ENABLED(ipv4)) {
		if (IPSEC_CHECK_POLICY(ipv4, n, last) != 0)
			policyfail = 1;
	}
#endif /* IPSEC */
#ifdef MAC
	if (!policyfail && mac_inpcb_check_deliver(last, n) != 0)
		policyfail = 1;
#endif
	/* Check the minimum TTL for socket. */
	if (last->inp_ip_minttl && last->inp_ip_minttl > ip->ip_ttl)
		policyfail = 1;
	if (!policyfail) {
		struct mbuf *opts = NULL;
		struct socket *so;

		so = last->inp_socket;
		if ((last->inp_flags & INP_CONTROLOPTS) ||
		    (so->so_options & (SO_TIMESTAMP | SO_BINTIME)))
			ip_savecontrol(last, &opts, ip, n);
		SOCKBUF_LOCK(&so->so_rcv);
		if (sbappendaddr_locked(&so->so_rcv,
		    (struct sockaddr *)ripsrc, n, opts) == 0) {
			/* should notify about lost packet */
			m_freem(n);
			if (opts)
				m_freem(opts);
			SOCKBUF_UNLOCK(&so->so_rcv);
		} else
			sorwakeup_locked(so);
	} else
		m_freem(n);
	return (policyfail);
}

/*
 * Setup generic address and protocol structures for raw_input routine, then
 * pass them along with mbuf chain.
 */
int
rip_input(struct mbuf **mp, int *offp, int proto)
{
	struct ifnet *ifp;
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	struct inpcb *inp, *last;
	struct sockaddr_in ripsrc;
	struct epoch_tracker et;
	int hash;

	*mp = NULL;

	bzero(&ripsrc, sizeof(ripsrc));
	ripsrc.sin_len = sizeof(ripsrc);
	ripsrc.sin_family = AF_INET;
	ripsrc.sin_addr = ip->ip_src;
	last = NULL;

	ifp = m->m_pkthdr.rcvif;

	hash = INP_PCBHASH_RAW(proto, ip->ip_src.s_addr,
	    ip->ip_dst.s_addr, V_ripcbinfo.ipi_hashmask);
	INP_INFO_RLOCK_ET(&V_ripcbinfo, et);
	CK_LIST_FOREACH(inp, &V_ripcbinfo.ipi_hashbase[hash], inp_hash) {
		if (inp->inp_ip_p != proto)
			continue;
#ifdef INET6
		/* XXX inp locking */
		if ((inp->inp_vflag & INP_IPV4) == 0)
			continue;
#endif
		if (inp->inp_laddr.s_addr != ip->ip_dst.s_addr)
			continue;
		if (inp->inp_faddr.s_addr != ip->ip_src.s_addr)
			continue;
		if (last != NULL) {
			struct mbuf *n;

			n = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (n != NULL)
			    (void) rip_append(last, ip, n, &ripsrc);
			/* XXX count dropped packet */
			INP_RUNLOCK(last);
			last = NULL;
		}
		INP_RLOCK(inp);
		if (__predict_false(inp->inp_flags2 & INP_FREED))
			goto skip_1;
		if (jailed_without_vnet(inp->inp_cred)) {
			/*
			 * XXX: If faddr was bound to multicast group,
			 * jailed raw socket will drop datagram.
			 */
			if (prison_check_ip4(inp->inp_cred, &ip->ip_dst) != 0)
				goto skip_1;
		}
		last = inp;
		continue;
	skip_1:
		INP_RUNLOCK(inp);
	}
	CK_LIST_FOREACH(inp, &V_ripcbinfo.ipi_hashbase[0], inp_hash) {
		if (inp->inp_ip_p && inp->inp_ip_p != proto)
			continue;
#ifdef INET6
		/* XXX inp locking */
		if ((inp->inp_vflag & INP_IPV4) == 0)
			continue;
#endif
		if (!in_nullhost(inp->inp_laddr) &&
		    !in_hosteq(inp->inp_laddr, ip->ip_dst))
			continue;
		if (!in_nullhost(inp->inp_faddr) &&
		    !in_hosteq(inp->inp_faddr, ip->ip_src))
			continue;
		if (last != NULL) {
			struct mbuf *n;

			n = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (n != NULL)
				(void) rip_append(last, ip, n, &ripsrc);
			/* XXX count dropped packet */
			INP_RUNLOCK(last);
			last = NULL;
		}
		INP_RLOCK(inp);
		if (__predict_false(inp->inp_flags2 & INP_FREED))
			goto skip_2;
		if (jailed_without_vnet(inp->inp_cred)) {
			/*
			 * Allow raw socket in jail to receive multicast;
			 * assume process had PRIV_NETINET_RAW at attach,
			 * and fall through into normal filter path if so.
			 */
			if (!IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) &&
			    prison_check_ip4(inp->inp_cred, &ip->ip_dst) != 0)
				goto skip_2;
		}
		/*
		 * If this raw socket has multicast state, and we
		 * have received a multicast, check if this socket
		 * should receive it, as multicast filtering is now
		 * the responsibility of the transport layer.
		 */
		if (inp->inp_moptions != NULL &&
		    IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
			/*
			 * If the incoming datagram is for IGMP, allow it
			 * through unconditionally to the raw socket.
			 *
			 * In the case of IGMPv2, we may not have explicitly
			 * joined the group, and may have set IFF_ALLMULTI
			 * on the interface. imo_multi_filter() may discard
			 * control traffic we actually need to see.
			 *
			 * Userland multicast routing daemons should continue
			 * filter the control traffic appropriately.
			 */
			int blocked;

			blocked = MCAST_PASS;
			if (proto != IPPROTO_IGMP) {
				struct sockaddr_in group;

				bzero(&group, sizeof(struct sockaddr_in));
				group.sin_len = sizeof(struct sockaddr_in);
				group.sin_family = AF_INET;
				group.sin_addr = ip->ip_dst;

				blocked = imo_multi_filter(inp->inp_moptions,
				    ifp,
				    (struct sockaddr *)&group,
				    (struct sockaddr *)&ripsrc);
			}

			if (blocked != MCAST_PASS) {
				IPSTAT_INC(ips_notmember);
				goto skip_2;
			}
		}
		last = inp;
		continue;
	skip_2:
		INP_RUNLOCK(inp);
	}
	INP_INFO_RUNLOCK_ET(&V_ripcbinfo, et);
	if (last != NULL) {
		if (rip_append(last, ip, m, &ripsrc) != 0)
			IPSTAT_INC(ips_delivered);
		INP_RUNLOCK(last);
	} else {
		if (inetsw[ip_protox[ip->ip_p]].pr_input == rip_input) {
			IPSTAT_INC(ips_noproto);
			IPSTAT_DEC(ips_delivered);
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_PROTOCOL, 0, 0);
		} else {
			m_freem(m);
		}
	}
	return (IPPROTO_DONE);
}

/*
 * Generate IP header and pass packet to ip_output.  Tack on options user may
 * have setup with control call.
 */
int
rip_output(struct mbuf *m, struct socket *so, ...)
{
	struct ip *ip;
	int error;
	struct inpcb *inp = sotoinpcb(so);
	va_list ap;
	u_long dst;
	int flags = ((so->so_options & SO_DONTROUTE) ? IP_ROUTETOIF : 0) |
	    IP_ALLOWBROADCAST;
	int cnt;
	u_char opttype, optlen, *cp;

	va_start(ap, so);
	dst = va_arg(ap, u_long);
	va_end(ap);

	/*
	 * If the user handed us a complete IP packet, use it.  Otherwise,
	 * allocate an mbuf for a header and fill it in.
	 */
	if ((inp->inp_flags & INP_HDRINCL) == 0) {
		if (m->m_pkthdr.len + sizeof(struct ip) > IP_MAXPACKET) {
			m_freem(m);
			return(EMSGSIZE);
		}
		M_PREPEND(m, sizeof(struct ip), M_NOWAIT);
		if (m == NULL)
			return(ENOBUFS);

		INP_RLOCK(inp);
		ip = mtod(m, struct ip *);
		ip->ip_tos = inp->inp_ip_tos;
		if (inp->inp_flags & INP_DONTFRAG)
			ip->ip_off = htons(IP_DF);
		else
			ip->ip_off = htons(0);
		ip->ip_p = inp->inp_ip_p;
		ip->ip_len = htons(m->m_pkthdr.len);
		ip->ip_src = inp->inp_laddr;
		ip->ip_dst.s_addr = dst;
		if (jailed(inp->inp_cred)) {
			/*
			 * prison_local_ip4() would be good enough but would
			 * let a source of INADDR_ANY pass, which we do not
			 * want to see from jails.
			 */
			if (ip->ip_src.s_addr == INADDR_ANY) {
				error = in_pcbladdr(inp, &ip->ip_dst, &ip->ip_src,
				    inp->inp_cred);
			} else {
				error = prison_local_ip4(inp->inp_cred,
				    &ip->ip_src);
			}
			if (error != 0) {
				INP_RUNLOCK(inp);
				m_freem(m);
				return (error);
			}
		}
		ip->ip_ttl = inp->inp_ip_ttl;
	} else {
		if (m->m_pkthdr.len > IP_MAXPACKET) {
			m_freem(m);
			return(EMSGSIZE);
		}
		INP_RLOCK(inp);
		ip = mtod(m, struct ip *);
		error = prison_check_ip4(inp->inp_cred, &ip->ip_src);
		if (error != 0) {
			INP_RUNLOCK(inp);
			m_freem(m);
			return (error);
		}

		/*
		 * Don't allow both user specified and setsockopt options,
		 * and don't allow packet length sizes that will crash.
		 */
		if (((ip->ip_hl != (sizeof (*ip) >> 2)) && inp->inp_options)
		    || (ntohs(ip->ip_len) != m->m_pkthdr.len)
		    || (ntohs(ip->ip_len) < (ip->ip_hl << 2))) {
			INP_RUNLOCK(inp);
			m_freem(m);
			return (EINVAL);
		}
		/*
		 * Don't allow IP options which do not have the required
		 * structure as specified in section 3.1 of RFC 791 on
		 * pages 15-23.
		 */
		cp = (u_char *)(ip + 1);
		cnt = (ip->ip_hl << 2) - sizeof (struct ip);
		for (; cnt > 0; cnt -= optlen, cp += optlen) {
			opttype = cp[IPOPT_OPTVAL];
			if (opttype == IPOPT_EOL)
				break;
			if (opttype == IPOPT_NOP) {
				optlen = 1;
				continue;
			}
			if (cnt < IPOPT_OLEN + sizeof(u_char)) {
				INP_RUNLOCK(inp);
				m_freem(m);
				return (EINVAL);
			}
			optlen = cp[IPOPT_OLEN];
			if (optlen < IPOPT_OLEN + sizeof(u_char) ||
			    optlen > cnt) {
				INP_RUNLOCK(inp);
				m_freem(m);
				return (EINVAL);
			}
		}
		/*
		 * This doesn't allow application to specify ID of zero,
		 * but we got this limitation from the beginning of history.
		 */
		if (ip->ip_id == 0)
			ip_fillid(ip);

		/*
		 * XXX prevent ip_output from overwriting header fields.
		 */
		flags |= IP_RAWOUTPUT;
		IPSTAT_INC(ips_rawout);
	}

	if (inp->inp_flags & INP_ONESBCAST)
		flags |= IP_SENDONES;

#ifdef MAC
	mac_inpcb_create_mbuf(inp, m);
#endif

	error = ip_output(m, inp->inp_options, NULL, flags,
	    inp->inp_moptions, inp);
	INP_RUNLOCK(inp);
	return (error);
}

/*
 * Raw IP socket option processing.
 *
 * IMPORTANT NOTE regarding access control: Traditionally, raw sockets could
 * only be created by a privileged process, and as such, socket option
 * operations to manage system properties on any raw socket were allowed to
 * take place without explicit additional access control checks.  However,
 * raw sockets can now also be created in jail(), and therefore explicit
 * checks are now required.  Likewise, raw sockets can be used by a process
 * after it gives up privilege, so some caution is required.  For options
 * passed down to the IP layer via ip_ctloutput(), checks are assumed to be
 * performed in ip_ctloutput() and therefore no check occurs here.
 * Unilaterally checking priv_check() here breaks normal IP socket option
 * operations on raw sockets.
 *
 * When adding new socket options here, make sure to add access control
 * checks here as necessary.
 *
 * XXX-BZ inp locking?
 */
int
rip_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct	inpcb *inp = sotoinpcb(so);
	int	error, optval;

	if (sopt->sopt_level != IPPROTO_IP) {
		if ((sopt->sopt_level == SOL_SOCKET) &&
		    (sopt->sopt_name == SO_SETFIB)) {
			inp->inp_inc.inc_fibnum = so->so_fibnum;
			return (0);
		}
		return (EINVAL);
	}

	error = 0;
	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case IP_HDRINCL:
			optval = inp->inp_flags & INP_HDRINCL;
			error = sooptcopyout(sopt, &optval, sizeof optval);
			break;

		case IP_FW3:	/* generic ipfw v.3 functions */
		case IP_FW_ADD:	/* ADD actually returns the body... */
		case IP_FW_GET:
		case IP_FW_TABLE_GETSIZE:
		case IP_FW_TABLE_LIST:
		case IP_FW_NAT_GET_CONFIG:
		case IP_FW_NAT_GET_LOG:
			if (V_ip_fw_ctl_ptr != NULL)
				error = V_ip_fw_ctl_ptr(sopt);
			else
				error = ENOPROTOOPT;
			break;

		case IP_DUMMYNET3:	/* generic dummynet v.3 functions */
		case IP_DUMMYNET_GET:
			if (ip_dn_ctl_ptr != NULL)
				error = ip_dn_ctl_ptr(sopt);
			else
				error = ENOPROTOOPT;
			break ;

		case MRT_INIT:
		case MRT_DONE:
		case MRT_ADD_VIF:
		case MRT_DEL_VIF:
		case MRT_ADD_MFC:
		case MRT_DEL_MFC:
		case MRT_VERSION:
		case MRT_ASSERT:
		case MRT_API_SUPPORT:
		case MRT_API_CONFIG:
		case MRT_ADD_BW_UPCALL:
		case MRT_DEL_BW_UPCALL:
			error = priv_check(curthread, PRIV_NETINET_MROUTE);
			if (error != 0)
				return (error);
			error = ip_mrouter_get ? ip_mrouter_get(so, sopt) :
				EOPNOTSUPP;
			break;

		default:
			error = ip_ctloutput(so, sopt);
			break;
		}
		break;

	case SOPT_SET:
		switch (sopt->sopt_name) {
		case IP_HDRINCL:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;
			if (optval)
				inp->inp_flags |= INP_HDRINCL;
			else
				inp->inp_flags &= ~INP_HDRINCL;
			break;

		case IP_FW3:	/* generic ipfw v.3 functions */
		case IP_FW_ADD:
		case IP_FW_DEL:
		case IP_FW_FLUSH:
		case IP_FW_ZERO:
		case IP_FW_RESETLOG:
		case IP_FW_TABLE_ADD:
		case IP_FW_TABLE_DEL:
		case IP_FW_TABLE_FLUSH:
		case IP_FW_NAT_CFG:
		case IP_FW_NAT_DEL:
			if (V_ip_fw_ctl_ptr != NULL)
				error = V_ip_fw_ctl_ptr(sopt);
			else
				error = ENOPROTOOPT;
			break;

		case IP_DUMMYNET3:	/* generic dummynet v.3 functions */
		case IP_DUMMYNET_CONFIGURE:
		case IP_DUMMYNET_DEL:
		case IP_DUMMYNET_FLUSH:
			if (ip_dn_ctl_ptr != NULL)
				error = ip_dn_ctl_ptr(sopt);
			else
				error = ENOPROTOOPT ;
			break ;

		case IP_RSVP_ON:
			error = priv_check(curthread, PRIV_NETINET_MROUTE);
			if (error != 0)
				return (error);
			error = ip_rsvp_init(so);
			break;

		case IP_RSVP_OFF:
			error = priv_check(curthread, PRIV_NETINET_MROUTE);
			if (error != 0)
				return (error);
			error = ip_rsvp_done();
			break;

		case IP_RSVP_VIF_ON:
		case IP_RSVP_VIF_OFF:
			error = priv_check(curthread, PRIV_NETINET_MROUTE);
			if (error != 0)
				return (error);
			error = ip_rsvp_vif ?
				ip_rsvp_vif(so, sopt) : EINVAL;
			break;

		case MRT_INIT:
		case MRT_DONE:
		case MRT_ADD_VIF:
		case MRT_DEL_VIF:
		case MRT_ADD_MFC:
		case MRT_DEL_MFC:
		case MRT_VERSION:
		case MRT_ASSERT:
		case MRT_API_SUPPORT:
		case MRT_API_CONFIG:
		case MRT_ADD_BW_UPCALL:
		case MRT_DEL_BW_UPCALL:
			error = priv_check(curthread, PRIV_NETINET_MROUTE);
			if (error != 0)
				return (error);
			error = ip_mrouter_set ? ip_mrouter_set(so, sopt) :
					EOPNOTSUPP;
			break;

		default:
			error = ip_ctloutput(so, sopt);
			break;
		}
		break;
	}

	return (error);
}

/*
 * This function exists solely to receive the PRC_IFDOWN messages which are
 * sent by if_down().  It looks for an ifaddr whose ifa_addr is sa, and calls
 * in_ifadown() to remove all routes corresponding to that address.  It also
 * receives the PRC_IFUP messages from if_up() and reinstalls the interface
 * routes.
 */
void
rip_ctlinput(int cmd, struct sockaddr *sa, void *vip)
{
	struct rm_priotracker in_ifa_tracker;
	struct in_ifaddr *ia;
	struct ifnet *ifp;
	int err;
	int flags;

	switch (cmd) {
	case PRC_IFDOWN:
		IN_IFADDR_RLOCK(&in_ifa_tracker);
		CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
			if (ia->ia_ifa.ifa_addr == sa
			    && (ia->ia_flags & IFA_ROUTE)) {
				ifa_ref(&ia->ia_ifa);
				IN_IFADDR_RUNLOCK(&in_ifa_tracker);
				/*
				 * in_scrubprefix() kills the interface route.
				 */
				in_scrubprefix(ia, 0);
				/*
				 * in_ifadown gets rid of all the rest of the
				 * routes.  This is not quite the right thing
				 * to do, but at least if we are running a
				 * routing process they will come back.
				 */
				in_ifadown(&ia->ia_ifa, 0);
				ifa_free(&ia->ia_ifa);
				break;
			}
		}
		if (ia == NULL)		/* If ia matched, already unlocked. */
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		break;

	case PRC_IFUP:
		IN_IFADDR_RLOCK(&in_ifa_tracker);
		CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
			if (ia->ia_ifa.ifa_addr == sa)
				break;
		}
		if (ia == NULL || (ia->ia_flags & IFA_ROUTE)) {
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			return;
		}
		ifa_ref(&ia->ia_ifa);
		IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		flags = RTF_UP;
		ifp = ia->ia_ifa.ifa_ifp;

		if ((ifp->if_flags & IFF_LOOPBACK)
		    || (ifp->if_flags & IFF_POINTOPOINT))
			flags |= RTF_HOST;

		err = ifa_del_loopback_route((struct ifaddr *)ia, sa);

		err = rtinit(&ia->ia_ifa, RTM_ADD, flags);
		if (err == 0)
			ia->ia_flags |= IFA_ROUTE;

		err = ifa_add_loopback_route((struct ifaddr *)ia, sa);

		ifa_free(&ia->ia_ifa);
		break;
	}
}

static int
rip_attach(struct socket *so, int proto, struct thread *td)
{
	struct inpcb *inp;
	int error;

	inp = sotoinpcb(so);
	KASSERT(inp == NULL, ("rip_attach: inp != NULL"));

	error = priv_check(td, PRIV_NETINET_RAW);
	if (error)
		return (error);
	if (proto >= IPPROTO_MAX || proto < 0)
		return EPROTONOSUPPORT;
	error = soreserve(so, rip_sendspace, rip_recvspace);
	if (error)
		return (error);
	INP_INFO_WLOCK(&V_ripcbinfo);
	error = in_pcballoc(so, &V_ripcbinfo);
	if (error) {
		INP_INFO_WUNLOCK(&V_ripcbinfo);
		return (error);
	}
	inp = (struct inpcb *)so->so_pcb;
	inp->inp_vflag |= INP_IPV4;
	inp->inp_ip_p = proto;
	inp->inp_ip_ttl = V_ip_defttl;
	rip_inshash(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
	INP_WUNLOCK(inp);
	return (0);
}

static void
rip_detach(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_detach: inp == NULL"));
	KASSERT(inp->inp_faddr.s_addr == INADDR_ANY, 
	    ("rip_detach: not closed"));

	INP_INFO_WLOCK(&V_ripcbinfo);
	INP_WLOCK(inp);
	rip_delhash(inp);
	if (so == V_ip_mrouter && ip_mrouter_done)
		ip_mrouter_done();
	if (ip_rsvp_force_done)
		ip_rsvp_force_done(so);
	if (so == V_ip_rsvpd)
		ip_rsvp_done();
	in_pcbdetach(inp);
	in_pcbfree(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
}

static void
rip_dodisconnect(struct socket *so, struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;

	pcbinfo = inp->inp_pcbinfo;
	INP_INFO_WLOCK(pcbinfo);
	INP_WLOCK(inp);
	rip_delhash(inp);
	inp->inp_faddr.s_addr = INADDR_ANY;
	rip_inshash(inp);
	SOCK_LOCK(so);
	so->so_state &= ~SS_ISCONNECTED;
	SOCK_UNLOCK(so);
	INP_WUNLOCK(inp);
	INP_INFO_WUNLOCK(pcbinfo);
}

static void
rip_abort(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_abort: inp == NULL"));

	rip_dodisconnect(so, inp);
}

static void
rip_close(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_close: inp == NULL"));

	rip_dodisconnect(so, inp);
}

static int
rip_disconnect(struct socket *so)
{
	struct inpcb *inp;

	if ((so->so_state & SS_ISCONNECTED) == 0)
		return (ENOTCONN);

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_disconnect: inp == NULL"));

	rip_dodisconnect(so, inp);
	return (0);
}

static int
rip_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct sockaddr_in *addr = (struct sockaddr_in *)nam;
	struct inpcb *inp;
	int error;

	if (nam->sa_len != sizeof(*addr))
		return (EINVAL);

	error = prison_check_ip4(td->td_ucred, &addr->sin_addr);
	if (error != 0)
		return (error);

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_bind: inp == NULL"));

	if (CK_STAILQ_EMPTY(&V_ifnet) ||
	    (addr->sin_family != AF_INET && addr->sin_family != AF_IMPLINK) ||
	    (addr->sin_addr.s_addr &&
	     (inp->inp_flags & INP_BINDANY) == 0 &&
	     ifa_ifwithaddr_check((struct sockaddr *)addr) == 0))
		return (EADDRNOTAVAIL);

	INP_INFO_WLOCK(&V_ripcbinfo);
	INP_WLOCK(inp);
	rip_delhash(inp);
	inp->inp_laddr = addr->sin_addr;
	rip_inshash(inp);
	INP_WUNLOCK(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
	return (0);
}

static int
rip_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct sockaddr_in *addr = (struct sockaddr_in *)nam;
	struct inpcb *inp;

	if (nam->sa_len != sizeof(*addr))
		return (EINVAL);
	if (CK_STAILQ_EMPTY(&V_ifnet))
		return (EADDRNOTAVAIL);
	if (addr->sin_family != AF_INET && addr->sin_family != AF_IMPLINK)
		return (EAFNOSUPPORT);

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_connect: inp == NULL"));

	INP_INFO_WLOCK(&V_ripcbinfo);
	INP_WLOCK(inp);
	rip_delhash(inp);
	inp->inp_faddr = addr->sin_addr;
	rip_inshash(inp);
	soisconnected(so);
	INP_WUNLOCK(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
	return (0);
}

static int
rip_shutdown(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_shutdown: inp == NULL"));

	INP_WLOCK(inp);
	socantsendmore(so);
	INP_WUNLOCK(inp);
	return (0);
}

static int
rip_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *nam,
    struct mbuf *control, struct thread *td)
{
	struct inpcb *inp;
	u_long dst;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip_send: inp == NULL"));

	/*
	 * Note: 'dst' reads below are unlocked.
	 */
	if (so->so_state & SS_ISCONNECTED) {
		if (nam) {
			m_freem(m);
			return (EISCONN);
		}
		dst = inp->inp_faddr.s_addr;	/* Unlocked read. */
	} else {
		if (nam == NULL) {
			m_freem(m);
			return (ENOTCONN);
		}
		dst = ((struct sockaddr_in *)nam)->sin_addr.s_addr;
	}
	return (rip_output(m, so, dst));
}
#endif /* INET */

static int
rip_pcblist(SYSCTL_HANDLER_ARGS)
{
	int error, i, n;
	struct inpcb *inp, **inp_list;
	inp_gen_t gencnt;
	struct xinpgen xig;
	struct epoch_tracker et;

	/*
	 * The process of preparing the TCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	if (req->oldptr == 0) {
		n = V_ripcbinfo.ipi_count;
		n += imax(n / 8, 10);
		req->oldidx = 2 * (sizeof xig) + n * sizeof(struct xinpcb);
		return (0);
	}

	if (req->newptr != 0)
		return (EPERM);

	/*
	 * OK, now we're committed to doing something.
	 */
	INP_INFO_WLOCK(&V_ripcbinfo);
	gencnt = V_ripcbinfo.ipi_gencnt;
	n = V_ripcbinfo.ipi_count;
	INP_INFO_WUNLOCK(&V_ripcbinfo);

	bzero(&xig, sizeof(xig));
	xig.xig_len = sizeof xig;
	xig.xig_count = n;
	xig.xig_gen = gencnt;
	xig.xig_sogen = so_gencnt;
	error = SYSCTL_OUT(req, &xig, sizeof xig);
	if (error)
		return (error);

	inp_list = malloc(n * sizeof *inp_list, M_TEMP, M_WAITOK);

	INP_INFO_RLOCK_ET(&V_ripcbinfo, et);
	for (inp = CK_LIST_FIRST(V_ripcbinfo.ipi_listhead), i = 0; inp && i < n;
	     inp = CK_LIST_NEXT(inp, inp_list)) {
		INP_WLOCK(inp);
		if (inp->inp_gencnt <= gencnt &&
		    cr_canseeinpcb(req->td->td_ucred, inp) == 0) {
			in_pcbref(inp);
			inp_list[i++] = inp;
		}
		INP_WUNLOCK(inp);
	}
	INP_INFO_RUNLOCK_ET(&V_ripcbinfo, et);
	n = i;

	error = 0;
	for (i = 0; i < n; i++) {
		inp = inp_list[i];
		INP_RLOCK(inp);
		if (inp->inp_gencnt <= gencnt) {
			struct xinpcb xi;

			in_pcbtoxinpcb(inp, &xi);
			INP_RUNLOCK(inp);
			error = SYSCTL_OUT(req, &xi, sizeof xi);
		} else
			INP_RUNLOCK(inp);
	}
	INP_INFO_WLOCK(&V_ripcbinfo);
	for (i = 0; i < n; i++) {
		inp = inp_list[i];
		INP_RLOCK(inp);
		if (!in_pcbrele_rlocked(inp))
			INP_RUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(&V_ripcbinfo);

	if (!error) {
		struct epoch_tracker et;
		/*
		 * Give the user an updated idea of our state.  If the
		 * generation differs from what we told her before, she knows
		 * that something happened while we were processing this
		 * request, and it might be necessary to retry.
		 */
		INP_INFO_RLOCK_ET(&V_ripcbinfo, et);
		xig.xig_gen = V_ripcbinfo.ipi_gencnt;
		xig.xig_sogen = so_gencnt;
		xig.xig_count = V_ripcbinfo.ipi_count;
		INP_INFO_RUNLOCK_ET(&V_ripcbinfo, et);
		error = SYSCTL_OUT(req, &xig, sizeof xig);
	}
	free(inp_list, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_inet_raw, OID_AUTO/*XXX*/, pcblist,
    CTLTYPE_OPAQUE | CTLFLAG_RD, NULL, 0,
    rip_pcblist, "S,xinpcb", "List of active raw IP sockets");

#ifdef INET
struct pr_usrreqs rip_usrreqs = {
	.pru_abort =		rip_abort,
	.pru_attach =		rip_attach,
	.pru_bind =		rip_bind,
	.pru_connect =		rip_connect,
	.pru_control =		in_control,
	.pru_detach =		rip_detach,
	.pru_disconnect =	rip_disconnect,
	.pru_peeraddr =		in_getpeeraddr,
	.pru_send =		rip_send,
	.pru_shutdown =		rip_shutdown,
	.pru_sockaddr =		in_getsockaddr,
	.pru_sosetlabel =	in_pcbsosetlabel,
	.pru_close =		rip_close,
};
#endif /* INET */
