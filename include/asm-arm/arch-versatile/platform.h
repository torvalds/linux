/*
 * linux/include/asm-arm/arch-versatile/platform.h
 *
 * Copyright (c) ARM Limited 2003.  All rights reserved.
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

#ifndef __address_h
#define __address_h                     1

/*
 * Memory definitions
 */
#define VERSATILE_BOOT_ROM_LO          0x30000000		/* DoC Base (64Mb)...*/
#define VERSATILE_BOOT_ROM_HI          0x30000000
#define VERSATILE_BOOT_ROM_BASE        VERSATILE_BOOT_ROM_HI	 /*  Normal position */
#define VERSATILE_BOOT_ROM_SIZE        SZ_64M

#define VERSATILE_SSRAM_BASE           /* VERSATILE_SSMC_BASE ? */
#define VERSATILE_SSRAM_SIZE           SZ_2M

#define VERSATILE_FLASH_BASE           0x34000000
#define VERSATILE_FLASH_SIZE           SZ_64M

/* 
 *  SDRAM
 */
#define VERSATILE_SDRAM_BASE           0x00000000

/* 
 *  Logic expansion modules
 * 
 */


/* ------------------------------------------------------------------------
 *  Versatile Registers
 * ------------------------------------------------------------------------
 * 
 */
#define VERSATILE_SYS_ID_OFFSET               0x00
#define VERSATILE_SYS_SW_OFFSET               0x04
#define VERSATILE_SYS_LED_OFFSET              0x08
#define VERSATILE_SYS_OSC0_OFFSET             0x0C

#if defined(CONFIG_ARCH_VERSATILE_PB)
#define VERSATILE_SYS_OSC1_OFFSET             0x10
#define VERSATILE_SYS_OSC2_OFFSET             0x14
#define VERSATILE_SYS_OSC3_OFFSET             0x18
#define VERSATILE_SYS_OSC4_OFFSET             0x1C
#elif defined(CONFIG_MACH_VERSATILE_AB)
#define VERSATILE_SYS_OSC1_OFFSET             0x1C
#endif

#define VERSATILE_SYS_LOCK_OFFSET             0x20
#define VERSATILE_SYS_100HZ_OFFSET            0x24
#define VERSATILE_SYS_CFGDATA1_OFFSET         0x28
#define VERSATILE_SYS_CFGDATA2_OFFSET         0x2C
#define VERSATILE_SYS_FLAGS_OFFSET            0x30
#define VERSATILE_SYS_FLAGSSET_OFFSET         0x30
#define VERSATILE_SYS_FLAGSCLR_OFFSET         0x34
#define VERSATILE_SYS_NVFLAGS_OFFSET          0x38
#define VERSATILE_SYS_NVFLAGSSET_OFFSET       0x38
#define VERSATILE_SYS_NVFLAGSCLR_OFFSET       0x3C
#define VERSATILE_SYS_RESETCTL_OFFSET         0x40
#define VERSATILE_SYS_PICCTL_OFFSET           0x44
#define VERSATILE_SYS_MCI_OFFSET              0x48
#define VERSATILE_SYS_FLASH_OFFSET            0x4C
#define VERSATILE_SYS_CLCD_OFFSET             0x50
#define VERSATILE_SYS_CLCDSER_OFFSET          0x54
#define VERSATILE_SYS_BOOTCS_OFFSET           0x58
#define VERSATILE_SYS_24MHz_OFFSET            0x5C
#define VERSATILE_SYS_MISC_OFFSET             0x60
#define VERSATILE_SYS_TEST_OSC0_OFFSET        0x80
#define VERSATILE_SYS_TEST_OSC1_OFFSET        0x84
#define VERSATILE_SYS_TEST_OSC2_OFFSET        0x88
#define VERSATILE_SYS_TEST_OSC3_OFFSET        0x8C
#define VERSATILE_SYS_TEST_OSC4_OFFSET        0x90

