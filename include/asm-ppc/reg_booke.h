/*
 * Contains register definitions common to the Book E PowerPC
 * specification.  Notice that while the IBM-40x series of CPUs
 * are not true Book E PowerPCs, they borrowed a number of features
 * before Book E was finalized, and are included here as well.  Unfortunatly,
 * they sometimes used different locations than true Book E CPUs did.
 */
#ifdef __KERNEL__
#ifndef __ASM_PPC_REG_BOOKE_H__
#define __ASM_PPC_REG_BOOKE_H__

#ifndef __ASSEMBLY__
/* Device Control Registers */
void __mtdcr(int reg, unsigned int val);
unsigned int __mfdcr(int reg);
#define mfdcr(rn)						\
	({unsigned int rval;					\
	if (__builtin_constant_p(rn))				\
		asm volatile("mfdcr %0," __stringify(rn)	\
		              : "=r" (rval));			\
	else							\
		rval = __mfdcr(rn);				\
	rval;})

#define mtdcr(rn, v)						\
do {								\
	if (__builtin_constant_p(rn))				\
		asm volatile("mtdcr " __stringify(rn) ",%0"	\
			      : : "r" (v)); 			\
	else							\
		__mtdcr(rn, v);					\
} while (0)

/* R/W of indirect DCRs make use of standard naming conventions for DCRs */
#define mfdcri(base, reg)			\
({						\
	mtdcr(base ## _CFGADDR, base ## _ ## reg);	\
	mfdcr(base ## _CFGDATA);			\
})

#define mtdcri(base, reg, data)			\
do {						\
	mtdcr(base ## _CFGADDR, base ## _ ## reg);	\
	mtdcr(base ## _CFGDATA, data);		\
} while (0)

/* Performance Monitor Registers */
#define mfpmr(rn)	({unsigned int rval; \
			asm volatile("mfpmr %0," __stringify(rn) \
				     : "=r" (rval)); rval;})
#define mtpmr(rn, v)	asm volatile("mtpmr " __stringify(rn) ",%0" : : "r" (v))
#endif /* __ASSEMBLY__ */

/* Freescale Book E Performance Monitor APU Registers */
#define PMRN_PMC0	0x010	/* Performance Monitor Counter 0 */
#define PMRN_PMC1	0x011	/* Performance Monitor Counter 1 */
#define PMRN_PMC2	0x012	/* Performance Monitor Counter 1 */
#define PMRN_PMC3	0x013	/* Performance Monitor Counter 1 */
#define PMRN_PMLCA0	0x090	/* PM Local Control A0 */
#define PMRN_PMLCA1	0x091	/* PM Local Control A1 */
#define PMRN_PMLCA2	0x092	/* PM Local Control A2 */
#define PMRN_PMLCA3	0x093	/* PM Local Control A3 */

#define PMLCA_FC	0x80000000	/* Freeze Counter */
#define PMLCA_FCS	0x40000000	/* Freeze in Supervisor */
#define PMLCA_FCU	0x20000000	/* Freeze in User */
#define PMLCA_FCM1	0x10000000	/* Freeze when PMM==1 */
#define PMLCA_FCM0	0x08000000	/* Freeze when PMM==0 */
#define PMLCA_CE	0x04000000	/* Condition Enable */

#define PMLCA_EVENT_MASK 0x007f0000	/* Event field */
#define PMLCA_EVENT_SHIFT	16

#define PMRN_PMLCB0	0x110	/* PM Local Control B0 */
#define PMRN_PMLCB1	0x111	/* PM Local Control B1 */
#define PMRN_PMLCB2	0x112	/* PM Local Control B2 */
#define PMRN_PMLCB3	0x113	/* PM Local Control B3 */

#define PMLCB_THRESHMUL_MASK	0x0700	/* Threshhold Multiple Field */
#define PMLCB_THRESHMUL_SHIFT	8

#define PMLCB_THRESHOLD_MASK	0x003f	/* Threshold Field */
#define PMLCB_THRESHOLD_SHIFT	0

#define PMRN_PMGC0	0x190	/* PM Global Control 0 */

#define PMGC0_FAC	0x80000000	/* Freeze all Counters */
#define PMGC0_PMIE	0x40000000	/* Interrupt Enable */
#define PMGC0_FCECE	0x20000000	/* Freeze countes on
					   Enabled Condition or
					   Event */

#define PMRN_UPMC0	0x000	/* User Performance Monitor Counter 0 */
#define PMRN_UPMC1	0x001	/* User Performance Monitor Counter 1 */
#define PMRN_UPMC2	0x002	/* User Performance Monitor Counter 1 */
#define PMRN_UPMC3	0x003	/* User Performance Monitor Counter 1 */
#define PMRN_UPMLCA0	0x080	/* User PM Local Control A0 */
#define PMRN_UPMLCA1	0x081	/* User PM Local Control A1 */
#define PMRN_UPMLCA2	0x082	/* User PM Local Control A2 */
#define PMRN_UPMLCA3	0x083	/* User PM Local Control A3 */
#define PMRN_UPMLCB0	0x100	/* User PM Local Control B0 */
#define PMRN_UPMLCB1	0x101	/* User PM Local Control B1 */
#define PMRN_UPMLCB2	0x102	/* User PM Local Control B2 */
#define PMRN_UPMLCB3	0x103	/* User PM Local Control B3 */
#define PMRN_UPMGC0	0x180	/* User PM Global Control 0 */


/* Machine State Register (MSR) Fields */
#define MSR_UCLE	(1<<26)	/* User-mode cache lock enable */
#define MSR_SPE		(1<<25)	/* Enable SPE */
#define MSR_DWE		(1<<10)	/* Debug Wait Enable */
#define MSR_UBLE	(1<<10)	/* BTB lock enable (e500) */
#define MSR_IS		MSR_IR	/* Instruction Space */
#define MSR_DS		MSR_DR	/* Data Space */
#define MSR_PMM		(1<<2)	/* Performance monitor mark bit */

/* Default MSR for kernel mode. */
#if defined (CONFIG_40x)
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_IR|MSR_DR|MSR_CE)
#elif defined(CONFIG_BOOKE)
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_CE)
#endif

