/*-
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/syslog.h>
#include <sys/proc.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <netipsec/ipsec_support.h>
#include <netipsec/ipsec.h>
#include <netipsec/ipsec6.h>
#include <netipsec/key.h>
#include <netipsec/key_debug.h>
#include <netipsec/xform.h>

#include <machine/atomic.h>
/*
 * This file is build in the kernel only when 'options IPSEC' or
 * 'options IPSEC_SUPPORT' is enabled.
 */

#ifdef INET
void
ipsec4_setsockaddrs(const struct mbuf *m, union sockaddr_union *src,
    union sockaddr_union *dst)
{
	static const struct sockaddr_in template = {
		sizeof (struct sockaddr_in),
		AF_INET,
		0, { 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }
	};

	src->sin = template;
	dst->sin = template;

	if (m->m_len < sizeof (struct ip)) {
		m_copydata(m, offsetof(struct ip, ip_src),
			   sizeof (struct  in_addr),
			   (caddr_t) &src->sin.sin_addr);
		m_copydata(m, offsetof(struct ip, ip_dst),
			   sizeof (struct  in_addr),
			   (caddr_t) &dst->sin.sin_addr);
	} else {
		const struct ip *ip = mtod(m, const struct ip *);
		src->sin.sin_addr = ip->ip_src;
		dst->sin.sin_addr = ip->ip_dst;
	}
}
#endif
#ifdef INET6
void
ipsec6_setsockaddrs(const struct mbuf *m, union sockaddr_union *src,
    union sockaddr_union *dst)
{
	struct ip6_hdr ip6buf;
	const struct ip6_hdr *ip6;

	if (m->m_len >= sizeof(*ip6))
		ip6 = mtod(m, const struct ip6_hdr *);
	else {
		m_copydata(m, 0, sizeof(ip6buf), (caddr_t)&ip6buf);
		ip6 = &ip6buf;
	}

	bzero(&src->sin6, sizeof(struct sockaddr_in6));
	src->sin6.sin6_family = AF_INET6;
	src->sin6.sin6_len = sizeof(struct sockaddr_in6);
	bcopy(&ip6->ip6_src, &src->sin6.sin6_addr, sizeof(ip6->ip6_src));
	if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_src)) {
		src->sin6.sin6_addr.s6_addr16[1] = 0;
		src->sin6.sin6_scope_id = ntohs(ip6->ip6_src.s6_addr16[1]);
	}

	bzero(&dst->sin6, sizeof(struct sockaddr_in6));
	dst->sin6.sin6_family = AF_INET6;
	dst->sin6.sin6_len = sizeof(struct sockaddr_in6);
	bcopy(&ip6->ip6_dst, &dst->sin6.sin6_addr, sizeof(ip6->ip6_dst));
	if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_dst)) {
		dst->sin6.sin6_addr.s6_addr16[1] = 0;
		dst->sin6.sin6_scope_id = ntohs(ip6->ip6_dst.s6_addr16[1]);
	}
}
#endif

#define	IPSEC_MODULE_INCR	2
static int
ipsec_kmod_enter(volatile u_int *cntr)
{
	u_int old, new;

	do {
		old = *cntr;
		if ((old & IPSEC_MODULE_ENABLED) == 0)
			return (ENXIO);
		new = old + IPSEC_MODULE_INCR;
	} while(atomic_cmpset_acq_int(cntr, old, new) == 0);
	return (0);
}

static void
ipsec_kmod_exit(volatile u_int *cntr)
{
	u_int old, new;

	do {
		old = *cntr;
		new = old - IPSEC_MODULE_INCR;
	} while (atomic_cmpset_rel_int(cntr, old, new) == 0);
}

static void
ipsec_kmod_drain(volatile u_int *cntr)
{
	u_int old, new;

	do {
		old = *cntr;
		new = old & ~IPSEC_MODULE_ENABLED;
	} while (atomic_cmpset_acq_int(cntr, old, new) == 0);
	while (atomic_cmpset_int(cntr, 0, 0) == 0)
		pause("ipsecd", hz/2);
}

static LIST_HEAD(xforms_list, xformsw) xforms = LIST_HEAD_INITIALIZER();
static struct mtx xforms_lock;
MTX_SYSINIT(xfroms_list, &xforms_lock, "IPsec transforms list", MTX_DEF);
#define	XFORMS_LOCK()		mtx_lock(&xforms_lock)
#define	XFORMS_UNLOCK()		mtx_unlock(&xforms_lock)

