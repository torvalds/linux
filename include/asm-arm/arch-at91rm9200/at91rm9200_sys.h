/*
 * include/asm-arm/arch-at91rm9200/at91rm9200_sys.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * System peripherals registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91RM9200_SYS_H
#define AT91RM9200_SYS_H

/*
 * Advanced Interrupt Controller.
 */
#define AT91_AIC	0x000

#define AT91_AIC_SMR(n)		(AT91_AIC + ((n) * 4))	/* Source Mode Registers 0-31 */
#define		AT91_AIC_PRIOR		(7 << 0)		/* Priority Level */
#define		AT91_AIC_SRCTYPE	(3 << 5)		/* Interrupt Source Type */
#define			AT91_AIC_SRCTYPE_LOW		(0 << 5)
#define			AT91_AIC_SRCTYPE_FALLING	(1 << 5)
#define			AT91_AIC_SRCTYPE_HIGH		(2 << 5)
#define			AT91_AIC_SRCTYPE_RISING		(3 << 5)

#define AT91_AIC_SVR(n)		(AT91_AIC + 0x80 + ((n) * 4))	/* Source Vector Registers 0-31 */
#define AT91_AIC_IVR		(AT91_AIC + 0x100)	/* Interrupt Vector Register */
#define AT91_AIC_FVR		(AT91_AIC + 0x104)	/* Fast Interrupt Vector Register */
#define AT91_AIC_ISR		(AT91_AIC + 0x108)	/* Interrupt Status Register */
#define		AT91_AIC_IRQID		(0x1f << 0)		/* Current Interrupt Identifier */

#define AT91_AIC_IPR		(AT91_AIC + 0x10c)	/* Interrupt Pending Register */
#define AT91_AIC_IMR		(AT91_AIC + 0x110)	/* Interrupt Mask Register */
#define AT91_AIC_CISR		(AT91_AIC + 0x114)	/* Core Interrupt Status Register */
#define		AT91_AIC_NFIQ		(1 << 0)		/* nFIQ Status */
#define		AT91_AIC_NIRQ		(1 << 1)		/* nIRQ Status */

#define AT91_AIC_IECR		(AT91_AIC + 0x120)	/* Interrupt Enable Command Register */
#define AT91_AIC_IDCR		(AT91_AIC + 0x124)	/* Interrupt Disable Command Register */
#define AT91_AIC_ICCR		(AT91_AIC + 0x128)	/* Interrupt Clear Command Register */
#define AT91_AIC_ISCR		(AT91_AIC + 0x12c)	/* Interrupt Set Command Register */
#define AT91_AIC_EOICR		(AT91_AIC + 0x130)	/* End of Interrupt Command Register */
#define AT91_AIC_SPU		(AT91_AIC + 0x134)	/* Spurious Interrupt Vector Register */
#define AT91_AIC_DCR		(AT91_AIC + 0x138)	/* Debug Control Register */
#define		AT91_AIC_DCR_PROT	(1 << 0)		/* Protection Mode */
#define		AT91_AIC_DCR_GMSK	(1 << 1)		/* General Mask */


/*
 * Debug Unit.
 */
#define AT91_DBGU	0x200

#define AT91_DBGU_CR		(AT91_DBGU + 0x00)	/* Control Register */
#define AT91_DBGU_MR		(AT91_DBGU + 0x04)	/* Mode Register */
#define AT91_DBGU_IER		(AT91_DBGU + 0x08)	/* Interrupt Enable Register */
#define		AT91_DBGU_TXRDY		(1 << 1)		/* Transmitter Ready */
#define		AT91_DBGU_TXEMPTY	(1 << 9)		/* Transmitter Empty */
#define AT91_DBGU_IDR		(AT91_DBGU + 0x0c)	/* Interrupt Disable Register */
#define AT91_DBGU_IMR		(AT91_DBGU + 0x10)	/* Interrupt Mask Register */
#define AT91_DBGU_SR		(AT91_DBGU + 0x14)	/* Status Register */
#define AT91_DBGU_RHR		(AT91_DBGU + 0x18)	/* Receiver Holding Register */
#define AT91_DBGU_THR		(AT91_DBGU + 0x1c)	/* Transmitter Holding Register */
#define AT91_DBGU_BRGR		(AT91_DBGU + 0x20)	/* Baud Rate Generator Register */
#define AT91_DBGU_CIDR		(AT91_DBGU + 0x40)	/* Chip ID Register */
#define AT91_DBGU_EXID		(AT91_DBGU + 0x44)	/* Chip ID Extension Register */


