/****************************************************************************/

/*
 *	mcfdebug.h -- ColdFire Debug Module support.
 *
 * 	(C) Copyright 2001, Lineo Inc. (www.lineo.com) 
 */

/****************************************************************************/
#ifndef mcfdebug_h
#define mcfdebug_h
/****************************************************************************/
#include <linux/config.h>

/* Define the debug module registers */
#define MCFDEBUG_CSR	0x0			/* Configuration status		*/
#define MCFDEBUG_BAAR	0x5			/* BDM address attribute	*/
#define MCFDEBUG_AATR	0x6			/* Address attribute trigger	*/
#define MCFDEBUG_TDR	0x7			/* Trigger definition		*/
#define MCFDEBUG_PBR	0x8			/* PC breakpoint		*/
#define MCFDEBUG_PBMR	0x9			/* PC breakpoint mask		*/
#define MCFDEBUG_ABHR	0xc			/* High address breakpoint	*/
#define MCFDEBUG_ABLR	0xd			/* Low address breakpoint	*/
#define MCFDEBUG_DBR	0xe			/* Data breakpoint		*/
#define MCFDEBUG_DBMR	0xf			/* Data breakpoint mask		*/

/* Define some handy constants for the trigger definition register */
#define MCFDEBUG_TDR_TRC_DISP	0x00000000	/* display on DDATA only	*/
#define MCFDEBUG_TDR_TRC_HALT	0x40000000	/* Processor halt on BP		*/
#define MCFDEBUG_TDR_TRC_INTR	0x80000000	/* Debug intr on BP		*/
#define MCFDEBUG_TDR_LXT1	0x00004000	/* TDR level 1			*/
#define MCFDEBUG_TDR_LXT2	0x00008000	/* TDR level 2			*/
#define MCFDEBUG_TDR_EBL1	0x00002000	/* Enable breakpoint level 1	*/
#define MCFDEBUG_TDR_EBL2	0x20000000	/* Enable breakpoint level 2	*/
#define MCFDEBUG_TDR_EDLW1	0x00001000	/* Enable data BP longword	*/
#define MCFDEBUG_TDR_EDLW2	0x10000000
#define MCFDEBUG_TDR_EDWL1	0x00000800	/* Enable data BP lower word	*/
#define MCFDEBUG_TDR_EDWL2	0x08000000
#define MCFDEBUG_TDR_EDWU1	0x00000400	/* Enable data BP upper word	*/
#define MCFDEBUG_TDR_EDWU2	0x04000000
#define MCFDEBUG_TDR_EDLL1	0x00000200	/* Enable data BP low low byte	*/
#define MCFDEBUG_TDR_EDLL2	0x02000000
#define MCFDEBUG_TDR_EDLM1	0x00000100	/* Enable data BP low mid byte	*/
#define MCFDEBUG_TDR_EDLM2	0x01000000
#define MCFDEBUG_TDR_EDUM1	0x00000080	/* Enable data BP up mid byte	*/
#define MCFDEBUG_TDR_EDUM2	0x00800000
#define MCFDEBUG_TDR_EDUU1	0x00000040	/* Enable data BP up up byte	*/
#define MCFDEBUG_TDR_EDUU2	0x00400000
#define MCFDEBUG_TDR_DI1	0x00000020	/* Data BP invert		*/
#define MCFDEBUG_TDR_DI2	0x00200000
#define MCFDEBUG_TDR_EAI1	0x00000010	/* Enable address BP inverted	*/
#define MCFDEBUG_TDR_EAI2	0x00100000
#define MCFDEBUG_TDR_EAR1	0x00000008	/* Enable address BP range	*/
#define MCFDEBUG_TDR_EAR2	0x00080000
#define MCFDEBUG_TDR_EAL1	0x00000004	/* Enable address BP low	*/
#define MCFDEBUG_TDR_EAL2	0x00040000
#define MCFDEBUG_TDR_EPC1	0x00000002	/* Enable PC BP			*/
#define MCFDEBUG_TDR_EPC2	0x00020000
#define MCFDEBUG_TDR_PCI1	0x00000001	/* PC BP invert			*/
#define MCFDEBUG_TDR_PCI2	0x00010000

/* Constants for the address attribute trigger register */
#define MCFDEBUG_AAR_RESET	0x00000005
/* Fields not yet implemented */

/* And some definitions for the writable sections of the CSR */
#define MCFDEBUG_CSR_RESET	0x00100000
#define MCFDEBUG_CSR_PSTCLK	0x00020000	/* PSTCLK disable		*/
#define MCFDEBUG_CSR_IPW	0x00010000	/* Inhibit processor writes	*/
#define MCFDEBUG_CSR_MAP	0x00008000	/* Processor refs in emul mode	*/
#define MCFDEBUG_CSR_TRC	0x00004000	/* Emul mode on trace exception	*/
#define MCFDEBUG_CSR_EMU	0x00002000	/* Force emulation mode		*/
#define MCFDEBUG_CSR_DDC_READ	0x00000800	/* Debug data control		*/
#define MCFDEBUG_CSR_DDC_WRITE	0x00001000
#define MCFDEBUG_CSR_UHE	0x00000400	/* User mode halt enable	*/
#define MCFDEBUG_CSR_BTB0	0x00000000	/* Branch target 0 bytes	*/
#define MCFDEBUG_CSR_BTB2	0x00000100	/* Branch target 2 bytes	*/
#define MCFDEBUG_CSR_BTB3	0x00000200	/* Branch target 3 bytes	*/
#define MCFDEBUG_CSR_BTB4	0x00000300	/* Branch target 4 bytes	*/
#define MCFDEBUG_CSR_NPL	0x00000040	/* Non-pipelined mode		*/
#define MCFDEBUG_CSR_SSM	0x00000010	/* Single step mode		*/

/* Constants for the BDM address attribute register */
#define MCFDEBUG_BAAR_RESET	0x00000005
/* Fields not yet implemented */


/* This routine wrappers up the wdebug asm instruction so that the register
 * and value can be relatively easily specified.  The biggest hassle here is
 * that the debug module instructions (2 longs) must be long word aligned and
 * some pointer fiddling is performed to ensure this.
 */
extern inline void wdebug(int reg, unsigned long data) {
	unsigned short dbg_spc[6];
	unsigned short *dbg;

	// Force alignment to long word boundary
	dbg = (unsigned short *)((((unsigned long)dbg_spc) + 3) & 0xfffffffc);

	// Build up the debug instruction
	dbg[0] = 0x2c80 | (reg & 0xf);
	dbg[1] = (data >> 16) & 0xffff;
	dbg[2] = data & 0xffff;
	dbg[3] = 0;

	// Perform the wdebug instruction
#if 0
	// This strain is for gas which doesn't have the wdebug instructions defined
	asm(	"move.l	%0, %%a0\n\t"
		".word	0xfbd0\n\t"
		".word	0x0003\n\t"
	    :: "g" (dbg) : "a0");
#else
	// And this is for when it does
	asm(	"wdebug	(%0)" :: "a" (dbg));
#endif
}

#endif
