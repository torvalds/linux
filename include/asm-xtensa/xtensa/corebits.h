#ifndef XTENSA_COREBITS_H
#define XTENSA_COREBITS_H

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 * xtensa/corebits.h - Xtensa Special Register field positions and masks.
 *
 * (In previous releases, these were defined in specreg.h, a generated file.
 *  This file is not generated, i.e. it is processor configuration independent.)
 */


/*  EXCCAUSE register fields:  */
#define EXCCAUSE_EXCCAUSE_SHIFT	0
#define EXCCAUSE_EXCCAUSE_MASK	0x3F
/*  Exception causes (mostly incomplete!):  */
#define EXCCAUSE_ILLEGAL		0
#define EXCCAUSE_SYSCALL		1
#define EXCCAUSE_IFETCHERROR		2
#define EXCCAUSE_LOADSTOREERROR		3
#define EXCCAUSE_LEVEL1INTERRUPT	4
#define EXCCAUSE_ALLOCA			5

/*  PS register fields:  */
#define PS_WOE_SHIFT		18
#define PS_WOE_MASK		0x00040000
#define PS_WOE			PS_WOE_MASK
#define PS_CALLINC_SHIFT	16
#define PS_CALLINC_MASK		0x00030000
#define PS_CALLINC(n)		(((n)&3)<<PS_CALLINC_SHIFT)	/* n = 0..3 */
#define PS_OWB_SHIFT		8
#define PS_OWB_MASK		0x00000F00
#define PS_OWB(n)		(((n)&15)<<PS_OWB_SHIFT)	/* n = 0..15 (or 0..7) */
#define PS_RING_SHIFT		6
#define PS_RING_MASK		0x000000C0
#define PS_RING(n)		(((n)&3)<<PS_RING_SHIFT)	/* n = 0..3 */
#define PS_UM_SHIFT		5
#define PS_UM_MASK		0x00000020
#define PS_UM			PS_UM_MASK
#define PS_EXCM_SHIFT		4
#define PS_EXCM_MASK		0x00000010
#define PS_EXCM			PS_EXCM_MASK
#define PS_INTLEVEL_SHIFT	0
#define PS_INTLEVEL_MASK	0x0000000F
#define PS_INTLEVEL(n)		((n)&PS_INTLEVEL_MASK)		/* n = 0..15 */
/*  Backward compatibility (deprecated):  */
#define PS_PROGSTACK_SHIFT	PS_UM_SHIFT
#define PS_PROGSTACK_MASK	PS_UM_MASK
#define PS_PROG_SHIFT		PS_UM_SHIFT
#define PS_PROG_MASK		PS_UM_MASK
#define PS_PROG			PS_UM

/*  DBREAKCn register fields:  */
#define DBREAKC_MASK_SHIFT		0
#define DBREAKC_MASK_MASK		0x0000003F
#define DBREAKC_LOADBREAK_SHIFT		30
#define DBREAKC_LOADBREAK_MASK		0x40000000
#define DBREAKC_STOREBREAK_SHIFT	31
#define DBREAKC_STOREBREAK_MASK		0x80000000

/*  DEBUGCAUSE register fields:  */
#define DEBUGCAUSE_DEBUGINT_SHIFT	5
#define DEBUGCAUSE_DEBUGINT_MASK	0x20	/* debug interrupt */
#define DEBUGCAUSE_BREAKN_SHIFT		4
#define DEBUGCAUSE_BREAKN_MASK		0x10	/* BREAK.N instruction */
#define DEBUGCAUSE_BREAK_SHIFT		3
#define DEBUGCAUSE_BREAK_MASK		0x08	/* BREAK instruction */
#define DEBUGCAUSE_DBREAK_SHIFT		2
#define DEBUGCAUSE_DBREAK_MASK		0x04	/* DBREAK match */
#define DEBUGCAUSE_IBREAK_SHIFT		1
#define DEBUGCAUSE_IBREAK_MASK		0x02	/* IBREAK match */
#define DEBUGCAUSE_ICOUNT_SHIFT		0
#define DEBUGCAUSE_ICOUNT_MASK		0x01	/* ICOUNT would increment to zero */

#endif /*XTENSA_COREBITS_H*/

