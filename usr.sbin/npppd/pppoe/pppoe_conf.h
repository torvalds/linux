/*	$OpenBSD: pppoe_conf.h,v 1.1 2012/09/18 13:14:08 yasuoka Exp $	*/

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
#ifndef PPPOE_CONF_H
#define PPPOE_CONF_H 1

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <stdbool.h>

#define	PPPOE_NAME_LEN		16

TAILQ_HEAD(pppoe_confs, pppoe_conf);

#include <net/if.h>

struct pppoe_conf {
	TAILQ_ENTRY(pppoe_conf)  entry;
	char                     name[PPPOE_NAME_LEN];
	char                     if_name[IF_NAMESIZE];
	char                    *service_name;
	char                    *ac_name;
	bool                     accept_any_service;
	bool                     desc_in_pktdump;
	bool                     desc_out_pktdump;
	bool                     session_in_pktdump;
	bool                     session_out_pktdump;
};

#endif
