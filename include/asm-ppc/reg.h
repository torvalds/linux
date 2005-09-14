/*
 * Contains the definition of registers common to all PowerPC variants.
 * If a register definition has been changed in a different PowerPC
 * variant, we will case it in #ifndef XXX ... #endif, and have the
 * number used in the Programming Environments Manual For 32-Bit
 * Implementations of the PowerPC Architecture (a.k.a. Green Book) here.
 */

#ifdef __KERNEL__
#ifndef __ASM_PPC_REGS_H__
#define __ASM_PPC_REGS_H__

#include <linux/stringify.h>

/* Pickup Book E specific registers. */
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
#include <asm/reg_booke.h>
#endif

/* Machine State Register (MSR) Fields */
#define MSR_SF		(1<<63)
#define MSR_ISF		(1<<61)
#define MSR_VEC		(1<<25)		/* Enable AltiVec */
#define MSR_POW		(1<<18)		/* Enable Power Management */
#define MSR_WE		(1<<18)		/* Wait State Enable */
#define MSR_TGPR	(1<<17)		/* TLB Update registers in use */
#define MSR_CE		(1<<17)		/* Critical Interrupt Enable */
#define MSR_ILE		(1<<16)		/* Interrupt Little Endian */
#define MSR_EE		(1<<15)		/* External Interrupt Enable */
#define MSR_PR		(1<<14)		/* Problem State / Privilege Level */
#define MSR_FP		(1<<13)		/* Floating Point enable */
#define MSR_ME		(1<<12)		/* Machine Check Enable */
#define MSR_FE0		(1<<11)		/* Floating Exception mode 0 */
#define MSR_SE		(1<<10)		/* Single Step */
#define MSR_BE		(1<<9)		/* Branch Trace */
#define MSR_DE		(1<<9)		/* Debug Exception Enable */
#define MSR_FE1		(1<<8)		/* Floating Exception mode 1 */
#define MSR_IP		(1<<6)		/* Exception prefix 0x000/0xFFF */
#define MSR_IR		(1<<5)		/* Instruction Relocate */
#define MSR_DR		(1<<4)		/* Data Relocate */
#define MSR_PE		(1<<3)		/* Protection Enable */
#define MSR_PX		(1<<2)		/* Protection Exclusive Mode */
#define MSR_RI		(1<<1)		/* Recoverable Exception */
#define MSR_LE		(1<<0)		/* Little Endian */

/* Default MSR for kernel mode. */
#ifdef CONFIG_APUS_FAST_EXCEPT
#define MSR_KERNEL	(MSR_ME|MSR_IP|MSR_RI|MSR_IR|MSR_DR)
#endif

#ifndef MSR_KERNEL
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_IR|MSR_DR)
#endif

#define MSR_USER	(MSR_KERNEL|MSR_PR|MSR_EE)

/* Floating Point Status and Control Register (FPSCR) Fields */
#define FPSCR_FX	0x80000000	/* FPU exception summary */
#define FPSCR_FEX	0x40000000	/* FPU enabled exception summary */
#define FPSCR_VX	0x20000000	/* Invalid operation summary */
#define FPSCR_OX	0x10000000	/* Overflow exception summary */
#define FPSCR_UX	0x08000000	/* Underflow exception summary */
#define FPSCR_ZX	0x04000000	/* Zero-devide exception summary */
#define FPSCR_XX	0x02000000	/* Inexact exception summary */
#define FPSCR_VXSNAN	0x01000000	/* Invalid op for SNaN */
#define FPSCR_VXISI	0x00800000	/* Invalid op for Inv - Inv */
#define FPSCR_VXIDI	0x00400000	/* Invalid op for Inv / Inv */
#define FPSCR_VXZDZ	0x00200000	/* Invalid op for Zero / Zero */
#define FPSCR_VXIMZ	0x00100000	/* Invalid op for Inv * Zero */
#define FPSCR_VXVC	0x00080000	/* Invalid op for Compare */
#define FPSCR_FR	0x00040000	/* Fraction rounded */
#define FPSCR_FI	0x00020000	/* Fraction inexact */
#define FPSCR_FPRF	0x0001f000	/* FPU Result Flags */
#define FPSCR_FPCC	0x0000f000	/* FPU Condition Codes */
#define FPSCR_VXSOFT	0x00000400	/* Invalid op for software request */
#define FPSCR_VXSQRT	0x00000200	/* Invalid op for square root */
#define FPSCR_VXCVI	0x00000100	/* Invalid op for integer convert */
#define FPSCR_VE	0x00000080	/* Invalid op exception enable */
#define FPSCR_OE	0x00000040	/* IEEE overflow exception enable */
#define FPSCR_UE	0x00000020	/* IEEE underflow exception enable */
#define FPSCR_ZE	0x00000010	/* IEEE zero divide exception enable */
#define FPSCR_XE	0x00000008	/* FP inexact exception enable */
#define FPSCR_NI	0x00000004	/* FPU non IEEE-Mode */
#define FPSCR_RN	0x00000003	/* FPU rounding control */

