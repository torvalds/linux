/*
 * linux/include/asm-arm/arch-omap/hardware.h
 *
 * Hardware definitions for TI OMAP processors and boards
 *
 * NOTE: Please put device driver specific defines into a separate header
 *	 file for each driver.
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc. Greg Lonnon <glonnon@ridgerun.com>
 *
 * Reorganized for Linux-2.6 by Tony Lindgren <tony@atomide.com>
 *                          and Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_OMAP_HARDWARE_H
#define __ASM_ARCH_OMAP_HARDWARE_H

#include <asm/sizes.h>
#ifndef __ASSEMBLER__
#include <asm/types.h>
#include <asm/arch/cpu.h>
#endif
#include <asm/arch/io.h>
#include <asm/arch/serial.h>

/*
 * ---------------------------------------------------------------------------
 * Common definitions for all OMAP processors
 * NOTE: Put all processor or board specific parts to the special header
 *	 files.
 * ---------------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------------
 * Timers
 * ----------------------------------------------------------------------------
 */
#define OMAP_MPU_TIMER1_BASE	(0xfffec500)
#define OMAP_MPU_TIMER2_BASE	(0xfffec600)
#define OMAP_MPU_TIMER3_BASE	(0xfffec700)
#define MPU_TIMER_FREE		(1 << 6)
#define MPU_TIMER_CLOCK_ENABLE	(1 << 5)
#define MPU_TIMER_AR		(1 << 1)
#define MPU_TIMER_ST		(1 << 0)

/*
 * ----------------------------------------------------------------------------
 * Clocks
 * ----------------------------------------------------------------------------
 */
#define CLKGEN_REG_BASE		(0xfffece00)
#define ARM_CKCTL		(CLKGEN_REG_BASE + 0x0)
#define ARM_IDLECT1		(CLKGEN_REG_BASE + 0x4)
#define ARM_IDLECT2		(CLKGEN_REG_BASE + 0x8)
#define ARM_EWUPCT		(CLKGEN_REG_BASE + 0xC)
#define ARM_RSTCT1		(CLKGEN_REG_BASE + 0x10)
#define ARM_RSTCT2		(CLKGEN_REG_BASE + 0x14)
#define ARM_SYSST		(CLKGEN_REG_BASE + 0x18)
#define ARM_IDLECT3		(CLKGEN_REG_BASE + 0x24)

#define CK_RATEF		1
#define CK_IDLEF		2
#define CK_ENABLEF		4
#define CK_SELECTF		8
#define SETARM_IDLE_SHIFT

/* DPLL control registers */
#define DPLL_CTL		(0xfffecf00)

/* DSP clock control. Must use __raw_readw() and __raw_writew() with these */
#define DSP_CONFIG_REG_BASE     (0xe1008000)
#define DSP_CKCTL		(DSP_CONFIG_REG_BASE + 0x0)
#define DSP_IDLECT1		(DSP_CONFIG_REG_BASE + 0x4)
#define DSP_IDLECT2		(DSP_CONFIG_REG_BASE + 0x8)
#define DSP_RSTCT2		(DSP_CONFIG_REG_BASE + 0x14)

/*
 * ---------------------------------------------------------------------------
 * UPLD
 * ---------------------------------------------------------------------------
 */
#define ULPD_REG_BASE		(0xfffe0800)
#define ULPD_IT_STATUS		(ULPD_REG_BASE + 0x14)
#define ULPD_SETUP_ANALOG_CELL_3	(ULPD_REG_BASE + 0x24)
#define ULPD_CLOCK_CTRL		(ULPD_REG_BASE + 0x30)
#	define DIS_USB_PVCI_CLK		(1 << 5)	/* no USB/FAC synch */
#	define USB_MCLK_EN		(1 << 4)	/* enable W4_USB_CLKO */
#define ULPD_SOFT_REQ		(ULPD_REG_BASE + 0x34)
#	define SOFT_UDC_REQ		(1 << 4)
#	define SOFT_USB_CLK_REQ		(1 << 3)
#	define SOFT_DPLL_REQ		(1 << 0)
#define ULPD_DPLL_CTRL		(ULPD_REG_BASE + 0x3c)
#define ULPD_STATUS_REQ		(ULPD_REG_BASE + 0x40)
#define ULPD_APLL_CTRL		(ULPD_REG_BASE + 0x4c)
#define ULPD_POWER_CTRL		(ULPD_REG_BASE + 0x50)
#define ULPD_SOFT_DISABLE_REQ_REG	(ULPD_REG_BASE + 0x68)
#	define DIS_MMC2_DPLL_REQ	(1 << 11)
#	define DIS_MMC1_DPLL_REQ	(1 << 10)
#	define DIS_UART3_DPLL_REQ	(1 << 9)
#	define DIS_UART2_DPLL_REQ	(1 << 8)
#	define DIS_UART1_DPLL_REQ	(1 << 7)
#	define DIS_USB_HOST_DPLL_REQ	(1 << 6)
#define ULPD_SDW_CLK_DIV_CTRL_SEL	(ULPD_REG_BASE + 0x74)
#define ULPD_CAM_CLK_CTRL	(ULPD_REG_BASE + 0x7c)

