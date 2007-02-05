/*
 * linux/include/asm-arm/arch-realview/platform.h
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
#define REALVIEW_BOOT_ROM_LO          0x30000000		/* DoC Base (64Mb)...*/
#define REALVIEW_BOOT_ROM_HI          0x30000000
#define REALVIEW_BOOT_ROM_BASE        REALVIEW_BOOT_ROM_HI	 /*  Normal position */
#define REALVIEW_BOOT_ROM_SIZE        SZ_64M

#define REALVIEW_SSRAM_BASE           /* REALVIEW_SSMC_BASE ? */
#define REALVIEW_SSRAM_SIZE           SZ_2M

#define REALVIEW_FLASH_BASE           0x40000000
#define REALVIEW_FLASH_SIZE           SZ_64M

/* 
 *  SDRAM
 */
#define REALVIEW_SDRAM_BASE           0x00000000

/* 
 *  Logic expansion modules
 * 
 */


/* ------------------------------------------------------------------------
 *  RealView Registers
 * ------------------------------------------------------------------------
 * 
 */
#define REALVIEW_SYS_ID_OFFSET               0x00
#define REALVIEW_SYS_SW_OFFSET               0x04
#define REALVIEW_SYS_LED_OFFSET              0x08
#define REALVIEW_SYS_OSC0_OFFSET             0x0C

#define REALVIEW_SYS_OSC1_OFFSET             0x10
#define REALVIEW_SYS_OSC2_OFFSET             0x14
#define REALVIEW_SYS_OSC3_OFFSET             0x18
#define REALVIEW_SYS_OSC4_OFFSET             0x1C	/* OSC1 for RealView/AB */

#define REALVIEW_SYS_LOCK_OFFSET             0x20
#define REALVIEW_SYS_100HZ_OFFSET            0x24
#define REALVIEW_SYS_CFGDATA1_OFFSET         0x28
#define REALVIEW_SYS_CFGDATA2_OFFSET         0x2C
#define REALVIEW_SYS_FLAGS_OFFSET            0x30
#define REALVIEW_SYS_FLAGSSET_OFFSET         0x30
#define REALVIEW_SYS_FLAGSCLR_OFFSET         0x34
#define REALVIEW_SYS_NVFLAGS_OFFSET          0x38
#define REALVIEW_SYS_NVFLAGSSET_OFFSET       0x38
#define REALVIEW_SYS_NVFLAGSCLR_OFFSET       0x3C
#define REALVIEW_SYS_RESETCTL_OFFSET         0x40
#define REALVIEW_SYS_PCICTL_OFFSET           0x44
#define REALVIEW_SYS_MCI_OFFSET              0x48
#define REALVIEW_SYS_FLASH_OFFSET            0x4C
#define REALVIEW_SYS_CLCD_OFFSET             0x50
#define REALVIEW_SYS_CLCDSER_OFFSET          0x54
#define REALVIEW_SYS_BOOTCS_OFFSET           0x58
#define REALVIEW_SYS_24MHz_OFFSET            0x5C
#define REALVIEW_SYS_MISC_OFFSET             0x60
#define REALVIEW_SYS_IOSEL_OFFSET            0x70
#define REALVIEW_SYS_TEST_OSC0_OFFSET        0x80
#define REALVIEW_SYS_TEST_OSC1_OFFSET        0x84
#define REALVIEW_SYS_TEST_OSC2_OFFSET        0x88
#define REALVIEW_SYS_TEST_OSC3_OFFSET        0x8C
#define REALVIEW_SYS_TEST_OSC4_OFFSET        0x90

#define REALVIEW_SYS_BASE                    0x10000000
#define REALVIEW_SYS_ID                      (REALVIEW_SYS_BASE + REALVIEW_SYS_ID_OFFSET)
#define REALVIEW_SYS_SW                      (REALVIEW_SYS_BASE + REALVIEW_SYS_SW_OFFSET)
#define REALVIEW_SYS_LED                     (REALVIEW_SYS_BASE + REALVIEW_SYS_LED_OFFSET)
#define REALVIEW_SYS_OSC0                    (REALVIEW_SYS_BASE + REALVIEW_SYS_OSC0_OFFSET)
#define REALVIEW_SYS_OSC1                    (REALVIEW_SYS_BASE + REALVIEW_SYS_OSC1_OFFSET)

