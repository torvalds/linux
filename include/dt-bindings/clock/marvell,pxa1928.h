/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DTS_MARVELL_PXA1928_CLOCK_H
#define __DTS_MARVELL_PXA1928_CLOCK_H

/*
 * Clock ID values here correspond to the control register offset/4.
 */

/* apb peripherals */
#define PXA1928_CLK_RTC			0x00
#define PXA1928_CLK_TWSI0		0x01
#define PXA1928_CLK_TWSI1		0x02
#define PXA1928_CLK_TWSI2		0x03
#define PXA1928_CLK_TWSI3		0x04
#define PXA1928_CLK_OWIRE		0x05
#define PXA1928_CLK_KPC			0x06
#define PXA1928_CLK_TB_ROTARY		0x07
#define PXA1928_CLK_SW_JTAG		0x08
#define PXA1928_CLK_TIMER1		0x09
#define PXA1928_CLK_UART0		0x0b
#define PXA1928_CLK_UART1		0x0c
#define PXA1928_CLK_UART2		0x0d
#define PXA1928_CLK_GPIO		0x0e
#define PXA1928_CLK_PWM0		0x0f
#define PXA1928_CLK_PWM1		0x10
#define PXA1928_CLK_PWM2		0x11
#define PXA1928_CLK_PWM3		0x12
#define PXA1928_CLK_SSP0		0x13
#define PXA1928_CLK_SSP1		0x14
#define PXA1928_CLK_SSP2		0x15

#define PXA1928_CLK_TWSI4		0x1f
#define PXA1928_CLK_TWSI5		0x20
#define PXA1928_CLK_UART3		0x22
#define PXA1928_CLK_THSENS_GLOB		0x24
#define PXA1928_CLK_THSENS_CPU		0x26
#define PXA1928_CLK_THSENS_VPU		0x27
#define PXA1928_CLK_THSENS_GC		0x28


/* axi peripherals */
#define PXA1928_CLK_SDH0		0x15
#define PXA1928_CLK_SDH1		0x16
#define PXA1928_CLK_USB			0x17
#define PXA1928_CLK_NAND		0x18
#define PXA1928_CLK_DMA			0x19

#define PXA1928_CLK_SDH2		0x3a
#define PXA1928_CLK_SDH3		0x3b
#define PXA1928_CLK_HSIC		0x3e
#define PXA1928_CLK_SDH4		0x57
#define PXA1928_CLK_GC3D		0x5d
#define PXA1928_CLK_GC2D		0x5f

#endif
