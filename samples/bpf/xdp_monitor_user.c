/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 "XDP monitor tool, based on tracepoints\n"
;

static const char *__doc_err_only__=
 " NOTICE: Only tracking XDP redirect errors\n"
 "         Enable TX success stats via '--stats'\n"
 "         (which comes with a per packet processing overhead)\n"
;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <locale.h>

#include <sys/resource.h>
#include <getopt.h>
#include <net/if.h>
#include <time.h>

#include <signal.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"

enum map_type {
	REDIRECT_ERR_CNT,
	EXCEPTION_CNT,
	CPUMAP_ENQUEUE_CNT,
	CPUMAP_KTHREAD_CNT,
	DEVMAP_XMIT_CNT,
};

static const char *const map_type_strings[] = {
	[REDIRECT_ERR_CNT] = "redirect_err_cnt",
	[EXCEPTION_CNT] = "exception_cnt",
	[CPUMAP_ENQUEUE_CNT] = "cpumap_enqueue_cnt",
	[CPUMAP_KTHREAD_CNT] = "cpumap_kthread_cnt",
	[DEVMAP_XMIT_CNT] = "devmap_xmit_cnt",
};

#define NUM_MAP 5
#define NUM_TP 8

static int tp_cnt;
static int map_cnt;
static int verbose = 1;
static bool debug = false;
struct bpf_map *map_data[NUM_MAP] = {};
struct bpf_link *tp_links[NUM_TP] = {};
struct bpf_object *obj;

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"debug",	no_argument,		NULL, 'D' },
	{"stats",	no_argument,		NULL, 'S' },
	{"sec", 	required_argument,	NULL, 's' },
	{0, 0, NULL,  0 }
};

static void int_exit(int sig)
{
	/* Detach tracepoints */
	while (tp_cnt)
		bpf_link__destroy(tp_links[--tp_cnt]);

	bpf_object__close(obj);
	exit(0);
}

/* C standard specifies two constants, EXIT_SUCCESS(0) and EXIT_FAILURE(1) */
#define EXIT_FAIL_MEM	5

static void usage(char *argv[])
{
	int i;
	printf("\nDOCUMENTATION:\n%s\n", __doc__);
	printf("\n");
	printf(" Usage: %s (options-see-below)\n",
	       argv[0]);
	printf(" Listing options:\n");
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-15s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)",
			       *long_options[i].flag);
		else
			printf("short-option: -%c",
			       long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
static __u64 gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		exit(EXIT_FAILURE);
	}
	return (__u64) t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
}

enum {
	REDIR_SUCCESS = 0,
	REDIR_ERROR = 1,
};
#define REDIR_RES_MAX 2
static const char *redir_names[REDIR_RES_MAX] = {
	[REDIR_SUCCESS]	= "Success",
	[REDIR_ERROR]	= "Error",
};
static const char *err2str(int err)
{
	if (err < REDIR_RES_MAX)
		return redir_names[err];
	return NULL;
}
/* enum xdp_action */
#define XDP_UNKNOWN	XDP_REDIRECT + 1
#define XDP_ACTION_MAX (XDP_UNKNOWN + 1)
static const char *xdp_action_names[XDP_ACTION_MAX] = {
	[XDP_ABORTED]	= "XDP_ABORTED",
	[XDP_DROP]	= "XDP_DROP",
	[XDP_PASS]	= "XDP_PASS",
	[XDP_TX]	= "XDP_TX",
	[XDP_REDIRECT]	= "XDP_REDIRECT",
	[XDP_UNKNOWN]	= "XDP_UNKNOWN",
};
static const char *action2str(int action)
{
	if (action < XDP_ACTION_MAX)
		return xdp_action_names[action];
	return NULL;
}

/* Common stats data record shared with _kern.c */
struct datarec {
	__u64 processed;
	__u64 dropped;
	__u64 info;
	__u64 err;
};
#define MAX_CPUS 64

/* Userspace structs for collection of stats from maps */
struct record {
	__u64 timestamp;
	struct datarec total;
	struct datarec *cpu;
};
struct u64rec {
	__u64 processed;
};
struct record_u64 {
	/* record for _kern side __u64 values */
	__u64 timestamp;
	struct u64rec total;
	struct u64rec *cpu;
};

