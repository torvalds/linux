/*	$OpenBSD: decnet.h,v 1.7 2007/10/07 16:41:05 deraadt Exp $	*/

/*
 * Copyright (c) 1992, 1994, 1996
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
 * @(#) $Id: decnet.h,v 1.7 2007/10/07 16:41:05 deraadt Exp $ (LBL)
 */

typedef unsigned char byte[1];		/* single byte field */
typedef unsigned char word[2];		/* 2 byte field */
typedef unsigned char longword[4];	/* 4 bytes field */

/*
 * Definitions for DECNET Phase IV protocol headers
 */
union etheraddress {
	unsigned char   dne_addr[6];		/* full ethernet address */
	struct {
		unsigned char dne_hiord[4];	/* DECnet HIORD prefix */
		unsigned char dne_nodeaddr[2];	/* DECnet node address */
	} dne_remote;
};

typedef union etheraddress etheraddr;	/* Ethernet address */

#define HIORD 0x000400aa		/* high 32-bits of address (swapped) */

#define AREAMASK	0176000		/* mask for area field */
#define	AREASHIFT	10		/* bit-offset for area field */
#define NODEMASK	01777		/* mask for node address field */

#define DN_MAXADDL	20		/* max size of DECnet address */
struct dn_naddr {
	unsigned short	a_len;		/* length of address */
	unsigned char a_addr[DN_MAXADDL]; /* address as bytes */
};

/*
 * Define long and short header formats.
 */
struct shorthdr
  {
    byte	sh_flags;		/* route flags */
    word	sh_dst;			/* destination node address */
    word	sh_src;			/* source node address */
    byte	sh_visits;		/* visit count */
  };

struct longhdr
  {
    byte	lg_flags;		/* route flags */
    byte	lg_darea;		/* destination area (reserved) */
    byte	lg_dsarea;		/* destination subarea (reserved) */
    etheraddr	lg_dst;			/* destination id */
    byte	lg_sarea;		/* source area (reserved) */
    byte	lg_ssarea;		/* source subarea (reserved) */
    etheraddr	lg_src;			/* source id */
    byte	lg_nextl2;		/* next level 2 router (reserved) */
    byte	lg_visits;		/* visit count */
    byte	lg_service;		/* service class (reserved) */
    byte	lg_pt;			/* protocol type (reserved) */
  };

union routehdr
  {
    struct shorthdr rh_short;		/* short route header */
    struct longhdr rh_long;		/* long route header */
  };

/*
 * Define the values of various fields in the protocol messages.
 *
 * 1. Data packet formats.
 */
#define RMF_MASK	7		/* mask for message type */
#define RMF_SHORT	2		/* short message format */
#define RMF_LONG	6		/* long message format */
#ifndef RMF_RQR
#define RMF_RQR		010		/* request return to sender */
#define RMF_RTS		020		/* returning to sender */
#define RMF_IE		040		/* intra-ethernet packet */
#endif /* RMR_RQR */
#define RMF_FVER	0100		/* future version flag */
#define RMF_PAD		0200		/* pad field */
#define RMF_PADMASK	0177		/* pad field mask */

#define VIS_MASK	077		/* visit field mask */

/*
 * 2. Control packet formats.
 */
#define RMF_CTLMASK	017		/* mask for message type */
#define RMF_CTLMSG	01		/* control message indicator */
#define RMF_INIT	01		/* initialization message */
#define RMF_VER		03		/* verification message */
#define RMF_TEST	05		/* hello and test message */
#define RMF_L1ROUT	07		/* level 1 routing message */
#define RMF_L2ROUT	011		/* level 2 routing message */
#define RMF_RHELLO	013		/* router hello message */
#define RMF_EHELLO	015		/* endnode hello message */

#define TI_L2ROUT	01		/* level 2 router */
#define TI_L1ROUT	02		/* level 1 router */
#define TI_ENDNODE	03		/* endnode */
#define TI_VERIF	04		/* verification required */
#define TI_BLOCK	010		/* blocking requested */

#define VE_VERS		2		/* version number (2) */
#define VE_ECO		0		/* ECO number */
#define VE_UECO		0		/* user ECO number (0) */

#define P3_VERS		1		/* phase III version number (1) */
#define P3_ECO		3		/* ECO number (3) */
#define P3_UECO		0		/* user ECO number (0) */

