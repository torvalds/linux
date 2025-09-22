/* $OpenBSD: pfkeyv2.c,v 1.272 2025/09/17 02:50:11 jsg Exp $ */

/*
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

/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Craig Metz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any contributors
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
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/mutex.h>

#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>
#include <net/radix.h>
#include <netinet/ip_ah.h>
#include <netinet/ip_esp.h>
#include <netinet/ip_ipcomp.h>
#include <crypto/blf.h>

#if NPF > 0
#include <net/if.h>
#include <net/pfvar.h>
#endif

#define	PFKEYSNDQ	8192
#define	PFKEYRCVQ	8192

static const struct sadb_alg ealgs[] = {
	{ SADB_EALG_NULL, 0, 0, 0 },
	{ SADB_EALG_3DESCBC, 64, 192, 192 },
	{ SADB_X_EALG_BLF, 64, 40, BLF_MAXKEYLEN * 8},
	{ SADB_X_EALG_CAST, 64, 40, 128},
	{ SADB_X_EALG_AES, 128, 128, 256},
	{ SADB_X_EALG_AESCTR, 128, 128 + 32, 256 + 32}
};

static const struct sadb_alg aalgs[] = {
	{ SADB_AALG_SHA1HMAC, 0, 160, 160 },
	{ SADB_AALG_MD5HMAC, 0, 128, 128 },
	{ SADB_X_AALG_RIPEMD160HMAC, 0, 160, 160 },
	{ SADB_X_AALG_SHA2_256, 0, 256, 256 },
	{ SADB_X_AALG_SHA2_384, 0, 384, 384 },
	{ SADB_X_AALG_SHA2_512, 0, 512, 512 }
};

static const struct sadb_alg calgs[] = {
	{ SADB_X_CALG_DEFLATE, 0, 0, 0}
};

struct pool pkpcb_pool;
#define PFKEY_MSG_MAXSZ 4096
const struct sockaddr pfkey_addr = { 2, PF_KEY, };
const struct domain pfkeydomain;

/*
 * pfkey PCB
 *
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	a	atomic operations
 *	l	pkptable's lock
 *	s	socket lock
 */
struct pkpcb {
	struct socket		*kcb_socket;	/* [I] associated socket */

	TAILQ_ENTRY(pkpcb)	kcb_list;	/* [l] */
	int			kcb_flags;	/* [s] */
	uint32_t		kcb_reg;	/* [s] Inc if SATYPE_MAX > 31 */
	uint32_t		kcb_pid;	/* [I] */
	unsigned int		kcb_rdomain;	/* [I] routing domain */
};
#define sotokeycb(so)		((struct pkpcb *)(so)->so_pcb)
#define keylock(kp)		solock((kp)->kcb_socket)
#define keyunlock(kp)		sounlock((kp)->kcb_socket)


struct dump_state {
	struct sadb_msg *sadb_msg;
	struct socket *socket;
};

struct pkptable {
	TAILQ_HEAD(, pkpcb)	pkp_list;
	struct rwlock		pkp_lk;
};

struct pkptable pkptable;
struct mutex pfkeyv2_mtx = MUTEX_INITIALIZER(IPL_MPFLOOR);
static uint32_t pfkeyv2_seq = 1;
static int nregistered = 0;
static int npromisc = 0;

void pfkey_init(void);

int pfkeyv2_attach(struct socket *, int, int);
int pfkeyv2_detach(struct socket *);
int pfkeyv2_disconnect(struct socket *);
int pfkeyv2_shutdown(struct socket *);
int pfkeyv2_send(struct socket *, struct mbuf *, struct mbuf *,
    struct mbuf *);
int pfkeyv2_sockaddr(struct socket *, struct mbuf *);
int pfkeyv2_peeraddr(struct socket *, struct mbuf *);
int pfkeyv2_output(struct mbuf *, struct socket *);
int pfkey_sendup(struct pkpcb *, struct mbuf *, int);
int pfkeyv2_sa_flush(struct tdb *, void *, int);
int pfkeyv2_policy_flush(struct ipsec_policy *, void *, unsigned int);
int pfkeyv2_sysctl_policydumper(struct ipsec_policy *, void *, unsigned int);

/*
 * Wrapper around m_devget(); copy data from contiguous buffer to mbuf
 * chain.
 */
int
pfdatatopacket(void *data, int len, struct mbuf **packet)
{
	if (!(*packet = m_devget(data, len, 0)))
		return (ENOMEM);

	/* Make sure, all data gets zeroized on free */
	(*packet)->m_flags |= M_ZEROIZE;

	return (0);
}

const struct pr_usrreqs pfkeyv2_usrreqs = {
	.pru_attach	= pfkeyv2_attach,
	.pru_detach	= pfkeyv2_detach,
	.pru_disconnect	= pfkeyv2_disconnect,
	.pru_shutdown	= pfkeyv2_shutdown,
	.pru_send	= pfkeyv2_send,
	.pru_sockaddr	= pfkeyv2_sockaddr,
	.pru_peeraddr	= pfkeyv2_peeraddr,
};

const struct protosw pfkeysw[] = {
{
  .pr_type      = SOCK_RAW,
  .pr_domain    = &pfkeydomain,
  .pr_protocol  = PF_KEY_V2,
  .pr_flags     = PR_ATOMIC | PR_ADDR,
  .pr_usrreqs   = &pfkeyv2_usrreqs,
  .pr_sysctl    = pfkeyv2_sysctl,
}
};

const struct domain pfkeydomain = {
  .dom_family = PF_KEY,
  .dom_name = "pfkey",
  .dom_init = pfkey_init,
  .dom_protosw = pfkeysw,
  .dom_protoswNPROTOSW = &pfkeysw[nitems(pfkeysw)],
};

void
pfkey_init(void)
{
	rn_init(sizeof(struct sockaddr_encap));
	rw_init(&pkptable.pkp_lk, "pfkey");
	TAILQ_INIT(&pkptable.pkp_list);
	pool_init(&pkpcb_pool, sizeof(struct pkpcb), 0,
	    IPL_SOFTNET, PR_WAITOK, "pkpcb", NULL);
	pool_init(&ipsec_policy_pool, sizeof(struct ipsec_policy), 0,
	    IPL_SOFTNET, 0, "ipsec policy", NULL);
	pool_init(&ipsec_acquire_pool, sizeof(struct ipsec_acquire), 0,
	    IPL_SOFTNET, 0, "ipsec acquire", NULL);
}


/*
 * Attach a new PF_KEYv2 socket.
 */
int
pfkeyv2_attach(struct socket *so, int proto, int wait)
{
	struct pkpcb *kp;
	int error;

	if ((so->so_state & SS_PRIV) == 0)
		return EACCES;

	error = soreserve(so, PFKEYSNDQ, PFKEYRCVQ);
	if (error)
		return (error);

	kp = pool_get(&pkpcb_pool, (wait == M_WAIT ? PR_WAITOK : PR_NOWAIT) |
	    PR_ZERO);
	if (kp == NULL)
		return (ENOBUFS);
	so->so_pcb = kp;
	kp->kcb_socket = so;
	kp->kcb_pid = curproc->p_p->ps_pid;
	kp->kcb_rdomain = rtable_l2(curproc->p_p->ps_rtableid);

	so->so_options |= SO_USELOOPBACK;
	soisconnected(so);

	rw_enter(&pkptable.pkp_lk, RW_WRITE);
	TAILQ_INSERT_TAIL(&pkptable.pkp_list, kp, kcb_list);
	rw_exit(&pkptable.pkp_lk);

	return (0);
}

/*
 * Close a PF_KEYv2 socket.
 */
int
pfkeyv2_detach(struct socket *so)
{
	struct pkpcb *kp;

	soassertlocked(so);

	kp = sotokeycb(so);
	if (kp == NULL)
		return ENOTCONN;

	if (kp->kcb_flags &
	    (PFKEYV2_SOCKETFLAGS_REGISTERED|PFKEYV2_SOCKETFLAGS_PROMISC)) {
		mtx_enter(&pfkeyv2_mtx);
		if (kp->kcb_flags & PFKEYV2_SOCKETFLAGS_REGISTERED)
			nregistered--;

		if (kp->kcb_flags & PFKEYV2_SOCKETFLAGS_PROMISC)
			npromisc--;
		mtx_leave(&pfkeyv2_mtx);
	}

	rw_enter(&pkptable.pkp_lk, RW_WRITE);
	TAILQ_REMOVE(&pkptable.pkp_list, kp, kcb_list);
	rw_exit(&pkptable.pkp_lk);

	so->so_pcb = NULL;
	KASSERT((so->so_state & SS_NOFDREF) == 0);
	pool_put(&pkpcb_pool, kp);

	return (0);
}

int
pfkeyv2_disconnect(struct socket *so)
{
	soisdisconnected(so);
	return (0);
}

int
pfkeyv2_shutdown(struct socket *so)
{
	socantsendmore(so);
	return (0);
}

int
pfkeyv2_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	int error;

	soassertlocked(so);

	if (control && control->m_len) {
		error = EOPNOTSUPP;
		goto out;
	}
	
	if (nam) {
		error = EISCONN;
		goto out;
	}

	error = pfkeyv2_output(m, so);
	m = NULL;

out:
	m_freem(control);
	m_freem(m);

	return (error);
}

int
pfkeyv2_sockaddr(struct socket *so, struct mbuf *nam)
{
	return (EINVAL);
}

int
pfkeyv2_peeraddr(struct socket *so, struct mbuf *nam)
{
	/* minimal support, just implement a fake peer address */
	bcopy(&pfkey_addr, mtod(nam, caddr_t), pfkey_addr.sa_len);
	nam->m_len = pfkey_addr.sa_len;
	return (0);
}

int
pfkeyv2_output(struct mbuf *mbuf, struct socket *so)
{
	void *message;
	int error = 0;

#ifdef DIAGNOSTIC
	if (!mbuf || !(mbuf->m_flags & M_PKTHDR)) {
		error = EINVAL;
		goto ret;
	}
#endif /* DIAGNOSTIC */

	if (mbuf->m_pkthdr.len > PFKEY_MSG_MAXSZ) {
		error = EMSGSIZE;
		goto ret;
	}

	if (!(message = malloc((unsigned long) mbuf->m_pkthdr.len,
	    M_PFKEY, M_DONTWAIT))) {
		error = ENOMEM;
		goto ret;
	}

	m_copydata(mbuf, 0, mbuf->m_pkthdr.len, message);

	/*
	 * The socket can't be closed concurrently because the file
	 * descriptor reference is still held.
	 */

	sounlock(so);
	error = pfkeyv2_dosend(so, message, mbuf->m_pkthdr.len);
	solock(so);

ret:
	m_freem(mbuf);
	return (error);
}

int
pfkey_sendup(struct pkpcb *kp, struct mbuf *m0, int more)
{
	struct socket *so = kp->kcb_socket;
	struct mbuf *m;
	int ret;

	if (more) {
		if (!(m = m_dup_pkt(m0, 0, M_DONTWAIT)))
			return (ENOMEM);
	} else
		m = m0;

	mtx_enter(&so->so_rcv.sb_mtx);
	ret = sbappendaddr(&so->so_rcv, &pfkey_addr, m, NULL);
	mtx_leave(&so->so_rcv.sb_mtx);

	if (ret == 0) {
		m_freem(m);
		return (ENOBUFS);
	}

	sorwakeup(so);
	return (0);
}

/*
 * Send a PFKEYv2 message, possibly to many receivers, based on the
 * satype of the socket (which is set by the REGISTER message), and the
 * third argument.
 */
