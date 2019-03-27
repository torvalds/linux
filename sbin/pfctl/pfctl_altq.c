/*	$OpenBSD: pfctl_altq.c,v 1.93 2007/10/15 02:16:35 deraadt Exp $	*/

/*
 * Copyright (c) 2002
 *	Sony Computer Science Laboratories Inc.
 * Copyright (c) 2002, 2003 Henning Brauer <henning@openbsd.org>
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
#include <sys/bitset.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <search.h>
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

#include "pfctl_parser.h"
#include "pfctl.h"

#define is_sc_null(sc)	(((sc) == NULL) || ((sc)->m1 == 0 && (sc)->m2 == 0))

static STAILQ_HEAD(interfaces, pfctl_altq) interfaces = STAILQ_HEAD_INITIALIZER(interfaces);
static struct hsearch_data queue_map;
static struct hsearch_data if_map;
static struct hsearch_data qid_map;

static struct pfctl_altq *pfaltq_lookup(char *ifname);
static struct pfctl_altq *qname_to_pfaltq(const char *, const char *);
static u_int32_t	 qname_to_qid(char *);

static int	eval_pfqueue_cbq(struct pfctl *, struct pf_altq *,
		    struct pfctl_altq *);
static int	cbq_compute_idletime(struct pfctl *, struct pf_altq *);
static int	check_commit_cbq(int, int, struct pfctl_altq *);
static int	print_cbq_opts(const struct pf_altq *);

static int	print_codel_opts(const struct pf_altq *,
		    const struct node_queue_opt *);

static int	eval_pfqueue_priq(struct pfctl *, struct pf_altq *,
		    struct pfctl_altq *);
static int	check_commit_priq(int, int, struct pfctl_altq *);
static int	print_priq_opts(const struct pf_altq *);

static int	eval_pfqueue_hfsc(struct pfctl *, struct pf_altq *,
		    struct pfctl_altq *, struct pfctl_altq *);
static int	check_commit_hfsc(int, int, struct pfctl_altq *);
static int	print_hfsc_opts(const struct pf_altq *,
		    const struct node_queue_opt *);

static int	eval_pfqueue_fairq(struct pfctl *, struct pf_altq *,
		    struct pfctl_altq *, struct pfctl_altq *);
static int	print_fairq_opts(const struct pf_altq *,
		    const struct node_queue_opt *);
static int	check_commit_fairq(int, int, struct pfctl_altq *);

static void		 gsc_add_sc(struct gen_sc *, struct service_curve *);
static int		 is_gsc_under_sc(struct gen_sc *,
			     struct service_curve *);
static struct segment	*gsc_getentry(struct gen_sc *, double);
static int		 gsc_add_seg(struct gen_sc *, double, double, double,
			     double);
static double		 sc_x2y(struct service_curve *, double);

#ifdef __FreeBSD__
u_int64_t	getifspeed(int, char *);
#else
u_int32_t	 getifspeed(char *);
#endif
u_long		 getifmtu(char *);
int		 eval_queue_opts(struct pf_altq *, struct node_queue_opt *,
		     u_int64_t);
u_int64_t	 eval_bwspec(struct node_queue_bw *, u_int64_t);
void		 print_hfsc_sc(const char *, u_int, u_int, u_int,
		     const struct node_hfsc_sc *);
void		 print_fairq_sc(const char *, u_int, u_int, u_int,
		     const struct node_fairq_sc *);

static __attribute__((constructor)) void
pfctl_altq_init(void)
{
	/*
	 * As hdestroy() will never be called on these tables, it will be
	 * safe to use references into the stored data as keys.
	 */
	if (hcreate_r(0, &queue_map) == 0)
		err(1, "Failed to create altq queue map");
	if (hcreate_r(0, &if_map) == 0)
		err(1, "Failed to create altq interface map");
	if (hcreate_r(0, &qid_map) == 0)
		err(1, "Failed to create altq queue id map");
}

void
pfaltq_store(struct pf_altq *a)
{
	struct pfctl_altq	*altq;
	ENTRY 			 item;
	ENTRY			*ret_item;
	size_t			 key_size;
	
	if ((altq = malloc(sizeof(*altq))) == NULL)
		err(1, "queue malloc");
	memcpy(&altq->pa, a, sizeof(struct pf_altq));
	memset(&altq->meta, 0, sizeof(altq->meta));

	if (a->qname[0] == 0) {
		item.key = altq->pa.ifname;
		item.data = altq;
		if (hsearch_r(item, ENTER, &ret_item, &if_map) == 0)
			err(1, "interface map insert");
		STAILQ_INSERT_TAIL(&interfaces, altq, meta.link);
	} else {
		key_size = sizeof(a->ifname) + sizeof(a->qname);
		if ((item.key = malloc(key_size)) == NULL)
			err(1, "queue map key malloc");
		snprintf(item.key, key_size, "%s:%s", a->ifname, a->qname);
		item.data = altq;
		if (hsearch_r(item, ENTER, &ret_item, &queue_map) == 0)
			err(1, "queue map insert");

		item.key = altq->pa.qname;
		item.data = &altq->pa.qid;
		if (hsearch_r(item, ENTER, &ret_item, &qid_map) == 0)
			err(1, "qid map insert");
	}
}

static struct pfctl_altq *
pfaltq_lookup(char *ifname)
{
	ENTRY	 item;
	ENTRY	*ret_item;

	item.key = ifname;
	if (hsearch_r(item, FIND, &ret_item, &if_map) == 0)
		return (NULL);

	return (ret_item->data);
}

static struct pfctl_altq *
qname_to_pfaltq(const char *qname, const char *ifname)
{
	ENTRY	 item;
	ENTRY	*ret_item;
	char	 key[IFNAMSIZ + PF_QNAME_SIZE];

	item.key = key;
	snprintf(item.key, sizeof(key), "%s:%s", ifname, qname);
	if (hsearch_r(item, FIND, &ret_item, &queue_map) == 0)
		return (NULL);

	return (ret_item->data);
}