/*
 * PIO Controllers.
 */
#define AT91_PIOA	0x400
#define AT91_PIOB	0x600
#define AT91_PIOC	0x800
#define AT91_PIOD	0xa00

#define PIO_PER		0x00	/* Enable Register */
#define PIO_PDR		0x04	/* Disable Register */
#define PIO_PSR		0x08	/* Status Register */
#define PIO_OER		0x10	/* Output Enable Register */
#define PIO_ODR		0x14	/* Output Disable Register */
#define PIO_OSR		0x18	/* Output Status Register */
#define PIO_IFER	0x20	/* Glitch Input Filter Enable */
#define PIO_IFDR	0x24	/* Glitch Input Filter Disable */
#define PIO_IFSR	0x28	/* Glitch Input Filter Status */
#define PIO_SODR	0x30	/* Set Output Data Register */
#define PIO_CODR	0x34	/* Clear Output Data Register */
#define PIO_ODSR	0x38	/* Output Data Status Register */
#define PIO_PDSR	0x3c	/* Pin Data Status Register */
#define PIO_IER		0x40	/* Interrupt Enable Register */
#define PIO_IDR		0x44	/* Interrupt Disable Register */
#define PIO_IMR		0x48	/* Interrupt Mask Register */
#define PIO_ISR		0x4c	/* Interrupt Status Register */
#define PIO_MDER	0x50	/* Multi-driver Enable Register */
#define PIO_MDDR	0x54	/* Multi-driver Disable Register */
#define PIO_MDSR	0x58	/* Multi-driver Status Register */
#define PIO_PUDR	0x60	/* Pull-up Disable Register */
#define PIO_PUER	0x64	/* Pull-up Enable Register */
#define PIO_PUSR	0x68	/* Pull-up Status Register */
#define PIO_ASR		0x70	/* Peripheral A Select Register */
#define PIO_BSR		0x74	/* Peripheral B Select Register */
#define PIO_ABSR	0x78	/* AB Status Register */
#define PIO_OWER	0xa0	/* Output Write Enable Register */
#define PIO_OWDR	0xa4	/* Output Write Disable Register */
#define PIO_OWSR	0xa8	/* Output Write Status Register */

#define AT91_PIO_P(n)	(1 << (n))


/*
 * Power Management Controller.
 */
#define	AT91_PMC	0xc00

#define	AT91_PMC_SCER		(AT91_PMC + 0x00)	/* System Clock Enable Register */
#define	AT91_PMC_SCDR		(AT91_PMC + 0x04)	/* System Clock Disable Register */

#define	AT91_PMC_SCSR		(AT91_PMC + 0x08)	/* System Clock Status Register */
#define		AT91_PMC_PCK		(1 <<  0)		/* Processor Clock */
#define		AT91_PMC_UDP		(1 <<  1)		/* USB Devcice Port Clock */
#define		AT91_PMC_MCKUDP		(1 <<  2)		/* USB Device Port Master Clock Automatic Disable on Suspend */
#define		AT91_PMC_UHP		(1 <<  4)		/* USB Host Port Clock */
#define		AT91_PMC_PCK0		(1 <<  8)		/* Programmable Clock 0 */
#define		AT91_PMC_PCK1		(1 <<  9)		/* Programmable Clock 1 */
#define		AT91_PMC_PCK2		(1 << 10)		/* Programmable Clock 2 */
#define		AT91_PMC_PCK3		(1 << 11)		/* Programmable Clock 3 */

#define	AT91_PMC_PCER		(AT91_PMC + 0x10)	/* Peripheral Clock Enable Register */
#define	AT91_PMC_PCDR		(AT91_PMC + 0x14)	/* Peripheral Clock Disable Register */
#define	AT91_PMC_PCSR		(AT91_PMC + 0x18)	/* Peripheral Clock Status Register */

#define	AT91_CKGR_MOR		(AT91_PMC + 0x20)	/* Main Oscillator Register */
#define		AT91_PMC_MOSCEN		(1    << 0)		/* Main Oscillator Enable */
#define		AT91_PMC_OSCOUNT	(0xff << 8)		/* Main Oscillator Start-up Time */

