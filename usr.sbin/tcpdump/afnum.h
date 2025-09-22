/* $OpenBSD: afnum.h,v 1.3 2024/12/18 06:36:48 tb Exp $ */

/*
 * Copyright (c) 2006 Reyk Floeter <reyk@openbsd.org>
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

#ifndef _AFNUM_H
#define _AFNUM_H

/*
 * RFC3232 address family numbers
 * see http://www.iana.org/assignments/address-family-numbers
 */
#define AFNUM_INET	1
#define AFNUM_INET6	2
#define AFNUM_NSAP	3
#define AFNUM_HDLC	4
#define AFNUM_BBN1822	5
#define AFNUM_802	6
#define AFNUM_E163	7
#define AFNUM_E164	8
#define AFNUM_F69	9
#define AFNUM_X121	10
#define AFNUM_IPX	11
#define AFNUM_ATALK	12
#define AFNUM_DECNET	13
#define AFNUM_BANYAN	14
#define AFNUM_E164NSAP	15
#define AFNUM_DNS	16
#define AFNUM_DN	17
#define AFNUM_AS	18
#define AFNUM_XTPINET	19
#define AFNUM_XTPINET6	20
#define AFNUM_XTP	21
#define AFNUM_FCPORT	22
#define AFNUM_FCNODE	23
#define AFNUM_GWID	24
#define AFNUM_MAX	24
#define AFNUM_RESERVED	65535

#define AFNUM_NAME_STR	{						\
	[0] = "Reserved",						\
	[AFNUM_INET] = "IPv4",						\
	[AFNUM_INET6] = "IPv6",						\
	[AFNUM_NSAP] = "NSAP",						\
	[AFNUM_HDLC] = "HDLC",						\
	[AFNUM_BBN1822] = "BBN 1822",					\
	[AFNUM_802] = "802",						\
	[AFNUM_E163] = "E.163",						\
	[AFNUM_E164] = "E.164",						\
	[AFNUM_F69] = "F.69",						\
	[AFNUM_X121] = "X.121",						\
	[AFNUM_IPX] = "IPX",						\
	[AFNUM_ATALK] = "Appletalk",					\
	[AFNUM_DECNET] = "Decnet IV",					\
	[AFNUM_BANYAN] = "Banyan Vines",				\
	[AFNUM_E164NSAP] = "E.164 with NSAP subaddress",		\
	[AFNUM_DNS] = "DNS",						\
	[AFNUM_DN] = "Distinguished Name",				\
	[AFNUM_AS] = "AS Number",					\
	[AFNUM_XTPINET] = "XTP over IPv4",				\
	[AFNUM_XTPINET6] = "XTP over IPv6",				\
	[AFNUM_XTP] = "XTP native mode",				\
	[AFNUM_FCPORT] = "Fibre Channel WWPN",				\
	[AFNUM_FCNODE] = "Fibre Channel WWNN",				\
	[AFNUM_GWID] = "GWID",						\
}

#endif /* _AFNUM_H */

