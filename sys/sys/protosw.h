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
 *	@(#)protosw.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _SYS_PROTOSW_H_
#define _SYS_PROTOSW_H_

/* Forward declare these structures referenced from prototypes below. */
struct kaiocb;
struct mbuf;
struct thread;
struct sockaddr;
struct socket;
struct sockopt;

/*#ifdef _KERNEL*/
/*
 * Protocol switch table.
 *
 * Each protocol has a handle initializing one of these structures,
 * which is used for protocol-protocol and system-protocol communication.
 *
 * A protocol is called through the pr_init entry before any other.
 * Thereafter it is called every 200ms through the pr_fasttimo entry and
 * every 500ms through the pr_slowtimo for timer based actions.
 * The system will call the pr_drain entry if it is low on space and
 * this should throw away any non-critical data.
 *
 * Protocols pass data between themselves as chains of mbufs using
 * the pr_input and pr_output hooks.  Pr_input passes data up (towards
 * the users) and pr_output passes it down (towards the interfaces); control
 * information passes up and down on pr_ctlinput and pr_ctloutput.
 * The protocol is responsible for the space occupied by any the
 * arguments to these entries and must dispose it.
 *
 * In retrospect, it would be a lot nicer to use an interface
 * similar to the vnode VOP interface.
 */
/* USE THESE FOR YOUR PROTOTYPES ! */
typedef int	pr_input_t (struct mbuf **, int*, int);
typedef int	pr_output_t (struct mbuf *, struct socket *, ...);
typedef void	pr_ctlinput_t (int, struct sockaddr *, void *);
typedef int	pr_ctloutput_t (struct socket *, struct sockopt *);
typedef	void	pr_init_t (void);
typedef	void	pr_fasttimo_t (void);
typedef	void	pr_slowtimo_t (void);
typedef	void	pr_drain_t (void);

struct protosw {
	short	pr_type;		/* socket type used for */
	struct	domain *pr_domain;	/* domain protocol a member of */
	short	pr_protocol;		/* protocol number */
	short	pr_flags;		/* see below */
/* protocol-protocol hooks */
	pr_input_t *pr_input;		/* input to protocol (from below) */
	pr_output_t *pr_output;		/* output to protocol (from above) */
	pr_ctlinput_t *pr_ctlinput;	/* control input (from below) */
	pr_ctloutput_t *pr_ctloutput;	/* control output (from above) */
/* utility hooks */
	pr_init_t *pr_init;
	pr_fasttimo_t *pr_fasttimo;	/* fast timeout (200ms) */
	pr_slowtimo_t *pr_slowtimo;	/* slow timeout (500ms) */
	pr_drain_t *pr_drain;		/* flush any excess space possible */

	struct	pr_usrreqs *pr_usrreqs;	/* user-protocol hook */
};
/*#endif*/

#define	PR_SLOWHZ	2		/* 2 slow timeouts per second */
#define	PR_FASTHZ	5		/* 5 fast timeouts per second */

/*
 * This number should be defined again within each protocol family to avoid
 * confusion.
 */
#define	PROTO_SPACER	32767		/* spacer for loadable protocols */

/*
 * Values for pr_flags.
 * PR_ADDR requires PR_ATOMIC;
 * PR_ADDR and PR_CONNREQUIRED are mutually exclusive.
 * PR_IMPLOPCL means that the protocol allows sendto without prior connect,
 *	and the protocol understands the MSG_EOF flag.  The first property is
 *	is only relevant if PR_CONNREQUIRED is set (otherwise sendto is allowed
 *	anyhow).
 */
#define	PR_ATOMIC	0x01		/* exchange atomic messages only */
#define	PR_ADDR		0x02		/* addresses given with messages */
#define	PR_CONNREQUIRED	0x04		/* connection required by protocol */
#define	PR_WANTRCVD	0x08		/* want PRU_RCVD calls */
#define	PR_RIGHTS	0x10		/* passes capabilities */
#define PR_IMPLOPCL	0x20		/* implied open/close */
#define	PR_LASTHDR	0x40		/* enforce ipsec policy; last header */

/*
 * In earlier BSD network stacks, a single pr_usrreq() function pointer was
 * invoked with an operation number indicating what operation was desired.
 * We now provide individual function pointers which protocols can implement,
 * which offers a number of benefits (such as type checking for arguments).
 * These older constants are still present in order to support TCP debugging.
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
#define	PRU_ABORT		10	/* abort (fast DISCONNECT, DETATCH) */
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
/* end for protocol's internal use */
#define PRU_SEND_EOF		22	/* send and close */
#define	PRU_SOSETLABEL		23	/* MAC label change */
#define	PRU_CLOSE		24	/* socket close */
#define	PRU_FLUSH		25	/* flush the socket */
#define	PRU_NREQ		25

#ifdef PRUREQUESTS
const char *prurequests[] = {
	"ATTACH",	"DETACH",	"BIND",		"LISTEN",
	"CONNECT",	"ACCEPT",	"DISCONNECT",	"SHUTDOWN",
	"RCVD",		"SEND",		"ABORT",	"CONTROL",
	"SENSE",	"RCVOOB",	"SENDOOB",	"SOCKADDR",
	"PEERADDR",	"CONNECT2",	"FASTTIMO",	"SLOWTIMO",
	"PROTORCV",	"PROTOSEND",	"SEND_EOF",	"SOSETLABEL",
	"CLOSE",	"FLUSH",
};
#endif

#ifdef	_KERNEL			/* users shouldn't see this decl */

struct ifnet;
struct stat;
struct ucred;
struct uio;

/*
 * If the ordering here looks odd, that's because it's alphabetical.  These
 * should eventually be merged back into struct protosw.
 *
 * Some fields initialized to defaults if they are NULL.
 * See uipc_domain.c:net_init_domain()
 */
struct pr_usrreqs {
	void	(*pru_abort)(struct socket *so);
	int	(*pru_accept)(struct socket *so, struct sockaddr **nam);
	int	(*pru_attach)(struct socket *so, int proto, struct thread *td);
	int	(*pru_bind)(struct socket *so, struct sockaddr *nam,
		    struct thread *td);
	int	(*pru_connect)(struct socket *so, struct sockaddr *nam,
		    struct thread *td);
	int	(*pru_connect2)(struct socket *so1, struct socket *so2);
	int	(*pru_control)(struct socket *so, u_long cmd, caddr_t data,
		    struct ifnet *ifp, struct thread *td);
	void	(*pru_detach)(struct socket *so);
	int	(*pru_disconnect)(struct socket *so);
	int	(*pru_listen)(struct socket *so, int backlog,
		    struct thread *td);
	int	(*pru_peeraddr)(struct socket *so, struct sockaddr **nam);
	int	(*pru_rcvd)(struct socket *so, int flags);
	int	(*pru_rcvoob)(struct socket *so, struct mbuf *m, int flags);
	int	(*pru_send)(struct socket *so, int flags, struct mbuf *m,
		    struct sockaddr *addr, struct mbuf *control,
		    struct thread *td);
#define	PRUS_OOB	0x1
#define	PRUS_EOF	0x2
#define	PRUS_MORETOCOME	0x4
#define	PRUS_NOTREADY	0x8
	int	(*pru_ready)(struct socket *so, struct mbuf *m, int count);
	int	(*pru_sense)(struct socket *so, struct stat *sb);
	int	(*pru_shutdown)(struct socket *so);
	int	(*pru_flush)(struct socket *so, int direction);
	int	(*pru_sockaddr)(struct socket *so, struct sockaddr **nam);
	int	(*pru_sosend)(struct socket *so, struct sockaddr *addr,
		    struct uio *uio, struct mbuf *top, struct mbuf *control,
		    int flags, struct thread *td);
	int	(*pru_soreceive)(struct socket *so, struct sockaddr **paddr,
		    struct uio *uio, struct mbuf **mp0, struct mbuf **controlp,
		    int *flagsp);
	int	(*pru_sopoll)(struct socket *so, int events,
		    struct ucred *cred, struct thread *td);
	void	(*pru_sosetlabel)(struct socket *so);
	void	(*pru_close)(struct socket *so);
	int	(*pru_bindat)(int fd, struct socket *so, struct sockaddr *nam,
		    struct thread *td);
	int	(*pru_connectat)(int fd, struct socket *so,
		    struct sockaddr *nam, struct thread *td);
	int	(*pru_aio_queue)(struct socket *so, struct kaiocb *job);
};

/*
 * All nonvoid pru_*() functions below return EOPNOTSUPP.
 */
int	pru_accept_notsupp(struct socket *so, struct sockaddr **nam);
int	pru_aio_queue_notsupp(struct socket *so, struct kaiocb *job);
int	pru_attach_notsupp(struct socket *so, int proto, struct thread *td);
int	pru_bind_notsupp(struct socket *so, struct sockaddr *nam,
	    struct thread *td);
int	pru_bindat_notsupp(int fd, struct socket *so, struct sockaddr *nam,
	    struct thread *td);
int	pru_connect_notsupp(struct socket *so, struct sockaddr *nam,
	    struct thread *td);
int	pru_connectat_notsupp(int fd, struct socket *so, struct sockaddr *nam,
	    struct thread *td);
int	pru_connect2_notsupp(struct socket *so1, struct socket *so2);
int	pru_control_notsupp(struct socket *so, u_long cmd, caddr_t data,
	    struct ifnet *ifp, struct thread *td);
int	pru_disconnect_notsupp(struct socket *so);
int	pru_listen_notsupp(struct socket *so, int backlog, struct thread *td);
int	pru_peeraddr_notsupp(struct socket *so, struct sockaddr **nam);
int	pru_rcvd_notsupp(struct socket *so, int flags);
int	pru_rcvoob_notsupp(struct socket *so, struct mbuf *m, int flags);
int	pru_send_notsupp(struct socket *so, int flags, struct mbuf *m,
	    struct sockaddr *addr, struct mbuf *control, struct thread *td);
int	pru_ready_notsupp(struct socket *so, struct mbuf *m, int count);
int	pru_sense_null(struct socket *so, struct stat *sb);
int	pru_shutdown_notsupp(struct socket *so);
int	pru_sockaddr_notsupp(struct socket *so, struct sockaddr **nam);
int	pru_sosend_notsupp(struct socket *so, struct sockaddr *addr,
	    struct uio *uio, struct mbuf *top, struct mbuf *control, int flags,
	    struct thread *td);
int	pru_soreceive_notsupp(struct socket *so, struct sockaddr **paddr,
	    struct uio *uio, struct mbuf **mp0, struct mbuf **controlp,
	    int *flagsp);
int	pru_sopoll_notsupp(struct socket *so, int events, struct ucred *cred,
	    struct thread *td);

#endif /* _KERNEL */

/*
 * The arguments to the ctlinput routine are
 *	(*protosw[].pr_ctlinput)(cmd, sa, arg);
 * where cmd is one of the commands below, sa is a pointer to a sockaddr,
 * and arg is a `void *' argument used within a protocol family.
 */
#define	PRC_IFDOWN		0	/* interface transition */
#define	PRC_ROUTEDEAD		1	/* select new route if possible ??? */
#define	PRC_IFUP		2	/* interface has come back up */
/* was	PRC_QUENCH2		3	DEC congestion bit says slow down */
/* was	PRC_QUENCH		4	Deprecated by RFC 6633 */
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
#define	PRC_UNREACH_ADMIN_PROHIB	21	/* packet administrativly prohibited */

#define	PRC_NCMDS		22

#define	PRC_IS_REDIRECT(cmd)	\
	((cmd) >= PRC_REDIRECT_NET && (cmd) <= PRC_REDIRECT_TOSHOST)

#ifdef PRCREQUESTS
char	*prcrequests[] = {
	"IFDOWN", "ROUTEDEAD", "IFUP", "DEC-BIT-QUENCH2",
	"QUENCH", "MSGSIZE", "HOSTDEAD", "#7",
	"NET-UNREACH", "HOST-UNREACH", "PROTO-UNREACH", "PORT-UNREACH",
	"#12", "SRCFAIL-UNREACH", "NET-REDIRECT", "HOST-REDIRECT",
	"TOSNET-REDIRECT", "TOSHOST-REDIRECT", "TX-INTRANS", "TX-REASS",
	"PARAMPROB", "ADMIN-UNREACH"
};
#endif

/*
 * The arguments to ctloutput are:
 *	(*protosw[].pr_ctloutput)(req, so, level, optname, optval, p);
 * req is one of the actions listed below, so is a (struct socket *),
 * level is an indication of which protocol layer the option is intended.
 * optname is a protocol dependent socket option request,
 * optval is a pointer to a mbuf-chain pointer, for value-return results.
 * The protocol is responsible for disposal of the mbuf chain *optval
 * if supplied,
 * the caller is responsible for any space held by *optval, when returned.
 * A non-zero return from ctloutput gives an
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
void	pfctlinput(int, struct sockaddr *);
void	pfctlinput2(int, struct sockaddr *, void *);
struct domain *pffinddomain(int family);
struct protosw *pffindproto(int family, int protocol, int type);
struct protosw *pffindtype(int family, int type);
int	pf_proto_register(int family, struct protosw *npr);
int	pf_proto_unregister(int family, int protocol, int type);
#endif

#endif
