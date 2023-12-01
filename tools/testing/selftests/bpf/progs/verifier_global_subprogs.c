// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

int arr[1];
int unkn_idx;

__noinline long global_bad(void)
{
	return arr[unkn_idx]; /* BOOM */
}

__noinline long global_good(void)
{
	return arr[0];
}

__noinline long global_calls_bad(void)
{
	return global_good() + global_bad() /* does BOOM indirectly */;
}

__noinline long global_calls_good_only(void)
{
	return global_good();
}

SEC("?raw_tp")
__success __log_level(2)
/* main prog is validated completely first */
__msg("('global_calls_good_only') is global and assumed valid.")
__msg("1: (95) exit")
/* eventually global_good() is transitively validated as well */
__msg("Validating global_good() func")
__msg("('global_good') is safe for any args that match its prototype")
int chained_global_func_calls_success(void)
{
	return global_calls_good_only();
}

SEC("?raw_tp")
__failure __log_level(2)
/* main prog validated successfully first */
__msg("1: (95) exit")
/* eventually we validate global_bad() and fail */
__msg("Validating global_bad() func")
__msg("math between map_value pointer and register") /* BOOM */
int chained_global_func_calls_bad(void)
{
	return global_calls_bad();
}

/* do out of bounds access forcing verifier to fail verification if this
 * global func is called
 */
__noinline int global_unsupp(const int *mem)
{
	if (!mem)
		return 0;
	return mem[100]; /* BOOM */
}

const volatile bool skip_unsupp_global = true;

SEC("?raw_tp")
__success
int guarded_unsupp_global_called(void)
{
	if (!skip_unsupp_global)
		return global_unsupp(NULL);
	return 0;
}

SEC("?raw_tp")
__failure __log_level(2)
__msg("Func#1 ('global_unsupp') is global and assumed valid.")
__msg("Validating global_unsupp() func#1...")
__msg("value is outside of the allowed memory range")
int unguarded_unsupp_global_called(void)
{
	int x = 0;

	return global_unsupp(&x);
}

char _license[] SEC("license") = "GPL";
