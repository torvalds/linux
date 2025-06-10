// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd.

#include <elfutils/libdwfl.h>
#include "perf_regs.h"
#include "../../util/unwind-libdw.h"
#include "../../util/perf_regs.h"
#include "../../util/event.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = perf_sample__user_regs(ui->sample);
	Dwarf_Word dwarf_regs[PERF_REG_CSKY_MAX];

#define REG(r) ({						\
	Dwarf_Word val = 0;					\
	perf_reg_value(&val, user_regs, PERF_REG_CSKY_##r);	\
	val;							\
})

#if defined(__CSKYABIV2__)
	dwarf_regs[0]  = REG(A0);
	dwarf_regs[1]  = REG(A1);
	dwarf_regs[2]  = REG(A2);
	dwarf_regs[3]  = REG(A3);
	dwarf_regs[4]  = REG(REGS0);
	dwarf_regs[5]  = REG(REGS1);
	dwarf_regs[6]  = REG(REGS2);
	dwarf_regs[7]  = REG(REGS3);
	dwarf_regs[8]  = REG(REGS4);
	dwarf_regs[9]  = REG(REGS5);
	dwarf_regs[10] = REG(REGS6);
	dwarf_regs[11] = REG(REGS7);
	dwarf_regs[12] = REG(REGS8);
	dwarf_regs[13] = REG(REGS9);
	dwarf_regs[14] = REG(SP);
	dwarf_regs[15] = REG(LR);
	dwarf_regs[16] = REG(EXREGS0);
	dwarf_regs[17] = REG(EXREGS1);
	dwarf_regs[18] = REG(EXREGS2);
	dwarf_regs[19] = REG(EXREGS3);
	dwarf_regs[20] = REG(EXREGS4);
	dwarf_regs[21] = REG(EXREGS5);
	dwarf_regs[22] = REG(EXREGS6);
	dwarf_regs[23] = REG(EXREGS7);
	dwarf_regs[24] = REG(EXREGS8);
	dwarf_regs[25] = REG(EXREGS9);
	dwarf_regs[26] = REG(EXREGS10);
	dwarf_regs[27] = REG(EXREGS11);
	dwarf_regs[28] = REG(EXREGS12);
	dwarf_regs[29] = REG(EXREGS13);
	dwarf_regs[30] = REG(EXREGS14);
	dwarf_regs[31] = REG(TLS);
	dwarf_regs[32] = REG(PC);
#else
	dwarf_regs[0]  = REG(SP);
	dwarf_regs[1]  = REG(REGS9);
	dwarf_regs[2]  = REG(A0);
	dwarf_regs[3]  = REG(A1);
	dwarf_regs[4]  = REG(A2);
	dwarf_regs[5]  = REG(A3);
	dwarf_regs[6]  = REG(REGS0);
	dwarf_regs[7]  = REG(REGS1);
	dwarf_regs[8]  = REG(REGS2);
	dwarf_regs[9]  = REG(REGS3);
	dwarf_regs[10] = REG(REGS4);
	dwarf_regs[11] = REG(REGS5);
	dwarf_regs[12] = REG(REGS6);
	dwarf_regs[13] = REG(REGS7);
	dwarf_regs[14] = REG(REGS8);
	dwarf_regs[15] = REG(LR);
#endif
	dwfl_thread_state_register_pc(thread, REG(PC));

	return dwfl_thread_state_registers(thread, 0, PERF_REG_CSKY_MAX,
					   dwarf_regs);
}
