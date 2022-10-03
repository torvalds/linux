// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <linux/string.h>
/* For the CLR_() macros */
#include <pthread.h>

#include <sched.h>
#include <perf/mmap.h>
#include "evlist.h"
#include "evsel.h"
#include "debug.h"
#include "record.h"
#include "tests.h"
#include "util/mmap.h"

static int sched__get_first_possible_cpu(pid_t pid, cpu_set_t *maskp)
{
	int i, cpu = -1, nrcpus = 1024;
realloc:
	CPU_ZERO(maskp);

	if (sched_getaffinity(pid, sizeof(*maskp), maskp) == -1) {
		if (errno == EINVAL && nrcpus < (1024 << 8)) {
			nrcpus = nrcpus << 2;
			goto realloc;
		}
		perror("sched_getaffinity");
			return -1;
	}

	for (i = 0; i < nrcpus; i++) {
		if (CPU_ISSET(i, maskp)) {
			if (cpu == -1)
				cpu = i;
			else
				CPU_CLR(i, maskp);
		}
	}

	return cpu;
}

static int test__PERF_RECORD(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct record_opts opts = {
		.target = {
			.uid = UINT_MAX,
			.uses_mmap = true,
		},
		.no_buffering = true,
		.mmap_pages   = 256,
	};
	cpu_set_t cpu_mask;
	size_t cpu_mask_size = sizeof(cpu_mask);
	struct evlist *evlist = evlist__new_dummy();
	struct evsel *evsel;
	struct perf_sample sample;
	const char *cmd = "sleep";
	const char *argv[] = { cmd, "1", NULL, };
	char *bname, *mmap_filename;
	u64 prev_time = 0;
	bool found_cmd_mmap = false,
	     found_coreutils_mmap = false,
	     found_libc_mmap = false,
	     found_vdso_mmap = false,
	     found_ld_mmap = false;
	int err = -1, errs = 0, i, wakeups = 0;
	u32 cpu;
	int total_events = 0, nr_events[PERF_RECORD_MAX] = { 0, };
	char sbuf[STRERR_BUFSIZE];

	if (evlist == NULL) /* Fallback for kernels lacking PERF_COUNT_SW_DUMMY */
		evlist = evlist__new_default();

	if (evlist == NULL) {
		pr_debug("Not enough memory to create evlist\n");
		goto out;
	}

	/*
	 * Create maps of threads and cpus to monitor. In this case
	 * we start with all threads and cpus (-1, -1) but then in
	 * evlist__prepare_workload we'll fill in the only thread
	 * we're monitoring, the one forked there.
	 */
	err = evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	/*
	 * Prepare the workload in argv[] to run, it'll fork it, and then wait
	 * for evlist__start_workload() to exec it. This is done this way
	 * so that we have time to open the evlist (calling sys_perf_event_open
	 * on all the fds) and then mmap them.
	 */
	err = evlist__prepare_workload(evlist, &opts.target, argv, false, NULL);
	if (err < 0) {
		pr_debug("Couldn't run the workload!\n");
		goto out_delete_evlist;
	}

	/*
	 * Config the evsels, setting attr->comm on the first one, etc.
	 */
	evsel = evlist__first(evlist);
	evsel__set_sample_bit(evsel, CPU);
	evsel__set_sample_bit(evsel, TID);
	evsel__set_sample_bit(evsel, TIME);
	evlist__config(evlist, &opts, NULL);

	err = sched__get_first_possible_cpu(evlist->workload.pid, &cpu_mask);
	if (err < 0) {
		pr_debug("sched__get_first_possible_cpu: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	cpu = err;

	/*
	 * So that we can check perf_sample.cpu on all the samples.
	 */
	if (sched_setaffinity(evlist->workload.pid, cpu_mask_size, &cpu_mask) < 0) {
		pr_debug("sched_setaffinity: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	/*
	 * Call sys_perf_event_open on all the fds on all the evsels,
	 * grouping them if asked to.
	 */
	err = evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	/*
	 * mmap the first fd on a given CPU and ask for events for the other
	 * fds in the same CPU to be injected in the same mmap ring buffer
	 * (using ioctl(PERF_EVENT_IOC_SET_OUTPUT)).
	 */
	err = evlist__mmap(evlist, opts.mmap_pages);
	if (err < 0) {
		pr_debug("evlist__mmap: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	/*
	 * Now that all is properly set up, enable the events, they will
	 * count just on workload.pid, which will start...
	 */
	evlist__enable(evlist);

	/*
	 * Now!
	 */
	evlist__start_workload(evlist);

	while (1) {
		int before = total_events;

		for (i = 0; i < evlist->core.nr_mmaps; i++) {
			union perf_event *event;
			struct mmap *md;

			md = &evlist->mmap[i];
			if (perf_mmap__read_init(&md->core) < 0)
				continue;

			while ((event = perf_mmap__read_event(&md->core)) != NULL) {
				const u32 type = event->header.type;
				const char *name = perf_event__name(type);

				++total_events;
				if (type < PERF_RECORD_MAX)
					nr_events[type]++;

				err = evlist__parse_sample(evlist, event, &sample);
				if (err < 0) {
					if (verbose > 0)
						perf_event__fprintf(event, NULL, stderr);
					pr_debug("Couldn't parse sample\n");
					goto out_delete_evlist;
				}

				if (verbose > 0) {
					pr_info("%" PRIu64" %d ", sample.time, sample.cpu);
					perf_event__fprintf(event, NULL, stderr);
				}

				if (prev_time > sample.time) {
					pr_debug("%s going backwards in time, prev=%" PRIu64 ", curr=%" PRIu64 "\n",
						 name, prev_time, sample.time);
					++errs;
				}

				prev_time = sample.time;

				if (sample.cpu != cpu) {
					pr_debug("%s with unexpected cpu, expected %d, got %d\n",
						 name, cpu, sample.cpu);
					++errs;
				}

				if ((pid_t)sample.pid != evlist->workload.pid) {
					pr_debug("%s with unexpected pid, expected %d, got %d\n",
						 name, evlist->workload.pid, sample.pid);
					++errs;
				}

				if ((pid_t)sample.tid != evlist->workload.pid) {
					pr_debug("%s with unexpected tid, expected %d, got %d\n",
						 name, evlist->workload.pid, sample.tid);
					++errs;
				}

				if ((type == PERF_RECORD_COMM ||
				     type == PERF_RECORD_MMAP ||
				     type == PERF_RECORD_MMAP2 ||
				     type == PERF_RECORD_FORK ||
				     type == PERF_RECORD_EXIT) &&
				     (pid_t)event->comm.pid != evlist->workload.pid) {
					pr_debug("%s with unexpected pid/tid\n", name);
					++errs;
				}

				if ((type == PERF_RECORD_COMM ||
				     type == PERF_RECORD_MMAP ||
				     type == PERF_RECORD_MMAP2) &&
				     event->comm.pid != event->comm.tid) {
					pr_debug("%s with different pid/tid!\n", name);
					++errs;
				}

				switch (type) {
				case PERF_RECORD_COMM:
					if (strcmp(event->comm.comm, cmd)) {
						pr_debug("%s with unexpected comm!\n", name);
						++errs;
					}
					break;
				case PERF_RECORD_EXIT:
					goto found_exit;
				case PERF_RECORD_MMAP:
					mmap_filename = event->mmap.filename;
					goto check_bname;
				case PERF_RECORD_MMAP2:
					mmap_filename = event->mmap2.filename;
				check_bname:
					bname = strrchr(mmap_filename, '/');
					if (bname != NULL) {
						if (!found_cmd_mmap)
							found_cmd_mmap = !strcmp(bname + 1, cmd);
						if (!found_coreutils_mmap)
							found_coreutils_mmap = !strcmp(bname + 1, "coreutils");
						if (!found_libc_mmap)
							found_libc_mmap = !strncmp(bname + 1, "libc", 4);
						if (!found_ld_mmap)
							found_ld_mmap = !strncmp(bname + 1, "ld", 2);
					} else if (!found_vdso_mmap)
						found_vdso_mmap = !strcmp(mmap_filename, "[vdso]");
					break;

				case PERF_RECORD_SAMPLE:
					/* Just ignore samples for now */
					break;
				default:
					pr_debug("Unexpected perf_event->header.type %d!\n",
						 type);
					++errs;
				}

				perf_mmap__consume(&md->core);
			}
			perf_mmap__read_done(&md->core);
		}

		/*
		 * We don't use poll here because at least at 3.1 times the
		 * PERF_RECORD_{!SAMPLE} events don't honour
		 * perf_event_attr.wakeup_events, just PERF_EVENT_SAMPLE does.
		 */
		if (total_events == before && false)
			evlist__poll(evlist, -1);

		sleep(1);
		if (++wakeups > 5) {
			pr_debug("No PERF_RECORD_EXIT event!\n");
			break;
		}
	}

found_exit:
	if (nr_events[PERF_RECORD_COMM] > 1 + !!found_coreutils_mmap) {
		pr_debug("Excessive number of PERF_RECORD_COMM events!\n");
		++errs;
	}

	if (nr_events[PERF_RECORD_COMM] == 0) {
		pr_debug("Missing PERF_RECORD_COMM for %s!\n", cmd);
		++errs;
	}

	if (!found_cmd_mmap && !found_coreutils_mmap) {
		pr_debug("PERF_RECORD_MMAP for %s missing!\n", cmd);
		++errs;
	}

	if (!found_libc_mmap) {
		pr_debug("PERF_RECORD_MMAP for %s missing!\n", "libc");
		++errs;
	}

	if (!found_ld_mmap) {
		pr_debug("PERF_RECORD_MMAP for %s missing!\n", "ld");
		++errs;
	}

	if (!found_vdso_mmap) {
		pr_debug("PERF_RECORD_MMAP for %s missing!\n", "[vdso]");
		++errs;
	}
out_delete_evlist:
	evlist__delete(evlist);
out:
	if (err == -EACCES)
		return TEST_SKIP;
	if (err < 0)
		return TEST_FAIL;
	return TEST_OK;
}

static struct test_case tests__PERF_RECORD[] = {
	TEST_CASE_REASON("PERF_RECORD_* events & perf_sample fields",
			 PERF_RECORD,
			 "permissions"),
	{	.name = NULL, }
};

struct test_suite suite__PERF_RECORD = {
	.desc = "PERF_RECORD_* events & perf_sample fields",
	.test_cases = tests__PERF_RECORD,
};
