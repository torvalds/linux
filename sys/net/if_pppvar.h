/*	$OpenBSD: if_pppvar.h,v 1.21 2024/02/28 16:08:34 denis Exp $	*/
/*	$NetBSD: if_pppvar.h,v 1.5 1997/01/03 07:23:29 mikel Exp $	*/
/*
 * if_pppvar.h - private structures and declarations for PPP.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Copyright (c) 1984-2000 Carnegie Mellon University. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_IF_PPPVAR_H_
#define _NET_IF_PPPVAR_H_

/*
 * Supported network protocols.  These values are used for
 * indexing sc_npmode.
 */
#define NP_IP	0		/* Internet Protocol */
#define NP_IPV6	1		/* Internet Protocol v6 */
#define NUM_NP	2		/* Number of NPs. */

struct ppp_pkt;

struct ppp_pkt_list {
	struct mutex	 pl_mtx;
	struct ppp_pkt	*pl_head;
	struct ppp_pkt	*pl_tail;
	u_int		 pl_count;
	u_int		 pl_limit;
};

/*
 * Structure describing each ppp unit.
 */
struct ppp_softc {
	struct	ifnet sc_if;		/* network-visible interface */
	struct	timeout sc_timo;	/* timeout control (for ptys) */
	int	sc_unit;		/* XXX unit number */
	u_int	sc_flags;		/* control/status bits; see if_ppp.h */
	void	*sc_devp;		/* pointer to device-dep structure */
	void	(*sc_start)(struct ppp_softc *); /* start output proc */
	void	(*sc_ctlp)(struct ppp_softc *); /* rcvd control pkt */
	void	(*sc_relinq)(struct ppp_softc *); /* relinquish ifunit */
	u_int16_t sc_mru;		/* max receive unit */
	pid_t	sc_xfer;		/* used in transferring unit */
	struct	ppp_pkt_list sc_rawq;	/* received packets */
	struct	mbuf_queue sc_inq;	/* queue of input packets for daemon */
	struct	ifqueue sc_fastq;	/* interactive output packet q */
	struct	mbuf *sc_togo;		/* output packet ready to go */
	struct	mbuf_list sc_npqueue;	/* output packets not to be sent yet */
	struct	pppstat sc_stats;	/* count of bytes/pkts sent/rcvd */
	enum	NPmode sc_npmode[NUM_NP]; /* what to do with each NP */
	struct	compressor *sc_xcomp;	/* transmit compressor */
	void	*sc_xc_state;		/* transmit compressor state */
	struct	compressor *sc_rcomp;	/* receive decompressor */
	void	*sc_rc_state;		/* receive decompressor state */
	time_t	sc_last_sent;		/* time (secs) last NP pkt sent */
	time_t	sc_last_recv;		/* time (secs) last NP pkt rcvd */
	struct	bpf_program sc_pass_filt; /* filter for packets to pass */
	struct	bpf_program sc_active_filt; /* filter for "non-idle" packets */
#ifdef	VJC
	struct	slcompress *sc_comp;	/* vjc control buffer */
#endif

	/* Device-dependent part for async lines. */
	ext_accm sc_asyncmap;		/* async control character map */
	u_int32_t sc_rasyncmap;		/* receive async control char map */
	struct	mbuf *sc_outm;		/* mbuf chain currently being output */
	struct	ppp_pkt *sc_pkt;	/* pointer to input pkt chain */
	struct	ppp_pkt *sc_pktc;	/* pointer to current input pkt */
	uint8_t	*sc_pktp;		/* ptr to next char in input pkt */
	u_int16_t sc_ilen;		/* length of input packet so far */
	u_int16_t sc_fcs;		/* FCS so far (input) */
	u_int16_t sc_outfcs;		/* FCS so far for output packet */
	u_char	sc_rawin[16];		/* chars as received */
	int	sc_rawin_count;		/* # in sc_rawin */
	LIST_ENTRY(ppp_softc) sc_list;	/* all ppp interfaces */
};

#ifdef _KERNEL

struct ppp_pkt_hdr {
	struct ppp_pkt		*ph_next; /* next in pkt chain */
	struct ppp_pkt		*ph_pkt;  /* prev in chain or next in list */
	uint16_t		ph_len;
	uint16_t		ph_errmark;
};

struct ppp_pkt {
	struct ppp_pkt_hdr	p_hdr;
	uint8_t			p_buf[MCLBYTES - sizeof(struct ppp_pkt_hdr)];
};

void	ppp_pkt_free(struct ppp_pkt *);

#define PKT_NEXT(_p)		((_p)->p_hdr.ph_next)
#define PKT_PREV(_p)		((_p)->p_hdr.ph_pkt)
#define PKT_NEXTPKT(_p)		((_p)->p_hdr.ph_pkt)
#define PKT_LEN(_p)		((_p)->p_hdr.ph_len)

extern	struct ppp_softc ppp_softc[];

struct	ppp_softc *pppalloc(pid_t pid);
void	pppdealloc(struct ppp_softc *sc);
int	pppioctl(struct ppp_softc *sc, u_long cmd, caddr_t data,
		      int flag, struct proc *p);
void	ppppktin(struct ppp_softc *sc, struct ppp_pkt *pkt, int lost);
struct	mbuf *ppp_dequeue(struct ppp_softc *sc);
void	ppp_restart(struct ppp_softc *sc);
int	pppoutput(struct ifnet *, struct mbuf *,
		       struct sockaddr *, struct rtentry *);
#endif /* _KERNEL */
#endif /* _NET_IF_PPPVAR_H_ */