#define	AT91_CKGR_MCFR		(AT91_PMC + 0x24)	/* Main Clock Frequency Register */
#define		AT91_PMC_MAINF		(0xffff <<  0)		/* Main Clock Frequency */
#define		AT91_PMC_MAINRDY	(1	<< 16)		/* Main Clock Ready */

#define	AT91_CKGR_PLLAR		(AT91_PMC + 0x28)	/* PLL A Register */
#define	AT91_CKGR_PLLBR		(AT91_PMC + 0x2c)	/* PLL B Register */
#define		AT91_PMC_DIV		(0xff  <<  0)		/* Divider */
#define		AT91_PMC_PLLCOUNT	(0x3f  <<  8)		/* PLL Counter */
#define		AT91_PMC_OUT		(3     << 14)		/* PLL Clock Frequency Range */
#define		AT91_PMC_MUL		(0x7ff << 16)		/* PLL Multiplier */
#define		AT91_PMC_USB96M		(1     << 28)		/* Divider by 2 Enable (PLLB only) */

#define	AT91_PMC_MCKR		(AT91_PMC + 0x30)	/* Master Clock Register */
#define		AT91_PMC_CSS		(3 <<  0)		/* Master Clock Selection */
#define			AT91_PMC_CSS_SLOW		(0 << 0)
#define			AT91_PMC_CSS_MAIN		(1 << 0)
#define			AT91_PMC_CSS_PLLA		(2 << 0)
#define			AT91_PMC_CSS_PLLB		(3 << 0)
#define		AT91_PMC_PRES		(7 <<  2)		/* Master Clock Prescaler */
#define 		AT91_PMC_PRES_1			(0 << 2)
#define			AT91_PMC_PRES_2			(1 << 2)
#define			AT91_PMC_PRES_4			(2 << 2)
#define			AT91_PMC_PRES_8			(3 << 2)
#define			AT91_PMC_PRES_16		(4 << 2)
#define			AT91_PMC_PRES_32		(5 << 2)
#define			AT91_PMC_PRES_64		(6 << 2)
#define		AT91_PMC_MDIV		(3 <<  8)		/* Master Clock Division */
#define			AT91_PMC_MDIV_1			(0 << 8)
#define			AT91_PMC_MDIV_2			(1 << 8)
#define			AT91_PMC_MDIV_3			(2 << 8)
#define			AT91_PMC_MDIV_4			(3 << 8)

#define	AT91_PMC_PCKR(n)	(AT91_PMC + 0x40 + ((n) * 4))	/* Programmable Clock 0-3 Registers */

#define	AT91_PMC_IER		(AT91_PMC + 0x60)	/* Interrupt Enable Register */
#define	AT91_PMC_IDR		(AT91_PMC + 0x64)	/* Interrupt Disable Register */
#define	AT91_PMC_SR		(AT91_PMC + 0x68)	/* Status Register */
#define		AT91_PMC_MOSCS		(1 <<  0)		/* MOSCS Flag */
#define		AT91_PMC_LOCKA		(1 <<  1)		/* PLLA Lock */
#define		AT91_PMC_LOCKB		(1 <<  2)		/* PLLB Lock */
#define		AT91_PMC_MCKRDY		(1 <<  3)		/* Master Clock */
#define		AT91_PMC_PCK0RDY	(1 <<  8)		/* Programmable Clock 0 */
#define		AT91_PMC_PCK1RDY	(1 <<  9)		/* Programmable Clock 1 */
#define		AT91_PMC_PCK2RDY	(1 << 10)		/* Programmable Clock 2 */
#define		AT91_PMC_PCK3RDY	(1 << 11)		/* Programmable Clock 3 */
#define	AT91_PMC_IMR		(AT91_PMC + 0x6c)	/* Interrupt Mask Register */


/*
 * System Timer.
 */
#define	AT91_ST		0xd00

