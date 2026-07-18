// SPDX-License-Identifier: GPL-2.0
/*
 * pipe_bench - exercise concurrent pipe operation
 *
 * N writer threads hammer a single pipe with multi-page writes; M reader
 * threads drain it. Each writer records its own write() latency histogram.
 * Multi-page writes (msgsize >= PAGE_SIZE) force the loop in
 * anon_pipe_write() to call alloc_page(GFP_HIGHUSER | __GFP_ACCOUNT) under
 * pipe->mutex, which is the critical section the patch shrinks.
 *
 * By default the benchmark sweeps writers in {1, 2, 5} x readers in
 * {1, 5, 10} and prints one block per configuration so two runs (e.g.
 * baseline vs patched) can be diffed directly. Pass -w and -r to run a
 * single configuration instead. Pass --memory-pressure to spawn stress-ng
 * alongside the sweep so the per-page alloc_page() path under pipe->mutex
 * has to dip into reclaim.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
#define HIST_BUCKETS	32

static size_t g_msgsize = 16 * 4096;
static int g_duration = 3;
static int g_pipe_size = 1024 * 1024;
static int g_memory_pressure;

static atomic_int g_stop;
static int g_pipe[2];

struct wstats {
	uint64_t writes;
	uint64_t bytes;
	uint64_t lat_sum_ns;
	uint64_t lat_max_ns;
	uint64_t lat_hist[HIST_BUCKETS];
	char *buf;
};

struct rstats {
	char *buf;
};

struct hist_totals {
	uint64_t writes;
	uint64_t bytes;
	uint64_t lat_sum;
	uint64_t lat_max;
};

static inline uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline int log2_bucket(uint64_t v)
{
	int b = 0;

	if (!v)
		return 0;
	while (v >>= 1)
		b++;
	return b < HIST_BUCKETS ? b : HIST_BUCKETS - 1;
}

static void *writer(void *arg)
{
	struct wstats *s = arg;

	while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
		uint64_t t0 = now_ns();
		ssize_t n = write(g_pipe[1], s->buf, g_msgsize);
		uint64_t dt = now_ns() - t0;

		if (n > 0) {
			s->writes++;
			s->bytes += (uint64_t)n;
			s->lat_sum_ns += dt;
			if (dt > s->lat_max_ns)
				s->lat_max_ns = dt;
			s->lat_hist[log2_bucket(dt)]++;
		} else if (n < 0 && (errno == EPIPE || errno == EBADF)) {
			break;
		}
	}
	return NULL;
}

static void *reader(void *arg)
{
	struct rstats *s = arg;

	/*
	 * Drain until EOF (write end closed by main). g_stop is not checked
	 * here on purpose: writers may be blocked in write() with the pipe
	 * full when g_stop is set, so the reader must keep draining until
	 * main closes the write end.
	 */
	for (;;) {
		ssize_t n = read(g_pipe[0], s->buf, g_msgsize);

		if (n <= 0)
			break;
	}
	return NULL;
}

/* Sum per-writer stats and per-bucket counts into the caller's aggregates. */
static void aggregate_wstats(struct wstats *all, int nw,
			     uint64_t agg[HIST_BUCKETS],
			     struct hist_totals *t)
{
	memset(t, 0, sizeof(*t));
	for (int i = 0; i < nw; i++) {
		t->writes += all[i].writes;
		t->bytes += all[i].bytes;
		t->lat_sum += all[i].lat_sum_ns;
		if (all[i].lat_max_ns > t->lat_max)
			t->lat_max = all[i].lat_max_ns;
		for (int b = 0; b < HIST_BUCKETS; b++)
			agg[b] += all[i].lat_hist[b];
	}
}

/*
 * Walk @agg in order, returning the inclusive upper bound (in ns) of the
 * log2 bucket where the running sum first reaches @target.
 *
 * A percentile is undefined with zero samples, and with very low sample
 * counts integer truncation could make @target zero -- then "cum >= 0"
 * would latch on the first (possibly empty) bucket. Callers must pass
 * @target >= 1.
 */
static uint64_t bucket_at(const uint64_t agg[HIST_BUCKETS], uint64_t target)
{
	uint64_t cum = 0;

	for (int b = 0; b < HIST_BUCKETS; b++) {
		/* HIST_BUCKETS <= 63, so (b + 1) is always a safe shift. */
		uint64_t upper = (1ULL << (b + 1)) - 1;

		cum += agg[b];
		if (cum >= target)
			return upper;
	}
	return 0;
}

