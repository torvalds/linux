/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: libunimsg/netnatm/msg/unistruct.h,v 1.7 2004/07/16 18:42:22 brandt Exp $
 *
 * This file defines all structures that are used by
 * API users.
 */
#ifndef _NETNATM_MSG_UNISTRUCT_H_
#define _NETNATM_MSG_UNISTRUCT_H_

#include <netnatm/msg/uni_config.h>

/*
 * define IE and MSG header
 */
#include <netnatm/msg/uni_hdr.h>

/*
 * define all IE's
 */
/*************************************************************************
 *
 * Free FORM IE
 */
struct uni_ie_unrec {
	struct uni_iehdr h;
	uint8_t id;		/* ID of this IE */
	u_int len;		/* data length */
	u_char data[128];	/* arbitrary maximum length */
};

/*************************************************************************
 *
 * ATM adaptation layer parameters information element
 */
enum {
	UNI_AAL_SUB_ID		= 0x85,
	UNI_AAL_CBR_ID		= 0x86,
	UNI_AAL_MULT_ID		= 0x87,
	UNI_AAL_SCREC_ID	= 0x88,
	UNI_AAL_ECM_ID		= 0x89,
	UNI_AAL_BSIZE_ID	= 0x8a,
	UNI_AAL_PART_ID		= 0x8b,
	UNI_AAL_FWDCPCS_ID	= 0x8c,
	UNI_AAL_BWDCPCS_ID	= 0x81,
	UNI_AAL_MID_ID		= 0x82,
	UNI_AAL_SSCS_ID		= 0x84,
};

enum uni_aal {
	UNI_AAL_0	= 0x00,	/* voice */
	UNI_AAL_1	= 0x01,
	UNI_AAL_2	= 0x02,
	UNI_AAL_4	= 0x03,	/* same as AAL 3 */
	UNI_AAL_5	= 0x05,
	UNI_AAL_USER	= 0x10,
};
enum uni_aal1_subtype {
	UNI_AAL1_SUB_NULL	= 0x00,
	UNI_AAL1_SUB_VOICE	= 0x01,
	UNI_AAL1_SUB_CIRCUIT	= 0x02,
	UNI_AAL1_SUB_HQAUDIO	= 0x04,
	UNI_AAL1_SUB_VIDEO	= 0x05,
};
enum uni_aal1_cbr {
	UNI_AAL1_CBR_64		= 0x01,
	UNI_AAL1_CBR_1544	= 0x04,
	UNI_AAL1_CBR_6312	= 0x05,
	UNI_AAL1_CBR_32064	= 0x06,
	UNI_AAL1_CBR_44736	= 0x07,
	UNI_AAL1_CBR_97728	= 0x08,
	UNI_AAL1_CBR_2048	= 0x10,
	UNI_AAL1_CBR_8448	= 0x11,
	UNI_AAL1_CBR_34368	= 0x12,
	UNI_AAL1_CBR_139264	= 0x13,
	UNI_AAL1_CBR_N64	= 0x40,
	UNI_AAL1_CBR_N8		= 0x41,
};
enum uni_aal1_screc {
	UNI_AAL1_SCREC_NULL	= 0x00,	/* synchr. circuit transport */
	UNI_AAL1_SCREC_SRTS	= 0x01,	/* synchr. residual timestamp */
	UNI_AAL1_SCREC_ACLK	= 0x02,	/* adaptive clock */
};
enum uni_aal1_ecm {
	UNI_AAL1_ECM_NULL	= 0x00,	/* no error correction */
	UNI_AAL1_ECM_LOSS	= 0x01,	/* for loss sensitive signals */
	UNI_AAL1_ECM_DELAY	= 0x02,	/* for delay sensitive signals */
};
enum uni_aal_sscs {
	UNI_AAL_SSCS_NULL	= 0x00,	/* Null */
	UNI_AAL_SSCS_SSCOPA	= 0x01,	/* assured SSCOP */
	UNI_AAL_SSCS_SSCOPU	= 0x02,	/* unassured SSCOP */
	UNI_AAL_SSCS_FRAME	= 0x04,	/* frame relay */
};

struct uni_ie_aal {
	struct uni_iehdr h;
	enum uni_aal	type;		/* aal type */

	union {
#define UNI_AAL1_MULT_P	0x01
#define UNI_AAL1_SCREC_P	0x02
#define UNI_AAL1_ECM_P		0x04
#define UNI_AAL1_BSIZE_P	0x08
#define UNI_AAL1_PART_P	0x10
	    struct {
		enum uni_aal1_subtype subtype;	/* AAL1 subtype */
		enum uni_aal1_cbr cbr_rate;	/* AAL1 CBR rate */
		u_int		mult;		/* AAL1 CBR mutliplier */
		enum uni_aal1_screc screc;	/* AAL1 source clock recovery */
		enum uni_aal1_ecm ecm;		/* AAL1 error correction */
		u_int	bsize;			/* AAL1 SDT blocksize */
		u_int	part;			/* AAL1 partial cell fill */
	    } aal1;

#define UNI_AAL4_CPCS_P	0x01
#define UNI_AAL4_MID_P		0x02
#define UNI_AAL4_SSCS_P	0x04
	    struct {
		u_int	fwd_cpcs;	/* max fwd cpcs blocksize */
		u_int	bwd_cpcs;	/* max bkw cpcs blocksize */
		u_int	mid_low;	/* MID low range */
		u_int	mid_high;	/* MID high range */
		enum uni_aal_sscs sscs;	/* sscs type */
	     } aal4;

#define UNI_AAL5_CPCS_P	0x01
#define UNI_AAL5_SSCS_P	0x02
	    struct {
		u_int	fwd_cpcs;	/* max fwd cpcs blocksize */
		u_int	bwd_cpcs;	/* max bkw cpcs blocksize */
		enum uni_aal_sscs sscs;	/* sscs type */
	     } aal5;

	    struct {
		u_int	len;		/* number of bytes */
		u_char	user[4];	/* user data */
	    } aalu;
	} u;
};

/*************************************************************************
 *
 * Called party number information element
 * Called party subaddress information element
 * Calling party number information element
 * Calling party subaddress information element
 * Q.2951/UNI4.0 Connected number information element
 * Q.2951/UNI4.0 Connected subaddress information element
 */
enum uni_addr_type {
	UNI_ADDR_UNKNOWN	= 0x0,
	UNI_ADDR_INTERNATIONAL	= 0x1,
	UNI_ADDR_NATIONAL	= 0x2,	/* not sup */
	UNI_ADDR_NETWORK	= 0x3,	/* not sup */
	UNI_ADDR_SUBSCR		= 0x4,	/* not sup */
	UNI_ADDR_ABBR		= 0x6,	/* not sup */
};
enum uni_addr_plan {
	/* UNI_ADDR_UNKNOWN	= 0x0, */	/* not sup */
	UNI_ADDR_E164		= 0x1,
	UNI_ADDR_ATME		= 0x2,
	UNI_ADDR_DATA		= 0x3,	/* not sup */
	UNI_ADDR_PRIVATE	= 0x9,	/* not sup */
};
enum uni_subaddr_type {
	UNI_SUBADDR_NSAP	= 0x0,
	UNI_SUBADDR_ATME	= 0x1,
	UNI_SUBADDR_USER	= 0x2,	/* not sup */
};
enum uni_addr_pres {
	UNI_ADDR_PRES		= 0x0,
	UNI_ADDR_RESTRICT	= 0x1,
	UNI_ADDR_NONUMBER	= 0x2,
};
enum uni_addr_screen {
	UNI_ADDR_SCREEN_NOT	= 0x0,
	UNI_ADDR_SCREEN_PASSED	= 0x1,
	UNI_ADDR_SCREEN_FAILED	= 0x2,
	UNI_ADDR_SCREEN_NET	= 0x3,
};

