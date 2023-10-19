/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

.text
.type perf_regs_load,%function
#define STR_REG(r)	str x##r, [x0, 8 * r]
#define LDR_REG(r)	ldr x##r, [x0, 8 * r]
#define SP	(8 * 31)
#define PC	(8 * 32)
SYM_FUNC_START(perf_regs_load)
	STR_REG(0)
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
	mov x1, sp
	str x1, [x0, #SP]
	str x30, [x0, #PC]
	LDR_REG(1)
	ret
SYM_FUNC_END(perf_regs_load)