#define II_L2ROUT	01		/* level 2 router */
#define II_L1ROUT	02		/* level 1 router */
#define II_ENDNODE	03		/* endnode */
#define II_VERIF	04		/* verification required */
#define II_NOMCAST	040		/* no multicast traffic accepted */
#define II_BLOCK	0100		/* blocking requested */
#define II_TYPEMASK	03		/* mask for node type */

#define TESTDATA	0252		/* test data bytes */
#define TESTLEN		1		/* length of transmitted test data */

/*
 * Define control message formats.
 */
struct initmsgIII			/* phase III initialization message */
  {
    byte	inIII_flags;		/* route flags */
    word	inIII_src;		/* source node address */
    byte	inIII_info;		/* routing layer information */
    word	inIII_blksize;		/* maximum data link block size */
    byte	inIII_vers;		/* version number */
    byte	inIII_eco;		/* ECO number */
    byte	inIII_ueco;		/* user ECO number */
    byte	inIII_rsvd;		/* reserved image field */
  };

struct initmsg				/* initialization message */
  {
    byte	in_flags;		/* route flags */
    word	in_src;			/* source node address */
    byte	in_info;		/* routing layer information */
    word	in_blksize;		/* maximum data link block size */
    byte	in_vers;		/* version number */
    byte	in_eco;			/* ECO number */
    byte	in_ueco;		/* user ECO number */
    word	in_hello;		/* hello timer */
    byte	in_rsvd;		/* reserved image field */
  };

struct verifmsg				/* verification message */
  {
    byte	ve_flags;		/* route flags */
    word	ve_src;			/* source node address */
    byte	ve_fcnval;		/* function value image field */
  };

struct testmsg				/* hello and test message */
  {
    byte	te_flags;		/* route flags */
    word	te_src;			/* source node address */
    byte	te_data;		/* test data image field */
  };

struct l1rout				/* level 1 routing message */
  {
    byte	r1_flags;		/* route flags */
    word	r1_src;			/* source node address */
    byte	r1_rsvd;		/* reserved field */
  };

struct l2rout				/* level 2 routing message */
  {
    byte	r2_flags;		/* route flags */
    word	r2_src;			/* source node address */
    byte	r2_rsvd;		/* reserved field */
  };

struct rhellomsg			/* router hello message */
  {
    byte	rh_flags;		/* route flags */
    byte	rh_vers;		/* version number */
    byte	rh_eco;			/* ECO number */
    byte	rh_ueco;		/* user ECO number */
    etheraddr	rh_src;			/* source id */
    byte	rh_info;		/* routing layer information */
    word	rh_blksize;		/* maximum data link block size */
    byte	rh_priority;		/* router's priority */
    byte	rh_area;		/* reserved */
    word	rh_hello;		/* hello timer */
    byte	rh_mpd;			/* reserved */
  };

struct ehellomsg			/* endnode hello message */
  {
    byte	eh_flags;		/* route flags */
    byte	eh_vers;		/* version number */
    byte	eh_eco;			/* ECO number */
    byte	eh_ueco;		/* user ECO number */
    etheraddr	eh_src;			/* source id */
    byte	eh_info;		/* routing layer information */
    word	eh_blksize;		/* maximum data link block size */
    byte	eh_area;		/* area (reserved) */
    byte	eh_seed[8];		/* verification seed */
    etheraddr	eh_router;		/* designated router */
    word	eh_hello;		/* hello timer */
    byte	eh_mpd;			/* (reserved) */
    byte	eh_data;		/* test data image field */
  };

union controlmsg
  {
    struct initmsg	cm_init;	/* initialization message */
    struct verifmsg	cm_ver;		/* verification message */
    struct testmsg	cm_test;	/* hello and test message */
    struct l1rout	cm_l1rou;	/* level 1 routing message */
    struct l2rout	cm_l2rout;	/* level 2 routing message */
    struct rhellomsg	cm_rhello;	/* router hello message */
    struct ehellomsg	cm_ehello;	/* endnode hello message */
  };

/* Macros for decoding routing-info fields */
#define	RI_COST(x)	((x)&0777)
#define	RI_HOPS(x)	(((x)>>10)&037)

