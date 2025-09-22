/*	$OpenBSD: printconf.c,v 1.8 2019/05/12 11:27:08 denis Exp $ */

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

#include "rip.h"
#include "ripd.h"
#include "ripe.h"

void	print_mainconf(struct ripd_conf *);
const char *print_no(u_int16_t);
void	print_redistribute(struct ripd_conf *);
void	print_iface(struct iface *);

void
print_mainconf(struct ripd_conf *conf)
{
	if (conf->flags & RIPD_FLAG_NO_FIB_UPDATE)
		printf("fib-update no\n");
	else
		printf("fib-update yes\n");

	printf("fib-priority %hhu\n", conf->fib_priority);

	print_redistribute(conf);

	if (conf->options & OPT_SPLIT_HORIZON)
		printf("split-horizon simple\n");
	else if (conf->options & OPT_SPLIT_POISONED)
		printf("split-horizon poisoned\n");
	else
		printf("split-horizon none\n");

	if (conf->options & OPT_TRIGGERED_UPDATES)
		printf("triggered-updates yes\n");
	else
		printf("triggered-updates no\n");
}

const char *
print_no(u_int16_t type)
{
	if (type & REDIST_NO)
		return ("no ");
	else
		return ("");
}

void
print_redistribute(struct ripd_conf *conf)
{
	struct redistribute	*r;

	SIMPLEQ_FOREACH(r, &conf->redist_list, entry) {
		switch (r->type & ~REDIST_NO) {
		case REDIST_STATIC:
			printf("%sredistribute static\n", print_no(r->type));
			break;
		case REDIST_CONNECTED:
			printf("%sredistribute connected\n", print_no(r->type));
			break;
		case REDIST_LABEL:
			printf("%sredistribute rtlabel %s\n",
			    print_no(r->type), rtlabel_id2name(r->label));
			break;
		case REDIST_DEFAULT:
			printf("redistribute default\n");
			break;
		case REDIST_ADDR:
			printf("%sredistribute %s/%d\n",
			    print_no(r->type), inet_ntoa(r->addr),
			    mask2prefixlen(r->mask.s_addr));
			break;
		}
	}
}

void
print_iface(struct iface *iface)
{
	struct auth_md	*m;

	printf("interface %s {\n", iface->name);

	if (iface->passive)
		printf("\tpassive\n");

	printf("\tcost %d\n", iface->cost);

	printf("\tauth-type %s\n", if_auth_name(iface->auth_type));
	switch (iface->auth_type) {
	case AUTH_NONE:
		break;
	case AUTH_SIMPLE:
		printf("\tauth-key XXXXXX\n");
		break;
	case AUTH_CRYPT:
		printf("\tauth-md-keyid %d\n", iface->auth_keyid);
		TAILQ_FOREACH(m, &iface->auth_md_list, entry)
			printf("\tauth-md %d XXXXXX\n", m->keyid);
		break;
	default:
		printf("\tunknown auth type!\n");
		break;
	}

	printf("}\n\n");
}

void
print_config(struct ripd_conf *conf)
{
	struct iface	*iface;

	print_mainconf(conf);
	printf("\n");

	LIST_FOREACH(iface, &conf->iface_list, entry) {
		print_iface(iface);
	}
}
