/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)2003-2006 Helsinki University of Technology
 * Copyright (C)2003-2006 USAGI/WIDE Project
 */
/*
 * Authors:
 *	Noriaki TAKAMIYA @USAGI
 *	Masahide NAKAMURA @USAGI
 *	YOSHIFUJI Hideaki @USAGI
 */
#ifndef _NET_MIP6_H
#define _NET_MIP6_H

#include <linux/skbuff.h>
#include <net/sock.h>

/*
 * Mobility Header
 */
struct ip6_mh {
	__u8	ip6mh_proto;
	__u8	ip6mh_hdrlen;
	__u8	ip6mh_type;
	__u8	ip6mh_reserved;
	__u16	ip6mh_cksum;
	/* Followed by type specific messages */
	__u8	data[];
} __packed;

#define IP6_MH_TYPE_BRR		0   /* Binding Refresh Request */
#define IP6_MH_TYPE_HOTI	1   /* HOTI Message   */
#define IP6_MH_TYPE_COTI	2   /* COTI Message  */
#define IP6_MH_TYPE_HOT		3   /* HOT Message   */
#define IP6_MH_TYPE_COT		4   /* COT Message  */
#define IP6_MH_TYPE_BU		5   /* Binding Update */
#define IP6_MH_TYPE_BACK	6   /* Binding ACK */
#define IP6_MH_TYPE_BERROR	7   /* Binding Error */
#define IP6_MH_TYPE_MAX		IP6_MH_TYPE_BERROR

#endif
