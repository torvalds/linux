// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

struct {
	char in[256];
	char out[256];
} data = {};

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

SEC("raw_tracepoint/sys_enter")
int test_core_bitfields(void *ctx)
{
	struct core_reloc_bitfields *in = (void *)&data.in;
	struct core_reloc_bitfields_output *out = (void *)&data.out;
	uint64_t res;

	out->ub1 = BPF_CORE_READ_BITFIELD_PROBED(in, ub1);
	out->ub2 = BPF_CORE_READ_BITFIELD_PROBED(in, ub2);
	out->ub7 = BPF_CORE_READ_BITFIELD_PROBED(in, ub7);
	out->sb4 = BPF_CORE_READ_BITFIELD_PROBED(in, sb4);
	out->sb20 = BPF_CORE_READ_BITFIELD_PROBED(in, sb20);
	out->u32 = BPF_CORE_READ_BITFIELD_PROBED(in, u32);
	out->s32 = BPF_CORE_READ_BITFIELD_PROBED(in, s32);

	return 0;
}