#define REALVIEW_SYS_LOCK                    (REALVIEW_SYS_BASE + REALVIEW_SYS_LOCK_OFFSET)
#define REALVIEW_SYS_100HZ                   (REALVIEW_SYS_BASE + REALVIEW_SYS_100HZ_OFFSET)
#define REALVIEW_SYS_CFGDATA1                (REALVIEW_SYS_BASE + REALVIEW_SYS_CFGDATA1_OFFSET)
#define REALVIEW_SYS_CFGDATA2                (REALVIEW_SYS_BASE + REALVIEW_SYS_CFGDATA2_OFFSET)
#define REALVIEW_SYS_FLAGS                   (REALVIEW_SYS_BASE + REALVIEW_SYS_FLAGS_OFFSET)
#define REALVIEW_SYS_FLAGSSET                (REALVIEW_SYS_BASE + REALVIEW_SYS_FLAGSSET_OFFSET)
#define REALVIEW_SYS_FLAGSCLR                (REALVIEW_SYS_BASE + REALVIEW_SYS_FLAGSCLR_OFFSET)
#define REALVIEW_SYS_NVFLAGS                 (REALVIEW_SYS_BASE + REALVIEW_SYS_NVFLAGS_OFFSET)
#define REALVIEW_SYS_NVFLAGSSET              (REALVIEW_SYS_BASE + REALVIEW_SYS_NVFLAGSSET_OFFSET)
#define REALVIEW_SYS_NVFLAGSCLR              (REALVIEW_SYS_BASE + REALVIEW_SYS_NVFLAGSCLR_OFFSET)
#define REALVIEW_SYS_RESETCTL                (REALVIEW_SYS_BASE + REALVIEW_SYS_RESETCTL_OFFSET)
#define REALVIEW_SYS_PCICTL                  (REALVIEW_SYS_BASE + REALVIEW_SYS_PCICTL_OFFSET)
#define REALVIEW_SYS_MCI                     (REALVIEW_SYS_BASE + REALVIEW_SYS_MCI_OFFSET)
#define REALVIEW_SYS_FLASH                   (REALVIEW_SYS_BASE + REALVIEW_SYS_FLASH_OFFSET)
#define REALVIEW_SYS_CLCD                    (REALVIEW_SYS_BASE + REALVIEW_SYS_CLCD_OFFSET)
#define REALVIEW_SYS_CLCDSER                 (REALVIEW_SYS_BASE + REALVIEW_SYS_CLCDSER_OFFSET)
#define REALVIEW_SYS_BOOTCS                  (REALVIEW_SYS_BASE + REALVIEW_SYS_BOOTCS_OFFSET)
#define REALVIEW_SYS_24MHz                   (REALVIEW_SYS_BASE + REALVIEW_SYS_24MHz_OFFSET)
#define REALVIEW_SYS_MISC                    (REALVIEW_SYS_BASE + REALVIEW_SYS_MISC_OFFSET)
#define REALVIEW_SYS_IOSEL                   (REALVIEW_SYS_BASE + REALVIEW_SYS_IOSEL_OFFSET)
#define REALVIEW_SYS_TEST_OSC0               (REALVIEW_SYS_BASE + REALVIEW_SYS_TEST_OSC0_OFFSET)
#define REALVIEW_SYS_TEST_OSC1               (REALVIEW_SYS_BASE + REALVIEW_SYS_TEST_OSC1_OFFSET)
#define REALVIEW_SYS_TEST_OSC2               (REALVIEW_SYS_BASE + REALVIEW_SYS_TEST_OSC2_OFFSET)
#define REALVIEW_SYS_TEST_OSC3               (REALVIEW_SYS_BASE + REALVIEW_SYS_TEST_OSC3_OFFSET)
#define REALVIEW_SYS_TEST_OSC4               (REALVIEW_SYS_BASE + REALVIEW_SYS_TEST_OSC4_OFFSET)

/* 
 * Values for REALVIEW_SYS_RESET_CTRL
 */