#define VERSATILE_SYS_BASE                    0x10000000
#define VERSATILE_SYS_ID                      (VERSATILE_SYS_BASE + VERSATILE_SYS_ID_OFFSET)
#define VERSATILE_SYS_SW                      (VERSATILE_SYS_BASE + VERSATILE_SYS_SW_OFFSET)
#define VERSATILE_SYS_LED                     (VERSATILE_SYS_BASE + VERSATILE_SYS_LED_OFFSET)
#define VERSATILE_SYS_OSC0                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC0_OFFSET)
#define VERSATILE_SYS_OSC1                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC1_OFFSET)

#if defined(CONFIG_ARCH_VERSATILE_PB)
#define VERSATILE_SYS_OSC2                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC2_OFFSET)
#define VERSATILE_SYS_OSC3                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC3_OFFSET)
#define VERSATILE_SYS_OSC4                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC4_OFFSET)
#endif

#define VERSATILE_SYS_LOCK                    (VERSATILE_SYS_BASE + VERSATILE_SYS_LOCK_OFFSET)
#define VERSATILE_SYS_100HZ                   (VERSATILE_SYS_BASE + VERSATILE_SYS_100HZ_OFFSET)
#define VERSATILE_SYS_CFGDATA1                (VERSATILE_SYS_BASE + VERSATILE_SYS_CFGDATA1_OFFSET)
#define VERSATILE_SYS_CFGDATA2                (VERSATILE_SYS_BASE + VERSATILE_SYS_CFGDATA2_OFFSET)
#define VERSATILE_SYS_FLAGS                   (VERSATILE_SYS_BASE + VERSATILE_SYS_FLAGS_OFFSET)
#define VERSATILE_SYS_FLAGSSET                (VERSATILE_SYS_BASE + VERSATILE_SYS_FLAGSSET_OFFSET)
#define VERSATILE_SYS_FLAGSCLR                (VERSATILE_SYS_BASE + VERSATILE_SYS_FLAGSCLR_OFFSET)
#define VERSATILE_SYS_NVFLAGS                 (VERSATILE_SYS_BASE + VERSATILE_SYS_NVFLAGS_OFFSET)
#define VERSATILE_SYS_NVFLAGSSET              (VERSATILE_SYS_BASE + VERSATILE_SYS_NVFLAGSSET_OFFSET)
#define VERSATILE_SYS_NVFLAGSCLR              (VERSATILE_SYS_BASE + VERSATILE_SYS_NVFLAGSCLR_OFFSET)
#define VERSATILE_SYS_RESETCTL                (VERSATILE_SYS_BASE + VERSATILE_SYS_RESETCTL_OFFSET)
#define VERSATILE_SYS_PICCTL                  (VERSATILE_SYS_BASE + VERSATILE_SYS_PICCTL_OFFSET)
#define VERSATILE_SYS_MCI                     (VERSATILE_SYS_BASE + VERSATILE_SYS_MCI_OFFSET)
#define VERSATILE_SYS_FLASH                   (VERSATILE_SYS_BASE + VERSATILE_SYS_FLASH_OFFSET)
#define VERSATILE_SYS_CLCD                    (VERSATILE_SYS_BASE + VERSATILE_SYS_CLCD_OFFSET)
#define VERSATILE_SYS_CLCDSER                 (VERSATILE_SYS_BASE + VERSATILE_SYS_CLCDSER_OFFSET)
#define VERSATILE_SYS_BOOTCS                  (VERSATILE_SYS_BASE + VERSATILE_SYS_BOOTCS_OFFSET)
#define VERSATILE_SYS_24MHz                   (VERSATILE_SYS_BASE + VERSATILE_SYS_24MHz_OFFSET)
#define VERSATILE_SYS_MISC                    (VERSATILE_SYS_BASE + VERSATILE_SYS_MISC_OFFSET)
#define VERSATILE_SYS_TEST_OSC0               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC0_OFFSET)
#define VERSATILE_SYS_TEST_OSC1               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC1_OFFSET)
#define VERSATILE_SYS_TEST_OSC2               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC2_OFFSET)
#define VERSATILE_SYS_TEST_OSC3               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC3_OFFSET)
#define VERSATILE_SYS_TEST_OSC4               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC4_OFFSET)

/* 
 * Values for VERSATILE_SYS_RESET_CTRL
 */
