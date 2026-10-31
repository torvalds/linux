/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

/*
 * Fill the u64 array 'regs' at the slots of enum perf_event_riscv_regs:
 * x1..x31 map one-to-one onto PERF_REG_RISCV_RA..PERF_REG_RISCV_T6, and the
 * stride stays 8 bytes on RV32 because the slots are u64 and the caller
 * zeroes them.
 *
 * PERF_REG_RISCV_PC is filled with ra, the return address of the call to
 * this function, so that unwinding starts at the caller rather than here.
 */

#if __riscv_xlen == 64
#define REG_S	sd
#else
#define REG_S	sw
#endif

#define STR_REG(r)	REG_S x##r, 8 * r(a0)

	.text
	.type perf_regs_load,%function
SYM_FUNC_START(perf_regs_load)
	STR_REG(1)
	STR_REG(2)
	STR_REG(3)
	STR_REG(4)
	STR_REG(5)
	STR_REG(6)
	STR_REG(7)
	STR_REG(8)
	STR_REG(9)
	STR_REG(10)
	STR_REG(11)
	STR_REG(12)
	STR_REG(13)
	STR_REG(14)
	STR_REG(15)
	STR_REG(16)
	STR_REG(17)
	STR_REG(18)
	STR_REG(19)
	STR_REG(20)
	STR_REG(21)
	STR_REG(22)
	STR_REG(23)
	STR_REG(24)
	STR_REG(25)
	STR_REG(26)
	STR_REG(27)
	STR_REG(28)
	STR_REG(29)
	STR_REG(30)
	STR_REG(31)
	REG_S ra, 0(a0)
	ret
SYM_FUNC_END(perf_regs_load)
