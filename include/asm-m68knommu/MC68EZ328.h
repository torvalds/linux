
/* include/asm-m68knommu/MC68EZ328.h: 'EZ328 control registers
 *
 * Copyright (C) 1999  Vladimir Gurevich <vgurevic@cisco.com>
 *                     Bear & Hare Software, Inc.
 *
 * Based on include/asm-m68knommu/MC68332.h
 * Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>,
 *                     The Silver Hammer Group, Ltd.
 *
 */

#ifndef _MC68EZ328_H_
#define _MC68EZ328_H_

#define BYTE_REF(addr) (*((volatile unsigned char*)addr))
#define WORD_REF(addr) (*((volatile unsigned short*)addr))
#define LONG_REF(addr) (*((volatile unsigned long*)addr))

#define PUT_FIELD(field, val) (((val) << field##_SHIFT) & field##_MASK)
#define GET_FIELD(reg, field) (((reg) & field##_MASK) >> field##_SHIFT)

/********** 
 *
 * 0xFFFFF0xx -- System Control
 *
 **********/
 
/*
 * System Control Register (SCR)
 */
#define SCR_ADDR	0xfffff000
#define SCR		BYTE_REF(SCR_ADDR)

#define SCR_WDTH8	0x01	/* 8-Bit Width Select */
#define SCR_DMAP	0x04	/* Double Map */
#define SCR_SO		0x08	/* Supervisor Only */
#define SCR_BETEN	0x10	/* Bus-Error Time-Out Enable */
#define SCR_PRV		0x20	/* Privilege Violation */
#define SCR_WPV		0x40	/* Write Protect Violation */
#define SCR_BETO	0x80	/* Bus-Error TimeOut */

/*
 * Silicon ID Register (Mask Revision Register (MRR) for '328 Compatibility)
 */
#define MRR_ADDR 0xfffff004
#define MRR	 LONG_REF(MRR_ADDR)

/********** 
 *
 * 0xFFFFF1xx -- Chip-Select logic
 *
 **********/
 
/*
 * Chip Select Group Base Registers 
 */
#define CSGBA_ADDR	0xfffff100
#define CSGBB_ADDR	0xfffff102

#define CSGBC_ADDR	0xfffff104
#define CSGBD_ADDR	0xfffff106

#define CSGBA		WORD_REF(CSGBA_ADDR)
#define CSGBB		WORD_REF(CSGBB_ADDR)
#define CSGBC		WORD_REF(CSGBC_ADDR)
#define CSGBD		WORD_REF(CSGBD_ADDR)

/*
 * Chip Select Registers 
 */
#define CSA_ADDR	0xfffff110
#define CSB_ADDR	0xfffff112
#define CSC_ADDR	0xfffff114
#define CSD_ADDR	0xfffff116

#define CSA		WORD_REF(CSA_ADDR)
#define CSB		WORD_REF(CSB_ADDR)
#define CSC		WORD_REF(CSC_ADDR)
#define CSD		WORD_REF(CSD_ADDR)

#define CSA_EN		0x0001		/* Chip-Select Enable */
#define CSA_SIZ_MASK	0x000e		/* Chip-Select Size */
#define CSA_SIZ_SHIFT   1
#define CSA_WS_MASK	0x0070		/* Wait State */
#define CSA_WS_SHIFT    4
#define CSA_BSW		0x0080		/* Data Bus Width */
#define CSA_FLASH	0x0100		/* FLASH Memory Support */
#define CSA_RO		0x8000		/* Read-Only */

#define CSB_EN		0x0001		/* Chip-Select Enable */
#define CSB_SIZ_MASK	0x000e		/* Chip-Select Size */
#define CSB_SIZ_SHIFT   1
#define CSB_WS_MASK	0x0070		/* Wait State */
#define CSB_WS_SHIFT    4
#define CSB_BSW		0x0080		/* Data Bus Width */
#define CSB_FLASH	0x0100		/* FLASH Memory Support */
#define CSB_UPSIZ_MASK	0x1800		/* Unprotected memory block size */
#define CSB_UPSIZ_SHIFT 11
#define CSB_ROP		0x2000		/* Readonly if protected */
#define CSB_SOP		0x4000		/* Supervisor only if protected */
#define CSB_RO		0x8000		/* Read-Only */

#define CSC_EN		0x0001		/* Chip-Select Enable */
#define CSC_SIZ_MASK	0x000e		/* Chip-Select Size */
#define CSC_SIZ_SHIFT   1
#define CSC_WS_MASK	0x0070		/* Wait State */
#define CSC_WS_SHIFT    4
#define CSC_BSW		0x0080		/* Data Bus Width */
#define CSC_FLASH	0x0100		/* FLASH Memory Support */
#define CSC_UPSIZ_MASK	0x1800		/* Unprotected memory block size */
#define CSC_UPSIZ_SHIFT 11
#define CSC_ROP		0x2000		/* Readonly if protected */
#define CSC_SOP		0x4000		/* Supervisor only if protected */
#define CSC_RO		0x8000		/* Read-Only */

#define CSD_EN		0x0001		/* Chip-Select Enable */
#define CSD_SIZ_MASK	0x000e		/* Chip-Select Size */
#define CSD_SIZ_SHIFT   1
#define CSD_WS_MASK	0x0070		/* Wait State */
#define CSD_WS_SHIFT    4
#define CSD_BSW		0x0080		/* Data Bus Width */
#define CSD_FLASH	0x0100		/* FLASH Memory Support */
#define CSD_DRAM	0x0200		/* Dram Selection */
#define	CSD_COMB	0x0400		/* Combining */
#define CSD_UPSIZ_MASK	0x1800		/* Unprotected memory block size */
#define CSD_UPSIZ_SHIFT 11
#define CSD_ROP		0x2000		/* Readonly if protected */
#define CSD_SOP		0x4000		/* Supervisor only if protected */
#define CSD_RO		0x8000		/* Read-Only */

/*
 * Emulation Chip-Select Register 
 */
#define EMUCS_ADDR	0xfffff118
#define EMUCS		WORD_REF(EMUCS_ADDR)

#define EMUCS_WS_MASK	0x0070
#define EMUCS_WS_SHIFT	4

/********** 
 *
 * 0xFFFFF2xx -- Phase Locked Loop (PLL) & Power Control
 *
 **********/

/*
 * PLL Control Register 
 */
#define PLLCR_ADDR	0xfffff200
#define PLLCR		WORD_REF(PLLCR_ADDR)

#define PLLCR_DISPLL	       0x0008	/* Disable PLL */
#define PLLCR_CLKEN	       0x0010	/* Clock (CLKO pin) enable */
#define PLLCR_PRESC	       0x0020	/* VCO prescaler */
#define PLLCR_SYSCLK_SEL_MASK  0x0700	/* System Clock Selection */
#define PLLCR_SYSCLK_SEL_SHIFT 8
#define PLLCR_LCDCLK_SEL_MASK  0x3800	/* LCD Clock Selection */
#define PLLCR_LCDCLK_SEL_SHIFT 11

/* '328-compatible definitions */
#define PLLCR_PIXCLK_SEL_MASK	PLLCR_LCDCLK_SEL_MASK
#define PLLCR_PIXCLK_SEL_SHIFT	PLLCR_LCDCLK_SEL_SHIFT

/*
 * PLL Frequency Select Register
 */
#define PLLFSR_ADDR	0xfffff202
#define PLLFSR		WORD_REF(PLLFSR_ADDR)

