/*	$OpenBSD: pptp_conf.h,v 1.2 2014/03/22 04:32:39 yasuoka Exp $	*/

/*
 * Copyright (c) 2012 YASUOKA Masahiko <yasuoka@openbsd.org>
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
#ifndef PPTP_CONF_H
#define PPTP_CONF_H 1

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <stdbool.h>

#define	PPTP_NAME_LEN		16

TAILQ_HEAD(pptp_confs, pptp_conf);

struct pptp_listen_addr {
	struct sockaddr_storage	 addr;
	TAILQ_ENTRY(pptp_listen_addr)
				 entry;
};

struct pptp_conf {
	TAILQ_ENTRY(pptp_conf)	 entry;
	char			 name[PPTP_NAME_LEN];
	char			*hostname;
	char			*vendor_name;
	int			 echo_interval;
	int			 echo_timeout;
	TAILQ_HEAD(pptp_listen_addrs, pptp_listen_addr)
				 listen;
	bool			 ctrl_in_pktdump;
	bool			 ctrl_out_pktdump;
	bool			 data_in_pktdump;
	bool			 data_out_pktdump;
};

#endif