/* don't use bitfields to get a defined structure layout */
struct uni_addr {
	uint8_t			type;
	uint8_t			plan;
	uint8_t			len;
	u_char			addr[UNI_ADDR_MAXLEN];
};
struct uni_subaddr {
	enum uni_subaddr_type	type;
	u_int			len;
	u_char			addr[UNI_SUBADDR_MAXLEN];
};

struct uni_ie_called {
	struct uni_iehdr	h;
	struct uni_addr	addr;
};

struct uni_ie_calledsub {
	struct uni_iehdr	h;
	struct uni_subaddr	addr;
};

struct uni_ie_calling {
	struct uni_iehdr	h;
#define UNI_CALLING_SCREEN_P 0x0001

	struct uni_addr		addr;
	enum uni_addr_pres	pres;
	enum uni_addr_screen	screen;
};

struct uni_ie_callingsub {
	struct uni_iehdr	h;
	struct uni_subaddr	addr;
};

struct uni_ie_conned {
	struct uni_iehdr	h;
#define UNI_CONNED_SCREEN_P 0x0001

	struct uni_addr		addr;
	enum uni_addr_pres	pres;
	enum uni_addr_screen	screen;
};

struct uni_ie_connedsub {
	struct uni_iehdr	h;
	struct uni_subaddr	addr;
};

/*************************************************************************
 *
 * Broadband bearer capability descriptor
 * On reception of an old bearer descriptor, it is automatically
 * converted to a new, legal one.
 */
enum uni_bearer_class {
	UNI_BEARER_A		= 0x01,
	UNI_BEARER_C		= 0x03,
	UNI_BEARER_X		= 0x10,
	UNI_BEARER_TVP		= 0x30,
};

enum uni_bearer_atc {
	UNI_BEARER_ATC_CBR	= 0x05,
	UNI_BEARER_ATC_CBR1	= 0x07,
	UNI_BEARER_ATC_VBR	= 0x09,
	UNI_BEARER_ATC_VBR1	= 0x13,
	UNI_BEARER_ATC_NVBR	= 0x0a,
	UNI_BEARER_ATC_NVBR1	= 0x0b,
	UNI_BEARER_ATC_ABR	= 0x0c,

	UNI_BEARER_ATCX_0	= 0x00,
	UNI_BEARER_ATCX_1	= 0x01,
	UNI_BEARER_ATCX_2	= 0x02,
	UNI_BEARER_ATCX_4	= 0x04,
	UNI_BEARER_ATCX_6	= 0x06,
	UNI_BEARER_ATCX_8	= 0x08,
};

enum uni_bearer_clip {
	UNI_BEARER_NOCLIP	= 0x0,
	UNI_BEARER_CLIP		= 0x1,
};

enum uni_bearer_cfg {
	UNI_BEARER_P2P		= 0x0,
	UNI_BEARER_MP		= 0x1,
};

struct uni_ie_bearer {
	struct uni_iehdr	h;
#define UNI_BEARER_ATC_P	0x02

	enum uni_bearer_class	bclass;		/* bearer class */
	enum uni_bearer_atc	atc;		/* ATM transfer capability */
	enum uni_bearer_clip	clip;		/* suspectibility to clipping */
	enum uni_bearer_cfg	cfg;		/* u-plane configuration */
};

/*************************************************************************
 *
 * Broadband higher layer information element
 */
enum uni_bhli {
	UNI_BHLI_ISO	= 0x00,	/* IDO defined */
	UNI_BHLI_USER	= 0x01,	/* user specific */
	UNI_BHLI_VENDOR	= 0x03,	/* vendor specific */
};

struct uni_ie_bhli {
	struct uni_iehdr	h;
	enum uni_bhli		type;
	u_char			info[8];
	u_int			len;
};

/*************************************************************************
 *
 * Boradband lower layer information element
 */
enum {
	UNI_BLLI_L1_ID		= 0x1,
	UNI_BLLI_L2_ID		= 0x2,
	UNI_BLLI_L3_ID		= 0x3,
};

enum uni_blli_l2 {
	UNI_BLLI_L2_BASIC	= 0x01,
	UNI_BLLI_L2_Q921	= 0x02,
	UNI_BLLI_L2_X25LL	= 0x06,
	UNI_BLLI_L2_X25ML	= 0x07,
	UNI_BLLI_L2_LABP	= 0x08,
	UNI_BLLI_L2_HDLC_ARM	= 0x09,
	UNI_BLLI_L2_HDLC_NRM	= 0x0a,
	UNI_BLLI_L2_HDLC_ABM	= 0x0b,
	UNI_BLLI_L2_LAN		= 0x0c,
	UNI_BLLI_L2_X75		= 0x0d,
	UNI_BLLI_L2_Q922	= 0x0e,
	UNI_BLLI_L2_USER	= 0x10,
	UNI_BLLI_L2_ISO7776	= 0x11,
};

enum uni_blli_l2_mode {
	UNI_BLLI_L2NORM		= 0x1,
	UNI_BLLI_L2EXT		= 0x2,
};

enum uni_blli_l3 {
	UNI_BLLI_L3_X25		= 0x06,
	UNI_BLLI_L3_ISO8208	= 0x07,
	UNI_BLLI_L3_X223	= 0x08,
	UNI_BLLI_L3_CLMP	= 0x09,
	UNI_BLLI_L3_T70		= 0x0a,
	UNI_BLLI_L3_TR9577	= 0x0b,
	UNI_BLLI_L3_H310	= 0x0c,
	UNI_BLLI_L3_H321	= 0x0d,
	UNI_BLLI_L3_USER	= 0x10,
};

enum uni_blli_l3_mode {
	UNI_BLLI_L3NSEQ		= 0x1,	/* normal sequence numbering */
	UNI_BLLI_L3ESEQ		= 0x2,	/* extended sequence numbering */
};

enum uni_blli_l3_psiz {
	UNI_BLLI_L3_16		= 0x4,	/* 16 byte packets */
	UNI_BLLI_L3_32		= 0x5,	/* 32 byte packets */
	UNI_BLLI_L3_64		= 0x6,	/* 64 byte packets */
	UNI_BLLI_L3_128		= 0x7,	/* 128 byte packets */
	UNI_BLLI_L3_256		= 0x8,	/* 256 byte packets */
	UNI_BLLI_L3_512		= 0x9,	/* 512 byte packets */
	UNI_BLLI_L3_1024	= 0xa,	/* 1024 byte packets */
	UNI_BLLI_L3_2048	= 0xb,	/* 2048 byte packets */
	UNI_BLLI_L3_4096	= 0xc,	/* 4096 byte packets */
};

