// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>

#include <ynl.h>
#include "netdev-user.h"

#include "main.h"

struct pp_stat {
	unsigned int ifc;

	struct {
		unsigned int cnt;
		size_t refs, bytes;
	} live[2];

	size_t alloc_slow, alloc_fast, recycle_ring, recycle_cache;
};

struct pp_stats_array {
	unsigned int i, max;
	struct pp_stat *s;
};

static struct pp_stat *find_ifc(struct pp_stats_array *a, unsigned int ifindex)
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

static void count_pool(struct pp_stat *s, unsigned int l,
		       struct netdev_page_pool_get_rsp *pp)
{
	s->live[l].cnt++;
	if (pp->_present.inflight)
		s->live[l].refs += pp->inflight;
	if (pp->_present.inflight_mem)
		s->live[l].bytes += pp->inflight_mem;
}

/* We don't know how many pages are sitting in cache and ring
 * so we will under-count the recycling rate a bit.
 */
static void print_json_recycling_stats(struct pp_stat *s)
{
	double recycle;

	if (s->alloc_fast + s->alloc_slow) {
		recycle = (double)(s->recycle_ring + s->recycle_cache) /
			(s->alloc_fast + s->alloc_slow) * 100;
		jsonw_float_field(json_wtr, "recycling_pct", recycle);
	}

	jsonw_name(json_wtr, "alloc");
	jsonw_start_object(json_wtr);
	jsonw_uint_field(json_wtr, "slow", s->alloc_slow);
	jsonw_uint_field(json_wtr, "fast", s->alloc_fast);
	jsonw_end_object(json_wtr);

	jsonw_name(json_wtr, "recycle");
	jsonw_start_object(json_wtr);
	jsonw_uint_field(json_wtr, "ring", s->recycle_ring);
	jsonw_uint_field(json_wtr, "cache", s->recycle_cache);
	jsonw_end_object(json_wtr);
}

static void print_plain_recycling_stats(struct pp_stat *s)
{
	double recycle;

	if (s->alloc_fast + s->alloc_slow) {
		recycle = (double)(s->recycle_ring + s->recycle_cache) /
			(s->alloc_fast + s->alloc_slow) * 100;
		printf("recycling: %.1lf%% (alloc: %zu:%zu recycle: %zu:%zu)",
		       recycle, s->alloc_slow, s->alloc_fast,
		       s->recycle_ring, s->recycle_cache);
	}
}

static void print_json_stats(struct pp_stats_array *a)
{
	jsonw_start_array(json_wtr);

	for (unsigned int i = 0; i < a->i; i++) {
		char ifname[IF_NAMESIZE];
		struct pp_stat *s = &a->s[i];
		const char *name;

		jsonw_start_object(json_wtr);

		if (!s->ifc) {
			jsonw_string_field(json_wtr, "ifname", "<orphan>");
			jsonw_uint_field(json_wtr, "ifindex", 0);
		} else {
			name = if_indextoname(s->ifc, ifname);
			if (name)
				jsonw_string_field(json_wtr, "ifname", name);
			jsonw_uint_field(json_wtr, "ifindex", s->ifc);
		}

		jsonw_uint_field(json_wtr, "page_pools", s->live[1].cnt);
		jsonw_uint_field(json_wtr, "zombies", s->live[0].cnt);

		jsonw_name(json_wtr, "live");
		jsonw_start_object(json_wtr);
		jsonw_uint_field(json_wtr, "refs", s->live[1].refs);
		jsonw_uint_field(json_wtr, "bytes", s->live[1].bytes);
		jsonw_end_object(json_wtr);

		jsonw_name(json_wtr, "zombie");
		jsonw_start_object(json_wtr);
		jsonw_uint_field(json_wtr, "refs", s->live[0].refs);
		jsonw_uint_field(json_wtr, "bytes", s->live[0].bytes);
		jsonw_end_object(json_wtr);

		if (s->alloc_fast || s->alloc_slow)
			print_json_recycling_stats(s);

		jsonw_end_object(json_wtr);
	}

	jsonw_end_array(json_wtr);
}

static void print_plain_stats(struct pp_stats_array *a)
{
	for (unsigned int i = 0; i < a->i; i++) {
		char ifname[IF_NAMESIZE];
		struct pp_stat *s = &a->s[i];
		const char *name;

		if (!s->ifc) {
			printf("<orphan>\t");
		} else {
			name = if_indextoname(s->ifc, ifname);
			if (name)
				printf("%8s", name);
			printf("[%u]\t", s->ifc);
		}

		printf("page pools: %u (zombies: %u)\n",
		       s->live[1].cnt, s->live[0].cnt);
		printf("\t\trefs: %zu bytes: %zu (refs: %zu bytes: %zu)\n",
		       s->live[1].refs, s->live[1].bytes,
		       s->live[0].refs, s->live[0].bytes);

		if (s->alloc_fast || s->alloc_slow) {
			printf("\t\t");
			print_plain_recycling_stats(s);
			printf("\n");
		}
	}
}

