/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include "builtin.h"

#include "util/cache.h"
#include "util/debug.h"
#include "util/debugfs.h"
#include "util/evlist.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/symbol.h"
#include "util/thread_map.h"
#include "util/pmu.h"
#include "../../include/linux/hw_breakpoint.h"

#include <sys/mman.h>

static int vmlinux_matches_kallsyms_filter(struct map *map __used, struct symbol *sym)
{
	bool *visited = symbol__priv(sym);
	*visited = true;
	return 0;
}

static int test__vmlinux_matches_kallsyms(void)
{
	int err = -1;
	struct rb_node *nd;
	struct symbol *sym;
	struct map *kallsyms_map, *vmlinux_map;
	struct machine kallsyms, vmlinux;
	enum map_type type = MAP__FUNCTION;
	long page_size = sysconf(_SC_PAGE_SIZE);
	struct ref_reloc_sym ref_reloc_sym = { .name = "_stext", };

	/*
	 * Step 1:
	 *
	 * Init the machines that will hold kernel, modules obtained from
	 * both vmlinux + .ko files and from /proc/kallsyms split by modules.
	 */
	machine__init(&kallsyms, "", HOST_KERNEL_ID);
	machine__init(&vmlinux, "", HOST_KERNEL_ID);

	/*
	 * Step 2:
	 *
	 * Create the kernel maps for kallsyms and the DSO where we will then
	 * load /proc/kallsyms. Also create the modules maps from /proc/modules
	 * and find the .ko files that match them in /lib/modules/`uname -r`/.
	 */
	if (machine__create_kernel_maps(&kallsyms) < 0) {
		pr_debug("machine__create_kernel_maps ");
		return -1;
	}

	/*
	 * Step 3:
	 *
	 * Load and split /proc/kallsyms into multiple maps, one per module.
	 */
	if (machine__load_kallsyms(&kallsyms, "/proc/kallsyms", type, NULL) <= 0) {
		pr_debug("dso__load_kallsyms ");
		goto out;
	}

	/*
	 * Step 4:
	 *
	 * kallsyms will be internally on demand sorted by name so that we can
	 * find the reference relocation * symbol, i.e. the symbol we will use
	 * to see if the running kernel was relocated by checking if it has the
	 * same value in the vmlinux file we load.
	 */
	kallsyms_map = machine__kernel_map(&kallsyms, type);

	sym = map__find_symbol_by_name(kallsyms_map, ref_reloc_sym.name, NULL);
	if (sym == NULL) {
		pr_debug("dso__find_symbol_by_name ");
		goto out;
	}

	ref_reloc_sym.addr = sym->start;

	/*
	 * Step 5:
	 *
	 * Now repeat step 2, this time for the vmlinux file we'll auto-locate.
	 */
	if (machine__create_kernel_maps(&vmlinux) < 0) {
		pr_debug("machine__create_kernel_maps ");
		goto out;
	}

	vmlinux_map = machine__kernel_map(&vmlinux, type);
	map__kmap(vmlinux_map)->ref_reloc_sym = &ref_reloc_sym;

	/*
	 * Step 6:
	 *
	 * Locate a vmlinux file in the vmlinux path that has a buildid that
	 * matches the one of the running kernel.
	 *
	 * While doing that look if we find the ref reloc symbol, if we find it
	 * we'll have its ref_reloc_symbol.unrelocated_addr and then
	 * maps__reloc_vmlinux will notice and set proper ->[un]map_ip routines
	 * to fixup the symbols.
	 */
	if (machine__load_vmlinux_path(&vmlinux, type,
				       vmlinux_matches_kallsyms_filter) <= 0) {
		pr_debug("machine__load_vmlinux_path ");
		goto out;
	}

	err = 0;
	/*
	 * Step 7:
	 *
	 * Now look at the symbols in the vmlinux DSO and check if we find all of them
	 * in the kallsyms dso. For the ones that are in both, check its names and
	 * end addresses too.
	 */
	for (nd = rb_first(&vmlinux_map->dso->symbols[type]); nd; nd = rb_next(nd)) {
		struct symbol *pair, *first_pair;
		bool backwards = true;

		sym  = rb_entry(nd, struct symbol, rb_node);

		if (sym->start == sym->end)
			continue;

		first_pair = machine__find_kernel_symbol(&kallsyms, type, sym->start, NULL, NULL);
		pair = first_pair;

		if (pair && pair->start == sym->start) {
next_pair:
			if (strcmp(sym->name, pair->name) == 0) {
				/*
				 * kallsyms don't have the symbol end, so we
				 * set that by using the next symbol start - 1,
				 * in some cases we get this up to a page
				 * wrong, trace_kmalloc when I was developing
				 * this code was one such example, 2106 bytes
				 * off the real size. More than that and we
				 * _really_ have a problem.
				 */
				s64 skew = sym->end - pair->end;
				if (llabs(skew) < page_size)
					continue;

				pr_debug("%#" PRIx64 ": diff end addr for %s v: %#" PRIx64 " k: %#" PRIx64 "\n",
					 sym->start, sym->name, sym->end, pair->end);
			} else {
				struct rb_node *nnd;
detour:
				nnd = backwards ? rb_prev(&pair->rb_node) :
						  rb_next(&pair->rb_node);
				if (nnd) {
					struct symbol *next = rb_entry(nnd, struct symbol, rb_node);

					if (next->start == sym->start) {
						pair = next;
						goto next_pair;
					}
				}

				if (backwards) {
					backwards = false;
					pair = first_pair;
					goto detour;
				}

				pr_debug("%#" PRIx64 ": diff name v: %s k: %s\n",
					 sym->start, sym->name, pair->name);
			}
		} else
			pr_debug("%#" PRIx64 ": %s not on kallsyms\n", sym->start, sym->name);

		err = -1;
	}

	if (!verbose)
		goto out;

	pr_info("Maps only in vmlinux:\n");

	for (nd = rb_first(&vmlinux.kmaps.maps[type]); nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node), *pair;
		/*
		 * If it is the kernel, kallsyms is always "[kernel.kallsyms]", while
		 * the kernel will have the path for the vmlinux file being used,
		 * so use the short name, less descriptive but the same ("[kernel]" in
		 * both cases.
		 */
		pair = map_groups__find_by_name(&kallsyms.kmaps, type,
						(pos->dso->kernel ?
							pos->dso->short_name :
							pos->dso->name));
		if (pair)
			pair->priv = 1;
		else
			map__fprintf(pos, stderr);
	}

	pr_info("Maps in vmlinux with a different name in kallsyms:\n");

	for (nd = rb_first(&vmlinux.kmaps.maps[type]); nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node), *pair;

		pair = map_groups__find(&kallsyms.kmaps, type, pos->start);
		if (pair == NULL || pair->priv)
			continue;

		if (pair->start == pos->start) {
			pair->priv = 1;
			pr_info(" %" PRIx64 "-%" PRIx64 " %" PRIx64 " %s in kallsyms as",
				pos->start, pos->end, pos->pgoff, pos->dso->name);
			if (pos->pgoff != pair->pgoff || pos->end != pair->end)
				pr_info(": \n*%" PRIx64 "-%" PRIx64 " %" PRIx64 "",
					pair->start, pair->end, pair->pgoff);
			pr_info(" %s\n", pair->dso->name);
			pair->priv = 1;
		}
	}

	pr_info("Maps only in kallsyms:\n");

	for (nd = rb_first(&kallsyms.kmaps.maps[type]);
	     nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node);

		if (!pos->priv)
			map__fprintf(pos, stderr);
	}
