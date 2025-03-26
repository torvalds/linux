/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <inttypes.h>
#include <fcntl.h>
#include <time.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_flatcg.h"
#include "scx_flatcg.bpf.skel.h"

#ifndef FILEID_KERNFS
#define FILEID_KERNFS		0xfe
#endif

const char help_fmt[] =
"A flattened cgroup hierarchy sched_ext scheduler.\n"
"\n"
"See the top-level comment in .bpf.c for more details.\n"
"\n"
"Usage: %s [-s SLICE_US] [-i INTERVAL] [-f] [-v]\n"
"\n"
"  -s SLICE_US   Override slice duration\n"
"  -i INTERVAL   Report interval\n"
"  -f            Use FIFO scheduling instead of weighted vtime scheduling\n"
"  -v            Print libbpf debug messages\n"
"  -h            Display this help and exit\n";

static bool verbose;
static volatile int exit_req;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sigint_handler(int dummy)
{
	exit_req = 1;
}

static float read_cpu_util(__u64 *last_sum, __u64 *last_idle)
{
	FILE *fp;
	char buf[4096];
	char *line, *cur = NULL, *tok;
	__u64 sum = 0, idle = 0;
	__u64 delta_sum, delta_idle;
	int idx;

	fp = fopen("/proc/stat", "r");
	if (!fp) {
		perror("fopen(\"/proc/stat\")");
		return 0.0;
	}

	if (!fgets(buf, sizeof(buf), fp)) {
		perror("fgets(\"/proc/stat\")");
		fclose(fp);
		return 0.0;
	}
	fclose(fp);

	line = buf;
	for (idx = 0; (tok = strtok_r(line, " \n", &cur)); idx++) {
		char *endp = NULL;
		__u64 v;

		if (idx == 0) {
			line = NULL;
			continue;
		}
		v = strtoull(tok, &endp, 0);
		if (!endp || *endp != '\0') {
			fprintf(stderr, "failed to parse %dth field of /proc/stat (\"%s\")\n",
				idx, tok);
			continue;
		}
		sum += v;
		if (idx == 4)
			idle = v;
	}

	delta_sum = sum - *last_sum;
	delta_idle = idle - *last_idle;
	*last_sum = sum;
	*last_idle = idle;

	return delta_sum ? (float)(delta_sum - delta_idle) / delta_sum : 0.0;
}

static void fcg_read_stats(struct scx_flatcg *skel, __u64 *stats)
{
	__u64 cnts[FCG_NR_STATS][skel->rodata->nr_cpus];
	__u32 idx;

	memset(stats, 0, sizeof(stats[0]) * FCG_NR_STATS);

	for (idx = 0; idx < FCG_NR_STATS; idx++) {
		int ret, cpu;

		ret = bpf_map_lookup_elem(bpf_map__fd(skel->maps.stats),
					  &idx, cnts[idx]);
		if (ret < 0)
			continue;
		for (cpu = 0; cpu < skel->rodata->nr_cpus; cpu++)
			stats[idx] += cnts[idx][cpu];
	}
}

int main(int argc, char **argv)
{
	struct scx_flatcg *skel;
	struct bpf_link *link;
	struct timespec intv_ts = { .tv_sec = 2, .tv_nsec = 0 };
	bool dump_cgrps = false;
	__u64 last_cpu_sum = 0, last_cpu_idle = 0;
	__u64 last_stats[FCG_NR_STATS] = {};
	unsigned long seq = 0;
	__s32 opt;
	__u64 ecode;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
restart:
	skel = SCX_OPS_OPEN(flatcg_ops, scx_flatcg);

	skel->rodata->nr_cpus = libbpf_num_possible_cpus();
	skel->rodata->cgrp_slice_ns = __COMPAT_ENUM_OR_ZERO("scx_public_consts", "SCX_SLICE_DFL");

	while ((opt = getopt(argc, argv, "s:i:dfvh")) != -1) {
		double v;

		switch (opt) {
		case 's':
			v = strtod(optarg, NULL);
			skel->rodata->cgrp_slice_ns = v * 1000;
			break;
		case 'i':
			v = strtod(optarg, NULL);
			intv_ts.tv_sec = v;
			intv_ts.tv_nsec = (v - (float)intv_ts.tv_sec) * 1000000000;
			break;
		case 'd':
			dump_cgrps = true;
			break;
		case 'f':
			skel->rodata->fifo_sched = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'h':
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	printf("slice=%.1lfms intv=%.1lfs dump_cgrps=%d",
	       (double)skel->rodata->cgrp_slice_ns / 1000000.0,
	       (double)intv_ts.tv_sec + (double)intv_ts.tv_nsec / 1000000000.0,
	       dump_cgrps);

	SCX_OPS_LOAD(skel, flatcg_ops, scx_flatcg, uei);
	link = SCX_OPS_ATTACH(skel, flatcg_ops, scx_flatcg);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		__u64 acc_stats[FCG_NR_STATS];
		__u64 stats[FCG_NR_STATS];
		float cpu_util;
		int i;

		cpu_util = read_cpu_util(&last_cpu_sum, &last_cpu_idle);

		fcg_read_stats(skel, acc_stats);
		for (i = 0; i < FCG_NR_STATS; i++)
			stats[i] = acc_stats[i] - last_stats[i];

		memcpy(last_stats, acc_stats, sizeof(acc_stats));

		printf("\n[SEQ %6lu cpu=%5.1lf hweight_gen=%" PRIu64 "]\n",
		       seq++, cpu_util * 100.0, skel->data->hweight_gen);
		printf("       act:%6llu  deact:%6llu global:%6llu local:%6llu\n",
		       stats[FCG_STAT_ACT],
		       stats[FCG_STAT_DEACT],
		       stats[FCG_STAT_GLOBAL],
		       stats[FCG_STAT_LOCAL]);
		printf("HWT  cache:%6llu update:%6llu   skip:%6llu  race:%6llu\n",
		       stats[FCG_STAT_HWT_CACHE],
		       stats[FCG_STAT_HWT_UPDATES],
		       stats[FCG_STAT_HWT_SKIP],
		       stats[FCG_STAT_HWT_RACE]);
		printf("ENQ   skip:%6llu   race:%6llu\n",
		       stats[FCG_STAT_ENQ_SKIP],
		       stats[FCG_STAT_ENQ_RACE]);
		printf("CNS   keep:%6llu expire:%6llu  empty:%6llu  gone:%6llu\n",
		       stats[FCG_STAT_CNS_KEEP],
		       stats[FCG_STAT_CNS_EXPIRE],
		       stats[FCG_STAT_CNS_EMPTY],
		       stats[FCG_STAT_CNS_GONE]);
		printf("PNC   next:%6llu  empty:%6llu nocgrp:%6llu  gone:%6llu race:%6llu fail:%6llu\n",
		       stats[FCG_STAT_PNC_NEXT],
		       stats[FCG_STAT_PNC_EMPTY],
		       stats[FCG_STAT_PNC_NO_CGRP],
		       stats[FCG_STAT_PNC_GONE],
		       stats[FCG_STAT_PNC_RACE],
		       stats[FCG_STAT_PNC_FAIL]);
		printf("BAD remove:%6llu\n",
		       acc_stats[FCG_STAT_BAD_REMOVAL]);
		fflush(stdout);

		nanosleep(&intv_ts, NULL);
	}

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_flatcg__destroy(skel);

	if (UEI_ECODE_RESTART(ecode))
		goto restart;
	return 0;
}
