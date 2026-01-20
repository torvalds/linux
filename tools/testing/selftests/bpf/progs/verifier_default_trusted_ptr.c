// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC.
 */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

SEC("syscall")
__success __retval(0)
int test_default_trusted_ptr(void *ctx)
{
	struct prog_test_member *trusted_ptr;

	trusted_ptr = bpf_kfunc_get_default_trusted_ptr_test();
	/*
	 * Test BPF kfunc bpf_get_default_trusted_ptr_test() returns a
	 * PTR_TO_BTF_ID | PTR_TRUSTED, therefore it should be accepted when
	 * passed to a BPF kfunc only accepting KF_TRUSTED_ARGS.
	 */
	bpf_kfunc_put_default_trusted_ptr_test(trusted_ptr);
	return 0;
}

char _license[] SEC("license") = "GPL";
