/*
 * linux/include/asm-arm/arch-omap/fpga.h
 *
 * Interrupt handler for OMAP-1510 FPGA
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Separated FPGA interrupts from innovator1510.c and cleaned up for 2.6
 * Copyright (C) 2004 Nokia Corporation by Tony Lindrgen <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_OMAP_FPGA_H
#define __ASM_ARCH_OMAP_FPGA_H

#if defined(CONFIG_MACH_OMAP_INNOVATOR) && defined(CONFIG_ARCH_OMAP15XX)
extern void omap1510_fpga_init_irq(void);
#else
#define omap1510_fpga_init_irq()	(0)
#endif

#define fpga_read(reg)			__raw_readb(reg)
#define fpga_write(val, reg)		__raw_writeb(val, reg)

/*
 * ---------------------------------------------------------------------------
 *  H2/P2 Debug board FPGA
 * ---------------------------------------------------------------------------
 */
/* maps in the FPGA registers and the ETHR registers */
#define H2P2_DBG_FPGA_BASE		0xE8000000	/* VA */
#define H2P2_DBG_FPGA_SIZE		SZ_4K		/* SIZE */
#define H2P2_DBG_FPGA_START		0x04000000	/* PA */

#define H2P2_DBG_FPGA_ETHR_START	(H2P2_DBG_FPGA_START + 0x300)
#define H2P2_DBG_FPGA_FPGA_REV		(H2P2_DBG_FPGA_BASE + 0x10)	/* FPGA Revision */
#define H2P2_DBG_FPGA_BOARD_REV		(H2P2_DBG_FPGA_BASE + 0x12)	/* Board Revision */
#define H2P2_DBG_FPGA_GPIO		(H2P2_DBG_FPGA_BASE + 0x14)	/* GPIO outputs */
#define H2P2_DBG_FPGA_LEDS		(H2P2_DBG_FPGA_BASE + 0x16)	/* LEDs outputs */
#define H2P2_DBG_FPGA_MISC_INPUTS	(H2P2_DBG_FPGA_BASE + 0x18)	/* Misc inputs */
#define H2P2_DBG_FPGA_LAN_STATUS	(H2P2_DBG_FPGA_BASE + 0x1A)	/* LAN Status line */
#define H2P2_DBG_FPGA_LAN_RESET		(H2P2_DBG_FPGA_BASE + 0x1C)	/* LAN Reset line */

/* NOTE:  most boards don't have a static mapping for the FPGA ... */
struct h2p2_dbg_fpga {
	/* offset 0x00 */
	u16		smc91x[8];
	/* offset 0x10 */
	u16		fpga_rev;
	u16		board_rev;
	u16		gpio_outputs;
	u16		leds;
	/* offset 0x18 */
	u16		misc_inputs;
	u16		lan_status;
	u16		lan_reset;
	u16		reserved0;
	/* offset 0x20 */
	u16		ps2_data;
	u16		ps2_ctrl;
	/* plus also 4 rs232 ports ... */
};

/* LEDs definition on debug board (16 LEDs, all physically green) */
#define H2P2_DBG_FPGA_LED_GREEN		(1 << 15)
#define H2P2_DBG_FPGA_LED_AMBER		(1 << 14)
#define H2P2_DBG_FPGA_LED_RED		(1 << 13)
#define H2P2_DBG_FPGA_LED_BLUE		(1 << 12)
/*  cpu0 load-meter LEDs */
#define H2P2_DBG_FPGA_LOAD_METER	(1 << 0)	// A bit of fun on our board ...
#define H2P2_DBG_FPGA_LOAD_METER_SIZE	11
#define H2P2_DBG_FPGA_LOAD_METER_MASK	((1 << H2P2_DBG_FPGA_LOAD_METER_SIZE) - 1)

#define H2P2_DBG_FPGA_P2_LED_TIMER		(1 << 0)
#define H2P2_DBG_FPGA_P2_LED_IDLE		(1 << 1)

/*
 * ---------------------------------------------------------------------------
 *  OMAP-1510 FPGA
 * ---------------------------------------------------------------------------
 */
#define OMAP1510_FPGA_BASE			0xE8000000	/* Virtual */
#define OMAP1510_FPGA_SIZE			SZ_4K
#define OMAP1510_FPGA_START			0x08000000	/* Physical */

