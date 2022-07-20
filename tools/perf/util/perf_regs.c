// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <string.h>
#include "perf_regs.h"
#include "event.h"

int __weak arch_sdt_arg_parse_op(char *old_op __maybe_unused,
				 char **new_op __maybe_unused)
{
	return SDT_ARG_SKIP;
}

uint64_t __weak arch__intr_reg_mask(void)
{
	return PERF_REGS_MASK;
}

uint64_t __weak arch__user_reg_mask(void)
{
	return PERF_REGS_MASK;
}

#ifdef HAVE_PERF_REGS_SUPPORT

#define perf_event_arm_regs perf_event_arm64_regs
#include "../../arch/arm64/include/uapi/asm/perf_regs.h"
#undef perf_event_arm_regs

#include "../../arch/arm/include/uapi/asm/perf_regs.h"
#include "../../arch/csky/include/uapi/asm/perf_regs.h"
#include "../../arch/mips/include/uapi/asm/perf_regs.h"
#include "../../arch/powerpc/include/uapi/asm/perf_regs.h"
#include "../../arch/riscv/include/uapi/asm/perf_regs.h"
#include "../../arch/s390/include/uapi/asm/perf_regs.h"
#include "../../arch/x86/include/uapi/asm/perf_regs.h"

static const char *__perf_reg_name_arm64(int id)
{
	switch (id) {
	case PERF_REG_ARM64_X0:
		return "x0";
	case PERF_REG_ARM64_X1:
		return "x1";
	case PERF_REG_ARM64_X2:
		return "x2";
	case PERF_REG_ARM64_X3:
		return "x3";
	case PERF_REG_ARM64_X4:
		return "x4";
	case PERF_REG_ARM64_X5:
		return "x5";
	case PERF_REG_ARM64_X6:
		return "x6";
	case PERF_REG_ARM64_X7:
		return "x7";
	case PERF_REG_ARM64_X8:
		return "x8";
	case PERF_REG_ARM64_X9:
		return "x9";
	case PERF_REG_ARM64_X10:
		return "x10";
	case PERF_REG_ARM64_X11:
		return "x11";
	case PERF_REG_ARM64_X12:
		return "x12";
	case PERF_REG_ARM64_X13:
		return "x13";
	case PERF_REG_ARM64_X14:
		return "x14";
	case PERF_REG_ARM64_X15:
		return "x15";
	case PERF_REG_ARM64_X16:
		return "x16";
	case PERF_REG_ARM64_X17:
		return "x17";
	case PERF_REG_ARM64_X18:
		return "x18";
	case PERF_REG_ARM64_X19:
		return "x19";
	case PERF_REG_ARM64_X20:
		return "x20";
	case PERF_REG_ARM64_X21:
		return "x21";
	case PERF_REG_ARM64_X22:
		return "x22";
	case PERF_REG_ARM64_X23:
		return "x23";
	case PERF_REG_ARM64_X24:
		return "x24";
	case PERF_REG_ARM64_X25:
		return "x25";
	case PERF_REG_ARM64_X26:
		return "x26";
	case PERF_REG_ARM64_X27:
		return "x27";
	case PERF_REG_ARM64_X28:
		return "x28";
	case PERF_REG_ARM64_X29:
		return "x29";
	case PERF_REG_ARM64_SP:
		return "sp";
	case PERF_REG_ARM64_LR:
		return "lr";
	case PERF_REG_ARM64_PC:
		return "pc";
	case PERF_REG_ARM64_VG:
		return "vg";
	default:
		return NULL;
	}

	return NULL;
}

static const char *__perf_reg_name_arm(int id)
{
	switch (id) {
	case PERF_REG_ARM_R0:
		return "r0";
	case PERF_REG_ARM_R1:
		return "r1";
	case PERF_REG_ARM_R2:
		return "r2";
	case PERF_REG_ARM_R3:
		return "r3";
	case PERF_REG_ARM_R4:
		return "r4";
	case PERF_REG_ARM_R5:
		return "r5";
	case PERF_REG_ARM_R6:
		return "r6";
	case PERF_REG_ARM_R7:
		return "r7";
	case PERF_REG_ARM_R8:
		return "r8";
	case PERF_REG_ARM_R9:
		return "r9";
	case PERF_REG_ARM_R10:
		return "r10";
	case PERF_REG_ARM_FP:
		return "fp";
	case PERF_REG_ARM_IP:
		return "ip";
	case PERF_REG_ARM_SP:
		return "sp";
	case PERF_REG_ARM_LR:
		return "lr";
	case PERF_REG_ARM_PC:
		return "pc";
	default:
		return NULL;
	}

	return NULL;
}

