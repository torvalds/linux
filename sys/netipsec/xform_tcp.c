/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Bruce M. Simpson <bms@spc.org>
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
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

/* TCP MD5 Signature Option (RFC2385) */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/md5.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/protosw.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>

#include <net/vnet.h>

#include <netipsec/ipsec.h>
#include <netipsec/ipsec_support.h>
#include <netipsec/xform.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netipsec/ipsec6.h>
#endif

#include <netipsec/key.h>
#include <netipsec/key_debug.h>

#define	TCP_SIGLEN	16	/* length of computed digest in bytes */
#define	TCP_KEYLEN_MIN	1	/* minimum length of TCP-MD5 key */
#define	TCP_KEYLEN_MAX	80	/* maximum length of TCP-MD5 key */

static int
tcp_ipsec_pcbctl(struct inpcb *inp, struct sockopt *sopt)
{
	struct tcpcb *tp;
	int error, optval;

	if (sopt->sopt_name != TCP_MD5SIG) {
		return (ENOPROTOOPT);
	}

	if (sopt->sopt_dir == SOPT_GET) {
		INP_RLOCK(inp);
		if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
			INP_RUNLOCK(inp);
			return (ECONNRESET);
		}
		tp = intotcpcb(inp);
		optval = (tp->t_flags & TF_SIGNATURE) ? 1 : 0;
		INP_RUNLOCK(inp);

		/* On success return with released INP_WLOCK */
		return (sooptcopyout(sopt, &optval, sizeof(optval)));
	}

	error = sooptcopyin(sopt, &optval, sizeof(optval), sizeof(optval));
	if (error != 0)
		return (error);

	/* INP_WLOCK_RECHECK */
	INP_WLOCK(inp);
	if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
		INP_WUNLOCK(inp);
		return (ECONNRESET);
	}
	tp = intotcpcb(inp);
	if (optval > 0)
		tp->t_flags |= TF_SIGNATURE;
	else
		tp->t_flags &= ~TF_SIGNATURE;

	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Callback function invoked by m_apply() to digest TCP segment data
 * contained within an mbuf chain.
 */
static int
tcp_signature_apply(void *fstate, void *data, u_int len)
{

	MD5Update(fstate, (u_char *)data, len);
	return (0);
}

#ifdef INET
static int
ip_pseudo_compute(struct mbuf *m, MD5_CTX *ctx)
{
	struct ippseudo ipp;
	struct ip *ip;

	ip = mtod(m, struct ip *);
	ipp.ippseudo_src.s_addr = ip->ip_src.s_addr;
	ipp.ippseudo_dst.s_addr = ip->ip_dst.s_addr;
	ipp.ippseudo_p = IPPROTO_TCP;
	ipp.ippseudo_pad = 0;
	ipp.ippseudo_len = htons(m->m_pkthdr.len - (ip->ip_hl << 2));
	MD5Update(ctx, (char *)&ipp, sizeof(ipp));
	return (ip->ip_hl << 2);
}
#endif

#ifdef INET6
static int
ip6_pseudo_compute(struct mbuf *m, MD5_CTX *ctx)
{
	struct ip6_pseudo {
		struct in6_addr src, dst;
		uint32_t len;
		uint32_t nxt;
	} ip6p __aligned(4);
	struct ip6_hdr *ip6;

	ip6 = mtod(m, struct ip6_hdr *);
	ip6p.src = ip6->ip6_src;
	ip6p.dst = ip6->ip6_dst;
	ip6p.len = htonl(m->m_pkthdr.len - sizeof(*ip6)); /* XXX: ext headers */
	ip6p.nxt = htonl(IPPROTO_TCP);
	MD5Update(ctx, (char *)&ip6p, sizeof(ip6p));
	return (sizeof(*ip6));
}
#endif

static int
tcp_signature_compute(struct mbuf *m, struct tcphdr *th,
    struct secasvar *sav, u_char *buf)
{
	MD5_CTX ctx;
	int len;
	u_short csum;

