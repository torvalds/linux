/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#include <asm/perf_regs.h>

void perf_regs_load(u64 *regs);

#define PERF_REGS_MASK  ((1ULL << PERF_REG_POWERPC_MAX) - 1)
#define PERF_REGS_MAX   PERF_REG_POWERPC_MAX
#ifdef __powerpc64__
	#define PERF_SAMPLE_REGS_ABI	PERF_SAMPLE_REGS_ABI_64
#else
	#define PERF_SAMPLE_REGS_ABI	PERF_SAMPLE_REGS_ABI_32
#endif

#define PERF_REG_IP     PERF_REG_POWERPC_NIP
#define PERF_REG_SP     PERF_REG_POWERPC_R1

static const char *reg_names[] = {
	[PERF_REG_POWERPC_R0] = "r0",
	[PERF_REG_POWERPC_R1] = "r1",
	[PERF_REG_POWERPC_R2] = "r2",
	[PERF_REG_POWERPC_R3] = "r3",
	[PERF_REG_POWERPC_R4] = "r4",
	[PERF_REG_POWERPC_R5] = "r5",
	[PERF_REG_POWERPC_R6] = "r6",
	[PERF_REG_POWERPC_R7] = "r7",
	[PERF_REG_POWERPC_R8] = "r8",
	[PERF_REG_POWERPC_R9] = "r9",
	[PERF_REG_POWERPC_R10] = "r10",
	[PERF_REG_POWERPC_R11] = "r11",
	[PERF_REG_POWERPC_R12] = "r12",
	[PERF_REG_POWERPC_R13] = "r13",
	[PERF_REG_POWERPC_R14] = "r14",
	[PERF_REG_POWERPC_R15] = "r15",
	[PERF_REG_POWERPC_R16] = "r16",
	[PERF_REG_POWERPC_R17] = "r17",
	[PERF_REG_POWERPC_R18] = "r18",
	[PERF_REG_POWERPC_R19] = "r19",
	[PERF_REG_POWERPC_R20] = "r20",
	[PERF_REG_POWERPC_R21] = "r21",
	[PERF_REG_POWERPC_R22] = "r22",
	[PERF_REG_POWERPC_R23] = "r23",
	[PERF_REG_POWERPC_R24] = "r24",
	[PERF_REG_POWERPC_R25] = "r25",
	[PERF_REG_POWERPC_R26] = "r26",
	[PERF_REG_POWERPC_R27] = "r27",
	[PERF_REG_POWERPC_R28] = "r28",
	[PERF_REG_POWERPC_R29] = "r29",
	[PERF_REG_POWERPC_R30] = "r30",
	[PERF_REG_POWERPC_R31] = "r31",
	[PERF_REG_POWERPC_NIP] = "nip",
	[PERF_REG_POWERPC_MSR] = "msr",
	[PERF_REG_POWERPC_ORIG_R3] = "orig_r3",
	[PERF_REG_POWERPC_CTR] = "ctr",
	[PERF_REG_POWERPC_LINK] = "link",
	[PERF_REG_POWERPC_XER] = "xer",
	[PERF_REG_POWERPC_CCR] = "ccr",
	[PERF_REG_POWERPC_SOFTE] = "softe",
	[PERF_REG_POWERPC_TRAP] = "trap",
	[PERF_REG_POWERPC_DAR] = "dar",
	[PERF_REG_POWERPC_DSISR] = "dsisr",
	[PERF_REG_POWERPC_SIER] = "sier",
	[PERF_REG_POWERPC_MMCRA] = "mmcra"
};

static inline const char *perf_reg_name(int id)
{
	return reg_names[id];
}
#endif /* ARCH_PERF_REGS_H */
