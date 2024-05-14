/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#define TAR_1   10
#define TAR_2   20
#define TAR_3   30
#define TAR_4   40
#define TAR_5   50

#define DSCR_1  100
#define DSCR_2  200
#define DSCR_3  300
#define DSCR_4  400
#define DSCR_5  500

#define PPR_1   0x4000000000000         /* or 31,31,31*/
#define PPR_2   0x8000000000000         /* or 1,1,1 */
#define PPR_3   0xc000000000000         /* or 6,6,6 */
#define PPR_4   0x10000000000000        /* or 2,2,2 */

char *user_read = "[User Read (Running)]";
char *user_write = "[User Write (Running)]";
char *ptrace_read_running = "[Ptrace Read (Running)]";
char *ptrace_write_running = "[Ptrace Write (Running)]";
char *ptrace_read_ckpt = "[Ptrace Read (Checkpointed)]";
char *ptrace_write_ckpt = "[Ptrace Write (Checkpointed)]";

int validate_tar_registers(unsigned long *reg, unsigned long tar,
				unsigned long ppr, unsigned long dscr)
{
	int match = 1;

	if (reg[0] != tar)
		match = 0;

	if (reg[1] != ppr)
		match = 0;

	if (reg[2] != dscr)
		match = 0;

	if (!match)
		return TEST_FAIL;
	return TEST_PASS;
}
