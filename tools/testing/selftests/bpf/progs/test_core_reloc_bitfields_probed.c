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

struct core_reloc_bitfields {
	/* unsigned bitfields */
	uint8_t		ub1: 1;
	uint8_t		ub2: 2;
	uint32_t	ub7: 7;
	/* signed bitfields */
	int8_t		sb4: 4;
	int32_t		sb20: 20;
	/* non-bitfields */
	uint32_t	u32;
	int32_t		s32;
};

/* bitfield read results, all as plain integers */
struct core_reloc_bitfields_output {
	int64_t		ub1;
	int64_t		ub2;
	int64_t		ub7;
	int64_t		sb4;
	int64_t		sb20;
	int64_t		u32;
	int64_t		s32;
};

#define TRANSFER_BITFIELD(in, out, field)				\
	if (BPF_CORE_READ_BITFIELD_PROBED(in, field, &res))		\
		return 1;						\
	out->field = res

SEC("raw_tracepoint/sys_enter")
int test_core_bitfields(void *ctx)
{
	struct core_reloc_bitfields *in = (void *)&data.in;
	struct core_reloc_bitfields_output *out = (void *)&data.out;
	uint64_t res;

	TRANSFER_BITFIELD(in, out, ub1);
	TRANSFER_BITFIELD(in, out, ub2);
	TRANSFER_BITFIELD(in, out, ub7);
	TRANSFER_BITFIELD(in, out, sb4);
	TRANSFER_BITFIELD(in, out, sb20);
	TRANSFER_BITFIELD(in, out, u32);
	TRANSFER_BITFIELD(in, out, s32);

	return 0;
}

