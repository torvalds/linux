/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <netinet/sctp_os.h>
#ifdef INET6
#include <sys/proc.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_var.h>
#include <netinet6/sctp6_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_input.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_bsd_addr.h>
#include <netinet/sctp_crc32.h>
#include <netinet/icmp6.h>
#include <netinet/udp.h>

extern struct protosw inetsw[];

int
sctp6_input_with_port(struct mbuf **i_pak, int *offp, uint16_t port)
{
	struct mbuf *m;
	int iphlen;
	uint32_t vrf_id;
	uint8_t ecn_bits;
	struct sockaddr_in6 src, dst;
	struct ip6_hdr *ip6;
	struct sctphdr *sh;
	struct sctp_chunkhdr *ch;
	int length, offset;
	uint8_t compute_crc;
	uint32_t mflowid;
	uint8_t mflowtype;
	uint16_t fibnum;

	iphlen = *offp;
	if (SCTP_GET_PKT_VRFID(*i_pak, vrf_id)) {
		SCTP_RELEASE_PKT(*i_pak);
		return (IPPROTO_DONE);
	}
	m = SCTP_HEADER_TO_CHAIN(*i_pak);
#ifdef SCTP_MBUF_LOGGING
	/* Log in any input mbufs */
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		sctp_log_mbc(m, SCTP_MBUF_INPUT);
	}
#endif
#ifdef SCTP_PACKET_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING) {
		sctp_packet_log(m);
	}
#endif
	SCTPDBG(SCTP_DEBUG_CRCOFFLOAD,
	    "sctp6_input(): Packet of length %d received on %s with csum_flags 0x%b.\n",
	    m->m_pkthdr.len,
	    if_name(m->m_pkthdr.rcvif),
	    (int)m->m_pkthdr.csum_flags, CSUM_BITS);
	mflowid = m->m_pkthdr.flowid;
	mflowtype = M_HASHTYPE_GET(m);
	fibnum = M_GETFIB(m);
	SCTP_STAT_INCR(sctps_recvpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_inpackets);
	/* Get IP, SCTP, and first chunk header together in the first mbuf. */
	offset = iphlen + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
	ip6 = mtod(m, struct ip6_hdr *);
	IP6_EXTHDR_GET(sh, struct sctphdr *, m, iphlen,
	    (int)(sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr)));
	if (sh == NULL) {
		SCTP_STAT_INCR(sctps_hdrops);
		return (IPPROTO_DONE);
	}
	ch = (struct sctp_chunkhdr *)((caddr_t)sh + sizeof(struct sctphdr));
	offset -= sizeof(struct sctp_chunkhdr);
	memset(&src, 0, sizeof(struct sockaddr_in6));
	src.sin6_family = AF_INET6;
	src.sin6_len = sizeof(struct sockaddr_in6);
	src.sin6_port = sh->src_port;
	src.sin6_addr = ip6->ip6_src;
	if (in6_setscope(&src.sin6_addr, m->m_pkthdr.rcvif, NULL) != 0) {
		goto out;
	}
	memset(&dst, 0, sizeof(struct sockaddr_in6));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(struct sockaddr_in6);
	dst.sin6_port = sh->dest_port;
	dst.sin6_addr = ip6->ip6_dst;
	if (in6_setscope(&dst.sin6_addr, m->m_pkthdr.rcvif, NULL) != 0) {
		goto out;
	}
	length = ntohs(ip6->ip6_plen) + iphlen;
	/* Validate mbuf chain length with IP payload length. */
	if (SCTP_HEADER_LEN(m) != length) {
		SCTPDBG(SCTP_DEBUG_INPUT1,
		    "sctp6_input() length:%d reported length:%d\n", length, SCTP_HEADER_LEN(m));
		SCTP_STAT_INCR(sctps_hdrops);
		goto out;
	}
	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		goto out;
	}
	ecn_bits = ((ntohl(ip6->ip6_flow) >> 20) & 0x000000ff);
	if (m->m_pkthdr.csum_flags & CSUM_SCTP_VALID) {
		SCTP_STAT_INCR(sctps_recvhwcrc);
		compute_crc = 0;
	} else {
		SCTP_STAT_INCR(sctps_recvswcrc);
		compute_crc = 1;
	}
	sctp_common_input_processing(&m, iphlen, offset, length,
	    (struct sockaddr *)&src,
	    (struct sockaddr *)&dst,
	    sh, ch,
	    compute_crc,
	    ecn_bits,
	    mflowtype, mflowid, fibnum,
	    vrf_id, port);
