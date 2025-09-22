/*	$OpenBSD: rde_decide_test.c,v 1.16 2023/04/19 13:25:07 claudio Exp $ */

/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rde.h"

struct rde_memstats rdemem;

struct rib dummy_rib = {
	.name = "regress RIB",
	.flags = 0,
};

struct rib flowrib;
struct pt_entry dummy_pt;
struct rib_entry dummy_re = { .prefix = &dummy_pt };

struct rde_peer peer1 = {
	.conf.ebgp = 1,
	.remote_bgpid = 1,
	.remote_addr = { .aid = AID_INET, .v4.s_addr = 0xef000001 },
};
struct rde_peer peer2 = {
	.conf.ebgp = 1,
	.remote_bgpid = 2,
	.remote_addr = { .aid = AID_INET, .v4.s_addr = 0xef000002 },
};
struct rde_peer peer3 = {
	.conf.ebgp = 1,
	.remote_bgpid = 3,
	.remote_addr = { .aid = AID_INET, .v4.s_addr = 0xef000003 },
};
struct rde_peer peer1_a4 = {
	.conf.ebgp = 1,
	.remote_bgpid = 1,
	.remote_addr = { .aid = AID_INET, .v4.s_addr = 0xef000004 },
};
struct rde_peer peer1_i = {
	.conf.ebgp = 0,
	.remote_bgpid = 3,
	.remote_addr = { .aid = AID_INET, .v4.s_addr = 0xef000003 },
};

union a {
	struct aspath	a;
	struct {
		uint32_t source_as;
		uint16_t len;
		uint16_t ascnt;
		uint8_t d[6];
	} x;
} asdata[] = {
	{ .x = { .len = 6, .ascnt = 2, .d = { 2, 1, 0, 0, 0, 1 } } },
	{ .x = { .len = 6, .ascnt = 3, .d = { 2, 1, 0, 0, 0, 1 } } },
	{ .x = { .len = 6, .ascnt = 2, .d = { 2, 1, 0, 0, 0, 2 } } },
	{ .x = { .len = 6, .ascnt = 3, .d = { 2, 1, 0, 0, 0, 2 } } },
};

struct rde_aspath asp[] = {
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_IGP, .weight = 1000 },
	/* 1 & 2: errors and loops */
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_IGP, .flags=F_ATTR_PARSE_ERR },
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_IGP, .flags=F_ATTR_LOOP },
	/* 3: local preference */
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 50, .origin = ORIGIN_IGP },
	/* 4: aspath count */
	{ .aspath = &asdata[1].a, .med = 100, .lpref = 100, .origin = ORIGIN_IGP },
	/* 5 & 6: origin */
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_EGP },
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_INCOMPLETE },
	/* 7: MED */
	{ .aspath = &asdata[0].a, .med = 200, .lpref = 100, .origin = ORIGIN_IGP },
	/* 8: Weight */
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_IGP, .weight = 100 },
};

#define T1	1610980000
#define T2	1610983600