#define VERSATILE_SYS_CTRL_RESET_CONFIGCLR    0x01
#define VERSATILE_SYS_CTRL_RESET_CONFIGINIT   0x02
#define VERSATILE_SYS_CTRL_RESET_DLLRESET     0x03
#define VERSATILE_SYS_CTRL_RESET_PLLRESET     0x04
#define VERSATILE_SYS_CTRL_RESET_POR          0x05
#define VERSATILE_SYS_CTRL_RESET_DoC          0x06

#define VERSATILE_SYS_CTRL_LED         (1 << 0)


/* ------------------------------------------------------------------------
 *  Versatile control registers
 * ------------------------------------------------------------------------
 */

/* 
 * VERSATILE_IDFIELD
 *
 * 31:24 = manufacturer (0x41 = ARM)
 * 23:16 = architecture (0x08 = AHB system bus, ASB processor bus)
 * 15:12 = FPGA (0x3 = XVC600 or XVC600E)
 * 11:4  = build value
 * 3:0   = revision number (0x1 = rev B (AHB))
 */

/*
 * VERSATILE_SYS_LOCK
 *     control access to SYS_OSCx, SYS_CFGDATAx, SYS_RESETCTL, 
 *     SYS_CLD, SYS_BOOTCS
 */
#define VERSATILE_SYS_LOCK_LOCKED    (1 << 16)
#define VERSATILE_SYS_LOCKVAL_MASK	0xFFFF		/* write 0xA05F to enable write access */

/*
 * VERSATILE_SYS_FLASH
 */
#define VERSATILE_FLASHPROG_FLVPPEN	(1 << 0)	/* Enable writing to flash */

/*
 * VERSATILE_INTREG
 *     - used to acknowledge and control MMCI and UART interrupts 
 */
#define VERSATILE_INTREG_WPROT        0x00    /* MMC protection status (no interrupt generated) */
#define VERSATILE_INTREG_RI0          0x01    /* Ring indicator UART0 is asserted,              */
#define VERSATILE_INTREG_CARDIN       0x08    /* MMCI card in detect                            */
                                                /* write 1 to acknowledge and clear               */
#define VERSATILE_INTREG_RI1          0x02    /* Ring indicator UART1 is asserted,              */
#define VERSATILE_INTREG_CARDINSERT   0x03    /* Signal insertion of MMC card                   */

/*
 * VERSATILE peripheral addresses
 */
#define VERSATILE_PCI_CORE_BASE        0x10001000	/* PCI core control */
#define VERSATILE_I2C_BASE             0x10002000	/* I2C control */
#define VERSATILE_SIC_BASE             0x10003000	/* Secondary interrupt controller */
#define VERSATILE_AACI_BASE            0x10004000	/* Audio */
#define VERSATILE_MMCI0_BASE           0x10005000	/* MMC interface */
#define VERSATILE_KMI0_BASE            0x10006000	/* KMI interface */
#define VERSATILE_KMI1_BASE            0x10007000	/* KMI 2nd interface */
#define VERSATILE_CHAR_LCD_BASE        0x10008000	/* Character LCD */
#define VERSATILE_UART3_BASE           0x10009000	/* UART 3 */
#define VERSATILE_SCI1_BASE            0x1000A000
#define VERSATILE_MMCI1_BASE           0x1000B000    /* MMC Interface */
	/* 0x1000C000 - 0x1000CFFF = reserved */
#define VERSATILE_ETH_BASE             0x10010000	/* Ethernet */
#define VERSATILE_USB_BASE             0x10020000	/* USB */
	/* 0x10030000 - 0x100FFFFF = reserved */
#define VERSATILE_SMC_BASE             0x10100000	/* SMC */
#define VERSATILE_MPMC_BASE            0x10110000	/* MPMC */
#define VERSATILE_CLCD_BASE            0x10120000	/* CLCD */
#define VERSATILE_DMAC_BASE            0x10130000	/* DMA controller */
#define VERSATILE_VIC_BASE             0x10140000	/* Vectored interrupt controller */
#define VERSATILE_PERIPH_BASE          0x10150000    /* off-chip peripherals alias from */
                                                /* 0x10000000 - 0x100FFFFF */
