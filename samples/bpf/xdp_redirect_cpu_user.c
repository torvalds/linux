// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__ =
	" XDP redirect with a CPU-map type \"BPF_MAP_TYPE_CPUMAP\"";

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <getopt.h>
#include <net/if.h>
#include <time.h>
#include <linux/limits.h>

#include <arpa/inet.h>
#include <linux/if_link.h>

/* How many xdp_progs are defined in _kern.c */
#define MAX_PROG 6

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"

static int ifindex = -1;
static char ifname_buf[IF_NAMESIZE];
static char *ifname;
static __u32 prog_id;

static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static int n_cpus;

enum map_type {
	CPU_MAP,
	RX_CNT,
	REDIRECT_ERR_CNT,
	CPUMAP_ENQUEUE_CNT,
	CPUMAP_KTHREAD_CNT,
	CPUS_AVAILABLE,
	CPUS_COUNT,
	CPUS_ITERATOR,
	EXCEPTION_CNT,
};

static const char *const map_type_strings[] = {
	[CPU_MAP] = "cpu_map",
	[RX_CNT] = "rx_cnt",
	[REDIRECT_ERR_CNT] = "redirect_err_cnt",
	[CPUMAP_ENQUEUE_CNT] = "cpumap_enqueue_cnt",
	[CPUMAP_KTHREAD_CNT] = "cpumap_kthread_cnt",
	[CPUS_AVAILABLE] = "cpus_available",
	[CPUS_COUNT] = "cpus_count",
	[CPUS_ITERATOR] = "cpus_iterator",
	[EXCEPTION_CNT] = "exception_cnt",
};

#define NUM_TP 5
#define NUM_MAP 9
struct bpf_link *tp_links[NUM_TP] = {};
static int map_fds[NUM_MAP];
static int tp_cnt = 0;

/* Exit return codes */
#define EXIT_OK		0
#define EXIT_FAIL		1
#define EXIT_FAIL_OPTION	2
#define EXIT_FAIL_XDP		3
#define EXIT_FAIL_BPF		4
#define EXIT_FAIL_MEM		5

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"dev",		required_argument,	NULL, 'd' },
	{"skb-mode",	no_argument,		NULL, 'S' },
	{"sec",		required_argument,	NULL, 's' },
	{"progname",	required_argument,	NULL, 'p' },
	{"qsize",	required_argument,	NULL, 'q' },
	{"cpu",		required_argument,	NULL, 'c' },
	{"stress-mode", no_argument,		NULL, 'x' },
	{"no-separators", no_argument,		NULL, 'z' },
	{"force",	no_argument,		NULL, 'F' },
	{"mprog-disable", no_argument,		NULL, 'n' },
	{"mprog-name",	required_argument,	NULL, 'e' },
	{"mprog-filename", required_argument,	NULL, 'f' },
	{"redirect-device", required_argument,	NULL, 'r' },
	{"redirect-map", required_argument,	NULL, 'm' },
	{0, 0, NULL,  0 }
};

static void int_exit(int sig)
{
	__u32 curr_prog_id = 0;

	if (ifindex > -1) {
		if (bpf_get_link_xdp_id(ifindex, &curr_prog_id, xdp_flags)) {
			printf("bpf_get_link_xdp_id failed\n");
			exit(EXIT_FAIL);
		}
		if (prog_id == curr_prog_id) {
			fprintf(stderr,
				"Interrupted: Removing XDP program on ifindex:%d device:%s\n",
				ifindex, ifname);
			bpf_set_link_xdp_fd(ifindex, -1, xdp_flags);
		} else if (!curr_prog_id) {
			printf("couldn't find a prog id on a given iface\n");
		} else {
			printf("program on interface changed, not removing\n");
		}
	}
	/* Detach tracepoints */
	while (tp_cnt)
		bpf_link__destroy(tp_links[--tp_cnt]);

	exit(EXIT_OK);
}

static void print_avail_progs(struct bpf_object *obj)
{
	struct bpf_program *pos;

	bpf_object__for_each_program(pos, obj) {
		if (bpf_program__is_xdp(pos))
			printf(" %s\n", bpf_program__section_name(pos));
	}
}

