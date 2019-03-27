/*-
 * Copyright (C) 1999-2002
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $KAME: altq_cdnr.h,v 1.9 2003/07/10 12:07:48 kjc Exp $
 * $FreeBSD$
 */

#ifndef _ALTQ_ALTQ_CDNR_H_
#define	_ALTQ_ALTQ_CDNR_H_

#include <net/altq/altq.h>

/*
 * traffic conditioner element types
 */
#define	TCETYPE_NONE		0
#define	TCETYPE_TOP		1	/* top level conditioner */
#define	TCETYPE_ELEMENT		2	/* a simple tc element */
#define	TCETYPE_TBMETER		3	/* token bucket meter */
#define	TCETYPE_TRTCM		4	/* (two-rate) three color marker */
#define	TCETYPE_TSWTCM		5	/* time sliding window 3-color maker */

/*
 * traffic conditioner action
 */
struct cdnr_block;

struct tc_action {
	int	tca_code;	/* e.g., TCACODE_PASS */
	/* tca_code dependent variable */
	union {
		u_long		un_value;	/* template */
		u_int8_t	un_dscp;	/* diffserv code point */
		u_long		un_handle;	/* tc action handle */
		struct cdnr_block *un_next;	/* next tc element block */
	} tca_un;
};
#define	tca_value	tca_un.un_value
#define	tca_dscp	tca_un.un_dscp
#define	tca_handle	tca_un.un_handle
#define	tca_next	tca_un.un_next

#define	TCACODE_NONE	0	/* action is not set */
#define	TCACODE_PASS	1 	/* pass this packet */
#define	TCACODE_DROP	2	/* discard this packet */
#define	TCACODE_RETURN	3	/* do not process this packet */
#define	TCACODE_MARK	4	/* mark dscp */
#define	TCACODE_HANDLE	5	/* take action specified by handle */
#define	TCACODE_NEXT	6	/* take action in the next tc element */
#define	TCACODE_MAX	6

#define	CDNR_NULL_HANDLE	0

struct cdnr_interface {
	char	cdnr_ifname[IFNAMSIZ];  /* interface name (e.g., fxp0) */
};

/* simple element operations */
struct cdnr_add_element {
	struct cdnr_interface	iface;
	struct tc_action	action;

	u_long			cdnr_handle;	/* return value */
};

struct cdnr_delete_element {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
};

/* token-bucket meter operations */
struct cdnr_add_tbmeter {
	struct cdnr_interface	iface;
	struct tb_profile	profile;
	struct tc_action	in_action;
	struct tc_action	out_action;

	u_long			cdnr_handle;	/* return value */
};

struct cdnr_modify_tbmeter {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
	struct tb_profile	profile;
};

struct cdnr_tbmeter_stats {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
	struct pktcntr		in_cnt;
	struct pktcntr		out_cnt;
};

/* two-rate three-color marker operations */
struct cdnr_add_trtcm {
	struct cdnr_interface	iface;
	struct tb_profile	cmtd_profile;	/* profile for committed tb */
	struct tb_profile	peak_profile;	/* profile for peak tb */
	struct tc_action	green_action;	/* action for green packets */
	struct tc_action	yellow_action;	/* action for yellow packets */
	struct tc_action	red_action;	/* action for red packets */
	int			coloraware;	/* color-aware/color-blind */

	u_long			cdnr_handle;	/* return value */
};

struct cdnr_modify_trtcm {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
	struct tb_profile	cmtd_profile;	/* profile for committed tb */
	struct tb_profile	peak_profile;	/* profile for peak tb */
	int			coloraware;	/* color-aware/color-blind */
};

struct cdnr_tcm_stats {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
	struct pktcntr		green_cnt;
	struct pktcntr		yellow_cnt;
	struct pktcntr		red_cnt;
};

