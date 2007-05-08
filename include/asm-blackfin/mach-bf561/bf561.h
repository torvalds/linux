/*
 * File:         include/asm-blackfin/mach-bf561/bf561.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:  SYSTEM MMR REGISTER AND MEMORY MAP FOR ADSP-BF561
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __MACH_BF561_H__
#define __MACH_BF561_H__

#define SUPPORTED_REVID		0x3

#define OFFSET_(x) ((x) & 0x0000FFFF)
#define L1_ISRAM		0xFFA00000
#define L1_ISRAM_END		0xFFA04000
#define DATA_BANKA_SRAM		0xFF800000
#define DATA_BANKA_SRAM_END	0xFF804000
#define DATA_BANKB_SRAM		0xFF900000
#define DATA_BANKB_SRAM_END	0xFF904000
#define L1_DSRAMA		0xFF800000
#define L1_DSRAMA_END		0xFF804000
#define L1_DSRAMB		0xFF900000
#define L1_DSRAMB_END		0xFF904000
#define L2_SRAM			0xFEB00000
#define L2_SRAM_END		0xFEB20000
#define AMB_FLASH		0x20000000
#define AMB_FLASH_END		0x21000000
#define AMB_FLASH_LENGTH	0x01000000
#define L1_ISRAM_LENGTH		0x4000
#define L1_DSRAMA_LENGTH	0x4000
#define L1_DSRAMB_LENGTH	0x4000
#define L2_SRAM_LENGTH		0x20000

/*some misc defines*/
#define IMASK_IVG15		0x8000
#define IMASK_IVG14		0x4000
#define IMASK_IVG13		0x2000
#define IMASK_IVG12		0x1000

#define IMASK_IVG11		0x0800
#define IMASK_IVG10		0x0400
#define IMASK_IVG9		0x0200
#define IMASK_IVG8		0x0100

#define IMASK_IVG7		0x0080
#define IMASK_IVGTMR		0x0040
#define IMASK_IVGHW		0x0020

/***************************
 * Blackfin Cache setup
 */


#define BLKFIN_ISUBBANKS	4
#define BLKFIN_IWAYS		4
#define BLKFIN_ILINES		32

#define BLKFIN_DSUBBANKS	4
#define BLKFIN_DWAYS		2
#define BLKFIN_DLINES		64

#define WAY0_L			0x1
#define WAY1_L			0x2
#define WAY01_L			0x3
#define WAY2_L			0x4
#define WAY02_L			0x5
#define	WAY12_L			0x6
#define	WAY012_L		0x7

#define	WAY3_L			0x8
#define	WAY03_L			0x9
#define	WAY13_L			0xA
#define	WAY013_L		0xB

#define	WAY32_L			0xC
#define	WAY320_L		0xD
#define	WAY321_L		0xE
#define	WAYALL_L		0xF

#define DMC_ENABLE (2<<2)	/*yes, 2, not 1 */

