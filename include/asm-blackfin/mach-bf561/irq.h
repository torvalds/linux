
/*
 * File:         include/asm-blackfin/mach-bf561/irq.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BF561_IRQ_H_
#define _BF561_IRQ_H_

/***********************************************************************
 * Interrupt source definitions:
             Event Source		Core Event Name	    IRQ No
						(highest priority)
	    Emulation Events			EMU         0
            Reset				RST         1
            NMI					NMI         2
            Exception				EVX         3
            Reserved				--          4
            Hardware Error			IVHW        5
            Core Timer				IVTMR       6 *

	    PLL Wakeup Interrupt		IVG7	    7
	    DMA1 Error (generic)		IVG7	    8
	    DMA2 Error (generic)		IVG7	    9
	    IMDMA Error (generic)		IVG7	    10
	    PPI1 Error Interrupt		IVG7	    11
	    PPI2 Error Interrupt		IVG7	    12
	    SPORT0 Error Interrupt		IVG7	    13
	    SPORT1 Error Interrupt		IVG7	    14
	    SPI Error Interrupt			IVG7	    15
	    UART Error Interrupt		IVG7	    16
	    Reserved Interrupt			IVG7        17

	    DMA1 0  Interrupt(PPI1)	        IVG8	    18
	    DMA1 1  Interrupt(PPI2)             IVG8	    19
	    DMA1 2  Interrupt	                IVG8	    20
	    DMA1 3  Interrupt	                IVG8	    21
	    DMA1 4  Interrupt	                IVG8	    22
	    DMA1 5  Interrupt	                IVG8	    23
	    DMA1 6  Interrupt	                IVG8	    24
	    DMA1 7  Interrupt	                IVG8	    25
	    DMA1 8  Interrupt	                IVG8	    26
	    DMA1 9  Interrupt	                IVG8	    27
	    DMA1 10 Interrupt	                IVG8	    28
	    DMA1 11 Interrupt	                IVG8	    29

	    DMA2 0  (SPORT0 RX)		        IVG9	    30
	    DMA2 1  (SPORT0 TX)	                IVG9	    31
	    DMA2 2  (SPORT1 RX)	                IVG9	    32
	    DMA2 3  (SPORT2 TX)	                IVG9	    33
	    DMA2 4  (SPI)	                IVG9	    34
	    DMA2 5  (UART RX)	                IVG9	    35
	    DMA2 6  (UART TX)	                IVG9	    36
	    DMA2 7  Interrupt	                IVG9	    37
	    DMA2 8  Interrupt	                IVG9	    38
	    DMA2 9  Interrupt	                IVG9	    39
	    DMA2 10 Interrupt	                IVG9	    40
	    DMA2 11 Interrupt	                IVG9	    41

	    TIMER 0  Interrupt		        IVG10	    42
	    TIMER 1  Interrupt	                IVG10	    43
	    TIMER 2  Interrupt	                IVG10	    44
	    TIMER 3  Interrupt	                IVG10	    45
	    TIMER 4  Interrupt	                IVG10	    46
	    TIMER 5  Interrupt	                IVG10	    47
	    TIMER 6  Interrupt	                IVG10	    48
	    TIMER 7  Interrupt	                IVG10	    49
	    TIMER 8  Interrupt	                IVG10	    50
	    TIMER 9  Interrupt	                IVG10	    51
	    TIMER 10 Interrupt	                IVG10	    52
	    TIMER 11 Interrupt	                IVG10	    53

	    Programmable Flags0 A (8)	        IVG11	    54
	    Programmable Flags0 B (8)           IVG11	    55
	    Programmable Flags1 A (8)           IVG11	    56
	    Programmable Flags1 B (8)           IVG11	    57
	    Programmable Flags2 A (8)           IVG11	    58
	    Programmable Flags2 B (8)           IVG11	    59

	    MDMA1 0 write/read INT		IVG8	    60
	    MDMA1 1 write/read INT		IVG8	    61

	    MDMA2 0 write/read INT		IVG9	    62
	    MDMA2 1 write/read INT		IVG9	    63

	    IMDMA 0 write/read INT		IVG12	    64
	    IMDMA 1 write/read INT		IVG12	    65

	    Watch Dog Timer			IVG13	    66

	    Reserved interrupt			IVG7	    67
	    Reserved interrupt			IVG7	    68
	    Supplemental interrupt 0		IVG7	    69
	    supplemental interrupt 1		IVG7	    70

            Software Interrupt 1		IVG14       71
            Software Interrupt 2		IVG15       72 *
						(lowest priority)
 **********************************************************************/

