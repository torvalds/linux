// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 *
 * Exercise a priority inversion with the owner and donor first pinned to the
 * same CPU, then with each on a different CPU. A high-priority donor blocks on
 * a mutex held by a low-priority owner while one medium-priority contender per
 * available CPU keeps the system busy. A weighted-vruntime BPF scheduler runs
 * both CPU placement configurations with SCX_OPS_ENQ_BLOCKED first disabled
 * and then enabled. The test validates blocked-donor admission and reports the
 * average mutex hold and wait times, plus their enabled-minus-disabled deltas,
 * for each configuration. The timing data is informational.
 *
 * CONFIG_SCHED_PROXY_EXEC=y is required to exercise the proxy-execution paths.
 */
#define _GNU_SOURCE

#include <bpf/bpf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <scx/common.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "enq_blocked.bpf.skel.h"
#include "enq_blocked.h"
#include "scx_test.h"

#define MODULE_NAME	"scx_enq_blocked_test"
#define MODULE_FILE	"test_modules/" MODULE_NAME ".ko"
#define DEVICE_PATH	"/dev/scx_enq_blocked"
#define WAIT_STEP_US	1000
#define WAIT_TIMEOUT_MS	2000
#define NR_WARMUP_TRIALS	1
#define NR_MEASURED_TRIALS	10
#define NR_TRIALS	(NR_WARMUP_TRIALS + NR_MEASURED_TRIALS)
#define JOIN_TIMEOUT_MS	((NR_TRIALS + 1) * WAIT_TIMEOUT_MS)
#define OWNER_NICE	19
#define DONOR_NICE	-20
#define CONTENDER_NICE	0

struct thread_ctx {
	atomic_bool start_donor;
	atomic_bool abort;
	atomic_bool stop_contender;
	atomic_bool measurement_ready;
	atomic_int donor_pid;
	atomic_int donor_completed;
	int fd;
	int donor_cpu;
	int owner_cpu;
};

struct contender_ctx {
	struct thread_ctx *thread_ctx;
	atomic_int status;
	int cpu;
};

struct run_result {
	struct enq_blocked_stats stats;
	u64 nr_blocked_enqueues;
	u64 nr_blocked_enqueues_donor_cpu;
	u64 nr_blocked_enqueues_owner_cpu;
	u64 nr_blocked_enqueues_other_cpu;
	u64 nr_blocked_wakeups;
};

static bool parse_bool(const char *value, bool *result)
{
	if (!strcasecmp(value, "1") || !strcasecmp(value, "y") ||
	    !strcasecmp(value, "yes") || !strcasecmp(value, "on") ||
	    !strcasecmp(value, "true")) {
		*result = true;
		return true;
	}

	if (!strcasecmp(value, "0") || !strcasecmp(value, "n") ||
	    !strcasecmp(value, "no") || !strcasecmp(value, "off") ||
	    !strcasecmp(value, "false")) {
		*result = false;
		return true;
	}

	return false;
}

static bool cmdline_bool(const char *name, bool default_value)
{
	char cmdline[4096], *newline, *saveptr = NULL, *token;
	size_t name_len = strlen(name);
	bool value = default_value;
	FILE *file;

	file = fopen("/proc/cmdline", "r");
	if (!file)
		return default_value;

	if (!fgets(cmdline, sizeof(cmdline), file)) {
		fclose(file);
		return default_value;
	}
	fclose(file);
	newline = strchr(cmdline, '\n');
	if (newline)
		*newline = '\0';

	for (token = strtok_r(cmdline, " ", &saveptr); token;
	     token = strtok_r(NULL, " ", &saveptr)) {
		bool parsed;

		if (strncmp(token, name, name_len) || token[name_len] != '=')
			continue;
		if (parse_bool(token + name_len + 1, &parsed))
			value = parsed;
	}

	return value;
}

