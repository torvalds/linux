/*
 *  linux/include/asm-arm/arch-pxa/mainstone.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 14, 2002
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_ARCH_MAINSTONE_H
#define ASM_ARCH_MAINSTONE_H

#define MST_ETH_PHYS		PXA_CS4_PHYS

#define MST_FPGA_PHYS		PXA_CS2_PHYS
#define MST_FPGA_VIRT		(0xf0000000)
#define MST_P2V(x)		((x) - MST_FPGA_PHYS + MST_FPGA_VIRT)
#define MST_V2P(x)		((x) - MST_FPGA_VIRT + MST_FPGA_PHYS)

#ifndef __ASSEMBLY__
# define __MST_REG(x)		(*((volatile unsigned long *)MST_P2V(x)))
#else
# define __MST_REG(x)		MST_P2V(x)
#endif

/* board level registers in the FPGA */

#define MST_LEDDAT1		__MST_REG(0x08000010)
#define MST_LEDDAT2		__MST_REG(0x08000014)
#define MST_LEDCTRL		__MST_REG(0x08000040)
#define MST_GPSWR		__MST_REG(0x08000060)
#define MST_MSCWR1		__MST_REG(0x08000080)
#define MST_MSCWR2		__MST_REG(0x08000084)
#define MST_MSCWR3		__MST_REG(0x08000088)
#define MST_MSCRD		__MST_REG(0x08000090)
#define MST_INTMSKENA		__MST_REG(0x080000c0)
#define MST_INTSETCLR		__MST_REG(0x080000d0)
#define MST_PCMCIA0		__MST_REG(0x080000e0)
#define MST_PCMCIA1		__MST_REG(0x080000e4)

#define MST_MSCWR1_CAMERA_ON	(1 << 15)  /* Camera interface power control */
#define MST_MSCWR1_CAMERA_SEL	(1 << 14)  /* Camera interface mux control */
#define MST_MSCWR1_LCD_CTL	(1 << 13)  /* General-purpose LCD control */
#define MST_MSCWR1_MS_ON	(1 << 12)  /* Memory Stick power control */
#define MST_MSCWR1_MMC_ON	(1 << 11)  /* MultiMediaCard* power control */
#define MST_MSCWR1_MS_SEL	(1 << 10)  /* SD/MS multiplexer control */
#define MST_MSCWR1_BB_SEL	(1 << 9)   /* PCMCIA/Baseband multiplexer */
#define MST_MSCWR1_BT_ON	(1 << 8)   /* Bluetooth UART transceiver */
#define MST_MSCWR1_BTDTR	(1 << 7)   /* Bluetooth UART DTR */

#define MST_MSCWR1_IRDA_MASK	(3 << 5)   /* IrDA transceiver mode */
#define MST_MSCWR1_IRDA_FULL	(0 << 5)   /* full distance power */
#define MST_MSCWR1_IRDA_OFF	(1 << 5)   /* shutdown */
#define MST_MSCWR1_IRDA_MED	(2 << 5)   /* 2/3 distance power */
#define MST_MSCWR1_IRDA_LOW	(3 << 5)   /* 1/3 distance power */

#define MST_MSCWR1_IRDA_FIR	(1 << 4)   /* IrDA transceiver SIR/FIR */
#define MST_MSCWR1_GREENLED	(1 << 3)   /* LED D1 control */
#define MST_MSCWR1_PDC_CTL	(1 << 2)   /* reserved */
#define MST_MSCWR1_MTR_ON	(1 << 1)   /* Silent alert motor */
#define MST_MSCWR1_SYSRESET	(1 << 0)   /* System reset */

#define MST_MSCWR2_USB_OTG_RST	(1 << 6)   /* USB On The Go reset */
#define MST_MSCWR2_USB_OTG_SEL	(1 << 5)   /* USB On The Go control */
#define MST_MSCWR2_nUSBC_SC	(1 << 4)   /* USB client soft connect control */
#define MST_MSCWR2_I2S_SPKROFF	(1 << 3)   /* I2S CODEC amplifier control */
#define MST_MSCWR2_AC97_SPKROFF	(1 << 2)   /* AC97 CODEC amplifier control */
#define MST_MSCWR2_RADIO_PWR	(1 << 1)   /* Radio module power control */
#define MST_MSCWR2_RADIO_WAKE	(1 << 0)   /* Radio module wake-up signal */

