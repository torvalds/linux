/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)ip_var.h	8.2 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _NETINET_IP_VAR_H_
#define	_NETINET_IP_VAR_H_

#include <sys/queue.h>
#include <sys/epoch.h>

/*
 * Overlay for ip header used by other protocols (tcp, udp).
 */
struct ipovly {
	u_char	ih_x1[9];		/* (unused) */
	u_char	ih_pr;			/* protocol */
	u_short	ih_len;			/* protocol length */
	struct	in_addr ih_src;		/* source internet address */
	struct	in_addr ih_dst;		/* destination internet address */
};

#ifdef _KERNEL
/*
 * Ip reassembly queue structure.  Each fragment
 * being reassembled is attached to one of these structures.
 * They are timed out after ipq_ttl drops to 0, and may also
 * be reclaimed if memory becomes tight.
 */
struct ipq {
	TAILQ_ENTRY(ipq) ipq_list;	/* to other reass headers */
	u_char	ipq_ttl;		/* time for reass q to live */
	u_char	ipq_p;			/* protocol of this fragment */
	u_short	ipq_id;			/* sequence id for reassembly */
	int	ipq_maxoff;		/* total length of packet */
	struct mbuf *ipq_frags;		/* to ip headers of fragments */
	struct	in_addr ipq_src,ipq_dst;
	u_char	ipq_nfrags;		/* # frags in this packet */
	struct label *ipq_label;	/* MAC label */
};
#endif /* _KERNEL */

/*
 * Structure stored in mbuf in inpcb.ip_options
 * and passed to ip_output when ip options are in use.
 * The actual length of the options (including ipopt_dst)
 * is in m_len.
 */
#define MAX_IPOPTLEN	40

struct ipoption {
	struct	in_addr ipopt_dst;	/* first-hop dst if source routed */
	char	ipopt_list[MAX_IPOPTLEN];	/* options proper */
};

/*
 * Structure attached to inpcb.ip_moptions and
 * passed to ip_output when IP multicast options are in use.
 * This structure is lazy-allocated.
 */
struct ip_moptions {
	struct	ifnet *imo_multicast_ifp; /* ifp for outgoing multicasts */
	struct in_addr imo_multicast_addr; /* ifindex/addr on MULTICAST_IF */
	u_long	imo_multicast_vif;	/* vif num outgoing multicasts */
	u_char	imo_multicast_ttl;	/* TTL for outgoing multicasts */
	u_char	imo_multicast_loop;	/* 1 => hear sends if a member */
	u_short	imo_num_memberships;	/* no. memberships this socket */
	u_short	imo_max_memberships;	/* max memberships this socket */
	struct	in_multi **imo_membership;	/* group memberships */
	struct	in_mfilter *imo_mfilters;	/* source filters */
	struct	epoch_context imo_epoch_ctx;
};

struct	ipstat {
	uint64_t ips_total;		/* total packets received */
	uint64_t ips_badsum;		/* checksum bad */
	uint64_t ips_tooshort;		/* packet too short */
	uint64_t ips_toosmall;		/* not enough data */
	uint64_t ips_badhlen;		/* ip header length < data size */
	uint64_t ips_badlen;		/* ip length < ip header length */
	uint64_t ips_fragments;		/* fragments received */
	uint64_t ips_fragdropped;	/* frags dropped (dups, out of space) */
	uint64_t ips_fragtimeout;	/* fragments timed out */
	uint64_t ips_forward;		/* packets forwarded */
	uint64_t ips_fastforward;	/* packets fast forwarded */
	uint64_t ips_cantforward;	/* packets rcvd for unreachable dest */
	uint64_t ips_redirectsent;	/* packets forwarded on same net */
	uint64_t ips_noproto;		/* unknown or unsupported protocol */
	uint64_t ips_delivered;		/* datagrams delivered to upper level*/
	uint64_t ips_localout;		/* total ip packets generated here */
	uint64_t ips_odropped;		/* lost packets due to nobufs, etc. */
	uint64_t ips_reassembled;	/* total packets reassembled ok */
	uint64_t ips_fragmented;	/* datagrams successfully fragmented */
	uint64_t ips_ofragments;	/* output fragments created */
	uint64_t ips_cantfrag;		/* don't fragment flag was set, etc. */
	uint64_t ips_badoptions;		/* error in option processing */
	uint64_t ips_noroute;		/* packets discarded due to no route */
	uint64_t ips_badvers;		/* ip version != 4 */
	uint64_t ips_rawout;		/* total raw ip packets generated */
	uint64_t ips_toolong;		/* ip length > max ip packet size */
	uint64_t ips_notmember;		/* multicasts for unregistered grps */
	uint64_t ips_nogif;		/* no match gif found */
	uint64_t ips_badaddr;		/* invalid address on header */
};