/*
 * NSP protocol fields and values.
 */

#define NSP_TYPEMASK 014		/* mask to isolate type code */
#define NSP_SUBMASK 0160		/* mask to isolate subtype code */
#define NSP_SUBSHFT 4			/* shift to move subtype code */

#define MFT_DATA 0			/* data message */
#define MFT_ACK  04			/* acknowledgement message */
#define MFT_CTL  010			/* control message */

#define MFS_ILS  020			/* data or I/LS indicator */
#define MFS_BOM  040			/* beginning of message (data) */
#define MFS_MOM  0			/* middle of message (data) */
#define MFS_EOM  0100			/* end of message (data) */
#define MFS_INT  040			/* interrupt message */

#define MFS_DACK 0			/* data acknowledgement */
#define MFS_IACK 020			/* I/LS acknowledgement */
#define MFS_CACK 040			/* connect acknowledgement */

#define MFS_NOP  0			/* no operation */
#define MFS_CI   020			/* connect initiate */
#define MFS_CC   040			/* connect confirm */
#define MFS_DI   060			/* disconnect initiate */
#define MFS_DC   0100			/* disconnect confirm */
#define MFS_RCI  0140			/* retransmitted connect initiate */

#define SGQ_ACK  0100000		/* ack */
#define SGQ_NAK  0110000		/* negative ack */
#define SGQ_OACK 0120000		/* other channel ack */
#define SGQ_ONAK 0130000		/* other channel negative ack */
#define SGQ_MASK 07777			/* mask to isolate seq # */
#define SGQ_OTHER 020000		/* other channel qualifier */
#define SGQ_DELAY 010000		/* ack delay flag */

#define SGQ_EOM  0100000		/* pseudo flag for end-of-message */

#define LSM_MASK 03			/* mask for modifier field */
#define LSM_NOCHANGE 0			/* no change */
#define LSM_DONOTSEND 1			/* do not send data */
#define LSM_SEND 2			/* send data */

#define LSI_MASK 014			/* mask for interpretation field */
#define LSI_DATA 0			/* data segment or message count */
#define LSI_INTR 4			/* interrupt request count */
#define LSI_INTM 0377			/* funny marker for int. message */

#define COS_MASK 014			/* mask for flow control field */
#define COS_NONE 0			/* no flow control */
#define COS_SEGMENT 04			/* segment flow control */
#define COS_MESSAGE 010			/* message flow control */
#define COS_CRYPTSER 020		/* cryptographic services requested */
#define COS_DEFAULT 1			/* default value for field */

#define COI_MASK 3			/* mask for version field */
#define COI_32 0			/* version 3.2 */
#define COI_31 1			/* version 3.1 */
#define COI_40 2			/* version 4.0 */
#define COI_41 3			/* version 4.1 */

#define MNU_MASK 140			/* mask for session control version */
#define MNU_10 000				/* session V1.0 */
#define MNU_20 040				/* session V2.0 */
#define MNU_ACCESS 1			/* access control present */
#define MNU_USRDATA 2			/* user data field present */
#define MNU_INVKPROXY 4			/* invoke proxy field present */
#define MNU_UICPROXY 8			/* use uic-based proxy */

#define DC_NORESOURCES 1		/* no resource reason code */
#define DC_NOLINK 41			/* no link terminate reason code */
#define DC_COMPLETE 42			/* disconnect complete reason code */

#define DI_NOERROR 0			/* user disconnect */
#define DI_SHUT 3			/* node is shutting down */
#define DI_NOUSER 4			/* destination end user does not exist */
#define DI_INVDEST 5			/* invalid end user destination */
#define DI_REMRESRC 6			/* insufficient remote resources */
#define DI_TPA 8			/* third party abort */
#define DI_PROTOCOL 7			/* protocol error discovered */
#define DI_ABORT 9			/* user abort */
#define DI_LOCALRESRC 32		/* insufficient local resources */
#define DI_REMUSERRESRC 33		/* insufficient remote user resources */
#define DI_BADACCESS 34			/* bad access control information */
#define DI_BADACCNT 36			/* bad ACCOUNT information */
#define DI_CONNECTABORT 38		/* connect request cancelled */
#define DI_TIMEDOUT 38			/* remote node or user crashed */
#define DI_UNREACHABLE 39		/* local timers expired due to ... */
#define DI_BADIMAGE 43			/* bad image data in connect */
#define DI_SERVMISMATCH 54		/* cryptographic service mismatch */