static const char *__perf_reg_name_csky(int id)
{
	switch (id) {
	case PERF_REG_CSKY_A0:
		return "a0";
	case PERF_REG_CSKY_A1:
		return "a1";
	case PERF_REG_CSKY_A2:
		return "a2";
	case PERF_REG_CSKY_A3:
		return "a3";
	case PERF_REG_CSKY_REGS0:
		return "regs0";
	case PERF_REG_CSKY_REGS1:
		return "regs1";
	case PERF_REG_CSKY_REGS2:
		return "regs2";
	case PERF_REG_CSKY_REGS3:
		return "regs3";
	case PERF_REG_CSKY_REGS4:
		return "regs4";
	case PERF_REG_CSKY_REGS5:
		return "regs5";
	case PERF_REG_CSKY_REGS6:
		return "regs6";
	case PERF_REG_CSKY_REGS7:
		return "regs7";
	case PERF_REG_CSKY_REGS8:
		return "regs8";
	case PERF_REG_CSKY_REGS9:
		return "regs9";
	case PERF_REG_CSKY_SP:
		return "sp";
	case PERF_REG_CSKY_LR:
		return "lr";
	case PERF_REG_CSKY_PC:
		return "pc";
#if defined(__CSKYABIV2__)
	case PERF_REG_CSKY_EXREGS0:
		return "exregs0";
	case PERF_REG_CSKY_EXREGS1:
		return "exregs1";
	case PERF_REG_CSKY_EXREGS2:
		return "exregs2";
	case PERF_REG_CSKY_EXREGS3:
		return "exregs3";
	case PERF_REG_CSKY_EXREGS4:
		return "exregs4";
	case PERF_REG_CSKY_EXREGS5:
		return "exregs5";
	case PERF_REG_CSKY_EXREGS6:
		return "exregs6";
	case PERF_REG_CSKY_EXREGS7:
		return "exregs7";
	case PERF_REG_CSKY_EXREGS8:
		return "exregs8";
	case PERF_REG_CSKY_EXREGS9:
		return "exregs9";
	case PERF_REG_CSKY_EXREGS10:
		return "exregs10";
	case PERF_REG_CSKY_EXREGS11:
		return "exregs11";
	case PERF_REG_CSKY_EXREGS12:
		return "exregs12";
	case PERF_REG_CSKY_EXREGS13:
		return "exregs13";
	case PERF_REG_CSKY_EXREGS14:
		return "exregs14";
	case PERF_REG_CSKY_TLS:
		return "tls";
	case PERF_REG_CSKY_HI:
		return "hi";
	case PERF_REG_CSKY_LO:
		return "lo";
#endif
	default:
		return NULL;
	}

	return NULL;
}

static const char *__perf_reg_name_mips(int id)
{
	switch (id) {
	case PERF_REG_MIPS_PC:
		return "PC";
	case PERF_REG_MIPS_R1:
		return "$1";
	case PERF_REG_MIPS_R2:
		return "$2";
	case PERF_REG_MIPS_R3:
		return "$3";
	case PERF_REG_MIPS_R4:
		return "$4";
	case PERF_REG_MIPS_R5:
		return "$5";
	case PERF_REG_MIPS_R6:
		return "$6";
	case PERF_REG_MIPS_R7:
		return "$7";
	case PERF_REG_MIPS_R8:
		return "$8";
	case PERF_REG_MIPS_R9:
		return "$9";
	case PERF_REG_MIPS_R10:
		return "$10";
	case PERF_REG_MIPS_R11:
		return "$11";
	case PERF_REG_MIPS_R12:
		return "$12";
	case PERF_REG_MIPS_R13:
		return "$13";
	case PERF_REG_MIPS_R14:
		return "$14";
	case PERF_REG_MIPS_R15:
		return "$15";
	case PERF_REG_MIPS_R16:
		return "$16";
	case PERF_REG_MIPS_R17:
		return "$17";
	case PERF_REG_MIPS_R18:
		return "$18";
	case PERF_REG_MIPS_R19:
		return "$19";
	case PERF_REG_MIPS_R20:
		return "$20";
	case PERF_REG_MIPS_R21:
		return "$21";
	case PERF_REG_MIPS_R22:
		return "$22";
	case PERF_REG_MIPS_R23:
		return "$23";
	case PERF_REG_MIPS_R24:
		return "$24";
	case PERF_REG_MIPS_R25:
		return "$25";
	case PERF_REG_MIPS_R28:
		return "$28";
	case PERF_REG_MIPS_R29:
		return "$29";
	case PERF_REG_MIPS_R30:
		return "$30";
	case PERF_REG_MIPS_R31:
		return "$31";
	default:
		break;
	}
	return NULL;
}