static int module_path(char *path, size_t size)
{
	ssize_t len;
	char *slash;

	len = readlink("/proc/self/exe", path, size - 1);
	if (len < 0)
		return -errno;
	path[len] = '\0';

	slash = strrchr(path, '/');
	if (!slash)
		return -EINVAL;
	*slash = '\0';

	if (snprintf(slash, size - (slash - path), "/%s", MODULE_FILE) >=
	    size - (slash - path))
		return -ENAMETOOLONG;

	return 0;
}

static int load_test_module(bool *loaded_here)
{
	char path[PATH_MAX];
	int fd, err;

	err = module_path(path, sizeof(path));
	if (err)
		return err;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	if (syscall(SYS_finit_module, fd, "", 0)) {
		err = errno;
		close(fd);
		if (err == EEXIST)
			return 0;
		return -err;
	}

	close(fd);
	*loaded_here = true;
	return 0;
}

static void unload_test_module(bool loaded_here)
{
	if (loaded_here && syscall(SYS_delete_module, MODULE_NAME, O_NONBLOCK))
		SCX_ERR("Failed to unload %s (%d)", MODULE_NAME, errno);
}

static int pin_to_cpu(int cpu)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	return sched_setaffinity(0, sizeof(mask), &mask) ? errno : 0;
}

static int select_test_cpus(bool cross_cpu, cpu_set_t *mask, int *donor_cpu,
			    int *owner_cpu)
{
	int cpu, first = -1;

	if (sched_getaffinity(0, sizeof(*mask), mask))
		return -errno;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (!CPU_ISSET(cpu, mask))
			continue;
		if (first < 0) {
			first = cpu;
			if (!cross_cpu)
				break;
		} else {
			*donor_cpu = first;
			*owner_cpu = cpu;
			return 0;
		}
	}

	if (first < 0)
		return -ENODEV;
	if (cross_cpu)
		return -EAGAIN;

	*donor_cpu = first;
	*owner_cpu = first;
	return 0;
}

static int set_nice(int nice)
{
	return setpriority(PRIO_PROCESS, 0, nice) ? errno : 0;
}

static bool wait_for_pid(atomic_int *pid)
{
	int waited_ms;

	for (waited_ms = 0; waited_ms < WAIT_TIMEOUT_MS; waited_ms++) {
		if (atomic_load_explicit(pid, memory_order_acquire) > 0)
			return true;
		usleep(WAIT_STEP_US);
	}

	return false;
}

static int wait_for_contenders(struct contender_ctx *contenders,
			       size_t nr_contenders)
{
	size_t i, nr_ready;
	int status, waited_ms;

	for (waited_ms = 0; waited_ms < WAIT_TIMEOUT_MS; waited_ms++) {
		nr_ready = 0;
		for (i = 0; i < nr_contenders; i++) {
			status = atomic_load_explicit(&contenders[i].status,
						      memory_order_acquire);
			if (status < 0)
				return status;
			if (status > 0)
				nr_ready++;
		}
		if (nr_ready == nr_contenders)
			return 1;
		usleep(WAIT_STEP_US);
	}

	return -ETIMEDOUT;
}

static int wait_for_donor_state(struct thread_ctx *ctx, int expected)
{
	int state, waited_ms;

	for (waited_ms = 0; waited_ms < WAIT_TIMEOUT_MS; waited_ms++) {
		state = ioctl(ctx->fd, ENQ_BLOCKED_IOCTL_DONOR_STATE);
		if (state == expected)
			return state;
		if (state < 0 && errno != ENOENT)
			return -errno;
		usleep(WAIT_STEP_US);
	}

	return -ETIMEDOUT;
}

static bool wait_for_donor(struct thread_ctx *ctx, int trial)
{
	int waited_ms;

	for (waited_ms = 0; waited_ms < WAIT_TIMEOUT_MS; waited_ms++) {
		if (atomic_load_explicit(&ctx->donor_completed,
					 memory_order_acquire) >= trial)
			return true;
		if (atomic_load_explicit(&ctx->abort, memory_order_relaxed))
			return false;
		usleep(WAIT_STEP_US);
	}

	return false;
}

