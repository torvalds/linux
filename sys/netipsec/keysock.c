/*	$FreeBSD$	*/
/*	$KAME: keysock.c,v 1.25 2001/08/13 20:07:41 itojun Exp $	*/

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
 */

#include "opt_ipsec.h"

/* This code has derived from sys/net/rtsock.c on FreeBSD2.2.5 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/domain.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/vnet.h>
#include <net/raw_cb.h>

#include <netinet/in.h>

#include <net/pfkeyv2.h>
#include <netipsec/key.h>
#include <netipsec/keysock.h>
#include <netipsec/key_debug.h>
#include <netipsec/ipsec.h>

#include <machine/stdarg.h>

struct key_cb {
	int key_count;
	int any_count;
};
VNET_DEFINE_STATIC(struct key_cb, key_cb);
#define	V_key_cb		VNET(key_cb)

static struct sockaddr key_src = { 2, PF_KEY, };

static int key_sendup0(struct rawcb *, struct mbuf *, int);

VNET_PCPUSTAT_DEFINE(struct pfkeystat, pfkeystat);
VNET_PCPUSTAT_SYSINIT(pfkeystat);

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(pfkeystat);
#endif /* VIMAGE */

/*
 * key_output()
 */
int
key_output(struct mbuf *m, struct socket *so, ...)
{
	struct sadb_msg *msg;
	int len, error = 0;

	if (m == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	PFKEYSTAT_INC(out_total);
	PFKEYSTAT_ADD(out_bytes, m->m_pkthdr.len);

	len = m->m_pkthdr.len;
	if (len < sizeof(struct sadb_msg)) {
		PFKEYSTAT_INC(out_tooshort);
		error = EINVAL;
		goto end;
	}

	if (m->m_len < sizeof(struct sadb_msg)) {
		if ((m = m_pullup(m, sizeof(struct sadb_msg))) == NULL) {
			PFKEYSTAT_INC(out_nomem);
			error = ENOBUFS;
			goto end;
		}
	}

	M_ASSERTPKTHDR(m);

	KEYDBG(KEY_DUMP, kdebug_mbuf(m));

	msg = mtod(m, struct sadb_msg *);
	PFKEYSTAT_INC(out_msgtype[msg->sadb_msg_type]);
	if (len != PFKEY_UNUNIT64(msg->sadb_msg_len)) {
		PFKEYSTAT_INC(out_invlen);
		error = EINVAL;
		goto end;
	}

	error = key_parse(m, so);
	m = NULL;
end:
	if (m)
		m_freem(m);
	return error;
}

/*
 * send message to the socket.
 */
static int
key_sendup0(struct rawcb *rp, struct mbuf *m, int promisc)
{
	int error;

	if (promisc) {
		struct sadb_msg *pmsg;

		M_PREPEND(m, sizeof(struct sadb_msg), M_NOWAIT);
		if (m == NULL) {
			PFKEYSTAT_INC(in_nomem);
			return (ENOBUFS);
		}
		pmsg = mtod(m, struct sadb_msg *);
		bzero(pmsg, sizeof(*pmsg));
		pmsg->sadb_msg_version = PF_KEY_V2;
		pmsg->sadb_msg_type = SADB_X_PROMISC;
		pmsg->sadb_msg_len = PFKEY_UNIT64(m->m_pkthdr.len);
		/* pid and seq? */

		PFKEYSTAT_INC(in_msgtype[pmsg->sadb_msg_type]);
	}

	if (!sbappendaddr(&rp->rcb_socket->so_rcv, (struct sockaddr *)&key_src,
	    m, NULL)) {
		PFKEYSTAT_INC(in_nomem);
		m_freem(m);
		error = ENOBUFS;
	} else
		error = 0;
	sorwakeup(rp->rcb_socket);
	return error;
}

/* so can be NULL if target != KEY_SENDUP_ONE */
int
key_sendup_mbuf(struct socket *so, struct mbuf *m, int target)
{
	struct mbuf *n;
	struct keycb *kp;
	struct rawcb *rp;
	int error = 0;

	KASSERT(m != NULL, ("NULL mbuf pointer was passed."));
	KASSERT(so != NULL || target != KEY_SENDUP_ONE,
	    ("NULL socket pointer was passed."));
	KASSERT(target == KEY_SENDUP_ONE || target == KEY_SENDUP_ALL ||
	    target == KEY_SENDUP_REGISTERED, ("Wrong target %d", target));

	PFKEYSTAT_INC(in_total);
	PFKEYSTAT_ADD(in_bytes, m->m_pkthdr.len);
	if (m->m_len < sizeof(struct sadb_msg)) {
		m = m_pullup(m, sizeof(struct sadb_msg));
		if (m == NULL) {
			PFKEYSTAT_INC(in_nomem);
			return ENOBUFS;
		}
	}
	if (m->m_len >= sizeof(struct sadb_msg)) {
		struct sadb_msg *msg;
		msg = mtod(m, struct sadb_msg *);
		PFKEYSTAT_INC(in_msgtype[msg->sadb_msg_type]);
	}
	mtx_lock(&rawcb_mtx);
	if (V_key_cb.any_count == 0) {
		mtx_unlock(&rawcb_mtx);
		m_freem(m);
		return (0);
	}
	LIST_FOREACH(rp, &V_rawcb_list, list)
	{
		if (rp->rcb_proto.sp_family != PF_KEY)
			continue;
		if (rp->rcb_proto.sp_protocol
		 && rp->rcb_proto.sp_protocol != PF_KEY_V2) {
			continue;
		}

		/*
		 * If you are in promiscuous mode, and when you get broadcasted
		 * reply, you'll get two PF_KEY messages.
		 * (based on pf_key@inner.net message on 14 Oct 1998)
		 */
		kp = (struct keycb *)rp;
		if (kp->kp_promisc) {
			n = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (n != NULL)
				key_sendup0(rp, n, 1);
			else
				PFKEYSTAT_INC(in_nomem);
		}

		/* the exact target will be processed later */
		if (so && sotorawcb(so) == rp)
			continue;

		if (target == KEY_SENDUP_ONE || (
		    target == KEY_SENDUP_REGISTERED && kp->kp_registered == 0))
			continue;

		/* KEY_SENDUP_ALL + KEY_SENDUP_REGISTERED */
		n = m_copym(m, 0, M_COPYALL, M_NOWAIT);
		if (n == NULL) {
			PFKEYSTAT_INC(in_nomem);
			/* Try send to another socket */
			continue;
		}

		if (key_sendup0(rp, n, 0) == 0)
			PFKEYSTAT_INC(in_msgtarget[target]);
	}

	if (so)	{ /* KEY_SENDUP_ONE */
		error = key_sendup0(sotorawcb(so), m, 0);
		if (error == 0)
			PFKEYSTAT_INC(in_msgtarget[KEY_SENDUP_ONE]);
	} else {
		error = 0;
		m_freem(m);
	}
	mtx_unlock(&rawcb_mtx);
	return (error);
}

/*
 * key_abort()
 * derived from net/rtsock.c:rts_abort()
 */
static void
key_abort(struct socket *so)
{
	raw_usrreqs.pru_abort(so);
}

/*
 * key_attach()
 * derived from net/rtsock.c:rts_attach()
 */
static int
key_attach(struct socket *so, int proto, struct thread *td)
{
	struct keycb *kp;
	int error;

	KASSERT(so->so_pcb == NULL, ("key_attach: so_pcb != NULL"));

	if (td != NULL) {
		error = priv_check(td, PRIV_NET_RAW);
		if (error)
			return error;
	}

	/* XXX */
	kp = malloc(sizeof *kp, M_PCB, M_WAITOK | M_ZERO); 
	if (kp == NULL)
		return ENOBUFS;

	so->so_pcb = (caddr_t)kp;
	error = raw_attach(so, proto);
	kp = (struct keycb *)sotorawcb(so);
	if (error) {
		free(kp, M_PCB);
		so->so_pcb = (caddr_t) 0;
		return error;
	}

	kp->kp_promisc = kp->kp_registered = 0;

	if (kp->kp_raw.rcb_proto.sp_protocol == PF_KEY) /* XXX: AF_KEY */
		V_key_cb.key_count++;
	V_key_cb.any_count++;
	soisconnected(so);
	so->so_options |= SO_USELOOPBACK;

	return 0;
}

/*
 * key_bind()
 * derived from net/rtsock.c:rts_bind()
 */
static int
key_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
  return EINVAL;
}

