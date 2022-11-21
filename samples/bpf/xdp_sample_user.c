// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/ethtool.h>
#include <linux/hashtable.h>
#include <linux/if_link.h>
#include <linux/jhash.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/sockios.h>
#include <locale.h>
#include <math.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/sysinfo.h>
#include <sys/timerfd.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "bpf_util.h"
#include "xdp_sample_user.h"

#define __sample_print(fmt, cond, ...)                                         \
	({                                                                     \
		if (cond)                                                      \
			printf(fmt, ##__VA_ARGS__);                            \
	})

#define print_always(fmt, ...) __sample_print(fmt, 1, ##__VA_ARGS__)
#define print_default(fmt, ...)                                                \
	__sample_print(fmt, sample_log_level & LL_DEFAULT, ##__VA_ARGS__)
#define __print_err(err, fmt, ...)                                             \
	({                                                                     \
		__sample_print(fmt, err > 0 || sample_log_level & LL_DEFAULT,  \
			       ##__VA_ARGS__);                                 \
		sample_err_exp = sample_err_exp ? true : err > 0;              \
	})
#define print_err(err, fmt, ...) __print_err(err, fmt, ##__VA_ARGS__)

#define __COLUMN(x) "%'10" x " %-13s"
#define FMT_COLUMNf __COLUMN(".0f")
#define FMT_COLUMNd __COLUMN("d")
#define FMT_COLUMNl __COLUMN("llu")
#define RX(rx) rx, "rx/s"
#define PPS(pps) pps, "pkt/s"
#define DROP(drop) drop, "drop/s"
#define ERR(err) err, "error/s"
#define HITS(hits) hits, "hit/s"
#define XMIT(xmit) xmit, "xmit/s"
#define PASS(pass) pass, "pass/s"
#define REDIR(redir) redir, "redir/s"
#define NANOSEC_PER_SEC 1000000000 /* 10^9 */

#define XDP_UNKNOWN (XDP_REDIRECT + 1)
#define XDP_ACTION_MAX (XDP_UNKNOWN + 1)
#define XDP_REDIRECT_ERR_MAX 7

enum map_type {
	MAP_RX,
	MAP_REDIRECT_ERR,
	MAP_CPUMAP_ENQUEUE,
	MAP_CPUMAP_KTHREAD,
	MAP_EXCEPTION,
	MAP_DEVMAP_XMIT,
	MAP_DEVMAP_XMIT_MULTI,
	NUM_MAP,
};

enum log_level {
	LL_DEFAULT = 1U << 0,
	LL_SIMPLE = 1U << 1,
	LL_DEBUG = 1U << 2,
};

struct record {
	__u64 timestamp;
	struct datarec total;
	struct datarec *cpu;
};

struct map_entry {
	struct hlist_node node;
	__u64 pair;
	struct record val;
};

struct stats_record {
	struct record rx_cnt;
	struct record redir_err[XDP_REDIRECT_ERR_MAX];
	struct record kthread;
	struct record exception[XDP_ACTION_MAX];
	struct record devmap_xmit;
	DECLARE_HASHTABLE(xmit_map, 5);
	struct record enq[];
};

struct sample_output {
	struct {
		__u64 rx;
		__u64 redir;
		__u64 drop;
		__u64 drop_xmit;
		__u64 err;
		__u64 xmit;
	} totals;
	struct {
		union {
			__u64 pps;
			__u64 num;
		};
		__u64 drop;
		__u64 err;
	} rx_cnt;
	struct {
		__u64 suc;
		__u64 err;
	} redir_cnt;
	struct {
		__u64 hits;
	} except_cnt;
	struct {
		__u64 pps;
		__u64 drop;
		__u64 err;
		double bavg;
	} xmit_cnt;
};

struct xdp_desc {
	int ifindex;
	__u32 prog_id;
	int flags;
} sample_xdp_progs[32];

struct datarec *sample_mmap[NUM_MAP];
struct bpf_map *sample_map[NUM_MAP];
size_t sample_map_count[NUM_MAP];
enum log_level sample_log_level;
struct sample_output sample_out;
unsigned long sample_interval;
bool sample_err_exp;
int sample_xdp_cnt;
int sample_n_cpus;
int sample_sig_fd;
int sample_mask;

static const char *xdp_redirect_err_names[XDP_REDIRECT_ERR_MAX] = {
	/* Key=1 keeps unknown errors */
	"Success",
	"Unknown",
	"EINVAL",
	"ENETDOWN",
	"EMSGSIZE",
	"EOPNOTSUPP",
	"ENOSPC",
};

/* Keyed from Unknown */
static const char *xdp_redirect_err_help[XDP_REDIRECT_ERR_MAX - 1] = {
	"Unknown error",
	"Invalid redirection",
	"Device being redirected to is down",
	"Packet length too large for device",
	"Operation not supported",
	"No space in ptr_ring of cpumap kthread",
};

static const char *xdp_action_names[XDP_ACTION_MAX] = {
	[XDP_ABORTED]  = "XDP_ABORTED",
	[XDP_DROP]     = "XDP_DROP",
	[XDP_PASS]     = "XDP_PASS",
	[XDP_TX]       = "XDP_TX",
	[XDP_REDIRECT] = "XDP_REDIRECT",
	[XDP_UNKNOWN]  = "XDP_UNKNOWN",
};

static __u64 gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		return UINT64_MAX;
	}
	return (__u64)t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
}

static const char *action2str(int action)
{
	if (action < XDP_ACTION_MAX)
		return xdp_action_names[action];
	return NULL;
}

static void sample_print_help(int mask)
{
	printf("Output format description\n\n"
	       "By default, redirect success statistics are disabled, use -s to enable.\n"
	       "The terse output mode is default, verbose mode can be activated using -v\n"
	       "Use SIGQUIT (Ctrl + \\) to switch the mode dynamically at runtime\n\n"
	       "Terse mode displays at most the following fields:\n"
	       "  rx/s        Number of packets received per second\n"
	       "  redir/s     Number of packets successfully redirected per second\n"
	       "  err,drop/s  Aggregated count of errors per second (including dropped packets)\n"
	       "  xmit/s      Number of packets transmitted on the output device per second\n\n"
	       "Output description for verbose mode:\n"
	       "  FIELD                 DESCRIPTION\n");

	if (mask & SAMPLE_RX_CNT) {
		printf("  receive\t\tDisplays the number of packets received & errors encountered\n"
		       " \t\t\tWhenever an error or packet drop occurs, details of per CPU error\n"
		       " \t\t\tand drop statistics will be expanded inline in terse mode.\n"
		       " \t\t\t\tpkt/s     - Packets received per second\n"
		       " \t\t\t\tdrop/s    - Packets dropped per second\n"
		       " \t\t\t\terror/s   - Errors encountered per second\n\n");
	}
	if (mask & (SAMPLE_REDIRECT_CNT | SAMPLE_REDIRECT_ERR_CNT)) {
		printf("  redirect\t\tDisplays the number of packets successfully redirected\n"
		       "  \t\t\tErrors encountered are expanded under redirect_err field\n"
		       "  \t\t\tNote that passing -s to enable it has a per packet overhead\n"
		       "  \t\t\t\tredir/s   - Packets redirected successfully per second\n\n"
		       "  redirect_err\t\tDisplays the number of packets that failed redirection\n"
		       "  \t\t\tThe errno is expanded under this field with per CPU count\n"
		       "  \t\t\tThe recognized errors are:\n");

		for (int i = 2; i < XDP_REDIRECT_ERR_MAX; i++)
			printf("\t\t\t  %s: %s\n", xdp_redirect_err_names[i],
			       xdp_redirect_err_help[i - 1]);

		printf("  \n\t\t\t\terror/s   - Packets that failed redirection per second\n\n");
	}

	if (mask & SAMPLE_CPUMAP_ENQUEUE_CNT) {
		printf("  enqueue to cpu N\tDisplays the number of packets enqueued to bulk queue of CPU N\n"
		       "  \t\t\tExpands to cpu:FROM->N to display enqueue stats for each CPU enqueuing to CPU N\n"
		       "  \t\t\tReceived packets can be associated with the CPU redirect program is enqueuing \n"
		       "  \t\t\tpackets to.\n"
		       "  \t\t\t\tpkt/s    - Packets enqueued per second from other CPU to CPU N\n"
		       "  \t\t\t\tdrop/s   - Packets dropped when trying to enqueue to CPU N\n"
		       "  \t\t\t\tbulk-avg - Average number of packets processed for each event\n\n");
	}

	if (mask & SAMPLE_CPUMAP_KTHREAD_CNT) {
		printf("  kthread\t\tDisplays the number of packets processed in CPUMAP kthread for each CPU\n"
		       "  \t\t\tPackets consumed from ptr_ring in kthread, and its xdp_stats (after calling \n"
		       "  \t\t\tCPUMAP bpf prog) are expanded below this. xdp_stats are expanded as a total and\n"
		       "  \t\t\tthen per-CPU to associate it to each CPU's pinned CPUMAP kthread.\n"
		       "  \t\t\t\tpkt/s    - Packets consumed per second from ptr_ring\n"
		       "  \t\t\t\tdrop/s   - Packets dropped per second in kthread\n"
		       "  \t\t\t\tsched    - Number of times kthread called schedule()\n\n"
		       "  \t\t\txdp_stats (also expands to per-CPU counts)\n"
		       "  \t\t\t\tpass/s  - XDP_PASS count for CPUMAP program execution\n"
		       "  \t\t\t\tdrop/s  - XDP_DROP count for CPUMAP program execution\n"
		       "  \t\t\t\tredir/s - XDP_REDIRECT count for CPUMAP program execution\n\n");
	}

	if (mask & SAMPLE_EXCEPTION_CNT) {
		printf("  xdp_exception\t\tDisplays xdp_exception tracepoint events\n"
		       "  \t\t\tThis can occur due to internal driver errors, unrecognized\n"
		       "  \t\t\tXDP actions and due to explicit user trigger by use of XDP_ABORTED\n"
		       "  \t\t\tEach action is expanded below this field with its count\n"
		       "  \t\t\t\thit/s     - Number of times the tracepoint was hit per second\n\n");
	}

	if (mask & SAMPLE_DEVMAP_XMIT_CNT) {
		printf("  devmap_xmit\t\tDisplays devmap_xmit tracepoint events\n"
		       "  \t\t\tThis tracepoint is invoked for successful transmissions on output\n"
		       "  \t\t\tdevice but these statistics are not available for generic XDP mode,\n"
		       "  \t\t\thence they will be omitted from the output when using SKB mode\n"
		       "  \t\t\t\txmit/s    - Number of packets that were transmitted per second\n"
		       "  \t\t\t\tdrop/s    - Number of packets that failed transmissions per second\n"
		       "  \t\t\t\tdrv_err/s - Number of internal driver errors per second\n"
		       "  \t\t\t\tbulk-avg  - Average number of packets processed for each event\n\n");
	}
}

void sample_usage(char *argv[], const struct option *long_options,
		  const char *doc, int mask, bool error)
{
	int i;

	if (!error)
		sample_print_help(mask);

	printf("\n%s\nOption for %s:\n", doc, argv[0]);
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-15s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value: %d)",
			       *long_options[i].flag);
		else
			printf("\t short-option: -%c", long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

static struct datarec *alloc_record_per_cpu(void)
{
	unsigned int nr_cpus = libbpf_num_possible_cpus();
	struct datarec *array;

	array = calloc(nr_cpus, sizeof(*array));
	if (!array) {
		fprintf(stderr, "Failed to allocate memory (nr_cpus: %u)\n",
			nr_cpus);
		return NULL;
	}
	return array;
}

static int map_entry_init(struct map_entry *e, __u64 pair)
{
	e->pair = pair;
	INIT_HLIST_NODE(&e->node);
	e->val.timestamp = gettime();
	e->val.cpu = alloc_record_per_cpu();
	if (!e->val.cpu)
		return -ENOMEM;
	return 0;
}

static void map_collect_percpu(struct datarec *values, struct record *rec)
{
	/* For percpu maps, userspace gets a value per possible CPU */
	unsigned int nr_cpus = libbpf_num_possible_cpus();
	__u64 sum_xdp_redirect = 0;
	__u64 sum_processed = 0;
	__u64 sum_xdp_pass = 0;
	__u64 sum_xdp_drop = 0;
	__u64 sum_dropped = 0;
	__u64 sum_issue = 0;
	int i;

	/* Get time as close as possible to reading map contents */
	rec->timestamp = gettime();

	/* Record and sum values from each CPU */
	for (i = 0; i < nr_cpus; i++) {
		rec->cpu[i].processed = READ_ONCE(values[i].processed);
		rec->cpu[i].dropped = READ_ONCE(values[i].dropped);
		rec->cpu[i].issue = READ_ONCE(values[i].issue);
		rec->cpu[i].xdp_pass = READ_ONCE(values[i].xdp_pass);
		rec->cpu[i].xdp_drop = READ_ONCE(values[i].xdp_drop);
		rec->cpu[i].xdp_redirect = READ_ONCE(values[i].xdp_redirect);

		sum_processed += rec->cpu[i].processed;
		sum_dropped += rec->cpu[i].dropped;
		sum_issue += rec->cpu[i].issue;
		sum_xdp_pass += rec->cpu[i].xdp_pass;
		sum_xdp_drop += rec->cpu[i].xdp_drop;
		sum_xdp_redirect += rec->cpu[i].xdp_redirect;
	}

	rec->total.processed = sum_processed;
	rec->total.dropped = sum_dropped;
	rec->total.issue = sum_issue;
	rec->total.xdp_pass = sum_xdp_pass;
	rec->total.xdp_drop = sum_xdp_drop;
	rec->total.xdp_redirect = sum_xdp_redirect;
}

static int map_collect_percpu_devmap(int map_fd, struct stats_record *rec)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u32 batch, count = 32;
	struct datarec *values;
	bool init = false;
	__u64 *keys;
	int i, ret;

	keys = calloc(count, sizeof(__u64));
	if (!keys)
		return -ENOMEM;
	values = calloc(count * nr_cpus, sizeof(struct datarec));
	if (!values) {
		free(keys);
		return -ENOMEM;
	}

	for (;;) {
		bool exit = false;

		ret = bpf_map_lookup_batch(map_fd, init ? &batch : NULL, &batch,
					   keys, values, &count, NULL);
		if (ret < 0 && errno != ENOENT)
			break;
		if (errno == ENOENT)
			exit = true;

		init = true;
		for (i = 0; i < count; i++) {
			struct map_entry *e, *x = NULL;
			__u64 pair = keys[i];
			struct datarec *arr;

			arr = &values[i * nr_cpus];
			hash_for_each_possible(rec->xmit_map, e, node, pair) {
				if (e->pair == pair) {
					x = e;
					break;
				}
			}
			if (!x) {
				x = calloc(1, sizeof(*x));
				if (!x)
					goto cleanup;
				if (map_entry_init(x, pair) < 0) {
					free(x);
					goto cleanup;
				}
				hash_add(rec->xmit_map, &x->node, pair);
			}
			map_collect_percpu(arr, &x->val);
		}

		if (exit)
			break;
		count = 32;
	}

	free(values);
	free(keys);
	return 0;
cleanup:
	free(values);
	free(keys);
	return -ENOMEM;
}

