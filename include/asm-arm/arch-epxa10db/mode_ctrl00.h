#ifndef __MODE_CTRL00_H
#define __MODE_CTRL00_H

/*
 * Register definitions for the reset and mode control
 */

/*
 *  Copyright (C) 2001 Altera Corporation
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */



#define BOOT_CR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  ))
#define BOOT_CR_BF_MSK (0x1)
#define BOOT_CR_BF_OFST (0)
#define BOOT_CR_HM_MSK (0x2)
#define BOOT_CR_HM_OFST (1)
#define BOOT_CR_RE_MSK (0x4)
#define BOOT_CR_RE_OFST (2)

#define RESET_SR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x4 ))
#define RESET_SR_WR_MSK (0x1)
#define RESET_SR_WR_OFST (0)
#define RESET_SR_CR_MSK (0x2)
#define RESET_SR_CR_OFST (1)
#define RESET_SR_JT_MSK (0x4)
#define RESET_SR_JT_OFST (2)
#define RESET_SR_ER_MSK (0x8)
#define RESET_SR_ER_OFST (3)

#define ID_CODE(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x08 ))

#define SRAM0_SR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x20 ))
#define SRAM0_SR_SIZE_MSK (0xFFFFF000)
#define SRAM0_SR_SIZE_OFST (12)

#define SRAM1_SR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x24 ))
#define SRAM1_SR_SIZE_MSK (0xFFFFF000)
#define SRAM1_SR_SIZE_OFST (12)

#define DPSRAM0_SR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x30 ))

#define DPSRAM0_SR_MODE_MSK (0xF)
#define DPSRAM0_SR_MODE_OFST (0)
#define DPSRAM0_SR_GLBL_MSK (0x30)
#define DPSRAM0_SR_SIZE_MSK (0xFFFFF000)
#define DPSRAM0_SR_SIZE_OFST (12)

#define DPSRAM0_LCR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x34 ))
#define DPSRAM0_LCR_LCKADDR_MSK (0x1FFE0)
#define DPSRAM0_LCR_LCKADDR_OFST (4)

#define DPSRAM1_SR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x38 ))
#define DPSRAM1_SR_MODE_MSK (0xF)
#define DPSRAM1_SR_MODE_OFST (0)
#define DPSRAM1_SR_GLBL_MSK (0x30)
#define DPSRAM1_SR_GLBL_OFST (4)
#define DPSRAM1_SR_SIZE_MSK (0xFFFFF000)
#define DPSRAM1_SR_SIZE_OFST (12)

#define DPSRAM1_LCR(BASE_ADDR) (MODE_CTRL00_TYPE (BASE_ADDR  + 0x3C ))
#define DPSRAM1_LCR_LCKADDR_MSK (0x1FFE0)
#define DPSRAM1_LCR_LCKADDR_OFST (4)

#endif /* __MODE_CTRL00_H */
