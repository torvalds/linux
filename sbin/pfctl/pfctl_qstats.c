/*	$OpenBSD: pfctl_qstats.c,v 1.30 2004/04/27 21:47:32 kjc Exp $ */

/*
 * Copyright (c) Henning Brauer <henning@openbsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define PFIOC_USE_LATEST

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>
#include <arpa/inet.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/altq/altq.h>
#include <net/altq/altq_cbq.h>
#include <net/altq/altq_codel.h>
#include <net/altq/altq_priq.h>
#include <net/altq/altq_hfsc.h>
#include <net/altq/altq_fairq.h>

#include "pfctl.h"
#include "pfctl_parser.h"

union class_stats {
	class_stats_t		cbq_stats;
	struct priq_classstats	priq_stats;
	struct hfsc_classstats	hfsc_stats;
	struct fairq_classstats fairq_stats;
	struct codel_ifstats	codel_stats;
};

#define AVGN_MAX	8
#define STAT_INTERVAL	5

struct queue_stats {
	union class_stats	 data;
	int			 avgn;
	double			 avg_bytes;
	double			 avg_packets;
	u_int64_t		 prev_bytes;
	u_int64_t		 prev_packets;
};

struct pf_altq_node {
	struct pf_altq		 altq;
	struct pf_altq_node	*next;
	struct pf_altq_node	*children;
	struct queue_stats	 qstats;
};

int			 pfctl_update_qstats(int, struct pf_altq_node **);
void			 pfctl_insert_altq_node(struct pf_altq_node **,
			    const struct pf_altq, const struct queue_stats);
struct pf_altq_node	*pfctl_find_altq_node(struct pf_altq_node *,
			    const char *, const char *);
void			 pfctl_print_altq_node(int, const struct pf_altq_node *,
			    unsigned, int);
void			 print_cbqstats(struct queue_stats);
void			 print_codelstats(struct queue_stats);
void			 print_priqstats(struct queue_stats);
void			 print_hfscstats(struct queue_stats);
void			 print_fairqstats(struct queue_stats);
void			 pfctl_free_altq_node(struct pf_altq_node *);
void			 pfctl_print_altq_nodestat(int,
			    const struct pf_altq_node *);

void			 update_avg(struct pf_altq_node *);

int
pfctl_show_altq(int dev, const char *iface, int opts, int verbose2)
{
	struct pf_altq_node	*root = NULL, *node;
	int			 nodes, dotitle = (opts & PF_OPT_SHOWALL);

#ifdef __FreeBSD__
	if (!altqsupport)
		return (-1);
#endif

	if ((nodes = pfctl_update_qstats(dev, &root)) < 0)
		return (-1);

	if (nodes == 0)
		printf("No queue in use\n");
	for (node = root; node != NULL; node = node->next) {
		if (iface != NULL && strcmp(node->altq.ifname, iface))
			continue;
		if (dotitle) {
			pfctl_print_title("ALTQ:");
			dotitle = 0;
		}
		pfctl_print_altq_node(dev, node, 0, opts);
	}

	while (verbose2 && nodes > 0) {
		printf("\n");
		fflush(stdout);
		sleep(STAT_INTERVAL);
		if ((nodes = pfctl_update_qstats(dev, &root)) == -1)
			return (-1);
		for (node = root; node != NULL; node = node->next) {
			if (iface != NULL && strcmp(node->altq.ifname, iface))
				continue;
#ifdef __FreeBSD__
			if (node->altq.local_flags & PFALTQ_FLAG_IF_REMOVED)
				continue;
#endif
			pfctl_print_altq_node(dev, node, 0, opts);
		}
	}
	pfctl_free_altq_node(root);
	return (0);
}

int
pfctl_update_qstats(int dev, struct pf_altq_node **root)
{
	struct pf_altq_node	*node;
	struct pfioc_altq	 pa;
	struct pfioc_qstats	 pq;
	u_int32_t		 mnr, nr;
	struct queue_stats	 qstats;
	static	u_int32_t	 last_ticket;

	memset(&pa, 0, sizeof(pa));
	memset(&pq, 0, sizeof(pq));
	memset(&qstats, 0, sizeof(qstats));
	pa.version = PFIOC_ALTQ_VERSION;
	if (ioctl(dev, DIOCGETALTQS, &pa)) {
		warn("DIOCGETALTQS");
		return (-1);
	}

	/* if a new set is found, start over */
	if (pa.ticket != last_ticket && *root != NULL) {
		pfctl_free_altq_node(*root);
		*root = NULL;
	}
	last_ticket = pa.ticket;

	mnr = pa.nr;
	for (nr = 0; nr < mnr; ++nr) {
		pa.nr = nr;
		if (ioctl(dev, DIOCGETALTQ, &pa)) {
			warn("DIOCGETALTQ");
			return (-1);
		}
#ifdef __FreeBSD__
		if ((pa.altq.qid > 0 || pa.altq.scheduler == ALTQT_CODEL) &&
		    !(pa.altq.local_flags & PFALTQ_FLAG_IF_REMOVED)) {
#else
		if (pa.altq.qid > 0) {
#endif
			pq.nr = nr;
			pq.ticket = pa.ticket;
			pq.buf = &qstats.data;
			pq.nbytes = sizeof(qstats.data);
			pq.version = altq_stats_version(pa.altq.scheduler);
			if (ioctl(dev, DIOCGETQSTATS, &pq)) {
				warn("DIOCGETQSTATS");
				return (-1);
			}
			if ((node = pfctl_find_altq_node(*root, pa.altq.qname,
			    pa.altq.ifname)) != NULL) {
				memcpy(&node->qstats.data, &qstats.data,
				    sizeof(qstats.data));
				update_avg(node);
			} else {
				pfctl_insert_altq_node(root, pa.altq, qstats);
			}
		}
#ifdef __FreeBSD__
		else if (pa.altq.local_flags & PFALTQ_FLAG_IF_REMOVED) {
			memset(&qstats.data, 0, sizeof(qstats.data));
			if ((node = pfctl_find_altq_node(*root, pa.altq.qname,
			    pa.altq.ifname)) != NULL) {
				memcpy(&node->qstats.data, &qstats.data,
				    sizeof(qstats.data));
				update_avg(node);
			} else {
				pfctl_insert_altq_node(root, pa.altq, qstats);
			}
		}
#endif
	}
	return (mnr);
}