/* Special Purpose Registers (SPRNs)*/
#define SPRN_DECAR	0x036	/* Decrementer Auto Reload Register */
#define SPRN_IVPR	0x03F	/* Interrupt Vector Prefix Register */
#define SPRN_USPRG0	0x100	/* User Special Purpose Register General 0 */
#define SPRN_SPRG4R	0x104	/* Special Purpose Register General 4 Read */
#define SPRN_SPRG5R	0x105	/* Special Purpose Register General 5 Read */
#define SPRN_SPRG6R	0x106	/* Special Purpose Register General 6 Read */
#define SPRN_SPRG7R	0x107	/* Special Purpose Register General 7 Read */
#define SPRN_SPRG4W	0x114	/* Special Purpose Register General 4 Write */
#define SPRN_SPRG5W	0x115	/* Special Purpose Register General 5 Write */
#define SPRN_SPRG6W	0x116	/* Special Purpose Register General 6 Write */
#define SPRN_SPRG7W	0x117	/* Special Purpose Register General 7 Write */
#define SPRN_DBCR2	0x136	/* Debug Control Register 2 */
#define SPRN_IAC3	0x13A	/* Instruction Address Compare 3 */
#define SPRN_IAC4	0x13B	/* Instruction Address Compare 4 */
#define SPRN_DVC1	0x13E	/* Data Value Compare Register 1 */
#define SPRN_DVC2	0x13F	/* Data Value Compare Register 2 */
#define SPRN_IVOR0	0x190	/* Interrupt Vector Offset Register 0 */
#define SPRN_IVOR1	0x191	/* Interrupt Vector Offset Register 1 */
#define SPRN_IVOR2	0x192	/* Interrupt Vector Offset Register 2 */
#define SPRN_IVOR3	0x193	/* Interrupt Vector Offset Register 3 */
#define SPRN_IVOR4	0x194	/* Interrupt Vector Offset Register 4 */
#define SPRN_IVOR5	0x195	/* Interrupt Vector Offset Register 5 */
#define SPRN_IVOR6	0x196	/* Interrupt Vector Offset Register 6 */
#define SPRN_IVOR7	0x197	/* Interrupt Vector Offset Register 7 */
#define SPRN_IVOR8	0x198	/* Interrupt Vector Offset Register 8 */
#define SPRN_IVOR9	0x199	/* Interrupt Vector Offset Register 9 */
#define SPRN_IVOR10	0x19A	/* Interrupt Vector Offset Register 10 */
#define SPRN_IVOR11	0x19B	/* Interrupt Vector Offset Register 11 */
#define SPRN_IVOR12	0x19C	/* Interrupt Vector Offset Register 12 */
#define SPRN_IVOR13	0x19D	/* Interrupt Vector Offset Register 13 */
#define SPRN_IVOR14	0x19E	/* Interrupt Vector Offset Register 14 */
#define SPRN_IVOR15	0x19F	/* Interrupt Vector Offset Register 15 */
#define SPRN_SPEFSCR	0x200	/* SPE & Embedded FP Status & Control */
#define SPRN_BBEAR	0x201	/* Branch Buffer Entry Address Register */
#define SPRN_BBTAR	0x202	/* Branch Buffer Target Address Register */
#define SPRN_IVOR32	0x210	/* Interrupt Vector Offset Register 32 */
#define SPRN_IVOR33	0x211	/* Interrupt Vector Offset Register 33 */
#define SPRN_IVOR34	0x212	/* Interrupt Vector Offset Register 34 */
#define SPRN_IVOR35	0x213	/* Interrupt Vector Offset Register 35 */
#define SPRN_MCSRR0	0x23A	/* Machine Check Save and Restore Register 0 */
#define SPRN_MCSRR1	0x23B	/* Machine Check Save and Restore Register 1 */
#define SPRN_MCSR	0x23C	/* Machine Check Status Register */
#define SPRN_MCAR	0x23D	/* Machine Check Address Register */
#define SPRN_DSRR0	0x23E	/* Debug Save and Restore Register 0 */
#define SPRN_DSRR1	0x23F	/* Debug Save and Restore Register 1 */
#define SPRN_MAS0	0x270	/* MMU Assist Register 0 */
#define SPRN_MAS1	0x271	/* MMU Assist Register 1 */
#define SPRN_MAS2	0x272	/* MMU Assist Register 2 */
#define SPRN_MAS3	0x273	/* MMU Assist Register 3 */
#define SPRN_MAS4	0x274	/* MMU Assist Register 4 */
#define SPRN_MAS5	0x275	/* MMU Assist Register 5 */
#define SPRN_MAS6	0x276	/* MMU Assist Register 6 */
#define SPRN_MAS7	0x3b0	/* MMU Assist Register 7 */
#define SPRN_PID1	0x279	/* Process ID Register 1 */
#define SPRN_PID2	0x27A	/* Process ID Register 2 */
#define SPRN_TLB0CFG	0x2B0	/* TLB 0 Config Register */
#define SPRN_TLB1CFG	0x2B1	/* TLB 1 Config Register */
#define SPRN_CCR1	0x378	/* Core Configuration Register 1 */
#define SPRN_ZPR	0x3B0	/* Zone Protection Register (40x) */
#define SPRN_MMUCR	0x3B2	/* MMU Control Register */
#define SPRN_CCR0	0x3B3	/* Core Configuration Register 0 */
#define SPRN_SGR	0x3B9	/* Storage Guarded Register */
#define SPRN_DCWR	0x3BA	/* Data Cache Write-thru Register */
#define SPRN_SLER	0x3BB	/* Little-endian real mode */
#define SPRN_SU0R	0x3BC	/* "User 0" real mode (40x) */
#define SPRN_DCMP	0x3D1	/* Data TLB Compare Register */
#define SPRN_ICDBDR	0x3D3	/* Instruction Cache Debug Data Register */
#define SPRN_EVPR	0x3D6	/* Exception Vector Prefix Register */
#define SPRN_L1CSR0	0x3F2	/* L1 Cache Control and Status Register 0 */
#define SPRN_L1CSR1	0x3F3	/* L1 Cache Control and Status Register 1 */
#define SPRN_PIT	0x3DB	/* Programmable Interval Timer */
#define SPRN_DCCR	0x3FA	/* Data Cache Cacheability Register */
#define SPRN_ICCR	0x3FB	/* Instruction Cache Cacheability Register */
#define SPRN_SVR	0x3FF	/* System Version Register */