static struct stats_record *alloc_stats_record(void)
{
	struct stats_record *rec;
	int i;

	rec = calloc(1, sizeof(*rec) + sample_n_cpus * sizeof(struct record));
	if (!rec) {
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	if (sample_mask & SAMPLE_RX_CNT) {
		rec->rx_cnt.cpu = alloc_record_per_cpu();
		if (!rec->rx_cnt.cpu) {
			fprintf(stderr,
				"Failed to allocate rx_cnt per-CPU array\n");
			goto end_rec;
		}
	}
	if (sample_mask & (SAMPLE_REDIRECT_CNT | SAMPLE_REDIRECT_ERR_CNT)) {
		for (i = 0; i < XDP_REDIRECT_ERR_MAX; i++) {
			rec->redir_err[i].cpu = alloc_record_per_cpu();
			if (!rec->redir_err[i].cpu) {
				fprintf(stderr,
					"Failed to allocate redir_err per-CPU array for "
					"\"%s\" case\n",
					xdp_redirect_err_names[i]);
				while (i--)
					free(rec->redir_err[i].cpu);
				goto end_rx_cnt;
			}
		}
	}
	if (sample_mask & SAMPLE_CPUMAP_KTHREAD_CNT) {
		rec->kthread.cpu = alloc_record_per_cpu();
		if (!rec->kthread.cpu) {
			fprintf(stderr,
				"Failed to allocate kthread per-CPU array\n");
			goto end_redir;
		}
	}
	if (sample_mask & SAMPLE_EXCEPTION_CNT) {
		for (i = 0; i < XDP_ACTION_MAX; i++) {
			rec->exception[i].cpu = alloc_record_per_cpu();
			if (!rec->exception[i].cpu) {
				fprintf(stderr,
					"Failed to allocate exception per-CPU array for "
					"\"%s\" case\n",
					action2str(i));
				while (i--)
					free(rec->exception[i].cpu);
				goto end_kthread;
			}
		}
	}
	if (sample_mask & SAMPLE_DEVMAP_XMIT_CNT) {
		rec->devmap_xmit.cpu = alloc_record_per_cpu();
		if (!rec->devmap_xmit.cpu) {
			fprintf(stderr,
				"Failed to allocate devmap_xmit per-CPU array\n");
			goto end_exception;
		}
	}
	if (sample_mask & SAMPLE_DEVMAP_XMIT_CNT_MULTI)
		hash_init(rec->xmit_map);
	if (sample_mask & SAMPLE_CPUMAP_ENQUEUE_CNT) {
		for (i = 0; i < sample_n_cpus; i++) {
			rec->enq[i].cpu = alloc_record_per_cpu();
			if (!rec->enq[i].cpu) {
				fprintf(stderr,
					"Failed to allocate enqueue per-CPU array for "
					"CPU %d\n",
					i);
				while (i--)
					free(rec->enq[i].cpu);
				goto end_devmap_xmit;
			}
		}
	}

	return rec;

end_devmap_xmit:
	free(rec->devmap_xmit.cpu);
end_exception:
	for (i = 0; i < XDP_ACTION_MAX; i++)
		free(rec->exception[i].cpu);
end_kthread:
	free(rec->kthread.cpu);
end_redir:
	for (i = 0; i < XDP_REDIRECT_ERR_MAX; i++)
		free(rec->redir_err[i].cpu);
end_rx_cnt:
	free(rec->rx_cnt.cpu);
end_rec:
	free(rec);
	return NULL;
}

static void free_stats_record(struct stats_record *r)
{
	struct hlist_node *tmp;
	struct map_entry *e;
	int i;

	for (i = 0; i < sample_n_cpus; i++)
		free(r->enq[i].cpu);
	hash_for_each_safe(r->xmit_map, i, tmp, e, node) {
		hash_del(&e->node);
		free(e->val.cpu);
		free(e);
	}
	free(r->devmap_xmit.cpu);
	for (i = 0; i < XDP_ACTION_MAX; i++)
		free(r->exception[i].cpu);
	free(r->kthread.cpu);
	for (i = 0; i < XDP_REDIRECT_ERR_MAX; i++)
		free(r->redir_err[i].cpu);
	free(r->rx_cnt.cpu);
	free(r);
}

static double calc_period(struct record *r, struct record *p)
{
	double period_ = 0;
	__u64 period = 0;

	period = r->timestamp - p->timestamp;
	if (period > 0)
		period_ = ((double)period / NANOSEC_PER_SEC);

	return period_;
}

static double sample_round(double val)
{
	if (val - floor(val) < 0.5)
		return floor(val);
	return ceil(val);
}

static __u64 calc_pps(struct datarec *r, struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->processed - p->processed;
		pps = sample_round(packets / period_);
	}
	return pps;
}