enum uni_blli_l3_ttype {
	UNI_BLLI_L3_TTYPE_RECV	= 0x1,	/* receive only */
	UNI_BLLI_L3_TTYPE_SEND	= 0x2,	/* send only */
	UNI_BLLI_L3_TTYPE_BOTH	= 0x3,	/* both */
};

enum uni_blli_l3_mux {
	UNI_BLLI_L3_MUX_NOMUX	= 0,	/* no multiplexing */
	UNI_BLLI_L3_MUX_TS	= 1,	/* transport stream */
	UNI_BLLI_L3_MUX_TSFEC	= 2,	/* transport stream with FEC */
	UNI_BLLI_L3_MUX_PS	= 3,	/* program stream */
	UNI_BLLI_L3_MUX_PSFEC	= 4,	/* program stream with FEC */
	UNI_BLLI_L3_MUX_H221	= 5,	/* H.221 */
};

enum uni_blli_l3_tcap {
	UNI_BLLI_L3_TCAP_NOIND	= 0,	/* no indication */
	UNI_BLLI_L3_TCAP_AAL1	= 1,	/* only AAL1 */
	UNI_BLLI_L3_TCAP_AAL5	= 2,	/* only AAL5 */
	UNI_BLLI_L3_TCAP_AAL15	= 3,	/* AAL1 and AAL5 */
};

/* Value for l3_ipi: */
enum {
	UNI_BLLI_L3_SNAP	= 0x80,	/* IEEE 802.1 SNAP */
};

struct uni_ie_blli {
	struct uni_iehdr	h;
#define UNI_BLLI_L1_P		0x0001
#define UNI_BLLI_L2_P		0x0002
#define UNI_BLLI_L2_Q933_P	0x0004
#define UNI_BLLI_L2_WSIZ_P	0x0008
#define UNI_BLLI_L2_USER_P	0x0010
#define UNI_BLLI_L3_P		0x0020
#define UNI_BLLI_L3_MODE_P	0x0040
#define UNI_BLLI_L3_PSIZ_P	0x0080
#define UNI_BLLI_L3_WSIZ_P	0x0100
#define UNI_BLLI_L3_USER_P	0x0200
#define UNI_BLLI_L3_IPI_P	0x0400
#define UNI_BLLI_L3_SNAP_P	0x0800
#define UNI_BLLI_L3_TTYPE_P	0x1000
#define UNI_BLLI_L3_MUX_P	0x2000

	u_int			l1:5;		/* layer 1 info */

	enum uni_blli_l2	l2;		/* layer 2 info */
	u_int			l2_q933:2;	/* layer 2 Q.933 use */
	enum uni_blli_l2_mode	l2_mode;	/* layer 2 HDLC mode */ 
	u_char			l2_user;	/* layer 2 user info */
	u_char			l2_wsiz;	/* layer 2 window size */

	enum uni_blli_l3	l3;		/* layer 3 info */
	enum uni_blli_l3_mode	l3_mode;	/* layer 3 mode */
	enum uni_blli_l3_psiz	l3_psiz;	/* layer 3 default packet size */
	u_char			l3_wsiz;	/* layer 3 window size */
	u_char			l3_user;	/* layer 3 user info */
	u_char			l3_ipi;		/* IPI byte */
	u_int			oui;		/* OUI bytes */
	u_int			pid;		/* PID bytes */
	enum uni_blli_l3_ttype	l3_ttype;	/* terminal bytes */
	enum uni_blli_l3_tcap	l3_tcap;	/* terminal capability */
	enum uni_blli_l3_mux	l3_fmux;	/* forward muxing */
	enum uni_blli_l3_mux	l3_bmux;	/* forward muxing */
};

/*************************************************************************
 *
 * Transit network selection IE
 */
struct uni_ie_tns {
	struct uni_iehdr h;
	u_char		net[UNI_TNS_MAXLEN];
	u_int		len;
};

/*************************************************************************
 *
 * Call state information element
 */
enum uni_callstate {
	UNI_CALLSTATE_U0	= 0x00,
	UNI_CALLSTATE_N0	= 0x00,
	UNI_CALLSTATE_NN0	= 0x00,

	UNI_CALLSTATE_U1	= 0x01,
	UNI_CALLSTATE_N1	= 0x01,
	UNI_CALLSTATE_NN1	= 0x01,

	UNI_CALLSTATE_U3	= 0x03,
	UNI_CALLSTATE_N3	= 0x03,
	UNI_CALLSTATE_NN3	= 0x03,

	UNI_CALLSTATE_U4	= 0x04,
	UNI_CALLSTATE_N4	= 0x04,
	UNI_CALLSTATE_NN4	= 0x04,

	UNI_CALLSTATE_U6	= 0x06,
	UNI_CALLSTATE_N6	= 0x06,
	UNI_CALLSTATE_NN6	= 0x06,

	UNI_CALLSTATE_U7	= 0x07,
	UNI_CALLSTATE_N7	= 0x07,
	UNI_CALLSTATE_NN7	= 0x07,

	UNI_CALLSTATE_U8	= 0x08,
	UNI_CALLSTATE_N8	= 0x08,

	UNI_CALLSTATE_U9	= 0x09,
	UNI_CALLSTATE_N9	= 0x09,
	UNI_CALLSTATE_NN9	= 0x09,

	UNI_CALLSTATE_U10	= 0x0a,
	UNI_CALLSTATE_N10	= 0x0a,
	UNI_CALLSTATE_NN10	= 0x0a,

	UNI_CALLSTATE_U11	= 0x0b,
	UNI_CALLSTATE_N11	= 0x0b,
	UNI_CALLSTATE_NN11	= 0x0b,

	UNI_CALLSTATE_U12	= 0x0c,
	UNI_CALLSTATE_N12	= 0x0c,
	UNI_CALLSTATE_NN12	= 0x0c,

	UNI_CALLSTATE_REST0	= 0x00,
	UNI_CALLSTATE_REST1	= 0x3d,
	UNI_CALLSTATE_REST2	= 0x3e,

	UNI_CALLSTATE_U13	= 0x0d,
	UNI_CALLSTATE_N13	= 0x0d,

	UNI_CALLSTATE_U14	= 0x0e,
	UNI_CALLSTATE_N14	= 0x0e,
};

struct uni_ie_callstate {
	struct uni_iehdr	h;
	enum uni_callstate	state;
};

/*************************************************************************
 *
 * Cause information element
 */
enum uni_cause_loc {
	UNI_CAUSE_LOC_USER	= 0x0,
	UNI_CAUSE_LOC_PRIVLOC	= 0x1,
	UNI_CAUSE_LOC_PUBLOC	= 0x2,
	UNI_CAUSE_LOC_TRANSIT	= 0x3,
	UNI_CAUSE_LOC_PUBREM	= 0x4,
	UNI_CAUSE_LOC_PRIVREM	= 0x5,
	UNI_CAUSE_LOC_INTERNAT	= 0x6,
	UNI_CAUSE_LOC_BEYOND	= 0x7,
};

