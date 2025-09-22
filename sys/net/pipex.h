/*	$OpenBSD: pipex.h,v 1.35 2025/03/02 21:28:32 bluhm Exp $	*/

/*
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef NET_PIPEX_H
#define NET_PIPEX_H 1

/*
 * Names for pipex sysctl objects
 */
#define PIPEXCTL_ENABLE		1
#define PIPEXCTL_MAXID		2

#define PIPEXCTL_NAMES { \
        { 0, 0 }, \
        { "enable", CTLTYPE_INT }, \
}

#define PIPEX_PROTO_L2TP		1	/* protocol L2TP */
#define PIPEX_PROTO_PPTP		2	/* protocol PPTP */
#define PIPEX_PROTO_PPPOE		3	/* protocol PPPoE */
#define PIPEX_MAX_LISTREQ		128	/* list request size */
#define	PIPEX_MPPE_KEYLEN		16

/* pipex_mppe */
struct pipex_mppe_req {
	int16_t	stateless;			/* mppe key mode.
						   1 for stateless */
	int16_t	keylenbits;			/* mppe key length(in bits)*/
	u_char	master_key[PIPEX_MPPE_KEYLEN];	/* mppe master key */
};

/* pipex statistics */
struct pipex_statistics {
	uint32_t ipackets;      /* packets received from tunnel */
	uint32_t ierrors;       /* error packets received from tunnel */
	uint64_t ibytes;        /* number of received bytes from tunnel */
	uint32_t opackets;      /* packets sent to tunnel */
	uint32_t oerrors;       /* error packets on sending to tunnel */
	uint64_t obytes;        /* number of sent bytes to tunnel */

	uint32_t idle_time;     /* idle time in seconds */
};

struct pipex_session_req {
	int		pr_protocol;		/* tunnel protocol  */
/*	u_int		pr_rdomain;	*/	/* rdomain id */
	uint16_t	pr_session_id;		/* session-id */
	uint16_t	pr_peer_session_id;	/* peer's session-id */
	uint32_t	pr_ppp_flags;	/* PPP configuration flags */
#define	PIPEX_PPP_ACFC_ACCEPTED		0x0001	/* ACFC accepted */
#define	PIPEX_PPP_PFC_ACCEPTED		0x0002	/* PFC accepted */
#define	PIPEX_PPP_ACFC_ENABLED		0x0004	/* ACFC enabled */
#define	PIPEX_PPP_PFC_ENABLED		0x0008	/* PFC enabled */
#define	PIPEX_PPP_MPPE_ACCEPTED		0x0010	/* MPPE accepted */
#define	PIPEX_PPP_MPPE_ENABLED		0x0020	/* MPPE enabled */
#define	PIPEX_PPP_MPPE_REQUIRED		0x0040	/* MPPE is required */
#define	PIPEX_PPP_HAS_ACF		0x0080	/* has ACF */
#define	PIPEX_PPP_ADJUST_TCPMSS		0x0100	/* do tcpmss adjustment */
#define	PIPEX_PPP_INGRESS_FILTER	0x0200	/* do ingress filter */
	int8_t		pr_ccp_id;		/* CCP current packet id */
	int		pr_ppp_id;		/* PPP Id. */
	uint16_t	pr_peer_mru;		/* Peer's MRU */
	uint32_t	pr_timeout_sec;		/* Idle Timer */

	struct in_addr	pr_ip_srcaddr;		/* local framed IP-Address */
	struct in_addr	pr_ip_address;		/* framed IP-Address */
	struct in_addr	pr_ip_netmask;		/* framed IP-Netmask */
	struct sockaddr_in6 pr_ip6_address;	/* framed IPv6-Address */
	int		pr_ip6_prefixlen;	/* framed IPv6-Prefixlen */
	union {
		struct {
			uint32_t snd_nxt;	/* send next */
			uint32_t rcv_nxt;	/* receive next */
			uint32_t snd_una;	/* unacked */
			uint32_t rcv_acked;	/* recv acked */
			int winsz;		/* window size */
			int maxwinsz;		/* max window size */
			int peer_maxwinsz;	/* peer's max window size */
		} pptp;
		struct {
			uint32_t option_flags;
#define	PIPEX_L2TP_USE_SEQUENCING	0x00000001

