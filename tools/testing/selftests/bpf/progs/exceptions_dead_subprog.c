// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

/*
 * Read-only and frozen, so the verifier sees a constant zero, while clang
 * cannot fold the volatile read and keeps the call below.
 */
const volatile int never;

/* Only called from dead code, hence removed as a whole before the JIT runs. */
static __noinline int dead_subprog(struct __sk_buff *ctx)
{
	return ctx->len;
}

__noinline int exception_cb_behind_dead_subprog_cb(u64 cookie)
{
	return 42;
}

/*
 * libbpf appends the exception callback behind the subprograms reached by
 * calls, so removing the dead one compacts subprog_info[] underneath the
 * callback's index.
 */
SEC("?tc")
__exception_cb(exception_cb_behind_dead_subprog_cb)
__success __retval(42)
int exception_cb_behind_dead_subprog(struct __sk_buff *ctx)
{
	if (never)
		return dead_subprog(ctx);
	bpf_throw(0);
	return 0;
}

char _license[] SEC("license") = "GPL";