static bool
find_pool_stat_in_list(struct netdev_page_pool_stats_get_list *pp_stats,
		       __u64 pool_id, struct pp_stat *pstat)
{
	ynl_dump_foreach(pp_stats, pp) {
		if (!pp->_present.info || !pp->info._present.id)
			continue;
		if (pp->info.id != pool_id)
			continue;

		memset(pstat, 0, sizeof(*pstat));
		if (pp->_present.alloc_fast)
			pstat->alloc_fast = pp->alloc_fast;
		if (pp->_present.alloc_refill)
			pstat->alloc_fast += pp->alloc_refill;
		if (pp->_present.alloc_slow)
			pstat->alloc_slow = pp->alloc_slow;
		if (pp->_present.recycle_ring)
			pstat->recycle_ring = pp->recycle_ring;
		if (pp->_present.recycle_cached)
			pstat->recycle_cache = pp->recycle_cached;
		return true;
	}
	return false;
}

static void
print_json_pool_list(struct netdev_page_pool_get_list *pools,
		     struct netdev_page_pool_stats_get_list *pp_stats,
		     bool zombies_only)
{
	jsonw_start_array(json_wtr);

	ynl_dump_foreach(pools, pp) {
		char ifname[IF_NAMESIZE];
		struct pp_stat pstat;
		const char *name;

		if (zombies_only && !pp->_present.detach_time)
			continue;

		jsonw_start_object(json_wtr);

		jsonw_uint_field(json_wtr, "id", pp->id);

		if (pp->_present.ifindex) {
			name = if_indextoname(pp->ifindex, ifname);
			if (name)
				jsonw_string_field(json_wtr, "ifname", name);
			jsonw_uint_field(json_wtr, "ifindex", pp->ifindex);
		}

		if (pp->_present.napi_id)
			jsonw_uint_field(json_wtr, "napi_id", pp->napi_id);

		if (pp->_present.inflight)
			jsonw_uint_field(json_wtr, "refs", pp->inflight);

		if (pp->_present.inflight_mem)
			jsonw_uint_field(json_wtr, "bytes", pp->inflight_mem);

		if (pp->_present.detach_time)
			jsonw_uint_field(json_wtr, "detach_time", pp->detach_time);

		if (pp->_present.dmabuf)
			jsonw_uint_field(json_wtr, "dmabuf", pp->dmabuf);

		if (find_pool_stat_in_list(pp_stats, pp->id, &pstat) &&
		    (pstat.alloc_fast || pstat.alloc_slow))
			print_json_recycling_stats(&pstat);

		jsonw_end_object(json_wtr);
	}

	jsonw_end_array(json_wtr);
}

static void
print_plain_pool_list(struct netdev_page_pool_get_list *pools,
		      struct netdev_page_pool_stats_get_list *pp_stats,
		      bool zombies_only)
{
	ynl_dump_foreach(pools, pp) {
		char ifname[IF_NAMESIZE];
		struct pp_stat pstat;
		const char *name;

		if (zombies_only && !pp->_present.detach_time)
			continue;

		printf("pool id: %llu", pp->id);

		if (pp->_present.ifindex) {
			name = if_indextoname(pp->ifindex, ifname);
			if (name)
				printf("  dev: %s", name);
			printf("[%u]", pp->ifindex);
		}

		if (pp->_present.napi_id)
			printf("  napi: %llu", pp->napi_id);

		printf("\n");

		if (pp->_present.inflight || pp->_present.inflight_mem) {
			printf("  inflight:");
			if (pp->_present.inflight)
				printf(" %llu pages", pp->inflight);
			if (pp->_present.inflight_mem)
				printf(" %llu bytes", pp->inflight_mem);
			printf("\n");
		}

		if (pp->_present.detach_time)
			printf("  detached: %llu\n", pp->detach_time);

		if (pp->_present.dmabuf)
			printf("  dmabuf: %u\n", pp->dmabuf);

		if (find_pool_stat_in_list(pp_stats, pp->id, &pstat) &&
		    (pstat.alloc_fast || pstat.alloc_slow)) {
			printf("  ");
			print_plain_recycling_stats(&pstat);
			printf("\n");
		}
	}
}