int
pfkeyv2_sendmessage(void **headers, int mode, struct socket *so,
    u_int8_t satype, int count, u_int rdomain)
{
	int i, j, rval;
	void *p, *buffer = NULL;
	struct mbuf *packet;
	struct pkpcb *kp;
	struct sadb_msg *smsg;

	/* Find out how much space we'll need... */
	j = sizeof(struct sadb_msg);

	for (i = 1; i <= SADB_EXT_MAX; i++)
		if (headers[i])
			j += ((struct sadb_ext *)headers[i])->sadb_ext_len *
			    sizeof(uint64_t);

	/* ...and allocate it */
	if (!(buffer = malloc(j + sizeof(struct sadb_msg), M_PFKEY,
	    M_NOWAIT))) {
		rval = ENOMEM;
		goto ret;
	}

	p = buffer + sizeof(struct sadb_msg);
	bcopy(headers[0], p, sizeof(struct sadb_msg));
	((struct sadb_msg *) p)->sadb_msg_len = j / sizeof(uint64_t);
	p += sizeof(struct sadb_msg);

	/* Copy payloads in the packet */
	for (i = 1; i <= SADB_EXT_MAX; i++)
		if (headers[i]) {
			((struct sadb_ext *) headers[i])->sadb_ext_type = i;
			bcopy(headers[i], p, EXTLEN(headers[i]));
			p += EXTLEN(headers[i]);
		}

	if ((rval = pfdatatopacket(buffer + sizeof(struct sadb_msg),
	    j, &packet)) != 0)
		goto ret;

	switch (mode) {
	case PFKEYV2_SENDMESSAGE_UNICAST:
		/*
		 * Send message to the specified socket, plus all
		 * promiscuous listeners.
		 */
		pfkey_sendup(sotokeycb(so), packet, 0);

		/*
		 * Promiscuous messages contain the original message
		 * encapsulated in another sadb_msg header.
		 */
		bzero(buffer, sizeof(struct sadb_msg));
		smsg = (struct sadb_msg *) buffer;
		smsg->sadb_msg_version = PF_KEY_V2;
		smsg->sadb_msg_type = SADB_X_PROMISC;
		smsg->sadb_msg_len = (sizeof(struct sadb_msg) + j) /
		    sizeof(uint64_t);
		smsg->sadb_msg_seq = 0;

		/* Copy to mbuf chain */
		if ((rval = pfdatatopacket(buffer, sizeof(struct sadb_msg) + j,
		    &packet)) != 0)
			goto ret;

		/*
		 * Search for promiscuous listeners, skipping the
		 * original destination.
		 */
		rw_enter_read(&pkptable.pkp_lk);
		TAILQ_FOREACH(kp, &pkptable.pkp_list, kcb_list) {
			if (kp->kcb_socket == so || kp->kcb_rdomain != rdomain)
				continue;

			if (kp->kcb_flags & PFKEYV2_SOCKETFLAGS_PROMISC)
				pfkey_sendup(kp, packet, 1);
		}
		rw_exit_read(&pkptable.pkp_lk);
		m_freem(packet);
		break;

	case PFKEYV2_SENDMESSAGE_REGISTERED:
		/*
		 * Send the message to all registered sockets that match
		 * the specified satype (e.g., all IPSEC-ESP negotiators)
		 */
		rw_enter_read(&pkptable.pkp_lk);
		TAILQ_FOREACH(kp, &pkptable.pkp_list, kcb_list) {
			if (kp->kcb_rdomain != rdomain)
				continue;

			if (kp->kcb_flags & PFKEYV2_SOCKETFLAGS_REGISTERED) {
				if (!satype) {
					/* Just send to everyone registered */
					pfkey_sendup(kp, packet, 1);
				} else {
					uint32_t kcb_reg;

					kcb_reg = READ_ONCE(kp->kcb_reg);
					/* Check for specified satype */
					if ((1 << satype) & kcb_reg)
						pfkey_sendup(kp, packet, 1);
				}
			}
		}
		rw_exit_read(&pkptable.pkp_lk);
		/* Free last/original copy of the packet */
		m_freem(packet);

		/* Encapsulate the original message "inside" an sadb_msg header */
		bzero(buffer, sizeof(struct sadb_msg));
		smsg = (struct sadb_msg *) buffer;
		smsg->sadb_msg_version = PF_KEY_V2;
		smsg->sadb_msg_type = SADB_X_PROMISC;
		smsg->sadb_msg_len = (sizeof(struct sadb_msg) + j) /
		    sizeof(uint64_t);
		smsg->sadb_msg_seq = 0;

		/* Convert to mbuf chain */
		if ((rval = pfdatatopacket(buffer, sizeof(struct sadb_msg) + j,
		    &packet)) != 0)
			goto ret;

		/* Send to all registered promiscuous listeners */
		rw_enter_read(&pkptable.pkp_lk);
		TAILQ_FOREACH(kp, &pkptable.pkp_list, kcb_list) {
			int flags = READ_ONCE(kp->kcb_flags);

			if (kp->kcb_rdomain != rdomain)
				continue;

			if ((flags & PFKEYV2_SOCKETFLAGS_PROMISC) &&
			    !(flags & PFKEYV2_SOCKETFLAGS_REGISTERED))
				pfkey_sendup(kp, packet, 1);
		}
		rw_exit_read(&pkptable.pkp_lk);
		m_freem(packet);
		break;

	case PFKEYV2_SENDMESSAGE_BROADCAST:
		/* Send message to all sockets */
		rw_enter_read(&pkptable.pkp_lk);
		TAILQ_FOREACH(kp, &pkptable.pkp_list, kcb_list) {
			if (kp->kcb_rdomain != rdomain)
				continue;

			pfkey_sendup(kp, packet, 1);
		}
		rw_exit_read(&pkptable.pkp_lk);
		m_freem(packet);
		break;
	}

ret:
	if (buffer != NULL) {
		explicit_bzero(buffer, j + sizeof(struct sadb_msg));
		free(buffer, M_PFKEY, j + sizeof(struct sadb_msg));
	}

	return (rval);
}

/*
 * Get SPD information for an ACQUIRE. We setup the message such that
 * the SRC/DST payloads are relative to us (regardless of whether the
 * SPD rule was for incoming or outgoing packets).
 */
int
pfkeyv2_policy(struct ipsec_acquire *ipa, void **headers, void **buffer,
    int *bufferlen)
{
	union sockaddr_union sunion;
	struct sadb_protocol *sp;
	int rval, i, dir;
	void *p;

	/* Find out how big a buffer we need */
	i = 4 * sizeof(struct sadb_address) + sizeof(struct sadb_protocol);
	bzero(&sunion, sizeof(union sockaddr_union));

	switch (ipa->ipa_info.sen_type) {
	case SENT_IP4:
		i += 4 * PADUP(sizeof(struct sockaddr_in));
		sunion.sa.sa_family = AF_INET;
		sunion.sa.sa_len = sizeof(struct sockaddr_in);
		dir = ipa->ipa_info.sen_direction;
		break;

#ifdef INET6
	case SENT_IP6:
		i += 4 * PADUP(sizeof(struct sockaddr_in6));
		sunion.sa.sa_family = AF_INET6;
		sunion.sa.sa_len = sizeof(struct sockaddr_in6);
		dir = ipa->ipa_info.sen_ip6_direction;
		break;
#endif /* INET6 */

	default:
		return (EINVAL);
	}

	if (!(p = malloc(i, M_PFKEY, M_NOWAIT | M_ZERO))) {
		rval = ENOMEM;
		goto ret;
	} else {
		*buffer = p;
		*bufferlen = i;
	}

	if (dir == IPSP_DIRECTION_OUT)
		headers[SADB_X_EXT_SRC_FLOW] = p;
	else
		headers[SADB_X_EXT_DST_FLOW] = p;
	switch (sunion.sa.sa_family) {
	case AF_INET:
		sunion.sin.sin_addr = ipa->ipa_info.sen_ip_src;
		sunion.sin.sin_port = ipa->ipa_info.sen_sport;
		break;

#ifdef INET6
	case AF_INET6:
		sunion.sin6.sin6_addr = ipa->ipa_info.sen_ip6_src;
		sunion.sin6.sin6_port = ipa->ipa_info.sen_ip6_sport;
		break;
#endif /* INET6 */
	}
	export_address(&p, &sunion.sa);

	if (dir == IPSP_DIRECTION_OUT)
		headers[SADB_X_EXT_SRC_MASK] = p;
	else
		headers[SADB_X_EXT_DST_MASK] = p;
	switch (sunion.sa.sa_family) {
	case AF_INET:
		sunion.sin.sin_addr = ipa->ipa_mask.sen_ip_src;
		sunion.sin.sin_port = ipa->ipa_mask.sen_sport;
		break;

#ifdef INET6
	case AF_INET6:
		sunion.sin6.sin6_addr = ipa->ipa_mask.sen_ip6_src;
		sunion.sin6.sin6_port = ipa->ipa_mask.sen_ip6_sport;
		break;
#endif /* INET6 */
	}
	export_address(&p, &sunion.sa);

	if (dir == IPSP_DIRECTION_OUT)
		headers[SADB_X_EXT_DST_FLOW] = p;
	else
		headers[SADB_X_EXT_SRC_FLOW] = p;
	switch (sunion.sa.sa_family) {
	case AF_INET:
		sunion.sin.sin_addr = ipa->ipa_info.sen_ip_dst;
		sunion.sin.sin_port = ipa->ipa_info.sen_dport;
		break;

#ifdef INET6
	case AF_INET6:
		sunion.sin6.sin6_addr = ipa->ipa_info.sen_ip6_dst;
		sunion.sin6.sin6_port = ipa->ipa_info.sen_ip6_dport;
		break;
#endif /* INET6 */
	}
	export_address(&p, &sunion.sa);

	if (dir == IPSP_DIRECTION_OUT)
		headers[SADB_X_EXT_DST_MASK] = p;
	else
		headers[SADB_X_EXT_SRC_MASK] = p;
	switch (sunion.sa.sa_family) {
	case AF_INET:
		sunion.sin.sin_addr = ipa->ipa_mask.sen_ip_dst;
		sunion.sin.sin_port = ipa->ipa_mask.sen_dport;
		break;

#ifdef INET6
	case AF_INET6:
		sunion.sin6.sin6_addr = ipa->ipa_mask.sen_ip6_dst;
		sunion.sin6.sin6_port = ipa->ipa_mask.sen_ip6_dport;
		break;
#endif /* INET6 */
	}
	export_address(&p, &sunion.sa);

	headers[SADB_X_EXT_FLOW_TYPE] = p;
	sp = p;
	sp->sadb_protocol_len = sizeof(struct sadb_protocol) /
	    sizeof(u_int64_t);
	switch (sunion.sa.sa_family) {
	case AF_INET:
		if (ipa->ipa_mask.sen_proto)
			sp->sadb_protocol_proto = ipa->ipa_info.sen_proto;
		sp->sadb_protocol_direction = ipa->ipa_info.sen_direction;
		break;

#ifdef INET6
	case AF_INET6:
		if (ipa->ipa_mask.sen_ip6_proto)
			sp->sadb_protocol_proto = ipa->ipa_info.sen_ip6_proto;
		sp->sadb_protocol_direction = ipa->ipa_info.sen_ip6_direction;
		break;
#endif /* INET6 */
	}

	rval = 0;

ret:
	return (rval);
}

/*
 * Get all the information contained in an SA to a PFKEYV2 message.
 */