void
xform_attach(void *data)
{
	struct xformsw *xsp, *entry;

	xsp = (struct xformsw *)data;
	XFORMS_LOCK();
	LIST_FOREACH(entry, &xforms, chain) {
		if (entry->xf_type == xsp->xf_type) {
			XFORMS_UNLOCK();
			printf("%s: failed to register %s xform\n",
			    __func__, xsp->xf_name);
			return;
		}
	}
	LIST_INSERT_HEAD(&xforms, xsp, chain);
	xsp->xf_cntr = IPSEC_MODULE_ENABLED;
	XFORMS_UNLOCK();
}

void
xform_detach(void *data)
{
	struct xformsw *xsp = (struct xformsw *)data;

	XFORMS_LOCK();
	LIST_REMOVE(xsp, chain);
	XFORMS_UNLOCK();

	/* Delete all SAs related to this xform. */
	key_delete_xform(xsp);
	if (xsp->xf_cntr & IPSEC_MODULE_ENABLED)
		ipsec_kmod_drain(&xsp->xf_cntr);
}

/*
 * Initialize transform support in an sav.
 */
int
xform_init(struct secasvar *sav, u_short xftype)
{
	struct xformsw *entry;
	int ret;

	IPSEC_ASSERT(sav->tdb_xform == NULL,
	    ("tdb_xform is already initialized"));

	XFORMS_LOCK();
	LIST_FOREACH(entry, &xforms, chain) {
		if (entry->xf_type == xftype) {
			ret = ipsec_kmod_enter(&entry->xf_cntr);
			XFORMS_UNLOCK();
			if (ret != 0)
				return (ret);
			ret = (*entry->xf_init)(sav, entry);
			ipsec_kmod_exit(&entry->xf_cntr);
			return (ret);
		}
	}
	XFORMS_UNLOCK();
	return (EINVAL);
}

#ifdef IPSEC_SUPPORT
/*
 * IPSEC_SUPPORT - loading of ipsec.ko and tcpmd5.ko is supported.
 * IPSEC + IPSEC_SUPPORT - loading tcpmd5.ko is supported.
 * IPSEC + TCP_SIGNATURE - all is build in the kernel, do not build
 *   IPSEC_SUPPORT.
 */
#if !defined(IPSEC) || !defined(TCP_SIGNATURE)
#define	METHOD_DECL(...)	__VA_ARGS__
#define	METHOD_ARGS(...)	__VA_ARGS__
#define	IPSEC_KMOD_METHOD(type, name, sc, method, decl, args)		\
type name (decl)							\
{									\
	type ret = (type)ipsec_kmod_enter(&sc->enabled);		\
	if (ret == 0) {							\
		ret = (*sc->methods->method)(args);			\
		ipsec_kmod_exit(&sc->enabled);				\
	}								\
	return (ret);							\
}

static int
ipsec_support_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		return (0);
	case MOD_UNLOAD:
		return (EBUSY);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t ipsec_support_mod = {
	"ipsec_support",
	ipsec_support_modevent,
	0
};
DECLARE_MODULE(ipsec_support, ipsec_support_mod, SI_SUB_PROTO_DOMAIN,
    SI_ORDER_ANY);
MODULE_VERSION(ipsec_support, 1);
#endif /* !IPSEC || !TCP_SIGNATURE */

#ifndef TCP_SIGNATURE
/* Declare TCP-MD5 support as kernel module. */
static struct tcpmd5_support tcpmd5_ipsec = {
	.enabled = 0,
	.methods = NULL
};
struct tcpmd5_support * const tcp_ipsec_support = &tcpmd5_ipsec;

IPSEC_KMOD_METHOD(int, tcpmd5_kmod_input, sc,
    input, METHOD_DECL(struct tcpmd5_support * const sc, struct mbuf *m,
	struct tcphdr *th, u_char *buf), METHOD_ARGS(m, th, buf)
)

IPSEC_KMOD_METHOD(int, tcpmd5_kmod_output, sc,
    output, METHOD_DECL(struct tcpmd5_support * const sc, struct mbuf *m,
	struct tcphdr *th, u_char *buf), METHOD_ARGS(m, th, buf)
)

IPSEC_KMOD_METHOD(int, tcpmd5_kmod_pcbctl, sc,
    pcbctl, METHOD_DECL(struct tcpmd5_support * const sc, struct inpcb *inp,
	struct sockopt *sopt), METHOD_ARGS(inp, sopt)
)