#define PLLFSR_PC_MASK	0x00ff		/* P Count */
#define PLLFSR_PC_SHIFT 0
#define PLLFSR_QC_MASK	0x0f00		/* Q Count */
#define PLLFSR_QC_SHIFT 8
#define PLLFSR_PROT	0x4000		/* Protect P & Q */
#define PLLFSR_CLK32	0x8000		/* Clock 32 (kHz) */

/*
 * Power Control Register
 */
#define PCTRL_ADDR	0xfffff207
#define PCTRL		BYTE_REF(PCTRL_ADDR)

#define PCTRL_WIDTH_MASK	0x1f	/* CPU Clock bursts width */
#define PCTRL_WIDTH_SHIFT	0
#define PCTRL_PCEN		0x80	/* Power Control Enable */

/**********
 *
 * 0xFFFFF3xx -- Interrupt Controller
 *
 **********/

/* 
 * Interrupt Vector Register
 */
#define IVR_ADDR	0xfffff300
#define IVR		BYTE_REF(IVR_ADDR)

#define IVR_VECTOR_MASK 0xF8

/*
 * Interrupt control Register
 */
#define ICR_ADDR	0xfffff302
#define ICR		WORD_REF(ICR_ADDR)

#define ICR_POL5	0x0080	/* Polarity Control for IRQ5 */
#define ICR_ET6		0x0100	/* Edge Trigger Select for IRQ6 */
#define ICR_ET3		0x0200	/* Edge Trigger Select for IRQ3 */
#define ICR_ET2		0x0400	/* Edge Trigger Select for IRQ2 */
#define ICR_ET1		0x0800	/* Edge Trigger Select for IRQ1 */
#define ICR_POL6	0x1000	/* Polarity Control for IRQ6 */
#define ICR_POL3	0x2000	/* Polarity Control for IRQ3 */
#define ICR_POL2	0x4000	/* Polarity Control for IRQ2 */
#define ICR_POL1	0x8000	/* Polarity Control for IRQ1 */

/*
 * Interrupt Mask Register
 */
#define IMR_ADDR	0xfffff304
#define IMR		LONG_REF(IMR_ADDR)

/*
 * Define the names for bit positions first. This is useful for 
 * request_irq
 */
#define SPI_IRQ_NUM	0	/* SPI interrupt */
#define TMR_IRQ_NUM	1	/* Timer interrupt */
#define UART_IRQ_NUM	2	/* UART interrupt */	
#define	WDT_IRQ_NUM	3	/* Watchdog Timer interrupt */
#define RTC_IRQ_NUM	4	/* RTC interrupt */
#define	KB_IRQ_NUM	6	/* Keyboard Interrupt */
#define PWM_IRQ_NUM	7	/* Pulse-Width Modulator int. */
#define	INT0_IRQ_NUM	8	/* External INT0 */
#define	INT1_IRQ_NUM	9	/* External INT1 */
#define	INT2_IRQ_NUM	10	/* External INT2 */
#define	INT3_IRQ_NUM	11	/* External INT3 */
#define IRQ1_IRQ_NUM	16	/* IRQ1 */
#define IRQ2_IRQ_NUM	17	/* IRQ2 */
#define IRQ3_IRQ_NUM	18	/* IRQ3 */
#define IRQ6_IRQ_NUM	19	/* IRQ6 */
#define IRQ5_IRQ_NUM	20	/* IRQ5 */
#define SAM_IRQ_NUM	22	/* Sampling Timer for RTC */
#define EMIQ_IRQ_NUM	23	/* Emulator Interrupt */

/* '328-compatible definitions */
#define SPIM_IRQ_NUM	SPI_IRQ_NUM
#define TMR1_IRQ_NUM	TMR_IRQ_NUM

/* 
 * Here go the bitmasks themselves
 */
#define IMR_MSPI 	(1 << SPI_IRQ_NUM)	/* Mask SPI interrupt */
#define	IMR_MTMR	(1 << TMR_IRQ_NUM)	/* Mask Timer interrupt */
#define IMR_MUART	(1 << UART_IRQ_NUM)	/* Mask UART interrupt */	
#define	IMR_MWDT	(1 << WDT_IRQ_NUM)	/* Mask Watchdog Timer interrupt */
#define IMR_MRTC	(1 << RTC_IRQ_NUM)	/* Mask RTC interrupt */
#define	IMR_MKB		(1 << KB_IRQ_NUM)	/* Mask Keyboard Interrupt */
#define IMR_MPWM	(1 << PWM_IRQ_NUM)	/* Mask Pulse-Width Modulator int. */
#define	IMR_MINT0	(1 << INT0_IRQ_NUM)	/* Mask External INT0 */
#define	IMR_MINT1	(1 << INT1_IRQ_NUM)	/* Mask External INT1 */
#define	IMR_MINT2	(1 << INT2_IRQ_NUM)	/* Mask External INT2 */
#define	IMR_MINT3	(1 << INT3_IRQ_NUM)	/* Mask External INT3 */
#define IMR_MIRQ1	(1 << IRQ1_IRQ_NUM)	/* Mask IRQ1 */
#define IMR_MIRQ2	(1 << IRQ2_IRQ_NUM)	/* Mask IRQ2 */
#define IMR_MIRQ3	(1 << IRQ3_IRQ_NUM)	/* Mask IRQ3 */
#define IMR_MIRQ6	(1 << IRQ6_IRQ_NUM)	/* Mask IRQ6 */
#define IMR_MIRQ5	(1 << IRQ5_IRQ_NUM)	/* Mask IRQ5 */
#define IMR_MSAM	(1 << SAM_IRQ_NUM)	/* Mask Sampling Timer for RTC */
#define IMR_MEMIQ	(1 << EMIQ_IRQ_NUM)	/* Mask Emulator Interrupt */

/* '328-compatible definitions */
#define IMR_MSPIM	IMR_MSPI
#define IMR_MTMR1	IMR_MTMR

/* 
 * Interrupt Status Register 
 */
#define ISR_ADDR	0xfffff30c
#define ISR		LONG_REF(ISR_ADDR)

#define ISR_SPI 	(1 << SPI_IRQ_NUM)	/* SPI interrupt */
#define	ISR_TMR		(1 << TMR_IRQ_NUM)	/* Timer interrupt */
#define ISR_UART	(1 << UART_IRQ_NUM)	/* UART interrupt */	
#define	ISR_WDT		(1 << WDT_IRQ_NUM)	/* Watchdog Timer interrupt */
#define ISR_RTC		(1 << RTC_IRQ_NUM)	/* RTC interrupt */
#define	ISR_KB		(1 << KB_IRQ_NUM)	/* Keyboard Interrupt */
#define ISR_PWM		(1 << PWM_IRQ_NUM)	/* Pulse-Width Modulator interrupt */
#define	ISR_INT0	(1 << INT0_IRQ_NUM)	/* External INT0 */
#define	ISR_INT1	(1 << INT1_IRQ_NUM)	/* External INT1 */
#define	ISR_INT2	(1 << INT2_IRQ_NUM)	/* External INT2 */
#define	ISR_INT3	(1 << INT3_IRQ_NUM)	/* External INT3 */
#define ISR_IRQ1	(1 << IRQ1_IRQ_NUM)	/* IRQ1 */
#define ISR_IRQ2	(1 << IRQ2_IRQ_NUM)	/* IRQ2 */
#define ISR_IRQ3	(1 << IRQ3_IRQ_NUM)	/* IRQ3 */
#define ISR_IRQ6	(1 << IRQ6_IRQ_NUM)	/* IRQ6 */
#define ISR_IRQ5	(1 << IRQ5_IRQ_NUM)	/* IRQ5 */
#define ISR_SAM		(1 << SAM_IRQ_NUM)	/* Sampling Timer for RTC */
#define ISR_EMIQ	(1 << EMIQ_IRQ_NUM)	/* Emulator Interrupt */