static __u64 calc_drop_pps(struct datarec *r, struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->dropped - p->dropped;
		pps = sample_round(packets / period_);
	}
	return pps;
}

static __u64 calc_errs_pps(struct datarec *r, struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->issue - p->issue;
		pps = sample_round(packets / period_);
	}
	return pps;
}

static __u64 calc_info_pps(struct datarec *r, struct datarec *p, double period_)
{
	__u64 packets = 0;
	__u64 pps = 0;

	if (period_ > 0) {
		packets = r->info - p->info;
		pps = sample_round(packets / period_);
	}
	return pps;
}

static void calc_xdp_pps(struct datarec *r, struct datarec *p, double *xdp_pass,
			 double *xdp_drop, double *xdp_redirect, double period_)
{
	*xdp_pass = 0, *xdp_drop = 0, *xdp_redirect = 0;
	if (period_ > 0) {
		*xdp_redirect = (r->xdp_redirect - p->xdp_redirect) / period_;
		*xdp_pass = (r->xdp_pass - p->xdp_pass) / period_;
		*xdp_drop = (r->xdp_drop - p->xdp_drop) / period_;
	}
}

static void stats_get_rx_cnt(struct stats_record *stats_rec,
			     struct stats_record *stats_prev,
			     unsigned int nr_cpus, struct sample_output *out)
{
	struct record *rec, *prev;
	double t, pps, drop, err;
	int i;

	rec = &stats_rec->rx_cnt;
	prev = &stats_prev->rx_cnt;
	t = calc_period(rec, prev);

	for (i = 0; i < nr_cpus; i++) {
		struct datarec *r = &rec->cpu[i];
		struct datarec *p = &prev->cpu[i];
		char str[64];

		pps = calc_pps(r, p, t);
		drop = calc_drop_pps(r, p, t);
		err = calc_errs_pps(r, p, t);
		if (!pps && !drop && !err)
			continue;

		snprintf(str, sizeof(str), "cpu:%d", i);
		print_default("    %-18s " FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf
			      "\n",
			      str, PPS(pps), DROP(drop), ERR(err));
	}

	if (out) {
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		err = calc_errs_pps(&rec->total, &prev->total, t);

		out->rx_cnt.pps = pps;
		out->rx_cnt.drop = drop;
		out->rx_cnt.err = err;
		out->totals.rx += pps;
		out->totals.drop += drop;
		out->totals.err += err;
	}
}

