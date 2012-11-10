/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include "builtin.h"

#include "util/cache.h"
#include "util/color.h"
#include "util/debug.h"
#include "util/debugfs.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/symbol.h"
#include "util/thread_map.h"
#include "util/pmu.h"
#include "event-parse.h"
#include "../../include/linux/hw_breakpoint.h"

#include <sys/mman.h>

#include "util/cpumap.h"
#include "util/evsel.h"
#include <sys/types.h>

#include "tests.h"

#include <sched.h>



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

static int test__PERF_RECORD(void)
{
	struct perf_record_opts opts = {
		.target = {
			.uid = UINT_MAX,
			.uses_mmap = true,
		},
		.no_delay   = true,
		.freq	    = 10,
		.mmap_pages = 256,
	};
	cpu_set_t cpu_mask;
	size_t cpu_mask_size = sizeof(cpu_mask);
	struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);
	struct perf_evsel *evsel;
	struct perf_sample sample;
	const char *cmd = "sleep";
	const char *argv[] = { cmd, "1", NULL, };
	char *bname;
	u64 prev_time = 0;
	bool found_cmd_mmap = false,
	     found_libc_mmap = false,
	     found_vdso_mmap = false,
	     found_ld_mmap = false;
	int err = -1, errs = 0, i, wakeups = 0;
	u32 cpu;
	int total_events = 0, nr_events[PERF_RECORD_MAX] = { 0, };

	if (evlist == NULL || argv == NULL) {
		pr_debug("Not enough memory to create evlist\n");
		goto out;
	}

	/*
	 * We need at least one evsel in the evlist, use the default
	 * one: "cycles".
	 */
	err = perf_evlist__add_default(evlist);
	if (err < 0) {
		pr_debug("Not enough memory to create evsel\n");
		goto out_delete_evlist;
	}

	/*
	 * Create maps of threads and cpus to monitor. In this case
	 * we start with all threads and cpus (-1, -1) but then in
	 * perf_evlist__prepare_workload we'll fill in the only thread
	 * we're monitoring, the one forked there.
	 */
	err = perf_evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	/*
	 * Prepare the workload in argv[] to run, it'll fork it, and then wait
	 * for perf_evlist__start_workload() to exec it. This is done this way
	 * so that we have time to open the evlist (calling sys_perf_event_open
	 * on all the fds) and then mmap them.
	 */
	err = perf_evlist__prepare_workload(evlist, &opts, argv);
	if (err < 0) {
		pr_debug("Couldn't run the workload!\n");
		goto out_delete_evlist;
	}

	/*
	 * Config the evsels, setting attr->comm on the first one, etc.
	 */
	evsel = perf_evlist__first(evlist);
	evsel->attr.sample_type |= PERF_SAMPLE_CPU;
	evsel->attr.sample_type |= PERF_SAMPLE_TID;
	evsel->attr.sample_type |= PERF_SAMPLE_TIME;
	perf_evlist__config_attrs(evlist, &opts);

	err = sched__get_first_possible_cpu(evlist->workload.pid, &cpu_mask);
	if (err < 0) {
		pr_debug("sched__get_first_possible_cpu: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	cpu = err;

	/*
	 * So that we can check perf_sample.cpu on all the samples.
	 */
	if (sched_setaffinity(evlist->workload.pid, cpu_mask_size, &cpu_mask) < 0) {
		pr_debug("sched_setaffinity: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	/*
	 * Call sys_perf_event_open on all the fds on all the evsels,
	 * grouping them if asked to.
	 */
	err = perf_evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	/*
	 * mmap the first fd on a given CPU and ask for events for the other
	 * fds in the same CPU to be injected in the same mmap ring buffer
	 * (using ioctl(PERF_EVENT_IOC_SET_OUTPUT)).
	 */
	err = perf_evlist__mmap(evlist, opts.mmap_pages, false);
	if (err < 0) {
		pr_debug("perf_evlist__mmap: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	/*
	 * Now that all is properly set up, enable the events, they will
	 * count just on workload.pid, which will start...
	 */
	perf_evlist__enable(evlist);

	/*
	 * Now!
	 */
	perf_evlist__start_workload(evlist);

	while (1) {
		int before = total_events;

		for (i = 0; i < evlist->nr_mmaps; i++) {
			union perf_event *event;

			while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
				const u32 type = event->header.type;
				const char *name = perf_event__name(type);

				++total_events;
				if (type < PERF_RECORD_MAX)
					nr_events[type]++;

				err = perf_evlist__parse_sample(evlist, event, &sample);
				if (err < 0) {
					if (verbose)
						perf_event__fprintf(event, stderr);
					pr_debug("Couldn't parse sample\n");
					goto out_err;
				}

				if (verbose) {
					pr_info("%" PRIu64" %d ", sample.time, sample.cpu);
					perf_event__fprintf(event, stderr);
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
				     type == PERF_RECORD_FORK ||
				     type == PERF_RECORD_EXIT) &&
				     (pid_t)event->comm.pid != evlist->workload.pid) {
					pr_debug("%s with unexpected pid/tid\n", name);
					++errs;
				}

				if ((type == PERF_RECORD_COMM ||
				     type == PERF_RECORD_MMAP) &&
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
					bname = strrchr(event->mmap.filename, '/');
					if (bname != NULL) {
						if (!found_cmd_mmap)
							found_cmd_mmap = !strcmp(bname + 1, cmd);
						if (!found_libc_mmap)
							found_libc_mmap = !strncmp(bname + 1, "libc", 4);
						if (!found_ld_mmap)
							found_ld_mmap = !strncmp(bname + 1, "ld", 2);
					} else if (!found_vdso_mmap)
						found_vdso_mmap = !strcmp(event->mmap.filename, "[vdso]");
					break;

				case PERF_RECORD_SAMPLE:
					/* Just ignore samples for now */
					break;
				default:
					pr_debug("Unexpected perf_event->header.type %d!\n",
						 type);
					++errs;
				}
			}
		}

		/*
		 * We don't use poll here because at least at 3.1 times the
		 * PERF_RECORD_{!SAMPLE} events don't honour
		 * perf_event_attr.wakeup_events, just PERF_EVENT_SAMPLE does.
		 */
		if (total_events == before && false)
			poll(evlist->pollfd, evlist->nr_fds, -1);

		sleep(1);
		if (++wakeups > 5) {
			pr_debug("No PERF_RECORD_EXIT event!\n");
			break;
		}
	}

found_exit:
	if (nr_events[PERF_RECORD_COMM] > 1) {
		pr_debug("Excessive number of PERF_RECORD_COMM events!\n");
		++errs;
	}

	if (nr_events[PERF_RECORD_COMM] == 0) {
		pr_debug("Missing PERF_RECORD_COMM for %s!\n", cmd);
		++errs;
	}

	if (!found_cmd_mmap) {
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
out_err:
	perf_evlist__munmap(evlist);
out_delete_evlist:
	perf_evlist__delete(evlist);
out:
	return (err < 0 || errs > 0) ? -1 : 0;
}


#if defined(__x86_64__) || defined(__i386__)

#define barrier() asm volatile("" ::: "memory")

static u64 rdpmc(unsigned int counter)
{
	unsigned int low, high;

	asm volatile("rdpmc" : "=a" (low), "=d" (high) : "c" (counter));

	return low | ((u64)high) << 32;
}

static u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}

static u64 mmap_read_self(void *addr)
{
	struct perf_event_mmap_page *pc = addr;
	u32 seq, idx, time_mult = 0, time_shift = 0;
	u64 count, cyc = 0, time_offset = 0, enabled, running, delta;

	do {
		seq = pc->lock;
		barrier();

		enabled = pc->time_enabled;
		running = pc->time_running;

		if (enabled != running) {
			cyc = rdtsc();
			time_mult = pc->time_mult;
			time_shift = pc->time_shift;
			time_offset = pc->time_offset;
		}

		idx = pc->index;
		count = pc->offset;
		if (idx)
			count += rdpmc(idx - 1);

		barrier();
	} while (pc->lock != seq);

	if (enabled != running) {
		u64 quot, rem;

		quot = (cyc >> time_shift);
		rem = cyc & ((1 << time_shift) - 1);
		delta = time_offset + quot * time_mult +
			((rem * time_mult) >> time_shift);

		enabled += delta;
		if (idx)
			running += delta;

		quot = count / running;
		rem = count % running;
		count = quot * enabled + (rem * enabled) / running;
	}

	return count;
}

/*
 * If the RDPMC instruction faults then signal this back to the test parent task:
 */
static void segfault_handler(int sig __maybe_unused,
			     siginfo_t *info __maybe_unused,
			     void *uc __maybe_unused)
{
	exit(-1);
}

static int __test__rdpmc(void)
{
	volatile int tmp = 0;
	u64 i, loops = 1000;
	int n;
	int fd;
	void *addr;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_INSTRUCTIONS,
		.exclude_kernel = 1,
	};
	u64 delta_sum = 0;
        struct sigaction sa;

	sigfillset(&sa.sa_mask);
	sa.sa_sigaction = segfault_handler;
	sigaction(SIGSEGV, &sa, NULL);

	fd = sys_perf_event_open(&attr, 0, -1, -1, 0);
	if (fd < 0) {
		pr_err("Error: sys_perf_event_open() syscall returned "
		       "with %d (%s)\n", fd, strerror(errno));
		return -1;
	}

	addr = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == (void *)(-1)) {
		pr_err("Error: mmap() syscall returned with (%s)\n",
		       strerror(errno));
		goto out_close;
	}

	for (n = 0; n < 6; n++) {
		u64 stamp, now, delta;

		stamp = mmap_read_self(addr);

		for (i = 0; i < loops; i++)
			tmp++;

		now = mmap_read_self(addr);
		loops *= 10;

		delta = now - stamp;
		pr_debug("%14d: %14Lu\n", n, (long long)delta);

		delta_sum += delta;
	}

	munmap(addr, page_size);
	pr_debug("   ");
out_close:
	close(fd);

	if (!delta_sum)
		return -1;

	return 0;
}

static int test__rdpmc(void)
{
	int status = 0;
	int wret = 0;
	int ret;
	int pid;

	pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		ret = __test__rdpmc();

		exit(ret);
	}

	wret = waitpid(pid, &status, 0);
	if (wret < 0 || status)
		return -1;

	return 0;
}