/* Revision */
#define OMAP1510_FPGA_REV_LOW			(OMAP1510_FPGA_BASE + 0x0)
#define OMAP1510_FPGA_REV_HIGH			(OMAP1510_FPGA_BASE + 0x1)

#define OMAP1510_FPGA_LCD_PANEL_CONTROL		(OMAP1510_FPGA_BASE + 0x2)
#define OMAP1510_FPGA_LED_DIGIT			(OMAP1510_FPGA_BASE + 0x3)
#define INNOVATOR_FPGA_HID_SPI			(OMAP1510_FPGA_BASE + 0x4)
#define OMAP1510_FPGA_POWER			(OMAP1510_FPGA_BASE + 0x5)

/* Interrupt status */
#define OMAP1510_FPGA_ISR_LO			(OMAP1510_FPGA_BASE + 0x6)
#define OMAP1510_FPGA_ISR_HI			(OMAP1510_FPGA_BASE + 0x7)

/* Interrupt mask */
#define OMAP1510_FPGA_IMR_LO			(OMAP1510_FPGA_BASE + 0x8)
#define OMAP1510_FPGA_IMR_HI			(OMAP1510_FPGA_BASE + 0x9)

/* Reset registers */
#define OMAP1510_FPGA_HOST_RESET		(OMAP1510_FPGA_BASE + 0xa)
#define OMAP1510_FPGA_RST			(OMAP1510_FPGA_BASE + 0xb)

#define OMAP1510_FPGA_AUDIO			(OMAP1510_FPGA_BASE + 0xc)
#define OMAP1510_FPGA_DIP			(OMAP1510_FPGA_BASE + 0xe)
#define OMAP1510_FPGA_FPGA_IO			(OMAP1510_FPGA_BASE + 0xf)
#define OMAP1510_FPGA_UART1			(OMAP1510_FPGA_BASE + 0x14)
#define OMAP1510_FPGA_UART2			(OMAP1510_FPGA_BASE + 0x15)
#define OMAP1510_FPGA_OMAP1510_STATUS		(OMAP1510_FPGA_BASE + 0x16)
#define OMAP1510_FPGA_BOARD_REV			(OMAP1510_FPGA_BASE + 0x18)
#define OMAP1510P1_PPT_DATA			(OMAP1510_FPGA_BASE + 0x100)
#define OMAP1510P1_PPT_STATUS			(OMAP1510_FPGA_BASE + 0x101)
#define OMAP1510P1_PPT_CONTROL			(OMAP1510_FPGA_BASE + 0x102)

#define OMAP1510_FPGA_TOUCHSCREEN		(OMAP1510_FPGA_BASE + 0x204)

#define INNOVATOR_FPGA_INFO			(OMAP1510_FPGA_BASE + 0x205)
#define INNOVATOR_FPGA_LCD_BRIGHT_LO		(OMAP1510_FPGA_BASE + 0x206)
#define INNOVATOR_FPGA_LCD_BRIGHT_HI		(OMAP1510_FPGA_BASE + 0x207)
#define INNOVATOR_FPGA_LED_GRN_LO		(OMAP1510_FPGA_BASE + 0x208)
#define INNOVATOR_FPGA_LED_GRN_HI		(OMAP1510_FPGA_BASE + 0x209)
#define INNOVATOR_FPGA_LED_RED_LO		(OMAP1510_FPGA_BASE + 0x20a)
#define INNOVATOR_FPGA_LED_RED_HI		(OMAP1510_FPGA_BASE + 0x20b)
#define INNOVATOR_FPGA_CAM_USB_CONTROL		(OMAP1510_FPGA_BASE + 0x20c)
#define INNOVATOR_FPGA_EXP_CONTROL		(OMAP1510_FPGA_BASE + 0x20d)
#define INNOVATOR_FPGA_ISR2			(OMAP1510_FPGA_BASE + 0x20e)
#define INNOVATOR_FPGA_IMR2			(OMAP1510_FPGA_BASE + 0x210)

#define OMAP1510_FPGA_ETHR_START		(OMAP1510_FPGA_START + 0x300)

/*
 * Power up Giga UART driver, turn on HID clock.
 * Turn off BT power, since we're not using it and it
 * draws power.
 */
#define OMAP1510_FPGA_RESET_VALUE		0x42