/*
 * SPRs which have conflicting definitions on true Book E versus classic,
 * or IBM 40x.
 */
#ifdef CONFIG_BOOKE
#define SPRN_PID	0x030	/* Process ID */
#define SPRN_PID0	SPRN_PID/* Process ID Register 0 */
#define SPRN_CSRR0	0x03A	/* Critical Save and Restore Register 0 */
#define SPRN_CSRR1	0x03B	/* Critical Save and Restore Register 1 */
#define SPRN_DEAR	0x03D	/* Data Error Address Register */
#define SPRN_ESR	0x03E	/* Exception Syndrome Register */
#define SPRN_PIR	0x11E	/* Processor Identification Register */
#define SPRN_DBSR	0x130	/* Debug Status Register */
#define SPRN_DBCR0	0x134	/* Debug Control Register 0 */
#define SPRN_DBCR1	0x135	/* Debug Control Register 1 */
#define SPRN_IAC1	0x138	/* Instruction Address Compare 1 */
#define SPRN_IAC2	0x139	/* Instruction Address Compare 2 */
#define SPRN_DAC1	0x13C	/* Data Address Compare 1 */
#define SPRN_DAC2	0x13D	/* Data Address Compare 2 */
#define SPRN_TSR	0x150	/* Timer Status Register */
#define SPRN_TCR	0x154	/* Timer Control Register */
#endif /* Book E */
#ifdef CONFIG_40x
#define SPRN_PID	0x3B1	/* Process ID */
#define SPRN_DBCR1	0x3BD	/* Debug Control Register 1 */		
#define SPRN_ESR	0x3D4	/* Exception Syndrome Register */
#define SPRN_DEAR	0x3D5	/* Data Error Address Register */
#define SPRN_TSR	0x3D8	/* Timer Status Register */
#define SPRN_TCR	0x3DA	/* Timer Control Register */
#define SPRN_SRR2	0x3DE	/* Save/Restore Register 2 */
#define SPRN_SRR3	0x3DF	/* Save/Restore Register 3 */
#define SPRN_DBSR	0x3F0	/* Debug Status Register */		
#define SPRN_DBCR0	0x3F2	/* Debug Control Register 0 */
#define SPRN_DAC1	0x3F6	/* Data Address Compare 1 */
#define SPRN_DAC2	0x3F7	/* Data Address Compare 2 */
#define SPRN_CSRR0	SPRN_SRR2 /* Critical Save and Restore Register 0 */
#define SPRN_CSRR1	SPRN_SRR3 /* Critical Save and Restore Register 1 */
#endif