struct stats_record {
	struct record_u64 xdp_redirect[REDIR_RES_MAX];
	struct record_u64 xdp_exception[XDP_ACTION_MAX];
	struct record xdp_cpumap_kthread;
	struct record xdp_cpumap_enqueue[MAX_CPUS];
	struct record xdp_devmap_xmit;
};

static bool map_collect_record(int fd, __u32 key, struct record *rec)
{
	/* For percpu maps, userspace gets a value per possible CPU */
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct datarec values[nr_cpus];
	__u64 sum_processed = 0;
	__u64 sum_dropped = 0;
	__u64 sum_info = 0;
	__u64 sum_err = 0;
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
		rec->cpu[i].info = values[i].info;
		sum_info        += values[i].info;
		rec->cpu[i].err = values[i].err;
		sum_err        += values[i].err;
	}
	rec->total.processed = sum_processed;
	rec->total.dropped   = sum_dropped;
	rec->total.info      = sum_info;
	rec->total.err       = sum_err;
	return true;
}

static bool map_collect_record_u64(int fd, __u32 key, struct record_u64 *rec)
{
	/* For percpu maps, userspace gets a value per possible CPU */
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct u64rec values[nr_cpus];
	__u64 sum_total = 0;
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
		sum_total            += values[i].processed;
	}
	rec->total.processed = sum_total;
	return true;
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

static double calc_period_u64(struct record_u64 *r, struct record_u64 *p)
{
	double period_ = 0;
	__u64 period = 0;

	period = r->timestamp - p->timestamp;
	if (period > 0)
		period_ = ((double) period / NANOSEC_PER_SEC);

	return period_;
}

static double calc_pps(struct datarec *r, struct datarec *p, double period)
{
	__u64 packets = 0;
	double pps = 0;

	if (period > 0) {
		packets = r->processed - p->processed;
		pps = packets / period;
	}
	return pps;
}

static double calc_pps_u64(struct u64rec *r, struct u64rec *p, double period)
{
	__u64 packets = 0;
	double pps = 0;

	if (period > 0) {
		packets = r->processed - p->processed;
		pps = packets / period;
	}
	return pps;
}

static double calc_drop(struct datarec *r, struct datarec *p, double period)
{
	__u64 packets = 0;
	double pps = 0;

	if (period > 0) {
		packets = r->dropped - p->dropped;
		pps = packets / period;
	}
	return pps;
}

static double calc_info(struct datarec *r, struct datarec *p, double period)
{
	__u64 packets = 0;
	double pps = 0;

	if (period > 0) {
		packets = r->info - p->info;
		pps = packets / period;
	}
	return pps;
}

static double calc_err(struct datarec *r, struct datarec *p, double period)
{
	__u64 packets = 0;
	double pps = 0;

	if (period > 0) {
		packets = r->err - p->err;
		pps = packets / period;
	}
	return pps;
}