static void stats_get_cpumap_enqueue(struct stats_record *stats_rec,
				     struct stats_record *stats_prev,
				     unsigned int nr_cpus)
{
	struct record *rec, *prev;
	double t, pps, drop, err;
	int i, to_cpu;

	/* cpumap enqueue stats */
	for (to_cpu = 0; to_cpu < sample_n_cpus; to_cpu++) {
		rec = &stats_rec->enq[to_cpu];
		prev = &stats_prev->enq[to_cpu];
		t = calc_period(rec, prev);

		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		err = calc_errs_pps(&rec->total, &prev->total, t);

		if (pps > 0 || drop > 0) {
			char str[64];

			snprintf(str, sizeof(str), "enqueue to cpu %d", to_cpu);

			if (err > 0)
				err = pps / err; /* calc average bulk size */

			print_err(drop,
				  "  %-20s " FMT_COLUMNf FMT_COLUMNf __COLUMN(
					  ".2f") "\n",
				  str, PPS(pps), DROP(drop), err, "bulk-avg");
		}

		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];
			char str[64];

			pps = calc_pps(r, p, t);
			drop = calc_drop_pps(r, p, t);
			err = calc_errs_pps(r, p, t);
			if (!pps && !drop && !err)
				continue;

			snprintf(str, sizeof(str), "cpu:%d->%d", i, to_cpu);
			if (err > 0)
				err = pps / err; /* calc average bulk size */
			print_default(
				"    %-18s " FMT_COLUMNf FMT_COLUMNf __COLUMN(
					".2f") "\n",
				str, PPS(pps), DROP(drop), err, "bulk-avg");
		}
	}
}

static void stats_get_cpumap_remote(struct stats_record *stats_rec,
				    struct stats_record *stats_prev,
				    unsigned int nr_cpus)
{
	double xdp_pass, xdp_drop, xdp_redirect;
	struct record *rec, *prev;
	double t;
	int i;

	rec = &stats_rec->kthread;
	prev = &stats_prev->kthread;
	t = calc_period(rec, prev);

	calc_xdp_pps(&rec->total, &prev->total, &xdp_pass, &xdp_drop,
		     &xdp_redirect, t);
	if (xdp_pass || xdp_drop || xdp_redirect) {
		print_err(xdp_drop,
			  "    %-18s " FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf "\n",
			  "xdp_stats", PASS(xdp_pass), DROP(xdp_drop),
			  REDIR(xdp_redirect));
	}

	for (i = 0; i < nr_cpus; i++) {
		struct datarec *r = &rec->cpu[i];
		struct datarec *p = &prev->cpu[i];
		char str[64];

		calc_xdp_pps(r, p, &xdp_pass, &xdp_drop, &xdp_redirect, t);
		if (!xdp_pass && !xdp_drop && !xdp_redirect)
			continue;

		snprintf(str, sizeof(str), "cpu:%d", i);
		print_default("      %-16s " FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf
			      "\n",
			      str, PASS(xdp_pass), DROP(xdp_drop),
			      REDIR(xdp_redirect));
	}
}

static void stats_get_cpumap_kthread(struct stats_record *stats_rec,
				     struct stats_record *stats_prev,
				     unsigned int nr_cpus)
{
	struct record *rec, *prev;
	double t, pps, drop, err;
	int i;

	rec = &stats_rec->kthread;
	prev = &stats_prev->kthread;
	t = calc_period(rec, prev);

	pps = calc_pps(&rec->total, &prev->total, t);
	drop = calc_drop_pps(&rec->total, &prev->total, t);
	err = calc_errs_pps(&rec->total, &prev->total, t);

	print_err(drop, "  %-20s " FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf "\n",
		  pps ? "kthread total" : "kthread", PPS(pps), DROP(drop), err,
		  "sched");

	for (i = 0; i < nr_cpus; i++) {
		struct datarec *r = &rec->cpu[i];
		struct datarec *p = &prev->cpu[i];
		char str[64];

		pps = calc_pps(r, p, t);
		drop = calc_drop_pps(r, p, t);
		err = calc_errs_pps(r, p, t);
		if (!pps && !drop && !err)
			continue;

		snprintf(str, sizeof(str), "cpu:%d", i);
		print_default("    %-18s " FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf
			      "\n",
			      str, PPS(pps), DROP(drop), err, "sched");
	}
}

static void stats_get_redirect_cnt(struct stats_record *stats_rec,
				   struct stats_record *stats_prev,
				   unsigned int nr_cpus,
				   struct sample_output *out)
{
	struct record *rec, *prev;
	double t, pps;
	int i;

	rec = &stats_rec->redir_err[0];
	prev = &stats_prev->redir_err[0];
	t = calc_period(rec, prev);
	for (i = 0; i < nr_cpus; i++) {
		struct datarec *r = &rec->cpu[i];
		struct datarec *p = &prev->cpu[i];
		char str[64];

		pps = calc_pps(r, p, t);
		if (!pps)
			continue;

		snprintf(str, sizeof(str), "cpu:%d", i);
		print_default("    %-18s " FMT_COLUMNf "\n", str, REDIR(pps));
	}

