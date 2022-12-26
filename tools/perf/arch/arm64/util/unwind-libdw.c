// SPDX-License-Identifier: GPL-2.0
#include <elfutils/libdwfl.h>
#include "../../../util/unwind-libdw.h"
#include "../../../util/perf_regs.h"
#include "../../../util/sample.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = &ui->sample->user_regs;
	Dwarf_Word dwarf_regs[PERF_REG_ARM64_MAX], dwarf_pc;

#define REG(r) ({						\
	Dwarf_Word val = 0;					\
	perf_reg_value(&val, user_regs, PERF_REG_ARM64_##r);	\
	val;							\
})

	dwarf_regs[0]  = REG(X0);
	dwarf_regs[1]  = REG(X1);
	dwarf_regs[2]  = REG(X2);
	dwarf_regs[3]  = REG(X3);
	dwarf_regs[4]  = REG(X4);
	dwarf_regs[5]  = REG(X5);
	dwarf_regs[6]  = REG(X6);
	dwarf_regs[7]  = REG(X7);
	dwarf_regs[8]  = REG(X8);
	dwarf_regs[9]  = REG(X9);
	dwarf_regs[10] = REG(X10);
	dwarf_regs[11] = REG(X11);
	dwarf_regs[12] = REG(X12);
	dwarf_regs[13] = REG(X13);
	dwarf_regs[14] = REG(X14);
	dwarf_regs[15] = REG(X15);
	dwarf_regs[16] = REG(X16);
	dwarf_regs[17] = REG(X17);
	dwarf_regs[18] = REG(X18);
	dwarf_regs[19] = REG(X19);
	dwarf_regs[20] = REG(X20);
	dwarf_regs[21] = REG(X21);
	dwarf_regs[22] = REG(X22);
	dwarf_regs[23] = REG(X23);
	dwarf_regs[24] = REG(X24);
	dwarf_regs[25] = REG(X25);
	dwarf_regs[26] = REG(X26);
	dwarf_regs[27] = REG(X27);
	dwarf_regs[28] = REG(X28);
	dwarf_regs[29] = REG(X29);
	dwarf_regs[30] = REG(LR);
	dwarf_regs[31] = REG(SP);

	if (!dwfl_thread_state_registers(thread, 0, PERF_REG_ARM64_MAX,
					 dwarf_regs))
		return false;

	dwarf_pc = REG(PC);
	dwfl_thread_state_register_pc(thread, dwarf_pc);

	return true;
}
