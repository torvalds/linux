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
 *
 * $FreeBSD$
 */

#ifndef _NETIPSEC_IPSEC_SUPPORT_H_
#define	_NETIPSEC_IPSEC_SUPPORT_H_

#ifdef _KERNEL
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
struct mbuf;
struct inpcb;
struct tcphdr;
struct sockopt;
struct sockaddr;
struct ipsec_support;
struct tcpmd5_support;

size_t ipsec_hdrsiz_inpcb(struct inpcb *);
int ipsec_init_pcbpolicy(struct inpcb *);
int ipsec_delete_pcbpolicy(struct inpcb *);
int ipsec_copy_pcbpolicy(struct inpcb *, struct inpcb *);

struct ipsec_methods {
	int	(*input)(struct mbuf *, int, int);
	int	(*check_policy)(const struct mbuf *, struct inpcb *);
	int	(*forward)(struct mbuf *);
	int	(*output)(struct mbuf *, struct inpcb *);
	int	(*pcbctl)(struct inpcb *, struct sockopt *);
	size_t	(*hdrsize)(struct inpcb *);
	int	(*capability)(struct mbuf *, u_int);
	int	(*ctlinput)(int, struct sockaddr *, void *);

	int	(*udp_input)(struct mbuf *, int, int);
	int	(*udp_pcbctl)(struct inpcb *, struct sockopt *);
};
#define	IPSEC_CAP_OPERABLE		1
#define	IPSEC_CAP_BYPASS_FILTER		2

struct tcpmd5_methods {
	int	(*input)(struct mbuf *, struct tcphdr *, u_char *);
	int	(*output)(struct mbuf *, struct tcphdr *, u_char *);
	int	(*pcbctl)(struct inpcb *, struct sockopt *);
};