out:
	if (m) {
		sctp_m_freem(m);
	}
	return (IPPROTO_DONE);
}


int
sctp6_input(struct mbuf **i_pak, int *offp, int proto SCTP_UNUSED)
{
	return (sctp6_input_with_port(i_pak, offp, 0));
}

void
sctp6_notify(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    struct sctp_nets *net,
    uint8_t icmp6_type,
    uint8_t icmp6_code,
    uint32_t next_mtu)
{
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif
	int timer_stopped;

	switch (icmp6_type) {
	case ICMP6_DST_UNREACH:
		if ((icmp6_code == ICMP6_DST_UNREACH_NOROUTE) ||
		    (icmp6_code == ICMP6_DST_UNREACH_ADMIN) ||
		    (icmp6_code == ICMP6_DST_UNREACH_BEYONDSCOPE) ||
		    (icmp6_code == ICMP6_DST_UNREACH_ADDR)) {
			/* Mark the net unreachable. */
			if (net->dest_state & SCTP_ADDR_REACHABLE) {
				/* Ok that destination is not reachable */
				net->dest_state &= ~SCTP_ADDR_REACHABLE;
				net->dest_state &= ~SCTP_ADDR_PF;
				sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN,
				    stcb, 0, (void *)net, SCTP_SO_NOT_LOCKED);
			}
		}
		SCTP_TCB_UNLOCK(stcb);
		break;
	case ICMP6_PARAM_PROB:
		/* Treat it like an ABORT. */
		if (icmp6_code == ICMP6_PARAMPROB_NEXTHEADER) {
			sctp_abort_notification(stcb, 1, 0, NULL, SCTP_SO_NOT_LOCKED);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			so = SCTP_INP_SO(inp);
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 1);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
			(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
			    SCTP_FROM_SCTP_USRREQ + SCTP_LOC_2);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			SCTP_SOCKET_UNLOCK(so, 1);
#endif
		} else {
			SCTP_TCB_UNLOCK(stcb);
		}
		break;
	case ICMP6_PACKET_TOO_BIG:
		if (net->dest_state & SCTP_ADDR_NO_PMTUD) {
			SCTP_TCB_UNLOCK(stcb);
			break;
		}
		if (SCTP_OS_TIMER_PENDING(&net->pmtu_timer.timer)) {
			timer_stopped = 1;
			sctp_timer_stop(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net,
			    SCTP_FROM_SCTP_USRREQ + SCTP_LOC_1);
		} else {
			timer_stopped = 0;
		}
		/* Update the path MTU. */
		if (net->port) {
			next_mtu -= sizeof(struct udphdr);
		}
		if (net->mtu > next_mtu) {
			net->mtu = next_mtu;
		}
		/* Update the association MTU */
		if (stcb->asoc.smallest_mtu > next_mtu) {
			sctp_pathmtu_adjustment(stcb, next_mtu);
		}
		/* Finally, start the PMTU timer if it was running before. */
		if (timer_stopped) {
			sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, net);
		}
		SCTP_TCB_UNLOCK(stcb);
		break;
	default:
		SCTP_TCB_UNLOCK(stcb);
		break;
	}
}