/* Bit definitions for CCR1. */
#define	CCR1_DPC	0x00000100 /* Disable L1 I-Cache/D-Cache parity checking */
#define	CCR1_TCS	0x00000080 /* Timer Clock Select */

/* Bit definitions for the MCSR. */
#ifdef CONFIG_440A
#define MCSR_MCS	0x80000000 /* Machine Check Summary */
#define MCSR_IB		0x40000000 /* Instruction PLB Error */
#define MCSR_DRB	0x20000000 /* Data Read PLB Error */
#define MCSR_DWB	0x10000000 /* Data Write PLB Error */
#define MCSR_TLBP	0x08000000 /* TLB Parity Error */
#define MCSR_ICP	0x04000000 /* I-Cache Parity Error */
#define MCSR_DCSP	0x02000000 /* D-Cache Search Parity Error */
#define MCSR_DCFP	0x01000000 /* D-Cache Flush Parity Error */
#define MCSR_IMPE	0x00800000 /* Imprecise Machine Check Exception */
#endif
#ifdef CONFIG_E500
#define MCSR_MCP 	0x80000000UL /* Machine Check Input Pin */
#define MCSR_ICPERR 	0x40000000UL /* I-Cache Parity Error */
#define MCSR_DCP_PERR 	0x20000000UL /* D-Cache Push Parity Error */
#define MCSR_DCPERR 	0x10000000UL /* D-Cache Parity Error */
#define MCSR_GL_CI 	0x00010000UL /* Guarded Load or Cache-Inhibited stwcx. */
#define MCSR_BUS_IAERR 	0x00000080UL /* Instruction Address Error */
#define MCSR_BUS_RAERR 	0x00000040UL /* Read Address Error */
#define MCSR_BUS_WAERR 	0x00000020UL /* Write Address Error */
#define MCSR_BUS_IBERR 	0x00000010UL /* Instruction Data Error */
#define MCSR_BUS_RBERR 	0x00000008UL /* Read Data Bus Error */
#define MCSR_BUS_WBERR 	0x00000004UL /* Write Data Bus Error */
#define MCSR_BUS_IPERR 	0x00000002UL /* Instruction parity Error */
#define MCSR_BUS_RPERR 	0x00000001UL /* Read parity Error */
#endif
#ifdef CONFIG_E200
#define MCSR_MCP 	0x80000000UL /* Machine Check Input Pin */
#define MCSR_CP_PERR 	0x20000000UL /* Cache Push Parity Error */
#define MCSR_CPERR 	0x10000000UL /* Cache Parity Error */
#define MCSR_EXCP_ERR 	0x08000000UL /* ISI, ITLB, or Bus Error on 1st insn
					fetch for an exception handler */