static void usage(char *argv[], struct bpf_object *obj)
{
	int i;

	printf("\nDOCUMENTATION:\n%s\n", __doc__);
	printf("\n");
	printf(" Usage: %s (options-see-below)\n", argv[0]);
	printf(" Listing options:\n");
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-12s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)",
				*long_options[i].flag);
		else
			printf(" short-option: -%c",
				long_options[i].val);
		printf("\n");
	}
	printf("\n Programs to be used for --progname:\n");
	print_avail_progs(obj);
	printf("\n");
}

/* gettime returns the current time of day in nanoseconds.
 * Cost: clock_gettime (ns) => 26ns (CLOCK_MONOTONIC)
 *       clock_gettime (ns) =>  9ns (CLOCK_MONOTONIC_COARSE)
 */
#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
static __u64 gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		exit(EXIT_FAIL);
	}
	return (__u64) t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
}

/* Common stats data record shared with _kern.c */
struct datarec {
	__u64 processed;
	__u64 dropped;
	__u64 issue;
	__u64 xdp_pass;
	__u64 xdp_drop;
	__u64 xdp_redirect;
};
struct record {
	__u64 timestamp;
	struct datarec total;
	struct datarec *cpu;
};
struct stats_record {
	struct record rx_cnt;
	struct record redir_err;
	struct record kthread;
	struct record exception;
	struct record enq[];
};

