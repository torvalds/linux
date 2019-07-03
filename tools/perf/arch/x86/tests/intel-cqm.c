// SPDX-License-Identifier: GPL-2.0
#include "tests/tests.h"
#include "perf.h"
#include "cloexec.h"
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "arch-tests.h"
#include "util.h"

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

static pid_t spawn(void)
{
	pid_t pid;

	pid = fork();
	if (pid)
		return pid;

	while(1)
		sleep(5);
	return 0;
}

/*
 * Create an event group that contains both a sampled hardware
 * (cpu-cycles) and software (intel_cqm/llc_occupancy/) event. We then
 * wait for the hardware perf counter to overflow and generate a PMI,
 * which triggers an event read for both of the events in the group.
 *
 * Since reading Intel CQM event counters requires sending SMP IPIs, the
 * CQM pmu needs to handle the above situation gracefully, and return
 * the last read counter value to avoid triggering a WARN_ON_ONCE() in
 * smp_call_function_many() caused by sending IPIs from NMI context.
 */
int test__intel_cqm_count_nmi_context(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_evlist *evlist = NULL;
	struct perf_evsel *evsel = NULL;
	struct perf_event_attr pe;
	int i, fd[2], flag, ret;
	size_t mmap_len;
	void *event;
	pid_t pid;
	int err = TEST_FAIL;

	flag = perf_event_open_cloexec_flag();

	evlist = perf_evlist__new();
	if (!evlist) {
		pr_debug("perf_evlist__new failed\n");
		return TEST_FAIL;
	}

	ret = parse_events(evlist, "intel_cqm/llc_occupancy/", NULL);
	if (ret) {
		pr_debug("parse_events failed, is \"intel_cqm/llc_occupancy/\" available?\n");
		err = TEST_SKIP;
		goto out;
	}

	evsel = perf_evlist__first(evlist);
	if (!evsel) {
		pr_debug("perf_evlist__first failed\n");
		goto out;
	}

	memset(&pe, 0, sizeof(pe));
	pe.size = sizeof(pe);

	pe.type = PERF_TYPE_HARDWARE;
	pe.config = PERF_COUNT_HW_CPU_CYCLES;
	pe.read_format = PERF_FORMAT_GROUP;

	pe.sample_period = 128;
	pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_READ;

	pid = spawn();

	fd[0] = sys_perf_event_open(&pe, pid, -1, -1, flag);
	if (fd[0] < 0) {
		pr_debug("failed to open event\n");
		goto out;
	}

	memset(&pe, 0, sizeof(pe));
	pe.size = sizeof(pe);

	pe.type = evsel->attr.type;
	pe.config = evsel->attr.config;

	fd[1] = sys_perf_event_open(&pe, pid, -1, fd[0], flag);
	if (fd[1] < 0) {
		pr_debug("failed to open event\n");
		goto out;
	}

	/*
	 * Pick a power-of-two number of pages + 1 for the meta-data
	 * page (struct perf_event_mmap_page). See tools/perf/design.txt.
	 */
	mmap_len = page_size * 65;

	event = mmap(NULL, mmap_len, PROT_READ, MAP_SHARED, fd[0], 0);
	if (event == (void *)(-1)) {
		pr_debug("failed to mmap %d\n", errno);
		goto out;
	}

	sleep(1);

	err = TEST_OK;

	munmap(event, mmap_len);

	for (i = 0; i < 2; i++)
		close(fd[i]);

	kill(pid, SIGKILL);
	wait(NULL);
out:
	perf_evlist__delete(evlist);
	return err;
}
