/*
 * File:         include/asm-blackfin/cplbinit.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <asm/blackfin.h>
#include <asm/cplb.h>

#define INITIAL_T 0x1
#define SWITCH_T  0x2
#define I_CPLB    0x4
#define D_CPLB    0x8

#define IN_KERNEL 1

enum
{ZERO_P, L1I_MEM, L1D_MEM, SDRAM_KERN , SDRAM_RAM_MTD, SDRAM_DMAZ, RES_MEM, ASYNC_MEM, L2_MEM};

struct cplb_desc {
	u32 start; /* start address */
	u32 end; /* end address */
	u32 psize; /* prefered size if any otherwise 1MB or 4MB*/
	u16 attr;/* attributes */
	u16 i_conf;/* I-CPLB DATA */
	u16 d_conf;/* D-CPLB DATA */
	u16 valid;/* valid */
	const s8 name[30];/* name */
};

struct cplb_tab {
  u_long *tab;
	u16 pos;
	u16 size;
};

extern u_long icplb_table[MAX_CPLBS+1];
extern u_long dcplb_table[MAX_CPLBS+1];

/* Till here we are discussing about the static memory management model.
 * However, the operating envoronments commonly define more CPLB
 * descriptors to cover the entire addressable memory than will fit into
 * the available on-chip 16 CPLB MMRs. When this happens, the below table
 * will be used which will hold all the potentially required CPLB descriptors
 *
 * This is how Page descriptor Table is implemented in uClinux/Blackfin.
 */

#ifdef CONFIG_CPLB_SWITCH_TAB_L1
extern u_long ipdt_table[MAX_SWITCH_I_CPLBS+1]__attribute__((l1_data));
extern u_long dpdt_table[MAX_SWITCH_D_CPLBS+1]__attribute__((l1_data));

#ifdef CONFIG_CPLB_INFO
extern u_long ipdt_swapcount_table[MAX_SWITCH_I_CPLBS]__attribute__((l1_data));
extern u_long dpdt_swapcount_table[MAX_SWITCH_D_CPLBS]__attribute__((l1_data));
#endif /* CONFIG_CPLB_INFO */

#else

extern u_long ipdt_table[MAX_SWITCH_I_CPLBS+1];
extern u_long dpdt_table[MAX_SWITCH_D_CPLBS+1];

#ifdef CONFIG_CPLB_INFO
extern u_long ipdt_swapcount_table[MAX_SWITCH_I_CPLBS];
extern u_long dpdt_swapcount_table[MAX_SWITCH_D_CPLBS];
#endif /* CONFIG_CPLB_INFO */

#endif /*CONFIG_CPLB_SWITCH_TAB_L1*/

extern unsigned long reserved_mem_dcache_on;
extern unsigned long reserved_mem_icache_on;

extern void generate_cpl_tables(void);
