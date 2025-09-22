/*	$OpenBSD: protosw.h,v 1.72 2025/03/02 21:28:32 bluhm Exp $	*/
/*	$NetBSD: protosw.h,v 1.10 1996/04/09 20:55:32 cgd Exp $	*/

/*-
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
 *	@(#)protosw.h	8.1 (Berkeley) 6/2/93
 */

/*
 * Protocol switch table.
 *
 * Each protocol has a handle initializing one of these structures,
 * which is used for protocol-protocol and system-protocol communication.
 *
 * A protocol is called through the pr_init entry before any other.
 * Thereafter it is called every 200ms through the pr_fasttimo entry and
 * every 500ms through the pr_slowtimo for timer based actions.
 *
 * Protocols pass data between themselves as chains of mbufs using
 * the pr_input and pr_send hooks.  Pr_input passes data up (towards
 * UNIX) and pr_send passes it down (towards the imps); control
 * information passes up and down on pr_ctlinput and pr_ctloutput.
 * The protocol is responsible for the space occupied by any the
 * arguments to these entries and must dispose it.
 *
 * The userreq routine interfaces protocols to the system and is
 * described below.
 */

#ifndef _SYS_PROTOSW_H_
#define _SYS_PROTOSW_H_

struct mbuf;
struct sockaddr;
struct socket;
struct domain;
struct proc;
struct stat;
struct ifnet;
struct netstack;

struct pr_usrreqs {
	int	(*pru_attach)(struct socket *, int, int);
	int	(*pru_detach)(struct socket *);
	int	(*pru_bind)(struct socket *, struct mbuf *, struct proc *);
	int	(*pru_listen)(struct socket *);
	int	(*pru_connect)(struct socket *, struct mbuf *);
	int	(*pru_accept)(struct socket *, struct mbuf *);
	int	(*pru_disconnect)(struct socket *);
	int	(*pru_shutdown)(struct socket *);
	void	(*pru_rcvd)(struct socket *);
	int	(*pru_send)(struct socket *, struct mbuf *, struct mbuf *,
		    struct mbuf *);
	void	(*pru_abort)(struct socket *);
	int	(*pru_control)(struct socket *, u_long, caddr_t,
		    struct ifnet *);
	int	(*pru_sense)(struct socket *, struct stat *);
	int	(*pru_rcvoob)(struct socket *, struct mbuf *, int);
	int	(*pru_sendoob)(struct socket *, struct mbuf *, struct mbuf *,
		    struct mbuf *);
	int	(*pru_sockaddr)(struct socket *, struct mbuf *);
	int	(*pru_peeraddr)(struct socket *, struct mbuf *);
	int	(*pru_connect2)(struct socket *, struct socket *);
};

struct protosw {
	short	pr_type;		/* socket type used for */
	const	struct domain *pr_domain; /* domain protocol a member of */
	short	pr_protocol;		/* protocol number */
	short	pr_flags;		/* see below */

/* protocol-protocol hooks */
					/* input to protocol (from below) */
	int	(*pr_input)(struct mbuf **, int *, int, int, struct netstack *);
					/* control input (from below) */
	void	(*pr_ctlinput)(int, struct sockaddr *, u_int, void *);
					/* control output (from above) */
	int	(*pr_ctloutput)(int, struct socket *, int, int, struct mbuf *);

/* user-protocol hooks */
	const	struct pr_usrreqs *pr_usrreqs;
	
/* utility hooks */
	void	(*pr_init)(void);	/* initialization hook */
	void	(*pr_fasttimo)(void);	/* fast timeout (200ms) */
	void	(*pr_slowtimo)(void);	/* slow timeout (500ms) */
					/* sysctl for protocol */
	int	(*pr_sysctl)(int *, u_int, void *, size_t *, void *, size_t);
};

#define PR_SLOWHZ	2		/* 2 slow timeouts per second */
#define PR_FASTHZ	5		/* 5 fast timeouts per second */