#define	AT91_ST_CR		(AT91_ST + 0x00)	/* Control Register */
#define 	AT91_ST_WDRST		(1 << 0)		/* Watchdog Timer Restart */
#define	AT91_ST_PIMR		(AT91_ST + 0x04)	/* Period Interval Mode Register */
#define		AT91_ST_PIV		(0xffff <<  0)		/* Period Interval Value */
#define	AT91_ST_WDMR		(AT91_ST + 0x08)	/* Watchdog Mode Register */
#define		AT91_ST_WDV		(0xffff <<  0)		/* Watchdog Counter Value */
#define		AT91_ST_RSTEN		(1	<< 16)		/* Reset Enable */
#define		AT91_ST_EXTEN		(1	<< 17)		/* External Signal Assertion Enable */
#define	AT91_ST_RTMR		(AT91_ST + 0x0c)	/* Real-time Mode Register */
#define		AT91_ST_RTPRES		(0xffff <<  0)		/* Real-time Prescalar Value */
#define	AT91_ST_SR		(AT91_ST + 0x10)	/* Status Register */
#define		AT91_ST_PITS		(1 << 0)		/* Period Interval Timer Status */
#define		AT91_ST_WDOVF		(1 << 1) 		/* Watchdog Overflow */
#define		AT91_ST_RTTINC		(1 << 2) 		/* Real-time Timer Increment */
#define		AT91_ST_ALMS		(1 << 3) 		/* Alarm Status */
#define	AT91_ST_IER		(AT91_ST + 0x14)	/* Interrupt Enable Register */
#define	AT91_ST_IDR		(AT91_ST + 0x18)	/* Interrupt Disable Register */
#define	AT91_ST_IMR		(AT91_ST + 0x1c)	/* Interrupt Mask Register */
#define	AT91_ST_RTAR		(AT91_ST + 0x20)	/* Real-time Alarm Register */
#define		AT91_ST_ALMV		(0xfffff << 0)		/* Alarm Value */
#define	AT91_ST_CRTR		(AT91_ST + 0x24)	/* Current Real-time Register */
#define		AT91_ST_CRTV		(0xfffff << 0)		/* Current Real-Time Value */


/*
 * Real-time Clock.
 */
#define	AT91_RTC	0xe00

#define	AT91_RTC_CR		(AT91_RTC + 0x00)	/* Control Register */
#define		AT91_RTC_UPDTIM		(1 <<  0)		/* Update Request Time Register */
#define		AT91_RTC_UPDCAL		(1 <<  1)		/* Update Request Calendar Register */
#define		AT91_RTC_TIMEVSEL	(3 <<  8)		/* Time Event Selection */
#define			AT91_RTC_TIMEVSEL_MINUTE	(0 << 8)
#define 		AT91_RTC_TIMEVSEL_HOUR		(1 << 8)
#define 		AT91_RTC_TIMEVSEL_DAY24		(2 << 8)
#define 		AT91_RTC_TIMEVSEL_DAY12		(3 << 8)
#define		AT91_RTC_CALEVSEL	(3 << 16)		/* Calendar Event Selection */
#define 		AT91_RTC_CALEVSEL_WEEK		(0 << 16)
#define 		AT91_RTC_CALEVSEL_MONTH		(1 << 16)
#define 		AT91_RTC_CALEVSEL_YEAR		(2 << 16)

#define	AT91_RTC_MR		(AT91_RTC + 0x04)	/* Mode Register */
#define 	AT91_RTC_HRMOD		(1 <<  0)		/* 12/24 Hour Mode */

#define	AT91_RTC_TIMR		(AT91_RTC + 0x08)	/* Time Register */
#define		AT91_RTC_SEC		(0x7f <<  0)		/* Current Second */
#define		AT91_RTC_MIN		(0x7f <<  8)		/* Current Minute */
#define		AT91_RTC_HOUR 		(0x3f << 16)		/* Current Hour */
#define		At91_RTC_AMPM		(1    << 22)		/* Ante Meridiem Post Meridiem Indicator */

#define	AT91_RTC_CALR		(AT91_RTC + 0x0c)	/* Calendar Register */
#define		AT91_RTC_CENT		(0x7f <<  0)		/* Current Century */
#define		AT91_RTC_YEAR		(0xff <<  8)		/* Current Year */
#define		AT91_RTC_MONTH		(0x1f << 16)		/* Current Month */
#define		AT91_RTC_DAY		(7    << 21)		/* Current Day */
#define		AT91_RTC_DATE		(0x3f << 24)		/* Current Date */

#define	AT91_RTC_TIMALR		(AT91_RTC + 0x10)	/* Time Alarm Register */
#define		AT91_RTC_SECEN		(1 <<  7)		/* Second Alarm Enable */
#define		AT91_RTC_MINEN		(1 << 15)		/* Minute Alarm Enable */
#define		AT91_RTC_HOUREN		(1 << 23)		/* Hour Alarm Enable */