/* Special Purpose Registers (SPRNs)*/
#define SPRN_CTR	0x009	/* Count Register */
#define SPRN_DABR	0x3F5	/* Data Address Breakpoint Register */
#define SPRN_DAR	0x013	/* Data Address Register */
#define SPRN_TBRL	0x10C	/* Time Base Read Lower Register (user, R/O) */
#define SPRN_TBRU	0x10D	/* Time Base Read Upper Register (user, R/O) */
#define SPRN_TBWL	0x11C	/* Time Base Lower Register (super, R/W) */
#define SPRN_TBWU	0x11D	/* Time Base Upper Register (super, R/W) */
#define SPRN_HIOR	0x137	/* 970 Hypervisor interrupt offset */
#define SPRN_DBAT0L	0x219	/* Data BAT 0 Lower Register */
#define SPRN_DBAT0U	0x218	/* Data BAT 0 Upper Register */
#define SPRN_DBAT1L	0x21B	/* Data BAT 1 Lower Register */
#define SPRN_DBAT1U	0x21A	/* Data BAT 1 Upper Register */
#define SPRN_DBAT2L	0x21D	/* Data BAT 2 Lower Register */
#define SPRN_DBAT2U	0x21C	/* Data BAT 2 Upper Register */
#define SPRN_DBAT3L	0x21F	/* Data BAT 3 Lower Register */
#define SPRN_DBAT3U	0x21E	/* Data BAT 3 Upper Register */
#define SPRN_DBAT4L	0x239	/* Data BAT 4 Lower Register */
#define SPRN_DBAT4U	0x238	/* Data BAT 4 Upper Register */
#define SPRN_DBAT5L	0x23B	/* Data BAT 5 Lower Register */
#define SPRN_DBAT5U	0x23A	/* Data BAT 5 Upper Register */
#define SPRN_DBAT6L	0x23D	/* Data BAT 6 Lower Register */
#define SPRN_DBAT6U	0x23C	/* Data BAT 6 Upper Register */
#define SPRN_DBAT7L	0x23F	/* Data BAT 7 Lower Register */
#define SPRN_DBAT7U	0x23E	/* Data BAT 7 Upper Register */

