/******************************************************************/
/*   Copyright (C) 2008 ROCK-CHIPS FUZHOU . All Rights Reserved.  */
/*******************************************************************
File    :  gpio.h
Desc    :  定义gpio的寄存器结构体\寄存器位的宏定义\接口函数

Author  : 
Date    : 2008-11-20
Modified:
Revision:           1.00
$Log: gpio.h,v $
*********************************************************************/
#ifndef _DRIVER_TCA6424_GPIO_H_
#define _DRIVER_TCA6424_GPIO_H_

#define TCA6424_PortNum 	3
#define TCA6424_PortPinNum 	8
#define TCA6424_Port0PinNum 8
#define TCA6424_Port1PinNum 8
#define TCA6424_Port2PinNum 8
#define EXTGPIO_OUTPUT 	0
#define EXTGPIO_INPUT	1

#define TCA6424_TotalPortPinNum (TCA6424_Port0PinNum+TCA6424_Port1PinNum+TCA6424_Port2PinNum)

#define TCA6424_InputLevel_Reg 	0x0   	//r only
#define TCA6424_OutputLevel_Reg 0x4 	// r/w  default ffff
#define TCA6424_Invert_Reg 		0x8  	// r/w  default 0
#define TCA6424_Config_Reg 		0x0c 	// r/w  default ffff


#define TCA6424_Auto_InputLevel_Reg 	0x80  
#define TCA6424_Auto_OutputLevel_Reg 	0x84
#define TCA6424_Auto_Invert_Reg 		0x88
#define TCA6424_Auto_Config_Reg 		0x8c

#define TCA6424_OUTREGLOCK
#define TCA6424_INPUTREGLOCK
#define TCA6424_CONFIGREGLOCK

#define tca6424getbit(a,num) (((a)>>(num))&0x01)
#define tca6424setbit(a,num) ((a)|(0x01<<(num)))
#define tca6424clearbit(a,num) ((a)&(~(0x01<<(num))))

#define TCA6424_I2C_RATE 400*1000

#endif