#define UNI_DECLARE_CAUSE_VALUES \
D(UNALL_NUM,	0x01 /*  1*/, COND,	Q.850,	"Unallocated (unassigned) number") \
D(NOROUTE_NET,	0x02 /*  2*/, TNS,	Q.850,	"No route to specified transit network") \
D(NOROUTE,	0x03 /*  3*/, COND,	Q.850,	"No route to destination") \
D(SPTONE,	0x04 /*  4*/, NONE,	Q.850,	"Send special information tone") \
D(BADTRUNK,	0x05 /*  5*/, NONE,	Q.850,	"Misdialled trunk prefix") \
D(BADCHAN,	0x06 /*  6*/, NONE,	Q.850,	"Channel unacceptable") \
D(CALLAWARDED,	0x07 /*  7*/, NONE,	Q.850,	"Call awarded and being delivered in an established channel") \
D(PREEMPT,	0x08 /*  8*/, NONE,	Q.850,	"Preemption") \
D(PREEMPT_RES,	0x09 /*  9*/, NONE,	Q.850,	"Preemption - circuit reserved for reuse") \
D(CLEARING,	0x10 /* 16*/, COND,	Q.850,	"Normal call clearing") \
D(BUSY,		0x11 /* 17*/, CCBS,	Q.850,	"User busy") \
D(NO_RESPONSE,	0x12 /* 18*/, NONE,	Q.850,	"No user responding") \
D(NO_RESP_ALERT,0x13 /* 19*/, NONE,	Q.850,	"No answer from user (user alerted)") \
D(ABSENT,	0x14 /* 20*/, NONE,	Q.850,	"Subscriber absent") \
D(REJECTED,	0x15 /* 21*/, REJ,	Q.850,	"Call rejected") \
D(NUMCHG,	0x16 /* 22*/, NUMBER,	Q.850,	"Number changed") \
D(REDIR,	0x17 /* 23*/, NONE,	Q.850,	"Redirection to new destination") \
N(CLIR_REJECTED,0x17 /* 23*/, NONE,	UNI4.0,	"User rejects call with calling line identification restriction (CLIR)") \
D(EXCHG_ERR,	0x19 /* 25*/, NONE,	Q.850,	"Exchange routing error") \
D(NOSEL_CLEAR,	0x1a /* 26*/, NONE,	Q.850,	"Non-selected user clearing") \
D(DST_OOO,	0x1b /* 27*/, NONE,	Q.850,	"Destination out of order") \
D(INV_ADDR,	0x1c /* 28*/, NONE,	Q.850,	"Invalid number format (address incomplete)") \
D(FAC_REJ,	0x1d /* 29*/, FAC,	Q.850,	"Facility rejected") \
D(STATUS,	0x1e /* 30*/, NONE,	Q.850,	"Response to STATUS ENQUIRY") \
D(UNSPEC,	0x1f /* 31*/, NONE,	Q.850,	"Normal, unspecified") \
D(TMY_PARTY,	0x20 /* 32*/, NONE,	Q.2971,	"Too many pending add party requests") \
D(NOCHAN,	0x22 /* 34*/, CCBS,	Q.850,	"No circuit/channel available") \
N(SOFT_NAVL,	0x22 /* 34*/, NONE,	PNNI1.0,"Requested called party soft PVPC or PVCC not available")\
D(VPCI_NAVL,	0x23 /* 35*/, NONE,	Q.2610,	"Requested VPCI/VCI not available") \
D(VPCI_FAIL,	0x24 /* 36*/, NONE,	Q.2610,	"VPCI/VPI assignment failure") \
D(CRATE_NAVL,	0x25 /* 37*/, CRATE,	Q.2610,	"User cell rate not available") \
D(NET_OOO,	0x26 /* 38*/, NONE,	Q.850,	"Network out of order") \
D(FRAME_OOS,	0x27 /* 39*/, NONE,	Q.850,	"Permanent frame mode connection out of service") \
D(FRAME_OP,	0x28 /* 40*/, NONE,	Q.850,	"Permanent frame mode connection operational") \
D(TEMP,		0x29 /* 41*/, NONE,	Q.850,	"Temporary failure") \
D(CONG,		0x2a /* 42*/, NONE,	Q.850,	"Switching equipment congestion") \
D(ACC_DISC,	0x2b /* 43*/, IE,	Q.850,	"Access information discarded") \
D(REQNOCHAN,	0x2c /* 44*/, NONE,	Q.850,	"Requested circuit/channel not available") \
D(NOVPCI,	0x2d /* 45*/, NONE,	Q.2610,	"No VPCI/VCI available") \
D(PREC_BLOCK,	0x2e /* 46*/, NONE,	Q.850,	"Precedence call blocked") \
D(RESRC_NAVL,	0x2f /* 47*/, NONE,	Q.850,	"Resource unavailable, unspecified") \
D(QOS_NAVL,	0x31 /* 49*/, COND,	Q.850,	"Quality of service not available") \
D(FAC_NOTSUB,	0x32 /* 50*/, FAC,	Q.850,	"Requested facility not subscribed") \
D(OUT_CUG,	0x35 /* 53*/, NONE,	Q.850,	"Outgoing calls barred within CUG") \
N(PGL_CHG,	0x35 /* 53*/, NONE,	PNNI1.0,"Call cleared due to change in PGL") \
D(IN_CUG,	0x37 /* 55*/, NONE,	Q.850,	"Incoming call barred within CUG") \
D(BEARER_NAUTH,	0x39 /* 57*/, ATTR,	Q.850,	"Bearer capability not authorized") \
D(BEARER_NAVL,	0x3a /* 58*/, ATTR,	Q.850,	"Bearer capability not presently available") \
D(INCONS,	0x3e /* 62*/, NONE,	Q.850,	"Inconsistency in designated outgoing access information and subscriber class") \
D(OPT_NAVL,	0x3f /* 63*/, NONE,	Q.850,	"Service or option not available, unspecified") \
D(BEARER_NIMPL,	0x41 /* 65*/, ATTR,	Q.850,	"Bearer capability not implemented") \
D(CHANNEL_NIMPL,0x42 /* 66*/, CHANNEL,	Q.850,	"Channel type not implemented") \
D(FAC_NIMPL,	0x45 /* 69*/, FAC,	Q.850,	"Requested facility not implemented") \
D(RESTR_DIG,	0x46 /* 70*/, NONE,	Q.850,	"Only restricted digital information bearer capability is available") \
D(TRAFFIC_UNSUP,0x49 /* 73*/, NONE,	Q.2971,	"Unsupported combination of traffic parameters") \
N(AAL_UNSUP,	0x4c /* 78*/, NONE,	UNI3.1,	"AAL parameters cannot be supported") \
D(CREF_INV,	0x51 /* 81*/, NONE,	Q.850,	"Invalid call reference value") \
D(CHANNEL_NEX,	0x52 /* 82*/, CHANID,	Q.850,	"Identified channel does not exist") \
D(SUSPENDED,	0x53 /* 83*/, NONE,	Q.850,	"A suspended call exists, but this call identity does not") \
D(CID_INUSE,	0x54 /* 84*/, NONE,	Q.850,	"Call identity in use") \
D(NOTSUSP,	0x55 /* 85*/, NONE,	Q.850,	"No call suspended") \
D(CLEARED,	0x56 /* 86*/, CAUSE,	Q.850,	"Call having requested call identity has been cleared") \
D(NOT_MEMBER,	0x57 /* 87*/, NONE,	Q.850,	"User not member of CUG") \
D(INCOMP,	0x58 /* 88*/, PARAM,	Q.850,	"Incompatible destination") \
D(ENDP_INV,	0x59 /* 89*/, IE,	UNI3.1,	"Invalid endpoint reference") \
D(NEX_CUG,	0x5a /* 90*/, NONE,	Q.850,	"Non-existend CUG") \
D(TRANSIT_INV,	0x5b /* 91*/, NONE,	Q.850,	"Invalid transit network selection") \
D(AALNOTSUPP,	0x5d /* 93*/, NONE,	Q.2610,	"AAL parameters cannot be supported") \
D(INVMSG,	0x5f /* 95*/, NONE,	Q.850,	"Invalid message, unspecified") \
D(MANDAT,	0x60 /* 96*/, IE,	Q.850,	"Mandatory information element is missing") \
D(MTYPE_NIMPL,	0x61 /* 97*/, MTYPE,	Q.850,	"Message type non-existent or not implemented") \
D(MSG_NOTCOMP,	0x62 /* 98*/, MTYPE,	Q.850,	"Message not compatible with call state or message type non-existent or not implemented") \
D(IE_NIMPL,	0x63 /* 99*/, IE,	Q.850,	"Information element/parameter non-existent or not implemented") \
D(IE_INV,	0x64 /*100*/, IE,	Q.850,	"Invalid information element contents") \
D(MSG_INCOMP,	0x65 /*101*/, MTYPE,	Q.850,	"Message not compatible with call state") \
D(RECOVER,	0x66 /*102*/, TIMER,	Q.850,	"Recovery on timer expiry") \
D(PARAM_NEX,	0x67 /*103*/, PARAM,	Q.850,	"Parameter non-existent or not implemented, passed on") \
N(BAD_LENGTH,	0x68 /*104*/, NONE,	UNI3.1,	"Incorrect message length") \
D(PARAM_UNREC,	0x6e /*110*/, PARAM,	Q.850,	"Message with unrecognized parameter, discarded") \
D(PROTO,	0x6f /*111*/, NONE,	Q.850,	"Protocol error, unspecified") \
D(INTERWORKING,	0x7f /*127*/, NONE,	Q.850,	"Interworking, unspecified")