#define SPRN_DEC	0x016		/* Decrement Register */
#define SPRN_DER	0x095		/* Debug Enable Regsiter */
#define DER_RSTE	0x40000000	/* Reset Interrupt */
#define DER_CHSTPE	0x20000000	/* Check Stop */
#define DER_MCIE	0x10000000	/* Machine Check Interrupt */
#define DER_EXTIE	0x02000000	/* External Interrupt */
#define DER_ALIE	0x01000000	/* Alignment Interrupt */
#define DER_PRIE	0x00800000	/* Program Interrupt */
#define DER_FPUVIE	0x00400000	/* FP Unavailable Interrupt */
#define DER_DECIE	0x00200000	/* Decrementer Interrupt */
#define DER_SYSIE	0x00040000	/* System Call Interrupt */
#define DER_TRE		0x00020000	/* Trace Interrupt */
#define DER_SEIE	0x00004000	/* FP SW Emulation Interrupt */
#define DER_ITLBMSE	0x00002000	/* Imp. Spec. Instruction TLB Miss */
#define DER_ITLBERE	0x00001000	/* Imp. Spec. Instruction TLB Error */
#define DER_DTLBMSE	0x00000800	/* Imp. Spec. Data TLB Miss */
#define DER_DTLBERE	0x00000400	/* Imp. Spec. Data TLB Error */
#define DER_LBRKE	0x00000008	/* Load/Store Breakpoint Interrupt */
#define DER_IBRKE	0x00000004	/* Instruction Breakpoint Interrupt */
#define DER_EBRKE	0x00000002	/* External Breakpoint Interrupt */
#define DER_DPIE	0x00000001	/* Dev. Port Nonmaskable Request */
#define SPRN_DMISS	0x3D0		/* Data TLB Miss Register */
#define SPRN_DSISR	0x012	/* Data Storage Interrupt Status Register */
#define SPRN_EAR	0x11A		/* External Address Register */
#define SPRN_HASH1	0x3D2		/* Primary Hash Address Register */
#define SPRN_HASH2	0x3D3		/* Secondary Hash Address Resgister */
#define SPRN_HID0	0x3F0		/* Hardware Implementation Register 0 */
#define HID0_EMCP	(1<<31)		/* Enable Machine Check pin */
#define HID0_EBA	(1<<29)		/* Enable Bus Address Parity */
#define HID0_EBD	(1<<28)		/* Enable Bus Data Parity */
#define HID0_SBCLK	(1<<27)
#define HID0_EICE	(1<<26)
#define HID0_TBEN	(1<<26)		/* Timebase enable - 745x */
#define HID0_ECLK	(1<<25)
#define HID0_PAR	(1<<24)
#define HID0_STEN	(1<<24)		/* Software table search enable - 745x */
#define HID0_HIGH_BAT	(1<<23)		/* Enable high BATs - 7455 */
#define HID0_DOZE	(1<<23)
#define HID0_NAP	(1<<22)
#define HID0_SLEEP	(1<<21)
#define HID0_DPM	(1<<20)
#define HID0_BHTCLR	(1<<18)		/* Clear branch history table - 7450 */
#define HID0_XAEN	(1<<17)		/* Extended addressing enable - 7450 */
#define HID0_NHR	(1<<16)		/* Not hard reset (software bit-7450)*/
#define HID0_ICE	(1<<15)		/* Instruction Cache Enable */
#define HID0_DCE	(1<<14)		/* Data Cache Enable */
#define HID0_ILOCK	(1<<13)		/* Instruction Cache Lock */
#define HID0_DLOCK	(1<<12)		/* Data Cache Lock */
#define HID0_ICFI	(1<<11)		/* Instr. Cache Flash Invalidate */
#define HID0_DCI	(1<<10)		/* Data Cache Invalidate */
#define HID0_SPD	(1<<9)		/* Speculative disable */
#define HID0_DAPUEN	(1<<8)		/* Debug APU enable */
#define HID0_SGE	(1<<7)		/* Store Gathering Enable */
#define HID0_SIED	(1<<7)		/* Serial Instr. Execution [Disable] */
#define HID0_DFCA	(1<<6)		/* Data Cache Flush Assist */
#define HID0_LRSTK	(1<<4)		/* Link register stack - 745x */
#define HID0_BTIC	(1<<5)		/* Branch Target Instr Cache Enable */
#define HID0_ABE	(1<<3)		/* Address Broadcast Enable */
#define HID0_FOLD	(1<<3)		/* Branch Folding enable - 745x */
#define HID0_BHTE	(1<<2)		/* Branch History Table Enable */
#define HID0_BTCD	(1<<1)		/* Branch target cache disable */
#define HID0_NOPDST	(1<<1)		/* No-op dst, dstt, etc. instr. */
#define HID0_NOPTI	(1<<0)		/* No-op dcbt and dcbst instr. */