/*
 * ---------------------------------------------------------------------------
 * Watchdog timer
 * ---------------------------------------------------------------------------
 */

/* Watchdog timer within the OMAP3.2 gigacell */
#define OMAP_MPU_WATCHDOG_BASE	(0xfffec800)
#define OMAP_WDT_TIMER		(OMAP_MPU_WATCHDOG_BASE + 0x0)
#define OMAP_WDT_LOAD_TIM	(OMAP_MPU_WATCHDOG_BASE + 0x4)
#define OMAP_WDT_READ_TIM	(OMAP_MPU_WATCHDOG_BASE + 0x4)
#define OMAP_WDT_TIMER_MODE	(OMAP_MPU_WATCHDOG_BASE + 0x8)

/*
 * ---------------------------------------------------------------------------
 * Interrupts
 * ---------------------------------------------------------------------------
 */
#ifdef CONFIG_ARCH_OMAP1

/*
 * XXX: These probably want to be moved to arch/arm/mach-omap/omap1/irq.c
 * or something similar.. -- PFM.
 */

#define OMAP_IH1_BASE		0xfffecb00
#define OMAP_IH2_BASE		0xfffe0000

#define OMAP_IH1_ITR		(OMAP_IH1_BASE + 0x00)
#define OMAP_IH1_MIR		(OMAP_IH1_BASE + 0x04)
#define OMAP_IH1_SIR_IRQ	(OMAP_IH1_BASE + 0x10)
#define OMAP_IH1_SIR_FIQ	(OMAP_IH1_BASE + 0x14)
#define OMAP_IH1_CONTROL	(OMAP_IH1_BASE + 0x18)
#define OMAP_IH1_ILR0		(OMAP_IH1_BASE + 0x1c)
#define OMAP_IH1_ISR		(OMAP_IH1_BASE + 0x9c)

#define OMAP_IH2_ITR		(OMAP_IH2_BASE + 0x00)
#define OMAP_IH2_MIR		(OMAP_IH2_BASE + 0x04)
#define OMAP_IH2_SIR_IRQ	(OMAP_IH2_BASE + 0x10)
#define OMAP_IH2_SIR_FIQ	(OMAP_IH2_BASE + 0x14)
#define OMAP_IH2_CONTROL	(OMAP_IH2_BASE + 0x18)
#define OMAP_IH2_ILR0		(OMAP_IH2_BASE + 0x1c)
#define OMAP_IH2_ISR		(OMAP_IH2_BASE + 0x9c)

#define IRQ_ITR_REG_OFFSET	0x00
#define IRQ_MIR_REG_OFFSET	0x04
#define IRQ_SIR_IRQ_REG_OFFSET	0x10
#define IRQ_SIR_FIQ_REG_OFFSET	0x14
#define IRQ_CONTROL_REG_OFFSET	0x18
#define IRQ_ISR_REG_OFFSET	0x9c
#define IRQ_ILR0_REG_OFFSET	0x1c
#define IRQ_GMR_REG_OFFSET	0xa0

#endif

/*
 * ----------------------------------------------------------------------------
 * System control registers
 * ----------------------------------------------------------------------------
 */
#define MOD_CONF_CTRL_0		0xfffe1080
#define MOD_CONF_CTRL_1		0xfffe1110

/*
 * ----------------------------------------------------------------------------
 * Pin multiplexing registers
 * ----------------------------------------------------------------------------
 */
#define FUNC_MUX_CTRL_0		0xfffe1000
#define FUNC_MUX_CTRL_1		0xfffe1004
#define FUNC_MUX_CTRL_2		0xfffe1008
#define COMP_MODE_CTRL_0	0xfffe100c
#define FUNC_MUX_CTRL_3		0xfffe1010
#define FUNC_MUX_CTRL_4		0xfffe1014
#define FUNC_MUX_CTRL_5		0xfffe1018
#define FUNC_MUX_CTRL_6		0xfffe101C
#define FUNC_MUX_CTRL_7		0xfffe1020
#define FUNC_MUX_CTRL_8		0xfffe1024
#define FUNC_MUX_CTRL_9		0xfffe1028
#define FUNC_MUX_CTRL_A		0xfffe102C
#define FUNC_MUX_CTRL_B		0xfffe1030
#define FUNC_MUX_CTRL_C		0xfffe1034
#define FUNC_MUX_CTRL_D		0xfffe1038
#define PULL_DWN_CTRL_0		0xfffe1040
#define PULL_DWN_CTRL_1		0xfffe1044
#define PULL_DWN_CTRL_2		0xfffe1048
#define PULL_DWN_CTRL_3		0xfffe104c
#define PULL_DWN_CTRL_4		0xfffe10ac