static void stats_print(struct stats_record *stats_rec,
			struct stats_record *stats_prev,
			bool err_only)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	int rec_i = 0, i, to_cpu;
	double t = 0, pps = 0;

	/* Header */
	printf("%-15s %-7s %-12s %-12s %-9s\n",
	       "XDP-event", "CPU:to", "pps", "drop-pps", "extra-info");

	/* tracepoint: xdp:xdp_redirect_* */
	if (err_only)
		rec_i = REDIR_ERROR;

	for (; rec_i < REDIR_RES_MAX; rec_i++) {
		struct record_u64 *rec, *prev;
		char *fmt1 = "%-15s %-7d %'-12.0f %'-12.0f %s\n";
		char *fmt2 = "%-15s %-7s %'-12.0f %'-12.0f %s\n";

		rec  =  &stats_rec->xdp_redirect[rec_i];
		prev = &stats_prev->xdp_redirect[rec_i];
		t = calc_period_u64(rec, prev);

		for (i = 0; i < nr_cpus; i++) {
			struct u64rec *r = &rec->cpu[i];
			struct u64rec *p = &prev->cpu[i];

			pps = calc_pps_u64(r, p, t);
			if (pps > 0)
				printf(fmt1, "XDP_REDIRECT", i,
				       rec_i ? 0.0: pps, rec_i ? pps : 0.0,
				       err2str(rec_i));
		}
		pps = calc_pps_u64(&rec->total, &prev->total, t);
		printf(fmt2, "XDP_REDIRECT", "total",
		       rec_i ? 0.0: pps, rec_i ? pps : 0.0, err2str(rec_i));
	}

	/* tracepoint: xdp:xdp_exception */
	for (rec_i = 0; rec_i < XDP_ACTION_MAX; rec_i++) {
		struct record_u64 *rec, *prev;
		char *fmt1 = "%-15s %-7d %'-12.0f %'-12.0f %s\n";
		char *fmt2 = "%-15s %-7s %'-12.0f %'-12.0f %s\n";

		rec  =  &stats_rec->xdp_exception[rec_i];
		prev = &stats_prev->xdp_exception[rec_i];
		t = calc_period_u64(rec, prev);

		for (i = 0; i < nr_cpus; i++) {
			struct u64rec *r = &rec->cpu[i];
			struct u64rec *p = &prev->cpu[i];

			pps = calc_pps_u64(r, p, t);
			if (pps > 0)
				printf(fmt1, "Exception", i,
				       0.0, pps, action2str(rec_i));
		}
		pps = calc_pps_u64(&rec->total, &prev->total, t);
		if (pps > 0)
			printf(fmt2, "Exception", "total",
			       0.0, pps, action2str(rec_i));
	}

	/* cpumap enqueue stats */
	for (to_cpu = 0; to_cpu < MAX_CPUS; to_cpu++) {
		char *fmt1 = "%-15s %3d:%-3d %'-12.0f %'-12.0f %'-10.2f %s\n";
		char *fmt2 = "%-15s %3s:%-3d %'-12.0f %'-12.0f %'-10.2f %s\n";
		struct record *rec, *prev;
		char *info_str = "";
		double drop, info;

		rec  =  &stats_rec->xdp_cpumap_enqueue[to_cpu];
		prev = &stats_prev->xdp_cpumap_enqueue[to_cpu];
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop(r, p, t);
			info = calc_info(r, p, t);
			if (info > 0) {
				info_str = "bulk-average";
				info = pps / info; /* calc average bulk size */
			}
			if (pps > 0)
				printf(fmt1, "cpumap-enqueue",
				       i, to_cpu, pps, drop, info, info_str);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		if (pps > 0) {
			drop = calc_drop(&rec->total, &prev->total, t);
			info = calc_info(&rec->total, &prev->total, t);
			if (info > 0) {
				info_str = "bulk-average";
				info = pps / info; /* calc average bulk size */
			}
			printf(fmt2, "cpumap-enqueue",
			       "sum", to_cpu, pps, drop, info, info_str);
		}
	}

	/* cpumap kthread stats */
	{
		char *fmt1 = "%-15s %-7d %'-12.0f %'-12.0f %'-10.0f %s\n";
		char *fmt2 = "%-15s %-7s %'-12.0f %'-12.0f %'-10.0f %s\n";
		struct record *rec, *prev;
		double drop, info;
		char *i_str = "";

		rec  =  &stats_rec->xdp_cpumap_kthread;
		prev = &stats_prev->xdp_cpumap_kthread;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop(r, p, t);
			info = calc_info(r, p, t);
			if (info > 0)
				i_str = "sched";
			if (pps > 0 || drop > 0)
				printf(fmt1, "cpumap-kthread",
				       i, pps, drop, info, i_str);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop(&rec->total, &prev->total, t);
		info = calc_info(&rec->total, &prev->total, t);
		if (info > 0)
			i_str = "sched-sum";
		printf(fmt2, "cpumap-kthread", "total", pps, drop, info, i_str);
	}

	/* devmap ndo_xdp_xmit stats */
	{
		char *fmt1 = "%-15s %-7d %'-12.0f %'-12.0f %'-10.2f %s %s\n";
		char *fmt2 = "%-15s %-7s %'-12.0f %'-12.0f %'-10.2f %s %s\n";
		struct record *rec, *prev;
		double drop, info, err;
		char *i_str = "";
		char *err_str = "";

		rec  =  &stats_rec->xdp_devmap_xmit;
		prev = &stats_prev->xdp_devmap_xmit;
		t = calc_period(rec, prev);
		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];

			pps  = calc_pps(r, p, t);
			drop = calc_drop(r, p, t);
			info = calc_info(r, p, t);
			err  = calc_err(r, p, t);
			if (info > 0) {
				i_str = "bulk-average";
				info = (pps+drop) / info; /* calc avg bulk */
			}
			if (err > 0)
				err_str = "drv-err";
			if (pps > 0 || drop > 0)
				printf(fmt1, "devmap-xmit",
				       i, pps, drop, info, i_str, err_str);
		}
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop(&rec->total, &prev->total, t);
		info = calc_info(&rec->total, &prev->total, t);
		err  = calc_err(&rec->total, &prev->total, t);
		if (info > 0) {
			i_str = "bulk-average";
			info = (pps+drop) / info; /* calc avg bulk */
		}
		if (err > 0)
			err_str = "drv-err";
		printf(fmt2, "devmap-xmit", "total", pps, drop,
		       info, i_str, err_str);
	}

	printf("\n");
}