#define SPRN_HID1	0x3F1		/* Hardware Implementation Register 1 */
#define HID1_EMCP	(1<<31)		/* 7450 Machine Check Pin Enable */
#define HID1_DFS	(1<<22)		/* 7447A Dynamic Frequency Scaling */
#define HID1_PC0	(1<<16)		/* 7450 PLL_CFG[0] */
#define HID1_PC1	(1<<15)		/* 7450 PLL_CFG[1] */
#define HID1_PC2	(1<<14)		/* 7450 PLL_CFG[2] */
#define HID1_PC3	(1<<13)		/* 7450 PLL_CFG[3] */
#define HID1_SYNCBE	(1<<11)		/* 7450 ABE for sync, eieio */
#define HID1_ABE	(1<<10)		/* 7450 Address Broadcast Enable */
#define HID1_PS		(1<<16)		/* 750FX PLL selection */
#define SPRN_HID2	0x3F8		/* Hardware Implementation Register 2 */
#define SPRN_IABR	0x3F2	/* Instruction Address Breakpoint Register */
#define SPRN_HID4	0x3F4		/* 970 HID4 */
#define SPRN_HID5	0x3F6		/* 970 HID5 */
#if !defined(SPRN_IAC1) && !defined(SPRN_IAC2)
#define SPRN_IAC1	0x3F4		/* Instruction Address Compare 1 */
#define SPRN_IAC2	0x3F5		/* Instruction Address Compare 2 */
#endif
#define SPRN_IBAT0L	0x211		/* Instruction BAT 0 Lower Register */
#define SPRN_IBAT0U	0x210		/* Instruction BAT 0 Upper Register */
#define SPRN_IBAT1L	0x213		/* Instruction BAT 1 Lower Register */
#define SPRN_IBAT1U	0x212		/* Instruction BAT 1 Upper Register */
#define SPRN_IBAT2L	0x215		/* Instruction BAT 2 Lower Register */
#define SPRN_IBAT2U	0x214		/* Instruction BAT 2 Upper Register */
#define SPRN_IBAT3L	0x217		/* Instruction BAT 3 Lower Register */
#define SPRN_IBAT3U	0x216		/* Instruction BAT 3 Upper Register */
#define SPRN_IBAT4L	0x231		/* Instruction BAT 4 Lower Register */
#define SPRN_IBAT4U	0x230		/* Instruction BAT 4 Upper Register */
#define SPRN_IBAT5L	0x233		/* Instruction BAT 5 Lower Register */
#define SPRN_IBAT5U	0x232		/* Instruction BAT 5 Upper Register */
#define SPRN_IBAT6L	0x235		/* Instruction BAT 6 Lower Register */
#define SPRN_IBAT6U	0x234		/* Instruction BAT 6 Upper Register */
#define SPRN_IBAT7L	0x237		/* Instruction BAT 7 Lower Register */
#define SPRN_IBAT7U	0x236		/* Instruction BAT 7 Upper Register */
#define SPRN_ICMP	0x3D5		/* Instruction TLB Compare Register */
#define SPRN_ICTC	0x3FB	/* Instruction Cache Throttling Control Reg */
#define SPRN_ICTRL	0x3F3	/* 1011 7450 icache and interrupt ctrl */
#define ICTRL_EICE	0x08000000	/* enable icache parity errs */
#define ICTRL_EDC	0x04000000	/* enable dcache parity errs */
#define ICTRL_EICP	0x00000100	/* enable icache par. check */
#define SPRN_IMISS	0x3D4		/* Instruction TLB Miss Register */
#define SPRN_IMMR	0x27E		/* Internal Memory Map Register */
#define SPRN_L2CR	0x3F9		/* Level 2 Cache Control Regsiter */
#define SPRN_L2CR2	0x3f8
#define L2CR_L2E		0x80000000	/* L2 enable */
#define L2CR_L2PE		0x40000000	/* L2 parity enable */
#define L2CR_L2SIZ_MASK		0x30000000	/* L2 size mask */
#define L2CR_L2SIZ_256KB	0x10000000	/* L2 size 256KB */
#define L2CR_L2SIZ_512KB	0x20000000	/* L2 size 512KB */
#define L2CR_L2SIZ_1MB		0x30000000	/* L2 size 1MB */
#define L2CR_L2CLK_MASK		0x0e000000	/* L2 clock mask */
#define L2CR_L2CLK_DISABLED	0x00000000	/* L2 clock disabled */
#define L2CR_L2CLK_DIV1		0x02000000	/* L2 clock / 1 */
#define L2CR_L2CLK_DIV1_5	0x04000000	/* L2 clock / 1.5 */
#define L2CR_L2CLK_DIV2		0x08000000	/* L2 clock / 2 */
#define L2CR_L2CLK_DIV2_5	0x0a000000	/* L2 clock / 2.5 */
#define L2CR_L2CLK_DIV3		0x0c000000	/* L2 clock / 3 */
#define L2CR_L2RAM_MASK		0x01800000	/* L2 RAM type mask */
#define L2CR_L2RAM_FLOW		0x00000000	/* L2 RAM flow through */
#define L2CR_L2RAM_PIPE		0x01000000	/* L2 RAM pipelined */
#define L2CR_L2RAM_PIPE_LW	0x01800000	/* L2 RAM pipelined latewr */
#define L2CR_L2DO		0x00400000	/* L2 data only */
#define L2CR_L2I		0x00200000	/* L2 global invalidate */
#define L2CR_L2CTL		0x00100000	/* L2 RAM control */
#define L2CR_L2WT		0x00080000	/* L2 write-through */
#define L2CR_L2TS		0x00040000	/* L2 test support */
#define L2CR_L2OH_MASK		0x00030000	/* L2 output hold mask */
#define L2CR_L2OH_0_5		0x00000000	/* L2 output hold 0.5 ns */
#define L2CR_L2OH_1_0		0x00010000	/* L2 output hold 1.0 ns */
#define L2CR_L2SL		0x00008000	/* L2 DLL slow */
#define L2CR_L2DF		0x00004000	/* L2 differential clock */
#define L2CR_L2BYP		0x00002000	/* L2 DLL bypass */
#define L2CR_L2IP		0x00000001	/* L2 GI in progress */
#define L2CR_L2IO_745x		0x00100000	/* L2 instr. only (745x) */
#define L2CR_L2DO_745x		0x00010000	/* L2 data only (745x) */
#define L2CR_L2REP_745x		0x00001000	/* L2 repl. algorithm (745x) */
#define L2CR_L2HWF_745x		0x00000800	/* L2 hardware flush (745x) */
#define SPRN_L3CR		0x3FA	/* Level 3 Cache Control Regsiter */
#define L3CR_L3E		0x80000000	/* L3 enable */
#define L3CR_L3PE		0x40000000	/* L3 data parity enable */
#define L3CR_L3APE		0x20000000	/* L3 addr parity enable */
#define L3CR_L3SIZ		0x10000000	/* L3 size */
#define L3CR_L3CLKEN		0x08000000	/* L3 clock enable */
#define L3CR_L3RES		0x04000000	/* L3 special reserved bit */
#define L3CR_L3CLKDIV		0x03800000	/* L3 clock divisor */
#define L3CR_L3IO		0x00400000	/* L3 instruction only */
#define L3CR_L3SPO		0x00040000	/* L3 sample point override */
#define L3CR_L3CKSP		0x00030000	/* L3 clock sample point */
#define L3CR_L3PSP		0x0000e000	/* L3 P-clock sample point */
#define L3CR_L3REP		0x00001000	/* L3 replacement algorithm */
#define L3CR_L3HWF		0x00000800	/* L3 hardware flush */
#define L3CR_L3I		0x00000400	/* L3 global invalidate */
#define L3CR_L3RT		0x00000300	/* L3 SRAM type */
#define L3CR_L3NIRCA		0x00000080	/* L3 non-integer ratio clock adj. */
#define L3CR_L3DO		0x00000040	/* L3 data only mode */
#define L3CR_PMEN		0x00000004	/* L3 private memory enable */
#define L3CR_PMSIZ		0x00000001	/* L3 private memory size */
#define SPRN_MSSCR0	0x3f6	/* Memory Subsystem Control Register 0 */
#define SPRN_MSSSR0	0x3f7	/* Memory Subsystem Status Register 1 */
#define SPRN_LDSTCR	0x3f8	/* Load/Store control register */
#define SPRN_LDSTDB	0x3f4	/* */
#define SPRN_LR		0x008	/* Link Register */
#define SPRN_MMCR0	0x3B8	/* Monitor Mode Control Register 0 */
#define SPRN_MMCR1	0x3BC	/* Monitor Mode Control Register 1 */
#ifndef SPRN_PIR
#define SPRN_PIR	0x3FF	/* Processor Identification Register */
#endif
#define SPRN_PMC1	0x3B9	/* Performance Counter Register 1 */
#define SPRN_PMC2	0x3BA	/* Performance Counter Register 2 */
#define SPRN_PMC3	0x3BD	/* Performance Counter Register 3 */
#define SPRN_PMC4	0x3BE	/* Performance Counter Register 4 */
#define SPRN_PTEHI	0x3D5	/* 981 7450 PTE HI word (S/W TLB load) */
#define SPRN_PTELO	0x3D6	/* 982 7450 PTE LO word (S/W TLB load) */
#define SPRN_PVR	0x11F	/* Processor Version Register */
#define SPRN_RPA	0x3D6	/* Required Physical Address Register */
#define SPRN_SDA	0x3BF	/* Sampled Data Address Register */
#define SPRN_SDR1	0x019	/* MMU Hash Base Register */
#define SPRN_SIA	0x3BB	/* Sampled Instruction Address Register */
#define SPRN_SPRG0	0x110	/* Special Purpose Register General 0 */
#define SPRN_SPRG1	0x111	/* Special Purpose Register General 1 */
#define SPRN_SPRG2	0x112	/* Special Purpose Register General 2 */
#define SPRN_SPRG3	0x113	/* Special Purpose Register General 3 */
#define SPRN_SPRG4	0x114	/* Special Purpose Register General 4 */
#define SPRN_SPRG5	0x115	/* Special Purpose Register General 5 */
#define SPRN_SPRG6	0x116	/* Special Purpose Register General 6 */
#define SPRN_SPRG7	0x117	/* Special Purpose Register General 7 */
#define SPRN_SRR0	0x01A	/* Save/Restore Register 0 */
#define SPRN_SRR1	0x01B	/* Save/Restore Register 1 */
#ifndef SPRN_SVR
#define SPRN_SVR	0x11E	/* System Version Register */
#endif
#define SPRN_THRM1	0x3FC		/* Thermal Management Register 1 */
/* these bits were defined in inverted endian sense originally, ugh, confusing */
#define THRM1_TIN	(1 << 31)
#define THRM1_TIV	(1 << 30)
#define THRM1_THRES(x)	((x&0x7f)<<23)
#define THRM3_SITV(x)	((x&0x3fff)<<1)
#define THRM1_TID	(1<<2)
#define THRM1_TIE	(1<<1)
#define THRM1_V		(1<<0)
#define SPRN_THRM2	0x3FD		/* Thermal Management Register 2 */
#define SPRN_THRM3	0x3FE		/* Thermal Management Register 3 */
#define THRM3_E		(1<<0)
#define SPRN_TLBMISS	0x3D4		/* 980 7450 TLB Miss Register */
#define SPRN_UMMCR0	0x3A8	/* User Monitor Mode Control Register 0 */
#define SPRN_UMMCR1	0x3AC	/* User Monitor Mode Control Register 0 */
#define SPRN_UPMC1	0x3A9	/* User Performance Counter Register 1 */
#define SPRN_UPMC2	0x3AA	/* User Performance Counter Register 2 */
#define SPRN_UPMC3	0x3AD	/* User Performance Counter Register 3 */
#define SPRN_UPMC4	0x3AE	/* User Performance Counter Register 4 */
#define SPRN_USIA	0x3AB	/* User Sampled Instruction Address Register */
#define SPRN_VRSAVE	0x100	/* Vector Register Save Register */
#define SPRN_XER	0x001	/* Fixed Point Exception Register */

