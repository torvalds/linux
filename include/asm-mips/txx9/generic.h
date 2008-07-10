/*
 * linux/include/asm-mips/txx9/generic.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_TXX9_GENERIC_H
#define __ASM_TXX9_GENERIC_H

#include <linux/init.h>
#include <linux/ioport.h>	/* for struct resource */

extern struct resource txx9_ce_res[];
extern char txx9_pcode_str[8];
void txx9_reg_res_init(unsigned int pcode, unsigned long base,
		       unsigned long size);

extern unsigned int txx9_master_clock;
extern unsigned int txx9_cpu_clock;
extern unsigned int txx9_gbus_clock;

#endif /* __ASM_TXX9_GENERIC_H */