/* IAR0 BIT FIELDS */
#define	PLL_WAKEUP_BIT		0xFFFFFFFF
#define	DMA1_ERROR_BIT		0xFFFFFF0F
#define	DMA2_ERROR_BIT		0xFFFFF0FF
#define IMDMA_ERROR_BIT		0xFFFF0FFF
#define	PPI1_ERROR_BIT		0xFFF0FFFF
#define	PPI2_ERROR_BIT		0xFF0FFFFF
#define	SPORT0_ERROR_BIT	0xF0FFFFFF
#define	SPORT1_ERROR_BIT	0x0FFFFFFF
/* IAR1 BIT FIELDS */
#define	SPI_ERROR_BIT		0xFFFFFFFF
#define	UART_ERROR_BIT		0xFFFFFF0F
#define RESERVED_ERROR_BIT	0xFFFFF0FF
#define	DMA1_0_BIT		0xFFFF0FFF
#define	DMA1_1_BIT		0xFFF0FFFF
#define	DMA1_2_BIT		0xFF0FFFFF
#define	DMA1_3_BIT		0xF0FFFFFF
#define	DMA1_4_BIT		0x0FFFFFFF
/* IAR2 BIT FIELDS */
#define	DMA1_5_BIT		0xFFFFFFFF
#define	DMA1_6_BIT		0xFFFFFF0F
#define	DMA1_7_BIT		0xFFFFF0FF
#define	DMA1_8_BIT		0xFFFF0FFF
#define	DMA1_9_BIT		0xFFF0FFFF
#define	DMA1_10_BIT		0xFF0FFFFF
#define	DMA1_11_BIT		0xF0FFFFFF
#define	DMA2_0_BIT		0x0FFFFFFF
/* IAR3 BIT FIELDS */
#define	DMA2_1_BIT		0xFFFFFFFF
#define	DMA2_2_BIT		0xFFFFFF0F
#define	DMA2_3_BIT		0xFFFFF0FF
#define	DMA2_4_BIT		0xFFFF0FFF
#define	DMA2_5_BIT		0xFFF0FFFF
#define	DMA2_6_BIT		0xFF0FFFFF
#define	DMA2_7_BIT		0xF0FFFFFF
#define	DMA2_8_BIT		0x0FFFFFFF
/* IAR4 BIT FIELDS */
#define	DMA2_9_BIT		0xFFFFFFFF
#define	DMA2_10_BIT             0xFFFFFF0F
#define	DMA2_11_BIT             0xFFFFF0FF
#define TIMER0_BIT	        0xFFFF0FFF
#define TIMER1_BIT              0xFFF0FFFF
#define TIMER2_BIT              0xFF0FFFFF
#define TIMER3_BIT              0xF0FFFFFF
#define TIMER4_BIT              0x0FFFFFFF
/* IAR5 BIT FIELDS */
#define TIMER5_BIT		0xFFFFFFFF
#define TIMER6_BIT              0xFFFFFF0F
#define TIMER7_BIT              0xFFFFF0FF
#define TIMER8_BIT              0xFFFF0FFF
#define TIMER9_BIT              0xFFF0FFFF
#define TIMER10_BIT             0xFF0FFFFF
#define TIMER11_BIT             0xF0FFFFFF
#define	PROG0_INTA_BIT	        0x0FFFFFFF
/* IAR6 BIT FIELDS */
#define	PROG0_INTB_BIT		0xFFFFFFFF
#define	PROG1_INTA_BIT          0xFFFFFF0F
#define	PROG1_INTB_BIT          0xFFFFF0FF
#define	PROG2_INTA_BIT          0xFFFF0FFF
#define	PROG2_INTB_BIT          0xFFF0FFFF
#define DMA1_WRRD0_BIT          0xFF0FFFFF
#define DMA1_WRRD1_BIT          0xF0FFFFFF
#define DMA2_WRRD0_BIT          0x0FFFFFFF
/* IAR7 BIT FIELDS */
#define DMA2_WRRD1_BIT		0xFFFFFFFF
#define IMDMA_WRRD0_BIT         0xFFFFFF0F
#define IMDMA_WRRD1_BIT         0xFFFFF0FF
#define	WATCH_BIT	        0xFFFF0FFF
#define RESERVED_1_BIT	        0xFFF0FFFF
#define RESERVED_2_BIT	        0xFF0FFFFF
#define SUPPLE_0_BIT	        0xF0FFFFFF
#define SUPPLE_1_BIT	        0x0FFFFFFF

/* Miscellaneous Values */

/****************************** EBIU Settings ********************************/
#define AMBCTL0VAL	((CONFIG_BANK_1 << 16) | CONFIG_BANK_0)
#define AMBCTL1VAL	((CONFIG_BANK_3 << 16) | CONFIG_BANK_2)

#if defined(CONFIG_C_AMBEN_ALL)
#define V_AMBEN AMBEN_ALL
#elif defined(CONFIG_C_AMBEN)
#define V_AMBEN 0x0
#elif defined(CONFIG_C_AMBEN_B0)
#define V_AMBEN AMBEN_B0
#elif defined(CONFIG_C_AMBEN_B0_B1)
#define V_AMBEN AMBEN_B0_B1
#elif defined(CONFIG_C_AMBEN_B0_B1_B2)
#define V_AMBEN AMBEN_B0_B1_B2
#endif

#ifdef CONFIG_C_AMCKEN
#define V_AMCKEN AMCKEN
#else
#define V_AMCKEN 0x0
#endif

#ifdef CONFIG_C_B0PEN
#define V_B0PEN 0x10
#else
#define V_B0PEN 0x00
#endif

#ifdef CONFIG_C_B1PEN
#define V_B1PEN 0x20
#else
#define V_B1PEN 0x00
#endif

