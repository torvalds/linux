// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook

#ifndef __BPERF_STAT_U_H
#define __BPERF_STAT_U_H

enum bperf_filter_type {
	BPERF_FILTER_GLOBAL = 1,
	BPERF_FILTER_CPU,
	BPERF_FILTER_PID,
	BPERF_FILTER_TGID,
};

struct bperf_filter_value {
	__u32 accum_key;
	__u8 exited;
};

#endif /* __BPERF_STAT_U_H */
