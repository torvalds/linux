#include "perf.h"
#include "util/debug.h"
#include "util/symbol.h"
#include "util/sort.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/thread.h"
#include "util/parse-events.h"
#include "tests/tests.h"
#include "tests/hists_common.h"

struct sample {
	u32 pid;
	u64 ip;
	struct thread *thread;
	struct map *map;
	struct symbol *sym;
};

/* For the numbers, see hists_common.c */
static struct sample fake_samples[] = {
	/* perf [kernel] schedule() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_KERNEL_SCHEDULE, },
	/* perf [perf]   main() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_PERF_MAIN, },
	/* perf [perf]   cmd_record() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_PERF_CMD_RECORD, },
	/* perf [libc]   malloc() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_LIBC_MALLOC, },
	/* perf [libc]   free() */
	{ .pid = FAKE_PID_PERF1, .ip = FAKE_IP_LIBC_FREE, },
	/* perf [perf]   main() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_PERF_MAIN, },
	/* perf [kernel] page_fault() */
	{ .pid = FAKE_PID_PERF2, .ip = FAKE_IP_KERNEL_PAGE_FAULT, },
	/* bash [bash]   main() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_BASH_MAIN, },
	/* bash [bash]   xmalloc() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_BASH_XMALLOC, },
	/* bash [kernel] page_fault() */
	{ .pid = FAKE_PID_BASH,  .ip = FAKE_IP_KERNEL_PAGE_FAULT, },
};

/*
 * Will be casted to struct ip_callchain which has all 64 bit entries
 * of nr and ips[].
 */
static u64 fake_callchains[][10] = {
	/*   schedule => run_command => main */
	{ 3, FAKE_IP_KERNEL_SCHEDULE, FAKE_IP_PERF_RUN_COMMAND, FAKE_IP_PERF_MAIN, },
	/*   main  */
	{ 1, FAKE_IP_PERF_MAIN, },
	/*   cmd_record => run_command => main */
	{ 3, FAKE_IP_PERF_CMD_RECORD, FAKE_IP_PERF_RUN_COMMAND, FAKE_IP_PERF_MAIN, },
	/*   malloc => cmd_record => run_command => main */
	{ 4, FAKE_IP_LIBC_MALLOC, FAKE_IP_PERF_CMD_RECORD, FAKE_IP_PERF_RUN_COMMAND,
	     FAKE_IP_PERF_MAIN, },
	/*   free => cmd_record => run_command => main */
	{ 4, FAKE_IP_LIBC_FREE, FAKE_IP_PERF_CMD_RECORD, FAKE_IP_PERF_RUN_COMMAND,
	     FAKE_IP_PERF_MAIN, },
	/*   main */
	{ 1, FAKE_IP_PERF_MAIN, },
	/*   page_fault => sys_perf_event_open => run_command => main */
	{ 4, FAKE_IP_KERNEL_PAGE_FAULT, FAKE_IP_KERNEL_SYS_PERF_EVENT_OPEN,
	     FAKE_IP_PERF_RUN_COMMAND, FAKE_IP_PERF_MAIN, },
	/*   main */
	{ 1, FAKE_IP_BASH_MAIN, },
	/*   xmalloc => malloc => xmalloc => malloc => xmalloc => main */
	{ 6, FAKE_IP_BASH_XMALLOC, FAKE_IP_LIBC_MALLOC, FAKE_IP_BASH_XMALLOC,
	     FAKE_IP_LIBC_MALLOC, FAKE_IP_BASH_XMALLOC, FAKE_IP_BASH_MAIN, },
	/*   page_fault => malloc => main */
	{ 3, FAKE_IP_KERNEL_PAGE_FAULT, FAKE_IP_LIBC_MALLOC, FAKE_IP_BASH_MAIN, },
};