static const char *__perf_reg_name_powerpc(int id)
{
	switch (id) {
	case PERF_REG_POWERPC_R0:
		return "r0";
	case PERF_REG_POWERPC_R1:
		return "r1";
	case PERF_REG_POWERPC_R2:
		return "r2";
	case PERF_REG_POWERPC_R3:
		return "r3";
	case PERF_REG_POWERPC_R4:
		return "r4";
	case PERF_REG_POWERPC_R5:
		return "r5";
	case PERF_REG_POWERPC_R6:
		return "r6";
	case PERF_REG_POWERPC_R7:
		return "r7";
	case PERF_REG_POWERPC_R8:
		return "r8";
	case PERF_REG_POWERPC_R9:
		return "r9";
	case PERF_REG_POWERPC_R10:
		return "r10";
	case PERF_REG_POWERPC_R11:
		return "r11";
	case PERF_REG_POWERPC_R12:
		return "r12";
	case PERF_REG_POWERPC_R13:
		return "r13";
	case PERF_REG_POWERPC_R14:
		return "r14";
	case PERF_REG_POWERPC_R15:
		return "r15";
	case PERF_REG_POWERPC_R16:
		return "r16";
	case PERF_REG_POWERPC_R17:
		return "r17";
	case PERF_REG_POWERPC_R18:
		return "r18";
	case PERF_REG_POWERPC_R19:
		return "r19";
	case PERF_REG_POWERPC_R20:
		return "r20";
	case PERF_REG_POWERPC_R21:
		return "r21";
	case PERF_REG_POWERPC_R22:
		return "r22";
	case PERF_REG_POWERPC_R23:
		return "r23";
	case PERF_REG_POWERPC_R24:
		return "r24";
	case PERF_REG_POWERPC_R25:
		return "r25";
	case PERF_REG_POWERPC_R26:
		return "r26";
	case PERF_REG_POWERPC_R27:
		return "r27";
	case PERF_REG_POWERPC_R28:
		return "r28";
	case PERF_REG_POWERPC_R29:
		return "r29";
	case PERF_REG_POWERPC_R30:
		return "r30";
	case PERF_REG_POWERPC_R31:
		return "r31";
	case PERF_REG_POWERPC_NIP:
		return "nip";
	case PERF_REG_POWERPC_MSR:
		return "msr";
	case PERF_REG_POWERPC_ORIG_R3:
		return "orig_r3";
	case PERF_REG_POWERPC_CTR:
		return "ctr";
	case PERF_REG_POWERPC_LINK:
		return "link";
	case PERF_REG_POWERPC_XER:
		return "xer";
	case PERF_REG_POWERPC_CCR:
		return "ccr";
	case PERF_REG_POWERPC_SOFTE:
		return "softe";
	case PERF_REG_POWERPC_TRAP:
		return "trap";
	case PERF_REG_POWERPC_DAR:
		return "dar";
	case PERF_REG_POWERPC_DSISR:
		return "dsisr";
	case PERF_REG_POWERPC_SIER:
		return "sier";
	case PERF_REG_POWERPC_MMCRA:
		return "mmcra";
	case PERF_REG_POWERPC_MMCR0:
		return "mmcr0";
	case PERF_REG_POWERPC_MMCR1:
		return "mmcr1";
	case PERF_REG_POWERPC_MMCR2:
		return "mmcr2";
	case PERF_REG_POWERPC_MMCR3:
		return "mmcr3";
	case PERF_REG_POWERPC_SIER2:
		return "sier2";
	case PERF_REG_POWERPC_SIER3:
		return "sier3";
	case PERF_REG_POWERPC_PMC1:
		return "pmc1";
	case PERF_REG_POWERPC_PMC2:
		return "pmc2";
	case PERF_REG_POWERPC_PMC3:
		return "pmc3";
	case PERF_REG_POWERPC_PMC4:
		return "pmc4";
	case PERF_REG_POWERPC_PMC5:
		return "pmc5";
	case PERF_REG_POWERPC_PMC6:
		return "pmc6";
	case PERF_REG_POWERPC_SDAR:
		return "sdar";
	case PERF_REG_POWERPC_SIAR:
		return "siar";
	default:
		break;
	}
	return NULL;
}