#ifdef _KERNEL

#include <sys/counter.h>
#include <net/vnet.h>

VNET_PCPUSTAT_DECLARE(struct ipstat, ipstat);
/*
 * In-kernel consumers can use these accessor macros directly to update
 * stats.
 */
#define	IPSTAT_ADD(name, val)	\
    VNET_PCPUSTAT_ADD(struct ipstat, ipstat, name, (val))
#define	IPSTAT_SUB(name, val)	IPSTAT_ADD(name, -(val))
#define	IPSTAT_INC(name)	IPSTAT_ADD(name, 1)
#define	IPSTAT_DEC(name)	IPSTAT_SUB(name, 1)

/*
 * Kernel module consumers must use this accessor macro.
 */
void	kmod_ipstat_inc(int statnum);
#define	KMOD_IPSTAT_INC(name)	\
    kmod_ipstat_inc(offsetof(struct ipstat, name) / sizeof(uint64_t))
void	kmod_ipstat_dec(int statnum);
#define	KMOD_IPSTAT_DEC(name)	\
    kmod_ipstat_dec(offsetof(struct ipstat, name) / sizeof(uint64_t))

/* flags passed to ip_output as last parameter */
#define	IP_FORWARDING		0x1		/* most of ip header exists */
#define	IP_RAWOUTPUT		0x2		/* raw ip header exists */
#define	IP_SENDONES		0x4		/* send all-ones broadcast */
#define	IP_SENDTOIF		0x8		/* send on specific ifnet */
#define IP_ROUTETOIF		SO_DONTROUTE	/* 0x10 bypass routing tables */
#define IP_ALLOWBROADCAST	SO_BROADCAST	/* 0x20 can send broadcast packets */
#define	IP_NODEFAULTFLOWID	0x40		/* Don't set the flowid from inp */

#ifdef __NO_STRICT_ALIGNMENT
#define IP_HDR_ALIGNED_P(ip)	1
#else
#define IP_HDR_ALIGNED_P(ip)	((((intptr_t) (ip)) & 3) == 0)
#endif

struct ip;
struct inpcb;
struct route;
struct sockopt;
struct inpcbinfo;

VNET_DECLARE(int, ip_defttl);			/* default IP ttl */
VNET_DECLARE(int, ipforwarding);		/* ip forwarding */
#ifdef IPSTEALTH
VNET_DECLARE(int, ipstealth);			/* stealth forwarding */
#endif
extern u_char	ip_protox[];
VNET_DECLARE(struct socket *, ip_rsvpd);	/* reservation protocol daemon*/
VNET_DECLARE(struct socket *, ip_mrouter);	/* multicast routing daemon */
extern int	(*legal_vif_num)(int);
extern u_long	(*ip_mcast_src)(int);
VNET_DECLARE(int, rsvp_on);
VNET_DECLARE(int, drop_redirect);
extern struct	pr_usrreqs rip_usrreqs;

#define	V_ip_id			VNET(ip_id)
#define	V_ip_defttl		VNET(ip_defttl)
#define	V_ipforwarding		VNET(ipforwarding)
#ifdef IPSTEALTH
#define	V_ipstealth		VNET(ipstealth)
#endif
#define	V_ip_rsvpd		VNET(ip_rsvpd)
#define	V_ip_mrouter		VNET(ip_mrouter)
#define	V_rsvp_on		VNET(rsvp_on)
#define	V_drop_redirect		VNET(drop_redirect)

void	inp_freemoptions(struct ip_moptions *);
int	inp_getmoptions(struct inpcb *, struct sockopt *);
int	inp_setmoptions(struct inpcb *, struct sockopt *);

int	ip_ctloutput(struct socket *, struct sockopt *sopt);
void	ip_drain(void);
int	ip_fragment(struct ip *ip, struct mbuf **m_frag, int mtu,
	    u_long if_hwassist_flags);
void	ip_forward(struct mbuf *m, int srcrt);
void	ip_init(void);
extern int
	(*ip_mforward)(struct ip *, struct ifnet *, struct mbuf *,
	    struct ip_moptions *);
