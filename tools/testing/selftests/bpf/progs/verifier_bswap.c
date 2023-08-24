// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
     (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64)) && __clang_major__ >= 18

SEC("socket")
__description("BSWAP, 16")
__success __success_unpriv __retval(0x23ff)
__naked void bswap_16(void)
{
	asm volatile ("					\
	r0 = 0xff23;					\
	r0 = bswap16 r0;				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("BSWAP, 32")
__success __success_unpriv __retval(0x23ff0000)
__naked void bswap_32(void)
{
	asm volatile ("					\
	r0 = 0xff23;					\
	r0 = bswap32 r0;				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("BSWAP, 64")
__success __success_unpriv __retval(0x34ff12ff)
__naked void bswap_64(void)
{
	asm volatile ("					\
	r0 = %[u64_val] ll;					\
	r0 = bswap64 r0;				\
	exit;						\
"	:
	: [u64_val]"i"(0xff12ff34ff56ff78ull)
	: __clobber_all);
}

#else

SEC("socket")
__description("cpuv4 is not supported by compiler or jit, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