/* time sliding window three-color marker operations */
struct cdnr_add_tswtcm {
	struct cdnr_interface	iface;
	u_int32_t		cmtd_rate;	/* committed rate (bits/sec) */
	u_int32_t		peak_rate;	/* peak rate (bits/sec) */
	u_int32_t		avg_interval;	/* averaging interval (msec) */
	struct tc_action	green_action;	/* action for green packets */
	struct tc_action	yellow_action;	/* action for yellow packets */
	struct tc_action	red_action;	/* action for red packets */

	u_long			cdnr_handle;	/* return value */
};

struct cdnr_modify_tswtcm {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
	u_int32_t		cmtd_rate;	/* committed rate (bits/sec) */
	u_int32_t		peak_rate;	/* peak rate (bits/sec) */
	u_int32_t		avg_interval;	/* averaging interval (msec) */
};

struct cdnr_add_filter {
	struct cdnr_interface	iface;
	u_long			cdnr_handle;
#ifdef ALTQ3_CLFIER_COMPAT
	struct flow_filter	filter;
#endif
	u_long			filter_handle;	/* return value */
};

struct cdnr_delete_filter {
	struct cdnr_interface	iface;
	u_long			filter_handle;
};

struct tce_stats {
	u_long			tce_handle;	/* tc element handle */
	int			tce_type;	/* e.g., TCETYPE_ELEMENT */
	struct pktcntr		tce_cnts[3];	/* tcm returns 3 counters */
};

struct cdnr_get_stats {
	struct cdnr_interface	iface;
	struct pktcntr		cnts[TCACODE_MAX+1];

	/* element stats */
	int			nskip;		/* skip # of elements */
	int			nelements;	/* # of element stats (WR) */
	struct tce_stats	*tce_stats;	/* pointer to stats array */
};

#define	CDNR_IF_ATTACH		_IOW('Q', 1, struct cdnr_interface)
#define	CDNR_IF_DETACH		_IOW('Q', 2, struct cdnr_interface)
#define	CDNR_ENABLE		_IOW('Q', 3, struct cdnr_interface)
#define	CDNR_DISABLE		_IOW('Q', 4, struct cdnr_interface)
#define	CDNR_ADD_FILTER		_IOWR('Q', 10, struct cdnr_add_filter)
#define	CDNR_DEL_FILTER		_IOW('Q', 11, struct cdnr_delete_filter)
#define	CDNR_GETSTATS		_IOWR('Q', 12, struct cdnr_get_stats)
#define	CDNR_ADD_ELEM		_IOWR('Q', 30, struct cdnr_add_element)
#define	CDNR_DEL_ELEM		_IOW('Q', 31, struct cdnr_delete_element)
#define	CDNR_ADD_TBM		_IOWR('Q', 32, struct cdnr_add_tbmeter)
#define	CDNR_MOD_TBM		_IOW('Q', 33, struct cdnr_modify_tbmeter)
#define	CDNR_TBM_STATS		_IOWR('Q', 34, struct cdnr_tbmeter_stats)
#define	CDNR_ADD_TCM		_IOWR('Q', 35, struct cdnr_add_trtcm)
#define	CDNR_MOD_TCM		_IOWR('Q', 36, struct cdnr_modify_trtcm)
#define	CDNR_TCM_STATS		_IOWR('Q', 37, struct cdnr_tcm_stats)
#define	CDNR_ADD_TSW		_IOWR('Q', 38, struct cdnr_add_tswtcm)
#define	CDNR_MOD_TSW		_IOWR('Q', 39, struct cdnr_modify_tswtcm)

#ifndef DSCP_EF
/* diffserve code points */
#define	DSCP_MASK	0xfc
#define	DSCP_CUMASK	0x03
#define	DSCP_EF		0xb8
#define	DSCP_AF11	0x28
#define	DSCP_AF12	0x30
#define	DSCP_AF13	0x38
#define	DSCP_AF21	0x48
#define	DSCP_AF22	0x50
#define	DSCP_AF23	0x58
#define	DSCP_AF31	0x68
#define	DSCP_AF32	0x70
#define	DSCP_AF33	0x78
#define	DSCP_AF41	0x88
#define	DSCP_AF42	0x90
#define	DSCP_AF43	0x98
#define	AF_CLASSMASK		0xe0
#define	AF_DROPPRECMASK		0x18
#endif