out:
	return err;
}

#include "util/cpumap.h"
#include "util/evsel.h"
#include <sys/types.h>

static int trace_event__id(const char *evname)
{
	char *filename;
	int err = -1, fd;

	if (asprintf(&filename,
		     "%s/syscalls/%s/id",
		     tracing_events_path, evname) < 0)
		return -1;

	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		char id[16];
		if (read(fd, id, sizeof(id)) > 0)
			err = atoi(id);
		close(fd);
	}

	free(filename);
	return err;
}

static int test__open_syscall_event(void)
{
	int err = -1, fd;
	struct thread_map *threads;
	struct perf_evsel *evsel;
	struct perf_event_attr attr;
	unsigned int nr_open_calls = 111, i;
	int id = trace_event__id("sys_enter_open");

	if (id < 0) {
		pr_debug("is debugfs mounted on /sys/kernel/debug?\n");
		return -1;
	}

	threads = thread_map__new(-1, getpid(), UINT_MAX);
	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_TRACEPOINT;
	attr.config = id;
	evsel = perf_evsel__new(&attr, 0);
	if (evsel == NULL) {
		pr_debug("perf_evsel__new\n");
		goto out_thread_map_delete;
	}

	if (perf_evsel__open_per_thread(evsel, threads, false, NULL) < 0) {
		pr_debug("failed to open counter: %s, "
			 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
			 strerror(errno));
		goto out_evsel_delete;
	}

	for (i = 0; i < nr_open_calls; ++i) {
		fd = open("/etc/passwd", O_RDONLY);
		close(fd);
	}

	if (perf_evsel__read_on_cpu(evsel, 0, 0) < 0) {
		pr_debug("perf_evsel__read_on_cpu\n");
		goto out_close_fd;
	}

	if (evsel->counts->cpu[0].val != nr_open_calls) {
		pr_debug("perf_evsel__read_on_cpu: expected to intercept %d calls, got %" PRIu64 "\n",
			 nr_open_calls, evsel->counts->cpu[0].val);
		goto out_close_fd;
	}
	
	err = 0;
out_close_fd:
	perf_evsel__close_fd(evsel, 1, threads->nr);
out_evsel_delete:
	perf_evsel__delete(evsel);
out_thread_map_delete:
	thread_map__delete(threads);
	return err;
}