#define SYS_IRQS		72
#define NR_PERI_INTS		64

/*
 * The ABSTRACT IRQ definitions
 *  the first seven of the following are fixed,
 *  the rest you change if you need to.
 */
/* IVG 0-6*/
#define	IRQ_EMU			0	/* Emulation                */
#define	IRQ_RST			1	/* Reset                    */
#define	IRQ_NMI			2	/* Non Maskable Interrupt   */
#define	IRQ_EVX			3	/* Exception                */
#define	IRQ_UNUSED		4	/* Reserved interrupt       */
#define	IRQ_HWERR		5	/* Hardware Error           */
#define	IRQ_CORETMR		6	/* Core timer               */

#define IVG_BASE		7
/* IVG 7  */
#define	IRQ_PLL_WAKEUP		(IVG_BASE + 0)	/* PLL Wakeup Interrupt     */
#define	IRQ_DMA1_ERROR		(IVG_BASE + 1)	/* DMA1   Error (general)   */
#define	IRQ_DMA_ERROR		IRQ_DMA1_ERROR	/* DMA1   Error (general)   */
#define	IRQ_DMA2_ERROR		(IVG_BASE + 2)	/* DMA2   Error (general)   */
#define IRQ_IMDMA_ERROR		(IVG_BASE + 3)	/* IMDMA  Error Interrupt   */
#define	IRQ_PPI1_ERROR		(IVG_BASE + 4)	/* PPI1   Error Interrupt   */
#define	IRQ_PPI_ERROR		IRQ_PPI1_ERROR	/* PPI1   Error Interrupt   */
#define	IRQ_PPI2_ERROR		(IVG_BASE + 5)	/* PPI2   Error Interrupt   */
#define	IRQ_SPORT0_ERROR	(IVG_BASE + 6)	/* SPORT0 Error Interrupt   */
#define	IRQ_SPORT1_ERROR	(IVG_BASE + 7)	/* SPORT1 Error Interrupt   */
#define	IRQ_SPI_ERROR		(IVG_BASE + 8)	/* SPI    Error Interrupt   */
#define	IRQ_UART_ERROR		(IVG_BASE + 9)	/* UART   Error Interrupt   */
#define IRQ_RESERVED_ERROR	(IVG_BASE + 10)	/* Reversed     Interrupt   */
/* IVG 8  */
#define	IRQ_DMA1_0		(IVG_BASE + 11)	/* DMA1 0  Interrupt(PPI1)  */
#define	IRQ_PPI			IRQ_DMA1_0	/* DMA1 0  Interrupt(PPI1)  */
#define	IRQ_PPI0		IRQ_DMA1_0	/* DMA1 0  Interrupt(PPI1)  */
#define	IRQ_DMA1_1		(IVG_BASE + 12)	/* DMA1 1  Interrupt(PPI2)  */
#define	IRQ_PPI1		IRQ_DMA1_1	/* DMA1 1  Interrupt(PPI2)  */
#define	IRQ_DMA1_2		(IVG_BASE + 13)	/* DMA1 2  Interrupt        */
#define	IRQ_DMA1_3		(IVG_BASE + 14)	/* DMA1 3  Interrupt        */
#define	IRQ_DMA1_4		(IVG_BASE + 15)	/* DMA1 4  Interrupt        */
#define	IRQ_DMA1_5		(IVG_BASE + 16)	/* DMA1 5  Interrupt        */
#define	IRQ_DMA1_6		(IVG_BASE + 17)	/* DMA1 6  Interrupt        */
#define	IRQ_DMA1_7		(IVG_BASE + 18)	/* DMA1 7  Interrupt        */
#define	IRQ_DMA1_8		(IVG_BASE + 19)	/* DMA1 8  Interrupt        */
#define	IRQ_DMA1_9		(IVG_BASE + 20)	/* DMA1 9  Interrupt        */
#define	IRQ_DMA1_10		(IVG_BASE + 21)	/* DMA1 10 Interrupt        */
#define	IRQ_DMA1_11		(IVG_BASE + 22)	/* DMA1 11 Interrupt        */
/* IVG 9  */
#define	IRQ_DMA2_0		(IVG_BASE + 23)	/* DMA2 0  (SPORT0 RX)      */
#define	IRQ_SPORT0_RX		IRQ_DMA2_0	/* DMA2 0  (SPORT0 RX)      */
#define	IRQ_DMA2_1		(IVG_BASE + 24)	/* DMA2 1  (SPORT0 TX)      */
#define	IRQ_SPORT0_TX		IRQ_DMA2_1	/* DMA2 1  (SPORT0 TX)      */
#define	IRQ_DMA2_2		(IVG_BASE + 25)	/* DMA2 2  (SPORT1 RX)      */
#define	IRQ_SPORT1_RX		IRQ_DMA2_2	/* DMA2 2  (SPORT1 RX)      */
#define	IRQ_DMA2_3		(IVG_BASE + 26)	/* DMA2 3  (SPORT2 TX)      */
#define	IRQ_SPORT1_TX		IRQ_DMA2_3	/* DMA2 3  (SPORT2 TX)      */
#define	IRQ_DMA2_4		(IVG_BASE + 27)	/* DMA2 4  (SPI)            */
#define	IRQ_SPI			IRQ_DMA2_4	/* DMA2 4  (SPI)            */
#define	IRQ_DMA2_5		(IVG_BASE + 28)	/* DMA2 5  (UART RX)        */
#define	IRQ_UART_RX		IRQ_DMA2_5	/* DMA2 5  (UART RX)        */
#define	IRQ_DMA2_6		(IVG_BASE + 29)	/* DMA2 6  (UART TX)        */
#define	IRQ_UART_TX		IRQ_DMA2_6	/* DMA2 6  (UART TX)        */
#define	IRQ_DMA2_7		(IVG_BASE + 30)	/* DMA2 7  Interrupt        */
#define	IRQ_DMA2_8		(IVG_BASE + 31)	/* DMA2 8  Interrupt        */
#define	IRQ_DMA2_9		(IVG_BASE + 32)	/* DMA2 9  Interrupt        */
#define	IRQ_DMA2_10		(IVG_BASE + 33)	/* DMA2 10 Interrupt        */
#define	IRQ_DMA2_11		(IVG_BASE + 34)	/* DMA2 11 Interrupt        */
/* IVG 10 */
#define IRQ_TIMER0		(IVG_BASE + 35)	/* TIMER 0  Interrupt       */
#define IRQ_TIMER1		(IVG_BASE + 36)	/* TIMER 1  Interrupt       */
#define IRQ_TIMER2		(IVG_BASE + 37)	/* TIMER 2  Interrupt       */
#define IRQ_TIMER3		(IVG_BASE + 38)	/* TIMER 3  Interrupt       */
#define IRQ_TIMER4		(IVG_BASE + 39)	/* TIMER 4  Interrupt       */
#define IRQ_TIMER5		(IVG_BASE + 40)	/* TIMER 5  Interrupt       */
#define IRQ_TIMER6		(IVG_BASE + 41)	/* TIMER 6  Interrupt       */
#define IRQ_TIMER7		(IVG_BASE + 42)	/* TIMER 7  Interrupt       */
#define IRQ_TIMER8		(IVG_BASE + 43)	/* TIMER 8  Interrupt       */
#define IRQ_TIMER9		(IVG_BASE + 44)	/* TIMER 9  Interrupt       */
#define IRQ_TIMER10		(IVG_BASE + 45)	/* TIMER 10 Interrupt       */
#define IRQ_TIMER11		(IVG_BASE + 46)	/* TIMER 11 Interrupt       */
/* IVG 11 */
#define	IRQ_PROG0_INTA		(IVG_BASE + 47)	/* Programmable Flags0 A (8) */
#define	IRQ_PROG_INTA		IRQ_PROG0_INTA	/* Programmable Flags0 A (8) */
#define	IRQ_PROG0_INTB		(IVG_BASE + 48)	/* Programmable Flags0 B (8) */
#define	IRQ_PROG_INTB		IRQ_PROG0_INTB	/* Programmable Flags0 B (8) */
#define	IRQ_PROG1_INTA		(IVG_BASE + 49)	/* Programmable Flags1 A (8) */
#define	IRQ_PROG1_INTB		(IVG_BASE + 50)	/* Programmable Flags1 B (8) */
#define	IRQ_PROG2_INTA		(IVG_BASE + 51)	/* Programmable Flags2 A (8) */
#define	IRQ_PROG2_INTB		(IVG_BASE + 52)	/* Programmable Flags2 B (8) */
/* IVG 8  */
#define IRQ_DMA1_WRRD0		(IVG_BASE + 53)	/* MDMA1 0 write/read INT   */
#define IRQ_DMA_WRRD0		IRQ_DMA1_WRRD0	/* MDMA1 0 write/read INT   */
#define IRQ_MEM_DMA0		IRQ_DMA1_WRRD0
#define IRQ_DMA1_WRRD1		(IVG_BASE + 54)	/* MDMA1 1 write/read INT   */
#define IRQ_DMA_WRRD1		IRQ_DMA1_WRRD1	/* MDMA1 1 write/read INT   */
#define IRQ_MEM_DMA1		IRQ_DMA1_WRRD1
/* IVG 9  */
#define IRQ_DMA2_WRRD0		(IVG_BASE + 55)	/* MDMA2 0 write/read INT   */
#define IRQ_MEM_DMA2		IRQ_DMA2_WRRD0
#define IRQ_DMA2_WRRD1		(IVG_BASE + 56)	/* MDMA2 1 write/read INT   */
#define IRQ_MEM_DMA3		IRQ_DMA2_WRRD1
/* IVG 12 */
#define IRQ_IMDMA_WRRD0		(IVG_BASE + 57)	/* IMDMA 0 write/read INT   */
#define IRQ_IMEM_DMA0		IRQ_IMDMA_WRRD0
#define IRQ_IMDMA_WRRD1		(IVG_BASE + 58)	/* IMDMA 1 write/read INT   */
#define IRQ_IMEM_DMA1		IRQ_IMDMA_WRRD1
/* IVG 13 */
#define	IRQ_WATCH	   	(IVG_BASE + 59)	/* Watch Dog Timer          */
/* IVG 7  */
#define IRQ_RESERVED_1		(IVG_BASE + 60)	/* Reserved interrupt       */
#define IRQ_RESERVED_2		(IVG_BASE + 61)	/* Reserved interrupt       */
#define IRQ_SUPPLE_0		(IVG_BASE + 62)	/* Supplemental interrupt 0 */
#define IRQ_SUPPLE_1		(IVG_BASE + 63)	/* supplemental interrupt 1 */
#define	IRQ_SW_INT1		71	/* Software Interrupt 1     */
#define	IRQ_SW_INT2		72	/* Software Interrupt 2     */
						/* reserved for SYSCALL */