static u_int32_t
qname_to_qid(char *qname)
{
	ENTRY	 item;
	ENTRY	*ret_item;
	uint32_t qid;
	
	/*
	 * We guarantee that same named queues on different interfaces
	 * have the same qid.
	 */
	item.key = qname;
	if (hsearch_r(item, FIND, &ret_item, &qid_map) == 0)
		return (0);

	qid = *(uint32_t *)ret_item->data;
	return (qid);
}

void
print_altq(const struct pf_altq *a, unsigned int level,
    struct node_queue_bw *bw, struct node_queue_opt *qopts)
{
	if (a->qname[0] != 0) {
		print_queue(a, level, bw, 1, qopts);
		return;
	}

#ifdef __FreeBSD__
	if (a->local_flags & PFALTQ_FLAG_IF_REMOVED)
		printf("INACTIVE ");
#endif

	printf("altq on %s ", a->ifname);

	switch (a->scheduler) {
	case ALTQT_CBQ:
		if (!print_cbq_opts(a))
			printf("cbq ");
		break;
	case ALTQT_PRIQ:
		if (!print_priq_opts(a))
			printf("priq ");
		break;
	case ALTQT_HFSC:
		if (!print_hfsc_opts(a, qopts))
			printf("hfsc ");
		break;
	case ALTQT_FAIRQ:
		if (!print_fairq_opts(a, qopts))
			printf("fairq ");
		break;
	case ALTQT_CODEL:
		if (!print_codel_opts(a, qopts))
			printf("codel ");
		break;
	}

	if (bw != NULL && bw->bw_percent > 0) {
		if (bw->bw_percent < 100)
			printf("bandwidth %u%% ", bw->bw_percent);
	} else
		printf("bandwidth %s ", rate2str((double)a->ifbandwidth));

	if (a->qlimit != DEFAULT_QLIMIT)
		printf("qlimit %u ", a->qlimit);
	printf("tbrsize %u ", a->tbrsize);
}

void
print_queue(const struct pf_altq *a, unsigned int level,
    struct node_queue_bw *bw, int print_interface,
    struct node_queue_opt *qopts)
{
	unsigned int	i;

#ifdef __FreeBSD__
	if (a->local_flags & PFALTQ_FLAG_IF_REMOVED)
		printf("INACTIVE ");
#endif
	printf("queue ");
	for (i = 0; i < level; ++i)
		printf(" ");
	printf("%s ", a->qname);
	if (print_interface)
		printf("on %s ", a->ifname);
	if (a->scheduler == ALTQT_CBQ || a->scheduler == ALTQT_HFSC ||
		a->scheduler == ALTQT_FAIRQ) {
		if (bw != NULL && bw->bw_percent > 0) {
			if (bw->bw_percent < 100)
				printf("bandwidth %u%% ", bw->bw_percent);
		} else
			printf("bandwidth %s ", rate2str((double)a->bandwidth));
	}
	if (a->priority != DEFAULT_PRIORITY)
		printf("priority %u ", a->priority);
	if (a->qlimit != DEFAULT_QLIMIT)
		printf("qlimit %u ", a->qlimit);
	switch (a->scheduler) {
	case ALTQT_CBQ:
		print_cbq_opts(a);
		break;
	case ALTQT_PRIQ:
		print_priq_opts(a);
		break;
	case ALTQT_HFSC:
		print_hfsc_opts(a, qopts);
		break;
	case ALTQT_FAIRQ:
		print_fairq_opts(a, qopts);
		break;
	}
}

/*
 * eval_pfaltq computes the discipline parameters.
 */
int
eval_pfaltq(struct pfctl *pf, struct pf_altq *pa, struct node_queue_bw *bw,
    struct node_queue_opt *opts)
{
	u_int64_t	rate;
	u_int		size, errors = 0;

	if (bw->bw_absolute > 0)
		pa->ifbandwidth = bw->bw_absolute;
	else
#ifdef __FreeBSD__
		if ((rate = getifspeed(pf->dev, pa->ifname)) == 0) {
#else
		if ((rate = getifspeed(pa->ifname)) == 0) {
#endif
			fprintf(stderr, "interface %s does not know its bandwidth, "
			    "please specify an absolute bandwidth\n",
			    pa->ifname);
			errors++;
		} else if ((pa->ifbandwidth = eval_bwspec(bw, rate)) == 0)
			pa->ifbandwidth = rate;

	/*
	 * Limit bandwidth to UINT_MAX for schedulers that aren't 64-bit ready.
	 */
	if ((pa->scheduler != ALTQT_HFSC) && (pa->ifbandwidth > UINT_MAX)) {
		pa->ifbandwidth = UINT_MAX;
		warnx("interface %s bandwidth limited to %" PRIu64 " bps "
		    "because selected scheduler is 32-bit limited\n", pa->ifname,
		    pa->ifbandwidth);
	}
	errors += eval_queue_opts(pa, opts, pa->ifbandwidth);

	/* if tbrsize is not specified, use heuristics */
	if (pa->tbrsize == 0) {
		rate = pa->ifbandwidth;
		if (rate <= 1 * 1000 * 1000)
			size = 1;
		else if (rate <= 10 * 1000 * 1000)
			size = 4;
		else if (rate <= 200 * 1000 * 1000)
			size = 8;
		else if (rate <= 2500 * 1000 * 1000ULL)
			size = 24;
		else
			size = 128;
		size = size * getifmtu(pa->ifname);
		pa->tbrsize = size;
	}
	return (errors);
}

/*
 * check_commit_altq does consistency check for each interface
 */
int
check_commit_altq(int dev, int opts)
{
	struct pfctl_altq	*if_ppa;
	int			 error = 0;

	/* call the discipline check for each interface. */
	STAILQ_FOREACH(if_ppa, &interfaces, meta.link) {
		switch (if_ppa->pa.scheduler) {
		case ALTQT_CBQ:
			error = check_commit_cbq(dev, opts, if_ppa);
			break;
		case ALTQT_PRIQ:
			error = check_commit_priq(dev, opts, if_ppa);
			break;
		case ALTQT_HFSC:
			error = check_commit_hfsc(dev, opts, if_ppa);
			break;
		case ALTQT_FAIRQ:
			error = check_commit_fairq(dev, opts, if_ppa);
			break;
		default:
			break;
		}
	}
	return (error);
}

/*
 * eval_pfqueue computes the queue parameters.
 */
int
eval_pfqueue(struct pfctl *pf, struct pf_altq *pa, struct node_queue_bw *bw,
    struct node_queue_opt *opts)
{
	/* should be merged with expand_queue */
	struct pfctl_altq	*if_ppa, *parent;
	int		 	 error = 0;