static int add_hist_entries(struct hists *hists, struct machine *machine)
{
	struct addr_location al;
	struct perf_evsel *evsel = hists_to_evsel(hists);
	struct perf_sample sample = { .period = 1000, };
	size_t i;

	for (i = 0; i < ARRAY_SIZE(fake_samples); i++) {
		const union perf_event event = {
			.header = {
				.misc = PERF_RECORD_MISC_USER,
			},
		};
		struct hist_entry_iter iter = {
			.evsel = evsel,
			.sample	= &sample,
			.hide_unresolved = false,
		};

		if (symbol_conf.cumulate_callchain)
			iter.ops = &hist_iter_cumulative;
		else
			iter.ops = &hist_iter_normal;

		sample.pid = fake_samples[i].pid;
		sample.tid = fake_samples[i].pid;
		sample.ip = fake_samples[i].ip;
		sample.callchain = (struct ip_callchain *)fake_callchains[i];

		if (perf_event__preprocess_sample(&event, machine, &al,
						  &sample) < 0)
			goto out;

		if (hist_entry_iter__add(&iter, &al, PERF_MAX_STACK_DEPTH,
					 NULL) < 0) {
			addr_location__put(&al);
			goto out;
		}

		fake_samples[i].thread = al.thread;
		fake_samples[i].map = al.map;
		fake_samples[i].sym = al.sym;
	}

	return TEST_OK;

out:
	pr_debug("Not enough memory for adding a hist entry\n");
	return TEST_FAIL;
}

static void del_hist_entries(struct hists *hists)
{
	struct hist_entry *he;
	struct rb_root *root_in;
	struct rb_root *root_out;
	struct rb_node *node;

	if (sort__need_collapse)
		root_in = &hists->entries_collapsed;
	else
		root_in = hists->entries_in;

	root_out = &hists->entries;

	while (!RB_EMPTY_ROOT(root_out)) {
		node = rb_first(root_out);

		he = rb_entry(node, struct hist_entry, rb_node);
		rb_erase(node, root_out);
		rb_erase(&he->rb_node_in, root_in);
		hist_entry__delete(he);
	}
}

typedef int (*test_fn_t)(struct perf_evsel *, struct machine *);

#define COMM(he)  (thread__comm_str(he->thread))
#define DSO(he)   (he->ms.map->dso->short_name)
#define SYM(he)   (he->ms.sym->name)
#define CPU(he)   (he->cpu)
#define PID(he)   (he->thread->tid)
#define DEPTH(he) (he->callchain->max_depth)
#define CDSO(cl)  (cl->ms.map->dso->short_name)
#define CSYM(cl)  (cl->ms.sym->name)

struct result {
	u64 children;
	u64 self;
	const char *comm;
	const char *dso;
	const char *sym;
};

struct callchain_result {
	u64 nr;
	struct {
		const char *dso;
		const char *sym;
	} node[10];
};

static int do_test(struct hists *hists, struct result *expected, size_t nr_expected,
		   struct callchain_result *expected_callchain, size_t nr_callchain)
{
	char buf[32];
	size_t i, c;
	struct hist_entry *he;
	struct rb_root *root;
	struct rb_node *node;
	struct callchain_node *cnode;
	struct callchain_list *clist;

	/*
	 * adding and deleting hist entries must be done outside of this
	 * function since TEST_ASSERT_VAL() returns in case of failure.
	 */
	hists__collapse_resort(hists, NULL);
	hists__output_resort(hists, NULL);

	if (verbose > 2) {
		pr_info("use callchain: %d, cumulate callchain: %d\n",
			symbol_conf.use_callchain,
			symbol_conf.cumulate_callchain);
		print_hists_out(hists);
	}

