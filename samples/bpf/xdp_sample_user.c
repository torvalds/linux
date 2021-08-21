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
};

struct sample_output {
	struct {
		__u64 rx;
	} totals;
	struct {
		__u64 pps;
		__u64 drop;
		__u64 err;
	} rx_cnt;
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

	return rec;
end_rec:
	free(rec);
	return NULL;
}

static void free_stats_record(struct stats_record *r)
{
	struct hlist_node *tmp;
	struct map_entry *e;
	int i;

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


static void stats_print(const char *prefix, int mask, struct stats_record *r,
			struct stats_record *p, struct sample_output *out)
{
	int nr_cpus = libbpf_num_possible_cpus();
	const char *str;

	print_always("%-23s", prefix ?: "Summary");
	if (mask & SAMPLE_RX_CNT)
		print_always(FMT_COLUMNl, RX(out->totals.rx));
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

	if (sample_log_level & LL_DEFAULT ||
	    ((sample_log_level & LL_SIMPLE) && sample_err_exp)) {
		sample_err_exp = false;
		printf("\n");
	}
}

int sample_setup_maps(struct bpf_map **maps)
{
	sample_n_cpus = libbpf_num_possible_cpus();

	for (int i = 0; i < NUM_MAP; i++) {
		sample_map[i] = maps[i];

		switch (i) {
		case MAP_RX:
			sample_map_count[i] = sample_n_cpus;
			break;
		default:
			return -EINVAL;
		}
		if (bpf_map__resize(sample_map[i], sample_map_count[i]) < 0)
			return -errno;
	}
	return 0;
}

static int sample_setup_maps_mappings(void)
{
	for (int i = 0; i < NUM_MAP; i++) {
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
		ret = bpf_get_link_xdp_id(ifindex, &cur_prog_id, xdp_flags);
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

	return bpf_set_link_xdp_fd(ifindex, -1, xdp_flags);
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
	ret = bpf_set_link_xdp_fd(ifindex, bpf_program__fd(xdp_prog),
				  xdp_flags);
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

	ret = bpf_get_link_xdp_id(ifindex, &prog_id, xdp_flags);
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
	double period = sample_out.rx_cnt.pps;

	if (sample_out.totals.rx) {
		double pkts = sample_out.totals.rx;

		print_always("  Packets received    : %'-10llu\n",
			     sample_out.totals.rx);
		print_always("  Average packets/s   : %'-10.0f\n",
			     sample_round(pkts / period));
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

	return 0;
}

static void sample_summary_update(struct sample_output *out, int interval)
{
	sample_out.totals.rx += out->totals.rx;
	sample_out.rx_cnt.pps += interval;
}

static void sample_stats_print(int mask, struct stats_record *cur,
			       struct stats_record *prev, char *prog_name,
			       int interval)
{
	struct sample_output out = {};

	if (mask & SAMPLE_RX_CNT)
		stats_get_rx_cnt(cur, prev, 0, &out);
	sample_summary_update(&out, interval);

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
			   struct stats_record **prev, int interval)
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

	if (sample_xdp_cnt == 2) {
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

	sample_stats_print(sample_mask, *rec, *prev, line, interval);
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
			ret = sample_timer_cb(timerfd, &rec, &prev, interval);

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