#define MCSR_BUS_IRERR 	0x00000010UL /* Read Bus Error on instruction fetch*/
#define MCSR_BUS_DRERR 	0x00000008UL /* Read Bus Error on data load */
#define MCSR_BUS_WRERR 	0x00000004UL /* Write Bus Error on buffered
					store or cache line push */
#endif

/* Bit definitions for the DBSR. */
/*
 * DBSR bits which have conflicting definitions on true Book E versus IBM 40x.
 */
#ifdef CONFIG_BOOKE
#define DBSR_IC		0x08000000	/* Instruction Completion */
#define DBSR_BT		0x04000000	/* Branch Taken */
#define DBSR_TIE	0x01000000	/* Trap Instruction Event */
#define DBSR_IAC1	0x00800000	/* Instr Address Compare 1 Event */
#define DBSR_IAC2	0x00400000	/* Instr Address Compare 2 Event */
#define DBSR_IAC3	0x00200000	/* Instr Address Compare 3 Event */
#define DBSR_IAC4	0x00100000	/* Instr Address Compare 4 Event */
#define DBSR_DAC1R	0x00080000	/* Data Addr Compare 1 Read Event */
#define DBSR_DAC1W	0x00040000	/* Data Addr Compare 1 Write Event */
#define DBSR_DAC2R	0x00020000	/* Data Addr Compare 2 Read Event */
#define DBSR_DAC2W	0x00010000	/* Data Addr Compare 2 Write Event */
#endif
#ifdef CONFIG_40x
#define DBSR_IC		0x80000000	/* Instruction Completion */
#define DBSR_BT		0x40000000	/* Branch taken */
#define DBSR_TIE	0x10000000	/* Trap Instruction debug Event */
#define DBSR_IAC1	0x00800000	/* Instruction Address Compare 1 Event */
#define DBSR_IAC2	0x00400000	/* Instruction Address Compare 2 Event */
#define DBSR_IAC3	0x00200000	/* Instruction Address Compare 3 Event */
#define DBSR_IAC4	0x00100000	/* Instruction Address Compare 4 Event */
#define DBSR_DAC1R	0x00080000	/* Data Address Compare 1 Read Event */
#define DBSR_DAC1W	0x00040000	/* Data Address Compare 1 Write Event */
#define DBSR_DAC2R	0x00020000	/* Data Address Compare 2 Read Event */
#define DBSR_DAC2W	0x00010000	/* Data Address Compare 2 Write Event */
#endif