	root = &hists->entries;
	for (node = rb_first(root), i = 0;
	     node && (he = rb_entry(node, struct hist_entry, rb_node));
	     node = rb_next(node), i++) {
		scnprintf(buf, sizeof(buf), "Invalid hist entry #%zd", i);

		TEST_ASSERT_VAL("Incorrect number of hist entry",
				i < nr_expected);
		TEST_ASSERT_VAL(buf, he->stat.period == expected[i].self &&
				!strcmp(COMM(he), expected[i].comm) &&
				!strcmp(DSO(he), expected[i].dso) &&
				!strcmp(SYM(he), expected[i].sym));

		if (symbol_conf.cumulate_callchain)
			TEST_ASSERT_VAL(buf, he->stat_acc->period == expected[i].children);

		if (!symbol_conf.use_callchain)
			continue;

		/* check callchain entries */
		root = &he->callchain->node.rb_root;
		cnode = rb_entry(rb_first(root), struct callchain_node, rb_node);

		c = 0;
		list_for_each_entry(clist, &cnode->val, list) {
			scnprintf(buf, sizeof(buf), "Invalid callchain entry #%zd/%zd", i, c);

			TEST_ASSERT_VAL("Incorrect number of callchain entry",
					c < expected_callchain[i].nr);
			TEST_ASSERT_VAL(buf,
				!strcmp(CDSO(clist), expected_callchain[i].node[c].dso) &&
				!strcmp(CSYM(clist), expected_callchain[i].node[c].sym));
			c++;
		}
		/* TODO: handle multiple child nodes properly */
		TEST_ASSERT_VAL("Incorrect number of callchain entry",
				c <= expected_callchain[i].nr);
	}
	TEST_ASSERT_VAL("Incorrect number of hist entry",
			i == nr_expected);
	TEST_ASSERT_VAL("Incorrect number of callchain entry",
			!symbol_conf.use_callchain || nr_expected == nr_callchain);
	return 0;
}

/* NO callchain + NO children */
static int test1(struct perf_evsel *evsel, struct machine *machine)
{
	int err;
	struct hists *hists = evsel__hists(evsel);
	/*
	 * expected output:
	 *
	 * Overhead  Command  Shared Object          Symbol
	 * ========  =======  =============  ==============
	 *   20.00%     perf  perf           [.] main
	 *   10.00%     bash  [kernel]       [k] page_fault
	 *   10.00%     bash  bash           [.] main
	 *   10.00%     bash  bash           [.] xmalloc
	 *   10.00%     perf  [kernel]       [k] page_fault
	 *   10.00%     perf  [kernel]       [k] schedule
	 *   10.00%     perf  libc           [.] free
	 *   10.00%     perf  libc           [.] malloc
	 *   10.00%     perf  perf           [.] cmd_record
	 */
	struct result expected[] = {
		{ 0, 2000, "perf", "perf",     "main" },
		{ 0, 1000, "bash", "[kernel]", "page_fault" },
		{ 0, 1000, "bash", "bash",     "main" },
		{ 0, 1000, "bash", "bash",     "xmalloc" },
		{ 0, 1000, "perf", "[kernel]", "page_fault" },
		{ 0, 1000, "perf", "[kernel]", "schedule" },
		{ 0, 1000, "perf", "libc",     "free" },
		{ 0, 1000, "perf", "libc",     "malloc" },
		{ 0, 1000, "perf", "perf",     "cmd_record" },
	};

	symbol_conf.use_callchain = false;
	symbol_conf.cumulate_callchain = false;
	perf_evsel__reset_sample_bit(evsel, CALLCHAIN);

	setup_sorting();
	callchain_register_param(&callchain_param);

	err = add_hist_entries(hists, machine);
	if (err < 0)
		goto out;

	err = do_test(hists, expected, ARRAY_SIZE(expected), NULL, 0);

out:
	del_hist_entries(hists);
	reset_output_field();
	return err;
}