void
sctp6_ctlinput(int cmd, struct sockaddr *pktdst, void *d)
{
	struct ip6ctlparam *ip6cp;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;
	struct sctphdr sh;
	struct sockaddr_in6 src, dst;

	if (pktdst->sa_family != AF_INET6 ||
	    pktdst->sa_len != sizeof(struct sockaddr_in6)) {
		return;
	}

	if ((unsigned)cmd >= PRC_NCMDS) {
		return;
	}
	if (PRC_IS_REDIRECT(cmd)) {
		d = NULL;
	} else if (inet6ctlerrmap[cmd] == 0) {
		return;
	}
	/* If the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
	} else {
		ip6cp = (struct ip6ctlparam *)NULL;
	}

	if (ip6cp != NULL) {
		/*
		 * XXX: We assume that when IPV6 is non NULL, M and OFF are
		 * valid.
		 */
		if (ip6cp->ip6c_m == NULL) {
			return;
		}

		/*
		 * Check if we can safely examine the ports and the
		 * verification tag of the SCTP common header.
		 */
		if (ip6cp->ip6c_m->m_pkthdr.len <
		    (int32_t)(ip6cp->ip6c_off + offsetof(struct sctphdr, checksum))) {
			return;
		}

		/* Copy out the port numbers and the verification tag. */
		memset(&sh, 0, sizeof(sh));
		m_copydata(ip6cp->ip6c_m,
		    ip6cp->ip6c_off,
		    sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t),
		    (caddr_t)&sh);
		memset(&src, 0, sizeof(struct sockaddr_in6));
		src.sin6_family = AF_INET6;
		src.sin6_len = sizeof(struct sockaddr_in6);
		src.sin6_port = sh.src_port;
		src.sin6_addr = ip6cp->ip6c_ip6->ip6_src;
		if (in6_setscope(&src.sin6_addr, ip6cp->ip6c_m->m_pkthdr.rcvif, NULL) != 0) {
			return;
		}
		memset(&dst, 0, sizeof(struct sockaddr_in6));
		dst.sin6_family = AF_INET6;
		dst.sin6_len = sizeof(struct sockaddr_in6);
		dst.sin6_port = sh.dest_port;
		dst.sin6_addr = ip6cp->ip6c_ip6->ip6_dst;
		if (in6_setscope(&dst.sin6_addr, ip6cp->ip6c_m->m_pkthdr.rcvif, NULL) != 0) {
			return;
		}
		inp = NULL;
		net = NULL;
		stcb = sctp_findassociation_addr_sa((struct sockaddr *)&dst,
		    (struct sockaddr *)&src,
		    &inp, &net, 1, SCTP_DEFAULT_VRFID);
		if ((stcb != NULL) &&
		    (net != NULL) &&
		    (inp != NULL)) {
			/* Check the verification tag */
			if (ntohl(sh.v_tag) != 0) {
				/*
				 * This must be the verification tag used
				 * for sending out packets. We don't
				 * consider packets reflecting the
				 * verification tag.
				 */
				if (ntohl(sh.v_tag) != stcb->asoc.peer_vtag) {
					SCTP_TCB_UNLOCK(stcb);
					return;
				}
			} else {
				if (ip6cp->ip6c_m->m_pkthdr.len >=
				    ip6cp->ip6c_off + sizeof(struct sctphdr) +
				    sizeof(struct sctp_chunkhdr) +
				    offsetof(struct sctp_init, a_rwnd)) {
					/*
					 * In this case we can check if we
					 * got an INIT chunk and if the
					 * initiate tag matches.
					 */
					uint32_t initiate_tag;
					uint8_t chunk_type;

					m_copydata(ip6cp->ip6c_m,
					    ip6cp->ip6c_off +
					    sizeof(struct sctphdr),
					    sizeof(uint8_t),
					    (caddr_t)&chunk_type);
					m_copydata(ip6cp->ip6c_m,
					    ip6cp->ip6c_off +
					    sizeof(struct sctphdr) +
					    sizeof(struct sctp_chunkhdr),
					    sizeof(uint32_t),
					    (caddr_t)&initiate_tag);
					if ((chunk_type != SCTP_INITIATION) ||
					    (ntohl(initiate_tag) != stcb->asoc.my_vtag)) {
						SCTP_TCB_UNLOCK(stcb);
						return;
					}
				} else {
					SCTP_TCB_UNLOCK(stcb);
					return;
				}
			}
			sctp6_notify(inp, stcb, net,
			    ip6cp->ip6c_icmp6->icmp6_type,
			    ip6cp->ip6c_icmp6->icmp6_code,
			    ntohl(ip6cp->ip6c_icmp6->icmp6_mtu));
		} else {
			if ((stcb == NULL) && (inp != NULL)) {
				/* reduce inp's ref-count */
				SCTP_INP_WLOCK(inp);
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_WUNLOCK(inp);
			}
			if (stcb) {
				SCTP_TCB_UNLOCK(stcb);
			}
		}
	}
}

/*
 * this routine can probably be collasped into the one in sctp_userreq.c
 * since they do the same thing and now we lookup with a sockaddr
 */
