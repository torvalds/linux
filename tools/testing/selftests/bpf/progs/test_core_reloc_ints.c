// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include "bpf_helpers.h"
#include "bpf_core_read.h"

char _license[] SEC("license") = "GPL";

static volatile struct data {
	char in[256];
	char out[256];
} data;

struct core_reloc_ints {
	uint8_t		u8_field;
	int8_t		s8_field;
	uint16_t	u16_field;
	int16_t		s16_field;
	uint32_t	u32_field;
	int32_t		s32_field;
	uint64_t	u64_field;
	int64_t		s64_field;
};

#define CORE_READ(dst, src) bpf_core_read(dst, sizeof(*(dst)), src)

SEC("raw_tracepoint/sys_enter")
int test_core_ints(void *ctx)
{
	struct core_reloc_ints *in = (void *)&data.in;
	struct core_reloc_ints *out = (void *)&data.out;

	if (CORE_READ(&out->u8_field, &in->u8_field) ||
	    CORE_READ(&out->s8_field, &in->s8_field) ||
	    CORE_READ(&out->u16_field, &in->u16_field) ||
	    CORE_READ(&out->s16_field, &in->s16_field) ||
	    CORE_READ(&out->u32_field, &in->u32_field) ||
	    CORE_READ(&out->s32_field, &in->s32_field) ||
	    CORE_READ(&out->u64_field, &in->u64_field) ||
	    CORE_READ(&out->s64_field, &in->s64_field))
		return 1;

	return 0;
}

