// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020-2023 Loongson Technology Corporation Limited */

#include <elfutils/libdwfl.h>
#include "../../util/unwind-libdw.h"
#include "../../util/perf_regs.h"
#include "../../util/sample.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = &ui->sample->user_regs;
	Dwarf_Word dwarf_regs[PERF_REG_LOONGARCH_MAX];

#define REG(r) ({							\
	Dwarf_Word val = 0;						\
	perf_reg_value(&val, user_regs, PERF_REG_LOONGARCH_##r);	\
	val;								\
})

	dwarf_regs[0]  = 0;
	dwarf_regs[1]  = REG(R1);
	dwarf_regs[2]  = REG(R2);
	dwarf_regs[3]  = REG(R3);
	dwarf_regs[4]  = REG(R4);
	dwarf_regs[5]  = REG(R5);
	dwarf_regs[6]  = REG(R6);
	dwarf_regs[7]  = REG(R7);
	dwarf_regs[8]  = REG(R8);
	dwarf_regs[9]  = REG(R9);
	dwarf_regs[10] = REG(R10);
	dwarf_regs[11] = REG(R11);
	dwarf_regs[12] = REG(R12);
	dwarf_regs[13] = REG(R13);
	dwarf_regs[14] = REG(R14);
	dwarf_regs[15] = REG(R15);
	dwarf_regs[16] = REG(R16);
	dwarf_regs[17] = REG(R17);
	dwarf_regs[18] = REG(R18);
	dwarf_regs[19] = REG(R19);
	dwarf_regs[20] = REG(R20);
	dwarf_regs[21] = REG(R21);
	dwarf_regs[22] = REG(R22);
	dwarf_regs[23] = REG(R23);
	dwarf_regs[24] = REG(R24);
	dwarf_regs[25] = REG(R25);
	dwarf_regs[26] = REG(R26);
	dwarf_regs[27] = REG(R27);
	dwarf_regs[28] = REG(R28);
	dwarf_regs[29] = REG(R29);
	dwarf_regs[30] = REG(R30);
	dwarf_regs[31] = REG(R31);
	dwfl_thread_state_register_pc(thread, REG(PC));

	return dwfl_thread_state_registers(thread, 0, PERF_REG_LOONGARCH_MAX, dwarf_regs);
}
