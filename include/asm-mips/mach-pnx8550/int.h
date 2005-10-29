/*
 *
 * BRIEF MODULE DESCRIPTION
 *   Interrupt specific definitions
 *
 * Author: source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#ifndef __PNX8550_INT_H
#define __PNX8550_INT_H

#define PNX8550_GIC_BASE	0xBBE3E000

#define PNX8550_GIC_PRIMASK_0	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x000)
#define PNX8550_GIC_PRIMASK_1	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x004)
#define PNX8550_GIC_VECTOR_0	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x100)
#define PNX8550_GIC_VECTOR_1	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x104)
#define PNX8550_GIC_PEND_1_31	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x200)
#define PNX8550_GIC_PEND_32_63	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x204)
#define PNX8550_GIC_PEND_64_70	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x208)
#define PNX8550_GIC_FEATURES	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x300)
#define PNX8550_GIC_REQ(x)	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0x400 + (x)*4)
#define PNX8550_GIC_MOD_ID	*(volatile unsigned long *)(PNX8550_GIC_BASE + 0xFFC)

// cp0 is two software + six hw exceptions
#define PNX8550_INT_CP0_TOTINT	8
#define PNX8550_INT_CP0_MIN	0
#define PNX8550_INT_CP0_MAX	(PNX8550_INT_CP0_MIN + PNX8550_INT_CP0_TOTINT - 1)

#define MIPS_CPU_GIC_IRQ        2
#define MIPS_CPU_TIMER_IRQ      7

// GIC are 71 exceptions connected to cp0's first hardware exception
#define PNX8550_INT_GIC_TOTINT	71
#define PNX8550_INT_GIC_MIN	(PNX8550_INT_CP0_MAX+1)
#define PNX8550_INT_GIC_MAX	(PNX8550_INT_GIC_MIN + PNX8550_INT_GIC_TOTINT - 1)

#define PNX8550_INT_UNDEF              (PNX8550_INT_GIC_MIN+0)
#define PNX8550_INT_IPC_TARGET0_MIPS   (PNX8550_INT_GIC_MIN+1)
#define PNX8550_INT_IPC_TARGET1_TM32_1 (PNX8550_INT_GIC_MIN+2)
#define PNX8550_INT_IPC_TARGET1_TM32_2 (PNX8550_INT_GIC_MIN+3)
#define PNX8550_INT_RESERVED_4         (PNX8550_INT_GIC_MIN+4)
#define PNX8550_INT_USB                (PNX8550_INT_GIC_MIN+5)
#define PNX8550_INT_GPIO_EQ1           (PNX8550_INT_GIC_MIN+6)
#define PNX8550_INT_GPIO_EQ2           (PNX8550_INT_GIC_MIN+7)
#define PNX8550_INT_GPIO_EQ3           (PNX8550_INT_GIC_MIN+8)
#define PNX8550_INT_GPIO_EQ4           (PNX8550_INT_GIC_MIN+9)

#define PNX8550_INT_GPIO_EQ5           (PNX8550_INT_GIC_MIN+10)
#define PNX8550_INT_GPIO_EQ6           (PNX8550_INT_GIC_MIN+11)
#define PNX8550_INT_RESERVED_12        (PNX8550_INT_GIC_MIN+12)
#define PNX8550_INT_QVCP1              (PNX8550_INT_GIC_MIN+13)
#define PNX8550_INT_QVCP2              (PNX8550_INT_GIC_MIN+14)
#define PNX8550_INT_I2C1               (PNX8550_INT_GIC_MIN+15)
#define PNX8550_INT_I2C2               (PNX8550_INT_GIC_MIN+16)
#define PNX8550_INT_ISO_UART1          (PNX8550_INT_GIC_MIN+17)
#define PNX8550_INT_ISO_UART2          (PNX8550_INT_GIC_MIN+18)
#define PNX8550_INT_UART1              (PNX8550_INT_GIC_MIN+19)

#define PNX8550_INT_UART2              (PNX8550_INT_GIC_MIN+20)
#define PNX8550_INT_QNTR               (PNX8550_INT_GIC_MIN+21)
#define PNX8550_INT_RESERVED22         (PNX8550_INT_GIC_MIN+22)
#define PNX8550_INT_T_DSC              (PNX8550_INT_GIC_MIN+23)
#define PNX8550_INT_M_DSC              (PNX8550_INT_GIC_MIN+24)
#define PNX8550_INT_RESERVED25         (PNX8550_INT_GIC_MIN+25)
#define PNX8550_INT_2D_DRAW_ENG        (PNX8550_INT_GIC_MIN+26)
#define PNX8550_INT_MEM_BASED_SCALAR1  (PNX8550_INT_GIC_MIN+27)
#define PNX8550_INT_VIDEO_MPEG         (PNX8550_INT_GIC_MIN+28)
#define PNX8550_INT_VIDEO_INPUT_P1     (PNX8550_INT_GIC_MIN+29)

#define PNX8550_INT_VIDEO_INPUT_P2     (PNX8550_INT_GIC_MIN+30)
#define PNX8550_INT_SPDI1              (PNX8550_INT_GIC_MIN+31)
#define PNX8550_INT_SPDO               (PNX8550_INT_GIC_MIN+32)
#define PNX8550_INT_AUDIO_INPUT1       (PNX8550_INT_GIC_MIN+33)
#define PNX8550_INT_AUDIO_OUTPUT1      (PNX8550_INT_GIC_MIN+34)
#define PNX8550_INT_AUDIO_INPUT2       (PNX8550_INT_GIC_MIN+35)
#define PNX8550_INT_AUDIO_OUTPUT2      (PNX8550_INT_GIC_MIN+36)
#define PNX8550_INT_MEMBASED_SCALAR2   (PNX8550_INT_GIC_MIN+37)
#define PNX8550_INT_VPK                (PNX8550_INT_GIC_MIN+38)
#define PNX8550_INT_MPEG1_MIPS         (PNX8550_INT_GIC_MIN+39)

#define PNX8550_INT_MPEG1_TM           (PNX8550_INT_GIC_MIN+40)
#define PNX8550_INT_MPEG2_MIPS         (PNX8550_INT_GIC_MIN+41)
#define PNX8550_INT_MPEG2_TM           (PNX8550_INT_GIC_MIN+42)
#define PNX8550_INT_TS_DMA             (PNX8550_INT_GIC_MIN+43)
#define PNX8550_INT_EDMA               (PNX8550_INT_GIC_MIN+44)
#define PNX8550_INT_TM_DEBUG1          (PNX8550_INT_GIC_MIN+45)
#define PNX8550_INT_TM_DEBUG2          (PNX8550_INT_GIC_MIN+46)
#define PNX8550_INT_PCI_INTA           (PNX8550_INT_GIC_MIN+47)
#define PNX8550_INT_CLOCK_MODULE       (PNX8550_INT_GIC_MIN+48)
#define PNX8550_INT_PCI_XIO_INTA_PCI   (PNX8550_INT_GIC_MIN+49)

#define PNX8550_INT_PCI_XIO_INTB_DMA   (PNX8550_INT_GIC_MIN+50)
#define PNX8550_INT_PCI_XIO_INTC_GPPM  (PNX8550_INT_GIC_MIN+51)
#define PNX8550_INT_PCI_XIO_INTD_GPXIO (PNX8550_INT_GIC_MIN+52)
#define PNX8550_INT_DVD_CSS            (PNX8550_INT_GIC_MIN+53)
#define PNX8550_INT_VLD                (PNX8550_INT_GIC_MIN+54)
#define PNX8550_INT_GPIO_TSU_7_0       (PNX8550_INT_GIC_MIN+55)
#define PNX8550_INT_GPIO_TSU_15_8      (PNX8550_INT_GIC_MIN+56)
#define PNX8550_INT_GPIO_CTU_IR        (PNX8550_INT_GIC_MIN+57)
#define PNX8550_INT_GPIO0              (PNX8550_INT_GIC_MIN+58)
#define PNX8550_INT_GPIO1              (PNX8550_INT_GIC_MIN+59)

#define PNX8550_INT_GPIO2              (PNX8550_INT_GIC_MIN+60)
#define PNX8550_INT_GPIO3              (PNX8550_INT_GIC_MIN+61)
#define PNX8550_INT_GPIO4              (PNX8550_INT_GIC_MIN+62)
#define PNX8550_INT_GPIO5              (PNX8550_INT_GIC_MIN+63)
#define PNX8550_INT_GPIO6              (PNX8550_INT_GIC_MIN+64)
#define PNX8550_INT_GPIO7              (PNX8550_INT_GIC_MIN+65)
#define PNX8550_INT_PMAN_SECURITY      (PNX8550_INT_GIC_MIN+66)
#define PNX8550_INT_I2C3               (PNX8550_INT_GIC_MIN+67)
#define PNX8550_INT_RESERVED_68        (PNX8550_INT_GIC_MIN+68)
#define PNX8550_INT_SPDI2              (PNX8550_INT_GIC_MIN+69)

#define PNX8550_INT_I2C4               (PNX8550_INT_GIC_MIN+70)

// Timer are 3 exceptions connected to cp0's 7th hardware exception
#define PNX8550_INT_TIMER_TOTINT       3
#define PNX8550_INT_TIMER_MIN	       (PNX8550_INT_GIC_MAX+1)
#define PNX8550_INT_TIMER_MAX          (PNX8550_INT_TIMER_MIN + PNX8550_INT_TIMER_TOTINT - 1)

#define PNX8550_INT_TIMER1             (PNX8550_INT_TIMER_MIN+0)
#define PNX8550_INT_TIMER2             (PNX8550_INT_TIMER_MIN+1)
#define PNX8550_INT_TIMER3             (PNX8550_INT_TIMER_MIN+2)
#define PNX8550_INT_WATCHDOG           PNX8550_INT_TIMER3

#endif