static bool map_collect_percpu(int fd, __u32 key, struct record *rec)
{
	/* For percpu maps, userspace gets a value per possible CPU */
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct datarec values[nr_cpus];
	__u64 sum_xdp_redirect = 0;
	__u64 sum_xdp_pass = 0;
	__u64 sum_xdp_drop = 0;
	__u64 sum_processed = 0;
	__u64 sum_dropped = 0;
	__u64 sum_issue = 0;
	int i;

	if ((bpf_map_lookup_elem(fd, &key, values)) != 0) {
		fprintf(stderr,
			"ERR: bpf_map_lookup_elem failed key:0x%X\n", key);
		return false;
	}
	/* Get time as close as possible to reading map contents */
	rec->timestamp = gettime();

	/* Record and sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		rec->cpu[i].processed = values[i].processed;
		sum_processed        += values[i].processed;
		rec->cpu[i].dropped = values[i].dropped;
		sum_dropped        += values[i].dropped;
		rec->cpu[i].issue = values[i].issue;
		sum_issue        += values[i].issue;
		rec->cpu[i].xdp_pass = values[i].xdp_pass;
		sum_xdp_pass += values[i].xdp_pass;
		rec->cpu[i].xdp_drop = values[i].xdp_drop;
		sum_xdp_drop += values[i].xdp_drop;
		rec->cpu[i].xdp_redirect = values[i].xdp_redirect;
		sum_xdp_redirect += values[i].xdp_redirect;
	}
	rec->total.processed = sum_processed;
	rec->total.dropped   = sum_dropped;
	rec->total.issue     = sum_issue;
	rec->total.xdp_pass  = sum_xdp_pass;
	rec->total.xdp_drop  = sum_xdp_drop;
	rec->total.xdp_redirect = sum_xdp_redirect;
	return true;
}

static struct datarec *alloc_record_per_cpu(void)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct datarec *array;

	array = calloc(nr_cpus, sizeof(struct datarec));
	if (!array) {
		fprintf(stderr, "Mem alloc error (nr_cpus:%u)\n", nr_cpus);
		exit(EXIT_FAIL_MEM);
	}
	return array;
}

static struct stats_record *alloc_stats_record(void)
{
	struct stats_record *rec;
	int i, size;

	size = sizeof(*rec) + n_cpus * sizeof(struct record);
	rec = malloc(size);
	if (!rec) {
		fprintf(stderr, "Mem alloc error\n");
		exit(EXIT_FAIL_MEM);
	}
	memset(rec, 0, size);
	rec->rx_cnt.cpu    = alloc_record_per_cpu();
	rec->redir_err.cpu = alloc_record_per_cpu();
	rec->kthread.cpu   = alloc_record_per_cpu();
	rec->exception.cpu = alloc_record_per_cpu();
	for (i = 0; i < n_cpus; i++)
		rec->enq[i].cpu = alloc_record_per_cpu();

	return rec;
}

static void free_stats_record(struct stats_record *r)
{
	int i;

	for (i = 0; i < n_cpus; i++)
		free(r->enq[i].cpu);
	free(r->exception.cpu);
	free(r->kthread.cpu);
	free(r->redir_err.cpu);
	free(r->rx_cnt.cpu);
	free(r);
}

static double calc_period(struct record *r, struct record *p)
{
	double period_ = 0;
	__u64 period = 0;

	period = r->timestamp - p->timestamp;
	if (period > 0)
		period_ = ((double) period / NANOSEC_PER_SEC);

	return period_;
}

static __u64 calc_pps(struct datarec *r, struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->processed - p->processed;
		pps = packets / period_;
	}
	return pps;
}

static __u64 calc_drop_pps(struct datarec *r, struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->dropped - p->dropped;
		pps = packets / period_;
	}
	return pps;
}

static __u64 calc_errs_pps(struct datarec *r,
			    struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->issue - p->issue;
		pps = packets / period_;
	}
	return pps;
}

static void calc_xdp_pps(struct datarec *r, struct datarec *p,
			 double *xdp_pass, double *xdp_drop,
			 double *xdp_redirect, double period_)
{
	*xdp_pass = 0, *xdp_drop = 0, *xdp_redirect = 0;
	if (period_ > 0) {
		*xdp_redirect = (r->xdp_redirect - p->xdp_redirect) / period_;
		*xdp_pass = (r->xdp_pass - p->xdp_pass) / period_;
		*xdp_drop = (r->xdp_drop - p->xdp_drop) / period_;
	}
}

static void stats_print(struct stats_record *stats_rec,
			struct stats_record *stats_prev,
			char *prog_name, char *mprog_name, int mprog_fd)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	double pps = 0, drop = 0, err = 0;
	bool mprog_enabled = false;
	struct record *rec, *prev;
	int to_cpu;
	double t;
	int i;

	if (mprog_fd > 0)
		mprog_enabled = true;

	/* Header */
	printf("Running XDP/eBPF prog_name:%s\n", prog_name);
	printf("%-15s %-7s %-14s %-11s %-9s\n",
	       "XDP-cpumap", "CPU:to", "pps", "drop-pps", "extra-info");

	/* XDP rx_cnt */
	{
		char *fmt_rx = "%-15s %-7d %'-14.0f %'-11.0f %'-10.0f %s\n";
		char *fm2_rx = "%-15s %-7s %'-14.0f %'-11.0f\n";
		char *errstr = "";

		rec  = &stats_rec->rx_cnt;
		prev = &stats_prev->rx_cnt;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps = calc_pps(r, p, t);
			drop = calc_drop_pps(r, p, t);
			err  = calc_errs_pps(r, p, t);
			if (err > 0)
				errstr = "cpu-dest/err";
			if (pps > 0)
				printf(fmt_rx, "XDP-RX",
					i, pps, drop, err, errstr);
		}
		pps  = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		err  = calc_errs_pps(&rec->total, &prev->total, t);
		printf(fm2_rx, "XDP-RX", "total", pps, drop);
	}

	/* cpumap enqueue stats */
	for (to_cpu = 0; to_cpu < n_cpus; to_cpu++) {
		char *fmt = "%-15s %3d:%-3d %'-14.0f %'-11.0f %'-10.2f %s\n";
		char *fm2 = "%-15s %3s:%-3d %'-14.0f %'-11.0f %'-10.2f %s\n";
		char *errstr = "";

		rec  =  &stats_rec->enq[to_cpu];
		prev = &stats_prev->enq[to_cpu];
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop_pps(r, p, t);
			err  = calc_errs_pps(r, p, t);
			if (err > 0) {
				errstr = "bulk-average";
				err = pps / err; /* calc average bulk size */
			}
			if (pps > 0)
				printf(fmt, "cpumap-enqueue",
				       i, to_cpu, pps, drop, err, errstr);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		if (pps > 0) {
			drop = calc_drop_pps(&rec->total, &prev->total, t);
			err  = calc_errs_pps(&rec->total, &prev->total, t);
			if (err > 0) {
				errstr = "bulk-average";
				err = pps / err; /* calc average bulk size */
			}
			printf(fm2, "cpumap-enqueue",
			       "sum", to_cpu, pps, drop, err, errstr);
		}
	}

	/* cpumap kthread stats */
	{
		char *fmt_k = "%-15s %-7d %'-14.0f %'-11.0f %'-10.0f %s\n";
		char *fm2_k = "%-15s %-7s %'-14.0f %'-11.0f %'-10.0f %s\n";
		char *e_str = "";

		rec  = &stats_rec->kthread;
		prev = &stats_prev->kthread;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop_pps(r, p, t);
			err  = calc_errs_pps(r, p, t);
			if (err > 0)
				e_str = "sched";
			if (pps > 0)
				printf(fmt_k, "cpumap_kthread",
				       i, pps, drop, err, e_str);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		err  = calc_errs_pps(&rec->total, &prev->total, t);
		if (err > 0)
			e_str = "sched-sum";
		printf(fm2_k, "cpumap_kthread", "total", pps, drop, err, e_str);
	}

	/* XDP redirect err tracepoints (very unlikely) */
	{
		char *fmt_err = "%-15s %-7d %'-14.0f %'-11.0f\n";
		char *fm2_err = "%-15s %-7s %'-14.0f %'-11.0f\n";

		rec  = &stats_rec->redir_err;
		prev = &stats_prev->redir_err;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop_pps(r, p, t);
			if (pps > 0)
				printf(fmt_err, "redirect_err", i, pps, drop);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		printf(fm2_err, "redirect_err", "total", pps, drop);
	}

	/* XDP general exception tracepoints */
	{
		char *fmt_err = "%-15s %-7d %'-14.0f %'-11.0f\n";
		char *fm2_err = "%-15s %-7s %'-14.0f %'-11.0f\n";

		rec  = &stats_rec->exception;
		prev = &stats_prev->exception;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop_pps(r, p, t);
			if (pps > 0)
				printf(fmt_err, "xdp_exception", i, pps, drop);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		printf(fm2_err, "xdp_exception", "total", pps, drop);
	}

	/* CPUMAP attached XDP program that runs on remote/destination CPU */
	if (mprog_enabled) {
		char *fmt_k = "%-15s %-7d %'-14.0f %'-11.0f %'-10.0f\n";
		char *fm2_k = "%-15s %-7s %'-14.0f %'-11.0f %'-10.0f\n";
		double xdp_pass, xdp_drop, xdp_redirect;

		printf("\n2nd remote XDP/eBPF prog_name: %s\n", mprog_name);
		printf("%-15s %-7s %-14s %-11s %-9s\n",
		       "XDP-cpumap", "CPU:to", "xdp-pass", "xdp-drop", "xdp-redir");

		rec  = &stats_rec->kthread;
		prev = &stats_prev->kthread;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			calc_xdp_pps(r, p, &xdp_pass, &xdp_drop,
				     &xdp_redirect, t);
			if (xdp_pass > 0 || xdp_drop > 0 || xdp_redirect > 0)
				printf(fmt_k, "xdp-in-kthread", i, xdp_pass, xdp_drop,
				       xdp_redirect);
		}
		calc_xdp_pps(&rec->total, &prev->total, &xdp_pass, &xdp_drop,
			     &xdp_redirect, t);
		printf(fm2_k, "xdp-in-kthread", "total", xdp_pass, xdp_drop, xdp_redirect);
	}

	printf("\n");
	fflush(stdout);
}