	MD5Init(&ctx);
	 /* Step 1: Update MD5 hash with IP(v6) pseudo-header. */
	switch (sav->sah->saidx.dst.sa.sa_family) {
#ifdef INET
	case AF_INET:
		len = ip_pseudo_compute(m, &ctx);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		len = ip6_pseudo_compute(m, &ctx);
		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}
	/*
	 * Step 2: Update MD5 hash with TCP header, excluding options.
	 * The TCP checksum must be set to zero.
	 */
	csum = th->th_sum;
	th->th_sum = 0;
	MD5Update(&ctx, (char *)th, sizeof(struct tcphdr));
	th->th_sum = csum;
	/*
	 * Step 3: Update MD5 hash with TCP segment data.
	 * Use m_apply() to avoid an early m_pullup().
	 */
	len += (th->th_off << 2);
	if (m->m_pkthdr.len - len > 0)
		m_apply(m, len, m->m_pkthdr.len - len,
		    tcp_signature_apply, &ctx);
	/*
	 * Step 4: Update MD5 hash with shared secret.
	 */
	MD5Update(&ctx, sav->key_auth->key_data, _KEYLEN(sav->key_auth));
	MD5Final(buf, &ctx);
	key_sa_recordxfer(sav, m);
	return (0);
}

static void
setsockaddrs(const struct mbuf *m, union sockaddr_union *src,
    union sockaddr_union *dst)
{
	struct ip *ip;

	IPSEC_ASSERT(m->m_len >= sizeof(*ip), ("unexpected mbuf len"));

	ip = mtod(m, struct ip *);
	switch (ip->ip_v) {
#ifdef INET
	case IPVERSION:
		ipsec4_setsockaddrs(m, src, dst);
		break;
#endif
#ifdef INET6
	case (IPV6_VERSION >> 4):
		ipsec6_setsockaddrs(m, src, dst);
		break;
#endif
	default:
		bzero(src, sizeof(*src));
		bzero(dst, sizeof(*dst));
	}
}

/*
 * Compute TCP-MD5 hash of an *INBOUND* TCP segment.
 * Parameters:
 * m		pointer to head of mbuf chain
 * th		pointer to TCP header
 * buf		pointer to storage for computed MD5 digest
 *
 * Return 0 if successful, otherwise return -1.
 */
static int
tcp_ipsec_input(struct mbuf *m, struct tcphdr *th, u_char *buf)
{
	char tmpdigest[TCP_SIGLEN];
	struct secasindex saidx;
	struct secasvar *sav;

	setsockaddrs(m, &saidx.src, &saidx.dst);
	saidx.proto = IPPROTO_TCP;
	saidx.mode = IPSEC_MODE_TCPMD5;
	saidx.reqid = 0;
	sav = key_allocsa_tcpmd5(&saidx);
	if (sav == NULL) {
		KMOD_TCPSTAT_INC(tcps_sig_err_buildsig);
		return (EACCES);
	}
	/*
	 * tcp_input() operates with TCP header fields in host
	 * byte order. We expect them in network byte order.
	 */
	tcp_fields_to_net(th);
	tcp_signature_compute(m, th, sav, tmpdigest);
	tcp_fields_to_host(th);
	key_freesav(&sav);
	if (bcmp(buf, tmpdigest, TCP_SIGLEN) != 0) {
		KMOD_TCPSTAT_INC(tcps_sig_rcvbadsig);
		return (EACCES);
	}
	KMOD_TCPSTAT_INC(tcps_sig_rcvgoodsig);
	return (0);
}

/*
 * Compute TCP-MD5 hash of an *OUTBOUND* TCP segment.
 * Parameters:
 * m		pointer to head of mbuf chain
 * th		pointer to TCP header
 * buf		pointer to storage for computed MD5 digest
 *
 * Return 0 if successful, otherwise return error code.
 */