#define MST_MSCWR3_GPIO_RESET_EN	(1 << 2) /* Enable GPIO Reset */
#define MST_MSCWR3_GPIO_RESET		(1 << 1) /* Initiate a GPIO Reset */
#define MST_MSCWR3_COMMS_SW_RESET	(1 << 0) /* Communications Processor Reset Control */

#define MST_MSCRD_nPENIRQ	(1 << 9)   /* ADI7873* nPENIRQ signal */
#define MST_MSCRD_nMEMSTK_CD	(1 << 8)   /* Memory Stick detection signal */
#define MST_MSCRD_nMMC_CD	(1 << 7)   /* SD/MMC card detection signal */
#define MST_MSCRD_nUSIM_CD	(1 << 6)   /* USIM card detection signal */
#define MST_MSCRD_USB_CBL	(1 << 5)   /* USB client cable status */
#define MST_MSCRD_TS_BUSY	(1 << 4)   /* ADI7873 busy */
#define MST_MSCRD_BTDSR		(1 << 3)   /* Bluetooth UART DSR */
#define MST_MSCRD_BTRI		(1 << 2)   /* Bluetooth UART Ring Indicator */
#define MST_MSCRD_BTDCD		(1 << 1)   /* Bluetooth UART DCD */
#define MST_MSCRD_nMMC_WP	(1 << 0)   /* SD/MMC write-protect status */

#define MST_INT_S1_IRQ		(1 << 15)  /* PCMCIA socket 1 IRQ */
#define MST_INT_S1_STSCHG	(1 << 14)  /* PCMCIA socket 1 status changed */
#define MST_INT_S1_CD		(1 << 13)  /* PCMCIA socket 1 card detection */
#define MST_INT_S0_IRQ		(1 << 11)  /* PCMCIA socket 0 IRQ */
#define MST_INT_S0_STSCHG	(1 << 10)  /* PCMCIA socket 0 status changed */
#define MST_INT_S0_CD		(1 << 9)   /* PCMCIA socket 0 card detection */
#define MST_INT_nEXBRD_INT	(1 << 7)   /* Expansion board IRQ */
#define MST_INT_MSINS		(1 << 6)   /* Memory Stick* detection */
#define MST_INT_PENIRQ		(1 << 5)   /* ADI7873* touch-screen IRQ */
#define MST_INT_AC97		(1 << 4)   /* AC'97 CODEC IRQ */
#define MST_INT_ETHERNET	(1 << 3)   /* Ethernet controller IRQ */
#define MST_INT_USBC		(1 << 2)   /* USB client cable detection IRQ */
#define MST_INT_USIM		(1 << 1)   /* USIM card detection IRQ */
#define MST_INT_MMC		(1 << 0)   /* MMC/SD card detection IRQ */

#define MST_PCMCIA_nIRQ		(1 << 10)  /* IRQ / ready signal */
#define MST_PCMCIA_nSPKR_BVD2	(1 << 9)   /* VDD sense / digital speaker */
#define MST_PCMCIA_nSTSCHG_BVD1	(1 << 8)   /* VDD sense / card status changed */
#define MST_PCMCIA_nVS2		(1 << 7)   /* VSS voltage sense */
#define MST_PCMCIA_nVS1		(1 << 6)   /* VSS voltage sense */
#define MST_PCMCIA_nCD		(1 << 5)   /* Card detection signal */
#define MST_PCMCIA_RESET	(1 << 4)   /* Card reset signal */
#define MST_PCMCIA_PWR_MASK	(0x000f)   /* MAX1602 power-supply controls */

#define MST_PCMCIA_PWR_VPP_0    0x0	   /* voltage VPP = 0V */
#define MST_PCMCIA_PWR_VPP_120  0x2 	   /* voltage VPP = 12V*/
#define MST_PCMCIA_PWR_VPP_VCC  0x1	   /* voltage VPP = VCC */
#define MST_PCMCIA_PWR_VCC_0    0x0	   /* voltage VCC = 0V */
#define MST_PCMCIA_PWR_VCC_33   0x8	   /* voltage VCC = 3.3V */
#define MST_PCMCIA_PWR_VCC_50   0x4	   /* voltage VCC = 5.0V */

#endif
