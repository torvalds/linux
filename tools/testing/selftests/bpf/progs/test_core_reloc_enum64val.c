// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <stdint.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

struct {
	char in[256];
	char out[256];
	bool skip;
} data = {};

enum named_unsigned_enum64 {
	UNSIGNED_ENUM64_VAL1 = 0x1ffffffffULL,
	UNSIGNED_ENUM64_VAL2 = 0x2ffffffffULL,
	UNSIGNED_ENUM64_VAL3 = 0x3ffffffffULL,
};

enum named_signed_enum64 {
	SIGNED_ENUM64_VAL1 = 0x1ffffffffLL,
	SIGNED_ENUM64_VAL2 = -2,
	SIGNED_ENUM64_VAL3 = 0x3ffffffffLL,
};

struct core_reloc_enum64val_output {
	bool unsigned_val1_exists;
	bool unsigned_val2_exists;
	bool unsigned_val3_exists;
	bool signed_val1_exists;
	bool signed_val2_exists;
	bool signed_val3_exists;

	long unsigned_val1;
	long unsigned_val2;
	long signed_val1;
	long signed_val2;
};

SEC("raw_tracepoint/sys_enter")
int test_core_enum64val(void *ctx)
{
#if __clang_major__ >= 15
	struct core_reloc_enum64val_output *out = (void *)&data.out;
	enum named_unsigned_enum64 named_unsigned = 0;
	enum named_signed_enum64 named_signed = 0;

	out->unsigned_val1_exists = bpf_core_enum_value_exists(named_unsigned, UNSIGNED_ENUM64_VAL1);
	out->unsigned_val2_exists = bpf_core_enum_value_exists(enum named_unsigned_enum64, UNSIGNED_ENUM64_VAL2);
	out->unsigned_val3_exists = bpf_core_enum_value_exists(enum named_unsigned_enum64, UNSIGNED_ENUM64_VAL3);
	out->signed_val1_exists = bpf_core_enum_value_exists(named_signed, SIGNED_ENUM64_VAL1);
	out->signed_val2_exists = bpf_core_enum_value_exists(enum named_signed_enum64, SIGNED_ENUM64_VAL2);
	out->signed_val3_exists = bpf_core_enum_value_exists(enum named_signed_enum64, SIGNED_ENUM64_VAL3);

	out->unsigned_val1 = bpf_core_enum_value(named_unsigned, UNSIGNED_ENUM64_VAL1);
	out->unsigned_val2 = bpf_core_enum_value(named_unsigned, UNSIGNED_ENUM64_VAL2);
	out->signed_val1 = bpf_core_enum_value(named_signed, SIGNED_ENUM64_VAL1);
	out->signed_val2 = bpf_core_enum_value(named_signed, SIGNED_ENUM64_VAL2);
	/* NAMED_ENUM64_VAL3 value is optional */

#else
	data.skip = true;
#endif

	return 0;
}
