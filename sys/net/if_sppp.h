/*	$OpenBSD: if_sppp.h,v 1.31 2025/01/15 06:15:44 dlg Exp $	*/
/*	$NetBSD: if_sppp.h,v 1.2.2.1 1999/04/04 06:57:39 explorer Exp $	*/

/*
 * Defines for synchronous PPP link level subroutines.
 *
 * Copyright (C) 1994 Cronyx Ltd.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Heavily revamped to conform to RFC 1661.
 * Copyright (C) 1997, Joerg Wunsch.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE FREEBSD PROJECT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE FREEBSD PROJECT OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * From: Version 2.0, Fri Oct  6 20:39:21 MSK 1995
 *
 * From: if_sppp.h,v 1.8 1997/10/11 11:25:20 joerg Exp
 *
 * From: Id: if_sppp.h,v 1.7 1998/12/01 20:20:19 hm Exp
 */

#ifndef _NET_IF_SPPP_H_
#define _NET_IF_SPPP_H_

#define AUTHFLAG_NOCALLOUT	1 /* don't require authentication on callouts */
#define AUTHFLAG_NORECHALLENGE	2 /* don't re-challenge CHAP */

/*
 * Don't change the order of this.  Ordering the phases this way allows
 * for a comparison of ``pp_phase >= PHASE_AUTHENTICATE'' in order to
 * know whether LCP is up.
 */
enum ppp_phase {
	PHASE_DEAD, PHASE_ESTABLISH, PHASE_TERMINATE,
	PHASE_AUTHENTICATE, PHASE_NETWORK
};


#define AUTHMAXLEN	256	/* including terminating '\0' */
#define AUTHCHALEN	16	/* length of the challenge we send */

/*
 * Definitions to pass struct sppp data down into the kernel using the
 * SIOC[SG]IFGENERIC ioctl interface.
 *
 * In order to use this, create a struct spppreq, fill in the cmd
 * field with SPPPIOGDEFS, and put the address of this structure into
 * the ifr_data portion of a struct ifreq.  Pass this struct to a
 * SIOCGIFGENERIC ioctl.  Then replace the cmd field by SPPPIOSDEFS,
 * modify the defs field as desired, and pass the struct ifreq now
 * to a SIOCSIFGENERIC ioctl.
 */

struct sauthreq {
	int cmd;
	u_short	proto;			/* authentication protocol to use */
	u_short	flags;
	u_char	name[AUTHMAXLEN];	/* system identification name */
	u_char	secret[AUTHMAXLEN];	/* secret password */
};

struct spppreq {
	int	cmd;
	enum ppp_phase phase;	/* phase we're currently in */
};

#include <netinet/in.h>

#define IPCP_MAX_DNSSRV	2
struct sdnsreq {
	int cmd;
	struct in_addr dns[IPCP_MAX_DNSSRV];
};

#define SPPPIOGDEFS  ((int)(('S' << 24) + (1 << 16) + sizeof(struct spppreq)))
#define SPPPIOSDEFS  ((int)(('S' << 24) + (2 << 16) + sizeof(struct spppreq)))
#define SPPPIOGMAUTH ((int)(('S' << 24) + (3 << 16) + sizeof(struct sauthreq)))
#define SPPPIOSMAUTH ((int)(('S' << 24) + (4 << 16) + sizeof(struct sauthreq)))
#define SPPPIOGHAUTH ((int)(('S' << 24) + (5 << 16) + sizeof(struct sauthreq)))
#define SPPPIOSHAUTH ((int)(('S' << 24) + (6 << 16) + sizeof(struct sauthreq)))
#define SPPPIOGDNS   ((int)(('S' << 24) + (7 << 16) + sizeof(struct sdnsreq)))


#ifdef _KERNEL

#include <sys/timeout.h>
#include <sys/task.h>

#ifdef INET6
#include <netinet6/in6_var.h>
#endif

#define IDX_LCP 0		/* idx into state table */

struct slcp {
	u_long	opts;		/* LCP options to send (bitfield) */
	u_long  magic;          /* local magic number */
	u_long	mru;		/* our max receive unit */
	u_long	their_mru;	/* their max receive unit */
	u_long	protos;		/* bitmask of protos that are started */
	u_char  echoid;         /* id of last keepalive echo request */
	/* restart max values, see RFC 1661 */
	int	timeout;	/* seconds */
	int	max_terminate;
	int	max_configure;
	int	max_failure;
};

#define IDX_IPCP 1		/* idx into state table */
#define IDX_IPV6CP 2