#define	AT91_RTC_CALALR		(AT91_RTC + 0x14)	/* Calendar Alarm Register */
#define		AT91_RTC_MTHEN		(1 << 23)		/* Month Alarm Enable */
#define		AT91_RTC_DATEEN		(1 << 31)		/* Date Alarm Enable */

#define	AT91_RTC_SR		(AT91_RTC + 0x18)	/* Status Register */
#define		AT91_RTC_ACKUPD		(1 <<  0)		/* Acknowledge for Update */
#define		AT91_RTC_ALARM		(1 <<  1)		/* Alarm Flag */
#define		AT91_RTC_SECEV		(1 <<  2)		/* Second Event */
#define		AT91_RTC_TIMEV		(1 <<  3)		/* Time Event */
#define		AT91_RTC_CALEV		(1 <<  4)		/* Calendar Event */

#define	AT91_RTC_SCCR		(AT91_RTC + 0x1c)	/* Status Clear Command Register */
#define	AT91_RTC_IER		(AT91_RTC + 0x20)	/* Interrupt Enable Register */
#define	AT91_RTC_IDR		(AT91_RTC + 0x24)	/* Interrupt Disable Register */
#define	AT91_RTC_IMR		(AT91_RTC + 0x28)	/* Interrupt Mask Register */

#define	AT91_RTC_VER		(AT91_RTC + 0x2c)	/* Valid Entry Register */
#define		AT91_RTC_NVTIM		(1 <<  0)		/* Non valid Time */
#define		AT91_RTC_NVCAL		(1 <<  1)		/* Non valid Calendar */
#define		AT91_RTC_NVTIMALR	(1 <<  2)		/* Non valid Time Alarm */
#define		AT91_RTC_NVCALALR	(1 <<  3)		/* Non valid Calendar Alarm */


/*
 * Memory Controller.
 */
#define AT91_MC		0xf00

#define AT91_MC_RCR		(AT91_MC + 0x00)	/* MC Remap Control Register */
#define		AT91_MC_RCB		(1 <<  0)		/* Remap Command Bit */

#define AT91_MC_ASR		(AT91_MC + 0x04)	/* MC Abort Status Register */
#define		AT91_MC_UNADD		(1 <<  0)		/* Undefined Address Abort Status */
#define		AT91_MC_MISADD		(1 <<  1)		/* Misaligned Address Abort Status */
#define		AT91_MC_ABTSZ		(3 <<  8)		/* Abort Size Status */
#define			AT91_MC_ABTSZ_BYTE		(0 << 8)
#define			AT91_MC_ABTSZ_HALFWORD		(1 << 8)
#define			AT91_MC_ABTSZ_WORD		(2 << 8)
#define		AT91_MC_ABTTYP		(3 << 10)		/* Abort Type Status */
#define			AT91_MC_ABTTYP_DATAREAD		(0 << 10)
#define			AT91_MC_ABTTYP_DATAWRITE	(1 << 10)
#define			AT91_MC_ABTTYP_FETCH		(2 << 10)
#define		AT91_MC_MST0		(1 << 16)		/* ARM920T Abort Source */
#define		AT91_MC_MST1		(1 << 17)		/* PDC Abort Source */
#define		AT91_MC_MST2		(1 << 18)		/* UHP Abort Source */
#define		AT91_MC_MST3		(1 << 19)		/* EMAC Abort Source */
#define		AT91_MC_SVMST0		(1 << 24)		/* Saved ARM920T Abort Source */
#define		AT91_MC_SVMST1		(1 << 25)		/* Saved PDC Abort Source */
#define		AT91_MC_SVMST2		(1 << 26)		/* Saved UHP Abort Source */
#define		AT91_MC_SVMST3		(1 << 27)		/* Saved EMAC Abort Source */

#define AT91_MC_AASR		(AT91_MC + 0x08)	/* MC Abort Address Status Register */

#define AT91_MC_MPR		(AT91_MC + 0x0c)	/* MC Master Priority Register */
#define		AT91_MPR_MSTP0		(7 <<  0)		/* ARM920T Priority */
#define		AT91_MPR_MSTP1		(7 <<  4)		/* PDC Priority */
#define		AT91_MPR_MSTP2		(7 <<  8)		/* UHP Priority */
#define		AT91_MPR_MSTP3		(7 << 12)		/* EMAC Priority */

