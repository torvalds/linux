/*
 * ppp-ioctl.h - PPP ioctl definitions.
 *
 * Copyright 1999-2002 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 */
#ifndef _PPP_IOCTL_H
#define _PPP_IOCTL_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/ppp_defs.h>

/*
 * Bit definitions for flags argument to PPPIOCGFLAGS/PPPIOCSFLAGS.
 */
#define SC_COMP_PROT	0x00000001	/* protocol compression (output) */
#define SC_COMP_AC	0x00000002	/* header compression (output) */
#define	SC_COMP_TCP	0x00000004	/* TCP (VJ) compression (output) */
#define SC_NO_TCP_CCID	0x00000008	/* disable VJ connection-id comp. */
#define SC_REJ_COMP_AC	0x00000010	/* reject adrs/ctrl comp. on input */
#define SC_REJ_COMP_TCP	0x00000020	/* reject TCP (VJ) comp. on input */
#define SC_CCP_OPEN	0x00000040	/* Look at CCP packets */
#define SC_CCP_UP	0x00000080	/* May send/recv compressed packets */
#define SC_ENABLE_IP	0x00000100	/* IP packets may be exchanged */
#define SC_LOOP_TRAFFIC	0x00000200	/* send traffic to pppd */
#define SC_MULTILINK	0x00000400	/* do multilink encapsulation */
#define SC_MP_SHORTSEQ	0x00000800	/* use short MP sequence numbers */
#define SC_COMP_RUN	0x00001000	/* compressor has been inited */
#define SC_DECOMP_RUN	0x00002000	/* decompressor has been inited */
#define SC_MP_XSHORTSEQ	0x00004000	/* transmit short MP seq numbers */
#define SC_DEBUG	0x00010000	/* enable debug messages */
#define SC_LOG_INPKT	0x00020000	/* log contents of good pkts recvd */
#define SC_LOG_OUTPKT	0x00040000	/* log contents of pkts sent */
#define SC_LOG_RAWIN	0x00080000	/* log all chars received */
#define SC_LOG_FLUSH	0x00100000	/* log all chars flushed */
#define	SC_SYNC		0x00200000	/* synchronous serial mode */
#define	SC_MUST_COMP    0x00400000	/* no uncompressed packets may be sent or received */
#define	SC_MASK		0x0f600fff	/* bits that user can change */

/* state bits */
#define SC_XMIT_BUSY	0x10000000	/* (used by isdn_ppp?) */
#define SC_RCV_ODDP	0x08000000	/* have rcvd char with odd parity */
#define SC_RCV_EVNP	0x04000000	/* have rcvd char with even parity */
#define SC_RCV_B7_1	0x02000000	/* have rcvd char with bit 7 = 1 */
#define SC_RCV_B7_0	0x01000000	/* have rcvd char with bit 7 = 0 */
#define SC_DC_FERROR	0x00800000	/* fatal decomp error detected */
#define SC_DC_ERROR	0x00400000	/* non-fatal decomp error detected */

/* Used with PPPIOCGNPMODE/PPPIOCSNPMODE */
struct npioctl {
	int		protocol;	/* PPP protocol, e.g. PPP_IP */
	enum NPmode	mode;
};

/* Structure describing a CCP configuration option, for PPPIOCSCOMPRESS */
struct ppp_option_data {
	__u8	__user *ptr;
	__u32	length;
	int	transmit;
};

/* For PPPIOCGL2TPSTATS */
struct pppol2tp_ioc_stats {
	__u16		tunnel_id;	/* redundant */
	__u16		session_id;	/* if zero, get tunnel stats */
	__u32		using_ipsec:1;	/* valid only for session_id == 0 */
	__aligned_u64	tx_packets;
	__aligned_u64	tx_bytes;
	__aligned_u64	tx_errors;
	__aligned_u64	rx_packets;
	__aligned_u64	rx_bytes;
	__aligned_u64	rx_seq_discards;
	__aligned_u64	rx_oos_packets;
	__aligned_u64	rx_errors;
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
#define PPPIOCSPASS	_IOW('t', 71, struct sock_fprog) /* set pass filter */
#define PPPIOCSACTIVE	_IOW('t', 70, struct sock_fprog) /* set active filt */
#define PPPIOCGDEBUG	_IOR('t', 65, int)	/* Read debug level */
#define PPPIOCSDEBUG	_IOW('t', 64, int)	/* Set debug level */
#define PPPIOCGIDLE	_IOR('t', 63, struct ppp_idle) /* get idle time */
#define PPPIOCNEWUNIT	_IOWR('t', 62, int)	/* create new ppp unit */
#define PPPIOCATTACH	_IOW('t', 61, int)	/* attach to ppp unit */
#define PPPIOCDETACH	_IOW('t', 60, int)	/* detach from ppp unit/chan */
#define PPPIOCSMRRU	_IOW('t', 59, int)	/* set multilink MRU */
#define PPPIOCCONNECT	_IOW('t', 58, int)	/* connect channel to unit */
#define PPPIOCDISCONN	_IO('t', 57)		/* disconnect channel */
#define PPPIOCATTCHAN	_IOW('t', 56, int)	/* attach to ppp channel */
#define PPPIOCGCHAN	_IOR('t', 55, int)	/* get ppp channel number */
#define PPPIOCGL2TPSTATS _IOR('t', 54, struct pppol2tp_ioc_stats)

#define SIOCGPPPSTATS   (SIOCDEVPRIVATE + 0)
#define SIOCGPPPVER     (SIOCDEVPRIVATE + 1)	/* NEVER change this!! */
#define SIOCGPPPCSTATS  (SIOCDEVPRIVATE + 2)

#endif /* _PPP_IOCTL_H */
