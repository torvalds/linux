// SPDX-License-Identifier: GPL-2.0
#include <elfutils/libdwfl.h>
#include "../../../util/unwind-libdw.h"
#include "../../../util/perf_regs.h"
#include "../../../util/event.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = &ui->sample->user_regs;
	Dwarf_Word dwarf_regs[17];
	unsigned nregs;

#define REG(r) ({						\
	Dwarf_Word val = 0;					\
	perf_reg_value(&val, user_regs, PERF_REG_X86_##r);	\
	val;							\
})

	if (user_regs->abi == PERF_SAMPLE_REGS_ABI_32) {
		dwarf_regs[0] = REG(AX);
		dwarf_regs[1] = REG(CX);
		dwarf_regs[2] = REG(DX);
		dwarf_regs[3] = REG(BX);
		dwarf_regs[4] = REG(SP);
		dwarf_regs[5] = REG(BP);
		dwarf_regs[6] = REG(SI);
		dwarf_regs[7] = REG(DI);
		dwarf_regs[8] = REG(IP);
		nregs = 9;
	} else {
		dwarf_regs[0]  = REG(AX);
		dwarf_regs[1]  = REG(DX);
		dwarf_regs[2]  = REG(CX);
		dwarf_regs[3]  = REG(BX);
		dwarf_regs[4]  = REG(SI);
		dwarf_regs[5]  = REG(DI);
		dwarf_regs[6]  = REG(BP);
		dwarf_regs[7]  = REG(SP);
		dwarf_regs[8]  = REG(R8);
		dwarf_regs[9]  = REG(R9);
		dwarf_regs[10] = REG(R10);
		dwarf_regs[11] = REG(R11);
		dwarf_regs[12] = REG(R12);
		dwarf_regs[13] = REG(R13);
		dwarf_regs[14] = REG(R14);
		dwarf_regs[15] = REG(R15);
		dwarf_regs[16] = REG(IP);
		nregs = 17;
	}

	return dwfl_thread_state_registers(thread, 0, nregs, dwarf_regs);
}