	/* find the corresponding interface and copy fields used by queues */
	if ((if_ppa = pfaltq_lookup(pa->ifname)) == NULL) {
		fprintf(stderr, "altq not defined on %s\n", pa->ifname);
		return (1);
	}
	pa->scheduler = if_ppa->pa.scheduler;
	pa->ifbandwidth = if_ppa->pa.ifbandwidth;

	if (qname_to_pfaltq(pa->qname, pa->ifname) != NULL) {
		fprintf(stderr, "queue %s already exists on interface %s\n",
		    pa->qname, pa->ifname);
		return (1);
	}
	pa->qid = qname_to_qid(pa->qname);

	parent = NULL;
	if (pa->parent[0] != 0) {
		parent = qname_to_pfaltq(pa->parent, pa->ifname);
		if (parent == NULL) {
			fprintf(stderr, "parent %s not found for %s\n",
			    pa->parent, pa->qname);
			return (1);
		}
		pa->parent_qid = parent->pa.qid;
	}
	if (pa->qlimit == 0)
		pa->qlimit = DEFAULT_QLIMIT;

	if (pa->scheduler == ALTQT_CBQ || pa->scheduler == ALTQT_HFSC ||
		pa->scheduler == ALTQT_FAIRQ) {
		pa->bandwidth = eval_bwspec(bw,
		    parent == NULL ? pa->ifbandwidth : parent->pa.bandwidth);

		if (pa->bandwidth > pa->ifbandwidth) {
			fprintf(stderr, "bandwidth for %s higher than "
			    "interface\n", pa->qname);
			return (1);
		}
		/*
		 * If not HFSC, then check that the sum of the child
		 * bandwidths is less than the parent's bandwidth.  For
		 * HFSC, the equivalent concept is to check that the sum of
		 * the child linkshare service curves are under the parent's
		 * linkshare service curve, and that check is performed by
		 * eval_pfqueue_hfsc().
		 */
		if ((parent != NULL) && (pa->scheduler != ALTQT_HFSC)) {
			if (pa->bandwidth > parent->pa.bandwidth) {
				warnx("bandwidth for %s higher than parent",
				    pa->qname);
				return (1);
			}
			parent->meta.bwsum += pa->bandwidth;
			if (parent->meta.bwsum > parent->pa.bandwidth) {
				warnx("the sum of the child bandwidth (%" PRIu64
				    ") higher than parent \"%s\" (%" PRIu64 ")",
				    parent->meta.bwsum, parent->pa.qname,
				    parent->pa.bandwidth);
			}
		}
	}

	if (eval_queue_opts(pa, opts,
		parent == NULL ? pa->ifbandwidth : parent->pa.bandwidth))
		return (1);

	if (parent != NULL)
		parent->meta.children++;
	
