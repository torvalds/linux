// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Linutronix GmbH */

#include <test_progs.h>
#include <network_helpers.h>

#include "test_time_tai.skel.h"

#include <time.h>
#include <stdint.h>

#define TAI_THRESHOLD	1000000000ULL /* 1s */
#define NSEC_PER_SEC	1000000000ULL

static __u64 ts_to_ns(const struct timespec *ts)
{
	return ts->tv_sec * NSEC_PER_SEC + ts->tv_nsec;
}

void test_time_tai(void)
{
	struct __sk_buff skb = {
		.cb[0] = 0,
		.cb[1] = 0,
		.tstamp = 0,
	};
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.ctx_in = &skb,
		.ctx_size_in = sizeof(skb),
		.ctx_out = &skb,
		.ctx_size_out = sizeof(skb),
	);
	struct test_time_tai *skel;
	struct timespec now_tai;
	__u64 ts1, ts2, now;
	int ret, prog_fd;

	/* Open and load */
	skel = test_time_tai__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tai_open"))
		return;

	/* Run test program */
	prog_fd = bpf_program__fd(skel->progs.time_tai);
	ret = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(ret, "test_run");

	/* Retrieve generated TAI timestamps */
	ts1 = skb.tstamp;
	ts2 = skb.cb[0] | ((__u64)skb.cb[1] << 32);

	/* TAI != 0 */
	ASSERT_NEQ(ts1, 0, "tai_ts1");
	ASSERT_NEQ(ts2, 0, "tai_ts2");

	/* TAI is moving forward only */
	ASSERT_GE(ts2, ts1, "tai_forward");

	/* Check for future */
	ret = clock_gettime(CLOCK_TAI, &now_tai);
	ASSERT_EQ(ret, 0, "tai_gettime");
	now = ts_to_ns(&now_tai);

	ASSERT_TRUE(now > ts1, "tai_future_ts1");
	ASSERT_TRUE(now > ts2, "tai_future_ts2");

	/* Check for reasonable range */
	ASSERT_TRUE(now - ts1 < TAI_THRESHOLD, "tai_range_ts1");
	ASSERT_TRUE(now - ts2 < TAI_THRESHOLD, "tai_range_ts2");

	test_time_tai__destroy(skel);
}
