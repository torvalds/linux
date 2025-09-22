/*	$OpenBSD: if_pfsync.h,v 1.65 2025/07/07 00:55:15 jsg Exp $	*/

/*
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

/*
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

#ifndef _NET_IF_PFSYNC_H_
#define _NET_IF_PFSYNC_H_

#define PFSYNC_VERSION		6
#define PFSYNC_DFLTTL		255

#define PFSYNC_ACT_CLR		0	/* clear all states */
#define PFSYNC_ACT_OINS		1	/* old insert state */
#define PFSYNC_ACT_INS_ACK	2	/* ack of inserted state */
#define PFSYNC_ACT_OUPD		3	/* old update state */
#define PFSYNC_ACT_UPD_C	4	/* "compressed" update state */
#define PFSYNC_ACT_UPD_REQ	5	/* request "uncompressed" state */
#define PFSYNC_ACT_DEL		6	/* delete state */
#define PFSYNC_ACT_DEL_C	7	/* "compressed" delete state */
#define PFSYNC_ACT_INS_F	8	/* insert fragment */
#define PFSYNC_ACT_DEL_F	9	/* delete fragments */
#define PFSYNC_ACT_BUS		10	/* bulk update status */
#define PFSYNC_ACT_OTDB		11	/* old TDB replay counter update */
#define PFSYNC_ACT_EOF		12	/* end of frame - DEPRECATED */
#define PFSYNC_ACT_INS		13	/* insert state */
#define PFSYNC_ACT_UPD		14	/* update state */
#define PFSYNC_ACT_TDB		15	/* TDB replay counter update */
#define PFSYNC_ACT_MAX		16

#define PFSYNC_ACTIONS		"CLR ST",		\
				"INS ST OLD",		\
				"INS ST ACK",		\
				"UPD ST OLD",		\
				"UPD ST COMP",		\
				"UPD ST REQ",		\
				"DEL ST",		\
				"DEL ST COMP",		\
				"INS FR",		\
				"DEL FR",		\
				"BULK UPD STAT",	\
				"UPD TDB OLD",		\
				"EOF",			\
				"INS ST",		\
				"UPD ST",		\
				"UPD TDB"

/*
 * A pfsync frame is built from a header followed by several sections which
 * are all prefixed with their own subheaders.
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
 * +============================+
 */

/*
 * Frame header
 */

struct pfsync_header {
	u_int8_t			version;
	u_int8_t			_pad;
	u_int16_t			len; /* in bytes */
	u_int8_t			pfcksum[PF_MD5_DIGEST_LENGTH];
} __packed;

/*
 * Frame region subheader
 */

struct pfsync_subheader {
	u_int8_t			action;
	u_int8_t			len; /* in dwords */
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
 * OINS, OUPD
 */

/* these messages are deprecated */

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
	u_int8_t			state_flags;
	u_int8_t			_pad[2];
} __packed;

/*
 * UPD_REQ
 */

struct pfsync_upd_req {
	u_int64_t			id;
	u_int32_t			creatorid;
} __packed __aligned(4);

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
#define PFSYNC_BUS_START			1
#define PFSYNC_BUS_END				2
	u_int8_t			_pad[3];
} __packed;

/*
 * TDB
 */
struct pfsync_tdb {
	u_int32_t			spi;
	union sockaddr_union		dst;
	u_int64_t			rpl;
	u_int64_t			cur_bytes;
	u_int8_t			sproto;
	u_int8_t			updates;
	u_int16_t			rdomain;
} __packed;

/*
 * EOF
 */

/* this message is deprecated */


#define PFSYNC_HDRLEN		sizeof(struct pfsync_header)


/*
 * Names for PFSYNC sysctl objects
 */
#define	PFSYNCCTL_STATS		1	/* PFSYNC stats */
#define	PFSYNCCTL_MAXID		2

#define	PFSYNCCTL_NAMES { \
	{ 0, 0 }, \
	{ "stats", CTLTYPE_STRUCT }, \
}

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

#ifdef _KERNEL

enum pfsync_counters {
	pfsyncs_ipackets,
	pfsyncs_ipackets6,
	pfsyncs_badif,
	pfsyncs_badttl,
	pfsyncs_hdrops,
	pfsyncs_badver,
	pfsyncs_badact,
	pfsyncs_badlen,
	pfsyncs_badauth,
	pfsyncs_stale,
	pfsyncs_badval,
	pfsyncs_badstate,
	pfsyncs_opackets,
	pfsyncs_opackets6,
	pfsyncs_onomem,
	pfsyncs_oerrors,
	pfsyncs_ncounters,
};

/*
 * this shows where a pf state is with respect to the syncing.
 */
#define PFSYNC_S_IACK	0x00
#define PFSYNC_S_UPD_C	0x01
#define PFSYNC_S_DEL	0x02
#define PFSYNC_S_INS	0x03
#define PFSYNC_S_UPD	0x04
#define PFSYNC_S_COUNT	0x05

#define PFSYNC_S_NONE	0xd0
#define PFSYNC_S_SYNC	0xd1
#define PFSYNC_S_PFSYNC	0xd2
#define PFSYNC_S_DEAD	0xde

int			pfsync_input4(struct mbuf **, int *, int, int,
			    struct netstack *);
int			pfsync_sysctl(int *, u_int,  void *, size_t *,
			    void *, size_t);

#define	PFSYNC_SI_IOCTL		0x01
#define	PFSYNC_SI_CKSUM		0x02
#define	PFSYNC_SI_ACK		0x04
#define	PFSYNC_SI_PFSYNC	0x08

void			pfsync_init_state(struct pf_state *,
			    const struct pf_state_key *,
			    const struct pf_state_key *, int);
void			pfsync_insert_state(struct pf_state *);
void			pfsync_update_state(struct pf_state *);
void			pfsync_delete_state(struct pf_state *);
void			pfsync_clear_states(u_int32_t, const char *);

void			pfsync_update_tdb(struct tdb *, int);
void			pfsync_delete_tdb(struct tdb *);

int			pfsync_defer(struct pf_state *, struct mbuf *);

int			pfsync_is_up(void);
int			pfsync_state_in_use(struct pf_state *);
#endif /* _KERNEL */

#endif /* _NET_IF_PFSYNC_H_ */
