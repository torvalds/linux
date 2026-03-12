// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
	(defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) || \
	defined(__TARGET_ARCH_arm) || defined(__TARGET_ARCH_s390) || \
	defined(__TARGET_ARCH_loongarch)) && \
	__clang_major__ >= 18

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

#define BSWAP_RANGE_TEST(name, op, in_value, out_value) \
	SEC("socket") \
	__success __log_level(2) \
	__msg("r0 &= {{.*}}; R0=scalar({{.*}},var_off=(0x0; " #in_value "))") \
	__msg("r0 = " op " r0 {{.*}}; R0=scalar({{.*}},var_off=(0x0; " #out_value "))") \
	__naked void name(void) \
	{ \
		asm volatile (				\
		"call %[bpf_get_prandom_u32];"		\
		"r0 &= " #in_value ";"			\
		"r0 =  " op " r0;"			\
		"r2 =  " #out_value " ll;"		\
		"if r0 > r2 goto trap_%=;"		\
		"r0 = 0;"				\
		"exit;"					\
	"trap_%=:"					\
		"r1 = 42;"				\
		"r0 = *(u64 *)(r1 + 0);"		\
		"exit;"					\
	:						\
	: __imm(bpf_get_prandom_u32)			\
	: __clobber_all);				\
	}

BSWAP_RANGE_TEST(bswap16_range, "bswap16", 0x3f00, 0x3f)
BSWAP_RANGE_TEST(bswap32_range, "bswap32", 0x3f00, 0x3f0000)
BSWAP_RANGE_TEST(bswap64_range, "bswap64", 0x3f00, 0x3f000000000000)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
BSWAP_RANGE_TEST(be16_range, "be16", 0x3f00, 0x3f)
BSWAP_RANGE_TEST(be32_range, "be32", 0x3f00, 0x3f0000)
BSWAP_RANGE_TEST(be64_range, "be64", 0x3f00, 0x3f000000000000)
BSWAP_RANGE_TEST(le16_range, "le16", 0x3f00, 0x3f00)
BSWAP_RANGE_TEST(le32_range, "le32", 0x3f00, 0x3f00)
BSWAP_RANGE_TEST(le64_range, "le64", 0x3f00, 0x3f00)
#else
BSWAP_RANGE_TEST(be16_range, "be16", 0x3f00, 0x3f00)
BSWAP_RANGE_TEST(be32_range, "be32", 0x3f00, 0x3f00)
BSWAP_RANGE_TEST(be64_range, "be64", 0x3f00, 0x3f00)
BSWAP_RANGE_TEST(le16_range, "le16", 0x3f00, 0x3f)
BSWAP_RANGE_TEST(le32_range, "le32", 0x3f00, 0x3f0000)
BSWAP_RANGE_TEST(le64_range, "le64", 0x3f00, 0x3f000000000000)
#endif

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