static const char *__perf_reg_name_riscv(int id)
{
	switch (id) {
	case PERF_REG_RISCV_PC:
		return "pc";
	case PERF_REG_RISCV_RA:
		return "ra";
	case PERF_REG_RISCV_SP:
		return "sp";
	case PERF_REG_RISCV_GP:
		return "gp";
	case PERF_REG_RISCV_TP:
		return "tp";
	case PERF_REG_RISCV_T0:
		return "t0";
	case PERF_REG_RISCV_T1:
		return "t1";
	case PERF_REG_RISCV_T2:
		return "t2";
	case PERF_REG_RISCV_S0:
		return "s0";
	case PERF_REG_RISCV_S1:
		return "s1";
	case PERF_REG_RISCV_A0:
		return "a0";
	case PERF_REG_RISCV_A1:
		return "a1";
	case PERF_REG_RISCV_A2:
		return "a2";
	case PERF_REG_RISCV_A3:
		return "a3";
	case PERF_REG_RISCV_A4:
		return "a4";
	case PERF_REG_RISCV_A5:
		return "a5";
	case PERF_REG_RISCV_A6:
		return "a6";
	case PERF_REG_RISCV_A7:
		return "a7";
	case PERF_REG_RISCV_S2:
		return "s2";
	case PERF_REG_RISCV_S3:
		return "s3";
	case PERF_REG_RISCV_S4:
		return "s4";
	case PERF_REG_RISCV_S5:
		return "s5";
	case PERF_REG_RISCV_S6:
		return "s6";
	case PERF_REG_RISCV_S7:
		return "s7";
	case PERF_REG_RISCV_S8:
		return "s8";
	case PERF_REG_RISCV_S9:
		return "s9";
	case PERF_REG_RISCV_S10:
		return "s10";
	case PERF_REG_RISCV_S11:
		return "s11";
	case PERF_REG_RISCV_T3:
		return "t3";
	case PERF_REG_RISCV_T4:
		return "t4";
	case PERF_REG_RISCV_T5:
		return "t5";
	case PERF_REG_RISCV_T6:
		return "t6";
	default:
		return NULL;
	}

	return NULL;
}

static const char *__perf_reg_name_s390(int id)
{
	switch (id) {
	case PERF_REG_S390_R0:
		return "R0";
	case PERF_REG_S390_R1:
		return "R1";
	case PERF_REG_S390_R2:
		return "R2";
	case PERF_REG_S390_R3:
		return "R3";
	case PERF_REG_S390_R4:
		return "R4";
	case PERF_REG_S390_R5:
		return "R5";
	case PERF_REG_S390_R6:
		return "R6";
	case PERF_REG_S390_R7:
		return "R7";
	case PERF_REG_S390_R8:
		return "R8";
	case PERF_REG_S390_R9:
		return "R9";
	case PERF_REG_S390_R10:
		return "R10";
	case PERF_REG_S390_R11:
		return "R11";
	case PERF_REG_S390_R12:
		return "R12";
	case PERF_REG_S390_R13:
		return "R13";
	case PERF_REG_S390_R14:
		return "R14";
	case PERF_REG_S390_R15:
		return "R15";
	case PERF_REG_S390_FP0:
		return "FP0";
	case PERF_REG_S390_FP1:
		return "FP1";
	case PERF_REG_S390_FP2:
		return "FP2";
	case PERF_REG_S390_FP3:
		return "FP3";
	case PERF_REG_S390_FP4:
		return "FP4";
	case PERF_REG_S390_FP5:
		return "FP5";
	case PERF_REG_S390_FP6:
		return "FP6";
	case PERF_REG_S390_FP7:
		return "FP7";
	case PERF_REG_S390_FP8:
		return "FP8";
	case PERF_REG_S390_FP9:
		return "FP9";
	case PERF_REG_S390_FP10:
		return "FP10";
	case PERF_REG_S390_FP11:
		return "FP11";
	case PERF_REG_S390_FP12:
		return "FP12";
	case PERF_REG_S390_FP13:
		return "FP13";
	case PERF_REG_S390_FP14:
		return "FP14";
	case PERF_REG_S390_FP15:
		return "FP15";
	case PERF_REG_S390_MASK:
		return "MASK";
	case PERF_REG_S390_PC:
		return "PC";
	default:
		return NULL;
	}

	return NULL;
}