static void compute_p50_p99(const uint64_t agg[HIST_BUCKETS], uint64_t writes,
			    uint64_t *p50, uint64_t *p99)
{
	uint64_t p50_target, p99_target;

	*p50 = *p99 = 0;
	if (!writes)
		return;

	p50_target = writes * 50 / 100;
	p99_target = writes * 99 / 100;
	if (!p50_target)
		p50_target = 1;
	if (!p99_target)
		p99_target = 1;

	*p50 = bucket_at(agg, p50_target);
	*p99 = bucket_at(agg, p99_target);
}

static void print_summary(int nw, int nr, const struct hist_totals *t,
			  uint64_t p50, uint64_t p99)
{
	double sec = g_duration;
	uint64_t avg_ns = t->writes ? t->lat_sum / t->writes : 0;

	printf("config: writers=%d readers=%d msgsize=%zu duration=%d pipe_size=%d memory_pressure=%s\n",
	       nw, nr, g_msgsize, g_duration, g_pipe_size,
	       g_memory_pressure ? "yes" : "no");
	printf("writes: total=%llu rate=%.0f/s\n",
	       (unsigned long long)t->writes, (double)t->writes / sec);
	printf("throughput_MBps: %.2f\n",
	       ((double)t->bytes / sec) / (1024.0 * 1024.0));
	printf("lat_avg_ns: %llu\n", (unsigned long long)avg_ns);
	printf("lat_p50_ns_upper: %llu\n", (unsigned long long)p50);
	printf("lat_p99_ns_upper: %llu\n", (unsigned long long)p99);
	printf("lat_max_ns: %llu\n", (unsigned long long)t->lat_max);
}

static void summarize(struct wstats *all, int nw, int nr)
{
	uint64_t agg[HIST_BUCKETS] = {0};
	struct hist_totals t;
	uint64_t p50, p99;

	aggregate_wstats(all, nw, agg, &t);
	compute_p50_p99(agg, t.writes, &p50, &p99);
	print_summary(nw, nr, &t, p50, p99);
}

/*
 * Child branch of fork(): restore SIGPIPE to default (parent ignores it),
 * exec stress-ng, and on failure write the reason into @hs_wr before
 * exiting. The parent observes EOF on hs_wr (closed via O_CLOEXEC) when
 * exec succeeds.
 */
static void stress_ng_child(int hs_wr) __attribute__((noreturn));
static void stress_ng_child(int hs_wr)
{
	char errbuf[256];

	signal(SIGPIPE, SIG_DFL);
	execlp("stress-ng", "stress-ng",
	       "--vm", "4", "--vm-bytes", "80%",
	       "--vm-method", "all",
	       (char *)NULL);
	snprintf(errbuf, sizeof(errbuf),
		 "exec stress-ng failed: %s\n", strerror(errno));
	(void)!write(hs_wr, errbuf, strlen(errbuf));
	_exit(127);
}

/*
 * Read from the O_CLOEXEC handshake pipe. Anything readable means the
 * child wrote an error before exec; EOF (n == 0) means the write-end
 * closed because exec succeeded. Returns 0 on exec success, -1 if the
 * child failed and was reaped.
 */
static int stress_ng_wait_handshake(int hs_rd, pid_t pid)
{
	struct pollfd pfd = { .fd = hs_rd, .events = POLLIN };
	char errbuf[256];
	int status;
	int ret;

	ret = poll(&pfd, 1, 500);
	if (ret <= 0)
		return 0;

	ssize_t n = read(hs_rd, errbuf, sizeof(errbuf) - 1);

	if (n > 0) {
		errbuf[n] = '\0';
		fputs(errbuf, stderr);
		waitpid(pid, &status, 0);
		return -1;
	}
	return 0;
}

static pid_t spawn_stress_ng(void)
{
	int hs[2];
	pid_t pid;

	/*
	 * Handshake pipe: child writes one byte and _exit()s on exec
	 * failure. On exec success the O_CLOEXEC flag closes the write
	 * end, which the parent observes as EOF. This makes the "is
	 * stress-ng on $PATH?" check fail fast rather than silently.
	 */
	if (pipe2(hs, O_CLOEXEC) < 0) {
		perror("pipe2");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		close(hs[0]);
		close(hs[1]);
		return -1;
	}
	if (pid == 0) {
		close(hs[0]);
		stress_ng_child(hs[1]);
	}

	close(hs[1]);
	if (stress_ng_wait_handshake(hs[0], pid) < 0) {
		close(hs[0]);
		return -1;
	}
	close(hs[0]);

	/* Give stress-ng a moment to map its VM regions before measuring. */
	sleep(1);
	return pid;
}