/* External Bus Interface (EBI) registers */
#define AT91_EBI_CSA		(AT91_MC + 0x60)	/* Chip Select Assignment Register */
#define		AT91_EBI_CS0A		(1 << 0)		/* Chip Select 0 Assignment */
#define			AT91_EBI_CS0A_SMC		(0 << 0)
#define			AT91_EBI_CS0A_BFC		(1 << 0)
#define		AT91_EBI_CS1A		(1 << 1)		/* Chip Select 1 Assignment */
#define			AT91_EBI_CS1A_SMC		(0 << 1)
#define			AT91_EBI_CS1A_SDRAMC		(1 << 1)
#define		AT91_EBI_CS3A		(1 << 3)		/* Chip Select 2 Assignment */
#define			AT91_EBI_CS3A_SMC		(0 << 3)
#define			AT91_EBI_CS3A_SMC_SMARTMEDIA	(1 << 3)
#define		AT91_EBI_CS4A		(1 << 4)		/* Chip Select 3 Assignment */
#define			AT91_EBI_CS4A_SMC		(0 << 4)
#define			AT91_EBI_CS4A_SMC_COMPACTFLASH	(1 << 4)
#define AT91_EBI_CFGR		(AT91_MC + 0x64)	/* Configuration Register */
#define		AT91_EBI_DBPUC		(1 << 0)		/* Data Bus Pull-Up Configuration */

/* Static Memory Controller (SMC) registers */
#define	AT91_SMC_CSR(n)		(AT91_MC + 0x70 + ((n) * 4))/* SMC Chip Select Register */
#define		AT91_SMC_NWS		(0x7f <<  0)		/* Number of Wait States */
#define			AT91_SMC_NWS_(x)	((x) << 0)
#define		AT91_SMC_WSEN		(1    <<  7)		/* Wait State Enable */
#define		AT91_SMC_TDF		(0xf  <<  8)		/* Data Float Time */
#define			AT91_SMC_TDF_(x)	((x) << 8)
#define		AT91_SMC_BAT		(1    << 12)		/* Byte Access Type */
#define		AT91_SMC_DBW		(3    << 13)		/* Data Bus Width */
#define			AT91_SMC_DBW_16		(1 << 13)
#define			AT91_SMC_DBW_8		(2 << 13)
#define		AT91_SMC_DPR		(1 << 15)		/* Data Read Protocol */
#define		AT91_SMC_ACSS		(3 << 16)		/* Address to Chip Select Setup */
#define			AT91_SMC_ACSS_STD	(0 << 16)
#define			AT91_SMC_ACSS_1		(1 << 16)
#define			AT91_SMC_ACSS_2		(2 << 16)
#define			AT91_SMC_ACSS_3		(3 << 16)
#define		AT91_SMC_RWSETUP	(7 << 24)		/* Read & Write Signal Time Setup */
#define			AT91_SMC_RWSETUP_(x)	((x) << 24)
#define		AT91_SMC_RWHOLD		(7 << 28)		/* Read & Write Signal Hold Time */
#define			AT91_SMC_RWHOLD_(x)	((x) << 28)

/* SDRAM Controller registers */
#define AT91_SDRAMC_MR		(AT91_MC + 0x90)	/* Mode Register */
#define		AT91_SDRAMC_MODE	(0xf << 0)		/* Command Mode */
#define			AT91_SDRAMC_MODE_NORMAL		(0 << 0)
#define			AT91_SDRAMC_MODE_NOP		(1 << 0)
#define			AT91_SDRAMC_MODE_PRECHARGE	(2 << 0)
#define			AT91_SDRAMC_MODE_LMR		(3 << 0)
#define			AT91_SDRAMC_MODE_REFRESH	(4 << 0)
#define		AT91_SDRAMC_DBW		(1   << 4)		/* Data Bus Width */
#define			AT91_SDRAMC_DBW_32	(0 << 4)
#define			AT91_SDRAMC_DBW_16	(1 << 4)

#define AT91_SDRAMC_TR		(AT91_MC + 0x94)	/* Refresh Timer Register */
#define		AT91_SDRAMC_COUNT	(0xfff << 0)		/* Refresh Timer Count */