int
pfkeyv2_get(struct tdb *tdb, void **headers, void **buffer, int *lenp,
    int *lenused)
{
	int rval, i;
	void *p;

	NET_ASSERT_LOCKED();

	/* Find how much space we need */
	i = sizeof(struct sadb_sa) + sizeof(struct sadb_lifetime) +
	    sizeof(struct sadb_x_counter);

	if (tdb->tdb_soft_allocations || tdb->tdb_soft_bytes ||
	    tdb->tdb_soft_timeout || tdb->tdb_soft_first_use)
		i += sizeof(struct sadb_lifetime);

	if (tdb->tdb_exp_allocations || tdb->tdb_exp_bytes ||
	    tdb->tdb_exp_timeout || tdb->tdb_exp_first_use)
		i += sizeof(struct sadb_lifetime);

	if (tdb->tdb_last_used)
		i += sizeof(struct sadb_lifetime);

	i += sizeof(struct sadb_address) + PADUP(tdb->tdb_src.sa.sa_len);
	i += sizeof(struct sadb_address) + PADUP(tdb->tdb_dst.sa.sa_len);

	if (tdb->tdb_ids) {
		i += sizeof(struct sadb_ident) + PADUP(tdb->tdb_ids->id_local->len);
		i += sizeof(struct sadb_ident) + PADUP(tdb->tdb_ids->id_remote->len);
	}

	if (tdb->tdb_amxkey)
		i += sizeof(struct sadb_key) + PADUP(tdb->tdb_amxkeylen);

	if (tdb->tdb_emxkey)
		i += sizeof(struct sadb_key) + PADUP(tdb->tdb_emxkeylen);

	if (tdb->tdb_filter.sen_type) {
		i += 2 * sizeof(struct sadb_protocol);

		/* We'll need four of them: src, src mask, dst, dst mask. */
		switch (tdb->tdb_filter.sen_type) {
		case SENT_IP4:
			i += 4 * PADUP(sizeof(struct sockaddr_in));
			i += 4 * sizeof(struct sadb_address);
			break;
#ifdef INET6
		case SENT_IP6:
			i += 4 * PADUP(sizeof(struct sockaddr_in6));
			i += 4 * sizeof(struct sadb_address);
			break;
#endif /* INET6 */
		default:
			rval = EINVAL;
			goto ret;
		}
	}

	if (tdb->tdb_onext) {
		i += sizeof(struct sadb_sa);
		i += sizeof(struct sadb_address) +
		    PADUP(tdb->tdb_onext->tdb_dst.sa.sa_len);
		i += sizeof(struct sadb_protocol);
	}

	if (tdb->tdb_udpencap_port)
		i += sizeof(struct sadb_x_udpencap);

	i += sizeof(struct sadb_x_replay);

	if (tdb->tdb_mtu > 0)
		i+= sizeof(struct sadb_x_mtu);

	if (tdb->tdb_rdomain != tdb->tdb_rdomain_post)
		i += sizeof(struct sadb_x_rdomain);

#if NPF > 0
	if (tdb->tdb_tag)
		i += sizeof(struct sadb_x_tag) + PADUP(PF_TAG_NAME_SIZE);
	if (tdb->tdb_tap)
		i += sizeof(struct sadb_x_tap);
#endif

	if (ISSET(tdb->tdb_flags, TDBF_IFACE))
		i += sizeof(struct sadb_x_iface);

	if (lenp)
		*lenp = i;

	if (buffer == NULL) {
		rval = 0;
		goto ret;
	}

	if (!(p = malloc(i, M_PFKEY, M_NOWAIT | M_ZERO))) {
		rval = ENOMEM;
		goto ret;
	} else
		*buffer = p;

	headers[SADB_EXT_SA] = p;

	export_sa(&p, tdb);  /* Export SA information (mostly flags) */

	/* Export lifetimes where applicable */
	headers[SADB_EXT_LIFETIME_CURRENT] = p;
	export_lifetime(&p, tdb, PFKEYV2_LIFETIME_CURRENT);

	if (tdb->tdb_soft_allocations || tdb->tdb_soft_bytes ||
	    tdb->tdb_soft_first_use || tdb->tdb_soft_timeout) {
		headers[SADB_EXT_LIFETIME_SOFT] = p;
		export_lifetime(&p, tdb, PFKEYV2_LIFETIME_SOFT);
	}

	if (tdb->tdb_exp_allocations || tdb->tdb_exp_bytes ||
	    tdb->tdb_exp_first_use || tdb->tdb_exp_timeout) {
		headers[SADB_EXT_LIFETIME_HARD] = p;
		export_lifetime(&p, tdb, PFKEYV2_LIFETIME_HARD);
	}

	if (tdb->tdb_last_used) {
		headers[SADB_X_EXT_LIFETIME_LASTUSE] = p;
		export_lifetime(&p, tdb, PFKEYV2_LIFETIME_LASTUSE);
	}

	/* Export TDB source address */
	headers[SADB_EXT_ADDRESS_SRC] = p;
	export_address(&p, &tdb->tdb_src.sa);

	/* Export TDB destination address */
	headers[SADB_EXT_ADDRESS_DST] = p;
	export_address(&p, &tdb->tdb_dst.sa);

	/* Export source/destination identities, if present */
	if (tdb->tdb_ids)
		export_identities(&p, tdb->tdb_ids, tdb->tdb_ids_swapped, headers);

	/* Export authentication key, if present */
	if (tdb->tdb_amxkey) {
		headers[SADB_EXT_KEY_AUTH] = p;
		export_key(&p, tdb, PFKEYV2_AUTHENTICATION_KEY);
	}

	/* Export encryption key, if present */
	if (tdb->tdb_emxkey) {
		headers[SADB_EXT_KEY_ENCRYPT] = p;
		export_key(&p, tdb, PFKEYV2_ENCRYPTION_KEY);
	}

	/* Export flow/filter, if present */
	if (tdb->tdb_filter.sen_type)
		export_flow(&p, IPSP_IPSEC_USE, &tdb->tdb_filter,
		    &tdb->tdb_filtermask, headers);

	if (tdb->tdb_onext) {
		headers[SADB_X_EXT_SA2] = p;
		export_sa(&p, tdb->tdb_onext);
		headers[SADB_X_EXT_DST2] = p;
		export_address(&p, &tdb->tdb_onext->tdb_dst.sa);
		headers[SADB_X_EXT_SATYPE2] = p;
		export_satype(&p, tdb->tdb_onext);
	}

	/* Export UDP encapsulation port, if present */
	if (tdb->tdb_udpencap_port) {
		headers[SADB_X_EXT_UDPENCAP] = p;
		export_udpencap(&p, tdb);
	}

	headers[SADB_X_EXT_REPLAY] = p;
	export_replay(&p, tdb);

	if (tdb->tdb_mtu > 0) {
		headers[SADB_X_EXT_MTU] = p;
		export_mtu(&p, tdb);
	}

	/* Export rdomain switch, if present */
	if (tdb->tdb_rdomain != tdb->tdb_rdomain_post) {
		headers[SADB_X_EXT_RDOMAIN] = p;
		export_rdomain(&p, tdb);
	}

#if NPF > 0
	/* Export tag information, if present */
	if (tdb->tdb_tag) {
		headers[SADB_X_EXT_TAG] = p;
		export_tag(&p, tdb);
	}

	/* Export tap enc(4) device information, if present */
	if (tdb->tdb_tap) {
		headers[SADB_X_EXT_TAP] = p;
		export_tap(&p, tdb);
	}
#endif

	/* Export sec(4) interface information, if present */
	if (ISSET(tdb->tdb_flags, TDBF_IFACE)) {
		headers[SADB_X_EXT_IFACE] = p;
		export_iface(&p, tdb);
	}

	headers[SADB_X_EXT_COUNTER] = p;
	export_counter(&p, tdb);

	if (lenused)
		*lenused = p - *buffer;
	rval = 0;

 ret:
	return (rval);
}

/*
 * Dump a TDB.
 */
int
pfkeyv2_dump_walker(struct tdb *tdb, void *state, int last)
{
	struct dump_state *dump_state = (struct dump_state *) state;
	void *headers[SADB_EXT_MAX+1], *buffer;
	int buflen;
	int rval;

	/* If not satype was specified, dump all TDBs */
	if (!dump_state->sadb_msg->sadb_msg_satype ||
	    (tdb->tdb_satype == dump_state->sadb_msg->sadb_msg_satype)) {
		bzero(headers, sizeof(headers));
		headers[0] = (void *) dump_state->sadb_msg;

		/* Get the information from the TDB to a PFKEYv2 message */
		if ((rval = pfkeyv2_get(tdb, headers, &buffer, &buflen, NULL)) != 0)
			return (rval);

		if (last)
			((struct sadb_msg *)headers[0])->sadb_msg_seq = 0;

		/* Send the message to the specified socket */
		rval = pfkeyv2_sendmessage(headers,
		    PFKEYV2_SENDMESSAGE_UNICAST, dump_state->socket, 0, 0,
		    tdb->tdb_rdomain);

		explicit_bzero(buffer, buflen);
		free(buffer, M_PFKEY, buflen);
		if (rval)
			return (rval);
	}

	return (0);
}

/*
 * Delete an SA.
 */
int
pfkeyv2_sa_flush(struct tdb *tdb, void *satype_vp, int last)
{
	if (!(*((u_int8_t *) satype_vp)) ||
	    tdb->tdb_satype == *((u_int8_t *) satype_vp))
		tdb_delete(tdb);
	return (0);
}

/*
 * Convert between SATYPEs and IPsec protocols, taking into consideration
 * sysctl variables enabling/disabling ESP/AH and the presence of the old
 * IPsec transforms.
 */
int
pfkeyv2_get_proto_alg(u_int8_t satype, u_int8_t *sproto, int *alg)
{
	switch (satype) {
#ifdef IPSEC
	case SADB_SATYPE_AH:
		if (!atomic_load_int(&ah_enable))
			return (EOPNOTSUPP);

		*sproto = IPPROTO_AH;

		if(alg != NULL)
			*alg = satype = XF_AH;

		break;

	case SADB_SATYPE_ESP:
		if (!atomic_load_int(&esp_enable))
			return (EOPNOTSUPP);

		*sproto = IPPROTO_ESP;

		if(alg != NULL)
			*alg = satype = XF_ESP;

		break;

	case SADB_X_SATYPE_IPIP:
		*sproto = IPPROTO_IPIP;

		if (alg != NULL)
			*alg = XF_IP4;

		break;

	case SADB_X_SATYPE_IPCOMP:
		if (!atomic_load_int(&ipcomp_enable))
			return (EOPNOTSUPP);

		*sproto = IPPROTO_IPCOMP;

		if(alg != NULL)
			*alg = satype = XF_IPCOMP;

		break;
#endif /* IPSEC */
#ifdef TCP_SIGNATURE
	case SADB_X_SATYPE_TCPSIGNATURE:
		*sproto = IPPROTO_TCP;

		if (alg != NULL)
			*alg = XF_TCPSIGNATURE;

		break;
#endif /* TCP_SIGNATURE */

	default: /* Nothing else supported */
		return (EOPNOTSUPP);
	}

	return (0);
}

/*
 * Handle all messages from userland to kernel.
 */