static const char *__perf_reg_name_x86(int id)
{
	switch (id) {
	case PERF_REG_X86_AX:
		return "AX";
	case PERF_REG_X86_BX:
		return "BX";
	case PERF_REG_X86_CX:
		return "CX";
	case PERF_REG_X86_DX:
		return "DX";
	case PERF_REG_X86_SI:
		return "SI";
	case PERF_REG_X86_DI:
		return "DI";
	case PERF_REG_X86_BP:
		return "BP";
	case PERF_REG_X86_SP:
		return "SP";
	case PERF_REG_X86_IP:
		return "IP";
	case PERF_REG_X86_FLAGS:
		return "FLAGS";
	case PERF_REG_X86_CS:
		return "CS";
	case PERF_REG_X86_SS:
		return "SS";
	case PERF_REG_X86_DS:
		return "DS";
	case PERF_REG_X86_ES:
		return "ES";
	case PERF_REG_X86_FS:
		return "FS";
	case PERF_REG_X86_GS:
		return "GS";
	case PERF_REG_X86_R8:
		return "R8";
	case PERF_REG_X86_R9:
		return "R9";
	case PERF_REG_X86_R10:
		return "R10";
	case PERF_REG_X86_R11:
		return "R11";
	case PERF_REG_X86_R12:
		return "R12";
	case PERF_REG_X86_R13:
		return "R13";
	case PERF_REG_X86_R14:
		return "R14";
	case PERF_REG_X86_R15:
		return "R15";

#define XMM(x) \
	case PERF_REG_X86_XMM ## x:	\
	case PERF_REG_X86_XMM ## x + 1:	\
		return "XMM" #x;
	XMM(0)
	XMM(1)
	XMM(2)
	XMM(3)
	XMM(4)
	XMM(5)
	XMM(6)
	XMM(7)
	XMM(8)
	XMM(9)
	XMM(10)
	XMM(11)
	XMM(12)
	XMM(13)
	XMM(14)
	XMM(15)
#undef XMM
	default:
		return NULL;
	}

	return NULL;
}

const char *perf_reg_name(int id, const char *arch)
{
	const char *reg_name = NULL;

	if (!strcmp(arch, "csky"))
		reg_name = __perf_reg_name_csky(id);
	else if (!strcmp(arch, "mips"))
		reg_name = __perf_reg_name_mips(id);
	else if (!strcmp(arch, "powerpc"))
		reg_name = __perf_reg_name_powerpc(id);
	else if (!strcmp(arch, "riscv"))
		reg_name = __perf_reg_name_riscv(id);
	else if (!strcmp(arch, "s390"))
		reg_name = __perf_reg_name_s390(id);
	else if (!strcmp(arch, "x86"))
		reg_name = __perf_reg_name_x86(id);
	else if (!strcmp(arch, "arm"))
		reg_name = __perf_reg_name_arm(id);
	else if (!strcmp(arch, "arm64"))
		reg_name = __perf_reg_name_arm64(id);

	return reg_name ?: "unknown";
}

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id)
{
	int i, idx = 0;
	u64 mask = regs->mask;

	if ((u64)id >= PERF_SAMPLE_REGS_CACHE_SIZE)
		return -EINVAL;

	if (regs->cache_mask & (1ULL << id))
		goto out;

	if (!(mask & (1ULL << id)))
		return -EINVAL;

	for (i = 0; i < id; i++) {
		if (mask & (1ULL << i))
			idx++;
	}

	regs->cache_mask |= (1ULL << id);
	regs->cache_regs[id] = regs->regs[idx];

out:
	*valp = regs->cache_regs[id];
	return 0;
}
#endif