	if (out) {
		pps = calc_pps(&rec->total, &prev->total, t);
		out->redir_cnt.suc = pps;
		out->totals.redir += pps;
	}
}

static void stats_get_redirect_err_cnt(struct stats_record *stats_rec,
				       struct stats_record *stats_prev,
				       unsigned int nr_cpus,
				       struct sample_output *out)
{
	struct record *rec, *prev;
	double t, drop, sum = 0;
	int rec_i, i;

	for (rec_i = 1; rec_i < XDP_REDIRECT_ERR_MAX; rec_i++) {
		char str[64];

		rec = &stats_rec->redir_err[rec_i];
		prev = &stats_prev->redir_err[rec_i];
		t = calc_period(rec, prev);

		drop = calc_drop_pps(&rec->total, &prev->total, t);
		if (drop > 0 && !out) {
			snprintf(str, sizeof(str),
				 sample_log_level & LL_DEFAULT ? "%s total" :
								       "%s",
				 xdp_redirect_err_names[rec_i]);
			print_err(drop, "    %-18s " FMT_COLUMNf "\n", str,
				  ERR(drop));
		}

		for (i = 0; i < nr_cpus; i++) {
			struct datarec *r = &rec->cpu[i];
			struct datarec *p = &prev->cpu[i];
			double drop;

			drop = calc_drop_pps(r, p, t);
			if (!drop)
				continue;

			snprintf(str, sizeof(str), "cpu:%d", i);
			print_default("       %-16s" FMT_COLUMNf "\n", str,
				      ERR(drop));
		}

		sum += drop;
	}

	if (out) {
		out->redir_cnt.err = sum;
		out->totals.err += sum;
	}
}

static void stats_get_exception_cnt(struct stats_record *stats_rec,
				    struct stats_record *stats_prev,
				    unsigned int nr_cpus,
				    struct sample_output *out)
{
	double t, drop, sum = 0;
	struct record *rec, *prev;
	int rec_i, i;

	for (rec_i = 0; rec_i < XDP_ACTION_MAX; rec_i++) {
		rec = &stats_rec->exception[rec_i];
		prev = &stats_prev->exception[rec_i];
		t = calc_period(rec, prev);

		drop = calc_drop_pps(&rec->total, &prev->total, t);
		/* Fold out errors after heading */
		sum += drop;

		if (drop > 0 && !out) {
			print_always("    %-18s " FMT_COLUMNf "\n",
				     action2str(rec_i), ERR(drop));

			for (i = 0; i < nr_cpus; i++) {
				struct datarec *r = &rec->cpu[i];
				struct datarec *p = &prev->cpu[i];
				char str[64];
				double drop;

				drop = calc_drop_pps(r, p, t);
				if (!drop)
					continue;

				snprintf(str, sizeof(str), "cpu:%d", i);
				print_default("       %-16s" FMT_COLUMNf "\n",
					      str, ERR(drop));
			}
		}
	}

	if (out) {
		out->except_cnt.hits = sum;
		out->totals.err += sum;
	}
}

static void stats_get_devmap_xmit(struct stats_record *stats_rec,
				  struct stats_record *stats_prev,
				  unsigned int nr_cpus,
				  struct sample_output *out)
{
	double pps, drop, info, err;
	struct record *rec, *prev;
	double t;
	int i;

	rec = &stats_rec->devmap_xmit;
	prev = &stats_prev->devmap_xmit;
	t = calc_period(rec, prev);
	for (i = 0; i < nr_cpus; i++) {
		struct datarec *r = &rec->cpu[i];
		struct datarec *p = &prev->cpu[i];
		char str[64];

		pps = calc_pps(r, p, t);
		drop = calc_drop_pps(r, p, t);
		err = calc_errs_pps(r, p, t);

		if (!pps && !drop && !err)
			continue;

		snprintf(str, sizeof(str), "cpu:%d", i);
		info = calc_info_pps(r, p, t);
		if (info > 0)
			info = (pps + drop) / info; /* calc avg bulk */
		print_default("     %-18s" FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf
				      __COLUMN(".2f") "\n",
			      str, XMIT(pps), DROP(drop), err, "drv_err/s",
			      info, "bulk-avg");
	}
	if (out) {
		pps = calc_pps(&rec->total, &prev->total, t);
		drop = calc_drop_pps(&rec->total, &prev->total, t);
		info = calc_info_pps(&rec->total, &prev->total, t);
		if (info > 0)
			info = (pps + drop) / info; /* calc avg bulk */
		err = calc_errs_pps(&rec->total, &prev->total, t);

		out->xmit_cnt.pps = pps;
		out->xmit_cnt.drop = drop;
		out->xmit_cnt.bavg = info;
		out->xmit_cnt.err = err;
		out->totals.xmit += pps;
		out->totals.drop_xmit += drop;
		out->totals.err += err;
	}
}

static void stats_get_devmap_xmit_multi(struct stats_record *stats_rec,
					struct stats_record *stats_prev,
					unsigned int nr_cpus,
					struct sample_output *out,
					bool xmit_total)
{
	double pps, drop, info, err;
	struct map_entry *entry;
	struct record *r, *p;
	double t;
	int bkt;

	hash_for_each(stats_rec->xmit_map, bkt, entry, node) {
		struct map_entry *e, *x = NULL;
		char ifname_from[IFNAMSIZ];
		char ifname_to[IFNAMSIZ];
		const char *fstr, *tstr;
		unsigned long prev_time;
		struct record beg = {};
		__u32 from_idx, to_idx;
		char str[128];
		__u64 pair;
		int i;

		prev_time = sample_interval * NANOSEC_PER_SEC;

		pair = entry->pair;
		from_idx = pair >> 32;
		to_idx = pair & 0xFFFFFFFF;

		r = &entry->val;
		beg.timestamp = r->timestamp - prev_time;

		/* Find matching entry from stats_prev map */
		hash_for_each_possible(stats_prev->xmit_map, e, node, pair) {
			if (e->pair == pair) {
				x = e;
				break;
			}
		}
		if (x)
			p = &x->val;
		else
			p = &beg;
		t = calc_period(r, p);
		pps = calc_pps(&r->total, &p->total, t);
		drop = calc_drop_pps(&r->total, &p->total, t);
		info = calc_info_pps(&r->total, &p->total, t);
		if (info > 0)
			info = (pps + drop) / info; /* calc avg bulk */
		err = calc_errs_pps(&r->total, &p->total, t);

		if (out) {
			/* We are responsible for filling out totals */
			out->totals.xmit += pps;
			out->totals.drop_xmit += drop;
			out->totals.err += err;
			continue;
		}

		fstr = tstr = NULL;
		if (if_indextoname(from_idx, ifname_from))
			fstr = ifname_from;
		if (if_indextoname(to_idx, ifname_to))
			tstr = ifname_to;

		snprintf(str, sizeof(str), "xmit %s->%s", fstr ?: "?",
			 tstr ?: "?");
		/* Skip idle streams of redirection */
		if (pps || drop || err) {
			print_err(drop,
				  "  %-20s " FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf
				  __COLUMN(".2f") "\n", str, XMIT(pps), DROP(drop),
				  err, "drv_err/s", info, "bulk-avg");
		}

		for (i = 0; i < nr_cpus; i++) {
			struct datarec *rc = &r->cpu[i];
			struct datarec *pc, p_beg = {};
			char str[64];

			pc = p == &beg ? &p_beg : &p->cpu[i];

			pps = calc_pps(rc, pc, t);
			drop = calc_drop_pps(rc, pc, t);
			err = calc_errs_pps(rc, pc, t);

			if (!pps && !drop && !err)
				continue;

			snprintf(str, sizeof(str), "cpu:%d", i);
			info = calc_info_pps(rc, pc, t);
			if (info > 0)
				info = (pps + drop) / info; /* calc avg bulk */

			print_default("     %-18s" FMT_COLUMNf FMT_COLUMNf FMT_COLUMNf
				      __COLUMN(".2f") "\n", str, XMIT(pps),
				      DROP(drop), err, "drv_err/s", info, "bulk-avg");
		}
	}
}