static int
sctp6_getcred(SYSCTL_HANDLER_ARGS)
{
	struct xucred xuc;
	struct sockaddr_in6 addrs[2];
	struct sctp_inpcb *inp;
	struct sctp_nets *net;
	struct sctp_tcb *stcb;
	int error;
	uint32_t vrf_id;

	vrf_id = SCTP_DEFAULT_VRFID;

	error = priv_check(req->td, PRIV_NETINET_GETCRED);
	if (error)
		return (error);

	if (req->newlen != sizeof(addrs)) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}
	if (req->oldlen != sizeof(struct ucred)) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);

	stcb = sctp_findassociation_addr_sa(sin6tosa(&addrs[1]),
	    sin6tosa(&addrs[0]),
	    &inp, &net, 1, vrf_id);
	if (stcb == NULL || inp == NULL || inp->sctp_socket == NULL) {
		if ((inp != NULL) && (stcb == NULL)) {
			/* reduce ref-count */
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			goto cred_can_cont;
		}
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ENOENT);
		error = ENOENT;
		goto out;
	}
	SCTP_TCB_UNLOCK(stcb);
	/*
	 * We use the write lock here, only since in the error leg we need
	 * it. If we used RLOCK, then we would have to
	 * wlock/decr/unlock/rlock. Which in theory could create a hole.
	 * Better to use higher wlock.
	 */
	SCTP_INP_WLOCK(inp);
cred_can_cont:
	error = cr_canseesocket(req->td->td_ucred, inp->sctp_socket);
	if (error) {
		SCTP_INP_WUNLOCK(inp);
		goto out;
	}
	cru2x(inp->sctp_socket->so_cred, &xuc);
	SCTP_INP_WUNLOCK(inp);
	error = SYSCTL_OUT(req, &xuc, sizeof(struct xucred));
out:
	return (error);
}

SYSCTL_PROC(_net_inet6_sctp6, OID_AUTO, getcred, CTLTYPE_OPAQUE | CTLFLAG_RW,
    0, 0,
    sctp6_getcred, "S,ucred", "Get the ucred of a SCTP6 connection");


/* This is the same as the sctp_abort() could be made common */
static void
sctp6_abort(struct socket *so)
{
	struct sctp_inpcb *inp;
	uint32_t flags;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return;
	}
sctp_must_try_again:
	flags = inp->sctp_flags;
#ifdef SCTP_LOG_CLOSING
	sctp_log_closing(inp, NULL, 17);
#endif
	if (((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) &&
	    (atomic_cmpset_int(&inp->sctp_flags, flags, (flags | SCTP_PCB_FLAGS_SOCKET_GONE | SCTP_PCB_FLAGS_CLOSE_IP)))) {
#ifdef SCTP_LOG_CLOSING
		sctp_log_closing(inp, NULL, 16);
#endif
		sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
		    SCTP_CALLED_AFTER_CMPSET_OFCLOSE);
		SOCK_LOCK(so);
		SCTP_SB_CLEAR(so->so_snd);
		/*
		 * same for the rcv ones, they are only here for the
		 * accounting/select.
		 */
		SCTP_SB_CLEAR(so->so_rcv);
		/* Now null out the reference, we are completely detached. */
		so->so_pcb = NULL;
		SOCK_UNLOCK(so);
	} else {
		flags = inp->sctp_flags;
		if ((flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) {
			goto sctp_must_try_again;
		}
	}
	return;
}

static int
sctp6_attach(struct socket *so, int proto SCTP_UNUSED, struct thread *p SCTP_UNUSED)
{
	struct in6pcb *inp6;
	int error;
	struct sctp_inpcb *inp;
	uint32_t vrf_id = SCTP_DEFAULT_VRFID;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp != NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}

	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		error = SCTP_SORESERVE(so, SCTP_BASE_SYSCTL(sctp_sendspace), SCTP_BASE_SYSCTL(sctp_recvspace));
		if (error)
			return (error);
	}
	error = sctp_inpcb_alloc(so, vrf_id);
	if (error)
		return (error);
	inp = (struct sctp_inpcb *)so->so_pcb;
	SCTP_INP_WLOCK(inp);
	inp->sctp_flags |= SCTP_PCB_FLAGS_BOUND_V6;	/* I'm v6! */
	inp6 = (struct in6pcb *)inp;

	inp6->inp_vflag |= INP_IPV6;
	inp6->in6p_hops = -1;	/* use kernel default */
	inp6->in6p_cksum = -1;	/* just to be sure */