	switch (pa->scheduler) {
	case ALTQT_CBQ:
		error = eval_pfqueue_cbq(pf, pa, if_ppa);
		break;
	case ALTQT_PRIQ:
		error = eval_pfqueue_priq(pf, pa, if_ppa);
		break;
	case ALTQT_HFSC:
		error = eval_pfqueue_hfsc(pf, pa, if_ppa, parent);
		break;
	case ALTQT_FAIRQ:
		error = eval_pfqueue_fairq(pf, pa, if_ppa, parent);
		break;
	default:
		break;
	}
	return (error);
}

/*
 * CBQ support functions
 */
#define	RM_FILTER_GAIN	5	/* log2 of gain, e.g., 5 => 31/32 */
#define	RM_NS_PER_SEC	(1000000000)

static int
eval_pfqueue_cbq(struct pfctl *pf, struct pf_altq *pa, struct pfctl_altq *if_ppa)
{
	struct cbq_opts	*opts;
	u_int		 ifmtu;

	if (pa->priority >= CBQ_MAXPRI) {
		warnx("priority out of range: max %d", CBQ_MAXPRI - 1);
		return (-1);
	}

	ifmtu = getifmtu(pa->ifname);
	opts = &pa->pq_u.cbq_opts;

	if (opts->pktsize == 0) {	/* use default */
		opts->pktsize = ifmtu;
		if (opts->pktsize > MCLBYTES)	/* do what TCP does */
			opts->pktsize &= ~MCLBYTES;
	} else if (opts->pktsize > ifmtu)
		opts->pktsize = ifmtu;
	if (opts->maxpktsize == 0)	/* use default */
		opts->maxpktsize = ifmtu;
	else if (opts->maxpktsize > ifmtu)
		opts->pktsize = ifmtu;

	if (opts->pktsize > opts->maxpktsize)
		opts->pktsize = opts->maxpktsize;

	if (pa->parent[0] == 0)
		opts->flags |= (CBQCLF_ROOTCLASS | CBQCLF_WRR);

	if (pa->pq_u.cbq_opts.flags & CBQCLF_ROOTCLASS)
		if_ppa->meta.root_classes++;
	if (pa->pq_u.cbq_opts.flags & CBQCLF_DEFCLASS)
		if_ppa->meta.default_classes++;
	
	cbq_compute_idletime(pf, pa);
	return (0);
}

/*
 * compute ns_per_byte, maxidle, minidle, and offtime
 */
static int
cbq_compute_idletime(struct pfctl *pf, struct pf_altq *pa)
{
	struct cbq_opts	*opts;
	double		 maxidle_s, maxidle, minidle;
	double		 offtime, nsPerByte, ifnsPerByte, ptime, cptime;
	double		 z, g, f, gton, gtom;
	u_int		 minburst, maxburst;

	opts = &pa->pq_u.cbq_opts;
	ifnsPerByte = (1.0 / (double)pa->ifbandwidth) * RM_NS_PER_SEC * 8;
	minburst = opts->minburst;
	maxburst = opts->maxburst;

	if (pa->bandwidth == 0)
		f = 0.0001;	/* small enough? */
	else
		f = ((double) pa->bandwidth / (double) pa->ifbandwidth);

	nsPerByte = ifnsPerByte / f;
	ptime = (double)opts->pktsize * ifnsPerByte;
	cptime = ptime * (1.0 - f) / f;

	if (nsPerByte * (double)opts->maxpktsize > (double)INT_MAX) {
		/*
		 * this causes integer overflow in kernel!
		 * (bandwidth < 6Kbps when max_pkt_size=1500)
		 */
		if (pa->bandwidth != 0 && (pf->opts & PF_OPT_QUIET) == 0) {
			warnx("queue bandwidth must be larger than %s",
			    rate2str(ifnsPerByte * (double)opts->maxpktsize /
			    (double)INT_MAX * (double)pa->ifbandwidth));
			fprintf(stderr, "cbq: queue %s is too slow!\n",
			    pa->qname);
		}
		nsPerByte = (double)(INT_MAX / opts->maxpktsize);
	}

	if (maxburst == 0) {  /* use default */
		if (cptime > 10.0 * 1000000)
			maxburst = 4;
		else
			maxburst = 16;
	}
	if (minburst == 0)  /* use default */
		minburst = 2;
	if (minburst > maxburst)
		minburst = maxburst;

	z = (double)(1 << RM_FILTER_GAIN);
	g = (1.0 - 1.0 / z);
	gton = pow(g, (double)maxburst);
	gtom = pow(g, (double)(minburst-1));
	maxidle = ((1.0 / f - 1.0) * ((1.0 - gton) / gton));
	maxidle_s = (1.0 - g);
	if (maxidle > maxidle_s)
		maxidle = ptime * maxidle;
	else
		maxidle = ptime * maxidle_s;
	offtime = cptime * (1.0 + 1.0/(1.0 - g) * (1.0 - gtom) / gtom);
	minidle = -((double)opts->maxpktsize * (double)nsPerByte);

	/* scale parameters */
	maxidle = ((maxidle * 8.0) / nsPerByte) *
	    pow(2.0, (double)RM_FILTER_GAIN);
	offtime = (offtime * 8.0) / nsPerByte *
	    pow(2.0, (double)RM_FILTER_GAIN);
	minidle = ((minidle * 8.0) / nsPerByte) *
	    pow(2.0, (double)RM_FILTER_GAIN);

	maxidle = maxidle / 1000.0;
	offtime = offtime / 1000.0;
	minidle = minidle / 1000.0;

	opts->minburst = minburst;
	opts->maxburst = maxburst;
	opts->ns_per_byte = (u_int)nsPerByte;
	opts->maxidle = (u_int)fabs(maxidle);
	opts->minidle = (int)minidle;
	opts->offtime = (u_int)fabs(offtime);

	return (0);
}

static int
check_commit_cbq(int dev, int opts, struct pfctl_altq *if_ppa)
{
	int	error = 0;

	/*
	 * check if cbq has one root queue and one default queue
	 * for this interface
	 */
	if (if_ppa->meta.root_classes != 1) {
		warnx("should have one root queue on %s", if_ppa->pa.ifname);
		error++;
	}
	if (if_ppa->meta.default_classes != 1) {
		warnx("should have one default queue on %s", if_ppa->pa.ifname);
		error++;
	}
	return (error);
}

static int
print_cbq_opts(const struct pf_altq *a)
{
	const struct cbq_opts	*opts;

	opts = &a->pq_u.cbq_opts;
	if (opts->flags) {
		printf("cbq(");
		if (opts->flags & CBQCLF_RED)
			printf(" red");
		if (opts->flags & CBQCLF_ECN)
			printf(" ecn");
		if (opts->flags & CBQCLF_RIO)
			printf(" rio");
		if (opts->flags & CBQCLF_CODEL)
			printf(" codel");
		if (opts->flags & CBQCLF_CLEARDSCP)
			printf(" cleardscp");
		if (opts->flags & CBQCLF_FLOWVALVE)
			printf(" flowvalve");
		if (opts->flags & CBQCLF_BORROW)
			printf(" borrow");
		if (opts->flags & CBQCLF_WRR)
			printf(" wrr");
		if (opts->flags & CBQCLF_EFFICIENT)
			printf(" efficient");
		if (opts->flags & CBQCLF_ROOTCLASS)
			printf(" root");
		if (opts->flags & CBQCLF_DEFCLASS)
			printf(" default");
		printf(" ) ");

		return (1);
	} else
		return (0);
}

/*
 * PRIQ support functions
 */
static int
eval_pfqueue_priq(struct pfctl *pf, struct pf_altq *pa, struct pfctl_altq *if_ppa)
{

	if (pa->priority >= PRIQ_MAXPRI) {
		warnx("priority out of range: max %d", PRIQ_MAXPRI - 1);
		return (-1);
	}
	if (BIT_ISSET(QPRI_BITSET_SIZE, pa->priority, &if_ppa->meta.qpris)) {
		warnx("%s does not have a unique priority on interface %s",
		    pa->qname, pa->ifname);
		return (-1);
	} else
		BIT_SET(QPRI_BITSET_SIZE, pa->priority, &if_ppa->meta.qpris);

	if (pa->pq_u.priq_opts.flags & PRCF_DEFAULTCLASS)
		if_ppa->meta.default_classes++;
	return (0);
}

static int
check_commit_priq(int dev, int opts, struct pfctl_altq *if_ppa)
{

	/*
	 * check if priq has one default class for this interface
	 */
	if (if_ppa->meta.default_classes != 1) {
		warnx("should have one default queue on %s", if_ppa->pa.ifname);
		return (1);
	}
	return (0);
}

static int
print_priq_opts(const struct pf_altq *a)
{
	const struct priq_opts	*opts;

	opts = &a->pq_u.priq_opts;

	if (opts->flags) {
		printf("priq(");
		if (opts->flags & PRCF_RED)
			printf(" red");
		if (opts->flags & PRCF_ECN)
			printf(" ecn");
		if (opts->flags & PRCF_RIO)
			printf(" rio");
		if (opts->flags & PRCF_CODEL)
			printf(" codel");
		if (opts->flags & PRCF_CLEARDSCP)
			printf(" cleardscp");
		if (opts->flags & PRCF_DEFAULTCLASS)
			printf(" default");
		printf(" ) ");

		return (1);
	} else
		return (0);
}

/*
 * HFSC support functions
 */
static int
eval_pfqueue_hfsc(struct pfctl *pf, struct pf_altq *pa, struct pfctl_altq *if_ppa,
    struct pfctl_altq *parent)
{
	struct hfsc_opts_v1	*opts;
	struct service_curve	 sc;