static bool stats_collect(struct stats_record *rec)
{
	int fd;
	int i;

	/* TODO: Detect if someone unloaded the perf event_fd's, as
	 * this can happen by someone running perf-record -e
	 */

	fd = bpf_map__fd(map_data[REDIRECT_ERR_CNT]);
	for (i = 0; i < REDIR_RES_MAX; i++)
		map_collect_record_u64(fd, i, &rec->xdp_redirect[i]);

	fd = bpf_map__fd(map_data[EXCEPTION_CNT]);
	for (i = 0; i < XDP_ACTION_MAX; i++) {
		map_collect_record_u64(fd, i, &rec->xdp_exception[i]);
	}

	fd = bpf_map__fd(map_data[CPUMAP_ENQUEUE_CNT]);
	for (i = 0; i < MAX_CPUS; i++)
		map_collect_record(fd, i, &rec->xdp_cpumap_enqueue[i]);

	fd = bpf_map__fd(map_data[CPUMAP_KTHREAD_CNT]);
	map_collect_record(fd, 0, &rec->xdp_cpumap_kthread);

	fd = bpf_map__fd(map_data[DEVMAP_XMIT_CNT]);
	map_collect_record(fd, 0, &rec->xdp_devmap_xmit);

	return true;
}

static void *alloc_rec_per_cpu(int record_size)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	void *array;

	array = calloc(nr_cpus, record_size);
	if (!array) {
		fprintf(stderr, "Mem alloc error (nr_cpus:%u)\n", nr_cpus);
		exit(EXIT_FAIL_MEM);
	}
	return array;
}

static struct stats_record *alloc_stats_record(void)
{
	struct stats_record *rec;
	int rec_sz;
	int i;

	/* Alloc main stats_record structure */
	rec = calloc(1, sizeof(*rec));
	if (!rec) {
		fprintf(stderr, "Mem alloc error\n");
		exit(EXIT_FAIL_MEM);
	}

	/* Alloc stats stored per CPU for each record */
	rec_sz = sizeof(struct u64rec);
	for (i = 0; i < REDIR_RES_MAX; i++)
		rec->xdp_redirect[i].cpu = alloc_rec_per_cpu(rec_sz);

	for (i = 0; i < XDP_ACTION_MAX; i++)
		rec->xdp_exception[i].cpu = alloc_rec_per_cpu(rec_sz);

	rec_sz = sizeof(struct datarec);
	rec->xdp_cpumap_kthread.cpu = alloc_rec_per_cpu(rec_sz);
	rec->xdp_devmap_xmit.cpu    = alloc_rec_per_cpu(rec_sz);

	for (i = 0; i < MAX_CPUS; i++)
		rec->xdp_cpumap_enqueue[i].cpu = alloc_rec_per_cpu(rec_sz);

	return rec;
}

static void free_stats_record(struct stats_record *r)
{
	int i;

	for (i = 0; i < REDIR_RES_MAX; i++)
		free(r->xdp_redirect[i].cpu);

	for (i = 0; i < XDP_ACTION_MAX; i++)
		free(r->xdp_exception[i].cpu);

	free(r->xdp_cpumap_kthread.cpu);
	free(r->xdp_devmap_xmit.cpu);

	for (i = 0; i < MAX_CPUS; i++)
		free(r->xdp_cpumap_enqueue[i].cpu);

	free(r);
}