#define IRQ_PF0			73
#define IRQ_PF1			74
#define IRQ_PF2			75
#define IRQ_PF3			76
#define IRQ_PF4			77
#define IRQ_PF5			78
#define IRQ_PF6			79
#define IRQ_PF7			80
#define IRQ_PF8			81
#define IRQ_PF9			82
#define IRQ_PF10		83
#define IRQ_PF11		84
#define IRQ_PF12		85
#define IRQ_PF13		86
#define IRQ_PF14		87
#define IRQ_PF15		88
#define IRQ_PF16		89
#define IRQ_PF17		90
#define IRQ_PF18		91
#define IRQ_PF19		92
#define IRQ_PF20		93
#define IRQ_PF21		94
#define IRQ_PF22		95
#define IRQ_PF23		96
#define IRQ_PF24		97
#define IRQ_PF25		98
#define IRQ_PF26		99
#define IRQ_PF27		100
#define IRQ_PF28		101
#define IRQ_PF29		102
#define IRQ_PF30		103
#define IRQ_PF31		104
#define IRQ_PF32		105
#define IRQ_PF33		106
#define IRQ_PF34		107
#define IRQ_PF35		108
#define IRQ_PF36		109
#define IRQ_PF37		110
#define IRQ_PF38		111
#define IRQ_PF39		112
#define IRQ_PF40		113
#define IRQ_PF41		114
#define IRQ_PF42		115
#define IRQ_PF43		116
#define IRQ_PF44		117
#define IRQ_PF45		118
#define IRQ_PF46		119
#define IRQ_PF47		120

