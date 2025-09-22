/*	$OpenBSD: udp_var.h,v 1.53 2025/03/02 21:28:32 bluhm Exp $	*/
/*	$NetBSD: udp_var.h,v 1.12 1996/02/13 23:44:41 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)udp_var.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_UDP_VAR_H_
#define _NETINET_UDP_VAR_H_

/*
 * UDP kernel structures and variables.
 */
struct	udpiphdr {
	struct	ipovly ui_i;		/* overlaid ip structure */
	struct	udphdr ui_u;		/* udp header */
};
#define	ui_x1		ui_i.ih_x1
#define	ui_pr		ui_i.ih_pr
#define	ui_len		ui_i.ih_len
#define	ui_src		ui_i.ih_src
#define	ui_dst		ui_i.ih_dst
#define	ui_sport	ui_u.uh_sport
#define	ui_dport	ui_u.uh_dport
#define	ui_ulen		ui_u.uh_ulen
#define	ui_sum		ui_u.uh_sum

struct	udpstat {
				/* input statistics: */
	u_long	udps_ipackets;		/* total input packets */
	u_long	udps_hdrops;		/* packet shorter than header */
	u_long	udps_badsum;		/* checksum error */
	u_long	udps_nosum;		/* no checksum */
	u_long	udps_badlen;		/* data length larger than packet */
	u_long	udps_noport;		/* no socket on port */
	u_long	udps_noportbcast;	/* of above, arrived as broadcast */
	u_long	udps_nosec;		/* dropped for lack of ipsec */
	u_long	udps_fullsock;		/* not delivered, input socket full */
	u_long	udps_pcbhashmiss;	/* input packets missing pcb hash */
	u_long	udps_inswcsum;		/* input software-csummed packets */
				/* output statistics: */
	u_long	udps_opackets;		/* total output packets */
	u_long	udps_outswcsum;		/* output software-csummed packets */
};

/*
 * Names for UDP sysctl objects
 */
#define	UDPCTL_CHECKSUM		1 /* checksum UDP packets */
#define	UDPCTL_BADDYNAMIC	2 /* return bad dynamic port bitmap */
#define UDPCTL_RECVSPACE	3 /* receive buffer space */
#define UDPCTL_SENDSPACE	4 /* send buffer space */
#define UDPCTL_STATS		5 /* UDP statistics */
#define UDPCTL_ROOTONLY		6 /* root only port bitmap */
#define UDPCTL_MAXID		7

#define UDPCTL_NAMES { \
	{ 0, 0 }, \
	{ "checksum", CTLTYPE_INT }, \
	{ "baddynamic", CTLTYPE_STRUCT }, \
	{ "recvspace",  CTLTYPE_INT }, \
	{ "sendspace",  CTLTYPE_INT }, \
	{ "stats",	CTLTYPE_STRUCT }, \
	{ "rootonly", CTLTYPE_STRUCT }, \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum udpstat_counters {
			/* input statistics: */
	udps_ipackets,		/* total input packets */
	udps_hdrops,		/* packet shorter than header */
	udps_badsum,		/* checksum error */
	udps_nosum,		/* no checksum */
	udps_badlen,		/* data length larger than packet */
	udps_noport,		/* no socket on port */
	udps_noportbcast,	/* of above, arrived as broadcast */
	udps_nosec,		/* dropped for lack of ipsec */
	udps_fullsock,		/* not delivered, input socket full */
	udps_pcbhashmiss,	/* input packets missing pcb hash */
	udps_inswcsum,		/* input software-csummed packets */
			/* output statistics: */
	udps_opackets,		/* total output packets */
	udps_outswcsum,		/* output software-csummed packets */

	udps_ncounters
};

extern struct cpumem *udpcounters;

static inline void
udpstat_inc(enum udpstat_counters c)
{
	counters_inc(udpcounters, c);
}

extern struct	inpcbtable udbtable, udb6table;
extern struct	udpstat udpstat;

extern const struct pr_usrreqs udp_usrreqs;

#ifdef INET6
extern const struct pr_usrreqs udp6_usrreqs;
#endif

#ifdef INET6
void	udp6_ctlinput(int, struct sockaddr *, u_int, void *);
#endif /* INET6 */
void	 udp_ctlinput(int, struct sockaddr *, u_int, void *);
void	 udp_init(void);
int	 udp_input(struct mbuf **, int *, int, int, struct netstack *);
#ifdef INET6
int	 udp6_output(struct inpcb *, struct mbuf *, struct mbuf *,
	struct mbuf *);
#endif /* INET6 */
int	 udp_sysctl(int *, u_int, void *, size_t *, void *, size_t);
int	 udp_attach(struct socket *, int, int);
int	 udp_detach(struct socket *);
int	 udp_bind(struct socket *, struct mbuf *, struct proc *);
int	 udp_connect(struct socket *, struct mbuf *);
int	 udp_disconnect(struct socket *);
int	 udp_shutdown(struct socket *);
int	 udp_send(struct socket *, struct mbuf *, struct mbuf *,
	     struct mbuf *);
#endif /* _KERNEL */
#endif /* _NETINET_UDP_VAR_H_ */