#define VERSATILE_AHBM_BASE            0x101D0000	/* AHB monitor */
#define VERSATILE_SCTL_BASE            0x101E0000	/* System controller */
#define VERSATILE_WATCHDOG_BASE        0x101E1000	/* Watchdog */
#define VERSATILE_TIMER0_1_BASE        0x101E2000	/* Timer 0 and 1 */
#define VERSATILE_TIMER2_3_BASE        0x101E3000	/* Timer 2 and 3 */
#define VERSATILE_GPIO0_BASE           0x101E4000	/* GPIO port 0 */
#define VERSATILE_GPIO1_BASE           0x101E5000    /* GPIO port 1 */
#define VERSATILE_GPIO2_BASE           0x101E6000	/* GPIO port 2 */
#define VERSATILE_GPIO3_BASE           0x101E7000	/* GPIO port 3 */
#define VERSATILE_RTC_BASE             0x101E8000	/* Real Time Clock */
	/* 0x101E9000 - reserved */
#define VERSATILE_SCI_BASE             0x101F0000	/* Smart card controller */
#define VERSATILE_UART0_BASE           0x101F1000	/* Uart 0 */
#define VERSATILE_UART1_BASE           0x101F2000	/* Uart 1 */
#define VERSATILE_UART2_BASE           0x101F3000	/* Uart 2 */
#define VERSATILE_SSP_BASE             0x101F4000	/* Synchronous Serial Port */

#define VERSATILE_SSMC_BASE            0x20000000	/* SSMC */
#define VERSATILE_IB2_BASE             0x24000000	/* IB2 module */
#define VERSATILE_MBX_BASE             0x40000000	/* MBX */
#define VERSATILE_PCI_BASE             0x41000000	/* PCI Interface */
#define VERSATILE_SDRAM67_BASE         0x70000000	/* SDRAM banks 6 and 7 */
#define VERSATILE_LT_BASE              0x80000000	/* Logic Tile expansion */

/*
 * Disk on Chip
 */
#define VERSATILE_DOC_BASE             0x2C000000
#define VERSATILE_DOC_SIZE             (16 << 20)
#define VERSATILE_DOC_PAGE_SIZE        512
#define VERSATILE_DOC_TOTAL_PAGES     (DOC_SIZE / PAGE_SIZE)

#define ERASE_UNIT_PAGES    32
#define START_PAGE          0x80

/* 
 *  LED settings, bits [7:0]
 */
#define VERSATILE_SYS_LED0             (1 << 0)
#define VERSATILE_SYS_LED1             (1 << 1)
#define VERSATILE_SYS_LED2             (1 << 2)
#define VERSATILE_SYS_LED3             (1 << 3)
#define VERSATILE_SYS_LED4             (1 << 4)
#define VERSATILE_SYS_LED5             (1 << 5)
#define VERSATILE_SYS_LED6             (1 << 6)
#define VERSATILE_SYS_LED7             (1 << 7)

#define ALL_LEDS                  0xFF

#define LED_BANK                  VERSATILE_SYS_LED

/* 
 * Control registers
 */
#define VERSATILE_IDFIELD_OFFSET	0x0	/* Versatile build information */
#define VERSATILE_FLASHPROG_OFFSET	0x4	/* Flash devices */
#define VERSATILE_INTREG_OFFSET		0x8	/* Interrupt control */
#define VERSATILE_DECODE_OFFSET		0xC	/* Fitted logic modules */


/* ------------------------------------------------------------------------
 *  Versatile Interrupt Controller - control registers
 * ------------------------------------------------------------------------
 * 
 *  Offsets from interrupt controller base 
 * 
 *  System Controller interrupt controller base is
 * 
 * 	VERSATILE_IC_BASE
 * 
 *  Core Module interrupt controller base is
 * 
 * 	VERSATILE_SYS_IC 
 * 
 */
