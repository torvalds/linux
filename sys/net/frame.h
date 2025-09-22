/*	$OpenBSD: frame.h,v 1.1 2024/12/15 11:00:05 dlg Exp $ */

/*
 * Copyright (c) 2024 David Gwynne <dlg@openbsd.org>
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

#ifndef _NET_FRAME_H_
#define _NET_FRAME_H_

#define FRAME_ADDRLEN		8 /* big enough for Ethernet */
#define FRAME_DATALEN		32

struct sockaddr_frame {
	uint8_t			sfrm_len;
	uint8_t			sfrm_family; /* AF_FRAME */
	uint16_t		sfrm_proto;
	unsigned int		sfrm_ifindex;
	uint8_t			sfrm_addr[FRAME_ADDRLEN];
	char			sfrm_ifname[IFNAMSIZ];
	uint8_t			sfrm_data[FRAME_DATALEN];
};

#define FRAME_RECVDSTADDR	0  /* int */
#define FRAME_RECVPRIO		1  /* int */
#define FRAME_ADD_MEMBERSHIP	64 /* struct frame_mreq */
#define FRAME_DEL_MEMBERSHIP	65 /* struct frame_mreq */
#define FRAME_SENDPRIO		66 /* int: IF_HDRPRIO_{MIN-MAX,PACKET} */

struct frame_mreq {
	unsigned int		fmr_ifindex;
	uint8_t			fmr_addr[FRAME_ADDRLEN];
	char			fmr_ifname[IFNAMSIZ];
};

#endif /* _NET_FRAME_H_ */