#ifdef _KERNEL

/*
 * packet information passed to the input function of tc elements
 */
struct cdnr_pktinfo {
	int		pkt_len;	/* packet length */
	u_int8_t	pkt_dscp;	/* diffserv code point */
};

/*
 * traffic conditioner control block common to all types of tc elements
 */
struct cdnr_block {
	LIST_ENTRY(cdnr_block)	cb_next;
	int		cb_len;		/* size of this tc element */
	int		cb_type;	/* cdnr block type */
	int		cb_ref;		/* reference count of this element */
	u_long		cb_handle;	/* handle of this tc element */
	struct top_cdnr *cb_top;	/* back pointer to top */
	struct tc_action cb_action;	/* top level action for this tcb */
	struct tc_action *(*cb_input)(struct cdnr_block *,
				      struct cdnr_pktinfo *);
};

/*
 * top level traffic conditioner structure for an interface
 */
struct top_cdnr {
	struct cdnr_block	tc_block;

	LIST_ENTRY(top_cdnr)	tc_next;
	struct ifaltq		*tc_ifq;

	LIST_HEAD(, cdnr_block) tc_elements;
#ifdef ALTQ3_CLFIER_COMPAT
	struct acc_classifier	tc_classifier;
#endif
	struct pktcntr		tc_cnts[TCACODE_MAX+1];
};

/* token bucket element */
struct tbe {
	u_int64_t	rate;
	u_int64_t	depth;

	u_int64_t	token;
	u_int64_t	filluptime;
	u_int64_t	last;
};

/* token bucket meter structure */
struct tbmeter {
	struct cdnr_block	cdnrblk;	/* conditioner block */
	struct tbe		tb;		/* token bucket */
	struct tc_action	in_action;	/* actions for IN/OUT */
	struct tc_action	out_action;	/* actions for IN/OUT */
	struct pktcntr		in_cnt;		/* statistics for IN/OUT */
	struct pktcntr		out_cnt;	/* statistics for IN/OUT */
};

/* two-rate three-color marker structure */
struct trtcm {
	struct cdnr_block	cdnrblk;	/* conditioner block */
	struct tbe		cmtd_tb;	/* committed tb profile */
	struct tbe		peak_tb;	/* peak tb profile */
	struct tc_action	green_action;
	struct tc_action	yellow_action;
	struct tc_action	red_action;
	int			coloraware;
	u_int8_t		green_dscp;
	u_int8_t		yellow_dscp;
	u_int8_t		red_dscp;
	struct pktcntr		green_cnt;
	struct pktcntr		yellow_cnt;
	struct pktcntr		red_cnt;
};

/* time sliding window three-color marker structure */
struct tswtcm {
	struct cdnr_block	cdnrblk;	/* conditioner block */

	u_int32_t		avg_rate;	/* average rate (bytes/sec) */
	u_int64_t		t_front;	/* timestamp of last update */

	u_int64_t		timewin;	/* average interval */
	u_int32_t		cmtd_rate;	/* committed target rate */
	u_int32_t		peak_rate;	/* peak target rate */
	struct tc_action	green_action;
	struct tc_action	yellow_action;
	struct tc_action	red_action;
	u_int8_t		green_dscp;
	u_int8_t		yellow_dscp;
	u_int8_t		red_dscp;
	struct pktcntr		green_cnt;
	struct pktcntr		yellow_cnt;
	struct pktcntr		red_cnt;
};

#endif /* _KERNEL */

#endif /* _ALTQ_ALTQ_CDNR_H_ */
