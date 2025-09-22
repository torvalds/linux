/*	$OpenBSD: if_ppp.h,v 1.10 2022/01/02 22:36:03 jsg Exp $	*/
/*	$NetBSD: if_ppp.h,v 1.11 1996/03/15 02:28:05 paulus Exp $	*/

/*
 * if_ppp.h - Point-to-Point Protocol definitions.
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

#ifndef _NET_IF_PPP_H_
#define _NET_IF_PPP_H_

/*
 * Packet sizes
 */
#define	PPP_MTU		1500	/* Default MTU (size of Info field) */
#define PPP_MAXMRU	65000	/* Largest MRU we allow */

/*
 * Bit definitions for flags.
 */
#define SC_COMP_PROT	0x00000001	/* protocol compression (output) */
#define SC_COMP_AC	0x00000002	/* header compression (output) */
#define	SC_COMP_TCP	0x00000004	/* TCP (VJ) compression (output) */
#define SC_NO_TCP_CCID	0x00000008	/* disable VJ connection-id comp. */
#define SC_REJ_COMP_AC	0x00000010	/* reject adrs/ctrl comp. on input */
#define SC_REJ_COMP_TCP	0x00000020	/* reject TCP (VJ) comp. on input */
#define SC_CCP_OPEN	0x00000040	/* Look at CCP packets */
#define SC_CCP_UP	0x00000080	/* May send/recv compressed packets */
#define SC_DEBUG	0x00010000	/* enable debug messages */
#define SC_LOG_INPKT	0x00020000	/* log contents of good pkts recvd */
#define SC_LOG_OUTPKT	0x00040000	/* log contents of pkts sent */
#define SC_LOG_RAWIN	0x00080000	/* log all chars received */
#define SC_LOG_FLUSH	0x00100000	/* log all chars flushed */
#define SC_RCV_B7_0	0x01000000	/* have rcvd char with bit 7 = 0 */
#define SC_RCV_B7_1	0x02000000	/* have rcvd char with bit 7 = 1 */
#define SC_RCV_EVNP	0x04000000	/* have rcvd char with even parity */
#define SC_RCV_ODDP	0x08000000	/* have rcvd char with odd parity */
#define	SC_MASK		0x0fff00ff	/* bits that user can change */

/*
 * State bits in sc_flags, not changeable by user.
 */
#define SC_TIMEOUT	0x00000400	/* timeout is currently pending */
#define SC_VJ_RESET	0x00000800	/* need to reset VJ decomp */
#define SC_COMP_RUN	0x00001000	/* compressor has been inited */
#define SC_DECOMP_RUN	0x00002000	/* decompressor has been inited */
#define SC_DC_ERROR	0x00004000	/* non-fatal decomp error detected */
#define SC_DC_FERROR	0x00008000	/* fatal decomp error detected */
#define SC_TBUSY	0x10000000	/* xmitter doesn't need a packet yet */
#define SC_PKTLOST	0x20000000	/* have lost or dropped a packet */
#define	SC_FLUSH	0x40000000	/* flush input until next PPP_FLAG */
#define	SC_ESCAPED	0x80000000	/* saw a PPP_ESCAPE */

/*
 * Ioctl definitions.
 */

struct npioctl {
    int		protocol;	/* PPP protocol, e.g. PPP_IP */
    enum NPmode	mode;
};

/* Structure describing a CCP configuration option, for PPPIOCSCOMPRESS */
struct ppp_option_data {
	u_char	*ptr;
	u_int	length;
	int	transmit;
};

struct ifpppstatsreq {
    char ifr_name[IFNAMSIZ];
    struct ppp_stats stats;
};

struct ifpppcstatsreq {
    char ifr_name[IFNAMSIZ];
    struct ppp_comp_stats stats;
};

/*
 * Ioctl definitions.
 */

#define	PPPIOCGFLAGS	_IOR('t', 90, int)	/* get configuration flags */
#define	PPPIOCSFLAGS	_IOW('t', 89, int)	/* set configuration flags */
#define	PPPIOCGASYNCMAP	_IOR('t', 88, int)	/* get async map */
#define	PPPIOCSASYNCMAP	_IOW('t', 87, int)	/* set async map */
#define	PPPIOCGUNIT	_IOR('t', 86, int)	/* get ppp unit number */
#define	PPPIOCGRASYNCMAP _IOR('t', 85, int)	/* get receive async map */
#define	PPPIOCSRASYNCMAP _IOW('t', 84, int)	/* set receive async map */
#define	PPPIOCGMRU	_IOR('t', 83, int)	/* get max receive unit */
#define	PPPIOCSMRU	_IOW('t', 82, int)	/* set max receive unit */
#define	PPPIOCSMAXCID	_IOW('t', 81, int)	/* set VJ max slot ID */
#define PPPIOCGXASYNCMAP _IOR('t', 80, ext_accm) /* get extended ACCM */
#define PPPIOCSXASYNCMAP _IOW('t', 79, ext_accm) /* set extended ACCM */
#define PPPIOCXFERUNIT	_IO('t', 78)		/* transfer PPP unit */
#define PPPIOCSCOMPRESS	_IOW('t', 77, struct ppp_option_data)
#define PPPIOCGNPMODE	_IOWR('t', 76, struct npioctl) /* get NP mode */
#define PPPIOCSNPMODE	_IOW('t', 75, struct npioctl)  /* set NP mode */
#define PPPIOCGIDLE	_IOR('t', 74, struct ppp_idle) /* get idle time */
#define PPPIOCSPASS	_IOW('t', 71, struct bpf_program) /* set pass filter */
#define PPPIOCSACTIVE	_IOW('t', 70, struct bpf_program) /* set active filt */

/*
 * These two are interface ioctls so that pppstats can do them on
 * a socket without having to open the serial device.
 */
#define SIOCGPPPSTATS	_IOWR('i', 123, struct ifpppstatsreq)
#define SIOCGPPPCSTATS	_IOWR('i', 122, struct ifpppcstatsreq)

#ifdef _KERNEL
void pppattach(void);
int pppoutput(struct ifnet *, struct mbuf *, struct sockaddr *,
		   struct rtentry *);
void pppintr(void);
#endif
#endif /* _NET_IF_PPP_H_ */