static bool wait_for_measurement(struct thread_ctx *ctx)
{
	while (!atomic_load_explicit(&ctx->measurement_ready,
				     memory_order_acquire) &&
	       !atomic_load_explicit(&ctx->abort, memory_order_relaxed))
		sched_yield();

	return !atomic_load_explicit(&ctx->abort, memory_order_relaxed);
}

static void *contender_fn(void *arg)
{
	struct contender_ctx *contender = arg;
	struct thread_ctx *ctx = contender->thread_ctx;
	int err;

	err = pin_to_cpu(contender->cpu);
	if (!err)
		err = set_nice(CONTENDER_NICE);
	atomic_store_explicit(&contender->status, err ? -err : 1,
			      memory_order_release);
	if (err)
		return (void *)(uintptr_t)err;

	while (!atomic_load_explicit(&ctx->stop_contender,
				     memory_order_relaxed))
		;

	return NULL;
}

static void *owner_fn(void *arg)
{
	struct thread_ctx *ctx = arg;
	int err, i;

	err = pin_to_cpu(ctx->owner_cpu);
	if (err)
		return (void *)(uintptr_t)err;
	err = set_nice(OWNER_NICE);
	if (err)
		return (void *)(uintptr_t)err;

	for (i = 0; i < NR_TRIALS; i++) {
		if (ioctl(ctx->fd, ENQ_BLOCKED_IOCTL_OWNER))
			return (void *)(uintptr_t)errno;
		if (!wait_for_donor(ctx, i + 1))
			return (void *)(uintptr_t)ETIMEDOUT;

		if (i + 1 == NR_WARMUP_TRIALS && !wait_for_measurement(ctx))
			return NULL;
	}

	return NULL;
}

static int run_donor_trial(struct thread_ctx *ctx)
{
	int waited_ms;

	for (waited_ms = 0; waited_ms < WAIT_TIMEOUT_MS; waited_ms++) {
		if (!ioctl(ctx->fd, ENQ_BLOCKED_IOCTL_DONOR))
			return 0;
		if (errno != EAGAIN)
			return -errno;
		usleep(WAIT_STEP_US);
	}

	return -ETIMEDOUT;
}

static void *donor_fn(void *arg)
{
	struct thread_ctx *ctx = arg;
	int err, i;

	err = pin_to_cpu(ctx->donor_cpu);
	if (err)
		return (void *)(uintptr_t)err;
	err = set_nice(DONOR_NICE);
	if (err)
		return (void *)(uintptr_t)err;

	atomic_store_explicit(&ctx->donor_pid, syscall(SYS_gettid),
			      memory_order_release);
	while (!atomic_load_explicit(&ctx->start_donor, memory_order_acquire) &&
	       !atomic_load_explicit(&ctx->abort, memory_order_relaxed))
		sched_yield();

	if (atomic_load_explicit(&ctx->abort, memory_order_relaxed))
		return NULL;

	for (i = 0; i < NR_TRIALS; i++) {
		err = run_donor_trial(ctx);
		if (err)
			return (void *)(uintptr_t)-err;
		atomic_store_explicit(&ctx->donor_completed, i + 1,
				      memory_order_release);
	}

	return NULL;
}

static void print_avg_time(const char *name, u64 total_ns, u64 samples)
{
	u64 avg_ns = samples ? total_ns / samples : 0;

	printf("  %s_avg_ns=%llu (%llu.%03llu ms, samples=%llu)\n", name,
	       (unsigned long long)avg_ns,
	       (unsigned long long)(avg_ns / 1000000),
	       (unsigned long long)((avg_ns / 1000) % 1000),
	       (unsigned long long)samples);
}