/* Bit definitions for MMCR0 and PMC1 / PMC2. */
#define MMCR0_PMC1_CYCLES	(1 << 7)
#define MMCR0_PMC1_ICACHEMISS	(5 << 7)
#define MMCR0_PMC1_DTLB		(6 << 7)
#define MMCR0_PMC2_DCACHEMISS	0x6
#define MMCR0_PMC2_CYCLES	0x1
#define MMCR0_PMC2_ITLB		0x7
#define MMCR0_PMC2_LOADMISSTIME	0x5
#define MMCR0_PMXE	(1 << 26)

/* Processor Version Register */

/* Processor Version Register (PVR) field extraction */

#define PVR_VER(pvr)	(((pvr) >>  16) & 0xFFFF)	/* Version field */
#define PVR_REV(pvr)	(((pvr) >>   0) & 0xFFFF)	/* Revison field */

/*
 * IBM has further subdivided the standard PowerPC 16-bit version and
 * revision subfields of the PVR for the PowerPC 403s into the following:
 */

#define PVR_FAM(pvr)	(((pvr) >> 20) & 0xFFF)	/* Family field */
#define PVR_MEM(pvr)	(((pvr) >> 16) & 0xF)	/* Member field */
#define PVR_CORE(pvr)	(((pvr) >> 12) & 0xF)	/* Core field */
#define PVR_CFG(pvr)	(((pvr) >>  8) & 0xF)	/* Configuration field */
#define PVR_MAJ(pvr)	(((pvr) >>  4) & 0xF)	/* Major revision field */
#define PVR_MIN(pvr)	(((pvr) >>  0) & 0xF)	/* Minor revision field */