#define	IPSEC_MODULE_ENABLED	0x0001
#define	IPSEC_ENABLED(proto)	\
    ((proto ## _ipsec_support)->enabled & IPSEC_MODULE_ENABLED)
#define	TCPMD5_ENABLED()	IPSEC_ENABLED(tcp)

#ifdef TCP_SIGNATURE
/* TCP-MD5 build in the kernel */
struct tcpmd5_support {
	const u_int enabled;
	const struct tcpmd5_methods * const methods;
};
extern const struct tcpmd5_support * const tcp_ipsec_support;

#define	TCPMD5_INPUT(m, ...)		\
    (*tcp_ipsec_support->methods->input)(m, __VA_ARGS__)
#define	TCPMD5_OUTPUT(m, ...)		\
    (*tcp_ipsec_support->methods->output)(m, __VA_ARGS__)
#define	TCPMD5_PCBCTL(inp, sopt)	\
    (*tcp_ipsec_support->methods->pcbctl)(inp, sopt)
#elif defined(IPSEC_SUPPORT)
/* TCP-MD5 build as module */
struct tcpmd5_support {
	volatile u_int enabled;
	const struct tcpmd5_methods * volatile methods;
};
extern struct tcpmd5_support * const tcp_ipsec_support;

void tcpmd5_support_enable(const struct tcpmd5_methods * const);
void tcpmd5_support_disable(void);

int tcpmd5_kmod_pcbctl(struct tcpmd5_support * const, struct inpcb *,
    struct sockopt *);
int tcpmd5_kmod_input(struct tcpmd5_support * const, struct mbuf *,
    struct tcphdr *, u_char *);
int tcpmd5_kmod_output(struct tcpmd5_support * const, struct mbuf *,
    struct tcphdr *, u_char *);
#define	TCPMD5_INPUT(m, ...)		\
    tcpmd5_kmod_input(tcp_ipsec_support, m, __VA_ARGS__)
#define	TCPMD5_OUTPUT(m, ...)		\
    tcpmd5_kmod_output(tcp_ipsec_support, m, __VA_ARGS__)
#define	TCPMD5_PCBCTL(inp, sopt)	\
    tcpmd5_kmod_pcbctl(tcp_ipsec_support, inp, sopt)
#endif

#endif /* IPSEC || IPSEC_SUPPORT */

#if defined(IPSEC)
struct ipsec_support {
	const u_int enabled;
	const struct ipsec_methods * const methods;
};
extern const struct ipsec_support * const ipv4_ipsec_support;
extern const struct ipsec_support * const ipv6_ipsec_support;

#define	IPSEC_INPUT(proto, m, ...)		\
    (*(proto ## _ipsec_support)->methods->input)(m, __VA_ARGS__)
#define	IPSEC_CHECK_POLICY(proto, m, ...)	\
    (*(proto ## _ipsec_support)->methods->check_policy)(m, __VA_ARGS__)
#define	IPSEC_FORWARD(proto, m)		\
    (*(proto ## _ipsec_support)->methods->forward)(m)
#define	IPSEC_OUTPUT(proto, m, ...)		\
    (*(proto ## _ipsec_support)->methods->output)(m, __VA_ARGS__)
#define	IPSEC_PCBCTL(proto, inp, sopt)		\
    (*(proto ## _ipsec_support)->methods->pcbctl)(inp, sopt)
#define	IPSEC_CAPS(proto, m, ...)		\
    (*(proto ## _ipsec_support)->methods->capability)(m, __VA_ARGS__)
#define	IPSEC_HDRSIZE(proto, inp)		\
    (*(proto ## _ipsec_support)->methods->hdrsize)(inp)

#define	UDPENCAP_INPUT(m, ...)			\
    (*ipv4_ipsec_support->methods->udp_input)(m, __VA_ARGS__)
#define	UDPENCAP_PCBCTL(inp, sopt)		\
    (*ipv4_ipsec_support->methods->udp_pcbctl)(inp, sopt)

#elif defined(IPSEC_SUPPORT)
struct ipsec_support {
	volatile u_int enabled;
	const struct ipsec_methods * volatile methods;
};
extern struct ipsec_support * const ipv4_ipsec_support;
extern struct ipsec_support * const ipv6_ipsec_support;

void ipsec_support_enable(struct ipsec_support * const,
    const struct ipsec_methods * const);
void ipsec_support_disable(struct ipsec_support * const);

int ipsec_kmod_input(struct ipsec_support * const, struct mbuf *, int, int);
int ipsec_kmod_check_policy(struct ipsec_support * const, struct mbuf *,
    struct inpcb *);
int ipsec_kmod_forward(struct ipsec_support * const, struct mbuf *);
int ipsec_kmod_output(struct ipsec_support * const, struct mbuf *,
    struct inpcb *);
int ipsec_kmod_pcbctl(struct ipsec_support * const, struct inpcb *,
    struct sockopt *);
int ipsec_kmod_capability(struct ipsec_support * const, struct mbuf *, u_int);
size_t ipsec_kmod_hdrsize(struct ipsec_support * const, struct inpcb *);
int ipsec_kmod_udp_input(struct ipsec_support * const, struct mbuf *, int, int);
int ipsec_kmod_udp_pcbctl(struct ipsec_support * const, struct inpcb *,
    struct sockopt *);

#define	UDPENCAP_INPUT(m, ...)		\
    ipsec_kmod_udp_input(ipv4_ipsec_support, m, __VA_ARGS__)
#define	UDPENCAP_PCBCTL(inp, sopt)	\
    ipsec_kmod_udp_pcbctl(ipv4_ipsec_support, inp, sopt)

#define	IPSEC_INPUT(proto, ...)		\
    ipsec_kmod_input(proto ## _ipsec_support, __VA_ARGS__)
#define	IPSEC_CHECK_POLICY(proto, ...)	\
    ipsec_kmod_check_policy(proto ## _ipsec_support, __VA_ARGS__)
#define	IPSEC_FORWARD(proto, ...)	\
    ipsec_kmod_forward(proto ## _ipsec_support, __VA_ARGS__)
#define	IPSEC_OUTPUT(proto, ...)	\
    ipsec_kmod_output(proto ## _ipsec_support, __VA_ARGS__)
#define	IPSEC_PCBCTL(proto, ...)	\
    ipsec_kmod_pcbctl(proto ## _ipsec_support, __VA_ARGS__)
#define	IPSEC_CAPS(proto, ...)		\
    ipsec_kmod_capability(proto ## _ipsec_support, __VA_ARGS__)
#define	IPSEC_HDRSIZE(proto, ...)	\
    ipsec_kmod_hdrsize(proto ## _ipsec_support, __VA_ARGS__)
#endif /* IPSEC_SUPPORT */
#endif /* _KERNEL */
#endif /* _NETIPSEC_IPSEC_SUPPORT_H_ */