void
tcpmd5_support_enable(const struct tcpmd5_methods * const methods)
{

	KASSERT(tcp_ipsec_support->enabled == 0, ("TCP-MD5 already enabled"));
	tcp_ipsec_support->methods = methods;
	tcp_ipsec_support->enabled |= IPSEC_MODULE_ENABLED;
}

void
tcpmd5_support_disable(void)
{

	if (tcp_ipsec_support->enabled & IPSEC_MODULE_ENABLED) {
		ipsec_kmod_drain(&tcp_ipsec_support->enabled);
		tcp_ipsec_support->methods = NULL;
	}
}
#endif /* !TCP_SIGNATURE */

#ifndef IPSEC
/*
 * IPsec support is build as kernel module.
 */
#ifdef INET
static struct ipsec_support ipv4_ipsec = {
	.enabled = 0,
	.methods = NULL
};
struct ipsec_support * const ipv4_ipsec_support = &ipv4_ipsec;

IPSEC_KMOD_METHOD(int, ipsec_kmod_udp_input, sc,
    udp_input, METHOD_DECL(struct ipsec_support * const sc, struct mbuf *m,
	int off, int af), METHOD_ARGS(m, off, af)
)

IPSEC_KMOD_METHOD(int, ipsec_kmod_udp_pcbctl, sc,
    udp_pcbctl, METHOD_DECL(struct ipsec_support * const sc, struct inpcb *inp,
	struct sockopt *sopt), METHOD_ARGS(inp, sopt)
)
#endif

#ifdef INET6
static struct ipsec_support ipv6_ipsec = {
	.enabled = 0,
	.methods = NULL
};
struct ipsec_support * const ipv6_ipsec_support = &ipv6_ipsec;
#endif

IPSEC_KMOD_METHOD(int, ipsec_kmod_input, sc,
    input, METHOD_DECL(struct ipsec_support * const sc, struct mbuf *m,
	int offset, int proto), METHOD_ARGS(m, offset, proto)
)

IPSEC_KMOD_METHOD(int, ipsec_kmod_check_policy, sc,
    check_policy, METHOD_DECL(struct ipsec_support * const sc, struct mbuf *m,
	struct inpcb *inp), METHOD_ARGS(m, inp)
)

IPSEC_KMOD_METHOD(int, ipsec_kmod_forward, sc,
    forward, METHOD_DECL(struct ipsec_support * const sc, struct mbuf *m),
    (m)
)

IPSEC_KMOD_METHOD(int, ipsec_kmod_output, sc,
    output, METHOD_DECL(struct ipsec_support * const sc, struct mbuf *m,
	struct inpcb *inp), METHOD_ARGS(m, inp)
)

IPSEC_KMOD_METHOD(int, ipsec_kmod_pcbctl, sc,
    pcbctl, METHOD_DECL(struct ipsec_support * const sc, struct inpcb *inp,
	struct sockopt *sopt), METHOD_ARGS(inp, sopt)
)

IPSEC_KMOD_METHOD(size_t, ipsec_kmod_hdrsize, sc,
    hdrsize, METHOD_DECL(struct ipsec_support * const sc, struct inpcb *inp),
    (inp)
)

static IPSEC_KMOD_METHOD(int, ipsec_kmod_caps, sc,
    capability, METHOD_DECL(struct ipsec_support * const sc, struct mbuf *m,
	u_int cap), METHOD_ARGS(m, cap)
)

int
ipsec_kmod_capability(struct ipsec_support * const sc, struct mbuf *m,
    u_int cap)
{

	/*
	 * Since PF_KEY is build in the kernel, we can directly
	 * call key_havesp() without additional synchronizations.
	 */
	if (cap == IPSEC_CAP_OPERABLE)
		return (key_havesp(IPSEC_DIR_INBOUND) != 0 ||
		    key_havesp(IPSEC_DIR_OUTBOUND) != 0);
	return (ipsec_kmod_caps(sc, m, cap));
}

void
ipsec_support_enable(struct ipsec_support * const sc,
    const struct ipsec_methods * const methods)
{

	KASSERT(sc->enabled == 0, ("IPsec already enabled"));
	sc->methods = methods;
	sc->enabled |= IPSEC_MODULE_ENABLED;
}

void
ipsec_support_disable(struct ipsec_support * const sc)
{

	if (sc->enabled & IPSEC_MODULE_ENABLED) {
		ipsec_kmod_drain(&sc->enabled);
		sc->methods = NULL;
	}
}
#endif /* !IPSEC */
#endif /* IPSEC_SUPPORT */
