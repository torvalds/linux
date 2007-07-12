/*
 * Copyright (C)2003-2006 Helsinki University of Technology
 * Copyright (C)2003-2006 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#define MIP6_OPT_PAD_1	0
#define MIP6_OPT_PAD_N	1

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
	__u8	data[0];
} __attribute__ ((__packed__));

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