static void print_avg_delta(const char *name, u64 disabled_total,
			    u64 disabled_samples, u64 enabled_total,
			    u64 enabled_samples)
{
	u64 disabled_avg, enabled_avg;
	s64 delta_ns;
	double delta_pct;

	if (!disabled_samples || !enabled_samples)
		return;

	disabled_avg = disabled_total / disabled_samples;
	enabled_avg = enabled_total / enabled_samples;
	delta_ns = (s64)enabled_avg - (s64)disabled_avg;
	delta_pct = disabled_avg ? 100.0 * delta_ns / disabled_avg : 0.0;

	printf("  %s_delta_ns=%+lld (%+.2f%%)\n", name,
	       (long long)delta_ns, delta_pct);
}

static int join_thread(pthread_t thread, const struct timespec *deadline,
		       int *thread_err)
{
	void *result;
	int err;

	err = pthread_timedjoin_np(thread, &result, deadline);
	if (err)
		return err;

	*thread_err = (int)(uintptr_t)result;
	return 0;
}

static void set_join_deadline(struct timespec *deadline)
{
	clock_gettime(CLOCK_REALTIME, deadline);
	deadline->tv_sec += JOIN_TIMEOUT_MS / 1000;
	deadline->tv_nsec += (JOIN_TIMEOUT_MS % 1000) * 1000000;
	if (deadline->tv_nsec >= 1000000000) {
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000;
	}
}

static enum scx_test_status setup(void **ctx)
{
	struct enq_blocked *skel;
	u64 flag;

	skel = enq_blocked__open();
	SCX_FAIL_IF(!skel, "Failed to open skel");
	SCX_ENUM_INIT(skel);

	flag = SCX_OPS_ENQ_BLOCKED;
	if (!flag) {
		enq_blocked__destroy(skel);
		fprintf(stderr, "SKIP: SCX_OPS_ENQ_BLOCKED is unavailable\n");
		return SCX_TEST_SKIP;
	}

	enq_blocked__destroy(skel);
	*ctx = NULL;
	return SCX_TEST_PASS;
}

static enum scx_test_status run_one(bool enq_blocked, bool cross_cpu,
				    struct run_result *result)
{
	struct enq_blocked *skel;
	struct thread_ctx thread_ctx = {};
	struct contender_ctx *contender_ctxs = NULL;
	struct bpf_link *link = NULL;
	pthread_t owner, donor, *contenders = NULL;
	struct timespec join_deadline;
	cpu_set_t allowed_cpus;
	bool module_loaded = false;
	bool owner_started = false, donor_started = false;
	bool join_timed_out = false;
	bool proxy_enabled;
	enum scx_test_status status = SCX_TEST_PASS;
	int cpu, donor_pid, donor_state, err, thread_err;
	size_t i, nr_contenders, nr_contenders_started = 0;
	size_t nr_contenders_joined = 0;
	u64 nr_blocked, nr_blocked_donor_cpu, nr_blocked_owner_cpu;
	u64 nr_blocked_other_cpu, nr_blocked_wakeups;
	struct enq_blocked_stats stats;

	err = select_test_cpus(cross_cpu, &allowed_cpus, &thread_ctx.donor_cpu,
			       &thread_ctx.owner_cpu);
	if (err == -EAGAIN) {
		fprintf(stderr, "SKIP: cross-CPU case requires two allowed CPUs\n");
		return SCX_TEST_SKIP;
	}
	if (err) {
		SCX_ERR("Failed to select test CPUs (%d)", -err);
		return SCX_TEST_FAIL;
	}
	nr_contenders = CPU_COUNT(&allowed_cpus);
	contenders = calloc(nr_contenders, sizeof(*contenders));
	contender_ctxs = calloc(nr_contenders, sizeof(*contender_ctxs));
	if (!contenders || !contender_ctxs) {
		SCX_ERR("Failed to allocate %zu contender threads", nr_contenders);
		status = SCX_TEST_FAIL;
		goto out_contenders;
	}