#ifdef INET
	/*
	 * XXX: ugly!! IPv4 TTL initialization is necessary for an IPv6
	 * socket as well, because the socket may be bound to an IPv6
	 * wildcard address, which may match an IPv4-mapped IPv6 address.
	 */
	inp6->inp_ip_ttl = MODULE_GLOBAL(ip_defttl);
#endif
	SCTP_INP_WUNLOCK(inp);
	return (0);
}

static int
sctp6_bind(struct socket *so, struct sockaddr *addr, struct thread *p)
{
	struct sctp_inpcb *inp;
	struct in6pcb *inp6;
	int error;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}

	if (addr) {
		switch (addr->sa_family) {
#ifdef INET
		case AF_INET:
			if (addr->sa_len != sizeof(struct sockaddr_in)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
				return (EINVAL);
			}
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (addr->sa_len != sizeof(struct sockaddr_in6)) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
				return (EINVAL);
			}
			break;
#endif
		default:
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
	}
	inp6 = (struct in6pcb *)inp;
	inp6->inp_vflag &= ~INP_IPV4;
	inp6->inp_vflag |= INP_IPV6;
	if ((addr != NULL) && (SCTP_IPV6_V6ONLY(inp6) == 0)) {
		switch (addr->sa_family) {
#ifdef INET
		case AF_INET:
			/* binding v4 addr to v6 socket, so reset flags */
			inp6->inp_vflag |= INP_IPV4;
			inp6->inp_vflag &= ~INP_IPV6;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			{
				struct sockaddr_in6 *sin6_p;

				sin6_p = (struct sockaddr_in6 *)addr;

				if (IN6_IS_ADDR_UNSPECIFIED(&sin6_p->sin6_addr)) {
					inp6->inp_vflag |= INP_IPV4;
				}
#ifdef INET
				if (IN6_IS_ADDR_V4MAPPED(&sin6_p->sin6_addr)) {
					struct sockaddr_in sin;

					in6_sin6_2_sin(&sin, sin6_p);
					inp6->inp_vflag |= INP_IPV4;
					inp6->inp_vflag &= ~INP_IPV6;
					error = sctp_inpcb_bind(so, (struct sockaddr *)&sin, NULL, p);
					return (error);
				}
#endif
				break;
			}
#endif
		default:
			break;
		}
	} else if (addr != NULL) {
		struct sockaddr_in6 *sin6_p;

		/* IPV6_V6ONLY socket */
#ifdef INET
		if (addr->sa_family == AF_INET) {
			/* can't bind v4 addr to v6 only socket! */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
#endif
		sin6_p = (struct sockaddr_in6 *)addr;

		if (IN6_IS_ADDR_V4MAPPED(&sin6_p->sin6_addr)) {
			/* can't bind v4-mapped addrs either! */
			/* NOTE: we don't support SIIT */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
	}
	error = sctp_inpcb_bind(so, addr, NULL, p);
	return (error);
}


static void
sctp6_close(struct socket *so)
{
	sctp_close(so);
}

/* This could be made common with sctp_detach() since they are identical */

static
int
sctp6_disconnect(struct socket *so)
{
	return (sctp_disconnect(so));
}


int
sctp_sendm(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *p);


static int
sctp6_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *p)
{
	struct sctp_inpcb *inp;
	struct in6pcb *inp6;

#ifdef INET
	struct sockaddr_in6 *sin6;
#endif				/* INET */
	/* No SPL needed since sctp_output does this */

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		if (control) {
			SCTP_RELEASE_PKT(control);
			control = NULL;
		}
		SCTP_RELEASE_PKT(m);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}
	inp6 = (struct in6pcb *)inp;
	/*
	 * For the TCP model we may get a NULL addr, if we are a connected
	 * socket thats ok.
	 */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) &&
	    (addr == NULL)) {
		goto connected_type;
	}
	if (addr == NULL) {
		SCTP_RELEASE_PKT(m);
		if (control) {
			SCTP_RELEASE_PKT(control);
			control = NULL;
		}
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EDESTADDRREQ);
		return (EDESTADDRREQ);
	}