void
pfctl_insert_altq_node(struct pf_altq_node **root,
    const struct pf_altq altq, const struct queue_stats qstats)
{
	struct pf_altq_node	*node;

	node = calloc(1, sizeof(struct pf_altq_node));
	if (node == NULL)
		err(1, "pfctl_insert_altq_node: calloc");
	memcpy(&node->altq, &altq, sizeof(struct pf_altq));
	memcpy(&node->qstats, &qstats, sizeof(qstats));
	node->next = node->children = NULL;

	if (*root == NULL)
		*root = node;
	else if (!altq.parent[0]) {
		struct pf_altq_node	*prev = *root;

		while (prev->next != NULL)
			prev = prev->next;
		prev->next = node;
	} else {
		struct pf_altq_node	*parent;

		parent = pfctl_find_altq_node(*root, altq.parent, altq.ifname);
		if (parent == NULL)
			errx(1, "parent %s not found", altq.parent);
		if (parent->children == NULL)
			parent->children = node;
		else {
			struct pf_altq_node *prev = parent->children;

			while (prev->next != NULL)
				prev = prev->next;
			prev->next = node;
		}
	}
	update_avg(node);
}

struct pf_altq_node *
pfctl_find_altq_node(struct pf_altq_node *root, const char *qname,
    const char *ifname)
{
	struct pf_altq_node	*node, *child;

	for (node = root; node != NULL; node = node->next) {
		if (!strcmp(node->altq.qname, qname)
		    && !(strcmp(node->altq.ifname, ifname)))
			return (node);
		if (node->children != NULL) {
			child = pfctl_find_altq_node(node->children, qname,
			    ifname);
			if (child != NULL)
				return (child);
		}
	}
	return (NULL);
}