			uint16_t tunnel_id;	/* our tunnel-id */
			uint16_t peer_tunnel_id;/* peer's tunnel-id */
			uint32_t ns_nxt;	/* send next */
			uint32_t nr_nxt;	/* receive next */
			uint32_t ns_una;	/* unacked */
			uint32_t nr_acked;	/* recv acked */
			uint32_t ipsecflowinfo;	/* IPsec flow id for NAT-T */
		} l2tp;
		struct {
			char over_ifname[IF_NAMESIZE];	/* ethernet ifname */
		} pppoe;
	} pr_proto;
	struct sockaddr_storage  pr_peer_address;  /* peer address of tunnel */
	struct sockaddr_storage  pr_local_address; /* our address of tunnel */
	struct pipex_mppe_req    pr_mppe_recv;     /* mppe key for receive */
	struct pipex_mppe_req    pr_mppe_send;     /* mppe key for send */
};

struct pipex_session_stat_req {
	int                      psr_protocol;   /* tunnel protocol */
	uint16_t                 psr_session_id; /* session-id */
	struct pipex_statistics  psr_stat;       /* statistics */
};
struct pipex_session_close_req {
	int                      psr_protocol;   /* tunnel protocol */
	uint16_t                 psr_session_id; /* session-id */
	struct pipex_statistics  psr_stat;       /* statistics */
};
#define	pcr_protocol	psr_protocol
#define	pcr_session_id	psr_session_id
#define	pcr_stat	psr_stat

struct pipex_session_list_req {
	uint8_t	plr_flags;
#define	PIPEX_LISTREQ_MORE		0x01
	int	plr_ppp_id_count;		/* count of PPP id */
	int	plr_ppp_id[PIPEX_MAX_LISTREQ];	/* PPP id */
};

/* for pppx(4) */
struct pppx_hdr {
	u_int32_t	pppx_proto;	/* write: protocol in PIPEX_PROTO_ */
	u_int32_t	pppx_id;	/* write: session_id, read: ppp_id */
};

struct pipex_session_descr_req {
	int		pdr_protocol;		/* tunnel protocol */
	uint16_t	pdr_session_id;		/* session-id */
	char		pdr_descr[IFDESCRSIZE];	/* description */
};


/* PIPEX ioctls */
#define PIPEXASESSION	_IOW ('p',  3, struct pipex_session_req)
#define PIPEXDSESSION	_IOWR('p',  4, struct pipex_session_close_req)
#define PIPEXGSTAT	_IOWR('p',  6, struct pipex_session_stat_req)
#define PIPEXGCLOSED	_IOR ('p',  7, struct pipex_session_list_req)
#define PIPEXSIFDESCR	_IOW ('p',  8, struct pipex_session_descr_req)

#ifdef _KERNEL
extern int	pipex_enable;

struct pipex_session;

__BEGIN_DECLS
void			 pipex_init(void);

struct pipex_session	*pipex_pppoe_lookup_session(struct mbuf *);
struct mbuf		*pipex_pppoe_input(struct mbuf *,
			    struct pipex_session *, struct netstack *);
struct pipex_session	*pipex_pptp_lookup_session(struct mbuf *);
struct mbuf		*pipex_pptp_input(struct mbuf *,
			    struct pipex_session *, struct netstack *);
struct pipex_session	*pipex_pptp_userland_lookup_session_ipv4(struct mbuf *,
			    struct in_addr);
struct pipex_session	*pipex_pptp_userland_lookup_session_ipv6(struct mbuf *,
			    struct in6_addr);
struct pipex_session	*pipex_l2tp_userland_lookup_session(struct mbuf *,
			    struct sockaddr *);
struct mbuf		*pipex_pptp_userland_output(struct mbuf *,
			    struct pipex_session *);
struct pipex_session	*pipex_l2tp_lookup_session(struct mbuf *, int,
			    struct sockaddr *);
struct mbuf		*pipex_l2tp_input(struct mbuf *, int off,
			    struct pipex_session *, uint32_t,
			    struct netstack *);
struct pipex_session	*pipex_l2tp_userland_lookup_session_ipv4(struct mbuf *,
			    struct in_addr);
struct pipex_session	*pipex_l2tp_userland_lookup_session_ipv6(struct mbuf *,
			    struct in6_addr);
struct mbuf		 *pipex_l2tp_userland_output(struct mbuf *,
			    struct pipex_session *);
void			 pipex_rele_session(struct pipex_session *);
int			 pipex_ioctl(void *, u_long, caddr_t);
void			 pipex_session_init_mppe_recv(struct pipex_session *,
			    int, int, u_char *);
void			 pipex_session_init_mppe_send(struct pipex_session *,
			    int, int, u_char *);
__END_DECLS

#endif /* _KERNEL */
#endif
