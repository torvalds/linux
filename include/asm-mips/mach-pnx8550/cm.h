/*
 *
 * BRIEF MODULE DESCRIPTION
 *   Clock module specific definitions
 *
 * Author: source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#ifndef __PNX8550_CM_H
#define __PNX8550_CM_H

#define PNX8550_CM_BASE	0xBBE47000

#define PNX8550_CM_PLL0_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x000)
#define PNX8550_CM_PLL1_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x004)
#define PNX8550_CM_PLL2_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x008)
#define PNX8550_CM_PLL3_CTL    *(volatile unsigned long *)(PNX8550_CM_BASE + 0x00C)

// Table not complete.....

#define PNX8550_CM_PLL_BLOCKED_MASK     0x80000000
#define PNX8550_CM_PLL_LOCK_MASK        0x40000000
#define PNX8550_CM_PLL_CURRENT_ADJ_MASK 0x3c000000
#define PNX8550_CM_PLL_N_MASK           0x01ff0000
#define PNX8550_CM_PLL_M_MASK           0x00003f00
#define PNX8550_CM_PLL_P_MASK           0x0000000c
#define PNX8550_CM_PLL_PD_MASK          0x00000002


#endif