#define D(NAME,VAL,DIAG,STD,STR) UNI_CAUSE_##NAME = VAL,
#define N(NAME,VAL,DIAG,STD,STR) UNI_CAUSE_##NAME = VAL,

enum uni_cause {
UNI_DECLARE_CAUSE_VALUES
};

#undef D
#undef N

enum uni_cause_class {
	UNI_CAUSE_CLASS_NORM	= 0x0,
	UNI_CAUSE_CLASS_NORM1	= 0x1,
	UNI_CAUSE_CLASS_RES	= 0x2,
	UNI_CAUSE_CLASS_NAVL	= 0x3,
	UNI_CAUSE_CLASS_NIMPL	= 0x4,
	UNI_CAUSE_CLASS_INV	= 0x5,
	UNI_CAUSE_CLASS_PROTO	= 0x6,
	UNI_CAUSE_CLASS_INTER	= 0x7,
};
enum uni_cause_pu {
	UNI_CAUSE_PU_PROVIDER	= 0,
	UNI_CAUSE_PU_USER	= 1,
};
enum uni_cause_na {
	UNI_CAUSE_NA_NORMAL	= 0,
	UNI_CAUSE_NA_ABNORMAL	= 1,
};
enum uni_cause_cond {
	UNI_CAUSE_COND_UNKNOWN	= 0,
	UNI_CAUSE_COND_PERM	= 1,
	UNI_CAUSE_COND_TRANS	= 2,
};
enum uni_cause_reason {
	UNI_CAUSE_REASON_USER	= 0x00,
	UNI_CAUSE_REASON_IEMISS	= 0x01,
	UNI_CAUSE_REASON_IESUFF	= 0x02,
};

enum uni_diag {
	UNI_DIAG_NONE,		/* no diagnostics */

	UNI_DIAG_COND,		/* Condition */
	UNI_DIAG_TNS,		/* Transit Network Selector */
	UNI_DIAG_REJ,		/* Call Rejected */
	UNI_DIAG_NUMBER,	/* New Destination */
	UNI_DIAG_CRATE,		/* Traffic descriptor subfield */
	UNI_DIAG_ATTR,		/* Attribute idendity */
	UNI_DIAG_PARAM,		/* Parameter, same as one IE */
	UNI_DIAG_TIMER,		/* timer in ASCII */
	UNI_DIAG_MTYPE,		/* Message type */
	UNI_DIAG_IE,		/* Information element */
	UNI_DIAG_CHANID,	/* VPCI/VCI */

	UNI_DIAG_CAUSE = UNI_DIAG_NONE,		/* Not specified */
	UNI_DIAG_CHANNEL = UNI_DIAG_NONE,	/* For N-ISDN */
	UNI_DIAG_CCBS = UNI_DIAG_NONE,		/* Not used in Q.931 */
	UNI_DIAG_FAC = UNI_DIAG_NONE,		/* Not specified */
};

enum {
	UNI_CAUSE_TRAFFIC_N	= 34-6,
	UNI_CAUSE_IE_N		= 34-6,
	UNI_CAUSE_ATTR_N	= (34-6)/3,
};

struct uni_ie_cause {
	struct uni_iehdr	h;
#define UNI_CAUSE_COND_P	0x0001
#define UNI_CAUSE_REJ_P		0x0002
#define UNI_CAUSE_REJ_USER_P	0x0004
#define UNI_CAUSE_REJ_IE_P	0x0008
#define UNI_CAUSE_IE_P		0x0010
#define UNI_CAUSE_TRAFFIC_P	0x0020
#define UNI_CAUSE_VPCI_P	0x0040
#define UNI_CAUSE_MTYPE_P	0x0080
#define UNI_CAUSE_TIMER_P	0x0100
#define UNI_CAUSE_TNS_P		0x0200
#define UNI_CAUSE_NUMBER_P	0x0400
#define UNI_CAUSE_ATTR_P	0x0800
#define UNI_CAUSE_PARAM_P	0x1000

	enum uni_cause_loc	loc;
	enum uni_cause		cause;