#include <sched.h>

static int test__open_syscall_event_on_all_cpus(void)
{
	int err = -1, fd, cpu;
	struct thread_map *threads;
	struct cpu_map *cpus;
	struct perf_evsel *evsel;
	struct perf_event_attr attr;
	unsigned int nr_open_calls = 111, i;
	cpu_set_t cpu_set;
	int id = trace_event__id("sys_enter_open");

	if (id < 0) {
		pr_debug("is debugfs mounted on /sys/kernel/debug?\n");
		return -1;
	}

	threads = thread_map__new(-1, getpid(), UINT_MAX);
	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	cpus = cpu_map__new(NULL);
	if (cpus == NULL) {
		pr_debug("cpu_map__new\n");
		goto out_thread_map_delete;
	}


	CPU_ZERO(&cpu_set);

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_TRACEPOINT;
	attr.config = id;
	evsel = perf_evsel__new(&attr, 0);
	if (evsel == NULL) {
		pr_debug("perf_evsel__new\n");
		goto out_thread_map_delete;
	}

	if (perf_evsel__open(evsel, cpus, threads, false, NULL) < 0) {
		pr_debug("failed to open counter: %s, "
			 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
			 strerror(errno));
		goto out_evsel_delete;
	}

	for (cpu = 0; cpu < cpus->nr; ++cpu) {
		unsigned int ncalls = nr_open_calls + cpu;
		/*
		 * XXX eventually lift this restriction in a way that
		 * keeps perf building on older glibc installations
		 * without CPU_ALLOC. 1024 cpus in 2010 still seems
		 * a reasonable upper limit tho :-)
		 */
		if (cpus->map[cpu] >= CPU_SETSIZE) {
			pr_debug("Ignoring CPU %d\n", cpus->map[cpu]);
			continue;
		}

		CPU_SET(cpus->map[cpu], &cpu_set);
		if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0) {
			pr_debug("sched_setaffinity() failed on CPU %d: %s ",
				 cpus->map[cpu],
				 strerror(errno));
			goto out_close_fd;
		}
		for (i = 0; i < ncalls; ++i) {
			fd = open("/etc/passwd", O_RDONLY);
			close(fd);
		}
		CPU_CLR(cpus->map[cpu], &cpu_set);
	}

	/*
	 * Here we need to explicitely preallocate the counts, as if
	 * we use the auto allocation it will allocate just for 1 cpu,
	 * as we start by cpu 0.
	 */
	if (perf_evsel__alloc_counts(evsel, cpus->nr) < 0) {
		pr_debug("perf_evsel__alloc_counts(ncpus=%d)\n", cpus->nr);
		goto out_close_fd;
	}

	err = 0;

	for (cpu = 0; cpu < cpus->nr; ++cpu) {
		unsigned int expected;

		if (cpus->map[cpu] >= CPU_SETSIZE)
			continue;

		if (perf_evsel__read_on_cpu(evsel, cpu, 0) < 0) {
			pr_debug("perf_evsel__read_on_cpu\n");
			err = -1;
			break;
		}

		expected = nr_open_calls + cpu;
		if (evsel->counts->cpu[cpu].val != expected) {
			pr_debug("perf_evsel__read_on_cpu: expected to intercept %d calls on cpu %d, got %" PRIu64 "\n",
				 expected, cpus->map[cpu], evsel->counts->cpu[cpu].val);
			err = -1;
		}
	}

out_close_fd:
	perf_evsel__close_fd(evsel, 1, threads->nr);
out_evsel_delete:
	perf_evsel__delete(evsel);