/* callcain + NO children */
static int test2(struct perf_evsel *evsel, struct machine *machine)
{
	int err;
	struct hists *hists = evsel__hists(evsel);
	/*
	 * expected output:
	 *
	 * Overhead  Command  Shared Object          Symbol
	 * ========  =======  =============  ==============
	 *   20.00%     perf  perf           [.] main
	 *              |
	 *              --- main
	 *
	 *   10.00%     bash  [kernel]       [k] page_fault
	 *              |
	 *              --- page_fault
	 *                  malloc
	 *                  main
	 *
	 *   10.00%     bash  bash           [.] main
	 *              |
	 *              --- main
	 *
	 *   10.00%     bash  bash           [.] xmalloc
	 *              |
	 *              --- xmalloc
	 *                  malloc
	 *                  xmalloc     <--- NOTE: there's a cycle
	 *                  malloc
	 *                  xmalloc
	 *                  main
	 *
	 *   10.00%     perf  [kernel]       [k] page_fault
	 *              |
	 *              --- page_fault
	 *                  sys_perf_event_open
	 *                  run_command
	 *                  main
	 *
	 *   10.00%     perf  [kernel]       [k] schedule
	 *              |
	 *              --- schedule
	 *                  run_command
	 *                  main
	 *
	 *   10.00%     perf  libc           [.] free
	 *              |
	 *              --- free
	 *                  cmd_record
	 *                  run_command
	 *                  main
	 *
	 *   10.00%     perf  libc           [.] malloc
	 *              |
	 *              --- malloc
	 *                  cmd_record
	 *                  run_command
	 *                  main
	 *
	 *   10.00%     perf  perf           [.] cmd_record
	 *              |
	 *              --- cmd_record
	 *                  run_command
	 *                  main
	 *
	 */
	struct result expected[] = {
		{ 0, 2000, "perf", "perf",     "main" },
		{ 0, 1000, "bash", "[kernel]", "page_fault" },
		{ 0, 1000, "bash", "bash",     "main" },
		{ 0, 1000, "bash", "bash",     "xmalloc" },
		{ 0, 1000, "perf", "[kernel]", "page_fault" },
		{ 0, 1000, "perf", "[kernel]", "schedule" },
		{ 0, 1000, "perf", "libc",     "free" },
		{ 0, 1000, "perf", "libc",     "malloc" },
		{ 0, 1000, "perf", "perf",     "cmd_record" },
	};
	struct callchain_result expected_callchain[] = {
		{
			1, {	{ "perf",     "main" }, },
		},
		{
			3, {	{ "[kernel]", "page_fault" },
				{ "libc",     "malloc" },
				{ "bash",     "main" }, },
		},
		{
			1, {	{ "bash",     "main" }, },
		},
		{
			6, {	{ "bash",     "xmalloc" },
				{ "libc",     "malloc" },
				{ "bash",     "xmalloc" },
				{ "libc",     "malloc" },
				{ "bash",     "xmalloc" },
				{ "bash",     "main" }, },
		},
		{
			4, {	{ "[kernel]", "page_fault" },
				{ "[kernel]", "sys_perf_event_open" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			3, {	{ "[kernel]", "schedule" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			4, {	{ "libc",     "free" },
				{ "perf",     "cmd_record" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			4, {	{ "libc",     "malloc" },
				{ "perf",     "cmd_record" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			3, {	{ "perf",     "cmd_record" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
	};

	symbol_conf.use_callchain = true;
	symbol_conf.cumulate_callchain = false;
	perf_evsel__set_sample_bit(evsel, CALLCHAIN);

	setup_sorting();
	callchain_register_param(&callchain_param);

	err = add_hist_entries(hists, machine);
	if (err < 0)
		goto out;

	err = do_test(hists, expected, ARRAY_SIZE(expected),
		      expected_callchain, ARRAY_SIZE(expected_callchain));

out:
	del_hist_entries(hists);
	reset_output_field();
	return err;
}

/* NO callchain + children */
static int test3(struct perf_evsel *evsel, struct machine *machine)
{
	int err;
	struct hists *hists = evsel__hists(evsel);
	/*
	 * expected output:
	 *
	 * Children      Self  Command  Shared Object                   Symbol
	 * ========  ========  =======  =============  =======================
	 *   70.00%    20.00%     perf  perf           [.] main
	 *   50.00%     0.00%     perf  perf           [.] run_command
	 *   30.00%    10.00%     bash  bash           [.] main
	 *   30.00%    10.00%     perf  perf           [.] cmd_record
	 *   20.00%     0.00%     bash  libc           [.] malloc
	 *   10.00%    10.00%     bash  [kernel]       [k] page_fault
	 *   10.00%    10.00%     bash  bash           [.] xmalloc
	 *   10.00%    10.00%     perf  [kernel]       [k] page_fault
	 *   10.00%    10.00%     perf  libc           [.] malloc
	 *   10.00%    10.00%     perf  [kernel]       [k] schedule
	 *   10.00%    10.00%     perf  libc           [.] free
	 *   10.00%     0.00%     perf  [kernel]       [k] sys_perf_event_open
	 */
	struct result expected[] = {
		{ 7000, 2000, "perf", "perf",     "main" },
		{ 5000,    0, "perf", "perf",     "run_command" },
		{ 3000, 1000, "bash", "bash",     "main" },
		{ 3000, 1000, "perf", "perf",     "cmd_record" },
		{ 2000,    0, "bash", "libc",     "malloc" },
		{ 1000, 1000, "bash", "[kernel]", "page_fault" },
		{ 1000, 1000, "bash", "bash",     "xmalloc" },
		{ 1000, 1000, "perf", "[kernel]", "page_fault" },
		{ 1000, 1000, "perf", "[kernel]", "schedule" },
		{ 1000, 1000, "perf", "libc",     "free" },
		{ 1000, 1000, "perf", "libc",     "malloc" },
		{ 1000,    0, "perf", "[kernel]", "sys_perf_event_open" },
	};

	symbol_conf.use_callchain = false;
	symbol_conf.cumulate_callchain = true;
	perf_evsel__reset_sample_bit(evsel, CALLCHAIN);

	setup_sorting();
	callchain_register_param(&callchain_param);

	err = add_hist_entries(hists, machine);
	if (err < 0)
		goto out;

	err = do_test(hists, expected, ARRAY_SIZE(expected), NULL, 0);

out:
	del_hist_entries(hists);
	reset_output_field();
	return err;
}

/* callchain + children */
static int test4(struct perf_evsel *evsel, struct machine *machine)
{
	int err;
	struct hists *hists = evsel__hists(evsel);
	/*
	 * expected output:
	 *
	 * Children      Self  Command  Shared Object                   Symbol
	 * ========  ========  =======  =============  =======================
	 *   70.00%    20.00%     perf  perf           [.] main
	 *              |
	 *              --- main
	 *
	 *   50.00%     0.00%     perf  perf           [.] run_command
	 *              |
	 *              --- run_command
	 *                  main
	 *
	 *   30.00%    10.00%     bash  bash           [.] main
	 *              |
	 *              --- main
	 *
	 *   30.00%    10.00%     perf  perf           [.] cmd_record
	 *              |
	 *              --- cmd_record
	 *                  run_command
	 *                  main
	 *
	 *   20.00%     0.00%     bash  libc           [.] malloc
	 *              |
	 *              --- malloc
	 *                 |
	 *                 |--50.00%-- xmalloc
	 *                 |           main
	 *                  --50.00%-- main
	 *
	 *   10.00%    10.00%     bash  [kernel]       [k] page_fault
	 *              |
	 *              --- page_fault
	 *                  malloc
	 *                  main
	 *
	 *   10.00%    10.00%     bash  bash           [.] xmalloc
	 *              |
	 *              --- xmalloc
	 *                  malloc
	 *                  xmalloc     <--- NOTE: there's a cycle
	 *                  malloc
	 *                  xmalloc
	 *                  main
	 *
	 *   10.00%     0.00%     perf  [kernel]       [k] sys_perf_event_open
	 *              |
	 *              --- sys_perf_event_open
	 *                  run_command
	 *                  main
	 *
	 *   10.00%    10.00%     perf  [kernel]       [k] page_fault
	 *              |
	 *              --- page_fault
	 *                  sys_perf_event_open
	 *                  run_command
	 *                  main
	 *
	 *   10.00%    10.00%     perf  [kernel]       [k] schedule
	 *              |
	 *              --- schedule
	 *                  run_command
	 *                  main
	 *
	 *   10.00%    10.00%     perf  libc           [.] free
	 *              |
	 *              --- free
	 *                  cmd_record
	 *                  run_command
	 *                  main
	 *
	 *   10.00%    10.00%     perf  libc           [.] malloc
	 *              |
	 *              --- malloc
	 *                  cmd_record
	 *                  run_command
	 *                  main
	 *
	 */
	struct result expected[] = {
		{ 7000, 2000, "perf", "perf",     "main" },
		{ 5000,    0, "perf", "perf",     "run_command" },
		{ 3000, 1000, "bash", "bash",     "main" },
		{ 3000, 1000, "perf", "perf",     "cmd_record" },
		{ 2000,    0, "bash", "libc",     "malloc" },
		{ 1000, 1000, "bash", "[kernel]", "page_fault" },
		{ 1000, 1000, "bash", "bash",     "xmalloc" },
		{ 1000,    0, "perf", "[kernel]", "sys_perf_event_open" },
		{ 1000, 1000, "perf", "[kernel]", "page_fault" },
		{ 1000, 1000, "perf", "[kernel]", "schedule" },
		{ 1000, 1000, "perf", "libc",     "free" },
		{ 1000, 1000, "perf", "libc",     "malloc" },
	};
	struct callchain_result expected_callchain[] = {
		{
			1, {	{ "perf",     "main" }, },
		},
		{
			2, {	{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			1, {	{ "bash",     "main" }, },
		},
		{
			3, {	{ "perf",     "cmd_record" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			4, {	{ "libc",     "malloc" },
				{ "bash",     "xmalloc" },
				{ "bash",     "main" },
				{ "bash",     "main" }, },
		},
		{
			3, {	{ "[kernel]", "page_fault" },
				{ "libc",     "malloc" },
				{ "bash",     "main" }, },
		},
		{
			6, {	{ "bash",     "xmalloc" },
				{ "libc",     "malloc" },
				{ "bash",     "xmalloc" },
				{ "libc",     "malloc" },
				{ "bash",     "xmalloc" },
				{ "bash",     "main" }, },
		},
		{
			3, {	{ "[kernel]", "sys_perf_event_open" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			4, {	{ "[kernel]", "page_fault" },
				{ "[kernel]", "sys_perf_event_open" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			3, {	{ "[kernel]", "schedule" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			4, {	{ "libc",     "free" },
				{ "perf",     "cmd_record" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
		{
			4, {	{ "libc",     "malloc" },
				{ "perf",     "cmd_record" },
				{ "perf",     "run_command" },
				{ "perf",     "main" }, },
		},
	};

	symbol_conf.use_callchain = true;
	symbol_conf.cumulate_callchain = true;
	perf_evsel__set_sample_bit(evsel, CALLCHAIN);

	setup_sorting();
	callchain_register_param(&callchain_param);

	err = add_hist_entries(hists, machine);
	if (err < 0)
		goto out;

	err = do_test(hists, expected, ARRAY_SIZE(expected),
		      expected_callchain, ARRAY_SIZE(expected_callchain));

out:
	del_hist_entries(hists);
	reset_output_field();
	return err;
}

int test__hists_cumulate(void)
{
	int err = TEST_FAIL;
	struct machines machines;
	struct machine *machine;
	struct perf_evsel *evsel;
	struct perf_evlist *evlist = perf_evlist__new();
	size_t i;
	test_fn_t testcases[] = {
		test1,
		test2,
		test3,
		test4,
	};

	TEST_ASSERT_VAL("No memory", evlist);

	err = parse_events(evlist, "cpu-clock", NULL);
	if (err)
		goto out;

	machines__init(&machines);

	/* setup threads/dso/map/symbols also */
	machine = setup_fake_machine(&machines);
	if (!machine)
		goto out;

	if (verbose > 1)
		machine__fprintf(machine, stderr);

	evsel = perf_evlist__first(evlist);

	for (i = 0; i < ARRAY_SIZE(testcases); i++) {
		err = testcases[i](evsel, machine);
		if (err < 0)
			break;
	}

out:
	/* tear down everything */
	perf_evlist__delete(evlist);
	machines__exit(&machines);

	return err;
}
