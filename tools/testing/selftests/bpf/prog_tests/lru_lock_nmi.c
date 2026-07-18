// SPDX-License-Identifier: GPL-2.0
/*
 * Stress every LRU lock-failure and orphan-recovery.
 * perf_event NMI BPF on every online CPU does
 * update+delete on a small LRU map; userspace threads on every CPU do
 * the same from syscall context.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <test_progs.h>
#include "testing_helpers.h"
#include "lru_lock_nmi.skel.h"

#define MAP_ENTRIES	64
#define KEY_RANGE	(MAP_ENTRIES * 2)
#define STRESS_NS	(500 * 1000 * 1000ULL)

struct hammer_arg {
	int map_fd;
	int cpu;
	__u64 deadline_ns;
};

struct refill_arg {
	int map_fd;
	int cpu;
	int per_cpu_quota;
	int update_errors;
};

/*
 * Pin the calling thread to @cpu. Uses dynamically-allocated CPU sets so
 * we stay correct on hosts with @cpu >= CPU_SETSIZE (default 1024).
 */
static int pin_to_cpu(int cpu)
{
	cpu_set_t *cs;
	size_t cs_size;
	int err;

	cs = CPU_ALLOC(cpu + 1);
	if (!cs)
		return -ENOMEM;
	cs_size = CPU_ALLOC_SIZE(cpu + 1);

	CPU_ZERO_S(cs_size, cs);
	CPU_SET_S(cpu, cs_size, cs);
	err = pthread_setaffinity_np(pthread_self(), cs_size, cs);
	CPU_FREE(cs);
	return err;
}

static void *hammer_thread(void *p)
{
	struct hammer_arg *a = p;
	int nr_possible_cpus = libbpf_num_possible_cpus();
	__u64 val[nr_possible_cpus];
	unsigned int seed;
	__u32 key;

	memset(val, 0, sizeof(val));
	pin_to_cpu(a->cpu);

	seed = (unsigned int)a->cpu ^ (unsigned int)(uintptr_t)pthread_self();

	while (get_time_ns() < a->deadline_ns) {
		bool do_update = rand_r(&seed) & 1;

		key = rand_r(&seed) % KEY_RANGE;
		if (do_update)
			bpf_map_update_elem(a->map_fd, &key, val, BPF_ANY);
		else
			bpf_map_delete_elem(a->map_fd, &key);
	}
	return NULL;
}

static void *refill_thread(void *p)
{
	struct refill_arg *a = p;
	int nr_possible_cpus = libbpf_num_possible_cpus();
	__u64 val[nr_possible_cpus];
	__u32 start, end, key;

	memset(val, 0, sizeof(val));
	pin_to_cpu(a->cpu);

	start = (__u32)a->cpu * (__u32)a->per_cpu_quota;
	end   = start + (__u32)a->per_cpu_quota;
	for (key = start; key < end; key++)
		if (bpf_map_update_elem(a->map_fd, &key, val, BPF_ANY))
			a->update_errors++;
	return NULL;
}

/*
 * Drain the map, then refill it with each CPU inserting only its own
 * quota of keys.
 * After refill, lookup every key we inserted - a stranded node on any
 * CPU's pool would have forced eviction.
 */
static int drain_then_verify_capacity(int map_fd, int nr_cpus)
{
	int per_cpu_quota = MAP_ENTRIES / nr_cpus;
	int total = per_cpu_quota * nr_cpus;
	int nr_possible_cpus = libbpf_num_possible_cpus();
	pthread_t threads[nr_cpus];
	struct refill_arg args[nr_cpus];
	__u64 val[nr_possible_cpus];
	int i, hits = 0, nthreads = 0;
	__u32 key;

	memset(val, 0, sizeof(val));

	for (key = 0; key < KEY_RANGE; key++)
		bpf_map_delete_elem(map_fd, &key);

	for (i = 0; i < nr_cpus; i++) {
		args[i] = (struct refill_arg){
			.map_fd = map_fd,
			.cpu = i,
			.per_cpu_quota = per_cpu_quota,
		};
		if (pthread_create(&threads[nthreads], NULL, refill_thread, &args[i]) == 0)
			nthreads++;
	}
	for (i = 0; i < nthreads; i++)
		pthread_join(threads[i], NULL);

	for (i = 0; i < nr_cpus; i++)
		if (args[i].update_errors)
			return -ENOMEM;

	for (key = 0; key < (__u32)total; key++)
		if (bpf_map_lookup_elem(map_fd, &key, val) == 0)
			hits++;

	return hits == total ? 0 : -EIO;
}

