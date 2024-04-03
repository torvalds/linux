// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#define _SDT_HAS_SEMAPHORES 1
#include "sdt.h"

#define SHARED 1
#include "bpf/libbpf_internal.h"

#define SEC(name) __attribute__((section(name), used))

unsigned short urandlib_read_with_sema_semaphore SEC(".probes");

void urandlib_read_with_sema(int iter_num, int iter_cnt, int read_sz)
{
	STAP_PROBE3(urandlib, read_with_sema, iter_num, iter_cnt, read_sz);
}

COMPAT_VERSION(urandlib_api_v1, urandlib_api, LIBURANDOM_READ_1.0.0)
int urandlib_api_v1(void)
{
	return 1;
}

DEFAULT_VERSION(urandlib_api_v2, urandlib_api, LIBURANDOM_READ_2.0.0)
int urandlib_api_v2(void)
{
	return 2;
}

COMPAT_VERSION(urandlib_api_sameoffset, urandlib_api_sameoffset, LIBURANDOM_READ_1.0.0)
DEFAULT_VERSION(urandlib_api_sameoffset, urandlib_api_sameoffset, LIBURANDOM_READ_2.0.0)
int urandlib_api_sameoffset(void)
{
	return 3;
}