#ifdef INET
	sin6 = (struct sockaddr_in6 *)addr;
	if (SCTP_IPV6_V6ONLY(inp6)) {
		/*
		 * if IPV6_V6ONLY flag, we discard datagrams destined to a
		 * v4 addr or v4-mapped addr
		 */
		if (addr->sa_family == AF_INET) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
	}
	if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		struct sockaddr_in sin;

		/* convert v4-mapped into v4 addr and send */
		in6_sin6_2_sin(&sin, sin6);
		return (sctp_sendm(so, flags, m, (struct sockaddr *)&sin, control, p));
	}
#endif				/* INET */
connected_type:
	/* now what about control */
	if (control) {
		if (inp->control) {
			SCTP_PRINTF("huh? control set?\n");
			SCTP_RELEASE_PKT(inp->control);
			inp->control = NULL;
		}
		inp->control = control;
	}
	/* Place the data */
	if (inp->pkt) {
		SCTP_BUF_NEXT(inp->pkt_last) = m;
		inp->pkt_last = m;
	} else {
		inp->pkt_last = inp->pkt = m;
	}
	if (
	/* FreeBSD and MacOSX uses a flag passed */
	    ((flags & PRUS_MORETOCOME) == 0)
	    ) {
		/*
		 * note with the current version this code will only be used
		 * by OpenBSD, NetBSD and FreeBSD have methods for
		 * re-defining sosend() to use sctp_sosend().  One can
		 * optionaly switch back to this code (by changing back the
		 * defininitions but this is not advisable.
		 */
		int ret;

		ret = sctp_output(inp, inp->pkt, addr, inp->control, p, flags);
		inp->pkt = NULL;
		inp->control = NULL;
		return (ret);
	} else {
		return (0);
	}
}

static int
sctp6_connect(struct socket *so, struct sockaddr *addr, struct thread *p)
{
	uint32_t vrf_id;
	int error = 0;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
#ifdef INET
	struct in6pcb *inp6;
	struct sockaddr_in6 *sin6;
	union sctp_sockstore store;
#endif

#ifdef INET
	inp6 = (struct in6pcb *)so->so_pcb;
#endif
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ECONNRESET);
		return (ECONNRESET);	/* I made the same as TCP since we are
					 * not setup? */
	}
	if (addr == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}
	switch (addr->sa_family) {
#ifdef INET
	case AF_INET:
		if (addr->sa_len != sizeof(struct sockaddr_in)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (addr->sa_len != sizeof(struct sockaddr_in6)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
		break;
#endif
	default:
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}

	vrf_id = inp->def_vrf_id;
	SCTP_ASOC_CREATE_LOCK(inp);
	SCTP_INP_RLOCK(inp);
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) ==
	    SCTP_PCB_FLAGS_UNBOUND) {
		/* Bind a ephemeral port */
		SCTP_INP_RUNLOCK(inp);
		error = sctp6_bind(so, NULL, p);
		if (error) {
			SCTP_ASOC_CREATE_UNLOCK(inp);

			return (error);
		}
		SCTP_INP_RLOCK(inp);
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* We are already connected AND the TCP model */
		SCTP_INP_RUNLOCK(inp);
		SCTP_ASOC_CREATE_UNLOCK(inp);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EADDRINUSE);
		return (EADDRINUSE);
	}
#ifdef INET
	sin6 = (struct sockaddr_in6 *)addr;
	if (SCTP_IPV6_V6ONLY(inp6)) {
		/*
		 * if IPV6_V6ONLY flag, ignore connections destined to a v4
		 * addr or v4-mapped addr
		 */
		if (addr->sa_family == AF_INET) {
			SCTP_INP_RUNLOCK(inp);
			SCTP_ASOC_CREATE_UNLOCK(inp);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			SCTP_INP_RUNLOCK(inp);
			SCTP_ASOC_CREATE_UNLOCK(inp);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
			return (EINVAL);
		}
	}
	if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		/* convert v4-mapped into v4 addr */
		in6_sin6_2_sin(&store.sin, sin6);
		addr = &store.sa;
	}