#define REALVIEW_SYS_CTRL_RESET_CONFIGCLR    0x01
#define REALVIEW_SYS_CTRL_RESET_CONFIGINIT   0x02
#define REALVIEW_SYS_CTRL_RESET_DLLRESET     0x03
#define REALVIEW_SYS_CTRL_RESET_PLLRESET     0x04
#define REALVIEW_SYS_CTRL_RESET_POR          0x05
#define REALVIEW_SYS_CTRL_RESET_DoC          0x06

#define REALVIEW_SYS_CTRL_LED         (1 << 0)


/* ------------------------------------------------------------------------
 *  RealView control registers
 * ------------------------------------------------------------------------
 */

/* 
 * REALVIEW_IDFIELD
 *
 * 31:24 = manufacturer (0x41 = ARM)
 * 23:16 = architecture (0x08 = AHB system bus, ASB processor bus)
 * 15:12 = FPGA (0x3 = XVC600 or XVC600E)
 * 11:4  = build value
 * 3:0   = revision number (0x1 = rev B (AHB))
 */

/*
 * REALVIEW_SYS_LOCK
 *     control access to SYS_OSCx, SYS_CFGDATAx, SYS_RESETCTL, 
 *     SYS_CLD, SYS_BOOTCS
 */
#define REALVIEW_SYS_LOCK_LOCKED    (1 << 16)
#define REALVIEW_SYS_LOCKVAL_MASK	0xFFFF		/* write 0xA05F to enable write access */

/*
 * REALVIEW_SYS_FLASH
 */
#define REALVIEW_FLASHPROG_FLVPPEN	(1 << 0)	/* Enable writing to flash */

/*
 * REALVIEW_INTREG
 *     - used to acknowledge and control MMCI and UART interrupts 
 */
#define REALVIEW_INTREG_WPROT        0x00    /* MMC protection status (no interrupt generated) */
#define REALVIEW_INTREG_RI0          0x01    /* Ring indicator UART0 is asserted,              */
#define REALVIEW_INTREG_CARDIN       0x08    /* MMCI card in detect                            */
                                                /* write 1 to acknowledge and clear               */
#define REALVIEW_INTREG_RI1          0x02    /* Ring indicator UART1 is asserted,              */
#define REALVIEW_INTREG_CARDINSERT   0x03    /* Signal insertion of MMC card                   */

/*
 * REALVIEW peripheral addresses
 */
#define REALVIEW_SCTL_BASE            0x10001000	/* System controller */
#define REALVIEW_I2C_BASE             0x10002000	/* I2C control */
	/* Reserved 0x10003000 */
#define REALVIEW_AACI_BASE            0x10004000	/* Audio */
#define REALVIEW_MMCI0_BASE           0x10005000	/* MMC interface */
#define REALVIEW_KMI0_BASE            0x10006000	/* KMI interface */
#define REALVIEW_KMI1_BASE            0x10007000	/* KMI 2nd interface */
#define REALVIEW_CHAR_LCD_BASE        0x10008000	/* Character LCD */
#define REALVIEW_UART0_BASE           0x10009000	/* UART 0 */
#define REALVIEW_UART1_BASE           0x1000A000	/* UART 1 */
#define REALVIEW_UART2_BASE           0x1000B000	/* UART 2 */
#define REALVIEW_UART3_BASE           0x1000C000	/* UART 3 */
#define REALVIEW_SSP_BASE             0x1000D000	/* Synchronous Serial Port */
#define REALVIEW_SCI_BASE             0x1000E000	/* Smart card controller */
	/* Reserved 0x1000F000 */
#define REALVIEW_WATCHDOG_BASE        0x10010000	/* watchdog interface */
#define REALVIEW_TIMER0_1_BASE        0x10011000	/* Timer 0 and 1 */
#define REALVIEW_TIMER2_3_BASE        0x10012000	/* Timer 2 and 3 */
#define REALVIEW_GPIO0_BASE           0x10013000	/* GPIO port 0 */
#define REALVIEW_GPIO1_BASE           0x10014000	/* GPIO port 1 */
#define REALVIEW_GPIO2_BASE           0x10015000	/* GPIO port 2 */
	/* Reserved 0x10016000 */
#define REALVIEW_RTC_BASE             0x10017000	/* Real Time Clock */
#define REALVIEW_DMC_BASE             0x10018000	/* DMC configuration */
#define REALVIEW_PCI_CORE_BASE        0x10019000	/* PCI configuration */
	/* Reserved 0x1001A000 - 0x1001FFFF */