static void stats_print(const char *prefix, int mask, struct stats_record *r,
			struct stats_record *p, struct sample_output *out)
{
	int nr_cpus = libbpf_num_possible_cpus();
	const char *str;

	print_always("%-23s", prefix ?: "Summary");
	if (mask & SAMPLE_RX_CNT)
		print_always(FMT_COLUMNl, RX(out->totals.rx));
	if (mask & SAMPLE_REDIRECT_CNT)
		print_always(FMT_COLUMNl, REDIR(out->totals.redir));
	printf(FMT_COLUMNl,
	       out->totals.err + out->totals.drop + out->totals.drop_xmit,
	       "err,drop/s");
	if (mask & SAMPLE_DEVMAP_XMIT_CNT ||
	    mask & SAMPLE_DEVMAP_XMIT_CNT_MULTI)
		printf(FMT_COLUMNl, XMIT(out->totals.xmit));
	printf("\n");

	if (mask & SAMPLE_RX_CNT) {
		str = (sample_log_level & LL_DEFAULT) && out->rx_cnt.pps ?
				    "receive total" :
				    "receive";
		print_err((out->rx_cnt.err || out->rx_cnt.drop),
			  "  %-20s " FMT_COLUMNl FMT_COLUMNl FMT_COLUMNl "\n",
			  str, PPS(out->rx_cnt.pps), DROP(out->rx_cnt.drop),
			  ERR(out->rx_cnt.err));

		stats_get_rx_cnt(r, p, nr_cpus, NULL);
	}

	if (mask & SAMPLE_CPUMAP_ENQUEUE_CNT)
		stats_get_cpumap_enqueue(r, p, nr_cpus);

	if (mask & SAMPLE_CPUMAP_KTHREAD_CNT) {
		stats_get_cpumap_kthread(r, p, nr_cpus);
		stats_get_cpumap_remote(r, p, nr_cpus);
	}

	if (mask & SAMPLE_REDIRECT_CNT) {
		str = out->redir_cnt.suc ? "redirect total" : "redirect";
		print_default("  %-20s " FMT_COLUMNl "\n", str,
			      REDIR(out->redir_cnt.suc));

		stats_get_redirect_cnt(r, p, nr_cpus, NULL);
	}

	if (mask & SAMPLE_REDIRECT_ERR_CNT) {
		str = (sample_log_level & LL_DEFAULT) && out->redir_cnt.err ?
				    "redirect_err total" :
				    "redirect_err";
		print_err(out->redir_cnt.err, "  %-20s " FMT_COLUMNl "\n", str,
			  ERR(out->redir_cnt.err));

		stats_get_redirect_err_cnt(r, p, nr_cpus, NULL);
	}

	if (mask & SAMPLE_EXCEPTION_CNT) {
		str = out->except_cnt.hits ? "xdp_exception total" :
						   "xdp_exception";

		print_err(out->except_cnt.hits, "  %-20s " FMT_COLUMNl "\n", str,
			  HITS(out->except_cnt.hits));

		stats_get_exception_cnt(r, p, nr_cpus, NULL);
	}

	if (mask & SAMPLE_DEVMAP_XMIT_CNT) {
		str = (sample_log_level & LL_DEFAULT) && out->xmit_cnt.pps ?
				    "devmap_xmit total" :
				    "devmap_xmit";

		print_err(out->xmit_cnt.err || out->xmit_cnt.drop,
			  "  %-20s " FMT_COLUMNl FMT_COLUMNl FMT_COLUMNl
				  __COLUMN(".2f") "\n",
			  str, XMIT(out->xmit_cnt.pps),
			  DROP(out->xmit_cnt.drop), out->xmit_cnt.err,
			  "drv_err/s", out->xmit_cnt.bavg, "bulk-avg");

		stats_get_devmap_xmit(r, p, nr_cpus, NULL);
	}

	if (mask & SAMPLE_DEVMAP_XMIT_CNT_MULTI)
		stats_get_devmap_xmit_multi(r, p, nr_cpus, NULL,
					    mask & SAMPLE_DEVMAP_XMIT_CNT);

	if (sample_log_level & LL_DEFAULT ||
	    ((sample_log_level & LL_SIMPLE) && sample_err_exp)) {
		sample_err_exp = false;
		printf("\n");
	}
}

int sample_setup_maps(struct bpf_map **maps)
{
	sample_n_cpus = libbpf_num_possible_cpus();

	for (int i = 0; i < MAP_DEVMAP_XMIT_MULTI; i++) {
		sample_map[i] = maps[i];

		switch (i) {
		case MAP_RX:
		case MAP_CPUMAP_KTHREAD:
		case MAP_DEVMAP_XMIT:
			sample_map_count[i] = sample_n_cpus;
			break;
		case MAP_REDIRECT_ERR:
			sample_map_count[i] =
				XDP_REDIRECT_ERR_MAX * sample_n_cpus;
			break;
		case MAP_EXCEPTION:
			sample_map_count[i] = XDP_ACTION_MAX * sample_n_cpus;
		case MAP_CPUMAP_ENQUEUE:
			sample_map_count[i] = sample_n_cpus * sample_n_cpus;
			break;
		default:
			return -EINVAL;
		}
		if (bpf_map__set_max_entries(sample_map[i], sample_map_count[i]) < 0)
			return -errno;
	}
	sample_map[MAP_DEVMAP_XMIT_MULTI] = maps[MAP_DEVMAP_XMIT_MULTI];
	return 0;
}