int
pfkeyv2_dosend(struct socket *so, void *message, int len)
{
	int i, j, rval = 0, mode = PFKEYV2_SENDMESSAGE_BROADCAST;
	int delflag = 0;
	struct sockaddr_encap encapdst, encapnetmask;
	struct ipsec_policy *ipo;
	struct ipsec_acquire *ipa;
	struct radix_node_head *rnh;
	struct radix_node *rn = NULL;
	struct pkpcb *kp, *bkp;
	void *freeme = NULL, *freeme2 = NULL, *freeme3 = NULL;
	int freeme_sz = 0, freeme2_sz = 0, freeme3_sz = 0;
	void *bckptr = NULL;
	void *headers[SADB_EXT_MAX + 1];
	union sockaddr_union *sunionp;
	struct tdb *sa1 = NULL, *sa2 = NULL;
	struct sadb_msg *smsg;
	struct sadb_spirange *sprng;
	struct sadb_sa *ssa;
	struct sadb_supported *ssup;
	struct sadb_ident *sid, *did;
	struct sadb_x_rdomain *srdomain;
	u_int rdomain = 0;
	int promisc;

	mtx_enter(&pfkeyv2_mtx);
	promisc = npromisc;
	mtx_leave(&pfkeyv2_mtx);

	/* Verify that we received this over a legitimate pfkeyv2 socket */
	bzero(headers, sizeof(headers));

	kp = sotokeycb(so);
	if (!kp) {
		rval = EINVAL;
		goto ret;
	}

	rdomain = kp->kcb_rdomain;

	/* Validate message format */
	if ((rval = pfkeyv2_parsemessage(message, len, headers)) != 0)
		goto ret;

	/* If we have any promiscuous listeners, send them a copy of the message */
	if (promisc) {
		struct mbuf *packet;

		freeme_sz = sizeof(struct sadb_msg) + len;
		if (!(freeme = malloc(freeme_sz, M_PFKEY, M_NOWAIT))) {
			rval = ENOMEM;
			goto ret;
		}

		/* Initialize encapsulating header */
		bzero(freeme, sizeof(struct sadb_msg));
		smsg = (struct sadb_msg *) freeme;
		smsg->sadb_msg_version = PF_KEY_V2;
		smsg->sadb_msg_type = SADB_X_PROMISC;
		smsg->sadb_msg_len = (sizeof(struct sadb_msg) + len) /
		    sizeof(uint64_t);
		smsg->sadb_msg_seq = curproc->p_p->ps_pid;

		bcopy(message, freeme + sizeof(struct sadb_msg), len);

		/* Convert to mbuf chain */
		if ((rval = pfdatatopacket(freeme, freeme_sz, &packet)) != 0)
			goto ret;

		/* Send to all promiscuous listeners */
		rw_enter_read(&pkptable.pkp_lk);
		TAILQ_FOREACH(bkp, &pkptable.pkp_list, kcb_list) {
			if (bkp->kcb_rdomain != kp->kcb_rdomain)
				continue;

			if (bkp->kcb_flags & PFKEYV2_SOCKETFLAGS_PROMISC)
				pfkey_sendup(bkp, packet, 1);
		}
		rw_exit_read(&pkptable.pkp_lk);

		m_freem(packet);

		/* Paranoid */
		explicit_bzero(freeme, freeme_sz);
		free(freeme, M_PFKEY, freeme_sz);
		freeme = NULL;
		freeme_sz = 0;
	}

	/* use specified rdomain */
	srdomain = (struct sadb_x_rdomain *) headers[SADB_X_EXT_RDOMAIN];
	if (srdomain) {
		if (!rtable_exists(srdomain->sadb_x_rdomain_dom1) ||
		    !rtable_exists(srdomain->sadb_x_rdomain_dom2)) {
			rval = EINVAL;
			goto ret;
		}
		rdomain = srdomain->sadb_x_rdomain_dom1;
	}

	smsg = (struct sadb_msg *) headers[0];
	switch (smsg->sadb_msg_type) {
	case SADB_GETSPI:  /* Reserve an SPI */
		sa1 = malloc(sizeof (*sa1), M_PFKEY, M_NOWAIT | M_ZERO);
		if (sa1 == NULL) {
			rval = ENOMEM;
			goto ret;
		}

		sa1->tdb_satype = smsg->sadb_msg_satype;
		if ((rval = pfkeyv2_get_proto_alg(sa1->tdb_satype,
		    &sa1->tdb_sproto, 0)))
			goto ret;

		import_address(&sa1->tdb_src.sa, headers[SADB_EXT_ADDRESS_SRC]);
		import_address(&sa1->tdb_dst.sa, headers[SADB_EXT_ADDRESS_DST]);

		/* Find an unused SA identifier */
		sprng = (struct sadb_spirange *) headers[SADB_EXT_SPIRANGE];
		NET_LOCK();
		sa1->tdb_spi = reserve_spi(rdomain,
		    sprng->sadb_spirange_min, sprng->sadb_spirange_max,
		    &sa1->tdb_src, &sa1->tdb_dst, sa1->tdb_sproto, &rval);
		if (sa1->tdb_spi == 0) {
			NET_UNLOCK();
			goto ret;
		}

		/* Send a message back telling what the SA (the SPI really) is */
		freeme_sz = sizeof(struct sadb_sa);
		if (!(freeme = malloc(freeme_sz, M_PFKEY, M_NOWAIT | M_ZERO))) {
			rval = ENOMEM;
			NET_UNLOCK();
			goto ret;
		}

		headers[SADB_EXT_SPIRANGE] = NULL;
		headers[SADB_EXT_SA] = freeme;
		bckptr = freeme;

		/* We really only care about the SPI, but we'll export the SA */
		export_sa((void **) &bckptr, sa1);
		NET_UNLOCK();
		break;

	case SADB_UPDATE:
		ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
		sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
		    sizeof(struct sadb_address));

		/* Either all or none of the flow must be included */
		if ((headers[SADB_X_EXT_SRC_FLOW] ||
		    headers[SADB_X_EXT_PROTOCOL] ||
		    headers[SADB_X_EXT_FLOW_TYPE] ||
		    headers[SADB_X_EXT_DST_FLOW] ||
		    headers[SADB_X_EXT_SRC_MASK] ||
		    headers[SADB_X_EXT_DST_MASK]) &&
		    !(headers[SADB_X_EXT_SRC_FLOW] &&
		    headers[SADB_X_EXT_PROTOCOL] &&
		    headers[SADB_X_EXT_FLOW_TYPE] &&
		    headers[SADB_X_EXT_DST_FLOW] &&
		    headers[SADB_X_EXT_SRC_MASK] &&
		    headers[SADB_X_EXT_DST_MASK])) {
			rval = EINVAL;
			goto ret;
		}
#ifdef IPSEC
		/* UDP encap has to be enabled and is only supported for ESP */
		if (headers[SADB_X_EXT_UDPENCAP] &&
		    (!atomic_load_int(&udpencap_enable) ||
		    smsg->sadb_msg_satype != SADB_SATYPE_ESP)) {
			rval = EINVAL;
			goto ret;
		}
#endif /* IPSEC */

		/* Find TDB */
		NET_LOCK();
		sa2 = gettdb(rdomain, ssa->sadb_sa_spi, sunionp,
		    SADB_X_GETSPROTO(smsg->sadb_msg_satype));

		/* If there's no such SA, we're done */
		if (sa2 == NULL) {
			rval = ESRCH;
			NET_UNLOCK();
			goto ret;
		}

		/* If this is a reserved SA */
		if (sa2->tdb_flags & TDBF_INVALID) {
			struct tdb *newsa;
			struct ipsecinit ii;
			int alg;

			/* Create new TDB */
			newsa = tdb_alloc(rdomain);
			newsa->tdb_satype = smsg->sadb_msg_satype;

			if ((rval = pfkeyv2_get_proto_alg(newsa->tdb_satype,
			    &newsa->tdb_sproto, &alg))) {
				tdb_unref(newsa);
				NET_UNLOCK();
				goto ret;
			}

			/* Initialize SA */
			bzero(&ii, sizeof(struct ipsecinit));
			import_sa(newsa, headers[SADB_EXT_SA], &ii);
			import_address(&newsa->tdb_src.sa,
			    headers[SADB_EXT_ADDRESS_SRC]);
			import_address(&newsa->tdb_dst.sa,
			    headers[SADB_EXT_ADDRESS_DST]);
			import_lifetime(newsa,
			    headers[SADB_EXT_LIFETIME_CURRENT],
			    PFKEYV2_LIFETIME_CURRENT);
			import_lifetime(newsa, headers[SADB_EXT_LIFETIME_SOFT],
			    PFKEYV2_LIFETIME_SOFT);
			import_lifetime(newsa, headers[SADB_EXT_LIFETIME_HARD],
			    PFKEYV2_LIFETIME_HARD);
			import_key(&ii, headers[SADB_EXT_KEY_AUTH],
			    PFKEYV2_AUTHENTICATION_KEY);
			import_key(&ii, headers[SADB_EXT_KEY_ENCRYPT],
			    PFKEYV2_ENCRYPTION_KEY);
			newsa->tdb_ids_swapped = 1; /* only on TDB_UPDATE */
			import_identities(&newsa->tdb_ids,
			    newsa->tdb_ids_swapped,
			    headers[SADB_EXT_IDENTITY_SRC],
			    headers[SADB_EXT_IDENTITY_DST]);
			if ((rval = import_flow(&newsa->tdb_filter,
			    &newsa->tdb_filtermask,
			    headers[SADB_X_EXT_SRC_FLOW],
			    headers[SADB_X_EXT_SRC_MASK],
			    headers[SADB_X_EXT_DST_FLOW],
			    headers[SADB_X_EXT_DST_MASK],
			    headers[SADB_X_EXT_PROTOCOL],
			    headers[SADB_X_EXT_FLOW_TYPE]))) {
				tdb_unref(newsa);
				NET_UNLOCK();
				goto ret;
			}
			import_udpencap(newsa, headers[SADB_X_EXT_UDPENCAP]);
			import_rdomain(newsa, headers[SADB_X_EXT_RDOMAIN]);
#if NPF > 0
			import_tag(newsa, headers[SADB_X_EXT_TAG]);
			import_tap(newsa, headers[SADB_X_EXT_TAP]);
#endif
			import_iface(newsa, headers[SADB_X_EXT_IFACE]);

			/* Exclude sensitive data from reply message. */
			headers[SADB_EXT_KEY_AUTH] = NULL;
			headers[SADB_EXT_KEY_ENCRYPT] = NULL;
			headers[SADB_X_EXT_LOCAL_AUTH] = NULL;
			headers[SADB_X_EXT_REMOTE_AUTH] = NULL;

			newsa->tdb_seq = smsg->sadb_msg_seq;

			rval = tdb_init(newsa, alg, &ii);
			if (rval) {
				rval = EINVAL;
				tdb_unref(newsa);
				NET_UNLOCK();
				goto ret;
			}

			newsa->tdb_cur_allocations = sa2->tdb_cur_allocations;

			/* Delete old version of the SA, insert new one */
			tdb_delete(sa2);

			tdb_addtimeouts(newsa);

			puttdb(newsa);
		} else {
			/*
			 * The SA is already initialized, so we're only allowed to
			 * change lifetimes and some other information; we're
			 * not allowed to change keys, addresses or identities.
			 */
			if (headers[SADB_EXT_KEY_AUTH] ||
			    headers[SADB_EXT_KEY_ENCRYPT] ||
			    headers[SADB_EXT_IDENTITY_SRC] ||
			    headers[SADB_EXT_IDENTITY_DST] ||
			    headers[SADB_EXT_SENSITIVITY]) {
				rval = EINVAL;
				NET_UNLOCK();
				goto ret;
			}

			import_sa(sa2, headers[SADB_EXT_SA], NULL);
			import_lifetime(sa2,
			    headers[SADB_EXT_LIFETIME_CURRENT],
			    PFKEYV2_LIFETIME_CURRENT);
			import_lifetime(sa2, headers[SADB_EXT_LIFETIME_SOFT],
			    PFKEYV2_LIFETIME_SOFT);
			import_lifetime(sa2, headers[SADB_EXT_LIFETIME_HARD],
			    PFKEYV2_LIFETIME_HARD);
			import_udpencap(sa2, headers[SADB_X_EXT_UDPENCAP]);
#if NPF > 0
			import_tag(sa2, headers[SADB_X_EXT_TAG]);
			import_tap(sa2, headers[SADB_X_EXT_TAP]);
#endif
			import_iface(sa2, headers[SADB_X_EXT_IFACE]);

			tdb_addtimeouts(sa2);

			if (headers[SADB_EXT_ADDRESS_SRC] ||
			    headers[SADB_EXT_ADDRESS_PROXY]) {
				mtx_enter(&tdb_sadb_mtx);
				tdb_unlink_locked(sa2);
				import_address((struct sockaddr *)&sa2->tdb_src,
				    headers[SADB_EXT_ADDRESS_SRC]);
				import_address((struct sockaddr *)&sa2->tdb_dst,
				    headers[SADB_EXT_ADDRESS_PROXY]);
				puttdb_locked(sa2);
				mtx_leave(&tdb_sadb_mtx);
			}
		}
		NET_UNLOCK();

		break;
	case SADB_ADD:
		ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
		sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
		    sizeof(struct sadb_address));

		/* Either all or none of the flow must be included */
		if ((headers[SADB_X_EXT_SRC_FLOW] ||
		    headers[SADB_X_EXT_PROTOCOL] ||
		    headers[SADB_X_EXT_FLOW_TYPE] ||
		    headers[SADB_X_EXT_DST_FLOW] ||
		    headers[SADB_X_EXT_SRC_MASK] ||
		    headers[SADB_X_EXT_DST_MASK]) &&
		    !(headers[SADB_X_EXT_SRC_FLOW] &&
		    headers[SADB_X_EXT_PROTOCOL] &&
		    headers[SADB_X_EXT_FLOW_TYPE] &&
		    headers[SADB_X_EXT_DST_FLOW] &&
		    headers[SADB_X_EXT_SRC_MASK] &&
		    headers[SADB_X_EXT_DST_MASK])) {
			rval = EINVAL;
			goto ret;
		}