#endif				/* INET */
	/* Now do we connect? */
	if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		if (stcb) {
			SCTP_TCB_LOCK(stcb);
		}
		SCTP_INP_RUNLOCK(inp);
	} else {
		SCTP_INP_RUNLOCK(inp);
		SCTP_INP_WLOCK(inp);
		SCTP_INP_INCR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
		stcb = sctp_findassociation_ep_addr(&inp, addr, NULL, NULL, NULL);
		if (stcb == NULL) {
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		}
	}

	if (stcb != NULL) {
		/* Already have or am bring up an association */
		SCTP_ASOC_CREATE_UNLOCK(inp);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EALREADY);
		return (EALREADY);
	}
	/* We are GOOD to go */
	stcb = sctp_aloc_assoc(inp, addr, &error, 0, vrf_id,
	    inp->sctp_ep.pre_open_stream_count,
	    inp->sctp_ep.port, p);
	SCTP_ASOC_CREATE_UNLOCK(inp);
	if (stcb == NULL) {
		/* Gak! no memory */
		return (error);
	}
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
		/* Set the connected flag so we can queue data */
		soisconnecting(so);
	}
	SCTP_SET_STATE(stcb, SCTP_STATE_COOKIE_WAIT);
	(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);

	/* initialize authentication parameters for the assoc */
	sctp_initialize_auth_params(inp, stcb);

	sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
	SCTP_TCB_UNLOCK(stcb);
	return (error);
}

static int
sctp6_getaddr(struct socket *so, struct sockaddr **addr)
{
	struct sockaddr_in6 *sin6;
	struct sctp_inpcb *inp;
	uint32_t vrf_id;
	struct sctp_ifa *sctp_ifa;

	int error;

	/*
	 * Do the malloc first in case it blocks.
	 */
	SCTP_MALLOC_SONAME(sin6, struct sockaddr_in6 *, sizeof(*sin6));
	if (sin6 == NULL)
		return (ENOMEM);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_FREE_SONAME(sin6);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ECONNRESET);
		return (ECONNRESET);
	}
	SCTP_INP_RLOCK(inp);
	sin6->sin6_port = inp->sctp_lport;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/* For the bound all case you get back 0 */
		if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
			struct sctp_tcb *stcb;
			struct sockaddr_in6 *sin_a6;
			struct sctp_nets *net;
			int fnd;

			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb == NULL) {
				SCTP_INP_RUNLOCK(inp);
				SCTP_FREE_SONAME(sin6);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ENOENT);
				return (ENOENT);
			}
			fnd = 0;
			sin_a6 = NULL;
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				sin_a6 = (struct sockaddr_in6 *)&net->ro._l_addr;
				if (sin_a6 == NULL)
					/* this will make coverity happy */
					continue;

				if (sin_a6->sin6_family == AF_INET6) {
					fnd = 1;
					break;
				}
			}
			if ((!fnd) || (sin_a6 == NULL)) {
				/* punt */
				SCTP_INP_RUNLOCK(inp);
				SCTP_FREE_SONAME(sin6);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ENOENT);
				return (ENOENT);
			}
			vrf_id = inp->def_vrf_id;
			sctp_ifa = sctp_source_address_selection(inp, stcb, (sctp_route_t *)&net->ro, net, 0, vrf_id);
			if (sctp_ifa) {
				sin6->sin6_addr = sctp_ifa->address.sin6.sin6_addr;
			}
		} else {
			/* For the bound all case you get back 0 */
			memset(&sin6->sin6_addr, 0, sizeof(sin6->sin6_addr));
		}
	} else {
		/* Take the first IPv6 address in the list */
		struct sctp_laddr *laddr;
		int fnd = 0;

		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa->address.sa.sa_family == AF_INET6) {
				struct sockaddr_in6 *sin_a;

				sin_a = &laddr->ifa->address.sin6;
				sin6->sin6_addr = sin_a->sin6_addr;
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
			SCTP_FREE_SONAME(sin6);
			SCTP_INP_RUNLOCK(inp);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ENOENT);
			return (ENOENT);
		}
	}
	SCTP_INP_RUNLOCK(inp);
	/* Scoping things for v6 */
	if ((error = sa6_recoverscope(sin6)) != 0) {
		SCTP_FREE_SONAME(sin6);
		return (error);
	}
	(*addr) = (struct sockaddr *)sin6;
	return (0);
}