static int sample_setup_maps_mappings(void)
{
	for (int i = 0; i < MAP_DEVMAP_XMIT_MULTI; i++) {
		size_t size = sample_map_count[i] * sizeof(struct datarec);

		sample_mmap[i] = mmap(NULL, size, PROT_READ | PROT_WRITE,
				      MAP_SHARED, bpf_map__fd(sample_map[i]), 0);
		if (sample_mmap[i] == MAP_FAILED)
			return -errno;
	}
	return 0;
}

int __sample_init(int mask)
{
	sigset_t st;

	sigemptyset(&st);
	sigaddset(&st, SIGQUIT);
	sigaddset(&st, SIGINT);
	sigaddset(&st, SIGTERM);

	if (sigprocmask(SIG_BLOCK, &st, NULL) < 0)
		return -errno;

	sample_sig_fd = signalfd(-1, &st, SFD_CLOEXEC | SFD_NONBLOCK);
	if (sample_sig_fd < 0)
		return -errno;

	sample_mask = mask;

	return sample_setup_maps_mappings();
}

static int __sample_remove_xdp(int ifindex, __u32 prog_id, int xdp_flags)
{
	__u32 cur_prog_id = 0;
	int ret;

	if (prog_id) {
		ret = bpf_xdp_query_id(ifindex, xdp_flags, &cur_prog_id);
		if (ret < 0)
			return -errno;

		if (prog_id != cur_prog_id) {
			print_always(
				"Program on ifindex %d does not match installed "
				"program, skipping unload\n",
				ifindex);
			return -ENOENT;
		}
	}

	return bpf_xdp_detach(ifindex, xdp_flags, NULL);
}

int sample_install_xdp(struct bpf_program *xdp_prog, int ifindex, bool generic,
		       bool force)
{
	int ret, xdp_flags = 0;
	__u32 prog_id = 0;

	if (sample_xdp_cnt == 32) {
		fprintf(stderr,
			"Total limit for installed XDP programs in a sample reached\n");
		return -ENOTSUP;
	}

	xdp_flags |= !force ? XDP_FLAGS_UPDATE_IF_NOEXIST : 0;
	xdp_flags |= generic ? XDP_FLAGS_SKB_MODE : XDP_FLAGS_DRV_MODE;
	ret = bpf_xdp_attach(ifindex, bpf_program__fd(xdp_prog), xdp_flags, NULL);
	if (ret < 0) {
		ret = -errno;
		fprintf(stderr,
			"Failed to install program \"%s\" on ifindex %d, mode = %s, "
			"force = %s: %s\n",
			bpf_program__name(xdp_prog), ifindex,
			generic ? "skb" : "native", force ? "true" : "false",
			strerror(-ret));
		return ret;
	}

	ret = bpf_xdp_query_id(ifindex, xdp_flags, &prog_id);
	if (ret < 0) {
		ret = -errno;
		fprintf(stderr,
			"Failed to get XDP program id for ifindex %d, removing program: %s\n",
			ifindex, strerror(errno));
		__sample_remove_xdp(ifindex, 0, xdp_flags);
		return ret;
	}
	sample_xdp_progs[sample_xdp_cnt++] =
		(struct xdp_desc){ ifindex, prog_id, xdp_flags };

	return 0;
}

static void sample_summary_print(void)
{
	double num = sample_out.rx_cnt.num;

	if (sample_out.totals.rx) {
		double pkts = sample_out.totals.rx;

		print_always("  Packets received    : %'-10llu\n",
			     sample_out.totals.rx);
		print_always("  Average packets/s   : %'-10.0f\n",
			     sample_round(pkts / num));
	}
	if (sample_out.totals.redir) {
		double pkts = sample_out.totals.redir;

		print_always("  Packets redirected  : %'-10llu\n",
			     sample_out.totals.redir);
		print_always("  Average redir/s     : %'-10.0f\n",
			     sample_round(pkts / num));
	}
	if (sample_out.totals.drop)
		print_always("  Rx dropped          : %'-10llu\n",
			     sample_out.totals.drop);
	if (sample_out.totals.drop_xmit)
		print_always("  Tx dropped          : %'-10llu\n",
			     sample_out.totals.drop_xmit);
	if (sample_out.totals.err)
		print_always("  Errors recorded     : %'-10llu\n",
			     sample_out.totals.err);
	if (sample_out.totals.xmit) {
		double pkts = sample_out.totals.xmit;

		print_always("  Packets transmitted : %'-10llu\n",
			     sample_out.totals.xmit);
		print_always("  Average transmit/s  : %'-10.0f\n",
			     sample_round(pkts / num));
	}
}

void sample_exit(int status)
{
	size_t size;

	for (int i = 0; i < NUM_MAP; i++) {
		size = sample_map_count[i] * sizeof(**sample_mmap);
		munmap(sample_mmap[i], size);
	}
	while (sample_xdp_cnt--) {
		int i = sample_xdp_cnt, ifindex, xdp_flags;
		__u32 prog_id;

		prog_id = sample_xdp_progs[i].prog_id;
		ifindex = sample_xdp_progs[i].ifindex;
		xdp_flags = sample_xdp_progs[i].flags;

		__sample_remove_xdp(ifindex, prog_id, xdp_flags);
	}
	sample_summary_print();
	close(sample_sig_fd);
	exit(status);
}

static int sample_stats_collect(struct stats_record *rec)
{
	int i;

	if (sample_mask & SAMPLE_RX_CNT)
		map_collect_percpu(sample_mmap[MAP_RX], &rec->rx_cnt);

	if (sample_mask & SAMPLE_REDIRECT_CNT)
		map_collect_percpu(sample_mmap[MAP_REDIRECT_ERR], &rec->redir_err[0]);

	if (sample_mask & SAMPLE_REDIRECT_ERR_CNT) {
		for (i = 1; i < XDP_REDIRECT_ERR_MAX; i++)
			map_collect_percpu(&sample_mmap[MAP_REDIRECT_ERR][i * sample_n_cpus],
					   &rec->redir_err[i]);
	}

	if (sample_mask & SAMPLE_CPUMAP_ENQUEUE_CNT)
		for (i = 0; i < sample_n_cpus; i++)
			map_collect_percpu(&sample_mmap[MAP_CPUMAP_ENQUEUE][i * sample_n_cpus],
					   &rec->enq[i]);

	if (sample_mask & SAMPLE_CPUMAP_KTHREAD_CNT)
		map_collect_percpu(sample_mmap[MAP_CPUMAP_KTHREAD],
				   &rec->kthread);

	if (sample_mask & SAMPLE_EXCEPTION_CNT)
		for (i = 0; i < XDP_ACTION_MAX; i++)
			map_collect_percpu(&sample_mmap[MAP_EXCEPTION][i * sample_n_cpus],
					   &rec->exception[i]);

	if (sample_mask & SAMPLE_DEVMAP_XMIT_CNT)
		map_collect_percpu(sample_mmap[MAP_DEVMAP_XMIT], &rec->devmap_xmit);

	if (sample_mask & SAMPLE_DEVMAP_XMIT_CNT_MULTI) {
		if (map_collect_percpu_devmap(bpf_map__fd(sample_map[MAP_DEVMAP_XMIT_MULTI]), rec) < 0)
			return -EINVAL;
	}
	return 0;
}