int	ip_output(struct mbuf *,
	    struct mbuf *, struct route *, int, struct ip_moptions *,
	    struct inpcb *);
int	ipproto_register(short);
int	ipproto_unregister(short);
struct mbuf *
	ip_reass(struct mbuf *);
void	ip_savecontrol(struct inpcb *, struct mbuf **, struct ip *,
	    struct mbuf *);
void	ip_slowtimo(void);
void	ip_fillid(struct ip *);
int	rip_ctloutput(struct socket *, struct sockopt *);
void	rip_ctlinput(int, struct sockaddr *, void *);
void	rip_init(void);
int	rip_input(struct mbuf **, int *, int);
int	rip_output(struct mbuf *, struct socket *, ...);
int	ipip_input(struct mbuf **, int *, int);
int	rsvp_input(struct mbuf **, int *, int);
int	ip_rsvp_init(struct socket *);
int	ip_rsvp_done(void);
extern int	(*ip_rsvp_vif)(struct socket *, struct sockopt *);
extern void	(*ip_rsvp_force_done)(struct socket *);
extern int	(*rsvp_input_p)(struct mbuf **, int *, int);

VNET_DECLARE(struct pfil_head *, inet_pfil_head);
#define	V_inet_pfil_head	VNET(inet_pfil_head)
#define	PFIL_INET_NAME		"inet"

void	in_delayed_cksum(struct mbuf *m);

/* Hooks for ipfw, dummynet, divert etc. Most are declared in raw_ip.c */
/*
 * Reference to an ipfw or packet filter rule that can be carried
 * outside critical sections.
 * A rule is identified by rulenum:rule_id which is ordered.
 * In version chain_id the rule can be found in slot 'slot', so
 * we don't need a lookup if chain_id == chain->id.
 *
 * On exit from the firewall this structure refers to the rule after
 * the matching one (slot points to the new rule; rulenum:rule_id-1
 * is the matching rule), and additional info (e.g. info often contains
 * the insn argument or tablearg in the low 16 bits, in host format).
 * On entry, the structure is valid if slot>0, and refers to the starting
 * rules. 'info' contains the reason for reinject, e.g. divert port,
 * divert direction, and so on.
 */
struct ipfw_rule_ref {
	uint32_t	slot;		/* slot for matching rule	*/
	uint32_t	rulenum;	/* matching rule number		*/
	uint32_t	rule_id;	/* matching rule id		*/
	uint32_t	chain_id;	/* ruleset id			*/
	uint32_t	info;		/* see below			*/
};

enum {
	IPFW_INFO_MASK	= 0x0000ffff,
	IPFW_INFO_OUT	= 0x00000000,	/* outgoing, just for convenience */
	IPFW_INFO_IN	= 0x80000000,	/* incoming, overloads dir */
	IPFW_ONEPASS	= 0x40000000,	/* One-pass, do not reinject */
	IPFW_IS_MASK	= 0x30000000,	/* which source ? */
	IPFW_IS_DIVERT	= 0x20000000,
	IPFW_IS_DUMMYNET =0x10000000,
	IPFW_IS_PIPE	= 0x08000000,	/* pipe=1, queue = 0 */
};
#define MTAG_IPFW	1148380143	/* IPFW-tagged cookie */
#define MTAG_IPFW_RULE	1262273568	/* rule reference */
#define	MTAG_IPFW_CALL	1308397630	/* call stack */

struct ip_fw_args;
typedef int	(*ip_fw_chk_ptr_t)(struct ip_fw_args *args);
typedef int	(*ip_fw_ctl_ptr_t)(struct sockopt *);
VNET_DECLARE(ip_fw_ctl_ptr_t, ip_fw_ctl_ptr);
#define	V_ip_fw_ctl_ptr		VNET(ip_fw_ctl_ptr)

/* Divert hooks. */
extern void	(*ip_divert_ptr)(struct mbuf *m, bool incoming);
/* ng_ipfw hooks -- XXX make it the same as divert and dummynet */
extern int	(*ng_ipfw_input_p)(struct mbuf **, struct ip_fw_args *, bool);
extern int	(*ip_dn_ctl_ptr)(struct sockopt *);
extern int	(*ip_dn_io_ptr)(struct mbuf **, struct ip_fw_args *);
#endif /* _KERNEL */

#endif /* !_NETINET_IP_VAR_H_ */