#define VIC_IRQ_STATUS                  0
#define VIC_FIQ_STATUS                  0x04
#define VIC_IRQ_RAW_STATUS              0x08
#define VIC_INT_SELECT                  0x0C	/* 1 = FIQ, 0 = IRQ */
#define VIC_IRQ_ENABLE                  0x10	/* 1 = enable, 0 = disable */
#define VIC_IRQ_ENABLE_CLEAR            0x14
#define VIC_IRQ_SOFT                    0x18
#define VIC_IRQ_SOFT_CLEAR              0x1C
#define VIC_PROTECT                     0x20
#define VIC_VECT_ADDR                   0x30
#define VIC_DEF_VECT_ADDR               0x34
#define VIC_VECT_ADDR0                  0x100	/* 0 to 15 */
#define VIC_VECT_CNTL0                  0x200	/* 0 to 15 */
#define VIC_ITCR                        0x300   /* VIC test control register */

#define VIC_FIQ_RAW_STATUS              0x08
#define VIC_FIQ_ENABLE                  0x10	/* 1 = enable, 0 = disable */
#define VIC_FIQ_ENABLE_CLEAR            0x14
#define VIC_FIQ_SOFT                    0x18
#define VIC_FIQ_SOFT_CLEAR              0x1C

#define SIC_IRQ_STATUS                  0
#define SIC_IRQ_RAW_STATUS              0x04
#define SIC_IRQ_ENABLE                  0x08
#define SIC_IRQ_ENABLE_SET              0x08
#define SIC_IRQ_ENABLE_CLEAR            0x0C
#define SIC_INT_SOFT_SET                0x10
#define SIC_INT_SOFT_CLEAR              0x14
#define SIC_INT_PIC_ENABLE              0x20	/* read status of pass through mask */
#define SIC_INT_PIC_ENABLES             0x20	/* set interrupt pass through bits */
#define SIC_INT_PIC_ENABLEC             0x24	/* Clear interrupt pass through bits */

#define VICVectCntl_Enable		(1 << 5)

/* ------------------------------------------------------------------------
 *  Interrupts - bit assignment (primary)
 * ------------------------------------------------------------------------
 */

#define INT_WDOGINT                     0	/* Watchdog timer */
#define INT_SOFTINT                     1	/* Software interrupt */
#define INT_COMMRx                      2	/* Debug Comm Rx interrupt */
#define INT_COMMTx                      3	/* Debug Comm Tx interrupt */
#define INT_TIMERINT0_1                 4	/* Timer 0 and 1 */
#define INT_TIMERINT2_3                 5	/* Timer 2 and 3 */
#define INT_GPIOINT0                    6	/* GPIO 0 */
#define INT_GPIOINT1                    7	/* GPIO 1 */
#define INT_GPIOINT2                    8	/* GPIO 2 */
#define INT_GPIOINT3                    9	/* GPIO 3 */
#define INT_RTCINT                      10	/* Real Time Clock */
#define INT_SSPINT                      11	/* Synchronous Serial Port */
#define INT_UARTINT0                    12	/* UART 0 on development chip */
#define INT_UARTINT1                    13	/* UART 1 on development chip */
#define INT_UARTINT2                    14	/* UART 2 on development chip */
#define INT_SCIINT                      15	/* Smart Card Interface */
#define INT_CLCDINT                     16	/* CLCD controller */
#define INT_DMAINT                      17	/* DMA controller */
#define INT_PWRFAILINT                  18	/* Power failure */
#define INT_MBXINT                      19	/* Graphics processor */
#define INT_GNDINT                      20	/* Reserved */
	/* External interrupt signals from logic tiles or secondary controller */
#define INT_VICSOURCE21                 21	/* Disk on Chip */
#define INT_VICSOURCE22                 22	/* MCI0A */
#define INT_VICSOURCE23                 23	/* MCI1A */
#define INT_VICSOURCE24                 24	/* AACI */
#define INT_VICSOURCE25                 25	/* Ethernet */
#define INT_VICSOURCE26                 26	/* USB */
#define INT_VICSOURCE27                 27	/* PCI 0 */
#define INT_VICSOURCE28                 28	/* PCI 1 */
#define INT_VICSOURCE29                 29	/* PCI 2 */
#define INT_VICSOURCE30                 30	/* PCI 3 */
#define INT_VICSOURCE31                 31	/* SIC source */