/* '328-compatible definitions */
#define ISR_SPIM	ISR_SPI
#define ISR_TMR1	ISR_TMR

/* 
 * Interrupt Pending Register 
 */
#define IPR_ADDR	0xfffff30c
#define IPR		LONG_REF(IPR_ADDR)

#define IPR_SPI 	(1 << SPI_IRQ_NUM)	/* SPI interrupt */
#define	IPR_TMR		(1 << TMR_IRQ_NUM)	/* Timer interrupt */
#define IPR_UART	(1 << UART_IRQ_NUM)	/* UART interrupt */	
#define	IPR_WDT		(1 << WDT_IRQ_NUM)	/* Watchdog Timer interrupt */
#define IPR_RTC		(1 << RTC_IRQ_NUM)	/* RTC interrupt */
#define	IPR_KB		(1 << KB_IRQ_NUM)	/* Keyboard Interrupt */
#define IPR_PWM		(1 << PWM_IRQ_NUM)	/* Pulse-Width Modulator interrupt */
#define	IPR_INT0	(1 << INT0_IRQ_NUM)	/* External INT0 */
#define	IPR_INT1	(1 << INT1_IRQ_NUM)	/* External INT1 */
#define	IPR_INT2	(1 << INT2_IRQ_NUM)	/* External INT2 */
#define	IPR_INT3	(1 << INT3_IRQ_NUM)	/* External INT3 */
#define IPR_IRQ1	(1 << IRQ1_IRQ_NUM)	/* IRQ1 */
#define IPR_IRQ2	(1 << IRQ2_IRQ_NUM)	/* IRQ2 */
#define IPR_IRQ3	(1 << IRQ3_IRQ_NUM)	/* IRQ3 */
#define IPR_IRQ6	(1 << IRQ6_IRQ_NUM)	/* IRQ6 */
#define IPR_IRQ5	(1 << IRQ5_IRQ_NUM)	/* IRQ5 */
#define IPR_SAM		(1 << SAM_IRQ_NUM)	/* Sampling Timer for RTC */
#define IPR_EMIQ	(1 << EMIQ_IRQ_NUM)	/* Emulator Interrupt */

/* '328-compatible definitions */
#define IPR_SPIM	IPR_SPI
#define IPR_TMR1	IPR_TMR

/**********
 *
 * 0xFFFFF4xx -- Parallel Ports
 *
 **********/

/*
 * Port A
 */
#define PADIR_ADDR	0xfffff400		/* Port A direction reg */
#define PADATA_ADDR	0xfffff401		/* Port A data register */
#define PAPUEN_ADDR	0xfffff402		/* Port A Pull-Up enable reg */

#define PADIR		BYTE_REF(PADIR_ADDR)
#define PADATA		BYTE_REF(PADATA_ADDR)
#define PAPUEN		BYTE_REF(PAPUEN_ADDR)

#define PA(x)		(1 << (x))

/* 
 * Port B
 */
#define PBDIR_ADDR	0xfffff408		/* Port B direction reg */
#define PBDATA_ADDR	0xfffff409		/* Port B data register */
#define PBPUEN_ADDR	0xfffff40a		/* Port B Pull-Up enable reg */
#define PBSEL_ADDR	0xfffff40b		/* Port B Select Register */

#define PBDIR		BYTE_REF(PBDIR_ADDR)
#define PBDATA		BYTE_REF(PBDATA_ADDR)
#define PBPUEN		BYTE_REF(PBPUEN_ADDR)
#define PBSEL		BYTE_REF(PBSEL_ADDR)

#define PB(x)		(1 << (x))

#define PB_CSB0		0x01	/* Use CSB0      as PB[0] */
#define PB_CSB1		0x02	/* Use CSB1      as PB[1] */
#define PB_CSC0_RAS0	0x04    /* Use CSC0/RAS0 as PB[2] */	
#define PB_CSC1_RAS1	0x08    /* Use CSC1/RAS1 as PB[3] */	
#define PB_CSD0_CAS0	0x10    /* Use CSD0/CAS0 as PB[4] */	
#define PB_CSD1_CAS1	0x20    /* Use CSD1/CAS1 as PB[5] */
#define PB_TIN_TOUT	0x40	/* Use TIN/TOUT  as PB[6] */
#define PB_PWMO		0x80	/* Use PWMO      as PB[7] */

/* 
 * Port C
 */
#define PCDIR_ADDR	0xfffff410		/* Port C direction reg */
#define PCDATA_ADDR	0xfffff411		/* Port C data register */
#define PCPDEN_ADDR	0xfffff412		/* Port C Pull-Down enb. reg */
#define PCSEL_ADDR	0xfffff413		/* Port C Select Register */

#define PCDIR		BYTE_REF(PCDIR_ADDR)
#define PCDATA		BYTE_REF(PCDATA_ADDR)
#define PCPDEN		BYTE_REF(PCPDEN_ADDR)
#define PCSEL		BYTE_REF(PCSEL_ADDR)

#define PC(x)		(1 << (x))

#define PC_LD0		0x01	/* Use LD0  as PC[0] */
#define PC_LD1		0x02	/* Use LD1  as PC[1] */
#define PC_LD2		0x04	/* Use LD2  as PC[2] */
#define PC_LD3		0x08	/* Use LD3  as PC[3] */
#define PC_LFLM		0x10	/* Use LFLM as PC[4] */
#define PC_LLP 		0x20	/* Use LLP  as PC[5] */
#define PC_LCLK		0x40	/* Use LCLK as PC[6] */
#define PC_LACD		0x80	/* Use LACD as PC[7] */

/* 
 * Port D
 */
#define PDDIR_ADDR	0xfffff418		/* Port D direction reg */
#define PDDATA_ADDR	0xfffff419		/* Port D data register */
#define PDPUEN_ADDR	0xfffff41a		/* Port D Pull-Up enable reg */
#define PDSEL_ADDR	0xfffff41b		/* Port D Select Register */
#define PDPOL_ADDR	0xfffff41c		/* Port D Polarity Register */
#define PDIRQEN_ADDR	0xfffff41d		/* Port D IRQ enable register */
#define PDKBEN_ADDR	0xfffff41e		/* Port D Keyboard Enable reg */
#define	PDIQEG_ADDR	0xfffff41f		/* Port D IRQ Edge Register */

#define PDDIR		BYTE_REF(PDDIR_ADDR)
#define PDDATA		BYTE_REF(PDDATA_ADDR)
#define PDPUEN		BYTE_REF(PDPUEN_ADDR)
#define PDSEL		BYTE_REF(PDSEL_ADDR)
#define	PDPOL		BYTE_REF(PDPOL_ADDR)
#define PDIRQEN		BYTE_REF(PDIRQEN_ADDR)
#define PDKBEN		BYTE_REF(PDKBEN_ADDR)
#define PDIQEG		BYTE_REF(PDIQEG_ADDR)

#define PD(x)		(1 << (x))