static void sample_summary_update(struct sample_output *out)
{
	sample_out.totals.rx += out->totals.rx;
	sample_out.totals.redir += out->totals.redir;
	sample_out.totals.drop += out->totals.drop;
	sample_out.totals.drop_xmit += out->totals.drop_xmit;
	sample_out.totals.err += out->totals.err;
	sample_out.totals.xmit += out->totals.xmit;
	sample_out.rx_cnt.num++;
}

static void sample_stats_print(int mask, struct stats_record *cur,
			       struct stats_record *prev, char *prog_name)
{
	struct sample_output out = {};

	if (mask & SAMPLE_RX_CNT)
		stats_get_rx_cnt(cur, prev, 0, &out);
	if (mask & SAMPLE_REDIRECT_CNT)
		stats_get_redirect_cnt(cur, prev, 0, &out);
	if (mask & SAMPLE_REDIRECT_ERR_CNT)
		stats_get_redirect_err_cnt(cur, prev, 0, &out);
	if (mask & SAMPLE_EXCEPTION_CNT)
		stats_get_exception_cnt(cur, prev, 0, &out);
	if (mask & SAMPLE_DEVMAP_XMIT_CNT)
		stats_get_devmap_xmit(cur, prev, 0, &out);
	else if (mask & SAMPLE_DEVMAP_XMIT_CNT_MULTI)
		stats_get_devmap_xmit_multi(cur, prev, 0, &out,
					    mask & SAMPLE_DEVMAP_XMIT_CNT);
	sample_summary_update(&out);

	stats_print(prog_name, mask, cur, prev, &out);
}

void sample_switch_mode(void)
{
	sample_log_level ^= LL_DEBUG - 1;
}

static int sample_signal_cb(void)
{
	struct signalfd_siginfo si;
	int r;

	r = read(sample_sig_fd, &si, sizeof(si));
	if (r < 0)
		return -errno;

	switch (si.ssi_signo) {
	case SIGQUIT:
		sample_switch_mode();
		printf("\n");
		break;
	default:
		printf("\n");
		return 1;
	}

	return 0;
}

/* Pointer swap trick */
static void swap(struct stats_record **a, struct stats_record **b)
{
	struct stats_record *tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static int sample_timer_cb(int timerfd, struct stats_record **rec,
			   struct stats_record **prev)
{
	char line[64] = "Summary";
	int ret;
	__u64 t;

	ret = read(timerfd, &t, sizeof(t));
	if (ret < 0)
		return -errno;

	swap(prev, rec);
	ret = sample_stats_collect(*rec);
	if (ret < 0)
		return ret;

	if (sample_xdp_cnt == 2 && !(sample_mask & SAMPLE_SKIP_HEADING)) {
		char fi[IFNAMSIZ];
		char to[IFNAMSIZ];
		const char *f, *t;

		f = t = NULL;
		if (if_indextoname(sample_xdp_progs[0].ifindex, fi))
			f = fi;
		if (if_indextoname(sample_xdp_progs[1].ifindex, to))
			t = to;

		snprintf(line, sizeof(line), "%s->%s", f ?: "?", t ?: "?");
	}

	sample_stats_print(sample_mask, *rec, *prev, line);
	return 0;
}

int sample_run(int interval, void (*post_cb)(void *), void *ctx)
{
	struct timespec ts = { interval, 0 };
	struct itimerspec its = { ts, ts };
	struct stats_record *rec, *prev;
	struct pollfd pfd[2] = {};
	int timerfd, ret;

	if (!interval) {
		fprintf(stderr, "Incorrect interval 0\n");
		return -EINVAL;
	}
	sample_interval = interval;
	/* Pretty print numbers */
	setlocale(LC_NUMERIC, "en_US.UTF-8");

	timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (timerfd < 0)
		return -errno;
	timerfd_settime(timerfd, 0, &its, NULL);

	pfd[0].fd = sample_sig_fd;
	pfd[0].events = POLLIN;

	pfd[1].fd = timerfd;
	pfd[1].events = POLLIN;

	ret = -ENOMEM;
	rec = alloc_stats_record();
	if (!rec)
		goto end;
	prev = alloc_stats_record();
	if (!prev)
		goto end_rec;

	ret = sample_stats_collect(rec);
	if (ret < 0)
		goto end_rec_prev;

	for (;;) {
		ret = poll(pfd, 2, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else
				break;
		}

		if (pfd[0].revents & POLLIN)
			ret = sample_signal_cb();
		else if (pfd[1].revents & POLLIN)
			ret = sample_timer_cb(timerfd, &rec, &prev);

		if (ret)
			break;

		if (post_cb)
			post_cb(ctx);
	}

end_rec_prev:
	free_stats_record(prev);
end_rec:
	free_stats_record(rec);
end:
	close(timerfd);

	return ret;
}

const char *get_driver_name(int ifindex)
{
	struct ethtool_drvinfo drv = {};
	char ifname[IF_NAMESIZE];
	static char drvname[32];
	struct ifreq ifr = {};
	int fd, r = 0;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return "[error]";

	if (!if_indextoname(ifindex, ifname))
		goto end;

	drv.cmd = ETHTOOL_GDRVINFO;
	safe_strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (void *)&drv;

	r = ioctl(fd, SIOCETHTOOL, &ifr);
	if (r)
		goto end;

	safe_strncpy(drvname, drv.driver, sizeof(drvname));

	close(fd);
	return drvname;

end:
	r = errno;
	close(fd);
	return r == EOPNOTSUPP ? "loopback" : "[error]";
}

int get_mac_addr(int ifindex, void *mac_addr)
{
	char ifname[IF_NAMESIZE];
	struct ifreq ifr = {};
	int fd, r;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -errno;

	if (!if_indextoname(ifindex, ifname)) {
		r = -errno;
		goto end;
	}

	safe_strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	r = ioctl(fd, SIOCGIFHWADDR, &ifr);
	if (r) {
		r = -errno;
		goto end;
	}

	memcpy(mac_addr, ifr.ifr_hwaddr.sa_data, 6 * sizeof(char));

end:
	close(fd);
	return r;
}

__attribute__((constructor)) static void sample_ctor(void)
{
	if (libbpf_set_strict_mode(LIBBPF_STRICT_ALL) < 0) {
		fprintf(stderr, "Failed to set libbpf strict mode: %s\n",
			strerror(errno));
		/* Just exit, nothing to cleanup right now */
		exit(EXIT_FAIL_BPF);
	}
}
