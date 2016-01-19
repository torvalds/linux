// SPDX-License-Identifier: GPL-2.0
/*
 * Mapping of DWARF debug register numbers into register names.
 *
 *    Copyright IBM Corp. 2010
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <stddef.h>
#include <dwarf-regs.h>

#define NUM_GPRS 16

static const char *gpr_names[NUM_GPRS] = {
	"%r0", "%r1",  "%r2",  "%r3",  "%r4",  "%r5",  "%r6",  "%r7",
	"%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15",
};

const char *get_arch_regstr(unsigned int n)
{
	if (n == 64)
		return "mask";
	if (n == 65)
		return "pc";
	return (n >= NUM_GPRS) ? NULL : gpr_names[n];
}
