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

u_long icplb_table[MAX_CPLBS+1];
u_long dcplb_table[MAX_CPLBS+1];

/* Till here we are discussing about the static memory management model.
 * However, the operating envoronments commonly define more CPLB
 * descriptors to cover the entire addressable memory than will fit into
 * the available on-chip 16 CPLB MMRs. When this happens, the below table
 * will be used which will hold all the potentially required CPLB descriptors
 *
 * This is how Page descriptor Table is implemented in uClinux/Blackfin.
 */

#ifdef CONFIG_CPLB_SWITCH_TAB_L1
u_long ipdt_table[MAX_SWITCH_I_CPLBS+1]__attribute__((l1_data));
u_long dpdt_table[MAX_SWITCH_D_CPLBS+1]__attribute__((l1_data));

#ifdef CONFIG_CPLB_INFO
u_long ipdt_swapcount_table[MAX_SWITCH_I_CPLBS]__attribute__((l1_data));
u_long dpdt_swapcount_table[MAX_SWITCH_D_CPLBS]__attribute__((l1_data));
#endif /* CONFIG_CPLB_INFO */

#else

u_long ipdt_table[MAX_SWITCH_I_CPLBS+1];
u_long dpdt_table[MAX_SWITCH_D_CPLBS+1];

#ifdef CONFIG_CPLB_INFO
u_long ipdt_swapcount_table[MAX_SWITCH_I_CPLBS];
u_long dpdt_swapcount_table[MAX_SWITCH_D_CPLBS];
#endif /* CONFIG_CPLB_INFO */

#endif /*CONFIG_CPLB_SWITCH_TAB_L1*/

struct s_cplb {
	struct cplb_tab init_i;
	struct cplb_tab init_d;
	struct cplb_tab switch_i;
	struct cplb_tab switch_d;
};

#if defined(CONFIG_BLKFIN_DCACHE) || defined(CONFIG_BLKFIN_CACHE)
static struct cplb_desc cplb_data[] = {
	{
		.start = 0,
		.end = SIZE_4K,
		.psize = SIZE_4K,
		.attr = INITIAL_T | SWITCH_T | I_CPLB | D_CPLB,
		.i_conf = SDRAM_OOPS,
		.d_conf = SDRAM_OOPS,
#if defined(CONFIG_DEBUG_HUNT_FOR_ZERO)
		.valid = 1,
#else
		.valid = 0,
#endif
		.name = "ZERO Pointer Saveguard",
	},
	{
		.start = L1_CODE_START,
		.end = L1_CODE_START + L1_CODE_LENGTH,
		.psize = SIZE_4M,
		.attr = INITIAL_T | SWITCH_T | I_CPLB,
		.i_conf = L1_IMEMORY,
		.d_conf = 0,
		.valid = 1,
		.name = "L1 I-Memory",
	},
	{
		.start = L1_DATA_A_START,
		.end = L1_DATA_B_START + L1_DATA_B_LENGTH,
		.psize = SIZE_4M,
		.attr = INITIAL_T | SWITCH_T | D_CPLB,
		.i_conf = 0,
		.d_conf = L1_DMEMORY,
#if ((L1_DATA_A_LENGTH > 0) || (L1_DATA_B_LENGTH > 0))
		.valid = 1,
#else
		.valid = 0,
#endif
		.name = "L1 D-Memory",
	},
	{
		.start = 0,
		.end = 0,  /* dynamic */
		.psize = 0,
		.attr = INITIAL_T | SWITCH_T | I_CPLB | D_CPLB,
		.i_conf =  SDRAM_IGENERIC,
		.d_conf =  SDRAM_DGENERIC,
		.valid = 1,
		.name = "SDRAM Kernel",
	},
	{
		.start = 0, /* dynamic */
		.end = 0, /* dynamic */
		.psize = 0,
		.attr = INITIAL_T | SWITCH_T | D_CPLB,
		.i_conf =  SDRAM_IGENERIC,
		.d_conf =  SDRAM_DNON_CHBL,
		.valid = 1,
		.name = "SDRAM RAM MTD",
	},
	{
		.start = 0, /* dynamic */
		.end = 0,   /* dynamic */
		.psize = SIZE_1M,
		.attr = INITIAL_T | SWITCH_T | D_CPLB,
		.d_conf = SDRAM_DNON_CHBL,
		.valid = 1,//(DMA_UNCACHED_REGION > 0),
		.name = "SDRAM Uncached DMA ZONE",
	},
	{
		.start = 0, /* dynamic */
		.end = 0, /* dynamic */
		.psize = 0,
		.attr = SWITCH_T | D_CPLB,
		.i_conf = 0, /* dynamic */
		.d_conf = 0, /* dynamic */
		.valid = 1,
		.name = "SDRAM Reserved Memory",
	},
	{
		.start = ASYNC_BANK0_BASE,
		.end = ASYNC_BANK3_BASE + ASYNC_BANK3_SIZE,
		.psize = 0,
		.attr = SWITCH_T | D_CPLB,
		.d_conf = SDRAM_EBIU,
		.valid = 1,
		.name = "ASYNC Memory",
	},
	{
#if defined(CONFIG_BF561)
		.start = L2_SRAM,
		.end = L2_SRAM_END,
		.psize = SIZE_1M,
		.attr = SWITCH_T | D_CPLB,
		.i_conf = L2_MEMORY,
		.d_conf = L2_MEMORY,
		.valid = 1,
#else
		.valid = 0,
#endif
		.name = "L2 Memory",
	}
};
#endif
