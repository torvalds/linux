/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */
#ifndef _ASM_IA64_SN_RW_MMR_H
#define _ASM_IA64_SN_RW_MMR_H


/*
 * This file contains macros used to access MMR registers via
 * uncached physical addresses.
 * 	pio_phys_read_mmr  - read an MMR
 * 	pio_phys_write_mmr - write an MMR
 * 	pio_atomic_phys_write_mmrs - atomically write 1 or 2 MMRs with psr.ic=0
 *		Second MMR will be skipped if address is NULL
 *
 * Addresses passed to these routines should be uncached physical addresses
 * ie., 0x80000....
 */


extern inline long
pio_phys_read_mmr(volatile long *mmr) 
{
	long val;
        asm volatile
            ("mov r2=psr;;"
             "rsm psr.i | psr.dt;;"
             "srlz.i;;"
             "ld8.acq %0=[%1];;"
             "mov psr.l=r2;;"
             "srlz.i;;"
             : "=r"(val)
             : "r"(mmr)
	     : "r2");
        return val;
}



extern inline void
pio_phys_write_mmr(volatile long *mmr, long val) 
{
        asm volatile
            ("mov r2=psr;;"
             "rsm psr.i | psr.dt;;"
             "srlz.i;;"
             "st8.rel [%0]=%1;;"
             "mov psr.l=r2;;"
             "srlz.i;;"
	     :: "r"(mmr), "r"(val)
             : "r2", "memory");
}            

extern inline void
pio_atomic_phys_write_mmrs(volatile long *mmr1, long val1, volatile long *mmr2, long val2) 
{
        asm volatile
            ("mov r2=psr;;"
             "rsm psr.i | psr.dt | psr.ic;;"
	     "cmp.ne p9,p0=%2,r0;"
             "srlz.i;;"
             "st8.rel [%0]=%1;"
             "(p9) st8.rel [%2]=%3;;"
             "mov psr.l=r2;;"
             "srlz.i;;"
	     :: "r"(mmr1), "r"(val1), "r"(mmr2), "r"(val2)
             : "p9", "r2", "memory");
}            

#endif /* _ASM_IA64_SN_RW_MMR_H */
