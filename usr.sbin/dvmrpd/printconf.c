/*	$OpenBSD: printconf.c,v 1.2 2011/04/10 22:12:34 jsg Exp $ */

/*
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
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

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "dvmrpe.h"

void	 print_mainconf(struct dvmrpd_conf *);
void	 print_iface(struct iface *);

void
print_mainconf(struct dvmrpd_conf *conf)
{
	if (conf->flags & DVMRPD_FLAG_NO_FIB_UPDATE)
		printf("fib-update no\n");
	else
		printf("fib-update yes\n");
}

void
print_iface(struct iface *iface)
{
	printf("interface %s {\n", iface->name);

	if (iface->passive)
		printf("\tpassive\n");

	printf("\tmetric %d\n", iface->metric);
	printf("\tquery-interval %d\n", iface->query_interval);
	printf("\tquery-response-interval %d\n", iface->query_resp_interval);
	printf("\trobustness %d\n", iface->robustness);
	printf("\tstartup-query-count %d\n", iface->startup_query_cnt);
	printf("\tlast-member-query-count %d\n", iface->last_member_query_cnt);
	printf("\tlast-member-query-interval %d\n",
	    iface->last_member_query_interval);
	printf("\tigmp-version %d\n", iface->igmp_version);

	printf("}\n");
}

void
print_config(struct dvmrpd_conf *conf)
{
	struct iface	*iface;

	printf("\n");
	print_mainconf(conf);
	printf("\n");

	LIST_FOREACH(iface, &conf->iface_list, entry) {
		print_iface(iface);
	}
	printf("\n");

}