static void kill_stress_ng(pid_t pid)
{
	int status;

	if (pid <= 0)
		return;
	kill(pid, SIGTERM);
	for (int i = 0; i < 20; i++) {
		if (waitpid(pid, &status, WNOHANG) > 0)
			return;
		usleep(100 * 1000);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
}

/*
 * Allocate per-thread page-aligned buffers in main so a failed
 * aligned_alloc() aborts the run before any thread starts. Workers used
 * to allocate their own buffer and return NULL on failure, which left
 * peers blocked in write()/read() with nobody to unblock them.
 */
static int alloc_thread_bufs(struct wstats *ws, int nw,
			     struct rstats *rs, int nr)
{
	for (int i = 0; i < nw; i++) {
		ws[i].buf = aligned_alloc(4096, g_msgsize);
		if (!ws[i].buf) {
			fprintf(stderr, "writer %d: aligned_alloc(%zu) failed\n",
				i, g_msgsize);
			return -1;
		}
		memset(ws[i].buf, 0xAA, g_msgsize);
	}
	for (int i = 0; i < nr; i++) {
		rs[i].buf = aligned_alloc(4096, g_msgsize);
		if (!rs[i].buf) {
			fprintf(stderr, "reader %d: aligned_alloc(%zu) failed\n",
				i, g_msgsize);
			return -1;
		}
	}
	return 0;
}

static void free_thread_bufs(struct wstats *ws, int nw,
			     struct rstats *rs, int nr)
{
	if (ws)
		for (int i = 0; i < nw; i++)
			free(ws[i].buf);
	if (rs)
		for (int i = 0; i < nr; i++)
			free(rs[i].buf);
}

static int start_readers(pthread_t *rt, struct rstats *rs, int nr,
			 int *created)
{
	for (int i = 0; i < nr; i++) {
		int err = pthread_create(&rt[i], NULL, reader, &rs[i]);

		if (err) {
			fprintf(stderr, "pthread_create reader %d: %s\n",
				i, strerror(err));
			return -1;
		}
		(*created)++;
	}
	return 0;
}

static int start_writers(pthread_t *wt, struct wstats *ws, int nw,
			 int *created)
{
	for (int i = 0; i < nw; i++) {
		int err = pthread_create(&wt[i], NULL, writer, &ws[i]);

		if (err) {
			fprintf(stderr, "pthread_create writer %d: %s\n",
				i, strerror(err));
			return -1;
		}
		(*created)++;
	}
	return 0;
}

static int open_bench_pipe(void)
{
	if (pipe(g_pipe) < 0) {
		perror("pipe");
		return -1;
	}
	if (fcntl(g_pipe[1], F_SETPIPE_SZ, g_pipe_size) < 0)
		perror("F_SETPIPE_SZ (continuing)");
	return 0;
}

/*
 * Normal termination: g_stop tells writers to leave the loop after the
 * current write() returns. Closing the shared write-end fd means once
 * the in-flight writes drain, readers see EOF and exit. Writers are not
 * unblocked by EPIPE here -- g_pipe[0] stays open so readers can keep
 * draining.
 *
 * Error path: some threads may have been created and others skipped, so
 * writers could be blocked in write() with no reader making progress.
 * Close both ends -- closing the read end is what delivers EPIPE to a
 * blocked writer.
 */
static void stop_and_join(pthread_t *wt, int nw_created,
			  pthread_t *rt, int nr_created, int rc)
{
	atomic_store(&g_stop, 1);
	close(g_pipe[1]);
	if (rc < 0)
		close(g_pipe[0]);
	for (int i = 0; i < nw_created; i++)
		pthread_join(wt[i], NULL);
	for (int i = 0; i < nr_created; i++)
		pthread_join(rt[i], NULL);
	if (rc == 0)
		close(g_pipe[0]);
}

static int run_one(int nw, int nr)
{
	pthread_t *wt = NULL, *rt = NULL;
	struct wstats *ws = NULL;
	struct rstats *rs = NULL;
	int nw_created = 0, nr_created = 0;
	int rc = 0;

	atomic_store(&g_stop, 0);

	if (open_bench_pipe() < 0)
		return -1;

	wt = calloc((size_t)nw, sizeof(*wt));
	rt = calloc((size_t)nr, sizeof(*rt));
	ws = calloc((size_t)nw, sizeof(*ws));
	rs = calloc((size_t)nr, sizeof(*rs));
	if (!wt || !rt || !ws || !rs) {
		fprintf(stderr, "alloc failed\n");
		rc = -1;
		goto teardown;
	}

	if (alloc_thread_bufs(ws, nw, rs, nr) < 0) {
		rc = -1;
		goto teardown;
	}

	if (start_readers(rt, rs, nr, &nr_created) < 0 ||
	    start_writers(wt, ws, nw, &nw_created) < 0) {
		rc = -1;
		goto teardown;
	}

	sleep((unsigned int)g_duration);

teardown:
	stop_and_join(wt, nw_created, rt, nr_created, rc);

	if (rc == 0) {
		summarize(ws, nw, nr);
		fflush(stdout);
	}

	free_thread_bufs(ws, nw, rs, nr);
	free(wt);
	free(rt);
	free(ws);
	free(rs);
	return rc;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-w writers] [-r readers] [-s msgsize] [-d secs] [-p pipe_size] [--memory-pressure]\n"
		"  default: sweep writers={1,2,5} x readers={1,5,10}\n"
		"  --memory-pressure: spawn stress-ng (--vm 4 --vm-bytes 80%% --vm-method all) for the run\n",
		prog);
}