#ifdef IPSEC
		/* UDP encap has to be enabled and is only supported for ESP */
		if (headers[SADB_X_EXT_UDPENCAP] &&
		    (!atomic_load_int(&udpencap_enable) ||
		    smsg->sadb_msg_satype != SADB_SATYPE_ESP)) {
			rval = EINVAL;
			goto ret;
		}
#endif /* IPSEC */

		NET_LOCK();
		sa2 = gettdb(rdomain, ssa->sadb_sa_spi, sunionp,
		    SADB_X_GETSPROTO(smsg->sadb_msg_satype));

		/* We can't add an existing SA! */
		if (sa2 != NULL) {
			rval = EEXIST;
			NET_UNLOCK();
			goto ret;
		}

		/* We can only add "mature" SAs */
		if (ssa->sadb_sa_state != SADB_SASTATE_MATURE) {
			rval = EINVAL;
			NET_UNLOCK();
			goto ret;
		}

		{
			struct tdb *newsa;
			struct ipsecinit ii;
			int alg;

			/* Create new TDB */
			newsa = tdb_alloc(rdomain);
			newsa->tdb_satype = smsg->sadb_msg_satype;

			if ((rval = pfkeyv2_get_proto_alg(newsa->tdb_satype,
			    &newsa->tdb_sproto, &alg))) {
				tdb_unref(newsa);
				NET_UNLOCK();
				goto ret;
			}

			/* Initialize SA */
			bzero(&ii, sizeof(struct ipsecinit));
			import_sa(newsa, headers[SADB_EXT_SA], &ii);
			import_address(&newsa->tdb_src.sa,
			    headers[SADB_EXT_ADDRESS_SRC]);
			import_address(&newsa->tdb_dst.sa,
			    headers[SADB_EXT_ADDRESS_DST]);

			import_lifetime(newsa,
			    headers[SADB_EXT_LIFETIME_CURRENT],
			    PFKEYV2_LIFETIME_CURRENT);
			import_lifetime(newsa, headers[SADB_EXT_LIFETIME_SOFT],
			    PFKEYV2_LIFETIME_SOFT);
			import_lifetime(newsa, headers[SADB_EXT_LIFETIME_HARD],
			    PFKEYV2_LIFETIME_HARD);

			import_key(&ii, headers[SADB_EXT_KEY_AUTH],
			    PFKEYV2_AUTHENTICATION_KEY);
			import_key(&ii, headers[SADB_EXT_KEY_ENCRYPT],
			    PFKEYV2_ENCRYPTION_KEY);

			import_identities(&newsa->tdb_ids,
			    newsa->tdb_ids_swapped,
			    headers[SADB_EXT_IDENTITY_SRC],
			    headers[SADB_EXT_IDENTITY_DST]);

			if ((rval = import_flow(&newsa->tdb_filter,
			    &newsa->tdb_filtermask,
			    headers[SADB_X_EXT_SRC_FLOW],
			    headers[SADB_X_EXT_SRC_MASK],
			    headers[SADB_X_EXT_DST_FLOW],
			    headers[SADB_X_EXT_DST_MASK],
			    headers[SADB_X_EXT_PROTOCOL],
			    headers[SADB_X_EXT_FLOW_TYPE]))) {
				tdb_unref(newsa);
				NET_UNLOCK();
				goto ret;
			}
			import_udpencap(newsa, headers[SADB_X_EXT_UDPENCAP]);
			import_rdomain(newsa, headers[SADB_X_EXT_RDOMAIN]);
#if NPF > 0
			import_tag(newsa, headers[SADB_X_EXT_TAG]);
			import_tap(newsa, headers[SADB_X_EXT_TAP]);
#endif
			import_iface(newsa, headers[SADB_X_EXT_IFACE]);

			/* Exclude sensitive data from reply message. */
			headers[SADB_EXT_KEY_AUTH] = NULL;
			headers[SADB_EXT_KEY_ENCRYPT] = NULL;
			headers[SADB_X_EXT_LOCAL_AUTH] = NULL;
			headers[SADB_X_EXT_REMOTE_AUTH] = NULL;

			newsa->tdb_seq = smsg->sadb_msg_seq;

			rval = tdb_init(newsa, alg, &ii);
			if (rval) {
				rval = EINVAL;
				tdb_unref(newsa);
				NET_UNLOCK();
				goto ret;
			}

			tdb_addtimeouts(newsa);

			/* Add TDB in table */
			puttdb(newsa);
		}
		NET_UNLOCK();

		break;

	case SADB_DELETE:
		ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
		sunionp =
		    (union sockaddr_union *)(headers[SADB_EXT_ADDRESS_DST] +
			sizeof(struct sadb_address));

		NET_LOCK();
		sa2 = gettdb(rdomain, ssa->sadb_sa_spi, sunionp,
		    SADB_X_GETSPROTO(smsg->sadb_msg_satype));
		if (sa2 == NULL) {
			rval = ESRCH;
			NET_UNLOCK();
			goto ret;
		}

		tdb_delete(sa2);
		NET_UNLOCK();

		break;

	case SADB_X_ASKPOLICY:
		/* Get the relevant policy */
		NET_LOCK();
		ipa = ipsec_get_acquire(((struct sadb_x_policy *)
		    headers[SADB_X_EXT_POLICY])->sadb_x_policy_seq);
		if (ipa == NULL) {
			rval = ESRCH;
			NET_UNLOCK();
			goto ret;
		}

		rval = pfkeyv2_policy(ipa, headers, &freeme, &freeme_sz);
		NET_UNLOCK();
		ipsec_unref_acquire(ipa);
		if (rval)
			mode = PFKEYV2_SENDMESSAGE_UNICAST;

		break;

	case SADB_GET:
		ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
		sunionp =
		    (union sockaddr_union *)(headers[SADB_EXT_ADDRESS_DST] +
			sizeof(struct sadb_address));

		NET_LOCK();
		sa2 = gettdb(rdomain, ssa->sadb_sa_spi, sunionp,
		    SADB_X_GETSPROTO(smsg->sadb_msg_satype));
		if (sa2 == NULL) {
			rval = ESRCH;
			NET_UNLOCK();
			goto ret;
		}

		rval = pfkeyv2_get(sa2, headers, &freeme, &freeme_sz, NULL);
		NET_UNLOCK();
		if (rval)
			mode = PFKEYV2_SENDMESSAGE_UNICAST;

		break;

	case SADB_REGISTER:
		keylock(kp);
		if (!(kp->kcb_flags & PFKEYV2_SOCKETFLAGS_REGISTERED)) {
			kp->kcb_flags |= PFKEYV2_SOCKETFLAGS_REGISTERED;
			mtx_enter(&pfkeyv2_mtx);
			nregistered++;
			mtx_leave(&pfkeyv2_mtx);
		}
		keyunlock(kp);

		freeme_sz = sizeof(struct sadb_supported) + sizeof(ealgs);
		if (!(freeme = malloc(freeme_sz, M_PFKEY, M_NOWAIT | M_ZERO))) {
			rval = ENOMEM;
			goto ret;
		}

		ssup = (struct sadb_supported *) freeme;
		ssup->sadb_supported_len = freeme_sz / sizeof(uint64_t);

		{
			void *p = freeme + sizeof(struct sadb_supported);

			bcopy(&ealgs[0], p, sizeof(ealgs));
		}

		headers[SADB_EXT_SUPPORTED_ENCRYPT] = freeme;

		freeme2_sz = sizeof(struct sadb_supported) + sizeof(aalgs);
		if (!(freeme2 = malloc(freeme2_sz, M_PFKEY,
		    M_NOWAIT | M_ZERO))) {
			rval = ENOMEM;
			goto ret;
		}

		/* Keep track what this socket has registered for */
		keylock(kp);
		kp->kcb_reg |=
		    (1 << ((struct sadb_msg *)message)->sadb_msg_satype);
		keyunlock(kp);

		ssup = (struct sadb_supported *) freeme2;
		ssup->sadb_supported_len = freeme2_sz / sizeof(uint64_t);

		{
			void *p = freeme2 + sizeof(struct sadb_supported);

			bcopy(&aalgs[0], p, sizeof(aalgs));
		}

		headers[SADB_EXT_SUPPORTED_AUTH] = freeme2;

		freeme3_sz = sizeof(struct sadb_supported) + sizeof(calgs);
		if (!(freeme3 = malloc(freeme3_sz, M_PFKEY,
		    M_NOWAIT | M_ZERO))) {
			rval = ENOMEM;
			goto ret;
		}

		ssup = (struct sadb_supported *) freeme3;
		ssup->sadb_supported_len = freeme3_sz / sizeof(uint64_t);

		{
			void *p = freeme3 + sizeof(struct sadb_supported);

			bcopy(&calgs[0], p, sizeof(calgs));
		}

		headers[SADB_X_EXT_SUPPORTED_COMP] = freeme3;

		break;

	case SADB_ACQUIRE:
	case SADB_EXPIRE:
		/* Nothing to handle */
		rval = 0;
		break;

	case SADB_FLUSH:
		rval = 0;

		NET_LOCK();
		switch (smsg->sadb_msg_satype) {
		case SADB_SATYPE_UNSPEC:
			spd_table_walk(rdomain, pfkeyv2_policy_flush, NULL);
			/* FALLTHROUGH */
		case SADB_SATYPE_AH:
		case SADB_SATYPE_ESP:
		case SADB_X_SATYPE_IPIP:
		case SADB_X_SATYPE_IPCOMP:
#ifdef TCP_SIGNATURE
		case SADB_X_SATYPE_TCPSIGNATURE:
#endif /* TCP_SIGNATURE */
			tdb_walk(rdomain, pfkeyv2_sa_flush,
			    (u_int8_t *) &(smsg->sadb_msg_satype));

			break;

		default:
			rval = EINVAL; /* Unknown/unsupported type */
		}
		NET_UNLOCK();

		break;

	case SADB_DUMP:
	{
		struct dump_state dump_state;
		dump_state.sadb_msg = (struct sadb_msg *) headers[0];
		dump_state.socket = so;

		NET_LOCK();
		rval = tdb_walk(rdomain, pfkeyv2_dump_walker, &dump_state);
		NET_UNLOCK();
		if (!rval)
			goto realret;
		if ((rval == ENOMEM) || (rval == ENOBUFS))
			rval = 0;
	}
	break;

	case SADB_X_GRPSPIS:
	{
		struct tdb *tdb1, *tdb2, *tdb3;
		struct sadb_protocol *sa_proto;

		ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
		sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
		    sizeof(struct sadb_address));

		NET_LOCK();
		tdb1 = gettdb(rdomain, ssa->sadb_sa_spi, sunionp,
		    SADB_X_GETSPROTO(smsg->sadb_msg_satype));
		if (tdb1 == NULL) {
			rval = ESRCH;
			NET_UNLOCK();
			goto ret;
		}

		ssa = (struct sadb_sa *) headers[SADB_X_EXT_SA2];
		sunionp = (union sockaddr_union *) (headers[SADB_X_EXT_DST2] +
		    sizeof(struct sadb_address));
		sa_proto = (struct sadb_protocol *) headers[SADB_X_EXT_SATYPE2];

		/* optionally fetch tdb2 from rdomain2 */
		tdb2 = gettdb(srdomain ? srdomain->sadb_x_rdomain_dom2 : rdomain,
		    ssa->sadb_sa_spi, sunionp,
		    SADB_X_GETSPROTO(sa_proto->sadb_protocol_proto));
		if (tdb2 == NULL) {
			tdb_unref(tdb1);
			rval = ESRCH;
			NET_UNLOCK();
			goto ret;
		}

		/* Detect cycles */
		for (tdb3 = tdb2; tdb3; tdb3 = tdb3->tdb_onext)
			if (tdb3 == tdb1) {
				tdb_unref(tdb1);
				tdb_unref(tdb2);
				rval = ESRCH;
				NET_UNLOCK();
				goto ret;
			}

		/* Maintenance */
		if ((tdb1->tdb_onext) &&
		    (tdb1->tdb_onext->tdb_inext == tdb1)) {
			tdb_unref(tdb1->tdb_onext->tdb_inext);
			tdb1->tdb_onext->tdb_inext = NULL;
		}

		if ((tdb2->tdb_inext) &&
		    (tdb2->tdb_inext->tdb_onext == tdb2)) {
			tdb_unref(tdb2->tdb_inext->tdb_onext);
			tdb2->tdb_inext->tdb_onext = NULL;
		}

		/* Link them */
		tdb1->tdb_onext = tdb2;
		tdb2->tdb_inext = tdb1;
		NET_UNLOCK();
	}
	break;

	case SADB_X_DELFLOW:
		delflag = 1;
		/*FALLTHROUGH*/
	case SADB_X_ADDFLOW:
	{
		struct sadb_protocol *sab;
		union sockaddr_union *ssrc;
		int exists = 0;

		NET_LOCK();
		if ((rnh = spd_table_add(rdomain)) == NULL) {
			rval = ENOMEM;
			NET_UNLOCK();
			goto ret;
		}

		sab = (struct sadb_protocol *) headers[SADB_X_EXT_FLOW_TYPE];

		if ((sab->sadb_protocol_direction != IPSP_DIRECTION_IN) &&
		    (sab->sadb_protocol_direction != IPSP_DIRECTION_OUT)) {
			rval = EINVAL;
			NET_UNLOCK();
			goto ret;
		}

		/* If the security protocol wasn't specified, pretend it was ESP */
		if (smsg->sadb_msg_satype == 0)
			smsg->sadb_msg_satype = SADB_SATYPE_ESP;

		if (headers[SADB_EXT_ADDRESS_DST])
			sunionp = (union sockaddr_union *)
			    (headers[SADB_EXT_ADDRESS_DST] +
				sizeof(struct sadb_address));
		else
			sunionp = NULL;

		if (headers[SADB_EXT_ADDRESS_SRC])
			ssrc = (union sockaddr_union *)
			    (headers[SADB_EXT_ADDRESS_SRC] +
				sizeof(struct sadb_address));
		else
			ssrc = NULL;

		if ((rval = import_flow(&encapdst, &encapnetmask,
		    headers[SADB_X_EXT_SRC_FLOW], headers[SADB_X_EXT_SRC_MASK],
		    headers[SADB_X_EXT_DST_FLOW], headers[SADB_X_EXT_DST_MASK],
		    headers[SADB_X_EXT_PROTOCOL],
		    headers[SADB_X_EXT_FLOW_TYPE]))) {
			NET_UNLOCK();
			goto ret;
		}

		/* Determine whether the exact same SPD entry already exists. */
		if ((rn = rn_match(&encapdst, rnh)) != NULL) {
			ipo = (struct ipsec_policy *)rn;

			/* Verify that the entry is identical */
			if (bcmp(&ipo->ipo_addr, &encapdst,
				sizeof(struct sockaddr_encap)) ||
			    bcmp(&ipo->ipo_mask, &encapnetmask,
				sizeof(struct sockaddr_encap)))
				ipo = NULL; /* Fall through */
			else
				exists = 1;
		} else
			ipo = NULL;

		/*
		 * If the existing policy is static, only delete or update
		 * it if the new one is also static.
		 */
		if (exists && (ipo->ipo_flags & IPSP_POLICY_STATIC)) {
			if (!(sab->sadb_protocol_flags &
				SADB_X_POLICYFLAGS_POLICY)) {
				NET_UNLOCK();
				goto ret;
			}
		}

		/* Delete ? */
		if (delflag) {
			if (exists) {
				rval = ipsec_delete_policy(ipo);
				NET_UNLOCK();
				goto ret;
			}

			/* If we were asked to delete something non-existent, error. */
			rval = ESRCH;
			NET_UNLOCK();
			break;
		}

		if (!exists) {
			/* Allocate policy entry */
			ipo = pool_get(&ipsec_policy_pool, PR_NOWAIT|PR_ZERO);
			if (ipo == NULL) {
				rval = ENOMEM;
				NET_UNLOCK();
				goto ret;
			}
		}

		switch (sab->sadb_protocol_proto) {
		case SADB_X_FLOW_TYPE_USE:
			ipo->ipo_type = IPSP_IPSEC_USE;
			break;

		case SADB_X_FLOW_TYPE_ACQUIRE:
			ipo->ipo_type = IPSP_IPSEC_ACQUIRE;
			break;

		case SADB_X_FLOW_TYPE_REQUIRE:
			ipo->ipo_type = IPSP_IPSEC_REQUIRE;
			break;

		case SADB_X_FLOW_TYPE_DENY:
			ipo->ipo_type = IPSP_DENY;
			break;

		case SADB_X_FLOW_TYPE_BYPASS:
			ipo->ipo_type = IPSP_PERMIT;
			break;

		case SADB_X_FLOW_TYPE_DONTACQ:
			ipo->ipo_type = IPSP_IPSEC_DONTACQ;
			break;

		default:
			if (!exists)
				pool_put(&ipsec_policy_pool, ipo);
			else
				ipsec_delete_policy(ipo);

			rval = EINVAL;
			NET_UNLOCK();
			goto ret;
		}

		if (sab->sadb_protocol_flags & SADB_X_POLICYFLAGS_POLICY)
			ipo->ipo_flags |= IPSP_POLICY_STATIC;

		if (sunionp)
			bcopy(sunionp, &ipo->ipo_dst,
			    sizeof(union sockaddr_union));
		else
			bzero(&ipo->ipo_dst, sizeof(union sockaddr_union));

		if (ssrc)
			bcopy(ssrc, &ipo->ipo_src,
			    sizeof(union sockaddr_union));
		else
			bzero(&ipo->ipo_src, sizeof(union sockaddr_union));

		ipo->ipo_sproto = SADB_X_GETSPROTO(smsg->sadb_msg_satype);

		if (ipo->ipo_ids) {
			ipsp_ids_free(ipo->ipo_ids);
			ipo->ipo_ids = NULL;
		}

		if ((sid = headers[SADB_EXT_IDENTITY_SRC]) != NULL &&
		    (did = headers[SADB_EXT_IDENTITY_DST]) != NULL) {
			import_identities(&ipo->ipo_ids, 0, sid, did);
			if (ipo->ipo_ids == NULL) {
				if (exists)
					ipsec_delete_policy(ipo);
				else
					pool_put(&ipsec_policy_pool, ipo);
				rval = ENOBUFS;
				NET_UNLOCK();
				goto ret;
			}
		}

		/* Flow type */
		if (!exists) {
			/* Initialize policy entry */
			bcopy(&encapdst, &ipo->ipo_addr,
			    sizeof(struct sockaddr_encap));
			bcopy(&encapnetmask, &ipo->ipo_mask,
			    sizeof(struct sockaddr_encap));

			TAILQ_INIT(&ipo->ipo_acquires);
			ipo->ipo_rdomain = rdomain;
			refcnt_init(&ipo->ipo_refcnt);

			/* Add SPD entry */
			if ((rnh = spd_table_get(rdomain)) == NULL ||
			    (rn = rn_addroute((caddr_t)&ipo->ipo_addr,
				(caddr_t)&ipo->ipo_mask, rnh,
				ipo->ipo_nodes, 0)) == NULL) {
				/* Remove from linked list of policies on TDB */
				mtx_enter(&ipo_tdb_mtx);
				if (ipo->ipo_tdb != NULL) {
					TAILQ_REMOVE(
					    &ipo->ipo_tdb->tdb_policy_head,
					    ipo, ipo_tdb_next);
					tdb_unref(ipo->ipo_tdb);
					ipo->ipo_tdb = NULL;
				}
				mtx_leave(&ipo_tdb_mtx);
				if (ipo->ipo_ids)
					ipsp_ids_free(ipo->ipo_ids);
				pool_put(&ipsec_policy_pool, ipo);
				NET_UNLOCK();
				goto ret;
			}
			TAILQ_INSERT_HEAD(&ipsec_policy_head, ipo, ipo_list);
			ipsec_in_use++;
		} else {
			ipo->ipo_last_searched = ipo->ipo_flags = 0;
		}
		NET_UNLOCK();
	}
	break;

	case SADB_X_PROMISC:
		if (len >= 2 * sizeof(struct sadb_msg)) {
			struct mbuf *packet;

			if ((rval = pfdatatopacket(message, len, &packet)) != 0)
				goto ret;

			rw_enter_read(&pkptable.pkp_lk);
			TAILQ_FOREACH(bkp, &pkptable.pkp_list, kcb_list) {
				if (bkp == kp ||
				    bkp->kcb_rdomain != kp->kcb_rdomain)
					continue;

				if (!smsg->sadb_msg_seq ||
				    (smsg->sadb_msg_seq == kp->kcb_pid)) {
					pfkey_sendup(bkp, packet, 1);
				}
			}
			rw_exit_read(&pkptable.pkp_lk);

			m_freem(packet);
		} else {
			if (len != sizeof(struct sadb_msg)) {
				rval = EINVAL;
				goto ret;
			}

			keylock(kp);
			i = (kp->kcb_flags &
			    PFKEYV2_SOCKETFLAGS_PROMISC) ? 1 : 0;
			j = smsg->sadb_msg_satype ? 1 : 0;

			if (i ^ j) {
				if (j) {
					kp->kcb_flags |=
					    PFKEYV2_SOCKETFLAGS_PROMISC;
					mtx_enter(&pfkeyv2_mtx);
					npromisc++;
					mtx_leave(&pfkeyv2_mtx);
				} else {
					kp->kcb_flags &=
					    ~PFKEYV2_SOCKETFLAGS_PROMISC;
					mtx_enter(&pfkeyv2_mtx);
					npromisc--;
					mtx_leave(&pfkeyv2_mtx);
				}
			}
			keyunlock(kp);
		}

		break;

	default:
		rval = EINVAL;
		goto ret;
	}