#endif

static int test__perf_pmu(void)
{
	return perf_pmu__test();
}

static int perf_evsel__roundtrip_cache_name_test(void)
{
	char name[128];
	int type, op, err = 0, ret = 0, i, idx;
	struct perf_evsel *evsel;
        struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);

        if (evlist == NULL)
                return -ENOMEM;

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				err = parse_events(evlist, name, 0);
				if (err)
					ret = err;
			}
		}
	}

	idx = 0;
	evsel = perf_evlist__first(evlist);

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				if (evsel->idx != idx)
					continue;

				++idx;

				if (strcmp(perf_evsel__name(evsel), name)) {
					pr_debug("%s != %s\n", perf_evsel__name(evsel), name);
					ret = -1;
				}

				evsel = perf_evsel__next(evsel);
			}
		}
	}

	perf_evlist__delete(evlist);
	return ret;
}

static int __perf_evsel__name_array_test(const char *names[], int nr_names)
{
	int i, err;
	struct perf_evsel *evsel;
        struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);

        if (evlist == NULL)
                return -ENOMEM;

	for (i = 0; i < nr_names; ++i) {
		err = parse_events(evlist, names[i], 0);
		if (err) {
			pr_debug("failed to parse event '%s', err %d\n",
				 names[i], err);
			goto out_delete_evlist;
		}
	}

	err = 0;
	list_for_each_entry(evsel, &evlist->entries, node) {
		if (strcmp(perf_evsel__name(evsel), names[evsel->idx])) {
			--err;
			pr_debug("%s != %s\n", perf_evsel__name(evsel), names[evsel->idx]);
		}
	}