#define REALVIEW_CLCD_BASE            0x10020000	/* CLCD */
#define REALVIEW_DMAC_BASE            0x10030000	/* DMA controller */
#ifndef CONFIG_REALVIEW_MPCORE
#define REALVIEW_GIC_CPU_BASE         0x10040000	/* Generic interrupt controller CPU interface */
#define REALVIEW_GIC_DIST_BASE        0x10041000	/* Generic interrupt controller distributor */
#else
#ifdef CONFIG_REALVIEW_MPCORE_REVB
#define REALVIEW_MPCORE_SCU_BASE	0x10100000	/*  SCU registers */
#define REALVIEW_GIC_CPU_BASE		0x10100100	/* Generic interrupt controller CPU interface */
#define REALVIEW_TWD_BASE		0x10100700
#define REALVIEW_TWD_SIZE		0x00000100
#define REALVIEW_GIC_DIST_BASE		0x10101000	/* Generic interrupt controller distributor */
#define REALVIEW_MPCORE_L220_BASE	0x10102000	/* L220 registers */
#define REALVIEW_MPCORE_SYS_PLD_CTRL1 0xD8		/*  Register offset for MPCore sysctl */
#else
#define REALVIEW_MPCORE_SCU_BASE      0x1F000000	/*  SCU registers */
#define REALVIEW_GIC_CPU_BASE         0x1F000100	/* Generic interrupt controller CPU interface */
#define REALVIEW_TWD_BASE             0x1F000700
#define REALVIEW_TWD_SIZE             0x00000100
#define REALVIEW_GIC_DIST_BASE        0x1F001000	/* Generic interrupt controller distributor */
#define REALVIEW_MPCORE_L220_BASE     0x1F002000	/* L220 registers */
#define REALVIEW_MPCORE_SYS_PLD_CTRL1 0x74		/*  Register offset for MPCore sysctl */
#endif
#define REALVIEW_GIC1_CPU_BASE        0x10040000	/* Generic interrupt controller CPU interface */
#define REALVIEW_GIC1_DIST_BASE       0x10041000	/* Generic interrupt controller distributor */
#endif
#define REALVIEW_SMC_BASE             0x10080000	/* SMC */
	/* Reserved 0x10090000 - 0x100EFFFF */

#define REALVIEW_ETH_BASE             0x4E000000	/* Ethernet */

/* PCI space */
#define REALVIEW_PCI_BASE             0x41000000	/* PCI Interface */
#define REALVIEW_PCI_CFG_BASE	      0x42000000
#define REALVIEW_PCI_MEM_BASE0        0x44000000
#define REALVIEW_PCI_MEM_BASE1        0x50000000
#define REALVIEW_PCI_MEM_BASE2        0x60000000
/* Sizes of above maps */
#define REALVIEW_PCI_BASE_SIZE	       0x01000000
#define REALVIEW_PCI_CFG_BASE_SIZE    0x02000000
#define REALVIEW_PCI_MEM_BASE0_SIZE   0x0c000000	/* 32Mb */
#define REALVIEW_PCI_MEM_BASE1_SIZE   0x10000000	/* 256Mb */
#define REALVIEW_PCI_MEM_BASE2_SIZE   0x10000000	/* 256Mb */

#define REALVIEW_SDRAM67_BASE         0x70000000	/* SDRAM banks 6 and 7 */
#define REALVIEW_LT_BASE              0x80000000	/* Logic Tile expansion */

/*
 * Disk on Chip
 */
#define REALVIEW_DOC_BASE             0x2C000000
#define REALVIEW_DOC_SIZE             (16 << 20)
#define REALVIEW_DOC_PAGE_SIZE        512
#define REALVIEW_DOC_TOTAL_PAGES     (DOC_SIZE / PAGE_SIZE)

#define ERASE_UNIT_PAGES    32
#define START_PAGE          0x80

/* 
 *  LED settings, bits [7:0]
 */