#ifdef CONFIG_C_B2PEN
#define V_B2PEN 0x40
#else
#define V_B2PEN 0x00
#endif

#ifdef CONFIG_C_B3PEN
#define V_B3PEN 0x80
#else
#define V_B3PEN 0x00
#endif

#ifdef CONFIG_C_CDPRIO
#define V_CDPRIO 0x100
#else
#define V_CDPRIO 0x0
#endif

#define AMGCTLVAL	(V_AMBEN | V_AMCKEN | V_CDPRIO | V_B0PEN | V_B1PEN | V_B2PEN | V_B3PEN | 0x0002)

#define MAX_VC	600000000
#define MIN_VC	50000000

/******************************* PLL Settings ********************************/
#ifdef CONFIG_BFIN_KERNEL_CLOCK
#if (CONFIG_VCO_MULT < 0)
#error "VCO Multiplier is less than 0. Please select a different value"
#endif

#if (CONFIG_VCO_MULT == 0)
#error "VCO Multiplier should be greater than 0. Please select a different value"
#endif

#ifndef CONFIG_CLKIN_HALF
#define CONFIG_VCO_HZ	(CONFIG_CLKIN_HZ * CONFIG_VCO_MULT)
#else
#define CONFIG_VCO_HZ	((CONFIG_CLKIN_HZ * CONFIG_VCO_MULT)/2)
#endif

#ifndef CONFIG_PLL_BYPASS
#define CONFIG_CCLK_HZ	(CONFIG_VCO_HZ/CONFIG_CCLK_DIV)
#define CONFIG_SCLK_HZ	(CONFIG_VCO_HZ/CONFIG_SCLK_DIV)
#else
#define CONFIG_CCLK_HZ	CONFIG_CLKIN_HZ
#define CONFIG_SCLK_HZ	CONFIG_CLKIN_HZ
#endif

#if (CONFIG_SCLK_DIV < 1)
#error "SCLK DIV cannot be less than 1 or more than 15. Please select a proper value"
#endif

#if (CONFIG_SCLK_DIV > 15)
#error "SCLK DIV cannot be less than 1 or more than 15. Please select a proper value"
#endif

#if (CONFIG_CCLK_DIV != 1)
#if (CONFIG_CCLK_DIV != 2)
#if (CONFIG_CCLK_DIV != 4)
#if (CONFIG_CCLK_DIV != 8)
#error "CCLK DIV can be 1,2,4 or 8 only. Please select a proper value"
#endif
#endif
#endif
#endif

#if (CONFIG_VCO_HZ > MAX_VC)
#error "VCO selected is more than maximum value. Please change the VCO multipler"
#endif

#if (CONFIG_SCLK_HZ > 133000000)
#error "Sclk value selected is more than maximum. Please select a proper value for SCLK multiplier"
#endif

#if (CONFIG_SCLK_HZ < 27000000)
#error "Sclk value selected is less than minimum. Please select a proper value for SCLK multiplier"
#endif

#if (CONFIG_SCLK_HZ >= CONFIG_CCLK_HZ)
#if (CONFIG_SCLK_HZ != CONFIG_CLKIN_HZ)
#if (CONFIG_CCLK_HZ != CONFIG_CLKIN_HZ)
#error "Please select sclk less than cclk"
#endif
#endif
#endif

#if (CONFIG_CCLK_DIV == 1)
#define CONFIG_CCLK_ACT_DIV   CCLK_DIV1
#endif
#if (CONFIG_CCLK_DIV == 2)
#define CONFIG_CCLK_ACT_DIV   CCLK_DIV2
#endif
#if (CONFIG_CCLK_DIV == 4)
#define CONFIG_CCLK_ACT_DIV   CCLK_DIV4
#endif
#if (CONFIG_CCLK_DIV == 8)
#define CONFIG_CCLK_ACT_DIV   CCLK_DIV8
#endif
#ifndef CONFIG_CCLK_ACT_DIV
#define CONFIG_CCLK_ACT_DIV   CONFIG_CCLK_DIV_not_defined_properly
#endif

#if defined(ANOMALY_05000273) && (CONFIG_CCLK_DIV == 1)
#error ANOMALY 05000273, please make sure CCLK is at least 2x SCLK
#endif

#endif				/* CONFIG_BFIN_KERNEL_CLOCK */

#ifdef CONFIG_BF561
#define CPU "BF561"
#define CPUID 0x027bb000
#endif
#ifndef CPU
#define CPU "UNKNOWN"
#define CPUID 0x0
#endif