ret:
	if (rval) {
		if ((rval == EINVAL) || (rval == ENOMEM) || (rval == ENOBUFS))
			goto realret;

		for (i = 1; i <= SADB_EXT_MAX; i++)
			headers[i] = NULL;

		smsg->sadb_msg_errno = abs(rval);
	} else {
		uint64_t seen = 0LL;

		for (i = 1; i <= SADB_EXT_MAX; i++)
			if (headers[i])
				seen |= (1LL << i);

		if ((seen & sadb_exts_allowed_out[smsg->sadb_msg_type])
		    != seen) {
		    	rval = EPERM;
			goto realret;
		}

		if ((seen & sadb_exts_required_out[smsg->sadb_msg_type]) !=
		    sadb_exts_required_out[smsg->sadb_msg_type]) {
		    	rval = EPERM;
			goto realret;
		}
	}

	rval = pfkeyv2_sendmessage(headers, mode, so, 0, 0, kp->kcb_rdomain);

realret:

	if (freeme != NULL)
		explicit_bzero(freeme, freeme_sz);
	free(freeme, M_PFKEY, freeme_sz);
	free(freeme2, M_PFKEY, freeme2_sz);
	free(freeme3, M_PFKEY, freeme3_sz);

	explicit_bzero(message, len);
	free(message, M_PFKEY, len);

	free(sa1, M_PFKEY, sizeof(*sa1));

	NET_LOCK();
	tdb_unref(sa2);
	NET_UNLOCK();

	return (rval);
}