	skel = enq_blocked__open();
	if (!skel) {
		SCX_ERR("Failed to open skel");
		status = SCX_TEST_FAIL;
		goto out_contenders;
	}
	SCX_ENUM_INIT(skel);
	skel->struct_ops.enq_blocked_ops->flags =
		SCX_OPS_ENQ_LAST |
		(enq_blocked ? SCX_OPS_ENQ_BLOCKED : 0);
	if (enq_blocked__load(skel)) {
		SCX_ERR("Failed to load skel");
		status = SCX_TEST_FAIL;
		goto out_skel;
	}

	err = load_test_module(&module_loaded);
	if (err == -EPERM || err == -ENOENT) {
		fprintf(stderr, "SKIP: cannot load mutex fixture (%d)\n", -err);
		status = SCX_TEST_SKIP;
		goto out_skel;
	}
	if (err) {
		SCX_ERR("Failed to load mutex fixture (%d)", -err);
		status = SCX_TEST_FAIL;
		goto out_skel;
	}

	thread_ctx.fd = open(DEVICE_PATH, O_RDONLY | O_CLOEXEC);
	if (thread_ctx.fd < 0) {
		SCX_ERR("Failed to open %s (%d)", DEVICE_PATH, errno);
		status = SCX_TEST_FAIL;
		goto out_module;
	}
	err = ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_PROXY_SUPPORTED);
	if (err < 0) {
		SCX_ERR("Failed to query proxy-exec support (%d)", errno);
		status = SCX_TEST_FAIL;
		goto out_fd;
	}
	proxy_enabled = err && cmdline_bool("sched_proxy_exec", true);
	if (!proxy_enabled) {
		fprintf(stderr, "SKIP: proxy execution is not enabled\n");
		status = SCX_TEST_SKIP;
		goto out_fd;
	}
	if (ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_RESET_STATS)) {
		SCX_ERR("Failed to reset mutex statistics (%d)", errno);
		status = SCX_TEST_FAIL;
		goto out_fd;
	}

	if (ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_PREP_ATTACH)) {
		SCX_ERR("Failed to prepare scheduler attachment (%d)", errno);
		status = SCX_TEST_FAIL;
		goto out_fd;
	}

	err = pthread_create(&owner, NULL, owner_fn, &thread_ctx);
	if (err) {
		SCX_ERR("Failed to create owner thread (%d)", err);
		status = SCX_TEST_FAIL;
		goto out;
	}
	owner_started = true;

	err = pthread_create(&donor, NULL, donor_fn, &thread_ctx);
	if (err) {
		SCX_ERR("Failed to create donor thread (%d)", err);
		status = SCX_TEST_FAIL;
		goto out;
	}
	donor_started = true;

	if (!wait_for_pid(&thread_ctx.donor_pid)) {
		SCX_ERR("Timed out waiting for donor thread");
		status = SCX_TEST_FAIL;
		goto out;
	}

	donor_pid = atomic_load_explicit(&thread_ctx.donor_pid,
					 memory_order_acquire);
	skel->bss->donor_pid = donor_pid;
	skel->data->donor_cpu = thread_ctx.donor_cpu;
	skel->data->owner_cpu = thread_ctx.owner_cpu;
	atomic_store_explicit(&thread_ctx.start_donor, true,
			      memory_order_release);

	donor_state = ENQ_BLOCKED_DONOR_SLEEPING;
	if (proxy_enabled)
		donor_state |= ENQ_BLOCKED_DONOR_ON_RQ;
	err = wait_for_donor_state(&thread_ctx, donor_state);
	if (err < 0) {
		SCX_ERR("Donor did not block before scheduler attachment (%d)", -err);
		status = SCX_TEST_FAIL;
		goto out;
	}

	link = bpf_map__attach_struct_ops(skel->maps.enq_blocked_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		status = SCX_TEST_FAIL;
		goto out;
	}

	/* Scheduler ownership changes start from a fully blocked donor. */
	donor_state = ENQ_BLOCKED_DONOR_SLEEPING;
	err = wait_for_donor_state(&thread_ctx, donor_state);
	if (err < 0) {
		SCX_ERR("Unexpected donor state after scheduler attachment (%d)",
			-err);
		status = SCX_TEST_FAIL;
		goto out;
	}

	if (ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_ATTACH_DONE)) {
		SCX_ERR("Failed to complete scheduler attachment (%d)", errno);
		status = SCX_TEST_FAIL;
		goto out;
	}

	i = 0;
	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (!CPU_ISSET(cpu, &allowed_cpus))
			continue;

		contender_ctxs[i].thread_ctx = &thread_ctx;
		contender_ctxs[i].cpu = cpu;
		atomic_init(&contender_ctxs[i].status, 0);
		err = pthread_create(&contenders[i], NULL, contender_fn,
				     &contender_ctxs[i]);
		if (err) {
			SCX_ERR("Failed to create contender for CPU %d (%d)",
				cpu, err);
			status = SCX_TEST_FAIL;
			goto out;
		}
		nr_contenders_started++;
		i++;
	}

	err = wait_for_contenders(contender_ctxs, nr_contenders);
	if (err != 1) {
		SCX_ERR("Contender threads failed (%d)", -err);
		status = SCX_TEST_FAIL;
		goto out;
	}

	/*
	 * The first trial spans scheduler attachment and validates the state
	 * transition, but including it would skew scheduling latency. Exclude it
	 * from both the mutex and BPF enqueue measurements.
	 */
	if (!wait_for_donor(&thread_ctx, NR_WARMUP_TRIALS)) {
		SCX_ERR("Timed out waiting for warm-up trial");
		status = SCX_TEST_FAIL;
		goto out;
	}
	if (ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_RESET_STATS)) {
		SCX_ERR("Failed to reset mutex statistics after warm-up (%d)",
			errno);
		status = SCX_TEST_FAIL;
		goto out;
	}
	skel->bss->nr_blocked_enqueues = 0;
	skel->bss->nr_blocked_enqueues_donor_cpu = 0;
	skel->bss->nr_blocked_enqueues_owner_cpu = 0;
	skel->bss->nr_blocked_enqueues_other_cpu = 0;
	skel->bss->nr_blocked_wakeups = 0;
	atomic_store_explicit(&thread_ctx.measurement_ready, true,
			      memory_order_release);

