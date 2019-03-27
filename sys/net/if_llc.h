/*	$NetBSD: if_llc.h,v 1.12 1999/11/19 20:41:19 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_llc.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NET_IF_LLC_H_
#define _NET_IF_LLC_H_

/*
 * IEEE 802.2 Link Level Control headers, for use in conjunction with
 * 802.{3,4,5} media access control methods.
 *
 * Headers here do not use bit fields due to shortcommings in many
 * compilers.
 */

struct llc {
	u_int8_t llc_dsap;
	u_int8_t llc_ssap;
	union {
	    struct {
		u_int8_t control;
		u_int8_t format_id;
		u_int8_t class;
		u_int8_t window_x2;
	    } __packed type_u;
	    struct {
		u_int8_t num_snd_x2;
		u_int8_t num_rcv_x2;
	    } __packed type_i;
	    struct {
		u_int8_t control;
		u_int8_t num_rcv_x2;
	    } __packed type_s;
	    struct {
	        u_int8_t control;
		/*
		 * We cannot put the following fields in a structure because
		 * the structure rounding might cause padding.
		 */
		u_int8_t frmr_rej_pdu0;
		u_int8_t frmr_rej_pdu1;
		u_int8_t frmr_control;
		u_int8_t frmr_control_ext;
		u_int8_t frmr_cause;
	    } __packed type_frmr;
	    struct {
		u_int8_t  control;
		u_int8_t  org_code[3];
		u_int16_t ether_type;
	    } __packed type_snap;
	    struct {
		u_int8_t control;
		u_int8_t control_ext;
	    } __packed type_raw;
	} __packed llc_un;
} __packed;

struct frmrinfo {
	u_int8_t frmr_rej_pdu0;
	u_int8_t frmr_rej_pdu1;
	u_int8_t frmr_control;
	u_int8_t frmr_control_ext;
	u_int8_t frmr_cause;
} __packed;

#define	llc_control		llc_un.type_u.control
#define	llc_control_ext		llc_un.type_raw.control_ext
#define	llc_fid			llc_un.type_u.format_id
#define	llc_class		llc_un.type_u.class
#define	llc_window		llc_un.type_u.window_x2
#define	llc_frmrinfo 		llc_un.type_frmr.frmr_rej_pdu0
#define	llc_frmr_pdu0		llc_un.type_frmr.frmr_rej_pdu0
#define	llc_frmr_pdu1		llc_un.type_frmr.frmr_rej_pdu1
#define	llc_frmr_control	llc_un.type_frmr.frmr_control
#define	llc_frmr_control_ext	llc_un.type_frmr.frmr_control_ext
#define	llc_frmr_cause		llc_un.type_frmr.frmr_cause
#define	llc_snap		llc_un.type_snap

/*
 * Don't use sizeof(struct llc_un) for LLC header sizes
 */
#define LLC_ISFRAMELEN 4
#define LLC_UFRAMELEN  3
#define LLC_FRMRLEN    7
#define LLC_SNAPFRAMELEN 8

#ifdef CTASSERT
CTASSERT(sizeof (struct llc) == LLC_SNAPFRAMELEN);
#endif

/*
 * Unnumbered LLC format commands
 */
#define LLC_UI		0x3
#define LLC_UI_P	0x13
#define LLC_DISC	0x43
#define	LLC_DISC_P	0x53
#define LLC_UA		0x63
#define LLC_UA_P	0x73
#define LLC_TEST	0xe3
#define LLC_TEST_P	0xf3
#define LLC_FRMR	0x87
#define	LLC_FRMR_P	0x97
#define LLC_DM		0x0f
#define	LLC_DM_P	0x1f
#define LLC_XID		0xaf
#define LLC_XID_P	0xbf
#define LLC_SABME	0x6f
#define LLC_SABME_P	0x7f

/*
 * Supervisory LLC commands
 */
#define	LLC_RR		0x01
#define	LLC_RNR		0x05
#define	LLC_REJ		0x09

/*
 * Info format - dummy only
 */
#define	LLC_INFO	0x00

/*
 * ISO PDTR 10178 contains among others
 */
#define LLC_8021D_LSAP	0x42
#define LLC_X25_LSAP	0x7e
#define LLC_SNAP_LSAP	0xaa
#define LLC_ISO_LSAP	0xfe

#endif /* _NET_IF_LLC_H_ */