static int
tcp_ipsec_output(struct mbuf *m, struct tcphdr *th, u_char *buf)
{
	struct secasindex saidx;
	struct secasvar *sav;

	setsockaddrs(m, &saidx.src, &saidx.dst);
	saidx.proto = IPPROTO_TCP;
	saidx.mode = IPSEC_MODE_TCPMD5;
	saidx.reqid = 0;
	sav = key_allocsa_tcpmd5(&saidx);
	if (sav == NULL) {
		KMOD_TCPSTAT_INC(tcps_sig_err_buildsig);
		return (EACCES);
	}
	tcp_signature_compute(m, th, sav, buf);
	key_freesav(&sav);
	return (0);
}

/*
 * Initialize a TCP-MD5 SA. Called when the SA is being set up.
 *
 * We don't need to set up the tdb prefixed fields, as we don't use the
 * opencrypto code; we just perform a key length check.
 *
 * XXX: Currently we have used single 'magic' SPI and need to still
 * support this.
 *
 * This allows per-host granularity without affecting the userland
 * interface, which is a simple socket option toggle switch,
 * TCP_SIGNATURE_ENABLE.
 *
 * To allow per-service granularity requires that we have a means
 * of mapping port to SPI. The mandated way of doing this is to
 * use SPD entries to specify packet flows which get the TCP-MD5
 * treatment, however the code to do this is currently unstable
 * and unsuitable for production use.
 *
 * Therefore we use this compromise in the meantime.
 */
static int
tcpsignature_init(struct secasvar *sav, struct xformsw *xsp)
{
	int keylen;

	if (sav->alg_auth != SADB_X_AALG_TCP_MD5) {
		DPRINTF(("%s: unsupported authentication algorithm %u\n",
		    __func__, sav->alg_auth));
		return (EINVAL);
	}
	if (sav->key_auth == NULL) {
		DPRINTF(("%s: no authentication key present\n", __func__));
		return (EINVAL);
	}
	keylen = _KEYLEN(sav->key_auth);
	if ((keylen < TCP_KEYLEN_MIN) || (keylen > TCP_KEYLEN_MAX)) {
		DPRINTF(("%s: invalid key length %u\n", __func__, keylen));
		return (EINVAL);
	}
	sav->tdb_xform = xsp;
	return (0);
}

/*
 * Called when the SA is deleted.
 */
static int
tcpsignature_zeroize(struct secasvar *sav)
{

	if (sav->key_auth != NULL)
		bzero(sav->key_auth->key_data, _KEYLEN(sav->key_auth));
	sav->tdb_xform = NULL;
	return (0);
}

static struct xformsw tcpsignature_xformsw = {
	.xf_type =	XF_TCPSIGNATURE,
	.xf_name =	"TCP-MD5",
	.xf_init =	tcpsignature_init,
	.xf_zeroize =	tcpsignature_zeroize,
};

static const struct tcpmd5_methods tcpmd5_methods = {
	.input = tcp_ipsec_input,
	.output = tcp_ipsec_output,
	.pcbctl = tcp_ipsec_pcbctl,
};

#ifndef KLD_MODULE
/* TCP-MD5 support is build in the kernel */
static const struct tcpmd5_support tcpmd5_ipsec = {
	.enabled = IPSEC_MODULE_ENABLED,
	.methods = &tcpmd5_methods
};
const struct tcpmd5_support * const tcp_ipsec_support = &tcpmd5_ipsec;
#endif /* !KLD_MODULE */

static int
tcpmd5_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		xform_attach(&tcpsignature_xformsw);
#ifdef KLD_MODULE
		tcpmd5_support_enable(&tcpmd5_methods);
#endif
		break;
	case MOD_UNLOAD:
#ifdef KLD_MODULE
		tcpmd5_support_disable();
#endif
		xform_detach(&tcpsignature_xformsw);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t tcpmd5_mod = {
	"tcpmd5",
	tcpmd5_modevent,
	0
};

DECLARE_MODULE(tcpmd5, tcpmd5_mod, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY);
MODULE_VERSION(tcpmd5, 1);
#ifdef KLD_MODULE
MODULE_DEPEND(tcpmd5, ipsec_support, 1, 1, 1);
#endif