out_thread_map_delete:
	thread_map__delete(threads);
	return err;
}

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
static int test__basic_mmap(void)
{
	int err = -1;
	union perf_event *event;
	struct thread_map *threads;
	struct cpu_map *cpus;
	struct perf_evlist *evlist;
	struct perf_event_attr attr = {
		.type		= PERF_TYPE_TRACEPOINT,
		.read_format	= PERF_FORMAT_ID,
		.sample_type	= PERF_SAMPLE_ID,
		.watermark	= 0,
	};
	cpu_set_t cpu_set;
	const char *syscall_names[] = { "getsid", "getppid", "getpgrp",
					"getpgid", };
	pid_t (*syscalls[])(void) = { (void *)getsid, getppid, getpgrp,
				      (void*)getpgid };
#define nsyscalls ARRAY_SIZE(syscall_names)
	int ids[nsyscalls];
	unsigned int nr_events[nsyscalls],
		     expected_nr_events[nsyscalls], i, j;
	struct perf_evsel *evsels[nsyscalls], *evsel;
	int sample_size = __perf_evsel__sample_size(attr.sample_type);

	for (i = 0; i < nsyscalls; ++i) {
		char name[64];

		snprintf(name, sizeof(name), "sys_enter_%s", syscall_names[i]);
		ids[i] = trace_event__id(name);
		if (ids[i] < 0) {
			pr_debug("Is debugfs mounted on /sys/kernel/debug?\n");
			return -1;
		}
		nr_events[i] = 0;
		expected_nr_events[i] = random() % 257;
	}

	threads = thread_map__new(-1, getpid(), UINT_MAX);
	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	cpus = cpu_map__new(NULL);
	if (cpus == NULL) {
		pr_debug("cpu_map__new\n");
		goto out_free_threads;
	}

	CPU_ZERO(&cpu_set);
	CPU_SET(cpus->map[0], &cpu_set);
	sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0) {
		pr_debug("sched_setaffinity() failed on CPU %d: %s ",
			 cpus->map[0], strerror(errno));
		goto out_free_cpus;
	}

	evlist = perf_evlist__new(cpus, threads);
	if (evlist == NULL) {
		pr_debug("perf_evlist__new\n");
		goto out_free_cpus;
	}

	/* anonymous union fields, can't be initialized above */
	attr.wakeup_events = 1;
	attr.sample_period = 1;

	for (i = 0; i < nsyscalls; ++i) {
		attr.config = ids[i];
		evsels[i] = perf_evsel__new(&attr, i);
		if (evsels[i] == NULL) {
			pr_debug("perf_evsel__new\n");
			goto out_free_evlist;
		}

		perf_evlist__add(evlist, evsels[i]);

		if (perf_evsel__open(evsels[i], cpus, threads, false, NULL) < 0) {
			pr_debug("failed to open counter: %s, "
				 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
				 strerror(errno));
			goto out_close_fd;
		}
	}

	if (perf_evlist__mmap(evlist, 128, true) < 0) {
		pr_debug("failed to mmap events: %d (%s)\n", errno,
			 strerror(errno));
		goto out_close_fd;
	}

	for (i = 0; i < nsyscalls; ++i)
		for (j = 0; j < expected_nr_events[i]; ++j) {
			int foo = syscalls[i]();
			++foo;
		}

	while ((event = perf_evlist__mmap_read(evlist, 0)) != NULL) {
		struct perf_sample sample;

		if (event->header.type != PERF_RECORD_SAMPLE) {
			pr_debug("unexpected %s event\n",
				 perf_event__name(event->header.type));
			goto out_munmap;
		}

		err = perf_event__parse_sample(event, attr.sample_type, sample_size,
					       false, &sample, false);
		if (err) {
			pr_err("Can't parse sample, err = %d\n", err);
			goto out_munmap;
		}

		evsel = perf_evlist__id2evsel(evlist, sample.id);
		if (evsel == NULL) {
			pr_debug("event with id %" PRIu64
				 " doesn't map to an evsel\n", sample.id);
			goto out_munmap;
		}
		nr_events[evsel->idx]++;
	}

	list_for_each_entry(evsel, &evlist->entries, node) {
		if (nr_events[evsel->idx] != expected_nr_events[evsel->idx]) {
			pr_debug("expected %d %s events, got %d\n",
				 expected_nr_events[evsel->idx],
				 perf_evsel__name(evsel), nr_events[evsel->idx]);
			goto out_munmap;
		}
	}

	err = 0;
out_munmap:
	perf_evlist__munmap(evlist);