	opts = &pa->pq_u.hfsc_opts;

	if (parent == NULL) {
		/* root queue */
		opts->lssc_m1 = pa->ifbandwidth;
		opts->lssc_m2 = pa->ifbandwidth;
		opts->lssc_d = 0;
		return (0);
	}

	/* First child initializes the parent's service curve accumulators. */
	if (parent->meta.children == 1) {
		LIST_INIT(&parent->meta.rtsc);
		LIST_INIT(&parent->meta.lssc);
	}

	if (parent->pa.pq_u.hfsc_opts.flags & HFCF_DEFAULTCLASS) {
		warnx("adding %s would make default queue %s not a leaf",
		    pa->qname, pa->parent);
		return (-1);
	}

	if (pa->pq_u.hfsc_opts.flags & HFCF_DEFAULTCLASS)
		if_ppa->meta.default_classes++;
	
	/* if link_share is not specified, use bandwidth */
	if (opts->lssc_m2 == 0)
		opts->lssc_m2 = pa->bandwidth;

	if ((opts->rtsc_m1 > 0 && opts->rtsc_m2 == 0) ||
	    (opts->lssc_m1 > 0 && opts->lssc_m2 == 0) ||
	    (opts->ulsc_m1 > 0 && opts->ulsc_m2 == 0)) {
		warnx("m2 is zero for %s", pa->qname);
		return (-1);
	}

	if ((opts->rtsc_m1 < opts->rtsc_m2 && opts->rtsc_m1 != 0) ||
	    (opts->lssc_m1 < opts->lssc_m2 && opts->lssc_m1 != 0) ||
	    (opts->ulsc_m1 < opts->ulsc_m2 && opts->ulsc_m1 != 0)) {
		warnx("m1 must be zero for convex curve: %s", pa->qname);
		return (-1);
	}

	/*
	 * admission control:
	 * for the real-time service curve, the sum of the service curves
	 * should not exceed 80% of the interface bandwidth.  20% is reserved
	 * not to over-commit the actual interface bandwidth.
	 * for the linkshare service curve, the sum of the child service
	 * curve should not exceed the parent service curve.
	 * for the upper-limit service curve, the assigned bandwidth should
	 * be smaller than the interface bandwidth, and the upper-limit should
	 * be larger than the real-time service curve when both are defined.
	 */
	
	/* check the real-time service curve.  reserve 20% of interface bw */
	if (opts->rtsc_m2 != 0) {
		/* add this queue to the sum */
		sc.m1 = opts->rtsc_m1;
		sc.d = opts->rtsc_d;
		sc.m2 = opts->rtsc_m2;
		gsc_add_sc(&parent->meta.rtsc, &sc);
		/* compare the sum with 80% of the interface */
		sc.m1 = 0;
		sc.d = 0;
		sc.m2 = pa->ifbandwidth / 100 * 80;
		if (!is_gsc_under_sc(&parent->meta.rtsc, &sc)) {
			warnx("real-time sc exceeds 80%% of the interface "
			    "bandwidth (%s)", rate2str((double)sc.m2));
			return (-1);
		}
	}

	/* check the linkshare service curve. */
	if (opts->lssc_m2 != 0) {
		/* add this queue to the child sum */
		sc.m1 = opts->lssc_m1;
		sc.d = opts->lssc_d;
		sc.m2 = opts->lssc_m2;
		gsc_add_sc(&parent->meta.lssc, &sc);
		/* compare the sum of the children with parent's sc */
		sc.m1 = parent->pa.pq_u.hfsc_opts.lssc_m1;
		sc.d = parent->pa.pq_u.hfsc_opts.lssc_d;
		sc.m2 = parent->pa.pq_u.hfsc_opts.lssc_m2;
		if (!is_gsc_under_sc(&parent->meta.lssc, &sc)) {
			warnx("linkshare sc exceeds parent's sc");
			return (-1);
		}
	}

	/* check the upper-limit service curve. */
	if (opts->ulsc_m2 != 0) {
		if (opts->ulsc_m1 > pa->ifbandwidth ||
		    opts->ulsc_m2 > pa->ifbandwidth) {
			warnx("upper-limit larger than interface bandwidth");
			return (-1);
		}
		if (opts->rtsc_m2 != 0 && opts->rtsc_m2 > opts->ulsc_m2) {
			warnx("upper-limit sc smaller than real-time sc");
			return (-1);
		}
	}

	return (0);
}

/*
 * FAIRQ support functions
 */
static int
eval_pfqueue_fairq(struct pfctl *pf __unused, struct pf_altq *pa,
    struct pfctl_altq *if_ppa, struct pfctl_altq *parent)
{
	struct fairq_opts	*opts;
	struct service_curve	 sc;

	opts = &pa->pq_u.fairq_opts;

	if (pa->parent == NULL) {
		/* root queue */
		opts->lssc_m1 = pa->ifbandwidth;
		opts->lssc_m2 = pa->ifbandwidth;
		opts->lssc_d = 0;
		return (0);
	}

	/* First child initializes the parent's service curve accumulator. */
	if (parent->meta.children == 1)
		LIST_INIT(&parent->meta.lssc);

	if (parent->pa.pq_u.fairq_opts.flags & FARF_DEFAULTCLASS) {
		warnx("adding %s would make default queue %s not a leaf",
		    pa->qname, pa->parent);
		return (-1);
	}

	if (pa->pq_u.fairq_opts.flags & FARF_DEFAULTCLASS)
		if_ppa->meta.default_classes++;

	/* if link_share is not specified, use bandwidth */
	if (opts->lssc_m2 == 0)
		opts->lssc_m2 = pa->bandwidth;