/*
 * Send an ACQUIRE message to key management, to get a new SA.
 */
int
pfkeyv2_acquire(struct ipsec_policy *ipo, union sockaddr_union *gw,
    union sockaddr_union *laddr, u_int32_t *seq, struct sockaddr_encap *ddst)
{
	void *p, *headers[SADB_EXT_MAX + 1], *buffer = NULL;
	struct sadb_comb *sadb_comb;
	struct sadb_address *sadd;
	struct sadb_prop *sa_prop;
	struct sadb_msg *smsg;
	int rval = 0;
	int i, j, registered;

#ifdef IPSEC
	int require_pfs_local;
	int def_enc_local, def_comp_local, def_auth_local;
	int soft_allocations_local, exp_allocations_local;
	int soft_bytes_local, exp_bytes_local;
	int soft_timeout_local, exp_timeout_local;
	int soft_first_use_local, exp_first_use_local;

	require_pfs_local = atomic_load_int(&ipsec_require_pfs);

	def_enc_local = atomic_load_int(&ipsec_def_enc);
	def_comp_local = atomic_load_int(&ipsec_def_comp);
	def_auth_local = atomic_load_int(&ipsec_def_auth);

	soft_allocations_local = atomic_load_int(&ipsec_soft_allocations);
	exp_allocations_local = atomic_load_int(&ipsec_exp_allocations);

	soft_bytes_local = atomic_load_int(&ipsec_soft_bytes);
	exp_bytes_local = atomic_load_int(&ipsec_exp_bytes);

	soft_timeout_local = atomic_load_int(&ipsec_soft_timeout);
	exp_timeout_local = atomic_load_int(&ipsec_exp_timeout);

	soft_first_use_local = atomic_load_int(&ipsec_soft_first_use);
	exp_first_use_local = atomic_load_int(&ipsec_exp_first_use);
#endif

	mtx_enter(&pfkeyv2_mtx);
	*seq = pfkeyv2_seq++;

	registered = nregistered;
	mtx_leave(&pfkeyv2_mtx);

	if (!registered) {
		rval = ESRCH;
		goto ret;
	}

	/* How large a buffer do we need... XXX we only do one proposal for now */
	i = sizeof(struct sadb_msg) +
	    (laddr == NULL ? 0 : sizeof(struct sadb_address) +
		PADUP(ipo->ipo_src.sa.sa_len)) +
	    sizeof(struct sadb_address) + PADUP(gw->sa.sa_len) +
	    sizeof(struct sadb_prop) + 1 * sizeof(struct sadb_comb);

	if (ipo->ipo_ids) {
		i += sizeof(struct sadb_ident) + PADUP(ipo->ipo_ids->id_local->len);
		i += sizeof(struct sadb_ident) + PADUP(ipo->ipo_ids->id_remote->len);
	}

	/* Allocate */
	if (!(p = malloc(i, M_PFKEY, M_NOWAIT | M_ZERO))) {
		rval = ENOMEM;
		goto ret;
	}

	bzero(headers, sizeof(headers));

	buffer = p;

	headers[0] = p;
	p += sizeof(struct sadb_msg);

	smsg = (struct sadb_msg *) headers[0];
	smsg->sadb_msg_version = PF_KEY_V2;
	smsg->sadb_msg_type = SADB_ACQUIRE;
	smsg->sadb_msg_len = i / sizeof(uint64_t);
	smsg->sadb_msg_seq = *seq;

	if (ipo->ipo_sproto == IPPROTO_ESP)
		smsg->sadb_msg_satype = SADB_SATYPE_ESP;
	else if (ipo->ipo_sproto == IPPROTO_AH)
		smsg->sadb_msg_satype = SADB_SATYPE_AH;
	else if (ipo->ipo_sproto == IPPROTO_IPCOMP)
		smsg->sadb_msg_satype = SADB_X_SATYPE_IPCOMP;

	if (laddr) {
		headers[SADB_EXT_ADDRESS_SRC] = p;
		p += sizeof(struct sadb_address) + PADUP(laddr->sa.sa_len);
		sadd = (struct sadb_address *) headers[SADB_EXT_ADDRESS_SRC];
		sadd->sadb_address_len = (sizeof(struct sadb_address) +
		    laddr->sa.sa_len + sizeof(uint64_t) - 1) /
		    sizeof(uint64_t);
		bcopy(laddr, headers[SADB_EXT_ADDRESS_SRC] +
		    sizeof(struct sadb_address), laddr->sa.sa_len);
	}

	headers[SADB_EXT_ADDRESS_DST] = p;
	p += sizeof(struct sadb_address) + PADUP(gw->sa.sa_len);
	sadd = (struct sadb_address *) headers[SADB_EXT_ADDRESS_DST];
	sadd->sadb_address_len = (sizeof(struct sadb_address) +
	    gw->sa.sa_len + sizeof(uint64_t) - 1) / sizeof(uint64_t);
	bcopy(gw, headers[SADB_EXT_ADDRESS_DST] + sizeof(struct sadb_address),
	    gw->sa.sa_len);

	if (ipo->ipo_ids)
		export_identities(&p, ipo->ipo_ids, 0, headers);

	headers[SADB_EXT_PROPOSAL] = p;
	p += sizeof(struct sadb_prop);
	sa_prop = (struct sadb_prop *) headers[SADB_EXT_PROPOSAL];
	sa_prop->sadb_prop_num = 1; /* XXX One proposal only */
	sa_prop->sadb_prop_len = (sizeof(struct sadb_prop) +
	    (sizeof(struct sadb_comb) * sa_prop->sadb_prop_num)) /
	    sizeof(uint64_t);

	sadb_comb = p;

	/* XXX Should actually ask the crypto layer what's supported */
	for (j = 0; j < sa_prop->sadb_prop_num; j++) {
		sadb_comb->sadb_comb_flags = 0;
#ifdef IPSEC
		if (require_pfs_local)
			sadb_comb->sadb_comb_flags |= SADB_SAFLAGS_PFS;

		if (ipo->ipo_sproto == IPPROTO_ESP) {
			/* Set the encryption algorithm */
			switch(def_enc_local) {
			case IPSEC_ENC_AES:
				sadb_comb->sadb_comb_encrypt = SADB_X_EALG_AES;
				sadb_comb->sadb_comb_encrypt_minbits = 128;
				sadb_comb->sadb_comb_encrypt_maxbits = 256;
				break;
			case IPSEC_ENC_AESCTR:
				sadb_comb->sadb_comb_encrypt =
				    SADB_X_EALG_AESCTR;
				sadb_comb->sadb_comb_encrypt_minbits = 128 + 32;
				sadb_comb->sadb_comb_encrypt_maxbits = 256 + 32;
				break;
			case IPSEC_ENC_3DES:
				sadb_comb->sadb_comb_encrypt =
				    SADB_EALG_3DESCBC;
				sadb_comb->sadb_comb_encrypt_minbits = 192;
				sadb_comb->sadb_comb_encrypt_maxbits = 192;
				break;
			case IPSEC_ENC_BLOWFISH:
				sadb_comb->sadb_comb_encrypt = SADB_X_EALG_BLF;
				sadb_comb->sadb_comb_encrypt_minbits = 40;
				sadb_comb->sadb_comb_encrypt_maxbits =
				    BLF_MAXKEYLEN * 8;
				break;
			case IPSEC_ENC_CAST128:
				sadb_comb->sadb_comb_encrypt = SADB_X_EALG_CAST;
				sadb_comb->sadb_comb_encrypt_minbits = 40;
				sadb_comb->sadb_comb_encrypt_maxbits = 128;
				break;
			}
		} else if (ipo->ipo_sproto == IPPROTO_IPCOMP) {
			/* Set the compression algorithm */
			switch(def_comp_local) {
			case IPSEC_COMP_DEFLATE:
				sadb_comb->sadb_comb_encrypt =
				    SADB_X_CALG_DEFLATE;
				sadb_comb->sadb_comb_encrypt_minbits = 0;
				sadb_comb->sadb_comb_encrypt_maxbits = 0;
				break;
			}
		}

		/* Set the authentication algorithm */
		switch(def_auth_local) {
		case IPSEC_AUTH_HMAC_SHA1:
			sadb_comb->sadb_comb_auth = SADB_AALG_SHA1HMAC;
			sadb_comb->sadb_comb_auth_minbits = 160;
			sadb_comb->sadb_comb_auth_maxbits = 160;
			break;
		case IPSEC_AUTH_HMAC_RIPEMD160:
			sadb_comb->sadb_comb_auth = SADB_X_AALG_RIPEMD160HMAC;
			sadb_comb->sadb_comb_auth_minbits = 160;
			sadb_comb->sadb_comb_auth_maxbits = 160;
			break;
		case IPSEC_AUTH_MD5:
			sadb_comb->sadb_comb_auth = SADB_AALG_MD5HMAC;
			sadb_comb->sadb_comb_auth_minbits = 128;
			sadb_comb->sadb_comb_auth_maxbits = 128;
			break;
		case IPSEC_AUTH_SHA2_256:
			sadb_comb->sadb_comb_auth = SADB_X_AALG_SHA2_256;
			sadb_comb->sadb_comb_auth_minbits = 256;
			sadb_comb->sadb_comb_auth_maxbits = 256;
			break;
		case IPSEC_AUTH_SHA2_384:
			sadb_comb->sadb_comb_auth = SADB_X_AALG_SHA2_384;
			sadb_comb->sadb_comb_auth_minbits = 384;
			sadb_comb->sadb_comb_auth_maxbits = 384;
			break;
		case IPSEC_AUTH_SHA2_512:
			sadb_comb->sadb_comb_auth = SADB_X_AALG_SHA2_512;
			sadb_comb->sadb_comb_auth_minbits = 512;
			sadb_comb->sadb_comb_auth_maxbits = 512;
			break;
		}

		sadb_comb->sadb_comb_soft_allocations = soft_allocations_local;
		sadb_comb->sadb_comb_hard_allocations = exp_allocations_local;

		sadb_comb->sadb_comb_soft_bytes = soft_bytes_local;
		sadb_comb->sadb_comb_hard_bytes = exp_bytes_local;

		sadb_comb->sadb_comb_soft_addtime = soft_timeout_local;
		sadb_comb->sadb_comb_hard_addtime = exp_timeout_local;

		sadb_comb->sadb_comb_soft_usetime = soft_first_use_local;
		sadb_comb->sadb_comb_hard_usetime = exp_first_use_local;
#endif
		sadb_comb++;
	}

	/* Send the ACQUIRE message to all compliant registered listeners. */
	if ((rval = pfkeyv2_sendmessage(headers,
	    PFKEYV2_SENDMESSAGE_REGISTERED, NULL, smsg->sadb_msg_satype, 0,
	    ipo->ipo_rdomain)) != 0)
		goto ret;

	rval = 0;
ret:
	if (buffer != NULL) {
		explicit_bzero(buffer, i);
		free(buffer, M_PFKEY, i);
	}

	return (rval);
}

/*
 * Notify key management that an expiration went off. The second argument
 * specifies the type of expiration (soft or hard).
 */