	union {
	    struct {
		enum uni_cause_pu	pu;
		enum uni_cause_na	na;
		enum uni_cause_cond	cond;
	    } cond;
	    struct {
		enum uni_cause_reason	reason;
		enum uni_cause_cond	cond;
		u_int			user;
		uint8_t			ie;
	    } rej;
	    struct {
		uint8_t			ie[UNI_CAUSE_IE_N];
		u_int			len;
	    } ie;
	    struct {
		uint8_t			traffic[UNI_CAUSE_TRAFFIC_N];
		u_int			len;
	    } traffic;
	    struct {
		uint16_t		vpci;
		uint16_t		vci;
	    } vpci;
	    uint8_t			mtype;
	    u_char			timer[3];
	    struct uni_ie_tns		tns;
	    struct uni_ie_called	number;		/* TNS does not fit */
	    uint8_t			param;
	    struct {
		u_int			nattr;
	        u_char			attr[UNI_CAUSE_ATTR_N][3];
	    }				attr;
	} u;
};
enum uni_diag uni_diag(enum uni_cause, enum uni_coding);

/* return a string for the cause (NULL if the coding/cause are illegal) */
const char *uni_ie_cause2str(enum uni_coding, u_int);

/*************************************************************************
 *
 * Connection identifier information element
 */
enum uni_connid_type {
	UNI_CONNID_VCI		= 0,
	UNI_CONNID_ANYVCI	= 1,
	UNI_CONNID_NOVCI	= 4,
};
enum uni_connid_assoc {
	UNI_CONNID_ASSOC	= 0,
	UNI_CONNID_NONASSOC	= 1,
};
struct uni_ie_connid {
	struct uni_iehdr	h;
	enum uni_connid_assoc	assoc;
	enum uni_connid_type	type;
	u_int			vpci : 16;
	u_int			vci : 16;
};

/*************************************************************************
 *
 * End point reference IE
 */
struct uni_ie_epref {
	struct uni_iehdr	h;
	u_int			flag : 1;
	u_int			epref : 15;
};

/*************************************************************************
 *
 * End point state IE
 */
enum uni_epstate {
	UNI_EPSTATE_NULL	= 0x00,
	UNI_EPSTATE_ADD_INIT	= 0x01,
	UNI_EPSTATE_ALERT_DLVD	= 0x04,
	UNI_EPSTATE_ADD_RCVD	= 0x06,
	UNI_EPSTATE_ALERT_RCVD	= 0x07,
	UNI_EPSTATE_ACTIVE	= 0x0a,
	UNI_EPSTATE_DROP_INIT	= 0x0b,
	UNI_EPSTATE_DROP_RCVD	= 0x0c,
};

struct uni_ie_epstate {
	struct uni_iehdr h;
	enum uni_epstate state;
};

/*************************************************************************
 *
 * Q.2932 Facility IE
 */
enum {
	UNI_FACILITY_ROSE	= 0x11,

	UNI_FACILITY_MAXAPDU	= 128,
};

struct uni_ie_facility {
	struct uni_iehdr h;

	u_char		proto;
	u_char		apdu[UNI_FACILITY_MAXAPDU];
	u_int		len;
};

/*************************************************************************
 *
 * Notification indicator
 */
enum {
	UNI_NOTIFY_MAXLEN	= 128,	/* maximum info length */
};
struct uni_ie_notify {
	struct uni_iehdr h;
	u_int		len;
	u_char		notify[UNI_NOTIFY_MAXLEN];
};

/*************************************************************************
 *
 * QoS information element
 */
enum uni_qos {
	UNI_QOS_CLASS0	= 0x00,
	UNI_QOS_CLASS1	= 0x01,
	UNI_QOS_CLASS2	= 0x02,
	UNI_QOS_CLASS3	= 0x03,
	UNI_QOS_CLASS4	= 0x04,
};

struct uni_ie_qos {
	struct uni_iehdr h;
	enum uni_qos	fwd;
	enum uni_qos	bwd;
};

/*************************************************************************
 *
 * Broadband repeat indicator information element
 */
enum uni_repeat_type {
	UNI_REPEAT_PRIDESC	= 0x02,
	UNI_REPEAT_STACK	= 0x0a,		/* PNNI */
};

struct uni_ie_repeat {
	struct uni_iehdr h;
	enum uni_repeat_type type;
};

/*************************************************************************
 *
 * Restart indicator information element
 */
enum uni_restart_type {
	UNI_RESTART_CHANNEL	= 0x0,
	UNI_RESTART_PATH	= 0x1,
	UNI_RESTART_ALL		= 0x2,
};

struct uni_ie_restart {
	struct uni_iehdr h;
	enum uni_restart_type rclass;
};

/*************************************************************************
 *
 * Broadband sending complete indicator information element
 */
struct uni_ie_scompl {
	struct uni_iehdr h;
};

/*************************************************************************
 *
 * ATM traffic descriptor information element
 */
enum {
	UNI_TRAFFIC_FMDCR_ID	= 0x00,
	UNI_TRAFFIC_BMDCR_ID	= 0x02,
	UNI_TRAFFIC_FPCR0_ID	= 0x82,
	UNI_TRAFFIC_BPCR0_ID	= 0x83,
	UNI_TRAFFIC_FPCR1_ID	= 0x84,
	UNI_TRAFFIC_BPCR1_ID	= 0x85,
	UNI_TRAFFIC_FSCR0_ID	= 0x88,
	UNI_TRAFFIC_BSCR0_ID	= 0x89,
	UNI_TRAFFIC_FSCR1_ID	= 0x90,
	UNI_TRAFFIC_BSCR1_ID	= 0x91,
	UNI_TRAFFIC_FABR1_ID	= 0x92,
	UNI_TRAFFIC_BABR1_ID	= 0x93,
	UNI_TRAFFIC_FMBS0_ID	= 0xa0,
	UNI_TRAFFIC_BMBS0_ID	= 0xa1,
	UNI_TRAFFIC_FMBS1_ID	= 0xb0,
	UNI_TRAFFIC_BMBS1_ID	= 0xb1,
	UNI_TRAFFIC_BEST_ID	= 0xbe,
	UNI_TRAFFIC_MOPT_ID	= 0xbf,

	UNI_TRAFFIC_FTAG	= 0x01,
	UNI_TRAFFIC_BTAG	= 0x02,
	UNI_TRAFFIC_FDISC	= 0x80,
	UNI_TRAFFIC_BDISC	= 0x40,

	UNI_MINTRAFFIC_FPCR0_ID	= 0x82,
	UNI_MINTRAFFIC_BPCR0_ID	= 0x83,
	UNI_MINTRAFFIC_FPCR1_ID	= 0x84,
	UNI_MINTRAFFIC_BPCR1_ID	= 0x85,
	UNI_MINTRAFFIC_FABR1_ID	= 0x92,
	UNI_MINTRAFFIC_BABR1_ID	= 0x93,

	UNI_MDCR_ORIGIN_USER	= 0x00,
	UNI_MDCR_ORIGIN_NET	= 0x01,
};