#define UC_OBJREJECT 0			/* object rejected connect */
#define UC_USERDISCONNECT 0		/* user disconnect */
#define UC_RESOURCES 1			/* insufficient resources (local or remote) */
#define UC_NOSUCHNODE 2			/* unrecognized node name */
#define UC_REMOTESHUT 3			/* remote node shutting down */
#define UC_NOSUCHOBJ 4			/* unrecognized object */
#define UC_INVOBJFORMAT 5		/* invalid object name format */
#define UC_OBJTOOBUSY 6			/* object too busy */
#define UC_NETWORKABORT 8		/* network abort */
#define UC_USERABORT 9			/* user abort */
#define UC_INVNODEFORMAT 10		/* invalid node name format */
#define UC_LOCALSHUT 11			/* local node shutting down */
#define UC_ACCESSREJECT 34		/* invalid access control information */
#define UC_NORESPONSE 38		/* no response from object */
#define UC_UNREACHABLE 39		/* node unreachable */

/*
 * NSP message formats.
 */
struct nsphdr				/* general nsp header */
  {
    byte	nh_flags;		/* message flags */
    word	nh_dst;			/* destination link address */
    word	nh_src;			/* source link address */
  };

struct seghdr				/* data segment header */
  {
    byte	sh_flags;		/* message flags */
    word	sh_dst;			/* destination link address */
    word	sh_src;			/* source link address */
    word	sh_seq[3];		/* sequence numbers */
  };

struct minseghdr			/* minimum data segment header */
  {
    byte	ms_flags;		/* message flags */
    word	ms_dst;			/* destination link address */
    word	ms_src;			/* source link address */
    word	ms_seq;			/* sequence number */
  };

struct lsmsg				/* link service message (after hdr) */
  {
    byte	ls_lsflags;		/* link service flags */
    byte	ls_fcval;		/* flow control value */
  };

struct ackmsg				/* acknowledgement message */
  {
    byte	ak_flags;		/* message flags */
    word	ak_dst;			/* destination link address */
    word	ak_src;			/* source link address */
    word	ak_acknum[2];		/* acknowledgement numbers */
  };

struct minackmsg			/* minimum acknowledgement message */
  {
    byte	mk_flags;		/* message flags */
    word	mk_dst;			/* destination link address */
    word	mk_src;			/* source link address */
    word	mk_acknum;		/* acknowledgement number */
  };

struct ciackmsg				/* connect acknowledgement message */
  {
    byte	ck_flags;		/* message flags */
    word	ck_dst;			/* destination link address */
  };

struct cimsg				/* connect initiate message */
  {
    byte	ci_flags;		/* message flags */
    word	ci_dst;			/* destination link address (0) */
    word	ci_src;			/* source link address */
    byte	ci_services;		/* requested services */
    byte	ci_info;		/* information */
    word	ci_segsize;		/* maximum segment size */
  };

struct ccmsg				/* connect confirm message */
  {
    byte	cc_flags;		/* message flags */
    word	cc_dst;			/* destination link address */
    word	cc_src;			/* source link address */
    byte	cc_services;		/* requested services */
    byte	cc_info;		/* information */
    word	cc_segsize;		/* maximum segment size */
    byte	cc_optlen;		/* optional data length */
  };

struct cnmsg				/* generic connect message */
  {
    byte	cn_flags;		/* message flags */
    word	cn_dst;			/* destination link address */
    word	cn_src;			/* source link address */
    byte	cn_services;		/* requested services */
    byte	cn_info;		/* information */
    word	cn_segsize;		/* maximum segment size */
  };

struct dimsg				/* disconnect initiate message */
  {
    byte	di_flags;		/* message flags */
    word	di_dst;			/* destination link address */
    word	di_src;			/* source link address */
    word	di_reason;		/* reason code */
    byte	di_optlen;		/* optional data length */
  };

struct dcmsg				/* disconnect confirm message */
  {
    byte	dc_flags;		/* message flags */
    word	dc_dst;			/* destination link address */
    word	dc_src;			/* source link address */
    word	dc_reason;		/* reason code */
  };
