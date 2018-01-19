/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

/* Offset is based on macros from arch/powerpc/include/uapi/asm/ptrace.h. */
#define R0	 0
#define R1	 1 * 8
#define R2	 2 * 8
#define R3	 3 * 8
#define R4	 4 * 8
#define R5	 5 * 8
#define R6	 6 * 8
#define R7	 7 * 8
#define R8	 8 * 8
#define R9	 9 * 8
#define R10	10 * 8
#define R11	11 * 8
#define R12	12 * 8
#define R13	13 * 8
#define R14	14 * 8
#define R15	15 * 8
#define R16	16 * 8
#define R17	17 * 8
#define R18	18 * 8
#define R19	19 * 8
#define R20	20 * 8
#define R21	21 * 8
#define R22	22 * 8
#define R23	23 * 8
#define R24	24 * 8
#define R25	25 * 8
#define R26	26 * 8
#define R27	27 * 8
#define R28	28 * 8
#define R29	29 * 8
#define R30	30 * 8
#define R31	31 * 8
#define NIP	32 * 8
#define CTR	35 * 8
#define LINK	36 * 8
#define XER	37 * 8

.globl perf_regs_load
perf_regs_load:
	std 0, R0(3)
	std 1, R1(3)
	std 2, R2(3)
	std 3, R3(3)
	std 4, R4(3)
	std 5, R5(3)
	std 6, R6(3)
	std 7, R7(3)
	std 8, R8(3)
	std 9, R9(3)
	std 10, R10(3)
	std 11, R11(3)
	std 12, R12(3)
	std 13, R13(3)
	std 14, R14(3)
	std 15, R15(3)
	std 16, R16(3)
	std 17, R17(3)
	std 18, R18(3)
	std 19, R19(3)
	std 20, R20(3)
	std 21, R21(3)
	std 22, R22(3)
	std 23, R23(3)
	std 24, R24(3)
	std 25, R25(3)
	std 26, R26(3)
	std 27, R27(3)
	std 28, R28(3)
	std 29, R29(3)
	std 30, R30(3)
	std 31, R31(3)

	/* store NIP */
	mflr 4
	std 4, NIP(3)

	/* Store LR */
	std 4, LINK(3)

	/* Store XER */
	mfxer 4
	std 4, XER(3)

	/* Store CTR */
	mfctr 4
	std 4, CTR(3)

	/* Restore original value of r4 */
	ld 4, R4(3)

	blr