#define PD_INT0		0x01	/* Use INT0 as PD[0] */
#define PD_INT1		0x02	/* Use INT1 as PD[1] */
#define PD_INT2		0x04	/* Use INT2 as PD[2] */
#define PD_INT3		0x08	/* Use INT3 as PD[3] */
#define PD_IRQ1		0x10	/* Use IRQ1 as PD[4] */
#define PD_IRQ2		0x20	/* Use IRQ2 as PD[5] */
#define PD_IRQ3		0x40	/* Use IRQ3 as PD[6] */
#define PD_IRQ6		0x80	/* Use IRQ6 as PD[7] */

/* 
 * Port E
 */
#define PEDIR_ADDR	0xfffff420		/* Port E direction reg */
#define PEDATA_ADDR	0xfffff421		/* Port E data register */
#define PEPUEN_ADDR	0xfffff422		/* Port E Pull-Up enable reg */
#define PESEL_ADDR	0xfffff423		/* Port E Select Register */

#define PEDIR		BYTE_REF(PEDIR_ADDR)
#define PEDATA		BYTE_REF(PEDATA_ADDR)
#define PEPUEN		BYTE_REF(PEPUEN_ADDR)
#define PESEL		BYTE_REF(PESEL_ADDR)

#define PE(x)		(1 << (x))

#define PE_SPMTXD	0x01	/* Use SPMTXD as PE[0] */
#define PE_SPMRXD	0x02	/* Use SPMRXD as PE[1] */
#define PE_SPMCLK	0x04	/* Use SPMCLK as PE[2] */
#define PE_DWE		0x08	/* Use DWE    as PE[3] */
#define PE_RXD		0x10	/* Use RXD    as PE[4] */
#define PE_TXD		0x20	/* Use TXD    as PE[5] */
#define PE_RTS		0x40	/* Use RTS    as PE[6] */
#define PE_CTS		0x80	/* Use CTS    as PE[7] */

/* 
 * Port F
 */
#define PFDIR_ADDR	0xfffff428		/* Port F direction reg */
#define PFDATA_ADDR	0xfffff429		/* Port F data register */
#define PFPUEN_ADDR	0xfffff42a		/* Port F Pull-Up enable reg */
#define PFSEL_ADDR	0xfffff42b		/* Port F Select Register */

#define PFDIR		BYTE_REF(PFDIR_ADDR)
#define PFDATA		BYTE_REF(PFDATA_ADDR)
#define PFPUEN		BYTE_REF(PFPUEN_ADDR)
#define PFSEL		BYTE_REF(PFSEL_ADDR)

#define PF(x)		(1 << (x))

#define PF_LCONTRAST	0x01	/* Use LCONTRAST as PF[0] */
#define PF_IRQ5         0x02    /* Use IRQ5      as PF[1] */
#define PF_CLKO         0x04    /* Use CLKO      as PF[2] */
#define PF_A20          0x08    /* Use A20       as PF[3] */
#define PF_A21          0x10    /* Use A21       as PF[4] */
#define PF_A22          0x20    /* Use A22       as PF[5] */
#define PF_A23          0x40    /* Use A23       as PF[6] */
#define PF_CSA1		0x80    /* Use CSA1      as PF[7] */

/* 
 * Port G
 */
#define PGDIR_ADDR	0xfffff430		/* Port G direction reg */
#define PGDATA_ADDR	0xfffff431		/* Port G data register */
#define PGPUEN_ADDR	0xfffff432		/* Port G Pull-Up enable reg */
#define PGSEL_ADDR	0xfffff433		/* Port G Select Register */

#define PGDIR		BYTE_REF(PGDIR_ADDR)
#define PGDATA		BYTE_REF(PGDATA_ADDR)
#define PGPUEN		BYTE_REF(PGPUEN_ADDR)
#define PGSEL		BYTE_REF(PGSEL_ADDR)

#define PG(x)		(1 << (x))

#define PG_BUSW_DTACK	0x01	/* Use BUSW/DTACK as PG[0] */
#define PG_A0		0x02	/* Use A0         as PG[1] */
#define PG_EMUIRQ	0x04	/* Use EMUIRQ     as PG[2] */
#define PG_HIZ_P_D	0x08	/* Use HIZ/P/D    as PG[3] */
#define PG_EMUCS        0x10	/* Use EMUCS      as PG[4] */
#define PG_EMUBRK	0x20	/* Use EMUBRK     as PG[5] */

/**********
 *
 * 0xFFFFF5xx -- Pulse-Width Modulator (PWM)
 *
 **********/

/*
 * PWM Control Register 
 */
#define PWMC_ADDR	0xfffff500
#define PWMC		WORD_REF(PWMC_ADDR)

#define PWMC_CLKSEL_MASK	0x0003	/* Clock Selection */
#define PWMC_CLKSEL_SHIFT	0
#define PWMC_REPEAT_MASK	0x000c	/* Sample Repeats */
#define PWMC_REPEAT_SHIFT	2
#define PWMC_EN			0x0010	/* Enable PWM */
#define PMNC_FIFOAV		0x0020	/* FIFO Available */
#define PWMC_IRQEN		0x0040	/* Interrupt Request Enable */
#define PWMC_IRQ		0x0080	/* Interrupt Request (FIFO empty) */
#define PWMC_PRESCALER_MASK	0x7f00	/* Incoming Clock prescaler */
#define PWMC_PRESCALER_SHIFT	8
#define PWMC_CLKSRC		0x8000	/* Clock Source Select */

/* '328-compatible definitions */
#define PWMC_PWMEN	PWMC_EN

/*
 * PWM Sample Register 
 */
#define PWMS_ADDR	0xfffff502
#define PWMS		WORD_REF(PWMS_ADDR)

/*
 * PWM Period Register
 */
#define PWMP_ADDR	0xfffff504
#define PWMP		BYTE_REF(PWMP_ADDR)

/*
 * PWM Counter Register
 */
#define PWMCNT_ADDR	0xfffff505
#define PWMCNT		BYTE_REF(PWMCNT_ADDR)

/**********
 *
 * 0xFFFFF6xx -- General-Purpose Timer
 *
 **********/

/* 
 * Timer Control register
 */
#define TCTL_ADDR	0xfffff600
#define TCTL		WORD_REF(TCTL_ADDR)

#define	TCTL_TEN		0x0001	/* Timer Enable  */
#define TCTL_CLKSOURCE_MASK 	0x000e	/* Clock Source: */
#define   TCTL_CLKSOURCE_STOP	   0x0000	/* Stop count (disabled)    */
#define   TCTL_CLKSOURCE_SYSCLK	   0x0002	/* SYSCLK to prescaler      */
#define   TCTL_CLKSOURCE_SYSCLK_16 0x0004	/* SYSCLK/16 to prescaler   */
#define   TCTL_CLKSOURCE_TIN	   0x0006	/* TIN to prescaler         */
#define   TCTL_CLKSOURCE_32KHZ	   0x0008	/* 32kHz clock to prescaler */
#define TCTL_IRQEN		0x0010	/* IRQ Enable    */
#define TCTL_OM			0x0020	/* Output Mode   */
#define TCTL_CAP_MASK		0x00c0	/* Capture Edge: */
#define	  TCTL_CAP_RE		0x0040		/* Capture on rizing edge   */
#define   TCTL_CAP_FE		0x0080		/* Capture on falling edge  */
#define TCTL_FRR		0x0010	/* Free-Run Mode */

/* '328-compatible definitions */
#define TCTL1_ADDR	TCTL_ADDR
#define TCTL1		TCTL

/*
 * Timer Prescaler Register
 */
