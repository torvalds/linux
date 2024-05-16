// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>

#include "../../kselftest.h"

#define TESTS_PER_HWCAP 3

/*
 * Function expected to generate exception when the feature is not
 * supported and return when it is supported. If the specific exception
 * is generated then the handler must be able to skip over the
 * instruction safely.
 *
 * Note that it is expected that for many architecture extensions
 * there are no specific traps due to no architecture state being
 * added so we may not fault if running on a kernel which doesn't know
 * to add the hwcap.
 */
typedef void (*sig_fn)(void);

static void aes_sigill(void)
{
	/* AESE V0.16B, V0.16B */
	asm volatile(".inst 0x4e284800" : : : );
}

static void atomics_sigill(void)
{
	/* STADD W0, [SP] */
	asm volatile(".inst 0xb82003ff" : : : );
}

static void crc32_sigill(void)
{
	/* CRC32W W0, W0, W1 */
	asm volatile(".inst 0x1ac14800" : : : );
}

static void cssc_sigill(void)
{
	/* CNT x0, x0 */
	asm volatile(".inst 0xdac01c00" : : : "x0");
}

static void f8cvt_sigill(void)
{
	/* FSCALE V0.4H, V0.4H, V0.4H */
	asm volatile(".inst 0x2ec03c00");
}

static void f8dp2_sigill(void)
{
	/* FDOT V0.4H, V0.4H, V0.5H */
	asm volatile(".inst 0xe40fc00");
}

static void f8dp4_sigill(void)
{
	/* FDOT V0.2S, V0.2S, V0.2S */
	asm volatile(".inst 0xe00fc00");
}

static void f8fma_sigill(void)
{
	/* FMLALB V0.8H, V0.16B, V0.16B */
	asm volatile(".inst 0xec0fc00");
}

static void faminmax_sigill(void)
{
	/* FAMIN V0.4H, V0.4H, V0.4H */
	asm volatile(".inst 0x2ec01c00");
}

static void fp_sigill(void)
{
	asm volatile("fmov s0, #1");
}

static void fpmr_sigill(void)
{
	asm volatile("mrs x0, S3_3_C4_C4_2" : : : "x0");
}

static void ilrcpc_sigill(void)
{
	/* LDAPUR W0, [SP, #8] */
	asm volatile(".inst 0x994083e0" : : : );
}

static void jscvt_sigill(void)
{
	/* FJCVTZS W0, D0 */
	asm volatile(".inst 0x1e7e0000" : : : );
}

static void lrcpc_sigill(void)
{
	/* LDAPR W0, [SP, #0] */
	asm volatile(".inst 0xb8bfc3e0" : : : );
}

static void lse128_sigill(void)
{
	u64 __attribute__ ((aligned (16))) mem[2] = { 10, 20 };
	register u64 *memp asm ("x0") = mem;
	register u64 val0 asm ("x1") = 5;
	register u64 val1 asm ("x2") = 4;

	/* SWPP X1, X2, [X0] */
	asm volatile(".inst 0x19228001"
		     : "+r" (memp), "+r" (val0), "+r" (val1)
		     :
		     : "cc", "memory");
}

static void lut_sigill(void)
{
	/* LUTI2 V0.16B, { V0.16B }, V[0] */
	asm volatile(".inst 0x4e801000");
}

static void mops_sigill(void)
{
	char dst[1], src[1];
	register char *dstp asm ("x0") = dst;
	register char *srcp asm ("x1") = src;
	register long size asm ("x2") = 1;

	/* CPYP [x0]!, [x1]!, x2! */
	asm volatile(".inst 0x1d010440"
		     : "+r" (dstp), "+r" (srcp), "+r" (size)
		     :
		     : "cc", "memory");
}

static void pmull_sigill(void)
{
	/* PMULL V0.1Q, V0.1D, V0.1D */
	asm volatile(".inst 0x0ee0e000" : : : );
}

static void rng_sigill(void)
{
	asm volatile("mrs x0, S3_3_C2_C4_0" : : : "x0");
}

