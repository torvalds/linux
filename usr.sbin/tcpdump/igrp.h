/*	$OpenBSD: igrp.h,v 1.5 2022/12/28 21:30:19 jmc Exp $	*/

/* Cisco IGRP definitions */

/* IGRP Header */

struct igrphdr {
#ifdef WORDS_BIGENDIAN
	u_char ig_v:4;		/* protocol version number */
	u_char ig_op:4;		/* opcode */
#else
	u_char ig_op:4;		/* opcode */
	u_char ig_v:4;		/* protocol version number */
#endif
	u_char ig_ed;		/* edition number */
	u_short ig_as;		/* autonomous system number */
	u_short ig_ni;		/* number of subnet in local net */
	u_short ig_ns;		/* number of networks in AS */
	u_short ig_nx;		/* number of networks outside AS */
	u_short ig_sum;		/* checksum of IGRP header & data */
};

#define IGRP_UPDATE	1
#define IGRP_REQUEST	2

/* IGRP routing entry */

struct igrprte {
	u_char igr_net[3];	/* 3 significant octets of IP address */
	u_char igr_dly[3];	/* delay in tens of microseconds */
	u_char igr_bw[3];	/* bandwidth in units of 1 kb/s */
	u_char igr_mtu[2];	/* MTU in octets */
	u_char igr_rel;		/* percent packets successfully tx/rx */
	u_char igr_ld;		/* percent of channel occupied */
	u_char igr_hct;		/* hop count */
};

#define IGRP_RTE_SIZE	14	/* don't believe sizeof ! */
