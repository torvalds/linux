// SPDX-License-Identifier: GPL-2.0
/*
 * Self-check for the vm_util folio-order helpers, is_backed_by_folio() and
 * is_range_backed_by_order(), which the khugepaged mTHP cases use to detect
 * collapse results.  For every anon THP order the kernel supports, fault
 * memory in with only that order enabled and require the helpers to report
 * exactly that order.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kselftest.h"
#include "vm_util.h"
#include <mm/hugepage_settings.h>

static int pagemap_fd;
static int kpageflags_fd;

static char *alloc_aligned(size_t size)
{
	size_t len = size * 2;
	char *p, *aligned;

	p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED)
		ksft_exit_fail_perror("mmap()");

	aligned = (char *)ALIGN((uintptr_t)p, size);
	if (aligned != p)
		munmap(p, aligned - p);
	if (aligned + size != p + len)
		munmap(aligned + size, p + len - aligned - size);

	return aligned;
}

static void check_order(int order)
{
	struct thp_settings settings = *thp_current_settings();
	size_t size = psize() << order;
	bool ok = true;
	char *p;
	int i;

	for (i = 0; i < NR_ORDERS; i++)
		settings.hugepages[i].enabled = THP_NEVER;
	if (order)
		settings.hugepages[order].enabled = THP_ALWAYS;
	thp_push_settings(&settings);

	p = alloc_aligned(size);
	*p = 1;

	if (!is_range_backed_by_order(p, size, order, pagemap_fd, kpageflags_fd)) {
		ksft_print_msg("order %d not detected after fault\n", order);
		ok = false;
	}

	/* A lower order must be rejected: the folio is larger */
	if (order && is_range_backed_by_order(p, size, order - 1,
					      pagemap_fd, kpageflags_fd)) {
		ksft_print_msg("order %d also reported as order %d\n",
			       order, order - 1);
		ok = false;
	}

	/* A large folio must not pass as order 0 */
	if (order && is_range_backed_by_order(p, size, 0,
					      pagemap_fd, kpageflags_fd)) {
		ksft_print_msg("order %d also reported as order 0\n", order);
		ok = false;
	}

	munmap(p, size);
	thp_pop_settings();

	ksft_test_result(ok, "order %d classified\n", order);
}

int main(void)
{
	struct thp_settings settings;
	unsigned long orders;
	int order;

	ksft_print_header();

	if (!thp_available())
		ksft_exit_skip("Transparent Hugepages not available\n");

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_perror("open(/proc/self/pagemap)");
	kpageflags_fd = open("/proc/kpageflags", O_RDONLY);
	if (kpageflags_fd < 0)
		ksft_exit_skip("open(/proc/kpageflags) requires root\n");

	orders = thp_supported_orders();
	if (!orders)
		ksft_exit_skip("No supported THP orders\n");

	ksft_set_plan(__builtin_popcountl(orders) + 1);

	thp_save_settings();
	thp_read_settings(&settings);
	/* Base of the settings stack; the bottom entry is never popped */
	thp_push_settings(&settings);

	check_order(0);
	for (order = 1; order < NR_ORDERS; order++) {
		if (!(orders & (1UL << order)))
			continue;
		check_order(order);
	}

	ksft_finished();
}
