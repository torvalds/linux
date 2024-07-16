// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#define _SDT_HAS_SEMAPHORES 1
#include "sdt.h"

#define SEC(name) __attribute__((section(name), used))

unsigned short urandlib_read_with_sema_semaphore SEC(".probes");

void urandlib_read_with_sema(int iter_num, int iter_cnt, int read_sz)
{
	STAP_PROBE3(urandlib, read_with_sema, iter_num, iter_cnt, read_sz);
}
