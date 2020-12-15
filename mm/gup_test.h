/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __GUP_TEST_H
#define __GUP_TEST_H

#include <linux/types.h>

#define GUP_FAST_BENCHMARK	_IOWR('g', 1, struct gup_test)
#define GUP_BENCHMARK		_IOWR('g', 2, struct gup_test)
#define PIN_FAST_BENCHMARK	_IOWR('g', 3, struct gup_test)
#define PIN_BENCHMARK		_IOWR('g', 4, struct gup_test)
#define PIN_LONGTERM_BENCHMARK	_IOWR('g', 5, struct gup_test)

struct gup_test {
	__u64 get_delta_usec;
	__u64 put_delta_usec;
	__u64 addr;
	__u64 size;
	__u32 nr_pages_per_call;
	__u32 flags;
};

#endif	/* __GUP_TEST_H */
