// SPDX-License-Identifier: GPL-2.0
#include <elfutils/libdwfl.h>
#include "perf_regs.h"
#include "../../../util/unwind-libdw.h"
#include "../../../util/perf_regs.h"
#include "../../../util/sample.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = perf_sample__user_regs(ui->sample);
	Dwarf_Word dwarf_regs[PERF_REG_ARM_MAX];

#define REG(r) ({						\
	Dwarf_Word val = 0;					\
	perf_reg_value(&val, user_regs, PERF_REG_ARM_##r);	\
	val;							\
})

	dwarf_regs[0]  = REG(R0);
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
	dwarf_regs[11] = REG(FP);
	dwarf_regs[12] = REG(IP);
	dwarf_regs[13] = REG(SP);
	dwarf_regs[14] = REG(LR);
	dwarf_regs[15] = REG(PC);

	return dwfl_thread_state_registers(thread, 0, PERF_REG_ARM_MAX,
					   dwarf_regs);
}