static int parse_args(int argc, char **argv,
		      int *writers_override, int *readers_override)
{
	static const struct option long_opts[] = {
		{"memory-pressure", no_argument, NULL, 'M'},
		{0, 0, 0, 0},
	};
	int opt;

	while ((opt = getopt_long(argc, argv, "w:r:s:d:p:",
				  long_opts, NULL)) != -1) {
		switch (opt) {
		case 'w':
			*writers_override = atoi(optarg);
			break;
		case 'r':
			*readers_override = atoi(optarg);
			break;
		case 's':
			g_msgsize = (size_t)atol(optarg);
			break;
		case 'd':
			g_duration = atoi(optarg);
			break;
		case 'p':
			g_pipe_size = atoi(optarg);
			break;
		case 'M':
			g_memory_pressure = 1;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}
	return 0;
}

/*
 * aligned_alloc(4096, size) requires size to be a multiple of the
 * alignment (C11); glibc returns NULL otherwise, which would make
 * writer/reader threads silently exit and the run report zero writes.
 * Validate up front instead.
 */
static int validate_args(void)
{
	if (g_msgsize == 0 || g_msgsize % 4096 != 0) {
		fprintf(stderr,
			"msgsize must be a positive multiple of 4096 (got %zu)\n",
			g_msgsize);
		return -1;
	}
	if (g_duration <= 0) {
		fprintf(stderr, "duration must be > 0 seconds (got %d)\n",
			g_duration);
		return -1;
	}
	if (g_pipe_size <= 0) {
		fprintf(stderr, "pipe_size must be > 0 bytes (got %d)\n",
			g_pipe_size);
		return -1;
	}
	return 0;
}

static int run_sweep(void)
{
	static const int writers_sweep[] = {1, 2, 5};
	static const int readers_sweep[] = {1, 5, 10};

	for (size_t i = 0; i < ARRAY_SIZE(writers_sweep); i++) {
		for (size_t j = 0; j < ARRAY_SIZE(readers_sweep); j++) {
			printf("---\n");
			if (run_one(writers_sweep[i], readers_sweep[j]) < 0)
				return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int writers_override = 0, readers_override = 0;
	pid_t stress_pid = -1;
	int rc = 0;

	if (parse_args(argc, argv, &writers_override, &readers_override) < 0)
		return 1;
	if (validate_args() < 0)
		return 1;

	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	fprintf(stderr, "pid=%d\n", getpid());
	fflush(stderr);

	if (g_memory_pressure) {
		stress_pid = spawn_stress_ng();
		if (stress_pid < 0) {
			fprintf(stderr,
				"memory_pressure requested but stress-ng could not be spawned\n");
			return 1;
		}
	}

	if (writers_override > 0 || readers_override > 0) {
		int nw = writers_override > 0 ? writers_override : 1;
		int nr = readers_override > 0 ? readers_override : 1;

		rc = run_one(nw, nr) < 0 ? 1 : 0;
	} else {
		rc = run_sweep() < 0 ? 1 : 0;
	}

	kill_stress_ng(stress_pid);
	return rc;
}