static void aggregate_device_stats(struct pp_stats_array *a,
				   struct netdev_page_pool_get_list *pools,
				   struct netdev_page_pool_stats_get_list *pp_stats)
{
	ynl_dump_foreach(pools, pp) {
		struct pp_stat *s = find_ifc(a, pp->ifindex);

		count_pool(s, 1, pp);
		if (pp->_present.detach_time)
			count_pool(s, 0, pp);
	}

	ynl_dump_foreach(pp_stats, pp) {
		struct pp_stat *s = find_ifc(a, pp->info.ifindex);

		if (pp->_present.alloc_fast)
			s->alloc_fast += pp->alloc_fast;
		if (pp->_present.alloc_refill)
			s->alloc_fast += pp->alloc_refill;
		if (pp->_present.alloc_slow)
			s->alloc_slow += pp->alloc_slow;
		if (pp->_present.recycle_ring)
			s->recycle_ring += pp->recycle_ring;
		if (pp->_present.recycle_cached)
			s->recycle_cache += pp->recycle_cached;
	}
}

static int do_stats(int argc, char **argv)
{
	struct netdev_page_pool_stats_get_list *pp_stats;
	struct netdev_page_pool_get_list *pools;
	enum {
		GROUP_BY_DEVICE,
		GROUP_BY_POOL,
	} group_by = GROUP_BY_DEVICE;
	bool zombies_only = false;
	struct pp_stats_array a = {};
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ret = 0;

	/* Parse options */
	while (argc > 0) {
		if (is_prefix(*argv, "group-by")) {
			NEXT_ARG();

			if (!REQ_ARGS(1))
				return -1;

			if (is_prefix(*argv, "device")) {
				group_by = GROUP_BY_DEVICE;
			} else if (is_prefix(*argv, "pp") ||
				   is_prefix(*argv, "page-pool") ||
				   is_prefix(*argv, "none")) {
				group_by = GROUP_BY_POOL;
			} else {
				p_err("invalid group-by value '%s'", *argv);
				return -1;
			}
			NEXT_ARG();
		} else if (is_prefix(*argv, "zombies")) {
			zombies_only = true;
			group_by = GROUP_BY_POOL;
			NEXT_ARG();
		} else {
			p_err("unknown option '%s'", *argv);
			return -1;
		}
	}

	ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!ys) {
		p_err("YNL: %s", yerr.msg);
		return -1;
	}

	pools = netdev_page_pool_get_dump(ys);
	if (!pools) {
		p_err("failed to get page pools: %s", ys->err.msg);
		ret = -1;
		goto exit_close;
	}

	pp_stats = netdev_page_pool_stats_get_dump(ys);
	if (!pp_stats) {
		p_err("failed to get page pool stats: %s", ys->err.msg);
		ret = -1;
		goto exit_free_pp_list;
	}

	/* If grouping by pool, print individual pools */
	if (group_by == GROUP_BY_POOL) {
		if (json_output)
			print_json_pool_list(pools, pp_stats, zombies_only);
		else
			print_plain_pool_list(pools, pp_stats, zombies_only);
	} else {
		/* Aggregated stats mode (group-by device) */
		a.max = 64;
		a.s = calloc(a.max, sizeof(*a.s));
		if (!a.s) {
			p_err("failed to allocate stats array");
			ret = -1;
			goto exit_free_stats_list;
		}

		aggregate_device_stats(&a, pools, pp_stats);

		if (json_output)
			print_json_stats(&a);
		else
			print_plain_stats(&a);

		free(a.s);
	}

exit_free_stats_list:
	netdev_page_pool_stats_get_list_free(pp_stats);
exit_free_pp_list:
	netdev_page_pool_get_list_free(pools);
exit_close:
	ynl_sock_destroy(ys);
	return ret;
}

static int do_help(int argc __attribute__((unused)),
		   char **argv __attribute__((unused)))
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s page-pool { COMMAND | help }\n"
		"       %s page-pool stats [ OPTIONS ]\n"
		"\n"
		"       OPTIONS := { group-by { device | page-pool | none } | zombies }\n"
		"\n"
		"       stats                   - Display page pool statistics\n"
		"       stats group-by device   - Group statistics by network device (default)\n"
		"       stats group-by page-pool | pp | none\n"
		"                               - Show individual page pool details (no grouping)\n"
		"       stats zombies           - Show only zombie page pools (detached but with\n"
		"                                 pages in flight). Implies group-by page-pool.\n"
		"",
		bin_name, bin_name);

	return 0;
}

static const struct cmd page_pool_cmds[] = {
	{ "help",	do_help },
	{ "stats",	do_stats },
	{ 0 }
};

int do_page_pool(int argc, char **argv)
{
	return cmd_select(page_pool_cmds, argc, argv, do_help);
}