	/*
	 * admission control:
	 * for the real-time service curve, the sum of the service curves
	 * should not exceed 80% of the interface bandwidth.  20% is reserved
	 * not to over-commit the actual interface bandwidth.
	 * for the link-sharing service curve, the sum of the child service
	 * curve should not exceed the parent service curve.
	 * for the upper-limit service curve, the assigned bandwidth should
	 * be smaller than the interface bandwidth, and the upper-limit should
	 * be larger than the real-time service curve when both are defined.
	 */

	/* check the linkshare service curve. */
	if (opts->lssc_m2 != 0) {
		/* add this queue to the child sum */
		sc.m1 = opts->lssc_m1;
		sc.d = opts->lssc_d;
		sc.m2 = opts->lssc_m2;
		gsc_add_sc(&parent->meta.lssc, &sc);
		/* compare the sum of the children with parent's sc */
		sc.m1 = parent->pa.pq_u.fairq_opts.lssc_m1;
		sc.d = parent->pa.pq_u.fairq_opts.lssc_d;
		sc.m2 = parent->pa.pq_u.fairq_opts.lssc_m2;
		if (!is_gsc_under_sc(&parent->meta.lssc, &sc)) {
			warnx("link-sharing sc exceeds parent's sc");
			return (-1);
		}
	}