struct test {
	char *what;
	struct prefix p;
} test_pfx[] = {
	{ .what = "test prefix",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* pathes with errors are not eligible */
	{ .what = "prefix with error",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[1], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* only loop free pathes are eligible */
	{ .what = "prefix with loop",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[2], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 1. check if prefix is eligible a.k.a reachable */
	{ .what = "prefix with unreachable nexthop",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer1, .nhflags = 0, .lastchange = T1, } },
	/* 2. local preference of prefix, bigger is better */
	{ .what = "local preference check",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[3], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 3. aspath count, the shorter the better */
	{ .what = "aspath count check",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[4], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 4. origin, the lower the better */
	{ .what = "origin EGP",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[5], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	{ .what = "origin INCOMPLETE",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[6], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 5. MED decision */
	{ .what = "MED",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[7], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 6. EBGP is cooler than IBGP */
	{ .what = "EBGP vs IBGP",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer1_i, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 7. weight */
	{ .what = "local weight",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[8], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 8. nexthop cost not implemented */
	/* 9. route age */
	{ .what = "route age",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T2, } },
	/* 10. BGP Id or ORIGINATOR_ID if present */
	{ .what = "BGP ID",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer2, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
	/* 11. CLUSTER_LIST length, TODO */
	/* 12. lowest peer address wins */
	{ .what = "remote peer address",
	.p = { .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer1_a4, .nhflags = NEXTHOP_VALID, .lastchange = T1, } },
};

struct rde_aspath med_asp[] = {
	{ .aspath = &asdata[0].a, .med = 100, .lpref = 100, .origin = ORIGIN_EGP },
	{ .aspath = &asdata[0].a, .med = 150, .lpref = 100, .origin = ORIGIN_EGP },
	{ .aspath = &asdata[2].a, .med = 75,  .lpref = 100, .origin = ORIGIN_EGP },
	{ .aspath = &asdata[2].a, .med = 125, .lpref = 100, .origin = ORIGIN_EGP },
	{ .aspath = &asdata[1].a, .med = 110, .lpref = 100, .origin = ORIGIN_EGP },
	{ .aspath = &asdata[1].a, .med = 90,  .lpref = 100, .origin = ORIGIN_EGP },
};

/*
 * Test 'rde med compare strict' vs 'rde med compare always'
 * med_pfx1 > med_pfx2 in both cases
 * med_pfx1 > med_pfx3 for strict but med_pfx1 < med_pfx3 for always
 * med_pfx1 < med_pfx4 for strict but med_pfx1 > med_pfx4 for always
 * For med_pfx3 and med_pfx4 the strict case differs in the bgp-id.
 */
struct prefix med_pfx1 = 
	{ .entry.list.re = &dummy_re, .aspath = &med_asp[0], .peer = &peer2, .nhflags = NEXTHOP_VALID, .lastchange = T1, };
struct prefix med_pfx2 = 
	{ .entry.list.re = &dummy_re, .aspath = &med_asp[1], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, };
struct prefix med_pfx3 = 
	{ .entry.list.re = &dummy_re, .aspath = &med_asp[2], .peer = &peer3, .nhflags = NEXTHOP_VALID, .lastchange = T1, };
struct prefix med_pfx4 = 
	{ .entry.list.re = &dummy_re, .aspath = &med_asp[3], .peer = &peer1_a4, .nhflags = NEXTHOP_VALID, .lastchange = T1, };
/* the next two prefixes have a longer aspath than med_pfx1 & 2 */
struct prefix med_pfx5 = 
	{ .entry.list.re = &dummy_re, .aspath = &med_asp[5], .peer = &peer3, .nhflags = NEXTHOP_VALID, .lastchange = T1, };
struct prefix med_pfx6 = 
	{ .entry.list.re = &dummy_re, .aspath = &med_asp[4], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T1, };

/*
 * Define two prefixes where pfx1 > pfx2 if 'rde route-age evaluate'
 * but pfx1 < pfx2 if 'rde route-age ignore' 
 */
struct prefix age_pfx1 = 
	{ .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer2, .nhflags = NEXTHOP_VALID, .lastchange = T1, };
struct prefix age_pfx2 = 
	{ .entry.list.re = &dummy_re, .aspath = &asp[0], .peer = &peer1, .nhflags = NEXTHOP_VALID, .lastchange = T2, };

int     prefix_cmp(struct prefix *, struct prefix *, int *);

int	decision_flags = BGPD_FLAG_DECISION_ROUTEAGE;

int	failed;

static int
test(struct prefix *a, struct prefix *b, int v)
{
	int rv = 0, dummy;
	if (prefix_cmp(a, b, &dummy) < 0) {
		if (v) printf(" FAILED\n");
		failed = rv = 1;
	} else if (prefix_cmp(b, a, &dummy) > 0) {
		if (v) printf(" reverse cmp FAILED\n");
		failed = rv = 1;
	} else if (v)
		printf(" OK\n");

	return rv;
}

static size_t
which(struct prefix **orig, struct prefix *p)
{
	size_t i;

	for (i = 0; orig[i] != NULL; i++)
		if (orig[i] == p)
			return i;
	return 9999;
}

/*
 * Evaluate a set of prefixes in all possible ways.
 * The input in orig should be in the expected order.
 */
static int
test_evaluate(struct prefix **orig, struct prefix **in, size_t nin)
{
	struct prefix *next[nin - 1];
	size_t i, j, n;
	int r = 0;
	
	if (nin == 0) {
		struct prefix *xp;

		j = 0;
		TAILQ_FOREACH(xp, &dummy_re.prefix_h, entry.list.rib)
			if (which(orig, xp) != j++)
				r = 1;
		if (r != 0) {
			printf("bad order");
			TAILQ_FOREACH(xp, &dummy_re.prefix_h, entry.list.rib)
				printf(" %zu", which(orig, xp));
			printf(" FAILED\n");
		}
	}
	for (i = 0; i < nin; i++) {
		/* add prefix to dummy_re */
		prefix_evaluate(&dummy_re, in[i], NULL);

		for (n = j = 0; j < nin; j++) {
			if (j == i)
				continue;
			next[n++] = in[j];
		}
		r |= test_evaluate(orig, next, n);

		/* remove prefix from dummy_re */
		prefix_evaluate(&dummy_re, NULL, in[i]);
	}

	return r;
}

int
main(int argc, char **argv)
{
	struct prefix *med_strict[7] = {
		&med_pfx1, &med_pfx2, &med_pfx3, &med_pfx4,
		&med_pfx5, &med_pfx6, NULL
	};
	struct prefix *med_always[7] = {
		&med_pfx3, &med_pfx1, &med_pfx4, &med_pfx2,
		&med_pfx5, &med_pfx6, NULL
	};
	size_t i, ntest;

	ntest = sizeof(test_pfx) / sizeof(*test_pfx);
	for (i = 1; i < ntest; i++) {
		printf("test %zu: %s", i, test_pfx[i].what);
		test(&test_pfx[0].p, &test_pfx[i].p, 1);
	}

	printf("test NULL element");
	test(&test_pfx[0].p, NULL, 1);

	printf("test rde med compare strict 1");
	test(&med_pfx1, &med_pfx2, 1);
	printf("test rde med compare strict 2");
	test(&med_pfx1, &med_pfx3, 1);
	printf("test rde med compare strict 3");
	test(&med_pfx4, &med_pfx1, 1);

	decision_flags |= BGPD_FLAG_DECISION_MED_ALWAYS;
	printf("test rde med compare always 1");
	test(&med_pfx1, &med_pfx2, 1);
	printf("test rde med compare always 2");
	test(&med_pfx3, &med_pfx1, 1);
	printf("test rde med compare always 3");
	test(&med_pfx1, &med_pfx4, 1);

	printf("test rde route-age evaluate");
	test(&age_pfx1, &age_pfx2, 1);
	decision_flags &= ~BGPD_FLAG_DECISION_ROUTEAGE;
	printf("test rde route-age ignore");
	test(&age_pfx2, &age_pfx1, 1);

	decision_flags = 0;
	printf("evaluate with rde med compare strict\n");
	if (test_evaluate(med_strict, med_strict, 6) == 0)
		printf("all OK\n");

	decision_flags = BGPD_FLAG_DECISION_MED_ALWAYS;
	printf("evaluate with rde med compare always\n");
	if (test_evaluate(med_always, med_always, 6) == 0)
		printf("all OK\n");

	if (failed)
		printf("some tests FAILED\n");
	else
		printf("all tests OK\n");
	exit(failed);
}

/* this function is called by prefix_cmp to alter the decision process */
int
rde_decisionflags(void)
{
	return decision_flags;
}

/*
 * Helper functions need to link and run the tests.
 */
uint32_t
rde_local_as(void)
{
	return 65000;
}

int
rde_evaluate_all(void)
{
        return 0;
}

int
as_set_match(const struct as_set *aset, uint32_t asnum)
{
	errx(1, __func__);
}

struct rib *
rib_byid(uint16_t id)
{
	return &dummy_rib;
}

void
rde_generate_updates(struct rib_entry *re, struct prefix *newpath,
    struct prefix *oldpath, enum eval_mode mode)
{
	/* maybe we want to do something here */
}

void
rde_send_kroute(struct rib *rib, struct prefix *new, struct prefix *old)
{
	/* nothing */
}

__dead void
fatalx(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verrx(2, emsg, ap);
}

__dead void
fatal(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verr(2, emsg, ap);
}

void
log_warnx(const char *emsg, ...)
{
	va_list  ap;
	va_start(ap, emsg);
	vwarnx(emsg, ap);
	va_end(ap);
}

void
log_debug(const char *emsg, ...)
{
	va_list  ap;
	va_start(ap, emsg);
	vwarnx(emsg, ap);
	va_end(ap);
}

void
pt_getaddr(struct pt_entry *pte, struct bgpd_addr *addr)
{
}