/*
 * key_close()
 * derived from net/rtsock.c:rts_close().
 */
static void
key_close(struct socket *so)
{

	raw_usrreqs.pru_close(so);
}

/*
 * key_connect()
 * derived from net/rtsock.c:rts_connect()
 */
static int
key_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	return EINVAL;
}

/*
 * key_detach()
 * derived from net/rtsock.c:rts_detach()
 */
static void
key_detach(struct socket *so)
{
	struct keycb *kp = (struct keycb *)sotorawcb(so);

	KASSERT(kp != NULL, ("key_detach: kp == NULL"));
	if (kp->kp_raw.rcb_proto.sp_protocol
	    == PF_KEY) /* XXX: AF_KEY */
		V_key_cb.key_count--;
	V_key_cb.any_count--;

	key_freereg(so);
	raw_usrreqs.pru_detach(so);
}

/*
 * key_disconnect()
 * derived from net/rtsock.c:key_disconnect()
 */
static int
key_disconnect(struct socket *so)
{
	return(raw_usrreqs.pru_disconnect(so));
}

/*
 * key_peeraddr()
 * derived from net/rtsock.c:rts_peeraddr()
 */
static int
key_peeraddr(struct socket *so, struct sockaddr **nam)
{
	return(raw_usrreqs.pru_peeraddr(so, nam));
}