#define UNI_TRAFFIC_FPCR0_P	0x0001
#define UNI_TRAFFIC_BPCR0_P	0x0002
#define UNI_TRAFFIC_FPCR1_P	0x0004
#define UNI_TRAFFIC_BPCR1_P	0x0008
#define UNI_TRAFFIC_FSCR0_P	0x0010
#define UNI_TRAFFIC_BSCR0_P	0x0020
#define UNI_TRAFFIC_FSCR1_P	0x0040
#define UNI_TRAFFIC_BSCR1_P	0x0080
#define UNI_TRAFFIC_FMBS0_P	0x0100
#define UNI_TRAFFIC_BMBS0_P	0x0200
#define UNI_TRAFFIC_FMBS1_P	0x0400
#define UNI_TRAFFIC_BMBS1_P	0x0800
#define UNI_TRAFFIC_BEST_P	0x1000
#define UNI_TRAFFIC_MOPT_P	0x2000
#define UNI_TRAFFIC_FABR1_P	0x4000
#define UNI_TRAFFIC_BABR1_P	0x8000
struct uni_xtraffic {
	u_int	fpcr0, bpcr0;
	u_int	fpcr1, bpcr1;
	u_int	fscr0, bscr0;
	u_int	fscr1, bscr1;
	u_int	fmbs0, bmbs0;
	u_int	fmbs1, bmbs1;
	u_int	fabr1, babr1;
	u_int	ftag, btag;
	u_int	fdisc, bdisc;
};

struct uni_ie_traffic {
	struct uni_iehdr h;
	struct uni_xtraffic t;
};
struct uni_ie_atraffic {
	struct uni_iehdr h;
	struct uni_xtraffic t;
};

/*
 * Q.2961 minimum traffic descriptor
 */
struct uni_ie_mintraffic {
	struct uni_iehdr h;
#define UNI_MINTRAFFIC_FPCR0_P	0x0001
#define UNI_MINTRAFFIC_BPCR0_P	0x0002
#define UNI_MINTRAFFIC_FPCR1_P	0x0004
#define UNI_MINTRAFFIC_BPCR1_P	0x0008
#define UNI_MINTRAFFIC_FABR1_P	0x0010
#define UNI_MINTRAFFIC_BABR1_P	0x0020

	u_int	fpcr0, bpcr0;
	u_int	fpcr1, bpcr1;
	u_int	fabr1, babr1;
};

/*
 * UNI4.0+ (af-cs-0147.000) Minimum Desired Cell Rate
 */
struct uni_ie_mdcr {
	struct uni_iehdr h;
	u_int	origin;
	u_int	fmdcr, bmdcr;
};

/*************************************************************************
 *
 * User-user information information element
 */
struct uni_ie_uu {
	struct uni_iehdr h;
	u_int		len;
	u_char		uu[UNI_UU_MAXLEN];
};

/*************************************************************************
 *
 * Generic identifier transport
 */
enum uni_git_std {
	UNI_GIT_STD_DSMCC	= 0x01,	/* DSM-CC */
	UNI_GIT_STD_H245	= 0x02,	/* H.245 */
};
enum uni_git_type {
	UNI_GIT_TYPE_SESS	= 0x01,	/* session id */
	UNI_GIT_TYPE_RES	= 0x02,	/* resource id */
};

enum {
	UNI_GIT_MAXSESS		= 20,	/* max session value length */
	UNI_GIT_MAXRES		= 4,	/* max resource value length */

	UNI_GIT_MAXVAL		= 20,	/* the maximum of the above */
	UNI_GIT_MAXSUB		= 2,	/* maximum number of og. 6 */
};

struct uni_ie_git {
	struct uni_iehdr	h;

	enum uni_git_std	std;	/* identifier related standard/application */
	u_int			numsub;
	struct {
		enum uni_git_type type;	
		u_int		len;
		u_char		val[UNI_GIT_MAXVAL];
	}			sub[UNI_GIT_MAXSUB];
};

/*************************************************************************
 *
 * End-to-end transit delay
 */
enum {
	UNI_EETD_CTD_ID		= 0x01,	/* cumulative transit delay */
	UNI_EETD_MTD_ID		= 0x03,	/* maximum transit delay */
	UNI_EETD_NET_ID		= 0x0a,	/* network generated */
	UNI_EETD_PMTD_ID	= 0x0b,	/* PNNI acceptable forward maximum ctd */
	UNI_EETD_PCTD_ID	= 0x11,	/* PNNI cumulative forward maximum ctd */

	UNI_EETD_ANYMAX		= 0xffff,
	UNI_EETD_MAXVAL		= 0xffff,	/* maximum value */
};

struct uni_ie_eetd {
	struct uni_iehdr	h;
#define UNI_EETD_CUM_P		0x0001
#define UNI_EETD_MAX_P		0x0002
#define UNI_EETD_NET_P		0x0004	/* UNI4.0 9.1.2.1 */
#define UNI_EETD_PMTD_P		0x0008	/* PNNI1.0 6.4.5.24 */
#define UNI_EETD_PCTD_P		0x0010	/* PNNI1.0 6.4.5.24 */

	u_int	cumulative;
	u_int	maximum;
	u_int	pmtd;
	u_int	pctd;
};

/*************************************************************************
 * 
 * Leaf-initiated-join call identifier
 */
enum uni_lij_idtype {
	UNI_LIJ_IDTYPE_ROOT	= 0x0,	/* root created */
};

struct uni_ie_lij_callid {
	struct uni_iehdr	h;

	enum uni_lij_idtype	type;
	u_int			callid;
};

/*
 * LIJ parameters
 */
enum uni_lij_screen {
	UNI_LIJ_SCREEN_NETJOIN	= 0x0,	/* without root notification */
};

struct uni_ie_lij_param {
	struct uni_iehdr	h;

	enum uni_lij_screen	screen;
};

/*
 * LIJ sequence number
 */
struct uni_ie_lij_seqno {
	struct uni_iehdr	h;

	u_int			seqno;
};

/*************************************************************************
 *
 * Locking/Non-locking shift not supported
 */
struct uni_ie_lshift {
	struct uni_iehdr h;
	u_int		set:3;
};

struct uni_ie_nlshift {
	struct uni_iehdr h;
	u_int		set:3;
};

/*************************************************************************
 *
 * Externded QoS information element
 */
enum {
	UNI_EXQOS_FACC_ID	= 0x94,
	UNI_EXQOS_BACC_ID	= 0x95,
	UNI_EXQOS_FCUM_ID	= 0x96,
	UNI_EXQOS_BCUM_ID	= 0x97,
	UNI_EXQOS_FCLR_ID	= 0xa2,
	UNI_EXQOS_BCLR_ID	= 0xa3,
};

enum uni_exqos_origin {
	UNI_EXQOS_USER	= 0,
	UNI_EXQOS_NET	= 1,
};

enum {
	UNI_EXQOS_ANY_CDV	= 0xffffff,
	UNI_EXQOS_ANY_CLR	= 0xff,
};

struct uni_ie_exqos {
	struct uni_iehdr	h;
#define UNI_EXQOS_FACC_P	0x0001
#define UNI_EXQOS_BACC_P	0x0002
#define UNI_EXQOS_FCUM_P	0x0004
#define UNI_EXQOS_BCUM_P	0x0008
#define UNI_EXQOS_FCLR_P	0x0010
#define UNI_EXQOS_BCLR_P	0x0020

	enum uni_exqos_origin	origin;
	u_int			facc;
	u_int			bacc;
	u_int			fcum;
	u_int			bcum;
	u_int			fclr;
	u_int			bclr;
};

