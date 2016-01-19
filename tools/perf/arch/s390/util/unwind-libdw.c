#include <elfutils/libdwfl.h>
#include "../../util/unwind-libdw.h"
#include "../../util/perf_regs.h"
#include "../../util/event.h"


bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = &ui->sample->user_regs;
	Dwarf_Word dwarf_regs[PERF_REG_S390_MAX];

#define REG(r) ({						\
	Dwarf_Word val = 0;					\
	perf_reg_value(&val, user_regs, PERF_REG_S390_##r);	\
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
	dwarf_regs[11] = REG(R11);
	dwarf_regs[12] = REG(R12);
	dwarf_regs[13] = REG(R13);
	dwarf_regs[14] = REG(R14);
	dwarf_regs[15] = REG(R15);
	dwarf_regs[16] = REG(MASK);
	dwarf_regs[17] = REG(PC);

	dwfl_thread_state_register_pc(thread, dwarf_regs[17]);
	return dwfl_thread_state_registers(thread, 0, 16, dwarf_regs);
}
