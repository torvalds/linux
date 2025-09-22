/*	$OpenBSD: radiusd_ipcp.h,v 1.3 2024/09/15 05:29:11 yasuoka Exp $	*/

/*
 * Copyright (c) 2024 Internet Initiative Japan Inc.
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

#ifndef RADIUSD_IPCP_H
#define RADIUSD_IPCP_H 1

#include <netinet/in.h>
#include <stdint.h>

#include "radiusd.h"

#define	RADIUSD_IPCP_DAE_MAX_INFLIGHT	64

enum imsg_module_ipcp_type {
	IMSG_RADIUSD_MODULE_IPCP_DUMP = IMSG_RADIUSD_MODULE_MIN,
	IMSG_RADIUSD_MODULE_IPCP_MONITOR,
	IMSG_RADIUSD_MODULE_IPCP_DUMP_AND_MONITOR,
	IMSG_RADIUSD_MODULE_IPCP_START,
	IMSG_RADIUSD_MODULE_IPCP_STOP,
	IMSG_RADIUSD_MODULE_IPCP_DELETE,
	IMSG_RADIUSD_MODULE_IPCP_DISCONNECT
};

#define _PATH_RADIUSD_IPCP_DB	"/var/run/radiusd_ipcp.db"

struct radiusd_ipcp_db_record {
	unsigned			seq;
	char				session_id[256];
	char				auth_method[16];
	char				username[256];
	struct timespec			start;	/* Start time in boottime */
	struct timespec			timeout;/* Timeout time in boottime */
	struct in_addr			nas_ipv4;
	struct in6_addr			nas_ipv6;
	char				nas_id[256];
	char				tun_type[8];
	union {
		struct sockaddr_in	sin4;
		struct sockaddr_in6	sin6;
	}				tun_client;
};

struct radiusd_ipcp_db_dump {
	int				 islast;
	struct {
		int			 af;
		union {
			struct in_addr	 ipv4;
			struct in6_addr	 ipv6;
		}			 addr;
		struct radiusd_ipcp_db_record
					 rec;
	}				 records[0];
};

struct radiusd_ipcp_statistics {
	uint32_t			 ipackets;
	uint32_t			 opackets;
	uint64_t			 ibytes;
	uint64_t			 obytes;
	char				 cause[80];
};
#endif