/* Pointer swap trick */
static inline void swap(struct stats_record **a, struct stats_record **b)
{
	struct stats_record *tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static void stats_poll(int interval, bool err_only)
{
	struct stats_record *rec, *prev;

	rec  = alloc_stats_record();
	prev = alloc_stats_record();
	stats_collect(rec);

	if (err_only)
		printf("\n%s\n", __doc_err_only__);

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	/* Header */
	if (verbose)
		printf("\n%s", __doc__);

	/* TODO Need more advanced stats on error types */
	if (verbose) {
		printf(" - Stats map0: %s\n", bpf_map__name(map_data[0]));
		printf(" - Stats map1: %s\n", bpf_map__name(map_data[1]));
		printf("\n");
	}
	fflush(stdout);

	while (1) {
		swap(&prev, &rec);
		stats_collect(rec);
		stats_print(rec, prev, err_only);
		fflush(stdout);
		sleep(interval);
	}

	free_stats_record(rec);
	free_stats_record(prev);
}

static void print_bpf_prog_info(void)
{
	struct bpf_program *prog;
	struct bpf_map *map;
	int i = 0;

	/* Prog info */
	printf("Loaded BPF prog have %d bpf program(s)\n", tp_cnt);
	bpf_object__for_each_program(prog, obj) {
		printf(" - prog_fd[%d] = fd(%d)\n", i, bpf_program__fd(prog));
		i++;
	}

	i = 0;
	/* Maps info */
	printf("Loaded BPF prog have %d map(s)\n", map_cnt);
	bpf_object__for_each_map(map, obj) {
		const char *name = bpf_map__name(map);
		int fd		 = bpf_map__fd(map);

		printf(" - map_data[%d] = fd(%d) name:%s\n", i, fd, name);
		i++;
	}

	/* Event info */
	printf("Searching for (max:%d) event file descriptor(s)\n", tp_cnt);
	for (i = 0; i < tp_cnt; i++) {
		int fd = bpf_link__fd(tp_links[i]);

		if (fd != -1)
			printf(" - event_fd[%d] = fd(%d)\n", i, fd);
	}
}

int main(int argc, char **argv)
{
	struct bpf_program *prog;
	int longindex = 0, opt;
	int ret = EXIT_FAILURE;
	enum map_type type;
	char filename[256];

	/* Default settings: */
	bool errors_only = true;
	int interval = 2;

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hDSs:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'D':
			debug = true;
			break;
		case 'S':
			errors_only = false;
			break;
		case 's':
			interval = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv);
			return ret;
		}
	}

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Remove tracepoint program when program is interrupted or killed */
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		printf("ERROR: opening BPF object file failed\n");
		obj = NULL;
		goto cleanup;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		printf("ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	for (type = 0; type < NUM_MAP; type++) {
		map_data[type] =
			bpf_object__find_map_by_name(obj, map_type_strings[type]);

		if (libbpf_get_error(map_data[type])) {
			printf("ERROR: finding a map in obj file failed\n");
			goto cleanup;
		}
		map_cnt++;
	}

	bpf_object__for_each_program(prog, obj) {
		tp_links[tp_cnt] = bpf_program__attach(prog);
		if (libbpf_get_error(tp_links[tp_cnt])) {
			printf("ERROR: bpf_program__attach failed\n");
			tp_links[tp_cnt] = NULL;
			goto cleanup;
		}
		tp_cnt++;
	}

	if (debug) {
		print_bpf_prog_info();
	}

	/* Unload/stop tracepoint event by closing bpf_link's */
	if (errors_only) {
		/* The bpf_link[i] depend on the order of
		 * the functions was defined in _kern.c
		 */
		bpf_link__destroy(tp_links[2]);	/* tracepoint/xdp/xdp_redirect */
		tp_links[2] = NULL;

		bpf_link__destroy(tp_links[3]);	/* tracepoint/xdp/xdp_redirect_map */
		tp_links[3] = NULL;
	}

	stats_poll(interval, errors_only);

	ret = EXIT_SUCCESS;

cleanup:
	/* Detach tracepoints */
	while (tp_cnt)
		bpf_link__destroy(tp_links[--tp_cnt]);

	bpf_object__close(obj);
	return ret;
}
