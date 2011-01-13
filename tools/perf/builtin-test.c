/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include "builtin.h"

#include "util/cache.h"
#include "util/debug.h"
#include "util/parse-options.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/thread.h"

static long page_size;

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

				pr_debug("%#Lx: diff end addr for %s v: %#Lx k: %#Lx\n",
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

				pr_debug("%#Lx: diff name v: %s k: %s\n",
					 sym->start, sym->name, pair->name);
			}
		} else
			pr_debug("%#Lx: %s not on kallsyms\n", sym->start, sym->name);

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
			pr_info(" %Lx-%Lx %Lx %s in kallsyms as",
				pos->start, pos->end, pos->pgoff, pos->dso->name);
			if (pos->pgoff != pair->pgoff || pos->end != pair->end)
				pr_info(": \n*%Lx-%Lx %Lx",
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

#include "util/evsel.h"
#include <sys/types.h>

static int trace_event__id(const char *event_name)
{
	char *filename;
	int err = -1, fd;

	if (asprintf(&filename,
		     "/sys/kernel/debug/tracing/events/syscalls/%s/id",
		     event_name) < 0)
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
	unsigned int nr_open_calls = 111, i;
	int id = trace_event__id("sys_enter_open");

	if (id < 0) {
		pr_debug("is debugfs mounted on /sys/kernel/debug?\n");
		return -1;
	}

	threads = thread_map__new(-1, getpid());
	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	evsel = perf_evsel__new(PERF_TYPE_TRACEPOINT, id, 0);
	if (evsel == NULL) {
		pr_debug("perf_evsel__new\n");
		goto out_thread_map_delete;
	}

	if (perf_evsel__open_per_thread(evsel, threads) < 0) {
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
		pr_debug("perf_evsel__open_read_on_cpu\n");
		goto out_close_fd;
	}

	if (evsel->counts->cpu[0].val != nr_open_calls) {
		pr_debug("perf_evsel__read_on_cpu: expected to intercept %d calls, got %Ld\n",
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
		.func = NULL,
	},
};

static int __cmd_test(void)
{
	int i = 0;

	page_size = sysconf(_SC_PAGE_SIZE);

	while (tests[i].func) {
		int err;
		pr_info("%2d: %s:", i + 1, tests[i].desc);
		pr_debug("\n--- start ---\n");
		err = tests[i].func();
		pr_debug("---- end ----\n%s:", tests[i].desc);
		pr_info(" %s\n", err ? "FAILED!\n" : "Ok");
		++i;
	}

	return 0;
}

static const char * const test_usage[] = {
	"perf test [<options>]",
	NULL,
};

static const struct option test_options[] = {
	OPT_INTEGER('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_END()
};

int cmd_test(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, test_options, test_usage, 0);
	if (argc)
		usage_with_options(test_usage, test_options);

	symbol_conf.priv_size = sizeof(int);
	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init() < 0)
		return -1;

	setup_pager();

	return __cmd_test();
}