#define REALVIEW_SYS_LED0             (1 << 0)
#define REALVIEW_SYS_LED1             (1 << 1)
#define REALVIEW_SYS_LED2             (1 << 2)
#define REALVIEW_SYS_LED3             (1 << 3)
#define REALVIEW_SYS_LED4             (1 << 4)
#define REALVIEW_SYS_LED5             (1 << 5)
#define REALVIEW_SYS_LED6             (1 << 6)
#define REALVIEW_SYS_LED7             (1 << 7)

#define ALL_LEDS                  0xFF

#define LED_BANK                  REALVIEW_SYS_LED

/* 
 * Control registers
 */
#define REALVIEW_IDFIELD_OFFSET	0x0	/* RealView build information */
#define REALVIEW_FLASHPROG_OFFSET	0x4	/* Flash devices */
#define REALVIEW_INTREG_OFFSET		0x8	/* Interrupt control */
#define REALVIEW_DECODE_OFFSET		0xC	/* Fitted logic modules */

/* ------------------------------------------------------------------------
 *  Interrupts - bit assignment (primary)
 * ------------------------------------------------------------------------
 */
#ifndef CONFIG_REALVIEW_MPCORE
#define INT_WDOGINT			0	/* Watchdog timer */
#define INT_SOFTINT			1	/* Software interrupt */
#define INT_COMMRx			2	/* Debug Comm Rx interrupt */
#define INT_COMMTx			3	/* Debug Comm Tx interrupt */
#define INT_TIMERINT0_1			4	/* Timer 0 and 1 */
#define INT_TIMERINT2_3			5	/* Timer 2 and 3 */
#define INT_GPIOINT0			6	/* GPIO 0 */
#define INT_GPIOINT1			7	/* GPIO 1 */
#define INT_GPIOINT2			8	/* GPIO 2 */
/* 9 reserved */
#define INT_RTCINT			10	/* Real Time Clock */
#define INT_SSPINT			11	/* Synchronous Serial Port */
#define INT_UARTINT0			12	/* UART 0 on development chip */
#define INT_UARTINT1			13	/* UART 1 on development chip */
#define INT_UARTINT2			14	/* UART 2 on development chip */
#define INT_UARTINT3			15	/* UART 3 on development chip */
#define INT_SCIINT			16	/* Smart Card Interface */
#define INT_MMCI0A			17	/* Multimedia Card 0A */
#define INT_MMCI0B			18	/* Multimedia Card 0B */
#define INT_AACI			19	/* Audio Codec */
#define INT_KMI0			20	/* Keyboard/Mouse port 0 */
#define INT_KMI1			21	/* Keyboard/Mouse port 1 */
#define INT_CHARLCD			22	/* Character LCD */
#define INT_CLCDINT			23	/* CLCD controller */
#define INT_DMAINT			24	/* DMA controller */
#define INT_PWRFAILINT			25	/* Power failure */
#define INT_PISMO			26
#define INT_DoC				27	/* Disk on Chip memory controller */
#define INT_ETH				28	/* Ethernet controller */
#define INT_USB				29	/* USB controller */
#define INT_TSPENINT			30	/* Touchscreen pen */
#define INT_TSKPADINT			31	/* Touchscreen keypad */

#else

#define MAX_GIC_NR			2

#define INT_AACI			0
#define INT_TIMERINT0_1			1
#define INT_TIMERINT2_3			2
#define INT_USB				3
#define INT_UARTINT0			4
#define INT_UARTINT1			5
#define INT_RTCINT			6
#define INT_KMI0			7
#define INT_KMI1			8
#define INT_ETH				9
#define INT_EB_IRQ1			10	/* main GIC */
#define INT_EB_IRQ2			11	/* tile GIC */
#define INT_EB_FIQ1			12	/* main GIC */
#define INT_EB_FIQ2			13	/* tile GIC */
#define INT_MMCI0A			14
#define INT_MMCI0B			15

#define INT_PMU_CPU0			17
#define INT_PMU_CPU1			18
#define INT_PMU_CPU2			19
#define INT_PMU_CPU3			20
#define INT_PMU_SCU0			21
#define INT_PMU_SCU1			22
#define INT_PMU_SCU2			23
#define INT_PMU_SCU3			24
#define INT_PMU_SCU4			25
#define INT_PMU_SCU5			26
#define INT_PMU_SCU6			27
#define INT_PMU_SCU7			28

