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
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)			(1 << (nr))
#endif

#define RKPM_BITS_W_MSK(bits_shift, msk)	((msk) << ((bits_shift) + 16))

#define RKPM_BITS_CLR_VAL(val, bits_shift, msk) (val&~(msk<<bits_shift))

#define RKPM_SETBITS(bits, bits_shift, msk)	(((bits)&(msk)) << (bits_shift))

#define RKPM_W_MSK_SETBITS(bits, bits_shift, msk) \
    (RKPM_BITS_W_MSK(bits_shift, msk) | RKPM_SETBITS(bits, bits_shift, msk))

#define RKPM_VAL_SETBITS(val,bits, bits_shift, msk) \
    (RKPM_BITS_CLR_VAL(val,bits_shift, msk) | RKPM_SETBITS(bits, bits_shift, msk))

#define RKPM_GETBITS(val, bits_shift, msk)	(((val)>> (bits_shift))&(msk))

/*********************CTRBIT DEFINE*****************************/
#define RKPM_CTR_PWR_DMNS BIT(0)
#define RKPM_CTR_GTCLKS BIT(1)
#define RKPM_CTR_PLLS BIT(2)
#define RKPM_CTR_VOLTS BIT(3)
#define RKPM_CTR_GPIOS BIT(4)
#define RKPM_CTR_DDR BIT(5)
#define RKPM_CTR_PMIC BIT(6)

/*************************************************************
*sys_clk 24M div , it is mutex for 
**RKPM_CTR_SYSCLK_DIV and RKPM_CTR_SYSCLK_32K and RKPM_CTR_SYSCLK_OSC_DIS.
**you can select only one
************************************************************/
#define RKPM_CTR_SYSCLK_DIV BIT(7)// system clk is 24M,and div to min
#define RKPM_CTR_SYSCLK_32K BIT(8)// switch sysclk to 32k,need hardwart susport. and div to min
#define RKPM_CTR_SYSCLK_OSC_DIS BIT(9) // switch sysclk to 32k,disable 24M OSC,need hardwart susport. and div to min

//Low Power Function Selection
#define RKPM_CTR_IDLESRAM_MD BIT(16) // ddr reslf by soft // nor low power
#define RKPM_CTR_IDLEAUTO_MD BIT(17) // ddr reslf by soc // nor low power
#define RKPM_CTR_ARMDP_LPMD BIT(18) // arm enter deep sleep,not off power supply  
#define RKPM_CTR_ARMOFF_LPMD BIT(19) // arm enter deep sleep,off power supply
#define RKPM_CTR_ARMLOGDP_LPMD BIT(20)// arm and logic enter deep sleep,both is not off power supply  
#define RKPM_CTR_ARMOFF_LOGDP_LPMD BIT(21)// arm off ,logic enter deep sleep,but it is not off power supply  
#define RKPM_CTR_ARMLOGOFF_DLPMD BIT(22) //// arm and logic enter deep sleep,both is off power supply  
    
        //debug ctrl Selection
#define RKPM_CTR_RET_DIRT BIT(24)
#define RKPM_CTR_SRAM_NO_WFI BIT(26)
#define RKPM_CTR_WAKE_UP_KEY BIT(27)
#define RKPM_CTR_ALL BIT(31)

/*********************CTRBIT DEFINE END*****************************/


//RKPM_IDX_TO_BITS
#define RKPM_OR_2BITS(bit1,bit2) ((RKPM_CTR_##bit1)|(RKPM_CTR_##bit2))
#define RKPM_OR_3BITS(bit1,bit2,bit3) ((RKPM_CTR_##bit1)|RKPM_OR_2BITS(bit2,bit3))
#define RKPM_OR_4BITS(bit1,bit2,bit3,bit4) ((RKPM_CTR_##bit1)|RKPM_OR_3BITS(bit2,bit3,bit4))
#define RKPM_OR_5BITS(bit1,bit2,bit3,bit4,bit5) ((RKPM_CTR_##bit1)|RKPM_OR_4BITS(bit2,bit3,bit4,bit5))

/*********************CTRBIT DEFINE END*****************************/

//pin=0x0a21  gpio0a2,port=0,bank=a,b_gpio=2,fun=1
#define RKPM_PINBITS_BGPIO(pins) RKPM_GETBITS(pins,4,0xf)
#define RKPM_PINBITS_BANK(pins) RKPM_GETBITS(pins,8,0xf) // a,b,c,d
#define RKPM_PINBITS_PORT(pins) RKPM_GETBITS(pins,0xc,0xf)
#define RKPM_PINBITS_FUN(pins) RKPM_GETBITS(pins,0,0xf)