static void run_variant(enum bpf_map_type type, __u32 map_flags, const char *name)
{
	struct perf_event_attr attr = {
		.size = sizeof(attr),
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.freq = 1,
	};
	int nr_cpus, max_cpus = 64;
	struct bpf_link *links[max_cpus];
	pthread_t threads[max_cpus];
	struct hammer_arg args[max_cpus];
	struct lru_lock_nmi *skel = NULL;
	int map_fd, i, err, nr_threads = 0, pmu_fd = -1;
	__u64 deadline;

	nr_cpus = libbpf_num_possible_cpus();
	if (!ASSERT_GT(nr_cpus, 0, "num_cpus"))
		return;

	if (nr_cpus > max_cpus)
		nr_cpus = max_cpus;

	if (!test__start_subtest(name))
		return;

	memset(links, 0, sizeof(links));
	skel = lru_lock_nmi__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	err = bpf_map__set_type(skel->maps.lru_map, type);
	if (!ASSERT_OK(err, "set_type"))
		goto cleanup;
	err = bpf_map__set_map_flags(skel->maps.lru_map, map_flags);
	if (!ASSERT_OK(err, "set_flags"))
		goto cleanup;
	err = bpf_map__set_max_entries(skel->maps.lru_map, MAP_ENTRIES);
	if (!ASSERT_OK(err, "set_max_entries"))
		goto cleanup;

	err = lru_lock_nmi__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	skel->bss->hits = 0;
	map_fd = bpf_map__fd(skel->maps.lru_map);
	attr.sample_freq = read_perf_max_sample_freq();

	for (i = 0; i < nr_cpus; i++) {
		pmu_fd = syscall(__NR_perf_event_open, &attr, -1, i, -1, 0);
		if (pmu_fd < 0) {
			if (i == 0 &&
			    (errno == ENOENT || errno == EOPNOTSUPP)) {
				test__skip();
				goto cleanup;
			}
			continue;
		}
		/* libbpf takes ownership of pfd on success */
		links[i] = bpf_program__attach_perf_event(skel->progs.oncpu, pmu_fd);
		if (!links[i])
			close(pmu_fd);
	}

	deadline = get_time_ns() + STRESS_NS;
	for (i = 0; i < nr_cpus; i++) {
		args[i].map_fd = map_fd;
		args[i].cpu = i;
		args[i].deadline_ns = deadline;
		if (pthread_create(&threads[nr_threads], NULL, hammer_thread, &args[i]) == 0)
			nr_threads++;
	}
	for (i = 0; i < nr_threads; i++)
		pthread_join(threads[i], NULL);

	for (i = 0; i < nr_cpus; i++) {
		if (links[i]) {
			bpf_link__destroy(links[i]);
			links[i] = NULL;
		}
	}

	ASSERT_GT(skel->bss->hits, 0, "nmi_bpf_ran");
	ASSERT_OK(drain_then_verify_capacity(map_fd, nr_cpus), "drain_then_verify_capacity");

cleanup:
	for (i = 0; i < nr_cpus; i++) {
		if (links[i])
			bpf_link__destroy(links[i]);
	}
	lru_lock_nmi__destroy(skel);
}

void serial_test_lru_lock_nmi(void)
{
	run_variant(BPF_MAP_TYPE_LRU_HASH, 0, "common_lru");
	run_variant(BPF_MAP_TYPE_LRU_HASH, BPF_F_NO_COMMON_LRU, "no_common_lru");
	run_variant(BPF_MAP_TYPE_LRU_PERCPU_HASH, 0, "percpu_lru");
}