#define AT91_SDRAMC_CR		(AT91_MC + 0x98)	/* Configuration Register */
#define		AT91_SDRAMC_NC		(3   <<  0)		/* Number of Column Bits */
#define			AT91_SDRAMC_NC_8	(0 << 0)
#define			AT91_SDRAMC_NC_9	(1 << 0)
#define			AT91_SDRAMC_NC_10	(2 << 0)
#define			AT91_SDRAMC_NC_11	(3 << 0)
#define		AT91_SDRAMC_NR		(3   <<  2)		/* Number of Row Bits */
#define			AT91_SDRAMC_NR_11	(0 << 2)
#define			AT91_SDRAMC_NR_12	(1 << 2)
#define			AT91_SDRAMC_NR_13	(2 << 2)
#define		AT91_SDRAMC_NB		(1   <<  4)		/* Number of Banks */
#define			AT91_SDRAMC_NB_2	(0 << 4)
#define			AT91_SDRAMC_NB_4	(1 << 4)
#define		AT91_SDRAMC_CAS		(3   <<  5)		/* CAS Latency */
#define			AT91_SDRAMC_CAS_2	(2 << 5)
#define		AT91_SDRAMC_TWR		(0xf <<  7)		/* Write Recovery Delay */
#define		AT91_SDRAMC_TRC		(0xf << 11)		/* Row Cycle Delay */
#define		AT91_SDRAMC_TRP		(0xf << 15)		/* Row Precharge Delay */
#define		AT91_SDRAMC_TRCD	(0xf << 19)		/* Row to Column Delay */
#define		AT91_SDRAMC_TRAS	(0xf << 23)		/* Active to Precharge Delay */
#define		AT91_SDRAMC_TXSR	(0xf << 27)		/* Exit Self Refresh to Active Delay */

#define AT91_SDRAMC_SRR		(AT91_MC + 0x9c)	/* Self Refresh Register */
#define AT91_SDRAMC_LPR		(AT91_MC + 0xa0)	/* Low Power Register */
#define AT91_SDRAMC_IER		(AT91_MC + 0xa4)	/* Interrupt Enable Register */
#define AT91_SDRAMC_IDR		(AT91_MC + 0xa8)	/* Interrupt Disable Register */
#define AT91_SDRAMC_IMR		(AT91_MC + 0xac)	/* Interrupt Mask Register */
#define AT91_SDRAMC_ISR		(AT91_MC + 0xb0)	/* Interrupt Status Register */

/* Burst Flash Controller register */
#define AT91_BFC_MR		(AT91_MC + 0xc0)	/* Mode Register */
#define		AT91_BFC_BFCOM		(3   <<  0)		/* Burst Flash Controller Operating Mode */
#define			AT91_BFC_BFCOM_DISABLED	(0 << 0)
#define			AT91_BFC_BFCOM_ASYNC	(1 << 0)
#define			AT91_BFC_BFCOM_BURST	(2 << 0)
#define		AT91_BFC_BFCC		(3   <<  2)		/* Burst Flash Controller Clock */
#define			AT91_BFC_BFCC_MCK	(1 << 2)
#define			AT91_BFC_BFCC_DIV2	(2 << 2)
#define			AT91_BFC_BFCC_DIV4	(3 << 2)
#define		AT91_BFC_AVL		(0xf <<  4)		/* Address Valid Latency */
#define		AT91_BFC_PAGES		(7   <<  8)		/* Page Size */
#define			AT91_BFC_PAGES_NO_PAGE	(0 << 8)
#define			AT91_BFC_PAGES_16	(1 << 8)
#define			AT91_BFC_PAGES_32	(2 << 8)
#define			AT91_BFC_PAGES_64	(3 << 8)
#define			AT91_BFC_PAGES_128	(4 << 8)
#define			AT91_BFC_PAGES_256	(5 << 8)
#define			AT91_BFC_PAGES_512	(6 << 8)
#define			AT91_BFC_PAGES_1024	(7 << 8)
#define		AT91_BFC_OEL		(3   << 12)		/* Output Enable Latency */
#define		AT91_BFC_BAAEN		(1   << 16)		/* Burst Address Advance Enable */
#define		AT91_BFC_BFOEH		(1   << 17)		/* Burst Flash Output Enable Handling */
#define		AT91_BFC_MUXEN		(1   << 18)		/* Multiplexed Bus Enable */
#define		AT91_BFC_RDYEN		(1   << 19)		/* Ready Enable Mode */

#endif