#ifdef CONFIG_IRQCHIP_DEMUX_GPIO
#define NR_IRQS			(IRQ_PF47 + 1)
#else
#define NR_IRQS			SYS_IRQS
#endif

#define IVG7			7
#define IVG8			8
#define IVG9			9
#define IVG10			10
#define IVG11			11
#define IVG12			12
#define IVG13			13
#define IVG14			14
#define IVG15			15

/*
 * DEFAULT PRIORITIES:
 */

#define	CONFIG_DEF_PLL_WAKEUP		7
#define	CONFIG_DEF_DMA1_ERROR		7
#define	CONFIG_DEF_DMA2_ERROR		7
#define CONFIG_DEF_IMDMA_ERROR		7
#define	CONFIG_DEF_PPI1_ERROR		7
#define	CONFIG_DEF_PPI2_ERROR		7
#define	CONFIG_DEF_SPORT0_ERROR		7
#define	CONFIG_DEF_SPORT1_ERROR		7
#define	CONFIG_DEF_SPI_ERROR		7
#define	CONFIG_DEF_UART_ERROR		7
#define CONFIG_DEF_RESERVED_ERROR	7
#define	CONFIG_DEF_DMA1_0		8
#define	CONFIG_DEF_DMA1_1		8
#define	CONFIG_DEF_DMA1_2		8
#define	CONFIG_DEF_DMA1_3		8
#define	CONFIG_DEF_DMA1_4		8
#define	CONFIG_DEF_DMA1_5		8
#define	CONFIG_DEF_DMA1_6		8
#define	CONFIG_DEF_DMA1_7		8
#define	CONFIG_DEF_DMA1_8		8
#define	CONFIG_DEF_DMA1_9		8
#define	CONFIG_DEF_DMA1_10		8
#define	CONFIG_DEF_DMA1_11		8
#define	CONFIG_DEF_DMA2_0		9
#define	CONFIG_DEF_DMA2_1		9
#define	CONFIG_DEF_DMA2_2		9
#define	CONFIG_DEF_DMA2_3		9
#define	CONFIG_DEF_DMA2_4		9
#define	CONFIG_DEF_DMA2_5		9
#define	CONFIG_DEF_DMA2_6		9
#define	CONFIG_DEF_DMA2_7		9
#define	CONFIG_DEF_DMA2_8		9
#define	CONFIG_DEF_DMA2_9		9
#define	CONFIG_DEF_DMA2_10		9
#define	CONFIG_DEF_DMA2_11		9
#define CONFIG_DEF_TIMER0		10
#define CONFIG_DEF_TIMER1		10
#define CONFIG_DEF_TIMER2		10
#define CONFIG_DEF_TIMER3		10
#define CONFIG_DEF_TIMER4		10
#define CONFIG_DEF_TIMER5		10
#define CONFIG_DEF_TIMER6		10
#define CONFIG_DEF_TIMER7		10
#define CONFIG_DEF_TIMER8		10
#define CONFIG_DEF_TIMER9		10
#define CONFIG_DEF_TIMER10		10
#define CONFIG_DEF_TIMER11		10
#define	CONFIG_DEF_PROG0_INTA		11
#define	CONFIG_DEF_PROG0_INTB		11
#define	CONFIG_DEF_PROG1_INTA		11
#define	CONFIG_DEF_PROG1_INTB		11
#define	CONFIG_DEF_PROG2_INTA		11
#define	CONFIG_DEF_PROG2_INTB		11
#define CONFIG_DEF_DMA1_WRRD0		8
#define CONFIG_DEF_DMA1_WRRD1		8
#define CONFIG_DEF_DMA2_WRRD0		9
#define CONFIG_DEF_DMA2_WRRD1		9
#define CONFIG_DEF_IMDMA_WRRD0		12
#define CONFIG_DEF_IMDMA_WRRD1		12
#define	CONFIG_DEF_WATCH	   	13
#define CONFIG_DEF_RESERVED_1		7
#define CONFIG_DEF_RESERVED_2		7
#define CONFIG_DEF_SUPPLE_0		7
#define CONFIG_DEF_SUPPLE_1		7