struct sipcp {
	u_long	opts;		/* IPCP options to send (bitfield) */
	u_int	flags;
#define IPCP_HISADDR_SEEN 1	/* have seen his address already */
#define IPCP_MYADDR_DYN   2	/* my address is dynamically assigned */
#define IPCP_MYADDR_SEEN  4	/* have seen my address already */
#define IPCP_HISADDR_DYN  8	/* his address is dynamically assigned */
#define IPV6CP_MYIFID_DYN	1 /* my ifid is dynamically assigned */
#define IPV6CP_MYIFID_SEEN	2 /* have seen my suggested ifid */
	u_int32_t saved_hisaddr; /* if hisaddr (IPv4) is dynamic, save
				  * original one here, in network byte order */
	u_int32_t req_hisaddr;	/* remote address requested (IPv4) */
	u_int32_t req_myaddr;	/* local address requested (IPv4) */
	struct in_addr dns[IPCP_MAX_DNSSRV]; /* IPv4 DNS servers (RFC 1877) */
#ifdef INET6
	struct in6_aliasreq req_ifid;	/* local ifid requested (IPv6) */
#endif
	struct task set_addr_task;	/* set address from process context */
	struct task clear_addr_task;	/* clear address from process context */
};

struct sauth {
	u_short	proto;			/* authentication protocol to use */
	u_short	flags;
	u_char	*name;	/* system identification name */
	u_char	*secret;	/* secret password */
};

#define IDX_PAP		3
#define IDX_CHAP	4

#define IDX_COUNT (IDX_CHAP + 1) /* bump this when adding cp's! */

struct sppp {
	/* NB: pp_if _must_ be first */
	struct  ifnet pp_if;    /* network interface data */
	struct	mbuf_queue pp_cpq;	/* PPP control protocol queue */
	struct  sppp *pp_next;  /* next interface in keepalive list */
	u_int   pp_flags;
	u_int   pp_framebytes;	/* number of bytes added by hardware framing */
	u_short pp_alivecnt;    /* keepalive packets counter */
	u_short pp_loopcnt;     /* loopback detection counter */
	u_int32_t  pp_seq;      /* local sequence number */
	u_int32_t  pp_rseq;     /* remote sequence number */
	time_t	pp_last_receive;	/* peer's last "sign of life" */
	time_t	pp_last_activity;	/* second of last payload data s/r */
	enum ppp_phase pp_phase;	/* phase we're currently in */
	int	state[IDX_COUNT];	/* state machine */
	u_char  confid[IDX_COUNT];	/* id of last configuration request */
	int	rst_counter[IDX_COUNT];	/* restart counter */
	int	fail_counter[IDX_COUNT]; /* negotiation failure counter */
	struct timeout ch[IDX_COUNT];
	struct timeout pap_my_to_ch;
	struct slcp lcp;		/* LCP params */
	struct sipcp ipcp;		/* IPCP params */
	struct sipcp ipv6cp;		/* IPV6CP params */
	struct sauth myauth;		/* auth params, i'm peer */
	struct sauth hisauth;		/* auth params, i'm authenticator */
	u_char chap_challenge[AUTHCHALEN]; /* random challenge used by CHAP */

	/*
	 * These functions are filled in by sppp_attach(), and are
	 * expected to be used by the lower layer (hardware) drivers
	 * in order to communicate the (un)availability of the
	 * communication link.  Lower layer drivers that are always
	 * ready to communicate (like hardware HDLC) can shortcut
	 * pp_up from pp_tls, and pp_down from pp_tlf.
	 */
	void	(*pp_up)(struct sppp *sp);
	void	(*pp_down)(struct sppp *sp);
	/*
	 * These functions need to be filled in by the lower layer
	 * (hardware) drivers if they request notification from the
	 * PPP layer whether the link is actually required.  They
	 * correspond to the tls and tlf actions.
	 */
	void	(*pp_tls)(struct sppp *sp);
	void	(*pp_tlf)(struct sppp *sp);
	/*
	 * These (optional) functions may be filled by the hardware
	 * driver if any notification of established connections
	 * (currently: IPCP up) is desired (pp_con) or any internal
	 * state change of the interface state machine should be
	 * signaled for monitoring purposes (pp_chg).
	 */
	void	(*pp_con)(struct sppp *sp);
	void	(*pp_chg)(struct sppp *sp, int new_state);
};

#define PP_KEEPALIVE    0x01    /* use keepalive protocol */
				/* 0x02 was PP_CISCO */
				/* 0x04 was PP_TIMO */
#define PP_CALLIN	0x08	/* we are being called */
#define PP_NEEDAUTH	0x10	/* remote requested authentication */
#define PP_NOFRAMING	0x20	/* do not add/expect encapsulation
                                   around PPP frames (i.e. the serial
                                   HDLC like encapsulation, RFC1662) */

#define PP_MIN_MRU	IP_MSS	/* minimal MRU */
#define PP_MTU		1500	/* default MTU */
#define PP_MAX_MRU	2048	/* maximal MRU we want to negotiate */

void sppp_attach (struct ifnet *ifp);
void sppp_detach (struct ifnet *ifp);
void sppp_input (struct ifnet *ifp, struct mbuf *m);
int sppp_proto_up(struct ifnet *ifp, uint16_t);

/* Workaround */
void spppattach (struct ifnet *ifp);
int sppp_ioctl(struct ifnet *ifp, u_long cmd, void *data);

struct mbuf *sppp_dequeue (struct ifnet *ifp);
int sppp_isempty (struct ifnet *ifp);
void sppp_flush (struct ifnet *ifp);
#endif /* _KERNEL */
#endif /* _NET_IF_SPPP_H_ */