/* Bit definitions related to the ESR. */
#define ESR_MCI		0x80000000	/* Machine Check - Instruction */
#define ESR_IMCP	0x80000000	/* Instr. Machine Check - Protection */
#define ESR_IMCN	0x40000000	/* Instr. Machine Check - Non-config */
#define ESR_IMCB	0x20000000	/* Instr. Machine Check - Bus error */
#define ESR_IMCT	0x10000000	/* Instr. Machine Check - Timeout */
#define ESR_PIL		0x08000000	/* Program Exception - Illegal */
#define ESR_PPR		0x04000000	/* Program Exception - Priveleged */
#define ESR_PTR		0x02000000	/* Program Exception - Trap */
#define ESR_FP		0x01000000	/* Floating Point Operation */
#define ESR_DST		0x00800000	/* Storage Exception - Data miss */
#define ESR_DIZ		0x00400000	/* Storage Exception - Zone fault */
#define ESR_ST		0x00800000	/* Store Operation */
#define ESR_DLK		0x00200000	/* Data Cache Locking */
#define ESR_ILK		0x00100000	/* Instr. Cache Locking */
#define ESR_PUO		0x00040000	/* Unimplemented Operation exception */
#define ESR_BO		0x00020000	/* Byte Ordering */

/* Bit definitions related to the DBCR0. */
#define DBCR0_EDM	0x80000000	/* External Debug Mode */
#define DBCR0_IDM	0x40000000	/* Internal Debug Mode */
#define DBCR0_RST	0x30000000	/* all the bits in the RST field */
#define DBCR0_RST_SYSTEM 0x30000000	/* System Reset */
#define DBCR0_RST_CHIP	0x20000000	/* Chip Reset */
#define DBCR0_RST_CORE	0x10000000	/* Core Reset */
#define DBCR0_RST_NONE	0x00000000	/* No Reset */
#define DBCR0_IC	0x08000000	/* Instruction Completion */
#define DBCR0_BT	0x04000000	/* Branch Taken */
#define DBCR0_EDE	0x02000000	/* Exception Debug Event */
#define DBCR0_TDE	0x01000000	/* TRAP Debug Event */
#define DBCR0_IA1	0x00800000	/* Instr Addr compare 1 enable */
#define DBCR0_IA2	0x00400000	/* Instr Addr compare 2 enable */
#define DBCR0_IA12	0x00200000	/* Instr Addr 1-2 range enable */
#define DBCR0_IA12X	0x00100000	/* Instr Addr 1-2 range eXclusive */
#define DBCR0_IA3	0x00080000	/* Instr Addr compare 3 enable */
#define DBCR0_IA4	0x00040000	/* Instr Addr compare 4 enable */
#define DBCR0_IA34	0x00020000	/* Instr Addr 3-4 range Enable */
#define DBCR0_IA34X	0x00010000	/* Instr Addr 3-4 range eXclusive */
#define DBCR0_IA12T	0x00008000	/* Instr Addr 1-2 range Toggle */
#define DBCR0_IA34T	0x00004000	/* Instr Addr 3-4 range Toggle */
#define DBCR0_FT	0x00000001	/* Freeze Timers on debug event */

/* Bit definitions related to the TCR. */
#define TCR_WP(x)	(((x)&0x3)<<30)	/* WDT Period */
#define TCR_WP_MASK	TCR_WP(3)
#define WP_2_17		0		/* 2^17 clocks */
#define WP_2_21		1		/* 2^21 clocks */
#define WP_2_25		2		/* 2^25 clocks */
#define WP_2_29		3		/* 2^29 clocks */
#define TCR_WRC(x)	(((x)&0x3)<<28)	/* WDT Reset Control */
#define TCR_WRC_MASK	TCR_WRC(3)
#define WRC_NONE	0		/* No reset will occur */
#define WRC_CORE	1		/* Core reset will occur */
#define WRC_CHIP	2		/* Chip reset will occur */
#define WRC_SYSTEM	3		/* System reset will occur */
#define TCR_WIE		0x08000000	/* WDT Interrupt Enable */
#define TCR_PIE		0x04000000	/* PIT Interrupt Enable */
#define TCR_DIE		TCR_PIE		/* DEC Interrupt Enable */
#define TCR_FP(x)	(((x)&0x3)<<24)	/* FIT Period */
#define TCR_FP_MASK	TCR_FP(3)
#define FP_2_9		0		/* 2^9 clocks */
#define FP_2_13		1		/* 2^13 clocks */
#define FP_2_17		2		/* 2^17 clocks */
#define FP_2_21		3		/* 2^21 clocks */
#define TCR_FIE		0x00800000	/* FIT Interrupt Enable */
#define TCR_ARE		0x00400000	/* Auto Reload Enable */

