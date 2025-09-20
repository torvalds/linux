// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <perf/cpumap.h>

#include "cpumap.h"
#include "debug.h"
#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"
#include "tests.h"
#include "util/affinity.h"
#include "util/mmap.h"
#include "util/sample.h"
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <perf/evlist.h>
#include <perf/mmap.h>

/*
 * This test will generate random numbers of calls to some getpid syscalls,
 * then establish an mmap for a group of events that are created to monitor
 * the syscalls.
 *
 * It will receive the events, using mmap, use its PERF_SAMPLE_ID generated
 * sample.id field to map back to its respective perf_evsel instance.
 *
 * Then it checks if the number of syscalls reported as perf events by
 * the kernel corresponds to the number of syscalls made.
 */
static int test__basic_mmap(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int err = TEST_FAIL;
	union perf_event *event;
	struct perf_thread_map *threads;
	struct perf_cpu_map *cpus;
	struct evlist *evlist;
	cpu_set_t cpu_set;
	const char *syscall_names[] = { "getsid", "getppid", "getpgid", };
	pid_t (*syscalls[])(void) = { (void *)getsid, getppid, (void*)getpgid };
#define nsyscalls ARRAY_SIZE(syscall_names)
	unsigned int nr_events[nsyscalls],
		     expected_nr_events[nsyscalls], i, j;
	struct evsel *evsels[nsyscalls], *evsel;
	char sbuf[STRERR_BUFSIZE];
	struct mmap *md;

	threads = thread_map__new_by_tid(getpid());
	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	cpus = perf_cpu_map__new_online_cpus();
	if (cpus == NULL) {
		pr_debug("perf_cpu_map__new\n");
		goto out_free_threads;
	}

	CPU_ZERO(&cpu_set);
	CPU_SET(perf_cpu_map__cpu(cpus, 0).cpu, &cpu_set);
	sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0) {
		pr_debug("sched_setaffinity() failed on CPU %d: %s ",
			 perf_cpu_map__cpu(cpus, 0).cpu,
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_free_cpus;
	}

	evlist = evlist__new();
	if (evlist == NULL) {
		pr_debug("evlist__new\n");
		goto out_free_cpus;
	}

	perf_evlist__set_maps(&evlist->core, cpus, threads);

	for (i = 0; i < nsyscalls; ++i) {
		char name[64];

		snprintf(name, sizeof(name), "sys_enter_%s", syscall_names[i]);
		evsels[i] = evsel__newtp("syscalls", name);
		if (IS_ERR(evsels[i])) {
			pr_debug("evsel__new(%s)\n", name);
			if (PTR_ERR(evsels[i]) == -EACCES) {
				/* Permissions failure, flag the failure as a skip. */
				err = TEST_SKIP;
			}
			goto out_delete_evlist;
		}

		evsels[i]->core.attr.wakeup_events = 1;
		evsel__set_sample_id(evsels[i], false);

		evlist__add(evlist, evsels[i]);

		if (evsel__open(evsels[i], cpus, threads) < 0) {
			pr_debug("failed to open counter: %s, "
				 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
				 str_error_r(errno, sbuf, sizeof(sbuf)));
			goto out_delete_evlist;
		}

		nr_events[i] = 0;
		expected_nr_events[i] = 1 + rand() % 127;
	}

	if (evlist__mmap(evlist, 128) < 0) {
		pr_debug("failed to mmap events: %d (%s)\n", errno,
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	for (i = 0; i < nsyscalls; ++i)
		for (j = 0; j < expected_nr_events[i]; ++j) {
			syscalls[i]();
		}

	md = &evlist->mmap[0];
	if (perf_mmap__read_init(&md->core) < 0)
		goto out_init;

	while ((event = perf_mmap__read_event(&md->core)) != NULL) {
		struct perf_sample sample;

		if (event->header.type != PERF_RECORD_SAMPLE) {
			pr_debug("unexpected %s event\n",
				 perf_event__name(event->header.type));
			goto out_delete_evlist;
		}

		perf_sample__init(&sample, /*all=*/false);
		err = evlist__parse_sample(evlist, event, &sample);
		if (err) {
			pr_err("Can't parse sample, err = %d\n", err);
			perf_sample__exit(&sample);
			goto out_delete_evlist;
		}

		err = -1;
		evsel = evlist__id2evsel(evlist, sample.id);
		perf_sample__exit(&sample);
		if (evsel == NULL) {
			pr_debug("event with id %" PRIu64
				 " doesn't map to an evsel\n", sample.id);
			goto out_delete_evlist;
		}
		nr_events[evsel->core.idx]++;
		perf_mmap__consume(&md->core);
	}
	perf_mmap__read_done(&md->core);

out_init:
	err = 0;
	evlist__for_each_entry(evlist, evsel) {
		if (nr_events[evsel->core.idx] != expected_nr_events[evsel->core.idx]) {
			pr_debug("expected %d %s events, got %d\n",
				 expected_nr_events[evsel->core.idx],
				 evsel__name(evsel), nr_events[evsel->core.idx]);
			err = -1;
			goto out_delete_evlist;
		}
	}

out_delete_evlist:
	evlist__delete(evlist);
out_free_cpus:
	perf_cpu_map__put(cpus);
out_free_threads:
	perf_thread_map__put(threads);
	return err;
}

enum user_read_state {
	USER_READ_ENABLED,
	USER_READ_DISABLED,
	USER_READ_UNKNOWN,
};

static enum user_read_state set_user_read(struct perf_pmu *pmu, enum user_read_state enabled)
{
	char buf[2] = {0, '\n'};
	ssize_t len;
	int events_fd, rdpmc_fd;
	enum user_read_state old_user_read = USER_READ_UNKNOWN;

	if (enabled == USER_READ_UNKNOWN)
		return USER_READ_UNKNOWN;

	events_fd = perf_pmu__event_source_devices_fd();
	if (events_fd < 0)
		return USER_READ_UNKNOWN;

	rdpmc_fd = perf_pmu__pathname_fd(events_fd, pmu->name, "rdpmc", O_RDWR);
	if (rdpmc_fd < 0) {
		close(events_fd);
		return USER_READ_UNKNOWN;
	}

	len = read(rdpmc_fd, buf, sizeof(buf));
	if (len != sizeof(buf))
		pr_debug("%s read failed\n", __func__);

	// Note, on Intel hybrid disabling on 1 PMU will implicitly disable on
	// all the core PMUs.
	old_user_read = (buf[0] == '1') ? USER_READ_ENABLED : USER_READ_DISABLED;

	if (enabled != old_user_read) {
		buf[0] = (enabled == USER_READ_ENABLED) ? '1' : '0';
		len = write(rdpmc_fd, buf, sizeof(buf));
		if (len != sizeof(buf))
			pr_debug("%s write failed\n", __func__);
	}
	close(rdpmc_fd);
	close(events_fd);
	return old_user_read;
}

static int test_stat_user_read(u64 event, enum user_read_state enabled)
{
	struct perf_pmu *pmu = NULL;
	struct perf_thread_map *threads = perf_thread_map__new_dummy();
	int ret = TEST_OK;

	pr_err("User space counter reading %" PRIu64 "\n", event);
	if (!threads) {
		pr_err("User space counter reading [Failed to create threads]\n");
		return TEST_FAIL;
	}
	perf_thread_map__set_pid(threads, 0, 0);

	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		enum user_read_state saved_user_read_state = set_user_read(pmu, enabled);
		struct perf_event_attr attr = {
			.type	= PERF_TYPE_HARDWARE,
			.config	= perf_pmus__supports_extended_type()
			? event | ((u64)pmu->type << PERF_PMU_TYPE_SHIFT)
				: event,
#ifdef __aarch64__
			.config1 = 0x2,		/* Request user access */
#endif
		};
		struct perf_evsel *evsel = NULL;
		int err;
		struct perf_event_mmap_page *pc;
		bool mapped = false, opened = false, rdpmc_supported;
		struct perf_counts_values counts = { .val = 0 };


		pr_debug("User space counter reading for PMU %s\n", pmu->name);
		/*
		 * Restrict scheduling to only use the rdpmc on the CPUs the
		 * event can be on. If the test doesn't run on the CPU of the
		 * event then the event will be disabled and the pc->index test
		 * will fail.
		 */
		if (pmu->cpus != NULL)
			cpu_map__set_affinity(pmu->cpus);

		/* Make the evsel. */
		evsel = perf_evsel__new(&attr);
		if (!evsel) {
			pr_err("User space counter reading for PMU %s [Failed to allocate evsel]\n",
				pmu->name);
			ret = TEST_FAIL;
			goto cleanup;
		}

		err = perf_evsel__open(evsel, NULL, threads);
		if (err) {
			pr_err("User space counter reading for PMU %s [Failed to open evsel]\n",
				pmu->name);
			ret = TEST_SKIP;
			goto cleanup;
		}
		opened = true;
		err = perf_evsel__mmap(evsel, 0);
		if (err) {
			pr_err("User space counter reading for PMU %s [Failed to mmap evsel]\n",
				pmu->name);
			ret = TEST_FAIL;
			goto cleanup;
		}
		mapped = true;

		pc = perf_evsel__mmap_base(evsel, 0, 0);
		if (!pc) {
			pr_err("User space counter reading for PMU %s [Failed to get mmaped address]\n",
				pmu->name);
			ret = TEST_FAIL;
			goto cleanup;
		}

		if (saved_user_read_state == USER_READ_UNKNOWN)
			rdpmc_supported = pc->cap_user_rdpmc && pc->index;
		else
			rdpmc_supported = (enabled == USER_READ_ENABLED);

		if (rdpmc_supported && (!pc->cap_user_rdpmc || !pc->index)) {
			pr_err("User space counter reading for PMU %s [Failed unexpected supported counter access %d %d]\n",
				pmu->name, pc->cap_user_rdpmc, pc->index);
			ret = TEST_FAIL;
			goto cleanup;
		}

		if (!rdpmc_supported && pc->cap_user_rdpmc) {
			pr_err("User space counter reading for PMU %s [Failed unexpected unsupported counter access %d]\n",
				pmu->name, pc->cap_user_rdpmc);
			ret = TEST_FAIL;
			goto cleanup;
		}

		if (rdpmc_supported && pc->pmc_width < 32) {
			pr_err("User space counter reading for PMU %s [Failed width not set %d]\n",
				pmu->name, pc->pmc_width);
			ret = TEST_FAIL;
			goto cleanup;
		}

		perf_evsel__read(evsel, 0, 0, &counts);
		if (counts.val == 0) {
			pr_err("User space counter reading for PMU %s [Failed read]\n", pmu->name);
			ret = TEST_FAIL;
			goto cleanup;
		}

		for (int i = 0; i < 5; i++) {
			volatile int count = 0x10000 << i;
			__u64 start, end, last = 0;

			pr_debug("\tloop = %u, ", count);

			perf_evsel__read(evsel, 0, 0, &counts);
			start = counts.val;

			while (count--) ;

			perf_evsel__read(evsel, 0, 0, &counts);
			end = counts.val;

			if ((end - start) < last) {
				pr_err("User space counter reading for PMU %s [Failed invalid counter data: end=%llu start=%llu last= %llu]\n",
					pmu->name, end, start, last);
				ret = TEST_FAIL;
				goto cleanup;
			}
			last = end - start;
			pr_debug("count = %llu\n", last);
		}
		pr_debug("User space counter reading for PMU %s [Success]\n", pmu->name);
cleanup:
		if (mapped)
			perf_evsel__munmap(evsel);
		if (opened)
			perf_evsel__close(evsel);
		perf_evsel__delete(evsel);

		/* If the affinity was changed, then put it back to all CPUs. */
		if (pmu->cpus != NULL) {
			struct perf_cpu_map *cpus = cpu_map__online();

			cpu_map__set_affinity(cpus);
			perf_cpu_map__put(cpus);
		}
		set_user_read(pmu, saved_user_read_state);
	}
	perf_thread_map__put(threads);
	return ret;
}

static int test__mmap_user_read_instr(struct test_suite *test __maybe_unused,
				      int subtest __maybe_unused)
{
	return test_stat_user_read(PERF_COUNT_HW_INSTRUCTIONS, USER_READ_ENABLED);
}

static int test__mmap_user_read_cycles(struct test_suite *test __maybe_unused,
				       int subtest __maybe_unused)
{
	return test_stat_user_read(PERF_COUNT_HW_CPU_CYCLES, USER_READ_ENABLED);
}

static int test__mmap_user_read_instr_disabled(struct test_suite *test __maybe_unused,
					       int subtest __maybe_unused)
{
	return test_stat_user_read(PERF_COUNT_HW_INSTRUCTIONS, USER_READ_DISABLED);
}

static int test__mmap_user_read_cycles_disabled(struct test_suite *test __maybe_unused,
						int subtest __maybe_unused)
{
	return test_stat_user_read(PERF_COUNT_HW_CPU_CYCLES, USER_READ_DISABLED);
}

static struct test_case tests__basic_mmap[] = {
	TEST_CASE_REASON("Read samples using the mmap interface",
			 basic_mmap,
			 "permissions"),
	TEST_CASE_REASON_EXCLUSIVE("User space counter reading of instructions",
			 mmap_user_read_instr,
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
			 (defined(__riscv) && __riscv_xlen == 64)
			 "permissions"
#else
			 "unsupported"
#endif
		),
	TEST_CASE_REASON_EXCLUSIVE("User space counter reading of cycles",
			 mmap_user_read_cycles,
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
			 (defined(__riscv) && __riscv_xlen == 64)
			 "permissions"
#else
			 "unsupported"
#endif
		),
	TEST_CASE_REASON_EXCLUSIVE("User space counter disabling instructions",
			 mmap_user_read_instr_disabled,
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
			 (defined(__riscv) && __riscv_xlen == 64)
			 "permissions"
#else
			 "unsupported"
#endif
		),
	TEST_CASE_REASON_EXCLUSIVE("User space counter disabling cycles",
			 mmap_user_read_cycles_disabled,
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
			 (defined(__riscv) && __riscv_xlen == 64)
			 "permissions"
#else
			 "unsupported"
#endif
		),
	{	.name = NULL, }
};

struct test_suite suite__basic_mmap = {
	.desc = "mmap interface tests",
	.test_cases = tests__basic_mmap,
};
