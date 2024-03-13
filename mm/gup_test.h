/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __GUP_TEST_H
#define __GUP_TEST_H

#include <linux/types.h>

#define GUP_FAST_BENCHMARK	_IOWR('g', 1, struct gup_test)
#define PIN_FAST_BENCHMARK	_IOWR('g', 2, struct gup_test)
#define PIN_LONGTERM_BENCHMARK	_IOWR('g', 3, struct gup_test)
#define GUP_BASIC_TEST		_IOWR('g', 4, struct gup_test)
#define PIN_BASIC_TEST		_IOWR('g', 5, struct gup_test)
#define DUMP_USER_PAGES_TEST	_IOWR('g', 6, struct gup_test)

#define GUP_TEST_MAX_PAGES_TO_DUMP		8

#define GUP_TEST_FLAG_DUMP_PAGES_USE_PIN	0x1

struct gup_test {
	__u64 get_delta_usec;
	__u64 put_delta_usec;
	__u64 addr;
	__u64 size;
	__u32 nr_pages_per_call;
	__u32 gup_flags;
	__u32 test_flags;
	/*
	 * Each non-zero entry is the number of the page (1-based: first page is
	 * page 1, so that zero entries mean "do nothing") from the .addr base.
	 */
	__u32 which_pages[GUP_TEST_MAX_PAGES_TO_DUMP];
};

#endif	/* __GUP_TEST_H */
