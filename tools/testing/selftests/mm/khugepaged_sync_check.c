// SPDX-License-Identifier: GPL-2.0
/*
 * Check that khugepaged_full_pass() drives khugepaged in step: one barrier
 * over one prepared window must collapse it with exactly one collapse
 * attempt attributed to its source pages, step after step.
 *
 * scan_sleep_millisecs is a minute so that a step which slept instead of
 * being woken blows the budget.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kselftest.h"
#include "vm_util.h"
#include <mm/hugepage_settings.h>

#define BASE_ADDR ((void *)(1UL << 30))
/* Smallest order khugepaged considers */
#define TARGET_ORDER 2
#define NR_ITERATIONS 5
#define PASS_TIMEOUT_S 30

static int pagemap_fd;
static int kpageflags_fd;
static int trace_events_fd = -1;
static unsigned long hpage_pmd_size;

/*
 * The events are system-wide: switch them off however the test ends,
 * including from inside a helper that gives up.
 */
static void trace_events_off(void)
{
	if (trace_events_fd >= 0)
		tracing_events_enable(trace_events_fd, false);
}

/* Count the isolate events whose scan_pfn is one of the window's source PFNs */
static int count_attributed(unsigned long *pfns, int nr_pfns,
			    unsigned int order)
{
	char line[1024];
	int count = 0;
	FILE *fp;

	fp = tracing_open_trace();
	if (!fp)
		ksft_exit_fail_msg("Cannot open trace buffer\n");

	while (fgets(line, sizeof(line), fp)) {
		unsigned long val;
		unsigned int ord;
		char *s, *o;
		int i;

		s = strstr(line, "mm_collapse_huge_page_isolate:");
		if (!s)
			continue;
		if (sscanf(s, "mm_collapse_huge_page_isolate: scan_pfn=0x%lx",
			   &val) != 1)
			continue;
		o = strstr(s, "order=");
		if (!o || sscanf(o, "order=%u", &ord) != 1 || ord != order)
			continue;
		for (i = 0; i < nr_pfns; i++) {
			if (val == pfns[i]) {
				count++;
				break;
			}
		}
	}
	fclose(fp);
	return count;
}

static void one_step(int iteration)
{
	const size_t window = getpagesize() << TARGET_ORDER;
	const int nr_pages = 1 << TARGET_ORDER;
	unsigned long pfns[1 << TARGET_ORDER];
	bool collapsed, passed;
	int attributed;
	char *p;
	int i;

	p = mmap(BASE_ADDR, hpage_pmd_size, PROT_READ | PROT_WRITE,
		 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
	if (p != BASE_ADDR)
		ksft_exit_fail_perror("mmap() window");

	for (i = 0; i < nr_pages; i++) {
		p[i * getpagesize()] = i + 1;
		pfns[i] = pagemap_get_pfn(pagemap_fd, p + i * getpagesize());
		if (pfns[i] == -1UL)
			ksft_exit_fail_msg("Source page not present\n");
	}

	/* Clear before enabling so the buffer holds only this step's events */
	if (tracing_clear_trace())
		ksft_exit_fail_msg("Cannot clear the trace buffer\n");
	if (tracing_events_enable(trace_events_fd, true))
		ksft_exit_fail_msg("Cannot enable huge_memory events\n");

	if (madvise(p, hpage_pmd_size, MADV_HUGEPAGE))
		ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");
	passed = khugepaged_full_pass(PASS_TIMEOUT_S);

	/* Off before anything that can give up: the events are system-wide */
	if (tracing_events_enable(trace_events_fd, false))
		ksft_exit_fail_msg("Cannot disable huge_memory events\n");
	if (!passed)
		ksft_exit_fail_msg("khugepaged did not complete a full pass\n");

	collapsed = is_range_backed_by_order(p, window, TARGET_ORDER,
					     pagemap_fd, kpageflags_fd);
	attributed = count_attributed(pfns, nr_pages, TARGET_ORDER);

	ksft_test_result(collapsed && attributed == 1,
			 "step %d: window collapsed, %d attributed result(s)\n",
			 iteration, attributed);

	munmap(p, hpage_pmd_size);
}

int main(void)
{
	struct thp_settings settings;
	int i;

	ksft_print_header();

	if (!thp_available())
		ksft_exit_skip("Transparent Hugepages not available\n");
	if (!(thp_supported_orders() & (1UL << TARGET_ORDER)))
		ksft_exit_skip("Order %d is not a supported anon THP order\n",
			       TARGET_ORDER);

	hpage_pmd_size = read_pmd_pagesize();
	if (!hpage_pmd_size)
		ksft_exit_fail_msg("Reading PMD pagesize failed\n");
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_perror("open(/proc/self/pagemap)");
	kpageflags_fd = open("/proc/kpageflags", O_RDONLY);
	if (kpageflags_fd < 0)
		ksft_exit_skip("open(/proc/kpageflags) requires root\n");
	trace_events_fd = tracing_events_open("huge_memory");
	if (trace_events_fd < 0)
		ksft_exit_skip("huge_memory events require tracefs and root\n");
	atexit(trace_events_off);

	ksft_set_plan(NR_ITERATIONS);

	thp_save_settings();
	thp_read_settings(&settings);
	settings.thp_enabled = THP_MADVISE;
	settings.thp_defrag = THP_DEFRAG_ALWAYS;
	settings.khugepaged.defrag = 1;
	settings.khugepaged.scan_sleep_millisecs = 60 * 1000;
	settings.khugepaged.alloc_sleep_millisecs = 60 * 1000;
	settings.khugepaged.max_ptes_none = (hpage_pmd_size / getpagesize()) - 1;
	/* One wake must complete one full pass; see khugepaged_full_pass() */
	settings.khugepaged.pages_to_scan = 1UL << 24;
	for (i = 0; i < NR_ORDERS; i++)
		settings.hugepages[i].enabled = THP_NEVER;
	settings.hugepages[TARGET_ORDER].enabled = THP_INHERIT;
	/* Base of the settings stack; the bottom entry is never popped */
	thp_push_settings(&settings);

	for (i = 0; i < NR_ITERATIONS; i++)
		one_step(i);

	ksft_finished();
}