static void stats_collect(struct stats_record *rec)
{
	int fd, i;

	fd = map_fds[RX_CNT];
	map_collect_percpu(fd, 0, &rec->rx_cnt);

	fd = map_fds[REDIRECT_ERR_CNT];
	map_collect_percpu(fd, 1, &rec->redir_err);

	fd = map_fds[CPUMAP_ENQUEUE_CNT];
	for (i = 0; i < n_cpus; i++)
		map_collect_percpu(fd, i, &rec->enq[i]);

	fd = map_fds[CPUMAP_KTHREAD_CNT];
	map_collect_percpu(fd, 0, &rec->kthread);

	fd = map_fds[EXCEPTION_CNT];
	map_collect_percpu(fd, 0, &rec->exception);
}


/* Pointer swap trick */
static inline void swap(struct stats_record **a, struct stats_record **b)
{
	struct stats_record *tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static int create_cpu_entry(__u32 cpu, struct bpf_cpumap_val *value,
			    __u32 avail_idx, bool new)
{
	__u32 curr_cpus_count = 0;
	__u32 key = 0;
	int ret;

	/* Add a CPU entry to cpumap, as this allocate a cpu entry in
	 * the kernel for the cpu.
	 */
	ret = bpf_map_update_elem(map_fds[CPU_MAP], &cpu, value, 0);
	if (ret) {
		fprintf(stderr, "Create CPU entry failed (err:%d)\n", ret);
		exit(EXIT_FAIL_BPF);
	}

	/* Inform bpf_prog's that a new CPU is available to select
	 * from via some control maps.
	 */
	ret = bpf_map_update_elem(map_fds[CPUS_AVAILABLE], &avail_idx, &cpu, 0);
	if (ret) {
		fprintf(stderr, "Add to avail CPUs failed\n");
		exit(EXIT_FAIL_BPF);
	}

	/* When not replacing/updating existing entry, bump the count */
	ret = bpf_map_lookup_elem(map_fds[CPUS_COUNT], &key, &curr_cpus_count);
	if (ret) {
		fprintf(stderr, "Failed reading curr cpus_count\n");
		exit(EXIT_FAIL_BPF);
	}
	if (new) {
		curr_cpus_count++;
		ret = bpf_map_update_elem(map_fds[CPUS_COUNT], &key,
					  &curr_cpus_count, 0);
		if (ret) {
			fprintf(stderr, "Failed write curr cpus_count\n");
			exit(EXIT_FAIL_BPF);
		}
	}
	/* map_fd[7] = cpus_iterator */
	printf("%s CPU:%u as idx:%u qsize:%d prog_fd: %d (cpus_count:%u)\n",
	       new ? "Add-new":"Replace", cpu, avail_idx,
	       value->qsize, value->bpf_prog.fd, curr_cpus_count);

	return 0;
}

/* CPUs are zero-indexed. Thus, add a special sentinel default value
 * in map cpus_available to mark CPU index'es not configured
 */
static void mark_cpus_unavailable(void)
{
	__u32 invalid_cpu = n_cpus;
	int ret, i;

	for (i = 0; i < n_cpus; i++) {
		ret = bpf_map_update_elem(map_fds[CPUS_AVAILABLE], &i,
					  &invalid_cpu, 0);
		if (ret) {
			fprintf(stderr, "Failed marking CPU unavailable\n");
			exit(EXIT_FAIL_BPF);
		}
	}
}

/* Stress cpumap management code by concurrently changing underlying cpumap */
static void stress_cpumap(struct bpf_cpumap_val *value)
{
	/* Changing qsize will cause kernel to free and alloc a new
	 * bpf_cpu_map_entry, with an associated/complicated tear-down
	 * procedure.
	 */
	value->qsize = 1024;
	create_cpu_entry(1, value, 0, false);
	value->qsize = 8;
	create_cpu_entry(1, value, 0, false);
	value->qsize = 16000;
	create_cpu_entry(1, value, 0, false);
}

static void stats_poll(int interval, bool use_separators, char *prog_name,
		       char *mprog_name, struct bpf_cpumap_val *value,
		       bool stress_mode)
{
	struct stats_record *record, *prev;
	int mprog_fd;

	record = alloc_stats_record();
	prev   = alloc_stats_record();
	stats_collect(record);

	/* Trick to pretty printf with thousands separators use %' */
	if (use_separators)
		setlocale(LC_NUMERIC, "en_US");

	while (1) {
		swap(&prev, &record);
		mprog_fd = value->bpf_prog.fd;
		stats_collect(record);
		stats_print(record, prev, prog_name, mprog_name, mprog_fd);
		sleep(interval);
		if (stress_mode)
			stress_cpumap(value);
	}

	free_stats_record(record);
	free_stats_record(prev);
}

static int init_tracepoints(struct bpf_object *obj)
{
	struct bpf_program *prog;

	bpf_object__for_each_program(prog, obj) {
		if (bpf_program__is_tracepoint(prog) != true)
			continue;

		tp_links[tp_cnt] = bpf_program__attach(prog);
		if (libbpf_get_error(tp_links[tp_cnt])) {
			tp_links[tp_cnt] = NULL;
			return -EINVAL;
		}
		tp_cnt++;
	}

	return 0;
}

static int init_map_fds(struct bpf_object *obj)
{
	enum map_type type;

	for (type = 0; type < NUM_MAP; type++) {
		map_fds[type] =
			bpf_object__find_map_fd_by_name(obj,
							map_type_strings[type]);

		if (map_fds[type] < 0)
			return -ENOENT;
	}

	return 0;
}

static int load_cpumap_prog(char *file_name, char *prog_name,
			    char *redir_interface, char *redir_map)
{
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type		= BPF_PROG_TYPE_XDP,
		.expected_attach_type	= BPF_XDP_CPUMAP,
		.file = file_name,
	};
	struct bpf_program *prog;
	struct bpf_object *obj;
	int fd;

	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &fd))
		return -1;

	if (fd < 0) {
		fprintf(stderr, "ERR: bpf_prog_load_xattr: %s\n",
			strerror(errno));
		return fd;
	}

	if (redir_interface && redir_map) {
		int err, map_fd, ifindex_out, key = 0;

		map_fd = bpf_object__find_map_fd_by_name(obj, redir_map);
		if (map_fd < 0)
			return map_fd;

		ifindex_out = if_nametoindex(redir_interface);
		if (!ifindex_out)
			return -1;

		err = bpf_map_update_elem(map_fd, &key, &ifindex_out, 0);
		if (err < 0)
			return err;
	}

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (!prog) {
		fprintf(stderr, "bpf_object__find_program_by_title failed\n");
		return EXIT_FAIL;
	}

	return bpf_program__fd(prog);
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char *prog_name = "xdp_cpu_map5_lb_hash_ip_pairs";
	char *mprog_filename = "xdp_redirect_kern.o";
	char *redir_interface = NULL, *redir_map = NULL;
	char *mprog_name = "xdp_redirect_dummy";
	bool mprog_disable = false;
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type	= BPF_PROG_TYPE_UNSPEC,
	};
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	struct bpf_cpumap_val value;
	bool use_separators = true;
	bool stress_mode = false;
	struct bpf_program *prog;
	struct bpf_object *obj;
	int err = EXIT_FAIL;
	char filename[256];
	int added_cpus = 0;
	int longindex = 0;
	int interval = 2;
	int add_cpu = -1;
	int opt, prog_fd;
	int *cpu, i;
	__u32 qsize;

	n_cpus = get_nprocs_conf();

	/* Notice: choosing he queue size is very important with the
	 * ixgbe driver, because it's driver page recycling trick is
	 * dependend on pages being returned quickly.  The number of
	 * out-standing packets in the system must be less-than 2x
	 * RX-ring size.
	 */
	qsize = 128+64;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	prog_load_attr.file = filename;

	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK)");
		return 1;
	}

	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &prog_fd))
		return err;

	if (prog_fd < 0) {
		fprintf(stderr, "ERR: bpf_prog_load_xattr: %s\n",
			strerror(errno));
		return err;
	}

	if (init_tracepoints(obj) < 0) {
		fprintf(stderr, "ERR: bpf_program__attach failed\n");
		return err;
	}

	if (init_map_fds(obj) < 0) {
		fprintf(stderr, "bpf_object__find_map_fd_by_name failed\n");
		return err;
	}
	mark_cpus_unavailable();

	cpu = malloc(n_cpus * sizeof(int));
	if (!cpu) {
		fprintf(stderr, "failed to allocate cpu array\n");
		return err;
	}
	memset(cpu, 0, n_cpus * sizeof(int));

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hSd:s:p:q:c:xzFf:e:r:m:n",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'd':
			if (strlen(optarg) >= IF_NAMESIZE) {
				fprintf(stderr, "ERR: --dev name too long\n");
				goto error;
			}
			ifname = (char *)&ifname_buf;
			strncpy(ifname, optarg, IF_NAMESIZE);
			ifindex = if_nametoindex(ifname);
			if (ifindex == 0) {
				fprintf(stderr,
					"ERR: --dev name unknown err(%d):%s\n",
					errno, strerror(errno));
				goto error;
			}
			break;
		case 's':
			interval = atoi(optarg);
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'x':
			stress_mode = true;
			break;
		case 'z':
			use_separators = false;
			break;
		case 'p':
			/* Selecting eBPF prog to load */
			prog_name = optarg;
			break;
		case 'n':
			mprog_disable = true;
			break;
		case 'f':
			mprog_filename = optarg;
			break;
		case 'e':
			mprog_name = optarg;
			break;
		case 'r':
			redir_interface = optarg;
			break;
		case 'm':
			redir_map = optarg;
			break;
		case 'c':
			/* Add multiple CPUs */
			add_cpu = strtoul(optarg, NULL, 0);
			if (add_cpu >= n_cpus) {
				fprintf(stderr,
				"--cpu nr too large for cpumap err(%d):%s\n",
					errno, strerror(errno));
				goto error;
			}
			cpu[added_cpus++] = add_cpu;
			break;
		case 'q':
			qsize = atoi(optarg);
			break;
		case 'F':
			xdp_flags &= ~XDP_FLAGS_UPDATE_IF_NOEXIST;
			break;
		case 'h':
		error:
		default:
			free(cpu);
			usage(argv, obj);
			return EXIT_FAIL_OPTION;
		}
	}

	if (!(xdp_flags & XDP_FLAGS_SKB_MODE))
		xdp_flags |= XDP_FLAGS_DRV_MODE;

	/* Required option */
	if (ifindex == -1) {
		fprintf(stderr, "ERR: required option --dev missing\n");
		usage(argv, obj);
		err = EXIT_FAIL_OPTION;
		goto out;
	}
	/* Required option */
	if (add_cpu == -1) {
		fprintf(stderr, "ERR: required option --cpu missing\n");
		fprintf(stderr, " Specify multiple --cpu option to add more\n");
		usage(argv, obj);
		err = EXIT_FAIL_OPTION;
		goto out;
	}

	value.bpf_prog.fd = 0;
	if (!mprog_disable)
		value.bpf_prog.fd = load_cpumap_prog(mprog_filename, mprog_name,
						     redir_interface, redir_map);
	if (value.bpf_prog.fd < 0) {
		err = value.bpf_prog.fd;
		goto out;
	}
	value.qsize = qsize;

	for (i = 0; i < added_cpus; i++)
		create_cpu_entry(cpu[i], &value, i, true);

	/* Remove XDP program when program is interrupted or killed */
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (!prog) {
		fprintf(stderr, "bpf_object__find_program_by_title failed\n");
		goto out;
	}

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		fprintf(stderr, "bpf_program__fd failed\n");
		goto out;
	}

	if (bpf_set_link_xdp_fd(ifindex, prog_fd, xdp_flags) < 0) {
		fprintf(stderr, "link set xdp fd failed\n");
		err = EXIT_FAIL_XDP;
		goto out;
	}

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (err) {
		printf("can't get prog info - %s\n", strerror(errno));
		goto out;
	}
	prog_id = info.id;

	stats_poll(interval, use_separators, prog_name, mprog_name,
		   &value, stress_mode);

	err = EXIT_OK;
out:
	free(cpu);
	return err;
}