#define TPRER_ADDR	0xfffff602
#define TPRER		WORD_REF(TPRER_ADDR)

/* '328-compatible definitions */
#define TPRER1_ADDR	TPRER_ADDR
#define TPRER1		TPRER

/*
 * Timer Compare Register
 */
#define TCMP_ADDR	0xfffff604
#define TCMP		WORD_REF(TCMP_ADDR)

/* '328-compatible definitions */
#define TCMP1_ADDR	TCMP_ADDR
#define TCMP1		TCMP

/*
 * Timer Capture register
 */
#define TCR_ADDR	0xfffff606
#define TCR		WORD_REF(TCR_ADDR)

/* '328-compatible definitions */
#define TCR1_ADDR	TCR_ADDR
#define TCR1		TCR

/*
 * Timer Counter Register
 */
#define TCN_ADDR	0xfffff608
#define TCN		WORD_REF(TCN_ADDR)

/* '328-compatible definitions */
#define TCN1_ADDR	TCN_ADDR
#define TCN1		TCN

/*
 * Timer Status Register
 */
#define TSTAT_ADDR	0xfffff60a
#define TSTAT		WORD_REF(TSTAT_ADDR)

#define TSTAT_COMP	0x0001		/* Compare Event occurred */
#define TSTAT_CAPT	0x0001		/* Capture Event occurred */

/* '328-compatible definitions */
#define TSTAT1_ADDR	TSTAT_ADDR
#define TSTAT1		TSTAT

/**********
 *
 * 0xFFFFF8xx -- Serial Periferial Interface Master (SPIM)
 *
 **********/

/*
 * SPIM Data Register
 */
#define SPIMDATA_ADDR	0xfffff800
#define SPIMDATA	WORD_REF(SPIMDATA_ADDR)

/*
 * SPIM Control/Status Register
 */
#define SPIMCONT_ADDR	0xfffff802
#define SPIMCONT	WORD_REF(SPIMCONT_ADDR)

#define SPIMCONT_BIT_COUNT_MASK	 0x000f	/* Transfer Length in Bytes */
#define SPIMCONT_BIT_COUNT_SHIFT 0
#define SPIMCONT_POL		 0x0010	/* SPMCLK Signel Polarity */
#define	SPIMCONT_PHA		 0x0020	/* Clock/Data phase relationship */
#define SPIMCONT_IRQEN		 0x0040 /* IRQ Enable */
#define SPIMCONT_IRQ		 0x0080	/* Interrupt Request */
#define SPIMCONT_XCH		 0x0100	/* Exchange */
#define SPIMCONT_ENABLE		 0x0200	/* Enable SPIM */
#define SPIMCONT_DATA_RATE_MASK	 0xe000	/* SPIM Data Rate */
#define SPIMCONT_DATA_RATE_SHIFT 13

/* '328-compatible definitions */
#define SPIMCONT_SPIMIRQ	SPIMCONT_IRQ
#define SPIMCONT_SPIMEN		SPIMCONT_ENABLE

/**********
 *
 * 0xFFFFF9xx -- UART
 *
 **********/

/*
 * UART Status/Control Register
 */
#define USTCNT_ADDR	0xfffff900
#define USTCNT		WORD_REF(USTCNT_ADDR)

#define USTCNT_TXAE	0x0001	/* Transmitter Available Interrupt Enable */
#define USTCNT_TXHE	0x0002	/* Transmitter Half Empty Enable */
#define USTCNT_TXEE	0x0004	/* Transmitter Empty Interrupt Enable */
#define USTCNT_RXRE	0x0008	/* Receiver Ready Interrupt Enable */
#define USTCNT_RXHE	0x0010	/* Receiver Half-Full Interrupt Enable */
#define USTCNT_RXFE	0x0020	/* Receiver Full Interrupt Enable */
#define USTCNT_CTSD	0x0040	/* CTS Delta Interrupt Enable */
#define USTCNT_ODEN	0x0080	/* Old Data Interrupt Enable */
#define USTCNT_8_7	0x0100	/* Eight or seven-bit transmission */
#define USTCNT_STOP	0x0200	/* Stop bit transmission */
#define USTCNT_ODD	0x0400	/* Odd Parity */
#define	USTCNT_PEN	0x0800	/* Parity Enable */
#define USTCNT_CLKM	0x1000	/* Clock Mode Select */
#define	USTCNT_TXEN	0x2000	/* Transmitter Enable */
#define USTCNT_RXEN	0x4000	/* Receiver Enable */
#define USTCNT_UEN	0x8000	/* UART Enable */

/* '328-compatible definitions */
#define USTCNT_TXAVAILEN	USTCNT_TXAE
#define USTCNT_TXHALFEN		USTCNT_TXHE
#define USTCNT_TXEMPTYEN	USTCNT_TXEE
#define USTCNT_RXREADYEN	USTCNT_RXRE
#define USTCNT_RXHALFEN		USTCNT_RXHE
#define USTCNT_RXFULLEN		USTCNT_RXFE
#define USTCNT_CTSDELTAEN	USTCNT_CTSD
#define USTCNT_ODD_EVEN		USTCNT_ODD
#define USTCNT_PARITYEN		USTCNT_PEN
#define USTCNT_CLKMODE		USTCNT_CLKM
#define USTCNT_UARTEN		USTCNT_UEN

/*
 * UART Baud Control Register
 */
#define UBAUD_ADDR	0xfffff902
#define UBAUD		WORD_REF(UBAUD_ADDR)

#define UBAUD_PRESCALER_MASK	0x003f	/* Actual divisor is 65 - PRESCALER */
#define UBAUD_PRESCALER_SHIFT	0
#define UBAUD_DIVIDE_MASK	0x0700	/* Baud Rate freq. divizor */
#define UBAUD_DIVIDE_SHIFT	8
#define UBAUD_BAUD_SRC		0x0800	/* Baud Rate Source */
#define UBAUD_UCLKDIR		0x2000	/* UCLK Direction */

/*
 * UART Receiver Register 
 */
#define URX_ADDR	0xfffff904
#define URX		WORD_REF(URX_ADDR)

#define URX_RXDATA_ADDR	0xfffff905
#define URX_RXDATA	BYTE_REF(URX_RXDATA_ADDR)

#define URX_RXDATA_MASK	 0x00ff	/* Received data */
#define URX_RXDATA_SHIFT 0
#define URX_PARITY_ERROR 0x0100	/* Parity Error */
#define URX_BREAK	 0x0200	/* Break Detected */
#define URX_FRAME_ERROR	 0x0400	/* Framing Error */
#define URX_OVRUN	 0x0800	/* Serial Overrun */
#define URX_OLD_DATA	 0x1000	/* Old data in FIFO */
#define URX_DATA_READY	 0x2000	/* Data Ready (FIFO not empty) */
#define URX_FIFO_HALF	 0x4000 /* FIFO is Half-Full */
#define URX_FIFO_FULL	 0x8000	/* FIFO is Full */

/*
 * UART Transmitter Register 
 */
#define UTX_ADDR	0xfffff906
#define UTX		WORD_REF(UTX_ADDR)

#define UTX_TXDATA_ADDR	0xfffff907
#define UTX_TXDATA	BYTE_REF(UTX_TXDATA_ADDR)

