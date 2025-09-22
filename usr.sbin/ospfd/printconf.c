/*	$OpenBSD: printconf.c,v 1.22 2020/01/21 20:38:52 remi Exp $ */

/*
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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

#include "ospf.h"
#include "ospfd.h"
#include "ospfe.h"

void	print_mainconf(struct ospfd_conf *);
const char *print_no(u_int16_t);
void	print_redistribute(struct redist_list *);
void	print_rtlabel(struct ospfd_conf *);
void	print_iface(struct iface *);

void
print_mainconf(struct ospfd_conf *conf)
{
	printf("router-id %s\n", inet_ntoa(conf->rtr_id));

	if (conf->flags & OSPFD_FLAG_NO_FIB_UPDATE)
		printf("fib-update no\n");
	else
		printf("fib-update yes\n");

	printf("fib-priority %hhu\n", conf->fib_priority);

	if (conf->rdomain)
		printf("rdomain %d\n", conf->rdomain);

	if (conf->rfc1583compat)
		printf("rfc1583compat yes\n");
	else
		printf("rfc1583compat no\n");

	if (conf->flags & OSPFD_FLAG_STUB_ROUTER)
		printf("stub router yes\n");

	print_redistribute(&conf->redist_list);
	print_rtlabel(conf);

	printf("spf-delay msec %u\n", conf->spf_delay);
	printf("spf-holdtime msec %u\n", conf->spf_hold_time);
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
print_redistribute(struct redist_list *rlh)
{
	struct redistribute	*r;

	SIMPLEQ_FOREACH(r, rlh, entry) {
		switch (r->type & ~REDIST_NO) {
		case REDIST_STATIC:
			printf("%sredistribute static ", print_no(r->type));
			break;
		case REDIST_CONNECTED:
			printf("%sredistribute connected ", print_no(r->type));
			break;
		case REDIST_LABEL:
			printf("%sredistribute rtlabel %s ",
			    print_no(r->type), rtlabel_id2name(r->label));
			break;
		case REDIST_ADDR:
			printf("%sredistribute %s/%d ",
			    print_no(r->type), inet_ntoa(r->addr),
			    mask2prefixlen(r->mask.s_addr));
			break;
		case REDIST_DEFAULT:
			printf("%sredistribute default ", print_no(r->type));
			break;
		}
		printf("set { metric %d type %d }",
		    (r->metric & LSA_METRIC_MASK),
		    ((r->metric & LSA_ASEXT_E_FLAG) == 0 ? 1 : 2));
		if (r->dependon[0])
			printf(" depend on %s", r->dependon);
		printf("\n");
	}
}

void
print_rtlabel(struct ospfd_conf *conf)
{
	struct n2id_label	*label;

	TAILQ_FOREACH(label, &rt_labels, entry)
		if (label->ext_tag)
			printf("rtlabel \"%s\" external-tag %u\n",
			    label->name, label->ext_tag);
}

void
print_iface(struct iface *iface)
{
	struct auth_md	*md;

	printf("\tinterface %s:%s {\n", iface->name, inet_ntoa(iface->addr));

	printf("\t\tmetric %d\n", iface->metric);

	if (iface->demote_group[0] != '\0')
		printf("\t\tdemote %s\n", iface->demote_group);
	if (iface->dependon[0] != '\0')
		printf("\t\tdepend on %s\n", iface->dependon);
	if (iface->passive)
		printf("\t\tpassive\n");
	else {
		printf("\t\tretransmit-interval %d\n", iface->rxmt_interval);
		if (iface->dead_interval == FAST_RTR_DEAD_TIME) {
			printf("\t\trouter-dead-time minimal\n");
			printf("\t\tfast-hello-interval msec %u\n",
			    iface->fast_hello_interval);
		} else {
			printf("\t\trouter-dead-time %d\n",
			    iface->dead_interval);
			printf("\t\thello-interval %d\n",
			    iface->hello_interval);
		}
		printf("\t\trouter-priority %d\n", iface->priority);
		printf("\t\ttransmit-delay %d\n", iface->transmit_delay);

		if (iface->type == IF_TYPE_POINTOPOINT)
			printf("\t\ttype p2p\n");

		printf("\t\tauth-type %s\n", if_auth_name(iface->auth_type));
		switch (iface->auth_type) {
		case AUTH_TYPE_NONE:
			break;
		case AUTH_TYPE_SIMPLE:
			printf("\t\tauth-key XXXXXX\n");
			break;
		case AUTH_TYPE_CRYPT:
			printf("\t\tauth-md-keyid %d\n", iface->auth_keyid);
			TAILQ_FOREACH(md, &iface->auth_md_list, entry)
				printf("\t\tauth-md %d XXXXXX\n", md->keyid);
			break;
		default:
			printf("\t\tunknown auth type!\n");
			break;
		}
	}

	printf("\t}\n");
}

void
print_config(struct ospfd_conf *conf)
{
	struct area	*area;
	struct iface	*iface;

	printf("\n");
	print_mainconf(conf);
	printf("\n");

	LIST_FOREACH(area, &conf->area_list, entry) {
		printf("area %s {\n", inet_ntoa(area->id));
		if (area->stub) {
			printf("\tstub");
			if (SIMPLEQ_EMPTY(&area->redist_list))
				printf("\n");
			else {
				printf(" ");
				print_redistribute(&area->redist_list);
			}
		}
		if (*area->demote_group)
			printf("\tdemote %s %d\n", area->demote_group,
			area->demote_level);
		LIST_FOREACH(iface, &area->iface_list, entry) {
			print_iface(iface);
		}
		printf("}\n\n");
	}
}