//#define RKPM_PINBITS(port,bank,bgpio,fun) 0

#if 1
#define RKPM_PINBITS(port,bank,bgpio,fun) (0\
                                                                            |RKPM_SETBITS(fun,0,0xf)\
                                                                            |RKPM_SETBITS(bgpio,0x4,0xf)\
                                                                            |RKPM_SETBITS(bank,0x8,0xf)\
                                                                            |RKPM_SETBITS(port,0xc,0xf)\
                                                                            )
#endif
#define RKPM_PINBITS_SET_FUN(pins,fun) RKPM_VAL_SETBITS(pins,fun,0,0xf)

#define RKPM_PINGPIO_PIN (0)
#define RKPM_PINGPIO_PIN_MSK (0xffff)

#define RKPM_PINGPIO_PULL (24)
#define RKPM_PINGPIO_PULL_MSK (0x3)

#define RKPM_PINGPIO_INOUT (26)
#define RKPM_PINGPIO_INOUT_MSK (0x3)

#define RKPM_PINGPIO_LEVEL (28)
#define RKPM_PINGPIO_LEVEL_MSK (0x3)

#define RKPM_GPIO_INPUT (0)
#define RKPM_GPIO_OUTPUT (1)

#define RKPM_GPIO_OUT_L (0)
#define RKPM_GPIO_OUT_H (1)

#define RKPM_GPIO_PULL_Z (0)
#define RKPM_GPIO_PULL_UP (0x1)
#define RKPM_GPIO_PULL_DN (0x2)
#define RKPM_GPIO_PULL_RPTR (0x3)

#define RKPM_PINGPIO_BITS(pin,pull,inout,_level) (0\
                                                                         |RKPM_SETBITS(pin,RKPM_PINGPIO_PIN,RKPM_PINGPIO_PIN_MSK)\
                                                                         |RKPM_SETBITS(inout,RKPM_PINGPIO_INOUT,RKPM_PINGPIO_INOUT_MSK)\
                                                                         |RKPM_SETBITS(pull,RKPM_PINGPIO_PULL,RKPM_PINGPIO_PULL_MSK)\
                                                                         |RKPM_SETBITS(_level,RKPM_PINGPIO_LEVEL,RKPM_PINGPIO_LEVEL_MSK)\
                                                                        )
                                                             
#define RKPM_PINGPIO_BITS_OUTPUT(pin,_level) (0\
                                                                         |RKPM_SETBITS(pin,RKPM_PINGPIO_PIN,RKPM_PINGPIO_PIN_MSK)\
                                                                         |RKPM_SETBITS(RKPM_GPIO_OUTPUT,RKPM_PINGPIO_INOUT,RKPM_PINGPIO_INOUT_MSK)\
                                                                         |RKPM_SETBITS(_level,RKPM_PINGPIO_LEVEL,RKPM_PINGPIO_LEVEL_MSK)\
                                                                        )
                                                                        
#define RKPM_PINGPIO_BITS_INTPUT(pin,pull) (0\
                                                                             |RKPM_SETBITS(pin,RKPM_PINGPIO_PIN,RKPM_PINGPIO_PIN_MSK)\
                                                                             |RKPM_SETBITS(RKPM_GPIO_INPUT,RKPM_PINGPIO_INOUT,RKPM_PINGPIO_INOUT_MSK)\
                                                                             |RKPM_SETBITS(pull,RKPM_PINGPIO_PULL,RKPM_PINGPIO_PULL_MSK)\
                                                                            )
 #define RKPM_PINGPIO_BITS_FUN(pin,pull) (0\
                                                                             |RKPM_SETBITS(pin,RKPM_PINGPIO_PIN,RKPM_PINGPIO_PIN_MSK)\
                                                                             |RKPM_SETBITS(pull,RKPM_PINGPIO_PULL,RKPM_PINGPIO_PULL_MSK)\
                                                                            )    

#define RKPM_PINGPIO_BITS_PIN(bits)  RKPM_GETBITS(bits,RKPM_PINGPIO_PIN,RKPM_PINGPIO_PIN_MSK)
#define RKPM_PINGPIO_BITS_LEVEL(bits) RKPM_GETBITS(bits,RKPM_PINGPIO_LEVEL,RKPM_PINGPIO_INOUT_MSK)  
#define RKPM_PINGPIO_BITS_PULL(bits) RKPM_GETBITS(bits,RKPM_PINGPIO_PULL,RKPM_PINGPIO_PULL_MSK)   
#define RKPM_PINGPIO_BITS_INOUT(bits) RKPM_GETBITS(bits,RKPM_PINGPIO_INOUT,RKPM_PINGPIO_LEVEL_MSK)   


#endif