#define UTX_TXDATA_MASK	 0x00ff	/* Data to be transmitted */
#define UTX_TXDATA_SHIFT 0
#define UTX_CTS_DELTA	 0x0100	/* CTS changed */
#define UTX_CTS_STAT	 0x0200	/* CTS State */
#define	UTX_BUSY	 0x0400	/* FIFO is busy, sending a character */
#define	UTX_NOCTS	 0x0800	/* Ignore CTS */
#define UTX_SEND_BREAK	 0x1000	/* Send a BREAK */
#define UTX_TX_AVAIL	 0x2000	/* Transmit FIFO has a slot available */
#define UTX_FIFO_HALF	 0x4000	/* Transmit FIFO is half empty */
#define UTX_FIFO_EMPTY	 0x8000	/* Transmit FIFO is empty */

/* '328-compatible definitions */
#define UTX_CTS_STATUS	UTX_CTS_STAT
#define UTX_IGNORE_CTS	UTX_NOCTS

/*
 * UART Miscellaneous Register 
 */
#define UMISC_ADDR	0xfffff908
#define UMISC		WORD_REF(UMISC_ADDR)

#define UMISC_TX_POL	 0x0004	/* Transmit Polarity */
#define UMISC_RX_POL	 0x0008	/* Receive Polarity */
#define UMISC_IRDA_LOOP	 0x0010	/* IrDA Loopback Enable */
#define UMISC_IRDA_EN	 0x0020	/* Infra-Red Enable */
#define UMISC_RTS	 0x0040	/* Set RTS status */
#define UMISC_RTSCONT	 0x0080	/* Choose RTS control */
#define UMISC_IR_TEST	 0x0400	/* IRDA Test Enable */
#define UMISC_BAUD_RESET 0x0800	/* Reset Baud Rate Generation Counters */
#define UMISC_LOOP	 0x1000	/* Serial Loopback Enable */
#define UMISC_FORCE_PERR 0x2000	/* Force Parity Error */
#define UMISC_CLKSRC	 0x4000	/* Clock Source */
#define UMISC_BAUD_TEST	 0x8000	/* Enable Baud Test Mode */

/* 
 * UART Non-integer Prescaler Register
 */
#define NIPR_ADDR	0xfffff90a
#define NIPR		WORD_REF(NIPR_ADDR)

#define NIPR_STEP_VALUE_MASK	0x00ff	/* NI prescaler step value */
#define NIPR_STEP_VALUE_SHIFT	0
#define NIPR_SELECT_MASK	0x0700	/* Tap Selection */
#define NIPR_SELECT_SHIFT	8
#define NIPR_PRE_SEL		0x8000	/* Non-integer prescaler select */


/* generalization of uart control registers to support multiple ports: */
typedef volatile struct {
  volatile unsigned short int ustcnt;
  volatile unsigned short int ubaud;
  union {
    volatile unsigned short int w;
    struct {
      volatile unsigned char status;
      volatile unsigned char rxdata;
    } b;
  } urx;
  union {
    volatile unsigned short int w;
    struct {
      volatile unsigned char status;
      volatile unsigned char txdata;
    } b;
  } utx;
  volatile unsigned short int umisc;
  volatile unsigned short int nipr;
  volatile unsigned short int pad1;
  volatile unsigned short int pad2;
} m68328_uart __attribute__((packed));


/**********
 *
 * 0xFFFFFAxx -- LCD Controller
 *
 **********/

/*
 * LCD Screen Starting Address Register 
 */
#define LSSA_ADDR	0xfffffa00
#define LSSA		LONG_REF(LSSA_ADDR)

#define LSSA_SSA_MASK	0x1ffffffe	/* Bits 0 and 29-31 are reserved */

/*
 * LCD Virtual Page Width Register 
 */
#define LVPW_ADDR	0xfffffa05
#define LVPW		BYTE_REF(LVPW_ADDR)

/*
 * LCD Screen Width Register (not compatible with '328 !!!) 
 */
#define LXMAX_ADDR	0xfffffa08
#define LXMAX		WORD_REF(LXMAX_ADDR)

#define LXMAX_XM_MASK	0x02f0		/* Bits 0-3 and 10-15 are reserved */

/*
 * LCD Screen Height Register
 */
#define LYMAX_ADDR	0xfffffa0a
#define LYMAX		WORD_REF(LYMAX_ADDR)

#define LYMAX_YM_MASK	0x01ff		/* Bits 9-15 are reserved */

/*
 * LCD Cursor X Position Register
 */
#define LCXP_ADDR	0xfffffa18
#define LCXP		WORD_REF(LCXP_ADDR)

#define LCXP_CC_MASK	0xc000		/* Cursor Control */
#define   LCXP_CC_TRAMSPARENT	0x0000
#define   LCXP_CC_BLACK		0x4000
#define   LCXP_CC_REVERSED	0x8000
#define   LCXP_CC_WHITE		0xc000
#define LCXP_CXP_MASK	0x02ff		/* Cursor X position */

/*
 * LCD Cursor Y Position Register
 */
#define LCYP_ADDR	0xfffffa1a
#define LCYP		WORD_REF(LCYP_ADDR)

#define LCYP_CYP_MASK	0x01ff		/* Cursor Y Position */

/*
 * LCD Cursor Width and Heigth Register
 */
#define LCWCH_ADDR	0xfffffa1c
#define LCWCH		WORD_REF(LCWCH_ADDR)

#define LCWCH_CH_MASK	0x001f		/* Cursor Height */
#define LCWCH_CH_SHIFT	0
#define LCWCH_CW_MASK	0x1f00		/* Cursor Width */
#define LCWCH_CW_SHIFT	8

/*
 * LCD Blink Control Register
 */
#define LBLKC_ADDR	0xfffffa1f
#define LBLKC		BYTE_REF(LBLKC_ADDR)

#define LBLKC_BD_MASK	0x7f	/* Blink Divisor */
#define LBLKC_BD_SHIFT	0
#define LBLKC_BKEN	0x80	/* Blink Enabled */

/*
 * LCD Panel Interface Configuration Register 
 */
#define LPICF_ADDR	0xfffffa20
#define LPICF		BYTE_REF(LPICF_ADDR)

#define LPICF_GS_MASK	 0x03	 /* Gray-Scale Mode */
#define	  LPICF_GS_BW	   0x00
#define   LPICF_GS_GRAY_4  0x01
#define   LPICF_GS_GRAY_16 0x02
#define LPICF_PBSIZ_MASK 0x0c	/* Panel Bus Width */
#define   LPICF_PBSIZ_1	   0x00
#define   LPICF_PBSIZ_2    0x04
#define   LPICF_PBSIZ_4    0x08

/*
 * LCD Polarity Configuration Register 
 */
#define LPOLCF_ADDR	0xfffffa21
#define LPOLCF		BYTE_REF(LPOLCF_ADDR)

#define LPOLCF_PIXPOL	0x01	/* Pixel Polarity */
#define LPOLCF_LPPOL	0x02	/* Line Pulse Polarity */
#define LPOLCF_FLMPOL	0x04	/* Frame Marker Polarity */
#define LPOLCF_LCKPOL	0x08	/* LCD Shift Lock Polarity */

/*
 * LACD (LCD Alternate Crystal Direction) Rate Control Register
 */
#define LACDRC_ADDR	0xfffffa23
#define LACDRC		BYTE_REF(LACDRC_ADDR)

#define LACDRC_ACDSLT	 0x80	/* Signal Source Select */
#define LACDRC_ACD_MASK	 0x0f	/* Alternate Crystal Direction Control */
#define LACDRC_ACD_SHIFT 0

/*
 * LCD Pixel Clock Divider Register
 */
#define LPXCD_ADDR	0xfffffa25
#define LPXCD		BYTE_REF(LPXCD_ADDR)