/* Processor Version Numbers */

#define PVR_403GA	0x00200000
#define PVR_403GB	0x00200100
#define PVR_403GC	0x00200200
#define PVR_403GCX	0x00201400
#define PVR_405GP	0x40110000
#define PVR_STB03XXX	0x40310000
#define PVR_NP405H	0x41410000
#define PVR_NP405L	0x41610000
#define PVR_601		0x00010000
#define PVR_602		0x00050000
#define PVR_603		0x00030000
#define PVR_603e	0x00060000
#define PVR_603ev	0x00070000
#define PVR_603r	0x00071000
#define PVR_604		0x00040000
#define PVR_604e	0x00090000
#define PVR_604r	0x000A0000
#define PVR_620		0x00140000
#define PVR_740		0x00080000
#define PVR_750		PVR_740
#define PVR_740P	0x10080000
#define PVR_750P	PVR_740P
#define PVR_7400	0x000C0000
#define PVR_7410	0x800C0000
#define PVR_7450	0x80000000
#define PVR_8540	0x80200000
#define PVR_8560	0x80200000
/*
 * For the 8xx processors, all of them report the same PVR family for
 * the PowerPC core. The various versions of these processors must be
 * differentiated by the version number in the Communication Processor
 * Module (CPM).
 */
#define PVR_821		0x00500000
#define PVR_823		PVR_821
#define PVR_850		PVR_821
#define PVR_860		PVR_821
#define PVR_8240	0x00810100
#define PVR_8245	0x80811014
#define PVR_8260	PVR_8240