#define INT_L220_EVENT			29
#define INT_L220_SLAVE			30
#define INT_L220_DECODE			31

#define INT_UARTINT2			-1
#define INT_UARTINT3			-1
#define INT_CLCDINT			-1
#define INT_DMAINT			-1
#define INT_WDOGINT			-1
#define INT_GPIOINT0			-1
#define INT_GPIOINT1			-1
#define INT_GPIOINT2			-1
#define INT_SCIINT			-1
#define INT_SSPINT			-1
#endif

/* 
 *  Interrupt bit positions
 * 
 */
#define INTMASK_WDOGINT			(1 << INT_WDOGINT)
#define INTMASK_SOFTINT			(1 << INT_SOFTINT)
#define INTMASK_COMMRx			(1 << INT_COMMRx)
#define INTMASK_COMMTx			(1 << INT_COMMTx)
#define INTMASK_TIMERINT0_1		(1 << INT_TIMERINT0_1)
#define INTMASK_TIMERINT2_3		(1 << INT_TIMERINT2_3)
#define INTMASK_GPIOINT0		(1 << INT_GPIOINT0)
#define INTMASK_GPIOINT1		(1 << INT_GPIOINT1)
#define INTMASK_GPIOINT2		(1 << INT_GPIOINT2)
#define INTMASK_RTCINT			(1 << INT_RTCINT)
#define INTMASK_SSPINT			(1 << INT_SSPINT)
#define INTMASK_UARTINT0		(1 << INT_UARTINT0)
#define INTMASK_UARTINT1		(1 << INT_UARTINT1)
#define INTMASK_UARTINT2		(1 << INT_UARTINT2)
#define INTMASK_UARTINT3		(1 << INT_UARTINT3)
#define INTMASK_SCIINT			(1 << INT_SCIINT)
#define INTMASK_MMCI0A			(1 << INT_MMCI0A)
#define INTMASK_MMCI0B			(1 << INT_MMCI0B)
#define INTMASK_AACI			(1 << INT_AACI)
#define INTMASK_KMI0			(1 << INT_KMI0)
#define INTMASK_KMI1			(1 << INT_KMI1)
#define INTMASK_CHARLCD			(1 << INT_CHARLCD)
#define INTMASK_CLCDINT			(1 << INT_CLCDINT)
#define INTMASK_DMAINT			(1 << INT_DMAINT)
#define INTMASK_PWRFAILINT		(1 << INT_PWRFAILINT)
#define INTMASK_PISMO			(1 << INT_PISMO)
#define INTMASK_DoC			(1 << INT_DoC)
#define INTMASK_ETH			(1 << INT_ETH)
#define INTMASK_USB			(1 << INT_USB)
#define INTMASK_TSPENINT		(1 << INT_TSPENINT)
#define INTMASK_TSKPADINT		(1 << INT_TSKPADINT)

#define MAXIRQNUM                       31
#define MAXFIQNUM                       31
#define MAXSWINUM                       31

/* 
 *  Application Flash
 * 
 */
#define FLASH_BASE                      REALVIEW_FLASH_BASE
#define FLASH_SIZE                      REALVIEW_FLASH_SIZE
#define FLASH_END                       (FLASH_BASE + FLASH_SIZE - 1)
#define FLASH_BLOCK_SIZE                SZ_128K

/* 
 *  Boot Flash
 * 
 */
#define EPROM_BASE                      REALVIEW_BOOT_ROM_HI
#define EPROM_SIZE                      REALVIEW_BOOT_ROM_SIZE
#define EPROM_END                       (EPROM_BASE + EPROM_SIZE - 1)

/* 
 *  Clean base - dummy
 * 
 */
#define CLEAN_BASE                      EPROM_BASE

/*
 * System controller bit assignment
 */
#define REALVIEW_REFCLK	0
#define REALVIEW_TIMCLK	1

#define REALVIEW_TIMER1_EnSel	15
#define REALVIEW_TIMER2_EnSel	17
#define REALVIEW_TIMER3_EnSel	19
#define REALVIEW_TIMER4_EnSel	21


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

#define REALVIEW_CSR_BASE             0x10000000
#define REALVIEW_CSR_SIZE             0x10000000

#endif

/* 	END */