/*
 * Values for pr_flags.
 * PR_ADDR requires PR_ATOMIC;
 * PR_ADDR and PR_CONNREQUIRED are mutually exclusive.
 */
#define PR_ATOMIC	0x0001		/* exchange atomic messages only */
#define PR_ADDR		0x0002		/* addresses given with messages */
#define PR_CONNREQUIRED	0x0004		/* connection required by protocol */
#define PR_WANTRCVD	0x0008		/* want PRU_RCVD calls */
#define PR_RIGHTS	0x0010		/* passes capabilities */
#define PR_ABRTACPTDIS	0x0020		/* abort on accept(2) to disconnected
					   socket */
#define PR_SPLICE	0x0040		/* socket splicing is possible */
#define PR_MPINPUT	0x0080		/* input runs with shared netlock */
#define PR_MPSYSCTL	0x0200		/* mp-safe sysctl(2) handler */

/*
 * The arguments to usrreq are:
 *	(*protosw[].pr_usrreq)(up, req, m, nam, opt);
 * where up is a (struct socket *), req is one of these requests,
 * m is a optional mbuf chain containing a message,
 * nam is an optional mbuf chain containing an address,
 * and opt is a pointer to a socketopt structure or nil.
 * The protocol is responsible for disposal of the mbuf chain m,
 * the caller is responsible for any space held by nam and opt.
 * A non-zero return from usrreq gives an
 * UNIX error number which should be passed to higher level software.
 */
#define	PRU_ATTACH		0	/* attach protocol to up */
#define	PRU_DETACH		1	/* detach protocol from up */
#define	PRU_BIND		2	/* bind socket to address */
#define	PRU_LISTEN		3	/* listen for connection */
#define	PRU_CONNECT		4	/* establish connection to peer */
#define	PRU_ACCEPT		5	/* accept connection from peer */
#define	PRU_DISCONNECT		6	/* disconnect from peer */
#define	PRU_SHUTDOWN		7	/* won't send any more data */
#define	PRU_RCVD		8	/* have taken data; more room now */
#define	PRU_SEND		9	/* send this data */
#define	PRU_ABORT		10	/* abort (fast DISCONNECT, DETACH) */
#define	PRU_CONTROL		11	/* control operations on protocol */
#define	PRU_SENSE		12	/* return status into m */
#define	PRU_RCVOOB		13	/* retrieve out of band data */
#define	PRU_SENDOOB		14	/* send out of band data */
#define	PRU_SOCKADDR		15	/* fetch socket's address */
#define	PRU_PEERADDR		16	/* fetch peer's address */
#define	PRU_CONNECT2		17	/* connect two sockets */
/* begin for protocols internal use */
#define	PRU_FASTTIMO		18	/* 200ms timeout */
#define	PRU_SLOWTIMO		19	/* 500ms timeout */
#define	PRU_PROTORCV		20	/* receive from below */
#define	PRU_PROTOSEND		21	/* send to below */

#define	PRU_NREQ		22

#ifdef PRUREQUESTS
const char *prurequests[] = {
	"ATTACH",	"DETACH",	"BIND",		"LISTEN",
	"CONNECT",	"ACCEPT",	"DISCONNECT",	"SHUTDOWN",
	"RCVD",		"SEND",		"ABORT",	"CONTROL",
	"SENSE",	"RCVOOB",	"SENDOOB",	"SOCKADDR",
	"PEERADDR",	"CONNECT2",	"FASTTIMO",	"SLOWTIMO",
	"PROTORCV",	"PROTOSEND",
};
#endif

/*
 * The arguments to the ctlinput routine are
 *	(*protosw[].pr_ctlinput)(cmd, sa, arg);
 * where cmd is one of the commands below, sa is a pointer to a sockaddr,
 * and arg is an optional caddr_t argument used within a protocol family.
 */