#if 0
/* Segment Registers */
#define SR0	0
#define SR1	1
#define SR2	2
#define SR3	3
#define SR4	4
#define SR5	5
#define SR6	6
#define SR7	7
#define SR8	8
#define SR9	9
#define SR10	10
#define SR11	11
#define SR12	12
#define SR13	13
#define SR14	14
#define SR15	15
#endif

/* Macros for setting and retrieving special purpose registers */
#ifndef __ASSEMBLY__
#define mfmsr()		({unsigned int rval; \
			asm volatile("mfmsr %0" : "=r" (rval)); rval;})
#define mtmsr(v)	asm volatile("mtmsr %0" : : "r" (v))

#define mfspr(rn)	({unsigned int rval; \
			asm volatile("mfspr %0," __stringify(rn) \
				: "=r" (rval)); rval;})
#define mtspr(rn, v)	asm volatile("mtspr " __stringify(rn) ",%0" : : "r" (v))

#define mfsrin(v)	({unsigned int rval; \
			asm volatile("mfsrin %0,%1" : "=r" (rval) : "r" (v)); \
					rval;})

#define proc_trap()	asm volatile("trap")
#endif /* __ASSEMBLY__ */
#endif /* __ASM_PPC_REGS_H__ */
#endif /* __KERNEL__ */
