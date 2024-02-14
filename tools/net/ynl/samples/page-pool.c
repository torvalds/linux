// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <net/if.h>

#include "netdev-user.h"

struct stat {
	unsigned int ifc;

	struct {
		unsigned int cnt;
		size_t refs, bytes;
	} live[2];

	size_t alloc_slow, alloc_fast, recycle_ring, recycle_cache;
};

struct stats_array {
	unsigned int i, max;
	struct stat *s;
};

static struct stat *find_ifc(struct stats_array *a, unsigned int ifindex)
{
	unsigned int i;

	for (i = 0; i < a->i; i++) {
		if (a->s[i].ifc == ifindex)
			return &a->s[i];
	}

	a->i++;
	if (a->i == a->max) {
		a->max *= 2;
		a->s = reallocarray(a->s, a->max, sizeof(*a->s));
	}
	a->s[i].ifc = ifindex;
	return &a->s[i];
}

static void count(struct stat *s, unsigned int l,
		  struct netdev_page_pool_get_rsp *pp)
{
	s->live[l].cnt++;
	if (pp->_present.inflight)
		s->live[l].refs += pp->inflight;
	if (pp->_present.inflight_mem)
		s->live[l].bytes += pp->inflight_mem;
}

int main(int argc, char **argv)
{
	struct netdev_page_pool_stats_get_list *pp_stats;
	struct netdev_page_pool_get_list *pools;
	struct stats_array a = {};
	struct ynl_error yerr;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	a.max = 128;
	a.s = calloc(a.max, sizeof(*a.s));
	if (!a.s)
		goto err_close;

	pools = netdev_page_pool_get_dump(ys);
	if (!pools)
		goto err_free;

	ynl_dump_foreach(pools, pp) {
		struct stat *s = find_ifc(&a, pp->ifindex);

		count(s, 1, pp);
		if (pp->_present.detach_time)
			count(s, 0, pp);
	}
	netdev_page_pool_get_list_free(pools);

	pp_stats = netdev_page_pool_stats_get_dump(ys);
	if (!pp_stats)
		goto err_free;

	ynl_dump_foreach(pp_stats, pp) {
		struct stat *s = find_ifc(&a, pp->info.ifindex);

		if (pp->_present.alloc_fast)
			s->alloc_fast += pp->alloc_fast;
		if (pp->_present.alloc_slow)
			s->alloc_slow += pp->alloc_slow;
		if (pp->_present.recycle_ring)
			s->recycle_ring += pp->recycle_ring;
		if (pp->_present.recycle_cached)
			s->recycle_cache += pp->recycle_cached;
	}
	netdev_page_pool_stats_get_list_free(pp_stats);

	for (unsigned int i = 0; i < a.i; i++) {
		char ifname[IF_NAMESIZE];
		struct stat *s = &a.s[i];
		const char *name;
		double recycle;

		if (!s->ifc) {
			name = "<orphan>\t";
		} else {
			name = if_indextoname(s->ifc, ifname);
			if (name)
				printf("%8s", name);
			printf("[%d]\t", s->ifc);
		}

		printf("page pools: %u (zombies: %u)\n",
		       s->live[1].cnt, s->live[0].cnt);
		printf("\t\trefs: %zu bytes: %zu (refs: %zu bytes: %zu)\n",
		       s->live[1].refs, s->live[1].bytes,
		       s->live[0].refs, s->live[0].bytes);

		/* We don't know how many pages are sitting in cache and ring
		 * so we will under-count the recycling rate a bit.
		 */
		recycle = (double)(s->recycle_ring + s->recycle_cache) /
			(s->alloc_fast + s->alloc_slow) * 100;
		printf("\t\trecycling: %.1lf%% (alloc: %zu:%zu recycle: %zu:%zu)\n",
		       recycle, s->alloc_slow, s->alloc_fast,
		       s->recycle_ring, s->recycle_cache);
	}

	ynl_sock_destroy(ys);
	return 0;

err_free:
	free(a.s);
err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
	ynl_sock_destroy(ys);
	return 2;
}
