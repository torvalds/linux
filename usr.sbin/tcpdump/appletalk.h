/*	$OpenBSD: appletalk.h,v 1.8 2007/10/07 16:41:05 deraadt Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * AppleTalk protocol formats (courtesy Bill Croft of Stanford/SUMEX).
 *
 * @(#) $Id: appletalk.h,v 1.8 2007/10/07 16:41:05 deraadt Exp $ (LBL)
 */

struct LAP {
	u_char	dst;
	u_char	src;
	u_char	type;
};
#define lapShortDDP	1	/* short DDP type */
#define lapDDP		2	/* DDP type */
#define lapKLAP		'K'	/* Kinetics KLAP type */

/* Datagram Delivery Protocol */

struct atDDP {
	u_short	length;
	u_short	checksum;
	u_short	dstNet;
	u_short	srcNet;
	u_char	dstNode;
	u_char	srcNode;
	u_char	dstSkt;
	u_char	srcSkt;
	u_char	type;
};

struct atShortDDP {
	u_short	length;
	u_char	dstSkt;
	u_char	srcSkt;
	u_char	type;
};

#define	ddpMaxWKS	0x7F
#define	ddpMaxData	586
#define	ddpLengthMask	0x3FF
#define	ddpHopShift	10
#define	ddpSize		13	/* size of DDP header (avoid struct padding) */
#define	ddpSSize	5
#define	ddpWKS		128	/* boundary of DDP well known sockets */
#define	ddpRTMP		1	/* RTMP type */
#define	ddpRTMPrequest	5	/* RTMP request type */
#define	ddpNBP		2	/* NBP type */
#define	ddpATP		3	/* ATP type */
#define	ddpECHO		4	/* ECHO type */
#define	ddpIP		22	/* IP type */
#define	ddpARP		23	/* ARP type */
#define	ddpKLAP		0x4b	/* Kinetics KLAP type */


/* AppleTalk Transaction Protocol */

struct atATP {
	u_char	control;
	u_char	bitmap;
	u_short	transID;
	int32_t userData;
};

#define	atpReqCode	0x40
#define	atpRspCode	0x80
#define	atpRelCode	0xC0
#define	atpXO		0x20
#define	atpEOM		0x10
#define	atpSTS		0x08
#define	atpFlagMask	0x3F
#define	atpControlMask	0xF8
#define	atpMaxNum	8
#define	atpMaxData	578


/* AppleTalk Echo Protocol */

struct atEcho {
	u_char	echoFunction;
	u_char	*echoData;
};

#define echoSkt		4		/* the echoer socket */
#define echoSize	1		/* size of echo header */
#define echoRequest	1		/* echo request */
#define echoReply	2		/* echo request */


/* Name Binding Protocol */

struct atNBP {
	u_char	control;
	u_char	id;
};

struct atNBPtuple {
	u_short	net;
	u_char	node;
	u_char	skt;
	u_char	enumerator;
};

#define	nbpBrRq		0x10
#define	nbpLkUp		0x20
#define	nbpLkUpReply	0x30
#define	nbpFwd		0x40
/* The following are not standard, but netatalk uses them. */
#define	nbpNATLKrgstr	0xc0
#define	nbpNATLKunrgstr	0xd0
#define	nbpNATLKok	0xe0
#define	nbpNATLKerr	0xf0

#define	nbpNIS		2
#define	nbpTupleMax	15

#define	nbpHeaderSize	2
#define nbpTupleSize	5

#define nbpSkt		2		/* NIS */


/* Routing Table Maint. Protocol */

#define	rtmpSkt		1	/* number of RTMP socket */
#define	rtmpSize	4	/* minimum size */
#define	rtmpTupleSize	3


/* Zone Information Protocol */

struct zipHeader {
	u_char	command;
	u_char	netcount;
};

#define	zipHeaderSize	2
#define	zipQuery	1
#define	zipReply	2
#define	zipTakedown	3
#define	zipBringup	4
#define	ddpZIP		6
#define	zipSkt		6
#define	GetMyZone	7
#define	GetZoneList	8

/*
 * UDP port range used for ddp-in-udp encapsulation is 16512-16639
 * for client sockets (128-255) and 200-327 for server sockets
 * (0-127).  We also try to recognize the pre-April 88 server
 * socket range of 768-895.
 */
#define atalk_port(p) \
	(((unsigned)((p) - 16512) < 128) || \
	 ((unsigned)((p) - 200) < 128) || \
	 ((unsigned)((p) - 768) < 128))