static int
sctp6_peeraddr(struct socket *so, struct sockaddr **addr)
{
	struct sockaddr_in6 *sin6;
	int fnd;
	struct sockaddr_in6 *sin_a6;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;
	int error;

	/* Do the malloc first in case it blocks. */
	SCTP_MALLOC_SONAME(sin6, struct sockaddr_in6 *, sizeof *sin6);
	if (sin6 == NULL)
		return (ENOMEM);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);

	inp = (struct sctp_inpcb *)so->so_pcb;
	if ((inp == NULL) ||
	    ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) == 0)) {
		/* UDP type and listeners will drop out here */
		SCTP_FREE_SONAME(sin6);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ENOTCONN);
		return (ENOTCONN);
	}
	SCTP_INP_RLOCK(inp);
	stcb = LIST_FIRST(&inp->sctp_asoc_list);
	if (stcb) {
		SCTP_TCB_LOCK(stcb);
	}
	SCTP_INP_RUNLOCK(inp);
	if (stcb == NULL) {
		SCTP_FREE_SONAME(sin6);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ECONNRESET);
		return (ECONNRESET);
	}
	fnd = 0;
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		sin_a6 = (struct sockaddr_in6 *)&net->ro._l_addr;
		if (sin_a6->sin6_family == AF_INET6) {
			fnd = 1;
			sin6->sin6_port = stcb->rport;
			sin6->sin6_addr = sin_a6->sin6_addr;
			break;
		}
	}
	SCTP_TCB_UNLOCK(stcb);
	if (!fnd) {
		/* No IPv4 address */
		SCTP_FREE_SONAME(sin6);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, ENOENT);
		return (ENOENT);
	}
	if ((error = sa6_recoverscope(sin6)) != 0) {
		SCTP_FREE_SONAME(sin6);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, error);
		return (error);
	}
	*addr = (struct sockaddr *)sin6;
	return (0);
}

static int
sctp6_in6getaddr(struct socket *so, struct sockaddr **nam)
{
	struct in6pcb *inp6 = sotoin6pcb(so);
	int error;

	if (inp6 == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}

	/* allow v6 addresses precedence */
	error = sctp6_getaddr(so, nam);
#ifdef INET
	if (error) {
		struct sockaddr_in6 *sin6;

		/* try v4 next if v6 failed */
		error = sctp_ingetaddr(so, nam);
		if (error) {
			return (error);
		}
		SCTP_MALLOC_SONAME(sin6, struct sockaddr_in6 *, sizeof *sin6);
		if (sin6 == NULL) {
			SCTP_FREE_SONAME(*nam);
			return (ENOMEM);
		}
		in6_sin_2_v4mapsin6((struct sockaddr_in *)*nam, sin6);
		SCTP_FREE_SONAME(*nam);
		*nam = (struct sockaddr *)sin6;
	}
#endif
	return (error);
}


static int
sctp6_getpeeraddr(struct socket *so, struct sockaddr **nam)
{
	struct in6pcb *inp6 = sotoin6pcb(so);
	int error;

	if (inp6 == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP6_USRREQ, EINVAL);
		return (EINVAL);
	}

	/* allow v6 addresses precedence */
	error = sctp6_peeraddr(so, nam);
#ifdef INET
	if (error) {
		struct sockaddr_in6 *sin6;

		/* try v4 next if v6 failed */
		error = sctp_peeraddr(so, nam);
		if (error) {
			return (error);
		}
		SCTP_MALLOC_SONAME(sin6, struct sockaddr_in6 *, sizeof *sin6);
		if (sin6 == NULL) {
			SCTP_FREE_SONAME(*nam);
			return (ENOMEM);
		}
		in6_sin_2_v4mapsin6((struct sockaddr_in *)*nam, sin6);
		SCTP_FREE_SONAME(*nam);
		*nam = (struct sockaddr *)sin6;
	}
#endif
	return (error);
}

struct pr_usrreqs sctp6_usrreqs = {
	.pru_abort = sctp6_abort,
	.pru_accept = sctp_accept,
	.pru_attach = sctp6_attach,
	.pru_bind = sctp6_bind,
	.pru_connect = sctp6_connect,
	.pru_control = in6_control,
	.pru_close = sctp6_close,
	.pru_detach = sctp6_close,
	.pru_sopoll = sopoll_generic,
	.pru_flush = sctp_flush,
	.pru_disconnect = sctp6_disconnect,
	.pru_listen = sctp_listen,
	.pru_peeraddr = sctp6_getpeeraddr,
	.pru_send = sctp6_send,
	.pru_shutdown = sctp_shutdown,
	.pru_sockaddr = sctp6_in6getaddr,
	.pru_sosend = sctp_sosend,
	.pru_soreceive = sctp_soreceive
};

#endif