static void sha1_sigill(void)
{
	/* SHA1H S0, S0 */
	asm volatile(".inst 0x5e280800" : : : );
}

static void sha2_sigill(void)
{
	/* SHA256H Q0, Q0, V0.4S */
	asm volatile(".inst 0x5e004000" : : : );
}

static void sha512_sigill(void)
{
	/* SHA512H Q0, Q0, V0.2D */
	asm volatile(".inst 0xce608000" : : : );
}

static void sme_sigill(void)
{
	/* RDSVL x0, #0 */
	asm volatile(".inst 0x04bf5800" : : : "x0");
}

static void sme2_sigill(void)
{
	/* SMSTART ZA */
	asm volatile("msr S0_3_C4_C5_3, xzr" : : : );

	/* ZERO ZT0 */
	asm volatile(".inst 0xc0480001" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void sme2p1_sigill(void)
{
	/* SMSTART SM */
	asm volatile("msr S0_3_C4_C3_3, xzr" : : : );

	/* BFCLAMP { Z0.H - Z1.H }, Z0.H, Z0.H */
	asm volatile(".inst 0xc120C000" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smei16i32_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* SMOPA ZA0.S, P0/M, P0/M, Z0.B, Z0.B */
	asm volatile(".inst 0xa0800000" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smebi32i32_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* BMOPA ZA0.S, P0/M, P0/M, Z0.B, Z0.B */
	asm volatile(".inst 0x80800008" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smeb16b16_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* BFADD ZA.H[W0, 0], {Z0.H-Z1.H} */
	asm volatile(".inst 0xC1E41C00" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smef16f16_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* FADD ZA.H[W0, 0], { Z0.H-Z1.H } */
	asm volatile(".inst 0xc1a41C00" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smef8f16_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* FDOT ZA.H[W0, 0], Z0.B-Z1.B, Z0.B-Z1.B */
	asm volatile(".inst 0xc1a01020" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smef8f32_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* FDOT ZA.S[W0, 0], { Z0.B-Z1.B }, Z0.B[0] */
	asm volatile(".inst 0xc1500038" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smelutv2_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* LUTI4 { Z0.B-Z3.B }, ZT0, { Z0-Z1 } */
	asm volatile(".inst 0xc08b0000" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smesf8dp2_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* FDOT Z0.H, Z0.B, Z0.B[0] */
	asm volatile(".inst 0x64204400" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smesf8dp4_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* FDOT Z0.S, Z0.B, Z0.B[0] */
	asm volatile(".inst 0xc1a41C00" : : : );

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void smesf8fma_sigill(void)
{
	/* SMSTART */
	asm volatile("msr S0_3_C4_C7_3, xzr" : : : );

	/* FMLALB V0.8H, V0.16B, V0.16B */
	asm volatile(".inst 0xec0fc00");

	/* SMSTOP */
	asm volatile("msr S0_3_C4_C6_3, xzr" : : : );
}

static void sve_sigill(void)
{
	/* RDVL x0, #0 */
	asm volatile(".inst 0x04bf5000" : : : "x0");
}

static void sve2_sigill(void)
{
	/* SQABS Z0.b, P0/M, Z0.B */
	asm volatile(".inst 0x4408A000" : : : "z0");
}

static void sve2p1_sigill(void)
{
	/* BFADD Z0.H, Z0.H, Z0.H */
	asm volatile(".inst 0x65000000" : : : "z0");
}

static void sveaes_sigill(void)
{
	/* AESD z0.b, z0.b, z0.b */
	asm volatile(".inst 0x4522e400" : : : "z0");
}

static void sveb16b16_sigill(void)
{
	/* BFADD ZA.H[W0, 0], {Z0.H-Z1.H} */
	asm volatile(".inst 0xC1E41C00" : : : );
}

static void svepmull_sigill(void)
{
	/* PMULLB Z0.Q, Z0.D, Z0.D */
	asm volatile(".inst 0x45006800" : : : "z0");
}

static void svebitperm_sigill(void)
{
	/* BDEP Z0.B, Z0.B, Z0.B */
	asm volatile(".inst 0x4500b400" : : : "z0");
}

static void svesha3_sigill(void)
{
	/* EOR3 Z0.D, Z0.D, Z0.D, Z0.D */
	asm volatile(".inst 0x4203800" : : : "z0");
}

static void svesm4_sigill(void)
{
	/* SM4E Z0.S, Z0.S, Z0.S */
	asm volatile(".inst 0x4523e000" : : : "z0");
}

static void svei8mm_sigill(void)
{
	/* USDOT Z0.S, Z0.B, Z0.B[0] */
	asm volatile(".inst 0x44a01800" : : : "z0");
}

static void svef32mm_sigill(void)
{
	/* FMMLA Z0.S, Z0.S, Z0.S */
	asm volatile(".inst 0x64a0e400" : : : "z0");
}

static void svef64mm_sigill(void)
{
	/* FMMLA Z0.D, Z0.D, Z0.D */
	asm volatile(".inst 0x64e0e400" : : : "z0");
}

static void svebf16_sigill(void)
{
	/* BFCVT Z0.H, P0/M, Z0.S */
	asm volatile(".inst 0x658aa000" : : : "z0");
}

static void hbc_sigill(void)
{
	/* BC.EQ +4 */
	asm volatile("cmp xzr, xzr\n"
		     ".inst 0x54000030" : : : "cc");
}

static void uscat_sigbus(void)
{
	/* unaligned atomic access */
	asm volatile("ADD x1, sp, #2" : : : );
	/* STADD W0, [X1] */
	asm volatile(".inst 0xb820003f" : : : );
}

static void lrcpc3_sigill(void)
{
	int data[2] = { 1, 2 };

	register int *src asm ("x0") = data;
	register int data0 asm ("w2") = 0;
	register int data1 asm ("w3") = 0;

	/* LDIAPP w2, w3, [x0] */
	asm volatile(".inst 0x99431802"
	              : "=r" (data0), "=r" (data1) : "r" (src) :);
}

static const struct hwcap_data {
	const char *name;
	unsigned long at_hwcap;
	unsigned long hwcap_bit;
	const char *cpuinfo;
	sig_fn sigill_fn;
	bool sigill_reliable;
	sig_fn sigbus_fn;
	bool sigbus_reliable;
} hwcaps[] = {
	{
		.name = "AES",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_AES,
		.cpuinfo = "aes",
		.sigill_fn = aes_sigill,
	},
	{
		.name = "CRC32",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_CRC32,
		.cpuinfo = "crc32",
		.sigill_fn = crc32_sigill,
	},
	{
		.name = "CSSC",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_CSSC,
		.cpuinfo = "cssc",
		.sigill_fn = cssc_sigill,
	},
	{
		.name = "F8CVT",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_F8CVT,
		.cpuinfo = "f8cvt",
		.sigill_fn = f8cvt_sigill,
	},
	{
		.name = "F8DP4",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_F8DP4,
		.cpuinfo = "f8dp4",
		.sigill_fn = f8dp4_sigill,
	},
	{
		.name = "F8DP2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_F8DP2,
		.cpuinfo = "f8dp4",
		.sigill_fn = f8dp2_sigill,
	},
	{
		.name = "F8E5M2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_F8E5M2,
		.cpuinfo = "f8e5m2",
	},
	{
		.name = "F8E4M3",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_F8E4M3,
		.cpuinfo = "f8e4m3",
	},
	{
		.name = "F8FMA",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_F8FMA,
		.cpuinfo = "f8fma",
		.sigill_fn = f8fma_sigill,
	},
	{
		.name = "FAMINMAX",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_FAMINMAX,
		.cpuinfo = "faminmax",
		.sigill_fn = faminmax_sigill,
	},
	{
		.name = "FP",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_FP,
		.cpuinfo = "fp",
		.sigill_fn = fp_sigill,
	},
	{
		.name = "FPMR",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_FPMR,
		.cpuinfo = "fpmr",
		.sigill_fn = fpmr_sigill,
		.sigill_reliable = true,
	},
	{
		.name = "JSCVT",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_JSCVT,
		.cpuinfo = "jscvt",
		.sigill_fn = jscvt_sigill,
	},
	{
		.name = "LRCPC",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_LRCPC,
		.cpuinfo = "lrcpc",
		.sigill_fn = lrcpc_sigill,
	},
	{
		.name = "LRCPC2",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_ILRCPC,
		.cpuinfo = "ilrcpc",
		.sigill_fn = ilrcpc_sigill,
	},
	{
		.name = "LRCPC3",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_LRCPC3,
		.cpuinfo = "lrcpc3",
		.sigill_fn = lrcpc3_sigill,
	},
	{
		.name = "LSE",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_ATOMICS,
		.cpuinfo = "atomics",
		.sigill_fn = atomics_sigill,
	},
	{
		.name = "LSE2",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_USCAT,
		.cpuinfo = "uscat",
		.sigill_fn = atomics_sigill,
		.sigbus_fn = uscat_sigbus,
		.sigbus_reliable = true,
	},
	{
		.name = "LSE128",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_LSE128,
		.cpuinfo = "lse128",
		.sigill_fn = lse128_sigill,
	},
	{
		.name = "LUT",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_LUT,
		.cpuinfo = "lut",
		.sigill_fn = lut_sigill,
	},
	{
		.name = "MOPS",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_MOPS,
		.cpuinfo = "mops",
		.sigill_fn = mops_sigill,
		.sigill_reliable = true,
	},
	{
		.name = "PMULL",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_PMULL,
		.cpuinfo = "pmull",
		.sigill_fn = pmull_sigill,
	},
	{
		.name = "RNG",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_RNG,
		.cpuinfo = "rng",
		.sigill_fn = rng_sigill,
	},
	{
		.name = "RPRFM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_RPRFM,
		.cpuinfo = "rprfm",
	},
	{
		.name = "SHA1",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_SHA1,
		.cpuinfo = "sha1",
		.sigill_fn = sha1_sigill,
	},
	{
		.name = "SHA2",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_SHA2,
		.cpuinfo = "sha2",
		.sigill_fn = sha2_sigill,
	},
	{
		.name = "SHA512",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_SHA512,
		.cpuinfo = "sha512",
		.sigill_fn = sha512_sigill,
	},
	{
		.name = "SME",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME,
		.cpuinfo = "sme",
		.sigill_fn = sme_sigill,
		.sigill_reliable = true,
	},
	{
		.name = "SME2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME2,
		.cpuinfo = "sme2",
		.sigill_fn = sme2_sigill,
		.sigill_reliable = true,
	},
	{
		.name = "SME 2.1",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME2P1,
		.cpuinfo = "sme2p1",
		.sigill_fn = sme2p1_sigill,
	},
	{
		.name = "SME I16I32",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_I16I32,
		.cpuinfo = "smei16i32",
		.sigill_fn = smei16i32_sigill,
	},
	{
		.name = "SME BI32I32",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_BI32I32,
		.cpuinfo = "smebi32i32",
		.sigill_fn = smebi32i32_sigill,
	},
	{
		.name = "SME B16B16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_B16B16,
		.cpuinfo = "smeb16b16",
		.sigill_fn = smeb16b16_sigill,
	},
	{
		.name = "SME F16F16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_F16F16,
		.cpuinfo = "smef16f16",
		.sigill_fn = smef16f16_sigill,
	},
	{
		.name = "SME F8F16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_F8F16,
		.cpuinfo = "smef8f16",
		.sigill_fn = smef8f16_sigill,
	},
	{
		.name = "SME F8F32",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_F8F32,
		.cpuinfo = "smef8f32",
		.sigill_fn = smef8f32_sigill,
	},
	{
		.name = "SME LUTV2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_LUTV2,
		.cpuinfo = "smelutv2",
		.sigill_fn = smelutv2_sigill,
	},
	{
		.name = "SME SF8FMA",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_SF8FMA,
		.cpuinfo = "smesf8fma",
		.sigill_fn = smesf8fma_sigill,
	},
	{
		.name = "SME SF8DP2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_SF8DP2,
		.cpuinfo = "smesf8dp2",
		.sigill_fn = smesf8dp2_sigill,
	},
	{
		.name = "SME SF8DP4",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME_SF8DP4,
		.cpuinfo = "smesf8dp4",
		.sigill_fn = smesf8dp4_sigill,
	},
	{
		.name = "SVE",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_SVE,
		.cpuinfo = "sve",
		.sigill_fn = sve_sigill,
		.sigill_reliable = true,
	},
	{
		.name = "SVE 2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE2,
		.cpuinfo = "sve2",
		.sigill_fn = sve2_sigill,
	},
	{
		.name = "SVE 2.1",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE2P1,
		.cpuinfo = "sve2p1",
		.sigill_fn = sve2p1_sigill,
	},
	{
		.name = "SVE AES",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEAES,
		.cpuinfo = "sveaes",
		.sigill_fn = sveaes_sigill,
	},
	{
		.name = "SVE2 B16B16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE_B16B16,
		.cpuinfo = "sveb16b16",
		.sigill_fn = sveb16b16_sigill,
	},
	{
		.name = "SVE2 PMULL",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEPMULL,
		.cpuinfo = "svepmull",
		.sigill_fn = svepmull_sigill,
	},
	{
		.name = "SVE2 BITPERM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEBITPERM,
		.cpuinfo = "svebitperm",
		.sigill_fn = svebitperm_sigill,
	},
	{
		.name = "SVE2 SHA3",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVESHA3,
		.cpuinfo = "svesha3",
		.sigill_fn = svesha3_sigill,
	},
	{
		.name = "SVE2 SM4",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVESM4,
		.cpuinfo = "svesm4",
		.sigill_fn = svesm4_sigill,
	},
	{
		.name = "SVE2 I8MM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEI8MM,
		.cpuinfo = "svei8mm",
		.sigill_fn = svei8mm_sigill,
	},
	{
		.name = "SVE2 F32MM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEF32MM,
		.cpuinfo = "svef32mm",
		.sigill_fn = svef32mm_sigill,
	},
	{
		.name = "SVE2 F64MM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEF64MM,
		.cpuinfo = "svef64mm",
		.sigill_fn = svef64mm_sigill,
	},
	{
		.name = "SVE2 BF16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEBF16,
		.cpuinfo = "svebf16",
		.sigill_fn = svebf16_sigill,
	},
	{
		.name = "SVE2 EBF16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE_EBF16,
		.cpuinfo = "sveebf16",
	},
	{
		.name = "HBC",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_HBC,
		.cpuinfo = "hbc",
		.sigill_fn = hbc_sigill,
		.sigill_reliable = true,
	},
};

typedef void (*sighandler_fn)(int, siginfo_t *, void *);

#define DEF_SIGHANDLER_FUNC(SIG, NUM)					\
static bool seen_##SIG;							\
static void handle_##SIG(int sig, siginfo_t *info, void *context)	\
{									\
	ucontext_t *uc = context;					\
									\
	seen_##SIG = true;						\
	/* Skip over the offending instruction */			\
	uc->uc_mcontext.pc += 4;					\
}

DEF_SIGHANDLER_FUNC(sigill, SIGILL);
DEF_SIGHANDLER_FUNC(sigbus, SIGBUS);

bool cpuinfo_present(const char *name)
{
	FILE *f;
	char buf[2048], name_space[30], name_newline[30];
	char *s;

	/*
	 * The feature should appear with a leading space and either a
	 * trailing space or a newline.
	 */
	snprintf(name_space, sizeof(name_space), " %s ", name);
	snprintf(name_newline, sizeof(name_newline), " %s\n", name);

	f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		ksft_print_msg("Failed to open /proc/cpuinfo\n");
		return false;
	}

	while (fgets(buf, sizeof(buf), f)) {
		/* Features: line? */
		if (strncmp(buf, "Features\t:", strlen("Features\t:")) != 0)
			continue;

		/* All CPUs should be symmetric, don't read any more */
		fclose(f);

		s = strstr(buf, name_space);
		if (s)
			return true;
		s = strstr(buf, name_newline);
		if (s)
			return true;

		return false;
	}

	ksft_print_msg("Failed to find Features in /proc/cpuinfo\n");
	fclose(f);
	return false;
}

static int install_sigaction(int signum, sighandler_fn handler)
{
	int ret;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	ret = sigaction(signum, &sa, NULL);
	if (ret < 0)
		ksft_exit_fail_msg("Failed to install SIGNAL handler: %s (%d)\n",
				   strerror(errno), errno);

	return ret;
}

static void uninstall_sigaction(int signum)
{
	if (sigaction(signum, NULL, NULL) < 0)
		ksft_exit_fail_msg("Failed to uninstall SIGNAL handler: %s (%d)\n",
				   strerror(errno), errno);
}

#define DEF_INST_RAISE_SIG(SIG, NUM)					\
static bool inst_raise_##SIG(const struct hwcap_data *hwcap,		\
				bool have_hwcap)			\
{									\
	if (!hwcap->SIG##_fn) {						\
		ksft_test_result_skip(#SIG"_%s\n", hwcap->name);	\
		/* assume that it would raise exception in default */	\
		return true;						\
	}								\
									\
	install_sigaction(NUM, handle_##SIG);				\
									\
	seen_##SIG = false;						\
	hwcap->SIG##_fn();						\
									\
	if (have_hwcap) {						\
		/* Should be able to use the extension */		\
		ksft_test_result(!seen_##SIG,				\
				#SIG"_%s\n", hwcap->name);		\
	} else if (hwcap->SIG##_reliable) {				\
		/* Guaranteed a SIGNAL */				\
		ksft_test_result(seen_##SIG,				\
				#SIG"_%s\n", hwcap->name);		\
	} else {							\
		/* Missing SIGNAL might be fine */			\
		ksft_print_msg(#SIG"_%sreported for %s\n",		\
				seen_##SIG ? "" : "not ",		\
				hwcap->name);				\
		ksft_test_result_skip(#SIG"_%s\n",			\
					hwcap->name);			\
	}								\
									\
	uninstall_sigaction(NUM);					\
	return seen_##SIG;						\
}

DEF_INST_RAISE_SIG(sigill, SIGILL);
DEF_INST_RAISE_SIG(sigbus, SIGBUS);

int main(void)
{
	int i;
	const struct hwcap_data *hwcap;
	bool have_cpuinfo, have_hwcap, raise_sigill;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(hwcaps) * TESTS_PER_HWCAP);

	for (i = 0; i < ARRAY_SIZE(hwcaps); i++) {
		hwcap = &hwcaps[i];

		have_hwcap = getauxval(hwcap->at_hwcap) & hwcap->hwcap_bit;
		have_cpuinfo = cpuinfo_present(hwcap->cpuinfo);

		if (have_hwcap)
			ksft_print_msg("%s present\n", hwcap->name);

		ksft_test_result(have_hwcap == have_cpuinfo,
				 "cpuinfo_match_%s\n", hwcap->name);

		/*
		 * Testing for SIGBUS only makes sense after make sure
		 * that the instruction does not cause a SIGILL signal.
		 */
		raise_sigill = inst_raise_sigill(hwcap, have_hwcap);
		if (!raise_sigill)
			inst_raise_sigbus(hwcap, have_hwcap);
		else
			ksft_test_result_skip("sigbus_%s\n", hwcap->name);
	}

	ksft_print_cnts();

	return 0;
}