out:
	ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_ATTACH_DONE);
	if (status != SCX_TEST_PASS) {
		atomic_store_explicit(&thread_ctx.abort, true, memory_order_release);
		atomic_store_explicit(&thread_ctx.start_donor, true,
				      memory_order_release);
		atomic_store_explicit(&thread_ctx.measurement_ready, true,
				      memory_order_release);
	}

	set_join_deadline(&join_deadline);
	if (donor_started) {
		err = join_thread(donor, &join_deadline, &thread_err);
		if (err == ETIMEDOUT) {
			SCX_ERR("Timed out waiting for donor thread");
			join_timed_out = true;
			status = SCX_TEST_FAIL;
		} else if (err) {
			SCX_ERR("Failed to join donor thread (%d)", err);
			status = SCX_TEST_FAIL;
		} else {
			donor_started = false;
			if (thread_err) {
				SCX_ERR("Donor thread failed (%d)", thread_err);
				status = SCX_TEST_FAIL;
			}
		}
	}
	if (!join_timed_out && owner_started) {
		err = join_thread(owner, &join_deadline, &thread_err);
		if (err == ETIMEDOUT) {
			SCX_ERR("Timed out waiting for owner thread");
			join_timed_out = true;
			status = SCX_TEST_FAIL;
		} else if (err) {
			SCX_ERR("Failed to join owner thread (%d)", err);
			status = SCX_TEST_FAIL;
		} else {
			owner_started = false;
			if (thread_err) {
				SCX_ERR("Owner thread failed (%d)", thread_err);
				status = SCX_TEST_FAIL;
			}
		}
	}
	atomic_store_explicit(&thread_ctx.stop_contender, true,
			      memory_order_release);
	for (i = 0; !join_timed_out && i < nr_contenders_started; i++) {
		err = join_thread(contenders[i], &join_deadline, &thread_err);
		if (err == ETIMEDOUT) {
			SCX_ERR("Timed out waiting for contender on CPU %d",
				contender_ctxs[i].cpu);
			join_timed_out = true;
			status = SCX_TEST_FAIL;
		} else if (err) {
			SCX_ERR("Failed to join contender on CPU %d (%d)",
				contender_ctxs[i].cpu, err);
			status = SCX_TEST_FAIL;
		} else {
			nr_contenders_joined++;
			if (thread_err) {
				SCX_ERR("Contender on CPU %d failed (%d)",
					contender_ctxs[i].cpu, thread_err);
				status = SCX_TEST_FAIL;
			}
		}
	}

	/* Restore the fair scheduler before waiting for any stranded thread. */
	if (join_timed_out) {
		atomic_store_explicit(&thread_ctx.abort, true,
				      memory_order_release);
		if (link) {
			bpf_link__destroy(link);
			link = NULL;
		}
		if (donor_started)
			pthread_join(donor, NULL);
		if (owner_started)
			pthread_join(owner, NULL);
		for (i = nr_contenders_joined;
		     i < nr_contenders_started; i++)
			pthread_join(contenders[i], NULL);
	}

	if (ioctl(thread_ctx.fd, ENQ_BLOCKED_IOCTL_GET_STATS, &stats)) {
		SCX_ERR("Failed to read mutex statistics (%d)", errno);
		status = SCX_TEST_FAIL;
	} else {
		result->stats = stats;
		printf("\n[topology=%s SCX_OPS_ENQ_BLOCKED=%s]\n",
		       cross_cpu ? "cross-cpu" : "same-cpu",
		       enq_blocked ? "enabled" : "disabled");
		printf("  proxy_exec=%s\n",
		       proxy_enabled ? "enabled" : "disabled");
		printf("  donor_cpu=%d\n", thread_ctx.donor_cpu);
		printf("  owner_cpu=%d\n", thread_ctx.owner_cpu);
		printf("  nr_contenders=%zu\n", nr_contenders);
		printf("  measured_trials=%d\n", NR_MEASURED_TRIALS);
		printf("  owner_nice=%d\n", OWNER_NICE);
		printf("  donor_nice=%d\n", DONOR_NICE);
		printf("  contender_nice=%d\n", CONTENDER_NICE);
		print_avg_time("mutex_hold", stats.hold_time_ns, stats.nr_holds);
		print_avg_time("mutex_wait", stats.wait_time_ns, stats.nr_waits);
		if (stats.nr_holds != NR_MEASURED_TRIALS ||
		    stats.nr_waits != NR_MEASURED_TRIALS) {
			SCX_ERR("Expected %d measured trials, got %llu holds and %llu waits",
				NR_MEASURED_TRIALS,
				(unsigned long long)stats.nr_holds,
				(unsigned long long)stats.nr_waits);
			status = SCX_TEST_FAIL;
		}
	}

	nr_blocked = skel->bss->nr_blocked_enqueues;
	nr_blocked_donor_cpu = skel->bss->nr_blocked_enqueues_donor_cpu;
	nr_blocked_owner_cpu = skel->bss->nr_blocked_enqueues_owner_cpu;
	nr_blocked_other_cpu = skel->bss->nr_blocked_enqueues_other_cpu;
	nr_blocked_wakeups = skel->bss->nr_blocked_wakeups;
	result->nr_blocked_enqueues = nr_blocked;
	result->nr_blocked_enqueues_donor_cpu = nr_blocked_donor_cpu;
	result->nr_blocked_enqueues_owner_cpu = nr_blocked_owner_cpu;
	result->nr_blocked_enqueues_other_cpu = nr_blocked_other_cpu;
	result->nr_blocked_wakeups = nr_blocked_wakeups;
	printf("  nr_blocked_enqueues=%llu\n",
	       (unsigned long long)nr_blocked);
	printf("  nr_blocked_enqueues_donor_cpu=%llu\n",
	       (unsigned long long)nr_blocked_donor_cpu);
	printf("  nr_blocked_enqueues_owner_cpu=%llu\n",
	       (unsigned long long)nr_blocked_owner_cpu);
	printf("  nr_blocked_enqueues_other_cpu=%llu\n",
	       (unsigned long long)nr_blocked_other_cpu);
	printf("  nr_blocked_wakeups=%llu\n",
	       (unsigned long long)nr_blocked_wakeups);
	if (status == SCX_TEST_PASS) {
		if (enq_blocked && proxy_enabled && !nr_blocked) {
			SCX_ERR("ops.enqueue() did not receive the blocked donor");
			status = SCX_TEST_FAIL;
		} else if ((!enq_blocked || !proxy_enabled) && nr_blocked) {
			SCX_ERR("ops.enqueue() unexpectedly received %llu blocked donors",
				(unsigned long long)nr_blocked);
			status = SCX_TEST_FAIL;
		} else if (nr_blocked_wakeups) {
			SCX_ERR("Ordinary wakeups received %llu blocked enqueue flags",
				(unsigned long long)nr_blocked_wakeups);
			status = SCX_TEST_FAIL;
		} else if (nr_blocked_other_cpu) {
			SCX_ERR("Blocked donor had %llu enqueues on unexpected CPUs",
				(unsigned long long)nr_blocked_other_cpu);
			status = SCX_TEST_FAIL;
		}
	}

	if (UEI_EXITED(skel, uei)) {
		SCX_ERR("Scheduler exited unexpectedly (kind=%llu code=%lld)",
			(unsigned long long)skel->data->uei.kind,
			(long long)skel->data->uei.exit_code);
		status = SCX_TEST_FAIL;
	}

	if (link)
		bpf_link__destroy(link);