void
pfctl_print_altq_node(int dev, const struct pf_altq_node *node,
    unsigned int level, int opts)
{
	const struct pf_altq_node	*child;

	if (node == NULL)
		return;

	print_altq(&node->altq, level, NULL, NULL);

	if (node->children != NULL) {
		printf("{");
		for (child = node->children; child != NULL;
		    child = child->next) {
			printf("%s", child->altq.qname);
			if (child->next != NULL)
				printf(", ");
		}
		printf("}");
	}
	printf("\n");

	if (opts & PF_OPT_VERBOSE)
		pfctl_print_altq_nodestat(dev, node);

	if (opts & PF_OPT_DEBUG)
		printf("  [ qid=%u ifname=%s ifbandwidth=%s ]\n",
		    node->altq.qid, node->altq.ifname,
		    rate2str((double)(node->altq.ifbandwidth)));

	for (child = node->children; child != NULL;
	    child = child->next)
		pfctl_print_altq_node(dev, child, level + 1, opts);
}

void
pfctl_print_altq_nodestat(int dev, const struct pf_altq_node *a)
{
	if (a->altq.qid == 0 && a->altq.scheduler != ALTQT_CODEL)
		return;

#ifdef __FreeBSD__
	if (a->altq.local_flags & PFALTQ_FLAG_IF_REMOVED)
		return;
#endif
	switch (a->altq.scheduler) {
	case ALTQT_CBQ:
		print_cbqstats(a->qstats);
		break;
	case ALTQT_PRIQ:
		print_priqstats(a->qstats);
		break;
	case ALTQT_HFSC:
		print_hfscstats(a->qstats);
		break;
	case ALTQT_FAIRQ:
		print_fairqstats(a->qstats);
		break;
	case ALTQT_CODEL:
		print_codelstats(a->qstats);
		break;
	}
}

void
print_cbqstats(struct queue_stats cur)
{
	printf("  [ pkts: %10llu  bytes: %10llu  "
	    "dropped pkts: %6llu bytes: %6llu ]\n",
	    (unsigned long long)cur.data.cbq_stats.xmit_cnt.packets,
	    (unsigned long long)cur.data.cbq_stats.xmit_cnt.bytes,
	    (unsigned long long)cur.data.cbq_stats.drop_cnt.packets,
	    (unsigned long long)cur.data.cbq_stats.drop_cnt.bytes);
	printf("  [ qlength: %3d/%3d  borrows: %6u  suspends: %6u ]\n",
	    cur.data.cbq_stats.qcnt, cur.data.cbq_stats.qmax,
	    cur.data.cbq_stats.borrows, cur.data.cbq_stats.delays);

	if (cur.avgn < 2)
		return;

	printf("  [ measured: %7.1f packets/s, %s/s ]\n",
	    cur.avg_packets / STAT_INTERVAL,
	    rate2str((8 * cur.avg_bytes) / STAT_INTERVAL));
}

void
print_codelstats(struct queue_stats cur)
{
	printf("  [ pkts: %10llu  bytes: %10llu  "
	    "dropped pkts: %6llu bytes: %6llu ]\n",
	    (unsigned long long)cur.data.codel_stats.cl_xmitcnt.packets,
	    (unsigned long long)cur.data.codel_stats.cl_xmitcnt.bytes,
	    (unsigned long long)cur.data.codel_stats.cl_dropcnt.packets +
	    cur.data.codel_stats.stats.drop_cnt.packets,
	    (unsigned long long)cur.data.codel_stats.cl_dropcnt.bytes +
	    cur.data.codel_stats.stats.drop_cnt.bytes);
	printf("  [ qlength: %3d/%3d ]\n",
	    cur.data.codel_stats.qlength, cur.data.codel_stats.qlimit);

	if (cur.avgn < 2)
		return;

	printf("  [ measured: %7.1f packets/s, %s/s ]\n",
	    cur.avg_packets / STAT_INTERVAL,
	    rate2str((8 * cur.avg_bytes) / STAT_INTERVAL));
}

void
print_priqstats(struct queue_stats cur)
{
	printf("  [ pkts: %10llu  bytes: %10llu  "
	    "dropped pkts: %6llu bytes: %6llu ]\n",
	    (unsigned long long)cur.data.priq_stats.xmitcnt.packets,
	    (unsigned long long)cur.data.priq_stats.xmitcnt.bytes,
	    (unsigned long long)cur.data.priq_stats.dropcnt.packets,
	    (unsigned long long)cur.data.priq_stats.dropcnt.bytes);
	printf("  [ qlength: %3d/%3d ]\n",
	    cur.data.priq_stats.qlength, cur.data.priq_stats.qlimit);

	if (cur.avgn < 2)
		return;

	printf("  [ measured: %7.1f packets/s, %s/s ]\n",
	    cur.avg_packets / STAT_INTERVAL,
	    rate2str((8 * cur.avg_bytes) / STAT_INTERVAL));
}