out_close_fd:
	for (i = 0; i < nsyscalls; ++i)
		perf_evsel__close_fd(evsels[i], 1, threads->nr);
out_free_evlist:
	perf_evlist__delete(evlist);
out_free_cpus:
	cpu_map__delete(cpus);
out_free_threads:
	thread_map__delete(threads);
	return err;
#undef nsyscalls
}

static int sched__get_first_possible_cpu(pid_t pid, cpu_set_t **maskp,
					 size_t *sizep)
{
	cpu_set_t *mask;
	size_t size;
	int i, cpu = -1, nrcpus = 1024;
realloc:
	mask = CPU_ALLOC(nrcpus);
	size = CPU_ALLOC_SIZE(nrcpus);
	CPU_ZERO_S(size, mask);

	if (sched_getaffinity(pid, size, mask) == -1) {
		CPU_FREE(mask);
		if (errno == EINVAL && nrcpus < (1024 << 8)) {
			nrcpus = nrcpus << 2;
			goto realloc;
		}
		perror("sched_getaffinity");
			return -1;
	}

	for (i = 0; i < nrcpus; i++) {
		if (CPU_ISSET_S(i, size, mask)) {
			if (cpu == -1) {
				cpu = i;
				*maskp = mask;
				*sizep = size;
			} else
				CPU_CLR_S(i, size, mask);
		}
	}

	if (cpu == -1)
		CPU_FREE(mask);

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
	cpu_set_t *cpu_mask = NULL;
	size_t cpu_mask_size = 0;
	struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);
	struct perf_evsel *evsel;
	struct perf_sample sample;
	const char *cmd = "sleep";
	const char *argv[] = { cmd, "1", NULL, };
	char *bname;
	u64 sample_type, prev_time = 0;
	bool found_cmd_mmap = false,
	     found_libc_mmap = false,
	     found_vdso_mmap = false,
	     found_ld_mmap = false;
	int err = -1, errs = 0, i, wakeups = 0, sample_size;
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
	evsel = list_entry(evlist->entries.next, struct perf_evsel, node);
	evsel->attr.sample_type |= PERF_SAMPLE_CPU;
	evsel->attr.sample_type |= PERF_SAMPLE_TID;
	evsel->attr.sample_type |= PERF_SAMPLE_TIME;
	perf_evlist__config_attrs(evlist, &opts);

	err = sched__get_first_possible_cpu(evlist->workload.pid, &cpu_mask,
					    &cpu_mask_size);
	if (err < 0) {
		pr_debug("sched__get_first_possible_cpu: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	cpu = err;

	/*
	 * So that we can check perf_sample.cpu on all the samples.
	 */
	if (sched_setaffinity(evlist->workload.pid, cpu_mask_size, cpu_mask) < 0) {
		pr_debug("sched_setaffinity: %s\n", strerror(errno));
		goto out_free_cpu_mask;
	}

	/*
	 * Call sys_perf_event_open on all the fds on all the evsels,
	 * grouping them if asked to.
	 */
	err = perf_evlist__open(evlist, opts.group);
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
	 * We'll need these two to parse the PERF_SAMPLE_* fields in each
	 * event.
	 */
	sample_type = perf_evlist__sample_type(evlist);
	sample_size = __perf_evsel__sample_size(sample_type);

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

				err = perf_event__parse_sample(event, sample_type,
							       sample_size, true,
							       &sample, false);
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
out_free_cpu_mask:
	CPU_FREE(cpu_mask);
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
static void segfault_handler(int sig __used, siginfo_t *info __used, void *uc __used)
{
	exit(-1);
}

static int __test__rdpmc(void)
{
	long page_size = sysconf(_SC_PAGE_SIZE);
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
		die("Error: sys_perf_event_open() syscall returned "
		    "with %d (%s)\n", fd, strerror(errno));
	}

	addr = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == (void *)(-1)) {
		die("Error: mmap() syscall returned "
		    "with (%s)\n", strerror(errno));
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
	close(fd);

	pr_debug("   ");

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

	while (tests[i].func) {
		int curr = i++, err;

		if (!perf_test__matches(curr, argc, argv))
			continue;

		pr_info("%2d: %s:", i, tests[curr].desc);
		pr_debug("\n--- start ---\n");
		err = tests[curr].func();
		pr_debug("---- end ----\n%s:", tests[curr].desc);
		pr_info(" %s\n", err ? "FAILED!\n" : "Ok");
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

int cmd_test(int argc, const char **argv, const char *prefix __used)
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
