/*
 * File:         include/asm-blackfin/mach-bf548/mem_init.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if (CONFIG_MEM_MT46V32M16)

#if defined CONFIG_CLKIN_HALF
#define CLKIN_HALF       1
#else
#define CLKIN_HALF       0
#endif

#if defined CONFIG_PLL_BYPASS
#define PLL_BYPASS      1
#else
#define PLL_BYPASS       0
#endif

/***************************************Currently Not Being Used *********************************/
#define flash_EBIU_AMBCTL_WAT  ((CONFIG_FLASH_SPEED_BWAT * 4) / (4000000000 / CONFIG_SCLK_HZ)) + 1
#define flash_EBIU_AMBCTL_RAT  ((CONFIG_FLASH_SPEED_BRAT * 4) / (4000000000 / CONFIG_SCLK_HZ)) + 1
#define flash_EBIU_AMBCTL_HT   ((CONFIG_FLASH_SPEED_BHT  * 4) / (4000000000 / CONFIG_SCLK_HZ))
#define flash_EBIU_AMBCTL_ST   ((CONFIG_FLASH_SPEED_BST  * 4) / (4000000000 / CONFIG_SCLK_HZ)) + 1
#define flash_EBIU_AMBCTL_TT   ((CONFIG_FLASH_SPEED_BTT  * 4) / (4000000000 / CONFIG_SCLK_HZ)) + 1

#if (flash_EBIU_AMBCTL_TT > 3)
#define flash_EBIU_AMBCTL0_TT   B0TT_4
#endif
#if (flash_EBIU_AMBCTL_TT == 3)
#define flash_EBIU_AMBCTL0_TT   B0TT_3
#endif
#if (flash_EBIU_AMBCTL_TT == 2)
#define flash_EBIU_AMBCTL0_TT   B0TT_2
#endif
#if (flash_EBIU_AMBCTL_TT < 2)
#define flash_EBIU_AMBCTL0_TT   B0TT_1
#endif

#if (flash_EBIU_AMBCTL_ST > 3)
#define flash_EBIU_AMBCTL0_ST   B0ST_4
#endif
#if (flash_EBIU_AMBCTL_ST == 3)
#define flash_EBIU_AMBCTL0_ST   B0ST_3
#endif
#if (flash_EBIU_AMBCTL_ST == 2)
#define flash_EBIU_AMBCTL0_ST   B0ST_2
#endif
#if (flash_EBIU_AMBCTL_ST < 2)
#define flash_EBIU_AMBCTL0_ST   B0ST_1
#endif

#if (flash_EBIU_AMBCTL_HT > 2)
#define flash_EBIU_AMBCTL0_HT   B0HT_3
#endif
#if (flash_EBIU_AMBCTL_HT == 2)
#define flash_EBIU_AMBCTL0_HT   B0HT_2
#endif
#if (flash_EBIU_AMBCTL_HT == 1)
#define flash_EBIU_AMBCTL0_HT   B0HT_1
#endif
#if (flash_EBIU_AMBCTL_HT == 0 && CONFIG_FLASH_SPEED_BHT == 0)
#define flash_EBIU_AMBCTL0_HT   B0HT_0
#endif
#if (flash_EBIU_AMBCTL_HT == 0 && CONFIG_FLASH_SPEED_BHT != 0)
#define flash_EBIU_AMBCTL0_HT   B0HT_1
#endif

#if (flash_EBIU_AMBCTL_WAT > 14)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_15
#endif
#if (flash_EBIU_AMBCTL_WAT == 14)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_14
#endif
#if (flash_EBIU_AMBCTL_WAT == 13)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_13
#endif
#if (flash_EBIU_AMBCTL_WAT == 12)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_12
#endif
#if (flash_EBIU_AMBCTL_WAT == 11)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_11
#endif
#if (flash_EBIU_AMBCTL_WAT == 10)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_10
#endif
#if (flash_EBIU_AMBCTL_WAT == 9)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_9
#endif
#if (flash_EBIU_AMBCTL_WAT == 8)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_8
#endif
#if (flash_EBIU_AMBCTL_WAT == 7)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_7
#endif
#if (flash_EBIU_AMBCTL_WAT == 6)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_6
#endif
#if (flash_EBIU_AMBCTL_WAT == 5)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_5
#endif
#if (flash_EBIU_AMBCTL_WAT == 4)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_4
#endif
#if (flash_EBIU_AMBCTL_WAT == 3)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_3
#endif
#if (flash_EBIU_AMBCTL_WAT == 2)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_2
#endif
#if (flash_EBIU_AMBCTL_WAT == 1)
#define flash_EBIU_AMBCTL0_WAT  B0WAT_1
#endif

#if (flash_EBIU_AMBCTL_RAT > 14)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_15
#endif
#if (flash_EBIU_AMBCTL_RAT == 14)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_14
#endif
#if (flash_EBIU_AMBCTL_RAT == 13)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_13
#endif
#if (flash_EBIU_AMBCTL_RAT == 12)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_12
#endif
#if (flash_EBIU_AMBCTL_RAT == 11)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_11
#endif
#if (flash_EBIU_AMBCTL_RAT == 10)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_10
#endif
#if (flash_EBIU_AMBCTL_RAT == 9)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_9
#endif
#if (flash_EBIU_AMBCTL_RAT == 8)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_8
#endif
#if (flash_EBIU_AMBCTL_RAT == 7)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_7
#endif
#if (flash_EBIU_AMBCTL_RAT == 6)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_6
#endif
#if (flash_EBIU_AMBCTL_RAT == 5)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_5
#endif
#if (flash_EBIU_AMBCTL_RAT == 4)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_4
#endif
#if (flash_EBIU_AMBCTL_RAT == 3)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_3
#endif
#if (flash_EBIU_AMBCTL_RAT == 2)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_2
#endif
#if (flash_EBIU_AMBCTL_RAT == 1)
#define flash_EBIU_AMBCTL0_RAT  B0RAT_1
#endif

#define flash_EBIU_AMBCTL0  \
	(flash_EBIU_AMBCTL0_WAT | flash_EBIU_AMBCTL0_RAT | flash_EBIU_AMBCTL0_HT | \
	 flash_EBIU_AMBCTL0_ST | flash_EBIU_AMBCTL0_TT | CONFIG_FLASH_SPEED_RDYEN)