/* Bit definitions for the TSR. */
#define TSR_ENW		0x80000000	/* Enable Next Watchdog */
#define TSR_WIS		0x40000000	/* WDT Interrupt Status */
#define TSR_WRS(x)	(((x)&0x3)<<28)	/* WDT Reset Status */
#define WRS_NONE	0		/* No WDT reset occurred */
#define WRS_CORE	1		/* WDT forced core reset */
#define WRS_CHIP	2		/* WDT forced chip reset */
#define WRS_SYSTEM	3		/* WDT forced system reset */
#define TSR_PIS		0x08000000	/* PIT Interrupt Status */
#define TSR_DIS		TSR_PIS		/* DEC Interrupt Status */
#define TSR_FIS		0x04000000	/* FIT Interrupt Status */

/* Bit definitions for the DCCR. */
#define DCCR_NOCACHE	0		/* Noncacheable */
#define DCCR_CACHE	1		/* Cacheable */

/* Bit definitions for DCWR. */
#define DCWR_COPY	0		/* Copy-back */
#define DCWR_WRITE	1		/* Write-through */

/* Bit definitions for ICCR. */
#define ICCR_NOCACHE	0		/* Noncacheable */
#define ICCR_CACHE	1		/* Cacheable */

/* Bit definitions for L1CSR0. */
#define L1CSR0_CLFC	0x00000100	/* Cache Lock Bits Flash Clear */
#define L1CSR0_DCFI	0x00000002	/* Data Cache Flash Invalidate */
#define L1CSR0_CFI	0x00000002	/* Cache Flash Invalidate */
#define L1CSR0_DCE	0x00000001	/* Data Cache Enable */

/* Bit definitions for L1CSR1. */
#define L1CSR1_ICLFR	0x00000100	/* Instr Cache Lock Bits Flash Reset */
#define L1CSR1_ICFI	0x00000002	/* Instr Cache Flash Invalidate */
#define L1CSR1_ICE	0x00000001	/* Instr Cache Enable */

/* Bit definitions for SGR. */
#define SGR_NORMAL	0		/* Speculative fetching allowed. */
#define SGR_GUARDED	1		/* Speculative fetching disallowed. */

/* Bit definitions for SPEFSCR. */
#define SPEFSCR_SOVH	0x80000000	/* Summary integer overflow high */
#define SPEFSCR_OVH	0x40000000	/* Integer overflow high */
#define SPEFSCR_FGH	0x20000000	/* Embedded FP guard bit high */
#define SPEFSCR_FXH	0x10000000	/* Embedded FP sticky bit high */
#define SPEFSCR_FINVH	0x08000000	/* Embedded FP invalid operation high */
#define SPEFSCR_FDBZH	0x04000000	/* Embedded FP div by zero high */
#define SPEFSCR_FUNFH	0x02000000	/* Embedded FP underflow high */
#define SPEFSCR_FOVFH	0x01000000	/* Embedded FP overflow high */
#define SPEFSCR_FINXS	0x00200000	/* Embedded FP inexact sticky */
#define SPEFSCR_FINVS	0x00100000	/* Embedded FP invalid op. sticky */
#define SPEFSCR_FDBZS	0x00080000	/* Embedded FP div by zero sticky */
#define SPEFSCR_FUNFS	0x00040000	/* Embedded FP underflow sticky */
#define SPEFSCR_FOVFS	0x00020000	/* Embedded FP overflow sticky */
#define SPEFSCR_MODE	0x00010000	/* Embedded FP mode */
#define SPEFSCR_SOV	0x00008000	/* Integer summary overflow */
#define SPEFSCR_OV	0x00004000	/* Integer overflow */
#define SPEFSCR_FG	0x00002000	/* Embedded FP guard bit */
#define SPEFSCR_FX	0x00001000	/* Embedded FP sticky bit */
#define SPEFSCR_FINV	0x00000800	/* Embedded FP invalid operation */
#define SPEFSCR_FDBZ	0x00000400	/* Embedded FP div by zero */
#define SPEFSCR_FUNF	0x00000200	/* Embedded FP underflow */
#define SPEFSCR_FOVF	0x00000100	/* Embedded FP overflow */
#define SPEFSCR_FINXE	0x00000040	/* Embedded FP inexact enable */
#define SPEFSCR_FINVE	0x00000020	/* Embedded FP invalid op. enable */
#define SPEFSCR_FDBZE	0x00000010	/* Embedded FP div by zero enable */
#define SPEFSCR_FUNFE	0x00000008	/* Embedded FP underflow enable */
#define SPEFSCR_FOVFE	0x00000004	/* Embedded FP overflow enable */
#define SPEFSCR_FRMC 	0x00000003	/* Embedded FP rounding mode control */

