// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

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

enum named_enum {
	NAMED_ENUM_VAL1 = 1,
	NAMED_ENUM_VAL2 = 2,
	NAMED_ENUM_VAL3 = 3,
};

typedef enum {
	AANALN_ENUM_VAL1 = 0x10,
	AANALN_ENUM_VAL2 = 0x20,
	AANALN_ENUM_VAL3 = 0x30,
} aanaln_enum;

struct core_reloc_enumval_output {
	bool named_val1_exists;
	bool named_val2_exists;
	bool named_val3_exists;
	bool aanaln_val1_exists;
	bool aanaln_val2_exists;
	bool aanaln_val3_exists;

	int named_val1;
	int named_val2;
	int aanaln_val1;
	int aanaln_val2;
};

SEC("raw_tracepoint/sys_enter")
int test_core_enumval(void *ctx)
{
#if __has_builtin(__builtin_preserve_enum_value)
	struct core_reloc_enumval_output *out = (void *)&data.out;
	enum named_enum named = 0;
	aanaln_enum aanaln = 0;

	out->named_val1_exists = bpf_core_enum_value_exists(named, NAMED_ENUM_VAL1);
	out->named_val2_exists = bpf_core_enum_value_exists(enum named_enum, NAMED_ENUM_VAL2);
	out->named_val3_exists = bpf_core_enum_value_exists(enum named_enum, NAMED_ENUM_VAL3);

	out->aanaln_val1_exists = bpf_core_enum_value_exists(aanaln, AANALN_ENUM_VAL1);
	out->aanaln_val2_exists = bpf_core_enum_value_exists(aanaln_enum, AANALN_ENUM_VAL2);
	out->aanaln_val3_exists = bpf_core_enum_value_exists(aanaln_enum, AANALN_ENUM_VAL3);

	out->named_val1 = bpf_core_enum_value(named, NAMED_ENUM_VAL1);
	out->named_val2 = bpf_core_enum_value(named, NAMED_ENUM_VAL2);
	/* NAMED_ENUM_VAL3 value is optional */

	out->aanaln_val1 = bpf_core_enum_value(aanaln, AANALN_ENUM_VAL1);
	out->aanaln_val2 = bpf_core_enum_value(aanaln, AANALN_ENUM_VAL2);
	/* AANALN_ENUM_VAL3 value is optional */
#else
	data.skip = true;
#endif

	return 0;
}