#define	PRC_IFDOWN		0	/* interface transition */
#define	PRC_ROUTEDEAD		1	/* select new route if possible ??? */
#define	PRC_MTUINC		2	/* increase in mtu to host */
#define	PRC_QUENCH2		3	/* DEC congestion bit says slow down */
#define	PRC_QUENCH		4	/* some one said to slow down */
#define	PRC_MSGSIZE		5	/* message size forced drop */
#define	PRC_HOSTDEAD		6	/* host appears to be down */
#define	PRC_HOSTUNREACH		7	/* deprecated (use PRC_UNREACH_HOST) */
#define	PRC_UNREACH_NET		8	/* no route to network */
#define	PRC_UNREACH_HOST	9	/* no route to host */
#define	PRC_UNREACH_PROTOCOL	10	/* dst says bad protocol */
#define	PRC_UNREACH_PORT	11	/* bad port # */
/* was	PRC_UNREACH_NEEDFRAG	12	   (use PRC_MSGSIZE) */
#define	PRC_UNREACH_SRCFAIL	13	/* source route failed */
#define	PRC_REDIRECT_NET	14	/* net routing redirect */
#define	PRC_REDIRECT_HOST	15	/* host routing redirect */
#define	PRC_REDIRECT_TOSNET	16	/* redirect for type of service & net */
#define	PRC_REDIRECT_TOSHOST	17	/* redirect for tos & host */
#define	PRC_TIMXCEED_INTRANS	18	/* packet lifetime expired in transit */
#define	PRC_TIMXCEED_REASS	19	/* lifetime expired on reass q */
#define	PRC_PARAMPROB		20	/* header incorrect */

#define	PRC_NCMDS		21

#define	PRC_IS_REDIRECT(cmd)	\
	((cmd) >= PRC_REDIRECT_NET && (cmd) <= PRC_REDIRECT_TOSHOST)

#ifdef PRCREQUESTS
char	*prcrequests[] = {
	"IFDOWN", "ROUTEDEAD", "MTUINC", "DEC-BIT-QUENCH2",
	"QUENCH", "MSGSIZE", "HOSTDEAD", "#7",
	"NET-UNREACH", "HOST-UNREACH", "PROTO-UNREACH", "PORT-UNREACH",
	"#12", "SRCFAIL-UNREACH", "NET-REDIRECT", "HOST-REDIRECT",
	"TOSNET-REDIRECT", "TOSHOST-REDIRECT", "TX-INTRANS", "TX-REASS",
	"PARAMPROB"
};
#endif

/*
 * The arguments to ctloutput are:
 *	(*protosw[].pr_ctloutput)(req, so, level, optname, optval);
 * req is one of the actions listed below, so is a (struct socket *),
 * level is an indication of which protocol layer the option is intended.
 * optname is a protocol dependent socket option request,
 * optval is a pointer to a mbuf-chain pointer, for value-return results.
 * The protocol is responsible for disposal of the mbuf chain *optval
 * if supplied,
 * the caller is responsible for any space held by *optval, when returned.
 * A non-zero return from usrreq gives an
 * UNIX error number which should be passed to higher level software.
 */
#define	PRCO_GETOPT	0
#define	PRCO_SETOPT	1

#define	PRCO_NCMDS	2

#ifdef PRCOREQUESTS
char	*prcorequests[] = {
	"GETOPT", "SETOPT",
};
#endif

#ifdef _KERNEL

#include <sys/mbuf.h>
#include <sys/socketvar.h>

const struct protosw *pffindproto(int, int, int);
const struct protosw *pffindtype(int, int);
const struct domain *pffinddomain(int);
void pfctlinput(int, struct sockaddr *);

extern u_char ip_protox[];
extern const struct protosw inetsw[];

#ifdef INET6
extern u_char ip6_protox[];
extern const struct protosw inet6sw[];
#endif /* INET6 */

static inline int
pru_attach(struct socket *so, int proto, int wait)
{
	return (*so->so_proto->pr_usrreqs->pru_attach)(so, proto, wait);
}

