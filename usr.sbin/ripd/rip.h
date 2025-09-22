/*	$OpenBSD: rip.h,v 1.4 2007/10/18 17:00:59 deraadt Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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

/* RIP protocol definitions */

#ifndef	_RIP_H_
#define	_RIP_H_

/* misc */
#define	RIP_VERSION		2
#define ALL_RIP_ROUTERS		"224.0.0.9"
#define	RIP_PORT		520

#define	MIN_MD_ID		0
#define	MAX_MD_ID		255

/* metric */
#define	INFINITY		16
#define	DEFAULT_COST		1

/* timers */
#define	KEEPALIVE		30
#define	OFFSET			10
#define	FAILED_NBR_TIMEOUT	86400

#define MAX_RIP_ENTRIES		25

/* RIP command */
#define	COMMAND_REQUEST		1
#define	COMMAND_RESPONSE	2

#define	RIP_HDR_LEN		sizeof(struct rip_hdr)
#define	RIP_ENTRY_LEN		sizeof(struct rip_entry)

struct rip_hdr {
	u_int8_t	command;
	u_int8_t	version;
	u_int16_t	dummy;
};

struct rip_entry {
	u_int16_t	AFI;
	u_int16_t	route_tag;
	u_int32_t	address;
	u_int32_t	mask;
	u_int32_t	nexthop;
	u_int32_t	metric;
};

/* auth */
#define AUTH			0xFFFF
#define	AUTH_TRLR_HDR_LEN	4

/* auth general struct */
struct rip_auth {
	u_int16_t	 auth_fixed;
	u_int16_t	 auth_type;
};

/* Keyed MD5 auth struct */
struct md5_auth {
	u_int16_t	 auth_offset;
	u_int8_t	 auth_keyid;
	u_int8_t	 auth_length;
	u_int32_t	 auth_seq;
	u_int64_t	 auth_reserved;
};

#endif /* _RIP_H_ */