/*
 * key_send()
 * derived from net/rtsock.c:rts_send()
 */
static int
key_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *nam,
	 struct mbuf *control, struct thread *td)
{
	return(raw_usrreqs.pru_send(so, flags, m, nam, control, td));
}

/*
 * key_shutdown()
 * derived from net/rtsock.c:rts_shutdown()
 */
static int
key_shutdown(struct socket *so)
{
	return(raw_usrreqs.pru_shutdown(so));
}

/*
 * key_sockaddr()
 * derived from net/rtsock.c:rts_sockaddr()
 */
static int
key_sockaddr(struct socket *so, struct sockaddr **nam)
{
	return(raw_usrreqs.pru_sockaddr(so, nam));
}

struct pr_usrreqs key_usrreqs = {
	.pru_abort =		key_abort,
	.pru_attach =		key_attach,
	.pru_bind =		key_bind,
	.pru_connect =		key_connect,
	.pru_detach =		key_detach,
	.pru_disconnect =	key_disconnect,
	.pru_peeraddr =		key_peeraddr,
	.pru_send =		key_send,
	.pru_shutdown =		key_shutdown,
	.pru_sockaddr =		key_sockaddr,
	.pru_close =		key_close,
};

/* sysctl */
SYSCTL_NODE(_net, PF_KEY, key, CTLFLAG_RW, 0, "Key Family");

/*
 * Definitions of protocols supported in the KEY domain.
 */

extern struct domain keydomain;

struct protosw keysw[] = {
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&keydomain,
	.pr_protocol =		PF_KEY_V2,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_output =		key_output,
	.pr_ctlinput =		raw_ctlinput,
	.pr_init =		raw_init,
	.pr_usrreqs =		&key_usrreqs
}
};

static void
key_init0(void)
{

	bzero((caddr_t)&V_key_cb, sizeof(V_key_cb));
	key_init();
}

struct domain keydomain = {
	.dom_family =		PF_KEY,
	.dom_name =		"key",
	.dom_init =		key_init0,
#ifdef VIMAGE
	.dom_destroy =		key_destroy,
#endif
	.dom_protosw =		keysw,
	.dom_protoswNPROTOSW =	&keysw[nitems(keysw)]
};

VNET_DOMAIN_SET(key);
