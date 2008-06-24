#ifndef _ASM_POWERPC_CODE_PATCHING_H
#define _ASM_POWERPC_CODE_PATCHING_H

/*
 * Copyright 2008, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* Flags for create_branch:
 * "b"   == create_branch(addr, target, 0);
 * "ba"  == create_branch(addr, target, BRANCH_ABSOLUTE);
 * "bl"  == create_branch(addr, target, BRANCH_SET_LINK);
 * "bla" == create_branch(addr, target, BRANCH_ABSOLUTE | BRANCH_SET_LINK);
 */
#define BRANCH_SET_LINK	0x1
#define BRANCH_ABSOLUTE	0x2

extern void create_branch(unsigned long addr, unsigned long target, int flags);
extern void create_instruction(unsigned long addr, unsigned int instr);

#endif /* _ASM_POWERPC_CODE_PATCHING_H */
