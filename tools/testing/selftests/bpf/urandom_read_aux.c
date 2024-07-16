// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include "sdt.h"

void urand_read_without_sema(int iter_num, int iter_cnt, int read_sz)
{
	/* semaphore-less USDT */
	STAP_PROBE3(urand, read_without_sema, iter_num, iter_cnt, read_sz);
}