#define OMAP1510_FPGA_PCR_IF_PD0		(1 << 7)
#define OMAP1510_FPGA_PCR_COM2_EN		(1 << 6)
#define OMAP1510_FPGA_PCR_COM1_EN		(1 << 5)
#define OMAP1510_FPGA_PCR_EXP_PD0		(1 << 4)
#define OMAP1510_FPGA_PCR_EXP_PD1		(1 << 3)
#define OMAP1510_FPGA_PCR_48MHZ_CLK		(1 << 2)
#define OMAP1510_FPGA_PCR_4MHZ_CLK		(1 << 1)
#define OMAP1510_FPGA_PCR_RSRVD_BIT0		(1 << 0)

/*
 * Innovator/OMAP1510 FPGA HID register bit definitions
 */
#define OMAP1510_FPGA_HID_SCLK	(1<<0)	/* output */
#define OMAP1510_FPGA_HID_MOSI	(1<<1)	/* output */
#define OMAP1510_FPGA_HID_nSS	(1<<2)	/* output 0/1 chip idle/select */
#define OMAP1510_FPGA_HID_nHSUS	(1<<3)	/* output 0/1 host active/suspended */
#define OMAP1510_FPGA_HID_MISO	(1<<4)	/* input */
#define OMAP1510_FPGA_HID_ATN	(1<<5)	/* input  0/1 chip idle/ATN */
#define OMAP1510_FPGA_HID_rsrvd	(1<<6)
#define OMAP1510_FPGA_HID_RESETn (1<<7)	/* output - 0/1 USAR reset/run */

/* The FPGA IRQ is cascaded through GPIO_13 */
#define OMAP1510_INT_FPGA		(IH_GPIO_BASE + 13)

/* IRQ Numbers for interrupts muxed through the FPGA */
#define OMAP1510_IH_FPGA_BASE		IH_BOARD_BASE
#define OMAP1510_INT_FPGA_ATN		(OMAP1510_IH_FPGA_BASE + 0)
#define OMAP1510_INT_FPGA_ACK		(OMAP1510_IH_FPGA_BASE + 1)
#define OMAP1510_INT_FPGA2		(OMAP1510_IH_FPGA_BASE + 2)
#define OMAP1510_INT_FPGA3		(OMAP1510_IH_FPGA_BASE + 3)
#define OMAP1510_INT_FPGA4		(OMAP1510_IH_FPGA_BASE + 4)
#define OMAP1510_INT_FPGA5		(OMAP1510_IH_FPGA_BASE + 5)
#define OMAP1510_INT_FPGA6		(OMAP1510_IH_FPGA_BASE + 6)
#define OMAP1510_INT_FPGA7		(OMAP1510_IH_FPGA_BASE + 7)
#define OMAP1510_INT_FPGA8		(OMAP1510_IH_FPGA_BASE + 8)
#define OMAP1510_INT_FPGA9		(OMAP1510_IH_FPGA_BASE + 9)
#define OMAP1510_INT_FPGA10		(OMAP1510_IH_FPGA_BASE + 10)
#define OMAP1510_INT_FPGA11		(OMAP1510_IH_FPGA_BASE + 11)
#define OMAP1510_INT_FPGA12		(OMAP1510_IH_FPGA_BASE + 12)
#define OMAP1510_INT_ETHER		(OMAP1510_IH_FPGA_BASE + 13)
#define OMAP1510_INT_FPGAUART1		(OMAP1510_IH_FPGA_BASE + 14)
#define OMAP1510_INT_FPGAUART2		(OMAP1510_IH_FPGA_BASE + 15)
#define OMAP1510_INT_FPGA_TS		(OMAP1510_IH_FPGA_BASE + 16)
#define OMAP1510_INT_FPGA17		(OMAP1510_IH_FPGA_BASE + 17)
#define OMAP1510_INT_FPGA_CAM		(OMAP1510_IH_FPGA_BASE + 18)
#define OMAP1510_INT_FPGA_RTC_A		(OMAP1510_IH_FPGA_BASE + 19)
#define OMAP1510_INT_FPGA_RTC_B		(OMAP1510_IH_FPGA_BASE + 20)
#define OMAP1510_INT_FPGA_CD		(OMAP1510_IH_FPGA_BASE + 21)
#define OMAP1510_INT_FPGA22		(OMAP1510_IH_FPGA_BASE + 22)
#define OMAP1510_INT_FPGA23		(OMAP1510_IH_FPGA_BASE + 23)

#endif