#define	LPXCD_PCD_MASK	0x3f 	/* Pixel Clock Divider */
#define LPXCD_PCD_SHIFT	0

/*
 * LCD Clocking Control Register
 */
#define LCKCON_ADDR	0xfffffa27
#define LCKCON		BYTE_REF(LCKCON_ADDR)

#define LCKCON_DWS_MASK	 0x0f	/* Display Wait-State */
#define LCKCON_DWS_SHIFT 0
#define LCKCON_DWIDTH	 0x40	/* Display Memory Width  */
#define LCKCON_LCDON	 0x80	/* Enable LCD Controller */

/* '328-compatible definitions */
#define LCKCON_DW_MASK  LCKCON_DWS_MASK
#define LCKCON_DW_SHIFT LCKCON_DWS_SHIFT
 
/*
 * LCD Refresh Rate Adjustment Register 
 */
#define LRRA_ADDR	0xfffffa29
#define LRRA		BYTE_REF(LRRA_ADDR)

/*
 * LCD Panning Offset Register
 */
#define LPOSR_ADDR	0xfffffa2d
#define LPOSR		BYTE_REF(LPOSR_ADDR)

#define LPOSR_POS_MASK	0x0f	/* Pixel Offset Code */
#define LPOSR_POS_SHIFT	0

/*
 * LCD Frame Rate Control Modulation Register
 */
#define LFRCM_ADDR	0xfffffa31
#define LFRCM		BYTE_REF(LFRCM_ADDR)

#define LFRCM_YMOD_MASK	 0x0f	/* Vertical Modulation */
#define LFRCM_YMOD_SHIFT 0
#define LFRCM_XMOD_MASK	 0xf0	/* Horizontal Modulation */
#define LFRCM_XMOD_SHIFT 4

/*
 * LCD Gray Palette Mapping Register
 */
#define LGPMR_ADDR	0xfffffa33
#define LGPMR		BYTE_REF(LGPMR_ADDR)

#define LGPMR_G1_MASK	0x0f
#define LGPMR_G1_SHIFT	0
#define LGPMR_G2_MASK	0xf0
#define LGPMR_G2_SHIFT	4

/* 
 * PWM Contrast Control Register
 */
#define PWMR_ADDR	0xfffffa36
#define PWMR		WORD_REF(PWMR_ADDR)

#define PWMR_PW_MASK	0x00ff	/* Pulse Width */
#define PWMR_PW_SHIFT	0
#define PWMR_CCPEN	0x0100	/* Contrast Control Enable */
#define PWMR_SRC_MASK	0x0600	/* Input Clock Source */
#define   PWMR_SRC_LINE	  0x0000	/* Line Pulse  */
#define   PWMR_SRC_PIXEL  0x0200	/* Pixel Clock */
#define   PWMR_SRC_LCD    0x4000	/* LCD clock   */

/**********
 *
 * 0xFFFFFBxx -- Real-Time Clock (RTC)
 *
 **********/

/*
 * RTC Hours Minutes and Seconds Register
 */
#define RTCTIME_ADDR	0xfffffb00
#define RTCTIME		LONG_REF(RTCTIME_ADDR)

#define RTCTIME_SECONDS_MASK	0x0000003f	/* Seconds */
#define RTCTIME_SECONDS_SHIFT	0
#define RTCTIME_MINUTES_MASK	0x003f0000	/* Minutes */
#define RTCTIME_MINUTES_SHIFT	16
#define RTCTIME_HOURS_MASK	0x1f000000	/* Hours */
#define RTCTIME_HOURS_SHIFT	24

/*
 *  RTC Alarm Register 
 */
#define RTCALRM_ADDR    0xfffffb04
#define RTCALRM         LONG_REF(RTCALRM_ADDR)

#define RTCALRM_SECONDS_MASK    0x0000003f      /* Seconds */
#define RTCALRM_SECONDS_SHIFT   0
#define RTCALRM_MINUTES_MASK    0x003f0000      /* Minutes */
#define RTCALRM_MINUTES_SHIFT   16
#define RTCALRM_HOURS_MASK      0x1f000000      /* Hours */
#define RTCALRM_HOURS_SHIFT     24

/*
 * Watchdog Timer Register 
 */
#define WATCHDOG_ADDR	0xfffffb0a
#define WATCHDOG	WORD_REF(WATCHDOG_ADDR)

#define WATCHDOG_EN	0x0001	/* Watchdog Enabled */
#define WATCHDOG_ISEL	0x0002	/* Select the watchdog interrupt */
#define WATCHDOG_INTF	0x0080	/* Watchdog interrupt occcured */
#define WATCHDOG_CNT_MASK  0x0300	/* Watchdog Counter */
#define WATCHDOG_CNT_SHIFT 8

/*
 * RTC Control Register
 */
#define RTCCTL_ADDR	0xfffffb0c
#define RTCCTL		WORD_REF(RTCCTL_ADDR)

#define RTCCTL_XTL	0x0020	/* Crystal Selection */
#define RTCCTL_EN	0x0080	/* RTC Enable */

/* '328-compatible definitions */
#define RTCCTL_384	RTCCTL_XTL
#define RTCCTL_ENABLE	RTCCTL_EN

/*
 * RTC Interrupt Status Register 
 */
#define RTCISR_ADDR	0xfffffb0e
#define RTCISR		WORD_REF(RTCISR_ADDR)

#define RTCISR_SW	0x0001	/* Stopwatch timed out */
#define RTCISR_MIN	0x0002	/* 1-minute interrupt has occurred */
#define RTCISR_ALM	0x0004	/* Alarm interrupt has occurred */
#define RTCISR_DAY	0x0008	/* 24-hour rollover interrupt has occurred */
#define RTCISR_1HZ	0x0010	/* 1Hz interrupt has occurred */
#define RTCISR_HR	0x0020	/* 1-hour interrupt has occurred */
#define RTCISR_SAM0	0x0100	/*   4Hz /   4.6875Hz interrupt has occurred */ 
#define RTCISR_SAM1	0x0200	/*   8Hz /   9.3750Hz interrupt has occurred */ 
#define RTCISR_SAM2	0x0400	/*  16Hz /  18.7500Hz interrupt has occurred */ 
#define RTCISR_SAM3	0x0800	/*  32Hz /  37.5000Hz interrupt has occurred */ 
#define RTCISR_SAM4	0x1000	/*  64Hz /  75.0000Hz interrupt has occurred */ 
#define RTCISR_SAM5	0x2000	/* 128Hz / 150.0000Hz interrupt has occurred */ 
#define RTCISR_SAM6	0x4000	/* 256Hz / 300.0000Hz interrupt has occurred */ 
#define RTCISR_SAM7	0x8000	/* 512Hz / 600.0000Hz interrupt has occurred */ 

/*
 * RTC Interrupt Enable Register
 */
#define RTCIENR_ADDR	0xfffffb10
#define RTCIENR		WORD_REF(RTCIENR_ADDR)