/* 
 *  Interrupt bit positions
 * 
 */
#define INTMASK_WDOGINT                 (1 << INT_WDOGINT)
#define INTMASK_SOFTINT                 (1 << INT_SOFTINT)
#define INTMASK_COMMRx                  (1 << INT_COMMRx)
#define INTMASK_COMMTx                  (1 << INT_COMMTx)
#define INTMASK_TIMERINT0_1             (1 << INT_TIMERINT0_1)
#define INTMASK_TIMERINT2_3             (1 << INT_TIMERINT2_3)
#define INTMASK_GPIOINT0                (1 << INT_GPIOINT0)
#define INTMASK_GPIOINT1                (1 << INT_GPIOINT1)
#define INTMASK_GPIOINT2                (1 << INT_GPIOINT2)
#define INTMASK_GPIOINT3                (1 << INT_GPIOINT3)
#define INTMASK_RTCINT                  (1 << INT_RTCINT)
#define INTMASK_SSPINT                  (1 << INT_SSPINT)
#define INTMASK_UARTINT0                (1 << INT_UARTINT0)
#define INTMASK_UARTINT1                (1 << INT_UARTINT1)
#define INTMASK_UARTINT2                (1 << INT_UARTINT2)
#define INTMASK_SCIINT                  (1 << INT_SCIINT)
#define INTMASK_CLCDINT                 (1 << INT_CLCDINT)
#define INTMASK_DMAINT                  (1 << INT_DMAINT)
#define INTMASK_PWRFAILINT              (1 << INT_PWRFAILINT)
#define INTMASK_MBXINT                  (1 << INT_MBXINT)
#define INTMASK_GNDINT                  (1 << INT_GNDINT)
#define INTMASK_VICSOURCE21             (1 << INT_VICSOURCE21)
#define INTMASK_VICSOURCE22             (1 << INT_VICSOURCE22)
#define INTMASK_VICSOURCE23             (1 << INT_VICSOURCE23)
#define INTMASK_VICSOURCE24             (1 << INT_VICSOURCE24)
#define INTMASK_VICSOURCE25             (1 << INT_VICSOURCE25)
#define INTMASK_VICSOURCE26             (1 << INT_VICSOURCE26)
#define INTMASK_VICSOURCE27             (1 << INT_VICSOURCE27)
#define INTMASK_VICSOURCE28             (1 << INT_VICSOURCE28)
#define INTMASK_VICSOURCE29             (1 << INT_VICSOURCE29)
#define INTMASK_VICSOURCE30             (1 << INT_VICSOURCE30)
#define INTMASK_VICSOURCE31             (1 << INT_VICSOURCE31)


#define VERSATILE_SC_VALID_INT               0x003FFFFF

#define MAXIRQNUM                       31
#define MAXFIQNUM                       31
#define MAXSWINUM                       31

/* ------------------------------------------------------------------------
 *  Interrupts - bit assignment (secondary)
 * ------------------------------------------------------------------------
 */
#define SIC_INT_MMCI0B                  1	/* Multimedia Card 0B */
#define SIC_INT_MMCI1B                  2	/* Multimedia Card 1B */
#define SIC_INT_KMI0                    3	/* Keyboard/Mouse port 0 */
#define SIC_INT_KMI1                    4	/* Keyboard/Mouse port 1 */
#define SIC_INT_SCI3                    5	/* Smart Card interface */
#define SIC_INT_UART3                   6	/* UART 3 empty or data available */
#define SIC_INT_CLCD                    7	/* Character LCD */
#define SIC_INT_TOUCH                   8	/* Touchscreen */
#define SIC_INT_KEYPAD                  9	/* Key pressed on display keypad */
	/* 10:20 - reserved */
#define SIC_INT_DoC                     21	/* Disk on Chip memory controller */
#define SIC_INT_MMCI0A                  22	/* MMC 0A */
#define SIC_INT_MMCI1A                  23	/* MMC 1A */
#define SIC_INT_AACI                    24	/* Audio Codec */
#define SIC_INT_ETH                     25	/* Ethernet controller */
#define SIC_INT_USB                     26	/* USB controller */
#define SIC_INT_PCI0                    27
#define SIC_INT_PCI1                    28
#define SIC_INT_PCI2                    29
#define SIC_INT_PCI3                    30


