#ifndef __PLD_CONF00_H
#define __PLD_CONF00_H

/*
 * Register definitions for the PLD Configuration Logic
 */

/*
 *  
 *  This file contains the register definitions for the Excalibur
 *  Interrupt controller INT_CTRL00.
 *
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
 
#define CONFIG_CONTROL(BASE_ADDR) (PLD_CONF00_TYPE (BASE_ADDR))  
#define CONFIG_CONTROL_LK_MSK (0x1)
#define CONFIG_CONTROL_LK_OFST (0)
#define CONFIG_CONTROL_CO_MSK (0x2)
#define CONFIG_CONTROL_CO_OFST (1)
#define CONFIG_CONTROL_B_MSK  (0x4)
#define CONFIG_CONTROL_B_OFST (2)
#define CONFIG_CONTROL_PC_MSK (0x8)
#define CONFIG_CONTROL_PC_OFST (3)
#define CONFIG_CONTROL_E_MSK (0x10)
#define CONFIG_CONTROL_E_OFST (4)
#define CONFIG_CONTROL_ES_MSK (0xE0)
#define CONFIG_CONTROL_ES_OFST (5)
#define CONFIG_CONTROL_ES_0_MSK (0x20)
#define CONFIG_CONTROL_ES_1_MSK (0x40)
#define CONFIG_CONTROL_ES_2_MSK (0x80)

#define CONFIG_CONTROL_CLOCK(BASE_ADDR) (PLD_CONF00_TYPE (BASE_ADDR  + 0x4 ))
#define CONFIG_CONTROL_CLOCK_RATIO_MSK (0xFFFF)
#define CONFIG_CONTROL_CLOCK_RATIO_OFST (0)

#define CONFIG_CONTROL_DATA(BASE_ADDR) (PLD_CONF00_TYPE (BASE_ADDR  + 0x8 ))
#define CONFIG_CONTROL_DATA_MSK (0xFFFFFFFF)
#define CONFIG_CONTROL_DATA_OFST (0)

#define CONFIG_UNLOCK(BASE_ADDR) (PLD_CONF00_TYPE (BASE_ADDR  + 0xC )) 
#define CONFIG_UNLOCK_MSK (0xFFFFFFFF)
#define CONFIG_UNLOCK_OFST (0)

#define CONFIG_UNLOCK_MAGIC (0x554E4C4B)

#endif /* __PLD_CONF00_H */