/*************************************************************************
 *
 * Additional ABR parameters
 * ABR setup parameters
 */
enum {
	UNI_ABRADD_FADD_ID	= 0xc2,
	UNI_ABRADD_BADD_ID	= 0xc3,
	UNI_ABRSETUP_FICR_ID	= 0xc2,
	UNI_ABRSETUP_BICR_ID	= 0xc3,
	UNI_ABRSETUP_FTBE_ID	= 0xc4,
	UNI_ABRSETUP_BTBE_ID	= 0xc5,
	UNI_ABRSETUP_RMFRT_ID	= 0xc6,
	UNI_ABRSETUP_FRIF_ID	= 0xc8,
	UNI_ABRSETUP_BRIF_ID	= 0xc9,
	UNI_ABRSETUP_FRDF_ID	= 0xca,
	UNI_ABRSETUP_BRDF_ID	= 0xcb,
};

struct uni_abr_rec {
	u_int			present;
#define UNI_ABR_REC_NRM_P	0x80000000
#define UNI_ABR_REC_TRM_P	0x40000000
#define UNI_ABR_REC_CDF_P	0x20000000
#define UNI_ABR_REC_ADTF_P	0x10000000
	u_int		nrm:3;
	u_int		trm:3;
	u_int		cdf:3;
	u_int		adtf:10;
};

struct uni_ie_abradd {
	struct uni_iehdr	h;
	struct uni_abr_rec	fwd, bwd;
};

struct uni_ie_abrsetup {
	struct uni_iehdr	h;
#define UNI_ABRSETUP_FICR_P	0x0001
#define UNI_ABRSETUP_BICR_P	0x0002
#define UNI_ABRSETUP_FTBE_P	0x0004
#define UNI_ABRSETUP_BTBE_P	0x0008
#define UNI_ABRSETUP_FRIF_P	0x0010
#define UNI_ABRSETUP_BRIF_P	0x0020
#define UNI_ABRSETUP_FRDF_P	0x0040
#define UNI_ABRSETUP_BRDF_P	0x0080
#define UNI_ABRSETUP_RMFRT_P	0x0100

	u_int		ficr, bicr;
	u_int		ftbe, btbe;
	u_int		rmfrt;
	u_int		frif, brif;
	u_int		frdf, brdf;
};

/*************************************************************************
 *
 * Connection scope information element
 */
enum uni_cscope {
	UNI_CSCOPE_ORG	= 0x01,
};

enum {
	UNI_CSCOPE_ORG_LOC	= 0x01,
	UNI_CSCOPE_ORG_LOC_P1	= 0x02,
	UNI_CSCOPE_ORG_LOC_P2	= 0x03,
	UNI_CSCOPE_ORG_SITE_M1	= 0x04,
	UNI_CSCOPE_ORG_SITE	= 0x05,
	UNI_CSCOPE_ORG_SITE_P1	= 0x06,
	UNI_CSCOPE_ORG_ORG_M1	= 0x07,
	UNI_CSCOPE_ORG_ORG	= 0x08,
	UNI_CSCOPE_ORG_ORG_P1	= 0x09,
	UNI_CSCOPE_ORG_COMM_M1	= 0x0a,
	UNI_CSCOPE_ORG_COMM	= 0x0b,
	UNI_CSCOPE_ORG_COMM_P1	= 0x0c,
	UNI_CSCOPE_ORG_REG	= 0x0d,
	UNI_CSCOPE_ORG_INTER	= 0x0e,
	UNI_CSCOPE_ORG_GLOBAL	= 0x0f,
};

struct uni_ie_cscope {
	struct uni_iehdr	h;
	enum uni_cscope		type;
	u_int			scope:8;
};

/*************************************************************************
 *
 * Connection scope information element
 */
enum uni_report {
	UNI_REPORT_MODCONF	= 0x01,
	UNI_REPORT_CLOCK	= 0x02,
	UNI_REPORT_EEAVAIL	= 0x04,
	UNI_REPORT_EEREQ	= 0x05,
	UNI_REPORT_EECOMPL	= 0x06,
};

struct uni_ie_report {
	struct uni_iehdr h;
	enum uni_report	report;
};

/*************************************************************************
 *
 * PNNI Designated transit list information element
 */
enum {
	UNI_DTL_LOGNP	= 0x01,
	UNI_DTL_LOGNP_SIZE = 27,
};

struct uni_ie_dtl {
	struct uni_iehdr	h;
	u_int			ptr:16;
	u_int			num;
	struct {
	  u_char		node_level;
	  u_char		node_id[21];
	  u_int			port_id;
	}			dtl[UNI_DTL_MAXNUM];
};

/*************************************************************************
 *
 * PNNI Crankback information element
 */
enum uni_crankback {
	UNI_CRANKBACK_IF	= 0x02,
	UNI_CRANKBACK_NODE	= 0x03,
	UNI_CRANKBACK_LINK	= 0x04,
};

enum {
	UNI_CAUSE_NXNODE_UNREACH = 128,
	UNI_CAUSE_DTL_NOT_MY_ID	= 160,
};

struct uni_ie_crankback {
	struct uni_iehdr	h;
#define UNI_CRANKBACK_TOP_P	0x0001
#define UNI_CRANKBACK_TOPX_P	0x0002
#define UNI_CRANKBACK_QOS_P	0x0004
	u_int			level:8;
	enum uni_crankback	type;
	union {
	  struct {
	    u_char		level;
	    u_char		id[21];
	  }			node;
	  struct {
	    u_char		plevel;
	    u_char		pid[21];
	    u_int		port;
	    u_char		slevel;
	    u_char		sid[21];
	  }			link;
	}			id;
	u_int			cause:8;
	union {
	  struct {
	    u_int		dir:8;
	    u_int		port;
	    u_int		avcr;
	    u_int		crm;
	    u_int		vf;
	  }			top;
	  struct {
	    u_int		ctd:1;
	    u_int		cdv:1;
	    u_int		clr:1;
	    u_int		other:1;
	  }			qos;
	}			diag;
};

/*************************************************************************
 *
 * PNNI Call_ing/called party soft PVPC/PVCC information element
 */
enum uni_soft_sel {
	UNI_SOFT_SEL_ANY	= 0x00,
	UNI_SOFT_SEL_REQ	= 0x02,
	UNI_SOFT_SEL_ASS	= 0x04,
};

struct uni_ie_calling_soft {
	struct uni_iehdr h;
#define UNI_CALLING_SOFT_VCI_P	0x0001
	u_int		vpi:12;
	u_int		vci:16;
};
struct uni_ie_called_soft {
	struct uni_iehdr h;
#define UNI_CALLED_SOFT_VPI_P	0x0001
#define UNI_CALLED_SOFT_VCI_P	0x0002
	enum uni_soft_sel sel;
	u_int		vpi:12;
	u_int		vci:16;
};

/*************************************************************************/

#include <netnatm/msg/uni_ie.h>
#include <netnatm/msg/uni_msg.h>

struct uni_all {
	enum uni_msgtype	mtype;
	union uni_msgall	u;
};

struct uni_ie {
	enum uni_ietype		ietype;
	union uni_ieall		u;
};

#endif
