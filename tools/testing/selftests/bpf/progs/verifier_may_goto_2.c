// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include "bpf_misc.h"
#include "bpf_experimental.h"

int gvar;

SEC("raw_tp")
__description("C code with may_goto 0")
__success
int may_goto_c_code(void)
{
	int i, tmp[3];

	for (i = 0; i < 3 && can_loop; i++)
		tmp[i] = 0;

	for (i = 0; i < 3 && can_loop; i++)
		tmp[i] = gvar - i;

	for (i = 0; i < 3 && can_loop; i++)
		gvar += tmp[i];

	return 0;
}

char _license[] SEC("license") = "GPL";
