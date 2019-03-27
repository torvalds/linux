/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Michael Shalayeff
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2008 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 *	$OpenBSD: if_pfsync.h,v 1.35 2008/06/29 08:42:15 mcbride Exp $
 *	$FreeBSD$
 */


#ifndef _NET_IF_PFSYNC_H_
#define	_NET_IF_PFSYNC_H_

#define	PFSYNC_VERSION		5
#define	PFSYNC_DFLTTL		255

#define	PFSYNC_ACT_CLR		0	/* clear all states */
#define	PFSYNC_ACT_INS		1	/* insert state */
#define	PFSYNC_ACT_INS_ACK	2	/* ack of insterted state */
#define	PFSYNC_ACT_UPD		3	/* update state */
#define	PFSYNC_ACT_UPD_C	4	/* "compressed" update state */
#define	PFSYNC_ACT_UPD_REQ	5	/* request "uncompressed" state */
#define	PFSYNC_ACT_DEL		6	/* delete state */
#define	PFSYNC_ACT_DEL_C	7	/* "compressed" delete state */
#define	PFSYNC_ACT_INS_F	8	/* insert fragment */
#define	PFSYNC_ACT_DEL_F	9	/* delete fragments */
#define	PFSYNC_ACT_BUS		10	/* bulk update status */
#define	PFSYNC_ACT_TDB		11	/* TDB replay counter update */
#define	PFSYNC_ACT_EOF		12	/* end of frame */
#define	PFSYNC_ACT_MAX		13

/*
 * A pfsync frame is built from a header followed by several sections which
 * are all prefixed with their own subheaders. Frames must be terminated with
 * an EOF subheader.
 *
 * | ...			|
 * | IP header			|
 * +============================+
 * | pfsync_header		|
 * +----------------------------+
 * | pfsync_subheader		|
 * +----------------------------+
 * | first action fields	|
 * | ...			|
 * +----------------------------+
 * | pfsync_subheader		|
 * +----------------------------+
 * | second action fields	|
 * | ...			|
 * +----------------------------+
 * | EOF pfsync_subheader	|
 * +----------------------------+
 * | HMAC			|
 * +============================+
 */

/*
 * Frame header
 */

struct pfsync_header {
	u_int8_t			version;
	u_int8_t			_pad;
	u_int16_t			len;
	u_int8_t			pfcksum[PF_MD5_DIGEST_LENGTH];
} __packed;

/*
 * Frame region subheader
 */

struct pfsync_subheader {
	u_int8_t			action;
	u_int8_t			_pad;
	u_int16_t			count;
} __packed;

/*
 * CLR
 */

struct pfsync_clr {
	char				ifname[IFNAMSIZ];
	u_int32_t			creatorid;
} __packed;

/*
 * INS, UPD, DEL
 */

/* these use struct pfsync_state in pfvar.h */

/*
 * INS_ACK
 */

struct pfsync_ins_ack {
	u_int64_t			id;
	u_int32_t			creatorid;
} __packed;

/*
 * UPD_C
 */

struct pfsync_upd_c {
	u_int64_t			id;
	struct pfsync_state_peer	src;
	struct pfsync_state_peer	dst;
	u_int32_t			creatorid;
	u_int32_t			expire;
	u_int8_t			timeout;
	u_int8_t			_pad[3];
} __packed;

/*
 * UPD_REQ
 */

struct pfsync_upd_req {
	u_int64_t			id;
	u_int32_t			creatorid;
} __packed;

/*
 * DEL_C
 */

struct pfsync_del_c {
	u_int64_t			id;
	u_int32_t			creatorid;
} __packed;

/*
 * INS_F, DEL_F
 */

/* not implemented (yet) */

/*
 * BUS
 */

struct pfsync_bus {
	u_int32_t			creatorid;
	u_int32_t			endtime;
	u_int8_t			status;
#define	PFSYNC_BUS_START			1
#define	PFSYNC_BUS_END				2
	u_int8_t			_pad[3];
} __packed;

/*
 * TDB
 */

struct pfsync_tdb {
	u_int32_t			spi;
	union sockaddr_union		dst;
	u_int32_t			rpl;
	u_int64_t			cur_bytes;
	u_int8_t			sproto;
	u_int8_t			updates;
	u_int8_t			_pad[2];
} __packed;

#define	PFSYNC_HDRLEN		sizeof(struct pfsync_header)

struct pfsyncstats {
	u_int64_t	pfsyncs_ipackets;	/* total input packets, IPv4 */
	u_int64_t	pfsyncs_ipackets6;	/* total input packets, IPv6 */
	u_int64_t	pfsyncs_badif;		/* not the right interface */
	u_int64_t	pfsyncs_badttl;		/* TTL is not PFSYNC_DFLTTL */
	u_int64_t	pfsyncs_hdrops;		/* packets shorter than hdr */
	u_int64_t	pfsyncs_badver;		/* bad (incl unsupp) version */
	u_int64_t	pfsyncs_badact;		/* bad action */
	u_int64_t	pfsyncs_badlen;		/* data length does not match */
	u_int64_t	pfsyncs_badauth;	/* bad authentication */
	u_int64_t	pfsyncs_stale;		/* stale state */
	u_int64_t	pfsyncs_badval;		/* bad values */
	u_int64_t	pfsyncs_badstate;	/* insert/lookup failed */

	u_int64_t	pfsyncs_opackets;	/* total output packets, IPv4 */
	u_int64_t	pfsyncs_opackets6;	/* total output packets, IPv6 */
	u_int64_t	pfsyncs_onomem;		/* no memory for an mbuf */
	u_int64_t	pfsyncs_oerrors;	/* ip output error */

	u_int64_t	pfsyncs_iacts[PFSYNC_ACT_MAX];
	u_int64_t	pfsyncs_oacts[PFSYNC_ACT_MAX];
};

/*
 * Configuration structure for SIOCSETPFSYNC SIOCGETPFSYNC
 */
struct pfsyncreq {
	char		 pfsyncr_syncdev[IFNAMSIZ];
	struct in_addr	 pfsyncr_syncpeer;
	int		 pfsyncr_maxupdates;
	int		 pfsyncr_defer;
};

#define	SIOCSETPFSYNC   _IOW('i', 247, struct ifreq)
#define	SIOCGETPFSYNC   _IOWR('i', 248, struct ifreq)

#ifdef _KERNEL

/*
 * this shows where a pf state is with respect to the syncing.
 */
#define	PFSYNC_S_INS	0x00
#define	PFSYNC_S_IACK	0x01
#define	PFSYNC_S_UPD	0x02
#define	PFSYNC_S_UPD_C	0x03
#define	PFSYNC_S_DEL	0x04
#define	PFSYNC_S_COUNT	0x05

#define	PFSYNC_S_DEFER	0xfe
#define	PFSYNC_S_NONE	0xff

#define	PFSYNC_SI_IOCTL		0x01
#define	PFSYNC_SI_CKSUM		0x02
#define	PFSYNC_SI_ACK		0x04

#endif /* _KERNEL */

#endif /* _NET_IF_PFSYNC_H_ */