	return (0);
}

static int
check_commit_hfsc(int dev, int opts, struct pfctl_altq *if_ppa)
{

	/* check if hfsc has one default queue for this interface */
	if (if_ppa->meta.default_classes != 1) {
		warnx("should have one default queue on %s", if_ppa->pa.ifname);
		return (1);
	}
	return (0);
}

static int
check_commit_fairq(int dev __unused, int opts __unused, struct pfctl_altq *if_ppa)
{

	/* check if fairq has one default queue for this interface */
	if (if_ppa->meta.default_classes != 1) {
		warnx("should have one default queue on %s", if_ppa->pa.ifname);
		return (1);
	}
	return (0);
}

static int
print_hfsc_opts(const struct pf_altq *a, const struct node_queue_opt *qopts)
{
	const struct hfsc_opts_v1	*opts;
	const struct node_hfsc_sc	*rtsc, *lssc, *ulsc;

	opts = &a->pq_u.hfsc_opts;
	if (qopts == NULL)
		rtsc = lssc = ulsc = NULL;
	else {
		rtsc = &qopts->data.hfsc_opts.realtime;
		lssc = &qopts->data.hfsc_opts.linkshare;
		ulsc = &qopts->data.hfsc_opts.upperlimit;
	}

	if (opts->flags || opts->rtsc_m2 != 0 || opts->ulsc_m2 != 0 ||
	    (opts->lssc_m2 != 0 && (opts->lssc_m2 != a->bandwidth ||
	    opts->lssc_d != 0))) {
		printf("hfsc(");
		if (opts->flags & HFCF_RED)
			printf(" red");
		if (opts->flags & HFCF_ECN)
			printf(" ecn");
		if (opts->flags & HFCF_RIO)
			printf(" rio");
		if (opts->flags & HFCF_CODEL)
			printf(" codel");
		if (opts->flags & HFCF_CLEARDSCP)
			printf(" cleardscp");
		if (opts->flags & HFCF_DEFAULTCLASS)
			printf(" default");
		if (opts->rtsc_m2 != 0)
			print_hfsc_sc("realtime", opts->rtsc_m1, opts->rtsc_d,
			    opts->rtsc_m2, rtsc);
		if (opts->lssc_m2 != 0 && (opts->lssc_m2 != a->bandwidth ||
		    opts->lssc_d != 0))
			print_hfsc_sc("linkshare", opts->lssc_m1, opts->lssc_d,
			    opts->lssc_m2, lssc);
		if (opts->ulsc_m2 != 0)
			print_hfsc_sc("upperlimit", opts->ulsc_m1, opts->ulsc_d,
			    opts->ulsc_m2, ulsc);
		printf(" ) ");

		return (1);
	} else
		return (0);
}

static int
print_codel_opts(const struct pf_altq *a, const struct node_queue_opt *qopts)
{
	const struct codel_opts *opts;

	opts = &a->pq_u.codel_opts;
	if (opts->target || opts->interval || opts->ecn) {
		printf("codel(");
		if (opts->target)
			printf(" target %d", opts->target);
		if (opts->interval)
			printf(" interval %d", opts->interval);
		if (opts->ecn)
			printf("ecn");
		printf(" ) ");

		return (1);
	}

	return (0);
}

static int
print_fairq_opts(const struct pf_altq *a, const struct node_queue_opt *qopts)
{
	const struct fairq_opts		*opts;
	const struct node_fairq_sc	*loc_lssc;

	opts = &a->pq_u.fairq_opts;
	if (qopts == NULL)
		loc_lssc = NULL;
	else
		loc_lssc = &qopts->data.fairq_opts.linkshare;

	if (opts->flags ||
	    (opts->lssc_m2 != 0 && (opts->lssc_m2 != a->bandwidth ||
	    opts->lssc_d != 0))) {
		printf("fairq(");
		if (opts->flags & FARF_RED)
			printf(" red");
		if (opts->flags & FARF_ECN)
			printf(" ecn");
		if (opts->flags & FARF_RIO)
			printf(" rio");
		if (opts->flags & FARF_CODEL)
			printf(" codel");
		if (opts->flags & FARF_CLEARDSCP)
			printf(" cleardscp");
		if (opts->flags & FARF_DEFAULTCLASS)
			printf(" default");
		if (opts->lssc_m2 != 0 && (opts->lssc_m2 != a->bandwidth ||
		    opts->lssc_d != 0))
			print_fairq_sc("linkshare", opts->lssc_m1, opts->lssc_d,
			    opts->lssc_m2, loc_lssc);
		printf(" ) ");

		return (1);
	} else
		return (0);
}

/*
 * admission control using generalized service curve
 */

/* add a new service curve to a generalized service curve */
static void
gsc_add_sc(struct gen_sc *gsc, struct service_curve *sc)
{
	if (is_sc_null(sc))
		return;
	if (sc->d != 0)
		gsc_add_seg(gsc, 0.0, 0.0, (double)sc->d, (double)sc->m1);
	gsc_add_seg(gsc, (double)sc->d, 0.0, INFINITY, (double)sc->m2);
}

/*
 * check whether all points of a generalized service curve have
 * their y-coordinates no larger than a given two-piece linear
 * service curve.
 */
static int
is_gsc_under_sc(struct gen_sc *gsc, struct service_curve *sc)
{
	struct segment	*s, *last, *end;
	double		 y;

	if (is_sc_null(sc)) {
		if (LIST_EMPTY(gsc))
			return (1);
		LIST_FOREACH(s, gsc, _next) {
			if (s->m != 0)
				return (0);
		}
		return (1);
	}
	/*
	 * gsc has a dummy entry at the end with x = INFINITY.
	 * loop through up to this dummy entry.
	 */
	end = gsc_getentry(gsc, INFINITY);
	if (end == NULL)
		return (1);
	last = NULL;
	for (s = LIST_FIRST(gsc); s != end; s = LIST_NEXT(s, _next)) {
		if (s->y > sc_x2y(sc, s->x))
			return (0);
		last = s;
	}
	/* last now holds the real last segment */
	if (last == NULL)
		return (1);
	if (last->m > sc->m2)
		return (0);
	if (last->x < sc->d && last->m > sc->m1) {
		y = last->y + (sc->d - last->x) * last->m;
		if (y > sc_x2y(sc, sc->d))
			return (0);
	}
	return (1);
}

/*
 * return a segment entry starting at x.
 * if gsc has no entry starting at x, a new entry is created at x.
 */
static struct segment *
gsc_getentry(struct gen_sc *gsc, double x)
{
	struct segment	*new, *prev, *s;

	prev = NULL;
	LIST_FOREACH(s, gsc, _next) {
		if (s->x == x)
			return (s);	/* matching entry found */
		else if (s->x < x)
			prev = s;
		else
			break;
	}

	/* we have to create a new entry */
	if ((new = calloc(1, sizeof(struct segment))) == NULL)
		return (NULL);

	new->x = x;
	if (x == INFINITY || s == NULL)
		new->d = 0;
	else if (s->x == INFINITY)
		new->d = INFINITY;
	else
		new->d = s->x - x;
	if (prev == NULL) {
		/* insert the new entry at the head of the list */
		new->y = 0;
		new->m = 0;
		LIST_INSERT_HEAD(gsc, new, _next);
	} else {
		/*
		 * the start point intersects with the segment pointed by
		 * prev.  divide prev into 2 segments
		 */
		if (x == INFINITY) {
			prev->d = INFINITY;
			if (prev->m == 0)
				new->y = prev->y;
			else
				new->y = INFINITY;
		} else {
			prev->d = x - prev->x;
			new->y = prev->d * prev->m + prev->y;
		}
		new->m = prev->m;
		LIST_INSERT_AFTER(prev, new, _next);
	}
	return (new);
}

/* add a segment to a generalized service curve */
static int
gsc_add_seg(struct gen_sc *gsc, double x, double y, double d, double m)
{
	struct segment	*start, *end, *s;
	double		 x2;

	if (d == INFINITY)
		x2 = INFINITY;
	else
		x2 = x + d;
	start = gsc_getentry(gsc, x);
	end = gsc_getentry(gsc, x2);
	if (start == NULL || end == NULL)
		return (-1);

	for (s = start; s != end; s = LIST_NEXT(s, _next)) {
		s->m += m;
		s->y += y + (s->x - x) * m;
	}

	end = gsc_getentry(gsc, INFINITY);
	for (; s != end; s = LIST_NEXT(s, _next)) {
		s->y += m * d;
	}

	return (0);
}

/* get y-projection of a service curve */
static double
sc_x2y(struct service_curve *sc, double x)
{
	double	y;

	if (x <= (double)sc->d)
		/* y belongs to the 1st segment */
		y = x * (double)sc->m1;
	else
		/* y belongs to the 2nd segment */
		y = (double)sc->d * (double)sc->m1
			+ (x - (double)sc->d) * (double)sc->m2;
	return (y);
}

/*
 * misc utilities
 */
#define	R2S_BUFS	8
#define	RATESTR_MAX	16

char *
rate2str(double rate)
{
	char		*buf;
	static char	 r2sbuf[R2S_BUFS][RATESTR_MAX];  /* ring bufer */
	static int	 idx = 0;
	int		 i;
	static const char unit[] = " KMG";

	buf = r2sbuf[idx++];
	if (idx == R2S_BUFS)
		idx = 0;

	for (i = 0; rate >= 1000 && i <= 3; i++)
		rate /= 1000;

	if ((int)(rate * 100) % 100)
		snprintf(buf, RATESTR_MAX, "%.2f%cb", rate, unit[i]);
	else
		snprintf(buf, RATESTR_MAX, "%d%cb", (int)rate, unit[i]);

	return (buf);
}

#ifdef __FreeBSD__
/*
 * XXX
 * FreeBSD does not have SIOCGIFDATA.
 * To emulate this, DIOCGIFSPEED ioctl added to pf.
 */
u_int64_t
getifspeed(int pfdev, char *ifname)
{
	struct pf_ifspeed io;

	bzero(&io, sizeof io);
	if (strlcpy(io.ifname, ifname, IFNAMSIZ) >=
	    sizeof(io.ifname)) 
		errx(1, "getifspeed: strlcpy");
	if (ioctl(pfdev, DIOCGIFSPEED, &io) == -1)
		err(1, "DIOCGIFSPEED");
	return (io.baudrate);
}
#else
u_int32_t
getifspeed(char *ifname)
{
	int		s;
	struct ifreq	ifr;
	struct if_data	ifrdat;

	s = get_query_socket();
	bzero(&ifr, sizeof(ifr));
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "getifspeed: strlcpy");
	ifr.ifr_data = (caddr_t)&ifrdat;
	if (ioctl(s, SIOCGIFDATA, (caddr_t)&ifr) == -1)
		err(1, "SIOCGIFDATA");
	return ((u_int32_t)ifrdat.ifi_baudrate);
}
#endif

