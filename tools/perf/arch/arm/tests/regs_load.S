/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

#define R0 0x00
#define R1 0x08
#define R2 0x10
#define R3 0x18
#define R4 0x20
#define R5 0x28
#define R6 0x30
#define R7 0x38
#define R8 0x40
#define R9 0x48
#define SL 0x50
#define FP 0x58
#define IP 0x60
#define SP 0x68
#define LR 0x70
#define PC 0x78

/*
 * Implementation of void perf_regs_load(u64 *regs);
 *
 * This functions fills in the 'regs' buffer from the actual registers values,
 * in the way the perf built-in unwinding test expects them:
 * - the PC at the time at the call to this function. Since this function
 *   is called using a bl instruction, the PC value is taken from LR.
 * The built-in unwinding test then unwinds the call stack from the dwarf
 * information in unwind__get_entries.
 *
 * Notes:
 * - the 8 bytes stride in the registers offsets comes from the fact
 * that the registers are stored in an u64 array (u64 *regs),
 * - the regs buffer needs to be zeroed before the call to this function,
 * in this case using a calloc in dwarf-unwind.c.
 */

.text
.type perf_regs_load,%function
SYM_FUNC_START(perf_regs_load)
	str r0, [r0, #R0]
	str r1, [r0, #R1]
	str r2, [r0, #R2]
	str r3, [r0, #R3]
	str r4, [r0, #R4]
	str r5, [r0, #R5]
	str r6, [r0, #R6]
	str r7, [r0, #R7]
	str r8, [r0, #R8]
	str r9, [r0, #R9]
	str sl, [r0, #SL]
	str fp, [r0, #FP]
	str ip, [r0, #IP]
	str sp, [r0, #SP]
	str lr, [r0, #LR]
	str lr, [r0, #PC]	// store pc as lr in order to skip the call
	                        //  to this function
	mov pc, lr
SYM_FUNC_END(perf_regs_load)