/*
 * The IBM-403 is an even more odd special case, as it is much
 * older than the IBM-405 series.  We put these down here incase someone
 * wishes to support these machines again.
 */
#ifdef CONFIG_403GCX
/* Special Purpose Registers (SPRNs)*/
#define SPRN_TBHU	0x3CC	/* Time Base High User-mode */
#define SPRN_TBLU	0x3CD	/* Time Base Low User-mode */
#define SPRN_CDBCR	0x3D7	/* Cache Debug Control Register */
#define SPRN_TBHI	0x3DC	/* Time Base High */
#define SPRN_TBLO	0x3DD	/* Time Base Low */
#define SPRN_DBCR	0x3F2	/* Debug Control Regsiter */
#define SPRN_PBL1	0x3FC	/* Protection Bound Lower 1 */
#define SPRN_PBL2	0x3FE	/* Protection Bound Lower 2 */
#define SPRN_PBU1	0x3FD	/* Protection Bound Upper 1 */
#define SPRN_PBU2	0x3FF	/* Protection Bound Upper 2 */


/* Bit definitions for the DBCR. */
#define DBCR_EDM	DBCR0_EDM
#define DBCR_IDM	DBCR0_IDM
#define DBCR_RST(x)	(((x) & 0x3) << 28)
#define DBCR_RST_NONE	0
#define DBCR_RST_CORE	1
#define DBCR_RST_CHIP	2
#define DBCR_RST_SYSTEM	3
#define DBCR_IC		DBCR0_IC	/* Instruction Completion Debug Evnt */
#define DBCR_BT		DBCR0_BT	/* Branch Taken Debug Event */
#define DBCR_EDE	DBCR0_EDE	/* Exception Debug Event */
#define DBCR_TDE	DBCR0_TDE	/* TRAP Debug Event */
#define DBCR_FER	0x00F80000	/* First Events Remaining Mask */
#define DBCR_FT		0x00040000	/* Freeze Timers on Debug Event */
#define DBCR_IA1	0x00020000	/* Instr. Addr. Compare 1 Enable */
#define DBCR_IA2	0x00010000	/* Instr. Addr. Compare 2 Enable */
#define DBCR_D1R	0x00008000	/* Data Addr. Compare 1 Read Enable */
#define DBCR_D1W	0x00004000	/* Data Addr. Compare 1 Write Enable */
#define DBCR_D1S(x)	(((x) & 0x3) << 12)	/* Data Adrr. Compare 1 Size */
#define DAC_BYTE	0
#define DAC_HALF	1
#define DAC_WORD	2
#define DAC_QUAD	3
#define DBCR_D2R	0x00000800	/* Data Addr. Compare 2 Read Enable */
#define DBCR_D2W	0x00000400	/* Data Addr. Compare 2 Write Enable */
#define DBCR_D2S(x)	(((x) & 0x3) << 8)	/* Data Addr. Compare 2 Size */
#define DBCR_SBT	0x00000040	/* Second Branch Taken Debug Event */
#define DBCR_SED	0x00000020	/* Second Exception Debug Event */
#define DBCR_STD	0x00000010	/* Second Trap Debug Event */
#define DBCR_SIA	0x00000008	/* Second IAC Enable */
#define DBCR_SDA	0x00000004	/* Second DAC Enable */
#define DBCR_JOI	0x00000002	/* JTAG Serial Outbound Int. Enable */
#define DBCR_JII	0x00000001	/* JTAG Serial Inbound Int. Enable */
#endif /* 403GCX */
#endif /* __ASM_PPC_REG_BOOKE_H__ */
#endif /* __KERNEL__ */