int
pfkeyv2_expire(struct tdb *tdb, u_int16_t type)
{
	void *p, *headers[SADB_EXT_MAX+1], *buffer = NULL;
	struct sadb_msg *smsg;
	int rval = 0;
	int i;

	NET_ASSERT_LOCKED();

	switch (tdb->tdb_sproto) {
	case IPPROTO_AH:
	case IPPROTO_ESP:
	case IPPROTO_IPIP:
	case IPPROTO_IPCOMP:
#ifdef TCP_SIGNATURE
	case IPPROTO_TCP:
#endif /* TCP_SIGNATURE */
		break;

	default:
		rval = EOPNOTSUPP;
		goto ret;
	}

	i = sizeof(struct sadb_msg) + sizeof(struct sadb_sa) +
	    2 * sizeof(struct sadb_lifetime) +
	    sizeof(struct sadb_address) + PADUP(tdb->tdb_src.sa.sa_len) +
	    sizeof(struct sadb_address) + PADUP(tdb->tdb_dst.sa.sa_len);

	if (!(p = malloc(i, M_PFKEY, M_NOWAIT | M_ZERO))) {
		rval = ENOMEM;
		goto ret;
	}

	bzero(headers, sizeof(headers));

	buffer = p;

	headers[0] = p;
	p += sizeof(struct sadb_msg);

	smsg = (struct sadb_msg *) headers[0];
	smsg->sadb_msg_version = PF_KEY_V2;
	smsg->sadb_msg_type = SADB_EXPIRE;
	smsg->sadb_msg_satype = tdb->tdb_satype;
	smsg->sadb_msg_len = i / sizeof(uint64_t);

	mtx_enter(&pfkeyv2_mtx);
	smsg->sadb_msg_seq = pfkeyv2_seq++;
	mtx_leave(&pfkeyv2_mtx);

	headers[SADB_EXT_SA] = p;
	export_sa(&p, tdb);

	headers[SADB_EXT_LIFETIME_CURRENT] = p;
	export_lifetime(&p, tdb, PFKEYV2_LIFETIME_CURRENT);

	headers[type] = p;
	export_lifetime(&p, tdb, type == SADB_EXT_LIFETIME_SOFT ?
	    PFKEYV2_LIFETIME_SOFT : PFKEYV2_LIFETIME_HARD);

	headers[SADB_EXT_ADDRESS_SRC] = p;
	export_address(&p, &tdb->tdb_src.sa);

	headers[SADB_EXT_ADDRESS_DST] = p;
	export_address(&p, &tdb->tdb_dst.sa);

	if ((rval = pfkeyv2_sendmessage(headers, PFKEYV2_SENDMESSAGE_BROADCAST,
	    NULL, 0, 0, tdb->tdb_rdomain)) != 0)
		goto ret;
	/* XXX */
	if (tdb->tdb_rdomain != tdb->tdb_rdomain_post)
		if ((rval = pfkeyv2_sendmessage(headers,
		    PFKEYV2_SENDMESSAGE_BROADCAST, NULL, 0, 0,
		    tdb->tdb_rdomain_post)) != 0)
			goto ret;

	rval = 0;

 ret:
	if (buffer != NULL) {
		explicit_bzero(buffer, i);
		free(buffer, M_PFKEY, i);
	}

	return (rval);
}

struct pfkeyv2_sysctl_walk {
	void		*w_where;
	size_t		 w_len;
	int		 w_op;
	u_int8_t	 w_satype;
};

int
pfkeyv2_sysctl_walker(struct tdb *tdb, void *arg, int last)
{
	struct pfkeyv2_sysctl_walk *w = (struct pfkeyv2_sysctl_walk *)arg;
	void *buffer = NULL;
	int error = 0;
	int usedlen, buflen, i;

	if (w->w_satype != SADB_SATYPE_UNSPEC &&
	    w->w_satype != tdb->tdb_satype)
		return (0);

	if (w->w_where) {
		void *headers[SADB_EXT_MAX+1];
		struct sadb_msg msg;

		bzero(headers, sizeof(headers));
		if ((error = pfkeyv2_get(tdb, headers, &buffer, &buflen,
		    &usedlen)) != 0)
			goto done;
		if (w->w_len < sizeof(msg) + usedlen) {
			error = ENOMEM;
			goto done;
		}
		/* prepend header */
		bzero(&msg, sizeof(msg));
		msg.sadb_msg_version = PF_KEY_V2;
		msg.sadb_msg_satype = tdb->tdb_satype;
		msg.sadb_msg_type = SADB_DUMP;
		msg.sadb_msg_len = (sizeof(msg) + usedlen) / sizeof(uint64_t);
		if ((error = copyout(&msg, w->w_where, sizeof(msg))) != 0)
			goto done;
		w->w_where += sizeof(msg);
		w->w_len -= sizeof(msg);
		/* set extension type */
		for (i = 1; i <= SADB_EXT_MAX; i++)
			if (headers[i])
				((struct sadb_ext *)
				    headers[i])->sadb_ext_type = i;
		if ((error = copyout(buffer, w->w_where, usedlen)) != 0)
			goto done;
		w->w_where += usedlen;
		w->w_len -= usedlen;
	} else {
		if ((error = pfkeyv2_get(tdb, NULL, NULL, &buflen, NULL)) != 0)
			return (error);
		w->w_len += buflen;
		w->w_len += sizeof(struct sadb_msg);
	}

done:
	if (buffer != NULL) {
		explicit_bzero(buffer, buflen);
		free(buffer, M_PFKEY, buflen);
	}
	return (error);
}

int
pfkeyv2_dump_policy(struct ipsec_policy *ipo, void **headers, void **buffer,
    int *lenp)
{
	int i, rval, perm;
	void *p;

	/* Find how much space we need. */
	i = 2 * sizeof(struct sadb_protocol);

	/* We'll need four of them: src, src mask, dst, dst mask. */
	switch (ipo->ipo_addr.sen_type) {
	case SENT_IP4:
		i += 4 * PADUP(sizeof(struct sockaddr_in));
		i += 4 * sizeof(struct sadb_address);
		break;
#ifdef INET6
	case SENT_IP6:
		i += 4 * PADUP(sizeof(struct sockaddr_in6));
		i += 4 * sizeof(struct sadb_address);
		break;
#endif /* INET6 */
	default:
		return (EINVAL);
	}

	/* Local address, might be zeroed. */
	switch (ipo->ipo_src.sa.sa_family) {
	case 0:
		break;
	case AF_INET:
		i += PADUP(sizeof(struct sockaddr_in));
		i += sizeof(struct sadb_address);
		break;
#ifdef INET6
	case AF_INET6:
		i += PADUP(sizeof(struct sockaddr_in6));
		i += sizeof(struct sadb_address);
		break;
#endif /* INET6 */
	default:
		return (EINVAL);
	}

	/* Remote address, might be zeroed. XXX ??? */
	switch (ipo->ipo_dst.sa.sa_family) {
	case 0:
		break;
	case AF_INET:
		i += PADUP(sizeof(struct sockaddr_in));
		i += sizeof(struct sadb_address);
		break;
#ifdef INET6
	case AF_INET6:
		i += PADUP(sizeof(struct sockaddr_in6));
		i += sizeof(struct sadb_address);
		break;
#endif /* INET6 */
	default:
		return (EINVAL);
	}

	if (ipo->ipo_ids) {
		i += sizeof(struct sadb_ident) + PADUP(ipo->ipo_ids->id_local->len);
		i += sizeof(struct sadb_ident) + PADUP(ipo->ipo_ids->id_remote->len);
	}

	if (lenp)
		*lenp = i;

	if (buffer == NULL) {
		rval = 0;
		goto ret;
	}

	if (!(p = malloc(i, M_PFKEY, M_NOWAIT | M_ZERO))) {
		rval = ENOMEM;
		goto ret;
	} else
		*buffer = p;

	/* Local address. */
	if (ipo->ipo_src.sa.sa_family) {
		headers[SADB_EXT_ADDRESS_SRC] = p;
		export_address(&p, &ipo->ipo_src.sa);
	}

	/* Remote address. */
	if (ipo->ipo_dst.sa.sa_family) {
		headers[SADB_EXT_ADDRESS_DST] = p;
		export_address(&p, &ipo->ipo_dst.sa);
	}

	/* Get actual flow. */
	export_flow(&p, ipo->ipo_type, &ipo->ipo_addr, &ipo->ipo_mask,
	    headers);

	/* Add ids only when we are root. */
	perm = suser(curproc);
	if (perm == 0 && ipo->ipo_ids)
		export_identities(&p, ipo->ipo_ids, 0, headers);

	rval = 0;
ret:
	return (rval);
}

int
pfkeyv2_sysctl_policydumper(struct ipsec_policy *ipo, void *arg,
    unsigned int tableid)
{
	struct pfkeyv2_sysctl_walk *w = (struct pfkeyv2_sysctl_walk *)arg;
	void *buffer = NULL;
	int i, buflen, error = 0;

	if (w->w_where) {
		void *headers[SADB_EXT_MAX + 1];
		struct sadb_msg msg;

		bzero(headers, sizeof(headers));
		if ((error = pfkeyv2_dump_policy(ipo, headers, &buffer,
		    &buflen)) != 0)
			goto done;
		if (w->w_len < buflen) {
			error = ENOMEM;
			goto done;
		}
		/* prepend header */
		bzero(&msg, sizeof(msg));
		msg.sadb_msg_version = PF_KEY_V2;
		if (ipo->ipo_sproto == IPPROTO_ESP)
			msg.sadb_msg_satype = SADB_SATYPE_ESP;
		else if (ipo->ipo_sproto == IPPROTO_AH)
			msg.sadb_msg_satype = SADB_SATYPE_AH;
		else if (ipo->ipo_sproto == IPPROTO_IPCOMP)
			msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		else if (ipo->ipo_sproto == IPPROTO_IPIP)
			msg.sadb_msg_satype = SADB_X_SATYPE_IPIP;
		msg.sadb_msg_type = SADB_X_SPDDUMP;
		msg.sadb_msg_len = (sizeof(msg) + buflen) / sizeof(uint64_t);
		if ((error = copyout(&msg, w->w_where, sizeof(msg))) != 0)
			goto done;
		w->w_where += sizeof(msg);
		w->w_len -= sizeof(msg);
		/* set extension type */
		for (i = 1; i <= SADB_EXT_MAX; i++)
			if (headers[i])
				((struct sadb_ext *)
				    headers[i])->sadb_ext_type = i;
		if ((error = copyout(buffer, w->w_where, buflen)) != 0)
			goto done;
		w->w_where += buflen;
		w->w_len -= buflen;
	} else {
		if ((error = pfkeyv2_dump_policy(ipo, NULL, NULL,
		    &buflen)) != 0)
			goto done;
		w->w_len += buflen;
		w->w_len += sizeof(struct sadb_msg);
	}

done:
	if (buffer)
		free(buffer, M_PFKEY, buflen);
	return (error);
}

int
pfkeyv2_policy_flush(struct ipsec_policy *ipo, void *arg, unsigned int tableid)
{
	int error;

	error = ipsec_delete_policy(ipo);
	if (error == 0)
		error = EAGAIN;

	return (error);
}

int
pfkeyv2_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *new, size_t newlen)
{
	struct pfkeyv2_sysctl_walk w;
	int error = EINVAL;
	u_int rdomain;
	u_int tableid;

	if (new)
		return (EPERM);
	if (namelen < 1)
		return (EINVAL);
	w.w_op = name[0];
	if (namelen >= 2)
		w.w_satype = name[1];
	else
		w.w_satype = SADB_SATYPE_UNSPEC;
	w.w_where = oldp;
	w.w_len = oldp ? *oldlenp : 0;

	if (namelen == 3) {
		tableid = name[2];
		if (!rtable_exists(tableid))
			return (ENOENT);
	} else
		tableid = curproc->p_p->ps_rtableid;
	rdomain = rtable_l2(tableid);

	switch(w.w_op) {
	case NET_KEY_SADB_DUMP:
		if ((error = suser(curproc)) != 0)
			return (error);
		NET_LOCK();
		error = tdb_walk(rdomain, pfkeyv2_sysctl_walker, &w);
		NET_UNLOCK();
		if (oldp)
			*oldlenp = w.w_where - oldp;
		else
			*oldlenp = w.w_len;
		break;

	case NET_KEY_SPD_DUMP:
		NET_LOCK_SHARED();
		error = spd_table_walk(rdomain,
		    pfkeyv2_sysctl_policydumper, &w);
		NET_UNLOCK_SHARED();
		if (oldp)
			*oldlenp = w.w_where - oldp;
		else
			*oldlenp = w.w_len;
		break;
	}

	return (error);
}