out_delete_evlist:
	perf_evlist__delete(evlist);
	return err;
}

#define perf_evsel__name_array_test(names) \
	__perf_evsel__name_array_test(names, ARRAY_SIZE(names))

static int perf_evsel__roundtrip_name_test(void)
{
	int err = 0, ret = 0;

	err = perf_evsel__name_array_test(perf_evsel__hw_names);
	if (err)
		ret = err;

	err = perf_evsel__name_array_test(perf_evsel__sw_names);
	if (err)
		ret = err;

	err = perf_evsel__roundtrip_cache_name_test();
	if (err)
		ret = err;

	return ret;
}

static int perf_evsel__test_field(struct perf_evsel *evsel, const char *name,
				  int size, bool should_be_signed)
{
	struct format_field *field = perf_evsel__field(evsel, name);
	int is_signed;
	int ret = 0;

	if (field == NULL) {
		pr_debug("%s: \"%s\" field not found!\n", evsel->name, name);
		return -1;
	}

	is_signed = !!(field->flags | FIELD_IS_SIGNED);
	if (should_be_signed && !is_signed) {
		pr_debug("%s: \"%s\" signedness(%d) is wrong, should be %d\n",
			 evsel->name, name, is_signed, should_be_signed);
		ret = -1;
	}

	if (field->size != size) {
		pr_debug("%s: \"%s\" size (%d) should be %d!\n",
			 evsel->name, name, field->size, size);
		ret = -1;
	}

	return ret;
}

