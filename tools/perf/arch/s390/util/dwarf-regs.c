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
#include <linux/kernel.h>
#include "dwarf-regs-table.h"

const char *get_arch_regstr(unsigned int n)
{
	return (n >= ARRAY_SIZE(s390_dwarf_regs)) ? NULL : s390_dwarf_regs[n];
}