static inline int
pru_detach(struct socket *so)
{
	return (*so->so_proto->pr_usrreqs->pru_detach)(so);
}

static inline int
pru_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	if (so->so_proto->pr_usrreqs->pru_bind)
		return (*so->so_proto->pr_usrreqs->pru_bind)(so, nam, p);
	return (EOPNOTSUPP);
}

static inline int
pru_listen(struct socket *so)
{
	if (so->so_proto->pr_usrreqs->pru_listen)
		return (*so->so_proto->pr_usrreqs->pru_listen)(so);
	return (EOPNOTSUPP);
}

static inline int
pru_connect(struct socket *so, struct mbuf *nam)
{
	if (so->so_proto->pr_usrreqs->pru_connect)
		return (*so->so_proto->pr_usrreqs->pru_connect)(so, nam);
	return (EOPNOTSUPP);
}

static inline int
pru_accept(struct socket *so, struct mbuf *nam)
{
	if (so->so_proto->pr_usrreqs->pru_accept)
		return (*so->so_proto->pr_usrreqs->pru_accept)(so, nam);
	return (EOPNOTSUPP);
}

static inline int
pru_disconnect(struct socket *so)
{
	if (so->so_proto->pr_usrreqs->pru_disconnect)
		return (*so->so_proto->pr_usrreqs->pru_disconnect)(so);
	return (EOPNOTSUPP);
}

static inline int
pru_shutdown(struct socket *so)
{
	return (*so->so_proto->pr_usrreqs->pru_shutdown)(so);
}

static inline void
pru_rcvd(struct socket *so)
{
	(*so->so_proto->pr_usrreqs->pru_rcvd)(so);
}

static inline int
pru_send(struct socket *so, struct mbuf *top, struct mbuf *addr,
    struct mbuf *control)
{
	return (*so->so_proto->pr_usrreqs->pru_send)(so, top, addr, control);
}

static inline void
pru_abort(struct socket *so)
{
	(*so->so_proto->pr_usrreqs->pru_abort)(so);
}

static inline int
pru_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp)
{
	if (so->so_proto->pr_usrreqs->pru_control)
		return (*so->so_proto->pr_usrreqs->pru_control)(so,
		    cmd, data, ifp);
	return (EOPNOTSUPP);
}

static inline int
pru_sense(struct socket *so, struct stat *ub)
{
	if (so->so_proto->pr_usrreqs->pru_sense)
		return (*so->so_proto->pr_usrreqs->pru_sense)(so, ub);
	return (0);
}

static inline int
pru_rcvoob(struct socket *so, struct mbuf *m, int flags)
{
	if (so->so_proto->pr_usrreqs->pru_rcvoob)
		return (*so->so_proto->pr_usrreqs->pru_rcvoob)(so, m, flags);
	return (EOPNOTSUPP);
}

static inline int
pru_sendoob(struct socket *so, struct mbuf *top, struct mbuf *addr,
    struct mbuf *control)
{
	if (so->so_proto->pr_usrreqs->pru_sendoob)
		return (*so->so_proto->pr_usrreqs->pru_sendoob)(so,
		    top, addr, control);
	m_freem(top);
	m_freem(control);
	return (EOPNOTSUPP);
}

static inline int
pru_sockaddr(struct socket *so, struct mbuf *addr)
{
	return (*so->so_proto->pr_usrreqs->pru_sockaddr)(so, addr);
}

static inline int
pru_peeraddr(struct socket *so, struct mbuf *addr)
{
	return (*so->so_proto->pr_usrreqs->pru_peeraddr)(so, addr);
}

static inline int
pru_connect2(struct socket *so1, struct socket *so2)
{
	if (so1->so_proto->pr_usrreqs->pru_connect2)
		return (*so1->so_proto->pr_usrreqs->pru_connect2)(so1, so2);
	return (EOPNOTSUPP);
}

#endif

#endif /* _SYS_PROTOSW_H_ */
