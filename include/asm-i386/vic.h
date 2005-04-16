/* Copyright (C) 1999,2001
 *
 * Author: J.E.J.Bottomley@HansenPartnership.com
 *
 * Standard include definitions for the NCR Voyager Interrupt Controller */

/* The eight CPI vectors.  To activate a CPI, you write a bit mask
 * corresponding to the processor set to be interrupted into the
 * relevant register.  That set of CPUs will then be interrupted with
 * the CPI */
static const int VIC_CPI_Registers[] =
	{0xFC00, 0xFC01, 0xFC08, 0xFC09,
	 0xFC10, 0xFC11, 0xFC18, 0xFC19 };

#define VIC_PROC_WHO_AM_I		0xfc29
#	define	QUAD_IDENTIFIER		0xC0
#	define  EIGHT_SLOT_IDENTIFIER	0xE0
#define QIC_EXTENDED_PROCESSOR_SELECT	0xFC72
#define VIC_CPI_BASE_REGISTER		0xFC41
#define VIC_PROCESSOR_ID		0xFC21
#	define VIC_CPU_MASQUERADE_ENABLE 0x8

#define VIC_CLAIM_REGISTER_0		0xFC38
#define VIC_CLAIM_REGISTER_1		0xFC39
#define VIC_REDIRECT_REGISTER_0		0xFC60
#define VIC_REDIRECT_REGISTER_1		0xFC61
#define VIC_PRIORITY_REGISTER		0xFC20

#define VIC_PRIMARY_MC_BASE		0xFC48
#define VIC_SECONDARY_MC_BASE		0xFC49

#define QIC_PROCESSOR_ID		0xFC71
#	define	QIC_CPUID_ENABLE	0x08

#define QIC_VIC_CPI_BASE_REGISTER	0xFC79
#define QIC_CPI_BASE_REGISTER		0xFC7A

#define QIC_MASK_REGISTER0		0xFC80
/* NOTE: these are masked high, enabled low */
#	define QIC_PERF_TIMER		0x01
#	define QIC_LPE			0x02
#	define QIC_SYS_INT		0x04
#	define QIC_CMN_INT		0x08
/* at the moment, just enable CMN_INT, disable SYS_INT */
#	define QIC_DEFAULT_MASK0	(~(QIC_CMN_INT /* | VIC_SYS_INT */))
#define QIC_MASK_REGISTER1		0xFC81
#	define QIC_BOOT_CPI_MASK	0xFE
/* Enable CPI's 1-6 inclusive */
#	define QIC_CPI_ENABLE		0x81

#define QIC_INTERRUPT_CLEAR0		0xFC8A
#define QIC_INTERRUPT_CLEAR1		0xFC8B

/* this is where we place the CPI vectors */
#define VIC_DEFAULT_CPI_BASE		0xC0
/* this is where we place the QIC CPI vectors */
#define QIC_DEFAULT_CPI_BASE		0xD0

#define VIC_BOOT_INTERRUPT_MASK		0xfe

extern void smp_vic_timer_interrupt(struct pt_regs *regs);