u_long
getifmtu(char *ifname)
{
	int		s;
	struct ifreq	ifr;

	s = get_query_socket();
	bzero(&ifr, sizeof(ifr));
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "getifmtu: strlcpy");
	if (ioctl(s, SIOCGIFMTU, (caddr_t)&ifr) == -1)
#ifdef __FreeBSD__
		ifr.ifr_mtu = 1500;
#else
		err(1, "SIOCGIFMTU");
#endif
	if (ifr.ifr_mtu > 0)
		return (ifr.ifr_mtu);
	else {
		warnx("could not get mtu for %s, assuming 1500", ifname);
		return (1500);
	}
}

int
eval_queue_opts(struct pf_altq *pa, struct node_queue_opt *opts,
    u_int64_t ref_bw)
{
	int	errors = 0;

	switch (pa->scheduler) {
	case ALTQT_CBQ:
		pa->pq_u.cbq_opts = opts->data.cbq_opts;
		break;
	case ALTQT_PRIQ:
		pa->pq_u.priq_opts = opts->data.priq_opts;
		break;
	case ALTQT_HFSC:
		pa->pq_u.hfsc_opts.flags = opts->data.hfsc_opts.flags;
		if (opts->data.hfsc_opts.linkshare.used) {
			pa->pq_u.hfsc_opts.lssc_m1 =
			    eval_bwspec(&opts->data.hfsc_opts.linkshare.m1,
			    ref_bw);
			pa->pq_u.hfsc_opts.lssc_m2 =
			    eval_bwspec(&opts->data.hfsc_opts.linkshare.m2,
			    ref_bw);
			pa->pq_u.hfsc_opts.lssc_d =
			    opts->data.hfsc_opts.linkshare.d;
		}
		if (opts->data.hfsc_opts.realtime.used) {
			pa->pq_u.hfsc_opts.rtsc_m1 =
			    eval_bwspec(&opts->data.hfsc_opts.realtime.m1,
			    ref_bw);
			pa->pq_u.hfsc_opts.rtsc_m2 =
			    eval_bwspec(&opts->data.hfsc_opts.realtime.m2,
			    ref_bw);
			pa->pq_u.hfsc_opts.rtsc_d =
			    opts->data.hfsc_opts.realtime.d;
		}
		if (opts->data.hfsc_opts.upperlimit.used) {
			pa->pq_u.hfsc_opts.ulsc_m1 =
			    eval_bwspec(&opts->data.hfsc_opts.upperlimit.m1,
			    ref_bw);
			pa->pq_u.hfsc_opts.ulsc_m2 =
			    eval_bwspec(&opts->data.hfsc_opts.upperlimit.m2,
			    ref_bw);
			pa->pq_u.hfsc_opts.ulsc_d =
			    opts->data.hfsc_opts.upperlimit.d;
		}
		break;
	case ALTQT_FAIRQ:
		pa->pq_u.fairq_opts.flags = opts->data.fairq_opts.flags;
		pa->pq_u.fairq_opts.nbuckets = opts->data.fairq_opts.nbuckets;
		pa->pq_u.fairq_opts.hogs_m1 =
			eval_bwspec(&opts->data.fairq_opts.hogs_bw, ref_bw);

		if (opts->data.fairq_opts.linkshare.used) {
			pa->pq_u.fairq_opts.lssc_m1 =
			    eval_bwspec(&opts->data.fairq_opts.linkshare.m1,
			    ref_bw);
			pa->pq_u.fairq_opts.lssc_m2 =
			    eval_bwspec(&opts->data.fairq_opts.linkshare.m2,
			    ref_bw);
			pa->pq_u.fairq_opts.lssc_d =
			    opts->data.fairq_opts.linkshare.d;
		}
		break;
	case ALTQT_CODEL:
		pa->pq_u.codel_opts.target = opts->data.codel_opts.target;
		pa->pq_u.codel_opts.interval = opts->data.codel_opts.interval;
		pa->pq_u.codel_opts.ecn = opts->data.codel_opts.ecn;
		break;
	default:
		warnx("eval_queue_opts: unknown scheduler type %u",
		    opts->qtype);
		errors++;
		break;
	}

	return (errors);
}

/*
 * If absolute bandwidth if set, return the lesser of that value and the
 * reference bandwidth.  Limiting to the reference bandwidth allows simple
 * limiting of configured bandwidth parameters for schedulers that are
 * 32-bit limited, as the root/interface bandwidth (top-level reference
 * bandwidth) will be properly limited in that case.
 *
 * Otherwise, if the absolute bandwidth is not set, return given percentage
 * of reference bandwidth.
 */
u_int64_t
eval_bwspec(struct node_queue_bw *bw, u_int64_t ref_bw)
{
	if (bw->bw_absolute > 0)
		return (MIN(bw->bw_absolute, ref_bw));

	if (bw->bw_percent > 0)
		return (ref_bw / 100 * bw->bw_percent);

	return (0);
}

void
print_hfsc_sc(const char *scname, u_int m1, u_int d, u_int m2,
    const struct node_hfsc_sc *sc)
{
	printf(" %s", scname);

	if (d != 0) {
		printf("(");
		if (sc != NULL && sc->m1.bw_percent > 0)
			printf("%u%%", sc->m1.bw_percent);
		else
			printf("%s", rate2str((double)m1));
		printf(" %u", d);
	}

	if (sc != NULL && sc->m2.bw_percent > 0)
		printf(" %u%%", sc->m2.bw_percent);
	else
		printf(" %s", rate2str((double)m2));

	if (d != 0)
		printf(")");
}

void
print_fairq_sc(const char *scname, u_int m1, u_int d, u_int m2,
    const struct node_fairq_sc *sc)
{
	printf(" %s", scname);

	if (d != 0) {
		printf("(");
		if (sc != NULL && sc->m1.bw_percent > 0)
			printf("%u%%", sc->m1.bw_percent);
		else
			printf("%s", rate2str((double)m1));
		printf(" %u", d);
	}

	if (sc != NULL && sc->m2.bw_percent > 0)
		printf(" %u%%", sc->m2.bw_percent);
	else
		printf(" %s", rate2str((double)m2));

	if (d != 0)
		printf(")");
}