#define SIC_INTMASK_MMCI0B              (1 << SIC_INT_MMCI0B)
#define SIC_INTMASK_MMCI1B              (1 << SIC_INT_MMCI1B)
#define SIC_INTMASK_KMI0                (1 << SIC_INT_KMI0)
#define SIC_INTMASK_KMI1                (1 << SIC_INT_KMI1)
#define SIC_INTMASK_SCI3                (1 << SIC_INT_SCI3)
#define SIC_INTMASK_UART3               (1 << SIC_INT_UART3)
#define SIC_INTMASK_CLCD                (1 << SIC_INT_CLCD)
#define SIC_INTMASK_TOUCH               (1 << SIC_INT_TOUCH)
#define SIC_INTMASK_KEYPAD              (1 << SIC_INT_KEYPAD)
#define SIC_INTMASK_DoC                 (1 << SIC_INT_DoC)
#define SIC_INTMASK_MMCI0A              (1 << SIC_INT_MMCI0A)
#define SIC_INTMASK_MMCI1A              (1 << SIC_INT_MMCI1A)
#define SIC_INTMASK_AACI                (1 << SIC_INT_AACI)
#define SIC_INTMASK_ETH                 (1 << SIC_INT_ETH)
#define SIC_INTMASK_USB                 (1 << SIC_INT_USB)
#define SIC_INTMASK_PCI0                (1 << SIC_INT_PCI0)
#define SIC_INTMASK_PCI1                (1 << SIC_INT_PCI1)
#define SIC_INTMASK_PCI2                (1 << SIC_INT_PCI2)
#define SIC_INTMASK_PCI3                (1 << SIC_INT_PCI3)
/* 
 *  Application Flash
 * 
 */
#define FLASH_BASE                      VERSATILE_FLASH_BASE
#define FLASH_SIZE                      VERSATILE_FLASH_SIZE
#define FLASH_END                       (FLASH_BASE + FLASH_SIZE - 1)
#define FLASH_BLOCK_SIZE                SZ_128K

/* 
 *  Boot Flash
 * 
 */
#define EPROM_BASE                      VERSATILE_BOOT_ROM_HI
#define EPROM_SIZE                      VERSATILE_BOOT_ROM_SIZE
#define EPROM_END                       (EPROM_BASE + EPROM_SIZE - 1)

/* 
 *  Clean base - dummy
 * 
 */
#define CLEAN_BASE                      EPROM_BASE

/*
 * System controller bit assignment
 */
#define VERSATILE_REFCLK	0
#define VERSATILE_TIMCLK	1

#define VERSATILE_TIMER1_EnSel	15
#define VERSATILE_TIMER2_EnSel	17
#define VERSATILE_TIMER3_EnSel	19
#define VERSATILE_TIMER4_EnSel	21


#define MAX_TIMER                       2
#define MAX_PERIOD                      699050
#define TICKS_PER_uSEC                  1

/* 
 *  These are useconds NOT ticks.  
 * 
 */
#define mSEC_1                          1000
#define mSEC_5                          (mSEC_1 * 5)
#define mSEC_10                         (mSEC_1 * 10)
#define mSEC_25                         (mSEC_1 * 25)
#define SEC_1                           (mSEC_1 * 1000)

#define VERSATILE_CSR_BASE             0x10000000
#define VERSATILE_CSR_SIZE             0x10000000

#ifdef CONFIG_MACH_VERSATILE_AB
/*
 * IB2 Versatile/AB expansion board definitions
 */
#define VERSATILE_IB2_CAMERA_BANK	0x24000000
#define VERSATILE_IB2_KBD_DATAREG	0x25000000
#define VERSATILE_IB2_IER		0x26000000	/* for VICINTSOURCE27 */
#define VERSATILE_IB2_CTRL		0x27000000
#define VERSATILE_IB2_STAT		0x27000004
#endif

#endif

/* 	END */