#define RTCIENR_SW	0x0001	/* Stopwatch interrupt enable */
#define RTCIENR_MIN	0x0002	/* 1-minute interrupt enable */
#define RTCIENR_ALM	0x0004	/* Alarm interrupt enable */
#define RTCIENR_DAY	0x0008	/* 24-hour rollover interrupt enable */
#define RTCIENR_1HZ	0x0010	/* 1Hz interrupt enable */
#define RTCIENR_HR	0x0020	/* 1-hour interrupt enable */
#define RTCIENR_SAM0	0x0100	/*   4Hz /   4.6875Hz interrupt enable */ 
#define RTCIENR_SAM1	0x0200	/*   8Hz /   9.3750Hz interrupt enable */ 
#define RTCIENR_SAM2	0x0400	/*  16Hz /  18.7500Hz interrupt enable */ 
#define RTCIENR_SAM3	0x0800	/*  32Hz /  37.5000Hz interrupt enable */ 
#define RTCIENR_SAM4	0x1000	/*  64Hz /  75.0000Hz interrupt enable */ 
#define RTCIENR_SAM5	0x2000	/* 128Hz / 150.0000Hz interrupt enable */ 
#define RTCIENR_SAM6	0x4000	/* 256Hz / 300.0000Hz interrupt enable */ 
#define RTCIENR_SAM7	0x8000	/* 512Hz / 600.0000Hz interrupt enable */ 

/* 
 * Stopwatch Minutes Register
 */
#define STPWCH_ADDR	0xfffffb12
#define STPWCH		WORD_REF(STPWCH)

#define STPWCH_CNT_MASK	 0x003f	/* Stopwatch countdown value */
#define SPTWCH_CNT_SHIFT 0

/*
 * RTC Day Count Register 
 */
#define DAYR_ADDR	0xfffffb1a
#define DAYR		WORD_REF(DAYR_ADDR)

#define DAYR_DAYS_MASK	0x1ff	/* Day Setting */
#define DAYR_DAYS_SHIFT 0

/*
 * RTC Day Alarm Register 
 */
#define DAYALARM_ADDR	0xfffffb1c
#define DAYALARM	WORD_REF(DAYALARM_ADDR)

#define DAYALARM_DAYSAL_MASK	0x01ff	/* Day Setting of the Alarm */
#define DAYALARM_DAYSAL_SHIFT 	0

/**********
 *
 * 0xFFFFFCxx -- DRAM Controller
 *
 **********/

/*
 * DRAM Memory Configuration Register 
 */
#define DRAMMC_ADDR	0xfffffc00
#define DRAMMC		WORD_REF(DRAMMC_ADDR)

#define DRAMMC_ROW12_MASK	0xc000	/* Row address bit for MD12 */
#define   DRAMMC_ROW12_PA10	0x0000
#define   DRAMMC_ROW12_PA21	0x4000	
#define   DRAMMC_ROW12_PA23	0x8000
#define	DRAMMC_ROW0_MASK	0x3000	/* Row address bit for MD0 */
#define	  DRAMMC_ROW0_PA11	0x0000
#define   DRAMMC_ROW0_PA22	0x1000
#define   DRAMMC_ROW0_PA23	0x2000
#define DRAMMC_ROW11		0x0800	/* Row address bit for MD11 PA20/PA22 */
#define DRAMMC_ROW10		0x0400	/* Row address bit for MD10 PA19/PA21 */
#define	DRAMMC_ROW9		0x0200	/* Row address bit for MD9  PA9/PA19  */
#define DRAMMC_ROW8		0x0100	/* Row address bit for MD8  PA10/PA20 */
#define DRAMMC_COL10		0x0080	/* Col address bit for MD10 PA11/PA0  */
#define DRAMMC_COL9		0x0040	/* Col address bit for MD9  PA10/PA0  */
#define DRAMMC_COL8		0x0020	/* Col address bit for MD8  PA9/PA0   */
#define DRAMMC_REF_MASK		0x001f	/* Reresh Cycle */
#define DRAMMC_REF_SHIFT	0

/*
 * DRAM Control Register
 */
#define DRAMC_ADDR	0xfffffc02
#define DRAMC		WORD_REF(DRAMC_ADDR)

#define DRAMC_DWE	   0x0001	/* DRAM Write Enable */
#define DRAMC_RST	   0x0002	/* Reset Burst Refresh Enable */
#define DRAMC_LPR	   0x0004	/* Low-Power Refresh Enable */
#define DRAMC_SLW	   0x0008	/* Slow RAM */
#define DRAMC_LSP	   0x0010	/* Light Sleep */
#define DRAMC_MSW	   0x0020	/* Slow Multiplexing */
#define DRAMC_WS_MASK	   0x00c0	/* Wait-states */
#define DRAMC_WS_SHIFT	   6
#define DRAMC_PGSZ_MASK    0x0300	/* Page Size for fast page mode */
#define DRAMC_PGSZ_SHIFT   8
#define   DRAMC_PGSZ_256K  0x0000	
#define   DRAMC_PGSZ_512K  0x0100
#define   DRAMC_PGSZ_1024K 0x0200
#define	  DRAMC_PGSZ_2048K 0x0300
#define DRAMC_EDO	   0x0400	/* EDO DRAM */
#define DRAMC_CLK	   0x0800	/* Refresh Timer Clock source select */
#define DRAMC_BC_MASK	   0x3000	/* Page Access Clock Cycle (FP mode) */
#define DRAMC_BC_SHIFT	   12
#define DRAMC_RM	   0x4000	/* Refresh Mode */
#define DRAMC_EN	   0x8000	/* DRAM Controller enable */


/**********
 *
 * 0xFFFFFDxx -- In-Circuit Emulation (ICE)
 *
 **********/

/*
 * ICE Module Address Compare Register
 */
#define ICEMACR_ADDR	0xfffffd00
#define ICEMACR		LONG_REF(ICEMACR_ADDR)

/*
 * ICE Module Address Mask Register
 */
#define ICEMAMR_ADDR	0xfffffd04
#define ICEMAMR		LONG_REF(ICEMAMR_ADDR)

/*
 * ICE Module Control Compare Register
 */
#define ICEMCCR_ADDR	0xfffffd08
#define ICEMCCR		WORD_REF(ICEMCCR_ADDR)

#define ICEMCCR_PD	0x0001	/* Program/Data Cycle Selection */
#define ICEMCCR_RW	0x0002	/* Read/Write Cycle Selection */

/*
 * ICE Module Control Mask Register
 */
#define ICEMCMR_ADDR	0xfffffd0a
#define ICEMCMR		WORD_REF(ICEMCMR_ADDR)

#define ICEMCMR_PDM	0x0001	/* Program/Data Cycle Mask */
#define ICEMCMR_RWM	0x0002	/* Read/Write Cycle Mask */

/*
 * ICE Module Control Register 
 */
#define ICEMCR_ADDR	0xfffffd0c
#define ICEMCR		WORD_REF(ICEMCR_ADDR)

#define ICEMCR_CEN	0x0001	/* Compare Enable */
#define ICEMCR_PBEN	0x0002	/* Program Break Enable */
#define ICEMCR_SB	0x0004	/* Single Breakpoint */
#define ICEMCR_HMDIS	0x0008	/* HardMap disable */
#define ICEMCR_BBIEN	0x0010	/* Bus Break Interrupt Enable */

/*
 * ICE Module Status Register 
 */
#define ICEMSR_ADDR	0xfffffd0e
#define ICEMSR		WORD_REF(ICEMSR_ADDR)

#define ICEMSR_EMUEN	0x0001	/* Emulation Enable */
#define ICEMSR_BRKIRQ	0x0002	/* A-Line Vector Fetch Detected */
#define ICEMSR_BBIRQ	0x0004	/* Bus Break Interrupt Detected */
#define ICEMSR_EMIRQ	0x0008	/* EMUIRQ Falling Edge Detected */

#endif /* _MC68EZ328_H_ */