static int perf_evsel__tp_sched_test(void)
{
	struct perf_evsel *evsel = perf_evsel__newtp("sched", "sched_switch", 0);
	int ret = 0;

	if (evsel == NULL) {
		pr_debug("perf_evsel__new\n");
		return -1;
	}

	if (perf_evsel__test_field(evsel, "prev_comm", 16, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prev_pid", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prev_prio", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prev_state", 8, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "next_comm", 16, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "next_pid", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "next_prio", 4, true))
		ret = -1;

	perf_evsel__delete(evsel);

	evsel = perf_evsel__newtp("sched", "sched_wakeup", 0);

	if (perf_evsel__test_field(evsel, "comm", 16, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "pid", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prio", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "success", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "target_cpu", 4, true))
		ret = -1;

	return ret;
}

static int test__syscall_open_tp_fields(void)
{
	struct perf_record_opts opts = {
		.target = {
			.uid = UINT_MAX,
			.uses_mmap = true,
		},
		.no_delay   = true,
		.freq	    = 1,
		.mmap_pages = 256,
		.raw_samples = true,
	};
	const char *filename = "/etc/passwd";
	int flags = O_RDONLY | O_DIRECTORY;
	struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);
	struct perf_evsel *evsel;
	int err = -1, i, nr_events = 0, nr_polls = 0;

	if (evlist == NULL) {
		pr_debug("%s: perf_evlist__new\n", __func__);
		goto out;
	}

	evsel = perf_evsel__newtp("syscalls", "sys_enter_open", 0);
	if (evsel == NULL) {
		pr_debug("%s: perf_evsel__newtp\n", __func__);
		goto out_delete_evlist;
	}

	perf_evlist__add(evlist, evsel);

	err = perf_evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("%s: perf_evlist__create_maps\n", __func__);
		goto out_delete_evlist;
	}

	perf_evsel__config(evsel, &opts, evsel);

	evlist->threads->map[0] = getpid();

	err = perf_evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	err = perf_evlist__mmap(evlist, UINT_MAX, false);
	if (err < 0) {
		pr_debug("perf_evlist__mmap: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	perf_evlist__enable(evlist);

	/*
	 * Generate the event:
	 */
	open(filename, flags);

	while (1) {
		int before = nr_events;

		for (i = 0; i < evlist->nr_mmaps; i++) {
			union perf_event *event;

			while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
				const u32 type = event->header.type;
				int tp_flags;
				struct perf_sample sample;

				++nr_events;

				if (type != PERF_RECORD_SAMPLE)
					continue;

				err = perf_evsel__parse_sample(evsel, event, &sample);
				if (err) {
					pr_err("Can't parse sample, err = %d\n", err);
					goto out_munmap;
				}

				tp_flags = perf_evsel__intval(evsel, &sample, "flags");

				if (flags != tp_flags) {
					pr_debug("%s: Expected flags=%#x, got %#x\n",
						 __func__, flags, tp_flags);
					goto out_munmap;
				}

				goto out_ok;
			}
		}

		if (nr_events == before)
			poll(evlist->pollfd, evlist->nr_fds, 10);

		if (++nr_polls > 5) {
			pr_debug("%s: no events!\n", __func__);
			goto out_munmap;
		}
	}
out_ok:
	err = 0;
out_munmap:
	perf_evlist__munmap(evlist);
out_delete_evlist:
	perf_evlist__delete(evlist);
out:
	return err;
}

static struct test {
	const char *desc;
	int (*func)(void);
} tests[] = {
	{
		.desc = "vmlinux symtab matches kallsyms",
		.func = test__vmlinux_matches_kallsyms,
	},
	{
		.desc = "detect open syscall event",
		.func = test__open_syscall_event,
	},
	{
		.desc = "detect open syscall event on all cpus",
		.func = test__open_syscall_event_on_all_cpus,
	},
	{
		.desc = "read samples using the mmap interface",
		.func = test__basic_mmap,
	},
	{
		.desc = "parse events tests",
		.func = parse_events__test,
	},
#if defined(__x86_64__) || defined(__i386__)
	{
		.desc = "x86 rdpmc test",
		.func = test__rdpmc,
	},
#endif
	{
		.desc = "Validate PERF_RECORD_* events & perf_sample fields",
		.func = test__PERF_RECORD,
	},
	{
		.desc = "Test perf pmu format parsing",
		.func = test__perf_pmu,
	},
	{
		.desc = "Test dso data interface",
		.func = dso__test_data,
	},
	{
		.desc = "roundtrip evsel->name check",
		.func = perf_evsel__roundtrip_name_test,
	},
	{
		.desc = "Check parsing of sched tracepoints fields",
		.func = perf_evsel__tp_sched_test,
	},
	{
		.desc = "Generate and check syscalls:sys_enter_open event fields",
		.func = test__syscall_open_tp_fields,
	},
	{
		.desc = "struct perf_event_attr setup",
		.func = test_attr__run,
	},
	{
		.func = NULL,
	},
};

static bool perf_test__matches(int curr, int argc, const char *argv[])
{
	int i;

	if (argc == 0)
		return true;

	for (i = 0; i < argc; ++i) {
		char *end;
		long nr = strtoul(argv[i], &end, 10);

		if (*end == '\0') {
			if (nr == curr + 1)
				return true;
			continue;
		}

		if (strstr(tests[curr].desc, argv[i]))
			return true;
	}

	return false;
}

static int __cmd_test(int argc, const char *argv[])
{
	int i = 0;
	int width = 0;

	while (tests[i].func) {
		int len = strlen(tests[i].desc);

		if (width < len)
			width = len;
		++i;
	}

	i = 0;
	while (tests[i].func) {
		int curr = i++, err;

		if (!perf_test__matches(curr, argc, argv))
			continue;

		pr_info("%2d: %-*s:", i, width, tests[curr].desc);
		pr_debug("\n--- start ---\n");
		err = tests[curr].func();
		pr_debug("---- end ----\n%s:", tests[curr].desc);
		if (err)
			color_fprintf(stderr, PERF_COLOR_RED, " FAILED!\n");
		else
			pr_info(" Ok\n");
	}

	return 0;
}

static int perf_test__list(int argc, const char **argv)
{
	int i = 0;

	while (tests[i].func) {
		int curr = i++;

		if (argc > 1 && !strstr(tests[curr].desc, argv[1]))
			continue;

		pr_info("%2d: %s\n", i, tests[curr].desc);
	}

	return 0;
}

int cmd_test(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const test_usage[] = {
	"perf test [<options>] [{list <test-name-fragment>|[<test-name-fragments>|<test-numbers>]}]",
	NULL,
	};
	const struct option test_options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_END()
	};

	argc = parse_options(argc, argv, test_options, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list"))
		return perf_test__list(argc, argv);

	symbol_conf.priv_size = sizeof(int);
	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init() < 0)
		return -1;

	return __cmd_test(argc, argv);
}