/* OMAP-1610 specific multiplexing registers */
#define FUNC_MUX_CTRL_E		0xfffe1090
#define FUNC_MUX_CTRL_F		0xfffe1094
#define FUNC_MUX_CTRL_10	0xfffe1098
#define FUNC_MUX_CTRL_11	0xfffe109c
#define FUNC_MUX_CTRL_12	0xfffe10a0
#define PU_PD_SEL_0		0xfffe10b4
#define PU_PD_SEL_1		0xfffe10b8
#define PU_PD_SEL_2		0xfffe10bc
#define PU_PD_SEL_3		0xfffe10c0
#define PU_PD_SEL_4		0xfffe10c4

/* Timer32K for 1610 and 1710*/
#define OMAP_TIMER32K_BASE	0xFFFBC400

/*
 * ---------------------------------------------------------------------------
 * TIPB bus interface
 * ---------------------------------------------------------------------------
 */
#define TIPB_PUBLIC_CNTL_BASE		0xfffed300
#define MPU_PUBLIC_TIPB_CNTL		(TIPB_PUBLIC_CNTL_BASE + 0x8)
#define TIPB_PRIVATE_CNTL_BASE		0xfffeca00
#define MPU_PRIVATE_TIPB_CNTL		(TIPB_PRIVATE_CNTL_BASE + 0x8)

/*
 * ----------------------------------------------------------------------------
 * MPUI interface
 * ----------------------------------------------------------------------------
 */
#define MPUI_BASE			(0xfffec900)
#define MPUI_CTRL			(MPUI_BASE + 0x0)
#define MPUI_DEBUG_ADDR			(MPUI_BASE + 0x4)
#define MPUI_DEBUG_DATA			(MPUI_BASE + 0x8)
#define MPUI_DEBUG_FLAG			(MPUI_BASE + 0xc)
#define MPUI_STATUS_REG			(MPUI_BASE + 0x10)
#define MPUI_DSP_STATUS			(MPUI_BASE + 0x14)
#define MPUI_DSP_BOOT_CONFIG		(MPUI_BASE + 0x18)
#define MPUI_DSP_API_CONFIG		(MPUI_BASE + 0x1c)

/*
 * ----------------------------------------------------------------------------
 * LED Pulse Generator
 * ----------------------------------------------------------------------------
 */
#define OMAP_LPG1_BASE			0xfffbd000
#define OMAP_LPG2_BASE			0xfffbd800
#define OMAP_LPG1_LCR			(OMAP_LPG1_BASE + 0x00)
#define OMAP_LPG1_PMR			(OMAP_LPG1_BASE + 0x04)
#define OMAP_LPG2_LCR			(OMAP_LPG2_BASE + 0x00)
#define OMAP_LPG2_PMR			(OMAP_LPG2_BASE + 0x04)

/*
 * ---------------------------------------------------------------------------
 * Processor specific defines
 * ---------------------------------------------------------------------------
 */

#include "omap730.h"
#include "omap1510.h"
#include "omap24xx.h"
#include "omap16xx.h"

#ifndef __ASSEMBLER__

/*
 * ---------------------------------------------------------------------------
 * Board specific defines
 * ---------------------------------------------------------------------------
 */

#ifdef CONFIG_MACH_OMAP_INNOVATOR
#include "board-innovator.h"
#endif

#ifdef CONFIG_MACH_OMAP_H2
#include "board-h2.h"
#endif

#ifdef CONFIG_MACH_OMAP_PERSEUS2
#include "board-perseus2.h"
#endif

#ifdef CONFIG_MACH_OMAP_FSAMPLE
#include "board-fsample.h"
#endif

#ifdef CONFIG_MACH_OMAP_H3
#include "board-h3.h"
#endif

#ifdef CONFIG_MACH_OMAP_H4
#include "board-h4.h"
#endif

#ifdef CONFIG_MACH_OMAP_APOLLON
#include "board-apollon.h"
#endif

#ifdef CONFIG_MACH_OMAP_OSK
#include "board-osk.h"
#endif

#ifdef CONFIG_MACH_VOICEBLUE
#include "board-voiceblue.h"
#endif

#endif /* !__ASSEMBLER__ */

#endif	/* __ASM_ARCH_OMAP_HARDWARE_H */