void
print_hfscstats(struct queue_stats cur)
{
	printf("  [ pkts: %10llu  bytes: %10llu  "
	    "dropped pkts: %6llu bytes: %6llu ]\n",
	    (unsigned long long)cur.data.hfsc_stats.xmit_cnt.packets,
	    (unsigned long long)cur.data.hfsc_stats.xmit_cnt.bytes,
	    (unsigned long long)cur.data.hfsc_stats.drop_cnt.packets,
	    (unsigned long long)cur.data.hfsc_stats.drop_cnt.bytes);
	printf("  [ qlength: %3d/%3d ]\n",
	    cur.data.hfsc_stats.qlength, cur.data.hfsc_stats.qlimit);

	if (cur.avgn < 2)
		return;

	printf("  [ measured: %7.1f packets/s, %s/s ]\n",
	    cur.avg_packets / STAT_INTERVAL,
	    rate2str((8 * cur.avg_bytes) / STAT_INTERVAL));
}

void
print_fairqstats(struct queue_stats cur)
{
	printf("  [ pkts: %10llu  bytes: %10llu  "
	    "dropped pkts: %6llu bytes: %6llu ]\n",
	    (unsigned long long)cur.data.fairq_stats.xmit_cnt.packets,
	    (unsigned long long)cur.data.fairq_stats.xmit_cnt.bytes,
	    (unsigned long long)cur.data.fairq_stats.drop_cnt.packets,
	    (unsigned long long)cur.data.fairq_stats.drop_cnt.bytes);
	printf("  [ qlength: %3d/%3d ]\n",
	    cur.data.fairq_stats.qlength, cur.data.fairq_stats.qlimit);

	if (cur.avgn < 2)
		return;

	printf("  [ measured: %7.1f packets/s, %s/s ]\n",
	    cur.avg_packets / STAT_INTERVAL,
	    rate2str((8 * cur.avg_bytes) / STAT_INTERVAL));
}

void
pfctl_free_altq_node(struct pf_altq_node *node)
{
	while (node != NULL) {
		struct pf_altq_node	*prev;

		if (node->children != NULL)
			pfctl_free_altq_node(node->children);
		prev = node;
		node = node->next;
		free(prev);
	}
}

void
update_avg(struct pf_altq_node *a)
{
	struct queue_stats	*qs;
	u_int64_t		 b, p;
	int			 n;

	if (a->altq.qid == 0 && a->altq.scheduler != ALTQT_CODEL)
		return;

	qs = &a->qstats;
	n = qs->avgn;

	switch (a->altq.scheduler) {
	case ALTQT_CBQ:
		b = qs->data.cbq_stats.xmit_cnt.bytes;
		p = qs->data.cbq_stats.xmit_cnt.packets;
		break;
	case ALTQT_PRIQ:
		b = qs->data.priq_stats.xmitcnt.bytes;
		p = qs->data.priq_stats.xmitcnt.packets;
		break;
	case ALTQT_HFSC:
		b = qs->data.hfsc_stats.xmit_cnt.bytes;
		p = qs->data.hfsc_stats.xmit_cnt.packets;
		break;
	case ALTQT_FAIRQ:
		b = qs->data.fairq_stats.xmit_cnt.bytes;
		p = qs->data.fairq_stats.xmit_cnt.packets;
		break;
	case ALTQT_CODEL:
		b = qs->data.codel_stats.cl_xmitcnt.bytes;
		p = qs->data.codel_stats.cl_xmitcnt.packets;
		break;
	default:
		b = 0;
		p = 0;
		break;
	}

	if (n == 0) {
		qs->prev_bytes = b;
		qs->prev_packets = p;
		qs->avgn++;
		return;
	}

	if (b >= qs->prev_bytes)
		qs->avg_bytes = ((qs->avg_bytes * (n - 1)) +
		    (b - qs->prev_bytes)) / n;

	if (p >= qs->prev_packets)
		qs->avg_packets = ((qs->avg_packets * (n - 1)) +
		    (p - qs->prev_packets)) / n;

	qs->prev_bytes = b;
	qs->prev_packets = p;
	if (n < AVGN_MAX)
		qs->avgn++;
}
