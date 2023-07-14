// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd. */

#include <elfutils/libdwfl.h>
#include "../../util/unwind-libdw.h"
#include "../../util/perf_regs.h"
#include "../../util/sample.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct regs_dump *user_regs = &ui->sample->user_regs;
	Dwarf_Word dwarf_regs[32];

#define REG(r) ({						\
	Dwarf_Word val = 0;					\
	perf_reg_value(&val, user_regs, PERF_REG_RISCV_##r);	\
	val;							\
})

	dwarf_regs[0]  = 0;
	dwarf_regs[1]  = REG(RA);
	dwarf_regs[2]  = REG(SP);
	dwarf_regs[3]  = REG(GP);
	dwarf_regs[4]  = REG(TP);
	dwarf_regs[5]  = REG(T0);
	dwarf_regs[6]  = REG(T1);
	dwarf_regs[7]  = REG(T2);
	dwarf_regs[8]  = REG(S0);
	dwarf_regs[9]  = REG(S1);
	dwarf_regs[10] = REG(A0);
	dwarf_regs[11] = REG(A1);
	dwarf_regs[12] = REG(A2);
	dwarf_regs[13] = REG(A3);
	dwarf_regs[14] = REG(A4);
	dwarf_regs[15] = REG(A5);
	dwarf_regs[16] = REG(A6);
	dwarf_regs[17] = REG(A7);
	dwarf_regs[18] = REG(S2);
	dwarf_regs[19] = REG(S3);
	dwarf_regs[20] = REG(S4);
	dwarf_regs[21] = REG(S5);
	dwarf_regs[22] = REG(S6);
	dwarf_regs[23] = REG(S7);
	dwarf_regs[24] = REG(S8);
	dwarf_regs[25] = REG(S9);
	dwarf_regs[26] = REG(S10);
	dwarf_regs[27] = REG(S11);
	dwarf_regs[28] = REG(T3);
	dwarf_regs[29] = REG(T4);
	dwarf_regs[30] = REG(T5);
	dwarf_regs[31] = REG(T6);
	dwfl_thread_state_register_pc(thread, REG(PC));

	return dwfl_thread_state_registers(thread, 0, PERF_REG_RISCV_MAX,
					   dwarf_regs);
}