#if (CONFIG_MEM_SIZE % 4)
#error "SDRAM memory size must be a multiple of 4MB!"
#endif
#define SDRAM_IGENERIC    (CPLB_L1_CHBL | CPLB_USER_RD | CPLB_VALID | CPLB_PORTPRIO)
#define SDRAM_IKERNEL     (SDRAM_IGENERIC | CPLB_LOCK)
#define L1_IMEMORY        (               CPLB_USER_RD | CPLB_VALID | CPLB_LOCK)
#define SDRAM_INON_CHBL   (               CPLB_USER_RD | CPLB_VALID)

/*Use the menuconfig cache policy here - CONFIG_BLKFIN_WT/CONFIG_BLKFIN_WB*/

#define ANOMALY_05000158_WORKAROUND		0x200
#ifdef CONFIG_BLKFIN_WB		/*Write Back Policy */
#define SDRAM_DGENERIC   (CPLB_L1_CHBL | CPLB_DIRTY \
			| CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND)
#else				/*Write Through */
#define SDRAM_DGENERIC   (CPLB_L1_CHBL | CPLB_WT | CPLB_L1_AOW | CPLB_DIRTY \
			| CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND)
#endif


#define L1_DMEMORY       (CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND | CPLB_LOCK | CPLB_DIRTY)
#define SDRAM_DNON_CHBL  (CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND | CPLB_DIRTY)
#define SDRAM_EBIU       (CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND | CPLB_DIRTY)
#define SDRAM_OOPS  	 (CPLB_VALID | ANOMALY_05000158_WORKAROUND | CPLB_LOCK | CPLB_DIRTY)

#define L2_MEMORY	(CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND | CPLB_DIRTY)

#define SIZE_1K 0x00000400	/* 1K */
#define SIZE_4K 0x00001000	/* 4K */
#define SIZE_1M 0x00100000	/* 1M */
#define SIZE_4M 0x00400000	/* 4M */

#define MAX_CPLBS (16 * 2)

/*
* Number of required data CPLB switchtable entries
* MEMSIZE / 4 (we mostly install 4M page size CPLBs
* approx 16 for smaller 1MB page size CPLBs for allignment purposes
* 1 for L1 Data Memory
* 1 for L2 Data Memory
* 1 for CONFIG_DEBUG_HUNT_FOR_ZERO
* 64 for ASYNC Memory
*/


#define MAX_SWITCH_D_CPLBS (((CONFIG_MEM_SIZE / 4) + 16 + 1 + 1 + 1 + 64) * 2)

/*
* Number of required instruction CPLB switchtable entries
* MEMSIZE / 4 (we mostly install 4M page size CPLBs
* approx 12 for smaller 1MB page size CPLBs for allignment purposes
* 1 for L1 Instruction Memory
* 1 for L2 Instruction Memory
* 1 for CONFIG_DEBUG_HUNT_FOR_ZERO
*/

#define MAX_SWITCH_I_CPLBS (((CONFIG_MEM_SIZE / 4) + 12 + 1 + 1 + 1) * 2)

#if 0				/* comment by mhfan */
/* Event Vector Table Address */
#define EVT_EMULATION_ADDR      0xffe02000
#define EVT_RESET_ADDR          0xffe02004
#define EVT_NMI_ADDR            0xffe02008
#define EVT_EXCEPTION_ADDR      0xffe0200c
#define EVT_GLOBAL_INT_ENB_ADDR 0xffe02010
#define EVT_HARDWARE_ERROR_ADDR 0xffe02014
#define EVT_TIMER_ADDR          0xffe02018
#define EVT_IVG7_ADDR           0xffe0201c
#define EVT_IVG8_ADDR           0xffe02020
#define EVT_IVG9_ADDR           0xffe02024
#define EVT_IVG10_ADDR          0xffe02028
#define EVT_IVG11_ADDR          0xffe0202c
#define EVT_IVG12_ADDR          0xffe02030
#define EVT_IVG13_ADDR          0xffe02034
#define EVT_IVG14_ADDR          0xffe02038
#define EVT_IVG15_ADDR          0xffe0203c
#define EVT_OVERRIDE_ADDR       0xffe02100
#endif				/* comment by mhfan */

#endif				/* __MACH_BF561_H__  */
