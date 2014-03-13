/*
 * Header providing constants for Rockchip pinctrl bindings.
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
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
 */

#ifndef __DT_BINDINGS_ROCKCHIP_PM_H__
#define __DT_BINDINGS_ROCKCHIP_PM_H__

/*********************CTRBIT DEFINE*****************************/
#define RKPM_CTR_PWR_DMNS (0x1<<0)
#define RKPM_CTR_GTCLKS (0x1<<1)
#define RKPM_CTR_PLLS (0x1<<2)
#define RKPM_CTR_VOLTS (0x1<<3)
#define RKPM_CTR_GPIOS (0x1<<4)
#define RKPM_CTR_DDR (0x1<<5)
#define RKPM_CTR_PMIC (0x1<<6)
	/*
	*sys_clk 24M div , it is mutex for 
	**RKPM_CTR_SYSCLK_DIV and RKPM_CTR_SYSCLK_32K and RKPM_CTR_SYSCLK_OSC_DIS.
	**you can select only one
	*/
#define RKPM_CTR_SYSCLK_DIV (0x1<<7)// system clk is 24M,and div to min
#define RKPM_CTR_SYSCLK_32K (0x1<<8)// switch sysclk to 32k,need hardwart susport. and div to min
#define RKPM_CTR_SYSCLK_OSC_DIS (0x1<<9) // switch sysclk to 32k,disable 24M OSC,need hardwart susport. and div to min

       //pm mode Function Selection
#define RKPM_CTR_NORIDLE_MD (0x1<<16)
    
        //debug ctrl Selection
#define RKPM_CTR_RET_DIRT (0x1<<24)
#define RKPM_CTR_ONLY_WFI (0x1<<25)
#define RKPM_CTR_SRAM_NO_WFI (0x1<<26)
#define RKPM_CTR_WAKE_UP_KEY (0x1<<27)
#define RKPM_CTR_ALL (0x1<<31)

/*********************CTRBIT DEFINE END*****************************/


//RKPM_IDX_TO_BITS
#define RKPM_OR_2BITS(bit1,bit2) ((RKPM_CTR_##bit1)|(RKPM_CTR_##bit2))
#define RKPM_OR_3BITS(bit1,bit2,bit3) ((RKPM_CTR_##bit1)|RKPM_OR_2BITS(bit2,bit3))
#define RKPM_OR_4BITS(bit1,bit2,bit3,bit4) ((RKPM_CTR_##bit1)|RKPM_OR_3BITS(bit2,bit3,bit4))
#define RKPM_OR_5BITS(bit1,bit2,bit3,bit4,bit5) ((RKPM_CTR_##bit1)|RKPM_OR_4BITS(bit2,bit3,bit4,bit5))

/*********************CTRBIT DEFINE END*****************************/

#define RKPM_GPIOBITS_LEVEL_PULL (24)
#define RKPM_GPIOBITS_MSK_LEVEL_PULL (0xf<<RKPM_GPIOBITS_LEVEL_PULL)

#define RKPM_GPIOBITS_INOUT (20)
#define RKPM_GPIOBITS_MSK_INOUT (0xf<<RKPM_GPIOBITS_INOUT)

#define RKPM_GPIOBITS_PINS (0)
#define RKPM_GPIOBITS_MSK_PINS (0xffff<<RKPM_GPIOBITS_PINS)


#define RKPM_GPIOS_INPUT (0)
#define RKPM_GPIOS_OUTPUT (1)

#define RKPM_GPIOS_OUT_H (1)
#define RKPM_GPIOS_OUT_L (0)

#define RKPM_GPIOS_IN_PULLUP (0)
#define RKPM_GPIOS_IN_PULLDN (0)
#define RKPM_GPIOS_IN_PULLNULL (0)


#define RKPM_GPIOS_SETTING(pins,in_out,level_pull) (\
                                                                        ((pins)&RKPM_GPIOBITS_MSK_PINS)\
                                                                         |(((in_out)<<RKPM_GPIOBITS_INOUT)&RKPM_GPIOBITS_MSK_INOUT)\
                                                                         |(((level_pull)<<RKPM_GPIOBITS_LEVEL_PULL)&RKPM_GPIOBITS_MSK_LEVEL_PULL)\
                                                                        )


#endif