/* IAR0 BIT FIELDS */
#define	IRQ_PLL_WAKEUP_POS			0
#define	IRQ_DMA1_ERROR_POS			4
#define	IRQ_DMA2_ERROR_POS			8
#define IRQ_IMDMA_ERROR_POS			12
#define	IRQ_PPI0_ERROR_POS			16
#define	IRQ_PPI1_ERROR_POS			20
#define	IRQ_SPORT0_ERROR_POS		24
#define	IRQ_SPORT1_ERROR_POS		28
/* IAR1 BIT FIELDS */
#define	IRQ_SPI_ERROR_POS			0
#define	IRQ_UART_ERROR_POS			4
#define IRQ_RESERVED_ERROR_POS		8
#define	IRQ_DMA1_0_POS			12
#define	IRQ_DMA1_1_POS			16
#define IRQ_DMA1_2_POS			20
#define IRQ_DMA1_3_POS			24
#define IRQ_DMA1_4_POS			28
/* IAR2 BIT FIELDS */
#define IRQ_DMA1_5_POS			0
#define IRQ_DMA1_6_POS			4
#define IRQ_DMA1_7_POS			8
#define IRQ_DMA1_8_POS			12
#define IRQ_DMA1_9_POS			16
#define IRQ_DMA1_10_POS			20
#define IRQ_DMA1_11_POS			24
#define IRQ_DMA2_0_POS			28
/* IAR3 BIT FIELDS */
#define IRQ_DMA2_1_POS			0
#define IRQ_DMA2_2_POS			4
#define IRQ_DMA2_3_POS			8
#define IRQ_DMA2_4_POS			12
#define IRQ_DMA2_5_POS			16
#define IRQ_DMA2_6_POS			20
#define IRQ_DMA2_7_POS			24
#define IRQ_DMA2_8_POS			28
/* IAR4 BIT FIELDS */
#define IRQ_DMA2_9_POS			0
#define IRQ_DMA2_10_POS			4
#define IRQ_DMA2_11_POS			8
#define IRQ_TIMER0_POS			12
#define IRQ_TIMER1_POS			16
#define IRQ_TIMER2_POS			20
#define IRQ_TIMER3_POS			24
#define IRQ_TIMER4_POS			28
/* IAR5 BIT FIELDS */
#define IRQ_TIMER5_POS			0
#define IRQ_TIMER6_POS			4
#define IRQ_TIMER7_POS			8
#define IRQ_TIMER8_POS			12
#define IRQ_TIMER9_POS			16
#define IRQ_TIMER10_POS			20
#define IRQ_TIMER11_POS			24
#define IRQ_PROG0_INTA_POS			28
/* IAR6 BIT FIELDS */
#define IRQ_PROG0_INTB_POS			0
#define IRQ_PROG1_INTA_POS			4
#define IRQ_PROG1_INTB_POS			8
#define IRQ_PROG2_INTA_POS			12
#define IRQ_PROG2_INTB_POS			16
#define IRQ_DMA1_WRRD0_POS			20
#define IRQ_DMA1_WRRD1_POS			24
#define IRQ_DMA2_WRRD0_POS			28
/* IAR7 BIT FIELDS */
#define IRQ_DMA2_WRRD1_POS			0
#define IRQ_IMDMA_WRRD0_POS			4
#define IRQ_IMDMA_WRRD1_POS			8
#define	IRQ_WDTIMER_POS			12
#define IRQ_RESERVED_1_POS			16
#define IRQ_RESERVED_2_POS			20
#define IRQ_SUPPLE_0_POS			24
#define IRQ_SUPPLE_1_POS			28

#endif				/* _BF561_IRQ_H_ */
