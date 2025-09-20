// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021. Huawei Technologies Co., Ltd */
#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define STRNCMP_STR_SZ 4096

/* Will be updated by benchmark before program loading */
const volatile unsigned int cmp_str_len = 1;
const char target[STRNCMP_STR_SZ];

long hits = 0;
char str[STRNCMP_STR_SZ];

char _license[] SEC("license") = "GPL";

static __always_inline int local_strncmp(const char *s1, unsigned int sz,
					 const char *s2)
{
	int ret = 0;
	unsigned int i;

	for (i = 0; i < sz; i++) {
		/* E.g. 0xff > 0x31 */
		ret = (unsigned char)s1[i] - (unsigned char)s2[i];
		if (ret || !s1[i])
			break;
	}

	return ret;
}

SEC("tp/syscalls/sys_enter_getpgid")
int strncmp_no_helper(void *ctx)
{
	const char *target_str = target;

	barrier_var(target_str);
	if (local_strncmp(str, cmp_str_len + 1, target_str) < 0)
		__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("tp/syscalls/sys_enter_getpgid")
int strncmp_helper(void *ctx)
{
	if (bpf_strncmp(str, cmp_str_len + 1, target) < 0)
		__sync_add_and_fetch(&hits, 1);
	return 0;
}

