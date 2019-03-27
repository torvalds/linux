/*-
 * Copyright (c) 2007-2008
 * 	Swinburne University of Technology, Melbourne, Australia.
 * Copyright (c) 2009-2010 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, by Lawrence Stewart and
 * James Healy, made possible in part by a grant from the Cisco University
 * Research Program Fund at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by David Hayes under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This software was first released in 2007 by James Healy and Lawrence Stewart
 * whilst working on the NewTCP research project at Swinburne University of
 * Technology's Centre for Advanced Internet Architectures, Melbourne,
 * Australia, which was made possible in part by a grant from the Cisco
 * University Research Program Fund at Community Foundation Silicon Valley.
 * More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#ifndef _NETINET_CC_CC_H_
#define _NETINET_CC_CC_H_

#if !defined(_KERNEL)
#error "no user-serviceable parts inside"
#endif

/* Global CC vars. */
extern STAILQ_HEAD(cc_head, cc_algo) cc_list;
extern const int tcprexmtthresh;
extern struct cc_algo newreno_cc_algo;

/* Per-netstack bits. */
VNET_DECLARE(struct cc_algo *, default_cc_ptr);
#define	V_default_cc_ptr VNET(default_cc_ptr)

VNET_DECLARE(int, cc_do_abe);
#define	V_cc_do_abe			VNET(cc_do_abe)

VNET_DECLARE(int, cc_abe_frlossreduce);
#define	V_cc_abe_frlossreduce		VNET(cc_abe_frlossreduce)

/* Define the new net.inet.tcp.cc sysctl tree. */
SYSCTL_DECL(_net_inet_tcp_cc);

/* CC housekeeping functions. */
int	cc_register_algo(struct cc_algo *add_cc);
int	cc_deregister_algo(struct cc_algo *remove_cc);

/*
 * Wrapper around transport structs that contain same-named congestion
 * control variables. Allows algos to be shared amongst multiple CC aware
 * transprots.
 */
struct cc_var {
	void		*cc_data; /* Per-connection private CC algorithm data. */
	int		bytes_this_ack; /* # bytes acked by the current ACK. */
	tcp_seq		curack; /* Most recent ACK. */
	uint32_t	flags; /* Flags for cc_var (see below) */
	int		type; /* Indicates which ptr is valid in ccvc. */
	union ccv_container {
		struct tcpcb		*tcp;
		struct sctp_nets	*sctp;
	} ccvc;
	uint16_t	nsegs; /* # segments coalesced into current chain. */
};

/* cc_var flags. */
#define	CCF_ABC_SENTAWND	0x0001	/* ABC counted cwnd worth of bytes? */
#define	CCF_CWND_LIMITED	0x0002	/* Are we currently cwnd limited? */
#define	CCF_DELACK		0x0004	/* Is this ack delayed? */
#define	CCF_ACKNOW		0x0008	/* Will this ack be sent now? */
#define	CCF_IPHDR_CE		0x0010	/* Does this packet set CE bit? */
#define	CCF_TCPHDR_CWR		0x0020	/* Does this packet set CWR bit? */

/* ACK types passed to the ack_received() hook. */
#define	CC_ACK		0x0001	/* Regular in sequence ACK. */
#define	CC_DUPACK	0x0002	/* Duplicate ACK. */
#define	CC_PARTIALACK	0x0004	/* Not yet. */
#define	CC_SACK		0x0008	/* Not yet. */

/*
 * Congestion signal types passed to the cong_signal() hook. The highest order 8
 * bits (0x01000000 - 0x80000000) are reserved for CC algos to declare their own
 * congestion signal types.
 */
#define	CC_ECN		0x00000001	/* ECN marked packet received. */
#define	CC_RTO		0x00000002	/* RTO fired. */
#define	CC_RTO_ERR	0x00000004	/* RTO fired in error. */
#define	CC_NDUPACK	0x00000008	/* Threshold of dupack's reached. */

#define	CC_SIGPRIVMASK	0xFF000000	/* Mask to check if sig is private. */

/*
 * Structure to hold data and function pointers that together represent a
 * congestion control algorithm.
 */
struct cc_algo {
	char	name[TCP_CA_NAME_MAX];

	/* Init global module state on kldload. */
	int	(*mod_init)(void);

	/* Cleanup global module state on kldunload. */
	int	(*mod_destroy)(void);

	/* Init CC state for a new control block. */
	int	(*cb_init)(struct cc_var *ccv);

	/* Cleanup CC state for a terminating control block. */
	void	(*cb_destroy)(struct cc_var *ccv);

	/* Init variables for a newly established connection. */
	void	(*conn_init)(struct cc_var *ccv);

	/* Called on receipt of an ack. */
	void	(*ack_received)(struct cc_var *ccv, uint16_t type);

	/* Called on detection of a congestion signal. */
	void	(*cong_signal)(struct cc_var *ccv, uint32_t type);

	/* Called after exiting congestion recovery. */
	void	(*post_recovery)(struct cc_var *ccv);

	/* Called when data transfer resumes after an idle period. */
	void	(*after_idle)(struct cc_var *ccv);

	/* Called for an additional ECN processing apart from RFC3168. */
	void	(*ecnpkt_handler)(struct cc_var *ccv);

	/* Called for {get|set}sockopt() on a TCP socket with TCP_CCALGOOPT. */
	int     (*ctl_output)(struct cc_var *, struct sockopt *, void *);

	STAILQ_ENTRY (cc_algo) entries;
};

/* Macro to obtain the CC algo's struct ptr. */
#define	CC_ALGO(tp)	((tp)->cc_algo)

/* Macro to obtain the CC algo's data ptr. */
#define	CC_DATA(tp)	((tp)->ccv->cc_data)

/* Macro to obtain the system default CC algo's struct ptr. */
#define	CC_DEFAULT()	V_default_cc_ptr

extern struct rwlock cc_list_lock;
#define	CC_LIST_LOCK_INIT()	rw_init(&cc_list_lock, "cc_list")
#define	CC_LIST_LOCK_DESTROY()	rw_destroy(&cc_list_lock)
#define	CC_LIST_RLOCK()		rw_rlock(&cc_list_lock)
#define	CC_LIST_RUNLOCK()	rw_runlock(&cc_list_lock)
#define	CC_LIST_WLOCK()		rw_wlock(&cc_list_lock)
#define	CC_LIST_WUNLOCK()	rw_wunlock(&cc_list_lock)
#define	CC_LIST_LOCK_ASSERT()	rw_assert(&cc_list_lock, RA_LOCKED)

#define CC_ALGOOPT_LIMIT	2048

#endif /* _NETINET_CC_CC_H_ */
