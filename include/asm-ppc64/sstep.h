/*
 * Copyright (C) 2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct pt_regs;

/*
 * We don't allow single-stepping an mtmsrd that would clear
 * MSR_RI, since that would make the exception unrecoverable.
 * Since we need to single-step to proceed from a breakpoint,
 * we don't allow putting a breakpoint on an mtmsrd instruction.
 * Similarly we don't allow breakpoints on rfid instructions.
 * These macros tell us if an instruction is a mtmsrd or rfid.
 */
#define IS_MTMSRD(instr)	(((instr) & 0xfc0007fe) == 0x7c000164)
#define IS_RFID(instr)		(((instr) & 0xfc0007fe) == 0x4c000024)

/* Emulate instructions that cause a transfer of control. */
extern int emulate_step(struct pt_regs *regs, unsigned int instr);