out_fd:
	close(thread_ctx.fd);
out_module:
	unload_test_module(module_loaded);
out_skel:
	enq_blocked__destroy(skel);
out_contenders:
	free(contender_ctxs);
	free(contenders);
	return status;
}

static enum scx_test_status run_topology(bool cross_cpu)
{
	struct run_result disabled = {}, enabled = {};
	enum scx_test_status status;

	status = run_one(false, cross_cpu, &disabled);
	if (status != SCX_TEST_PASS)
		return status;

	status = run_one(true, cross_cpu, &enabled);
	if (status != SCX_TEST_PASS)
		return status;

	printf("\n[topology=%s delta: enabled - disabled]\n",
	       cross_cpu ? "cross-cpu" : "same-cpu");
	print_avg_delta("mutex_hold", disabled.stats.hold_time_ns,
			disabled.stats.nr_holds, enabled.stats.hold_time_ns,
			enabled.stats.nr_holds);
	print_avg_delta("mutex_wait", disabled.stats.wait_time_ns,
			disabled.stats.nr_waits, enabled.stats.wait_time_ns,
			enabled.stats.nr_waits);

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	enum scx_test_status status;

	(void)ctx;

	status = run_topology(false);
	if (status != SCX_TEST_PASS)
		return status;

	status = run_topology(true);
	if (status == SCX_TEST_SKIP)
		return SCX_TEST_PASS;

	return status;
}

struct scx_test enq_blocked = {
	.name = "enq_blocked",
	.description = "Verify proxy donor admission under CPU-wide contention",
	.setup = setup,
	.run = run,
};

REGISTER_SCX_TEST(&enq_blocked)
