// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct int128_arg {
	__int128 v;
};

__u64 base;
__u64 ret_odd, ret_stack, ret_split, ret_unreliable;

#if defined(__clang__)

const volatile bool has_struct_arg = true;

/* arm64 would want v in x2 and x3, and b in x4. */
__noinline __u64 callee_odd(__u64 a, struct int128_arg v, __u64 b)
{
	return a + (__u64)v.v * 2 + (__u64)((unsigned __int128)v.v >> 64) * 3 +
	       b * 4;
}

#else

const volatile bool has_struct_arg = false;

#endif

#if defined(__clang__) && \
	(defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

const volatile bool has_stack_arg = true;

/*
 * x86-64 would want h in its third and fourth stack slots, and arm64 in
 * its first and second, leaving x7 empty.
 */
static __noinline __u64 callee_stack(__u64 a, __u64 b, __u64 c, __u64 d,
				     __u64 e, __u64 f, __u64 g,
				     struct int128_arg h, __u64 i)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 +
	       (__u64)h.v * 8 + (__u64)((unsigned __int128)h.v >> 64) * 9 +
	       i * 10;
}

/*
 * x86-64 BPF code passes h split between r9 and its first stack slot;
 * arm64 passes it in x5 and x6, so nothing is split there.
 */
static __noinline __u64 callee_split(__u64 a, __u64 b, __u64 c, __u64 d,
				     __u64 e, struct int128_arg h, __u64 i)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + (__u64)h.v * 6 +
	       (__u64)((unsigned __int128)h.v >> 64) * 7 + i * 8;
}

/*
 * A plain pointer makes the BTF of a static function unreliable, but f is
 * still passed on the stack. The store keeps p a pointer.
 */
static __noinline __u64 callee_unreliable(__u64 *p, __u64 b, __u64 c, __u64 d,
					  __u64 e, __u64 f)
{
	*p += 1;
	return *p + b * 2 + c * 3 + d * 4 + e * 5 + f * 6;
}

#else

const volatile bool has_stack_arg = false;

#endif

SEC("syscall")
int run(void *ctx)
{
#if defined(__clang__)
	struct int128_arg v = { .v = ((__int128)(base + 9) << 64) | (base + 8) };

	ret_odd = callee_odd(base + 1, v, base + 2);
#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)
	ret_stack = callee_stack(base + 1, base + 2, base + 3, base + 4,
				 base + 5, base + 6, base + 7, v, base + 10);
	ret_split = callee_split(base + 1, base + 2, base + 3, base + 4,
				 base + 5, v, base + 10);
	{
		__u64 x = base;

		ret_unreliable = callee_unreliable(&x, base + 2, base + 3,
						   base + 4, base + 5, base + 6);
	}
#endif
#endif
	return 0;
}

char _license[] SEC("license") = "GPL";
