/*	$OpenBSD: parser.h,v 1.46 2023/04/21 09:12:41 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <sys/types.h>

#include "bgpd.h"

enum actions {
	NONE,
	SHOW,
	SHOW_SUMMARY,
	SHOW_SUMMARY_TERSE,
	SHOW_NEIGHBOR,
	SHOW_NEIGHBOR_TIMERS,
	SHOW_NEIGHBOR_TERSE,
	SHOW_FIB,
	SHOW_FIB_TABLES,
	SHOW_RIB,
	SHOW_MRT,
	SHOW_SET,
	SHOW_RTR,
	SHOW_RIB_MEM,
	SHOW_NEXTHOP,
	SHOW_INTERFACE,
	SHOW_METRICS,
	RELOAD,
	FIB,
	FIB_COUPLE,
	FIB_DECOUPLE,
	LOG_VERBOSE,
	LOG_BRIEF,
	NEIGHBOR,
	NEIGHBOR_UP,
	NEIGHBOR_DOWN,
	NEIGHBOR_CLEAR,
	NEIGHBOR_RREFRESH,
	NEIGHBOR_DESTROY,
	NETWORK_ADD,
	NETWORK_REMOVE,
	NETWORK_FLUSH,
	NETWORK_SHOW,
	NETWORK_MRT,
	NETWORK_BULK_ADD,
	NETWORK_BULK_REMOVE,
	FLOWSPEC_ADD,
	FLOWSPEC_REMOVE,
	FLOWSPEC_FLUSH,
	FLOWSPEC_SHOW,
};

struct parse_result {
	struct bgpd_addr	 addr;
	struct bgpd_addr	 peeraddr;
	struct filter_as	 as;
	struct filter_set_head	 set;
	struct community	 community;
	char			 peerdesc[PEER_DESCR_LEN];
	char			 rib[PEER_DESCR_LEN];
	char			 reason[REASON_LEN];
	const char		*ext_comm_subtype;
	uint64_t		 rd;
	int			 flags;
	int			 is_group;
	int			 mrtfd;
	u_int			 rtableid;
	uint32_t		 pathid;
	enum actions		 action;
	uint8_t			 validation_state;
	uint8_t			 prefixlen;
	uint8_t			 aid;
	struct flowstate	{
		struct bgpd_addr	src;
		struct bgpd_addr	dst;
		uint8_t			*components[FLOWSPEC_TYPE_MAX];
		uint16_t		complen[FLOWSPEC_TYPE_MAX];
		uint8_t			srclen;
		uint8_t			dstlen;
	}			flow;
};

__dead void		 usage(void);
struct parse_result	*parse(int, char *[]);
int			 parse_prefix(const char *, size_t, struct bgpd_addr *,
			     uint8_t *);
