/****************************************************************************/
/*
 *  linux/include/asm-arm/arch-l7200/pmu.h
 *
 *   Registers and  helper functions for the L7200 Link-Up Systems
 *   Power Management Unit (PMU).
 *
 *   (C) Copyright 2000, S A McConnell  (samcconn@cotw.com)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/****************************************************************************/

#define PMU_OFF   0x00050000  /* Offset from IO_START to the PMU registers. */

/* IO_START and IO_BASE are defined in hardware.h */

#define PMU_START (IO_START + PMU_OFF)  /* Physical addr. of the PMU reg. */
#define PMU_BASE  (IO_BASE  + PMU_OFF)  /* Virtual addr. of the PMU reg. */


/* Define the PMU registers for use by device drivers and the kernel. */

typedef struct {
     unsigned int CURRENT;  /* Current configuration register */
     unsigned int NEXT;     /* Next configuration register */
     unsigned int reserved;
     unsigned int RUN;      /* Run configuration register */
     unsigned int COMM;     /* Configuration command register */
     unsigned int SDRAM;    /* SDRAM configuration bypass register */
} pmu_interface;

#define PMU ((volatile pmu_interface *)(PMU_BASE))


/* Macro's for reading the common register fields. */

#define GET_TRANSOP(reg)  ((reg >> 25) & 0x03) /* Bits 26-25 */
#define GET_OSCEN(reg)    ((reg >> 16) & 0x01)
#define GET_OSCMUX(reg)   ((reg >> 15) & 0x01)
#define GET_PLLMUL(reg)   ((reg >>  9) & 0x3f) /* Bits 14-9 */
#define GET_PLLEN(reg)    ((reg >>  8) & 0x01)
#define GET_PLLMUX(reg)   ((reg >>  7) & 0x01)
#define GET_BCLK_DIV(reg) ((reg >>  3) & 0x03) /* Bits 4-3 */
#define GET_SDRB_SEL(reg) ((reg >>  2) & 0x01)
#define GET_SDRF_SEL(reg) ((reg >>  1) & 0x01)
#define GET_FASTBUS(reg)  (reg & 0x1)

/* CFG_NEXT register */

#define CFG_NEXT_CLOCKRECOVERY ((PMU->NEXT >> 18) & 0x7f)   /* Bits 24-18 */
#define CFG_NEXT_INTRET        ((PMU->NEXT >> 17) & 0x01)
#define CFG_NEXT_SDR_STOP      ((PMU->NEXT >>  6) & 0x01)
#define CFG_NEXT_SYSCLKEN      ((PMU->NEXT >>  5) & 0x01)

/* Useful field values that can be used to construct the
 * CFG_NEXT and CFG_RUN registers.
 */

#define TRANSOP_NOP      0<<25  /* NOCHANGE_NOSTALL */
#define NOCHANGE_STALL   1<<25
#define CHANGE_NOSTALL   2<<25
#define CHANGE_STALL     3<<25

#define INTRET           1<<17
#define OSCEN            1<<16
#define OSCMUX           1<<15

/* PLL frequencies */

#define PLLMUL_0         0<<9         /*  3.6864 MHz */
#define PLLMUL_1         1<<9         /*  ?????? MHz */
#define PLLMUL_5         5<<9         /*  18.432 MHz */
#define PLLMUL_10       10<<9         /*  36.864 MHz */
#define PLLMUL_18       18<<9         /*  ?????? MHz */
#define PLLMUL_20       20<<9         /*  73.728 MHz */
#define PLLMUL_32       32<<9         /*  ?????? MHz */
#define PLLMUL_35       35<<9         /* 129.024 MHz */
#define PLLMUL_36       36<<9         /*  ?????? MHz */
#define PLLMUL_39       39<<9         /*  ?????? MHz */
#define PLLMUL_40       40<<9         /* 147.456 MHz */

/* Clock recovery times */

#define CRCLOCK_1        1<<18
#define CRCLOCK_2        2<<18
#define CRCLOCK_4        4<<18
#define CRCLOCK_8        8<<18
#define CRCLOCK_16      16<<18
#define CRCLOCK_32      32<<18
#define CRCLOCK_63      63<<18
#define CRCLOCK_127    127<<18

#define PLLEN            1<<8
#define PLLMUX           1<<7
#define SDR_STOP         1<<6
#define SYSCLKEN         1<<5

#define BCLK_DIV_4       2<<3
#define BCLK_DIV_2       1<<3
#define BCLK_DIV_1       0<<3

#define SDRB_SEL         1<<2
#define SDRF_SEL         1<<1
#define FASTBUS          1<<0


/* CFG_SDRAM */

#define SDRREFFQ         1<<0  /* Only if SDRSTOPRQ is not set. */
#define SDRREFACK        1<<1  /* Read-only */
#define SDRSTOPRQ        1<<2  /* Only if SDRREFFQ is not set. */
#define SDRSTOPACK       1<<3  /* Read-only */
#define PICEN            1<<4  /* Enable Co-procesor */
#define PICTEST          1<<5

#define GET_SDRREFFQ    ((PMU->SDRAM >> 0) & 0x01)
#define GET_SDRREFACK   ((PMU->SDRAM >> 1) & 0x01) /* Read-only */
#define GET_SDRSTOPRQ   ((PMU->SDRAM >> 2) & 0x01)
#define GET_SDRSTOPACK  ((PMU->SDRAM >> 3) & 0x01) /* Read-only */
#define GET_PICEN       ((PMU->SDRAM >> 4) & 0x01)
#define GET_PICTEST     ((PMU->SDRAM >> 5) & 0x01)
