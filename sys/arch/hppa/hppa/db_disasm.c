/*	$OpenBSD: db_disasm.c,v 1.25 2025/06/28 13:24:21 miod Exp $	*/

/* TODO parse 64bit insns or rewrite */

/*
 * Copyright (c) 1999,2005 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 *  (c) Copyright 1992 HEWLETT-PACKARD COMPANY
 *
 *  To anyone who acknowledges that this file is provided "AS IS"
 *  without any express or implied warranty:
 *      permission to use, copy, modify, and distribute this file
 *  for any purpose is hereby granted without fee, provided that
 *  the above copyright notice and this notice appears in all
 *  copies, and that the name of Hewlett-Packard Company not be
 *  used in advertising or publicity pertaining to distribution
 *  of the software without specific, written prior permission.
 *  Hewlett-Packard Company makes no representations about the
 *  suitability of this software for any purpose.
 */

/*
 * unasm.c -- HP_PA Instruction Printer
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>
#include <ddb/db_interface.h>


/* IMPORTANT NOTE:
 *  All modules using this header may assume that the datatype "int" is a
 *   32-bit (or > 32-bit) signed quantity.
 */


/* Spectrum Architecturally Defined Datatypes */
struct doubleword {
	int	wd0;
	int	wd1;
};

struct quadword {
	struct	doubleword	d0;
	struct	doubleword	d1;
};

	/* datatypes for halfword and byte fields of a word are defined
	 *  in ssBits.h */

/* Memory addressing datatypes */
typedef	unsigned int	SID,	/* range [0..MAXSID] */
			PGID,	/* range [0..MAXPGID] */
			OFS,	/* range [0..MAXINT]  */
			REALADR; /* range [0..MAXINT] */


/* data sizes */
enum datasize { Byte, Halfword, Word, Doubleword, Quadword, Variable };

/* Miscellaneous datatypes */
typedef	unsigned int	FLAGS;

/* struct for entry in unwind table */
struct ute {
	int	word1;
	int	word2;
	int	word3;
	int	word4;
};
/*
 *  Header: /n/schirf/u/baford/CVS/mach4-parisc/kernel_unused/parisc/kdb/unasm.c,v 1.5 1994/07/21 22:32:05 mike Exp
 *
 *  Spectrum Instruction Set Condition Completer Bit Assignments
 *  Dan Magenheimer - 6/14/82
 *  Terrence Miller - 6/21/82
 *  Computer Research Center, Hewlett-Packard Labs
 *
 *  (c) copyright 1982
 *  (p) protected 1982
 *  The Hewlett-Packard Company
 *  Hewlett-Packard Laboratories
 *  Computer Research Center
 *  Palo Alto, California
 *
 *  *** HP Company Confidential ***
 *
 * Log: unasm.c,v
 * Revision 1.5  1994/07/21  22:32:05  mike
 * official HP copyright notice
 *
 * Revision 1.4  1992/07/08  12:19:52  dalton
 * Checkin before split to 1.0.4 release (by LBS).
 *
 * Revision 1.3  92/06/06  16:16:45  dalton
 * *** empty log message ***
 *
 * Revision 1.2  92/06/06  15:42:28  dalton
 * Changed include to be a path relative to hp800.
 *
 * Revision 1.1  92/06/06  14:05:33  dalton
 * Initial revision
 *
 * Revision 1.2  91/04/14  20:29:49  osfrcs
 * 	Initial version.
 * 	[91/03/30  09:20:34  brezak]
 *
 * Revision 1.1.2.2  91/04/02  10:42:50  brezak
 * 	Initial version.
 * 	[91/03/30  09:20:34  brezak]
 *
 * Revision 1.1.1.2  91/03/30  09:20:34  brezak
 * 	Initial version.
 *
 * Revision 1.1  88/07/11  14:05:15  14:05:15  ren (Bob Naas)
 * 	Initial revision
 *
 * Revision 5.2  87/07/02  14:45:57  14:45:57  kent (Kent McMullen)
 * added constants to support addDasm and addDCond added to ssDID.c
 *
 * Revision 5.1  87/02/27  11:12:08  11:12:08  kent (Kent McMullen)
 * update all src to 5.1
 *
 * Revision 5.0  87/02/18  16:31:15  16:31:15  kent (Kent McMullen)
 * update all revision numbers to 5.0 for release
 *
 * Revision 1.1  86/07/15  08:34:55  08:34:55  kent (Kent McMullen)
 * Initial revision
 *
 * Revision 4.1  83/10/25  17:01:22  17:01:22  djm (Daniel J Magenheimer)
 * First release for ACD v4
 *
 * Revision 3.0  83/06/13  10:22:59  djm (Daniel Magenheimer)
 * First release for distribution
 *
 *
 */


/* Arithmetic/Logical Conditions */
#define	NEV	0x0
#define	EQZ	0x2
#define	LT	0x4
#define	LE	0x6
#define	LLT	0x8
#define	NUV	0x8
#define	LLE	0xA
#define	ZNV	0xA
#define	SV	0xC
#define	OD	0xE
#define	TR	0x1
#define	NEQZ	0x3
#define	GE	0x5
#define	GT	0x7
#define	LGE	0x9
#define	UV	0x9
#define	LGT	0xB
#define	VNZ	0xB
#define	NSV	0xD
#define	EV	0xF

/* unit conditions */
#define	SBZ	0x4
#define	SHZ	0x6
#define	SDC	0x8
#define	SBC	0xC
#define	SHC	0xE
#define	NBZ	0x5
#define	NHZ	0x7
#define	NDC	0x9
#define	NBC	0xD
#define	NHC	0xF

/*field conditions */
#define XEQ	0x1
#define XLT	0x2
#define	XOD	0x3
#define XTR	0x4
#define XNE	0x5
#define XGE	0x6
#define XEV	0x7



/*
 *  These macros are designed to be portable to all machines that have
 *  a wordsize greater than or equal to 32 bits that support the portable
 *  C compiler and the standard C preprocessor.  Wordsize (default 32)
 *  and bitfield assignment (default left-to-right,  unlike VAX, PDP-11)
 *  should be predefined using the constants HOSTWDSZ and BITFRL and
 *  the C compiler "-D" flag (e.g., -DHOSTWDSZ=36 -DBITFLR for the DEC-20).
 *  Note that the macro arguments assume that the integer being referenced
 *  is a 32-bit integer (right-justified on the 20) and that bit 0 is the
 *  most significant bit.
 */

#ifndef HOSTWDSZ
#define	HOSTWDSZ	32
#endif

#ifdef	vax
#ifndef BITFLR
#define	BITFRL
#endif
#else
#define	BITFLR
#endif

/*###########################  Macros  ######################################*/

/*---------------------------------------------------------------------------
 * DeclareBitfield$Reference - Declare a structure to be used to reference
 *  a specified bitfield within an integer (using BitfR, see below).
 *  The argument "n" must be an identifier name not used elsewhere in the
 *  program , "s" and "l" must (alas!) be constants.  (Suggestion: if
 *  "s" == 2 and "l" == 8, use _b28 for "n".)  The name "BITFLR" should
 *  be pre-defined if the compiler assigns bitfields from left-to-right.
 *  The resultant macro expansion defines a structure in which the bit field
 *  starting at position "s" with length "l" may be referenced by accessing
 *  member "n".  [Note: The leftmost bits in a 36-bit word may be accessed
 *  by specifying -4 <= s < 0 on the DEC-20.]
 *---------------------------------------------------------------------------*/

#ifdef	BITFRL
#define	DeclBitfR(s,l,n) struct n { int:(HOSTWDSZ-(s)-(l)); unsigned n:l;};
#else
#define	DeclBitfR(s,l,n) struct n { int:((s)+(HOSTWDSZ-32)); unsigned n:l;};
#endif

/*---------------------------------------------------------------------------
 * Bitfield$Reference - Reference a specified bitfield within an integer.
 *  The argument "i" must be an addressable variable (i.e., not a register
 *  variable or an expression... but see BitfX below), "n" must be an
 *  identifier name declared in a DeclBitfR invocation.  The resultant
 *  macro expansion references the bit field in "i" described by the
 *  DeclBitfR invocation with the same name ("n").  BitfR may be used as
 *  an lvalue or an rvalue. (i.e., either side of an assignment statement)
 *  The "s" and "l" arguments are historical and are now unused.  (They
 *  correspond to the "s" and "l" arguments in DeclBitfR)
 *  Translates to a single instruction on both the VAX and the DEC-20.
 *---------------------------------------------------------------------------*/
#define	BitfR(i,s,l,n)	(((struct n *)&i)->n)

/*---------------------------------------------------------------------------
 * Bitfield$eXtract - Extract the specified field from an integer.  Arguments
 *  are the same as for BitfR (except no "n"), however both "s" and "l" need
 *  no longer be constants. May only be used as an rvalue. Translates to
 *  two instructions on the VAX, three on the DEC-20.
 *---------------------------------------------------------------------------*/

#define	BitfX(i,s,l)	 (((i) >> (32-(s)-(l))) & ((1 << (l)) - 1))


/*---------------------------------------------------------------------------
 * Mask$32bits - Mask the low order 32 bits of passed word.  No-op on 32
 *  bit machines.
 *---------------------------------------------------------------------------*/

#if	HOSTWDSZ > 32
#define	Mask32(x)	((x) & 0xffffffff)
#else
#define	Mask32(x)	(x)
#endif


/*---------------------------------------------------------------------------
 * SignExtend$32bits - Force the high-order bits in machines with wordsize
 *  longer than 32 to match bit 0.
 *---------------------------------------------------------------------------*/

#if	HOSTWDSZ > 32
#define	SignEx32(x)	(((x) & 0x80000000) ? ((x) | ((unsigned)-1 << 32)) \
					    : Mask32(x))
#else
#define	SignEx32(x)	(x)
#endif

/**************************/
/* bit field declarations */
/**************************/

/* since the compiler complains if a structure name is declared twice, even
 *  if the declarations are identical, all DeclBitfR invocations are
 *  given here in one file. */

DeclBitfR(0,1,_b01)
DeclBitfR(0,15,_b015)
DeclBitfR(0,16,_b016)
DeclBitfR(0,4,_b04)
DeclBitfR(0,6,_b06)
DeclBitfR(0,8,_b08)
DeclBitfR(4,1,_b41)
DeclBitfR(4,4,_b44)
DeclBitfR(6,1,_b61)
DeclBitfR(6,13,_b613)
DeclBitfR(6,15,_b615)
DeclBitfR(6,17,_b617)
DeclBitfR(6,26,_b626)
DeclBitfR(6,5,_b65)
DeclBitfR(7,1,_b71)
DeclBitfR(8,1,_b81)
DeclBitfR(8,4,_b84)
DeclBitfR(8,8,_b88)
DeclBitfR(9,1,_b91)
DeclBitfR(10,1,_b101)
DeclBitfR(11,1,_b111)
DeclBitfR(11,10,_b1110)
DeclBitfR(11,4,_b114)
DeclBitfR(11,5,_b115)
DeclBitfR(12,1,_b121)
DeclBitfR(12,4,_b124)
DeclBitfR(13,1,_b131)
DeclBitfR(14,1,_b141)
DeclBitfR(15,1,_b151)
DeclBitfR(16,1,_b161)
DeclBitfR(16,15,_b1615)
DeclBitfR(16,16,_b1616)
DeclBitfR(16,2,_b162)
DeclBitfR(16,3,_b163)
DeclBitfR(16,4,_b164)
DeclBitfR(16,5,_b165)
DeclBitfR(16,8,_b168)
DeclBitfR(17,1,_b171)
DeclBitfR(18,1,_b181)
DeclBitfR(18,13,_b1813)
DeclBitfR(18,2,_b182)
DeclBitfR(18,7,_b187)
DeclBitfR(19,1,_b191)
DeclBitfR(19,8,_b198)
DeclBitfR(19,10,_b1910)
DeclBitfR(20,11,_b2011)
DeclBitfR(20,2,_b202)
DeclBitfR(20,4,_b204)
DeclBitfR(21,10,_b2110)
DeclBitfR(21,2,_b212)
DeclBitfR(21,5,_b215)
DeclBitfR(22,5,_b225)
DeclBitfR(23,3,_b233)
DeclBitfR(24,1,_b241)
DeclBitfR(24,4,_b244)
DeclBitfR(24,8,_b248)
DeclBitfR(25,1,_b251)
DeclBitfR(26,1,_b261)
DeclBitfR(27,1,_b271)
DeclBitfR(27,4,_b274)
DeclBitfR(27,5,_b275)
DeclBitfR(28,1,_b281)
DeclBitfR(28,4,_b284)
DeclBitfR(29,1,_b291)
DeclBitfR(30,1,_b301)
DeclBitfR(30,2,_b302)
DeclBitfR(31,1,_b311)

/******************/
/* Word subfields */
/******************/

#define	Sign(i)		BitfR(i,0,1,_b01)
/* halfwords */
#define	Hwd0(i)		BitfR(i,0,16,_b016)
#define	Hwd1sign(i)	BitfR(i,16,1,_b161)
#define	Hwd1(i)		BitfR(i,16,16,_b1616)
/* bytes */
#define	Byte0(i)	BitfR(i,0,8,_b08)
#define	Byte1sign(i)	BitfR(i,8,1,_b81)
#define	Byte1(i)	BitfR(i,8,8,_b88)
#define	Byte2(i)	BitfR(i,16,8,_b168)
#define	Byte3sign(i)	BitfR(i,24,1,_b241)
#define	Byte3(i)	BitfR(i,24,8,_b248)
/* digits */
#define	Digit0(i)	BitfR(i,0,4,_b04)
#define	Digit1(i)	BitfR(i,4,4,_b44)
#define	Digit2(i)	BitfR(i,8,4,_b84)
#define	Digit3(i)	BitfR(i,12,4,_b124)
#define	Digit4(i)	BitfR(i,16,4,_b164)
#define	Digit5(i)	BitfR(i,20,4,_b204)
#define	Digit6(i)	BitfR(i,24,4,_b244)
#define	Digit7(i)	BitfR(i,28,4,_b284)

/* Wordsize definitions */

#define		BIT_P_DW	64	/* bits/doubleword */
#define		BIT_P_WD	32	/* bits/word */
#define		BIT_P_HW	16	/* bits/halfword */
#define		BIT_P_BYT	8	/* bits/byte */
#define		BYT_P_DW	8	/* bytes/doubleword */
#define		BYT_P_WD	4	/* bytes/word */
#define		BYT_P_HW	2	/* bytes/halfword */

/* Masks */

#define		WDMASK		0xffffffff	/* 32-bit mask */
#define		OFSMASK		0xffffffff	/* 32-bit mask */
#define		SIDMASK		0xffffffff	/* 32-bit mask */
#define		SIGNMASK	0x80000000	/* 32 bit word sign bit */

/* Alignments */

#define		wdalign(ofs)	(ofs &= ~3)
/*
 *  Header: /n/schirf/u/baford/CVS/mach4-parisc/kernel_unused/parisc/kdb/unasm.c,v 1.5 1994/07/21 22:32:05 mike Exp
 *
 *  Spectrum Simulator Instruction Opcode Definitions
 *  Dan Magenheimer
 *  Computer Research Center, Hewlett-Packard Labs
 *
 *  (c) copyright 1982
 *  (p) protected 1982
 *  The Hewlett-Packard Company
 *  Hewlett-Packard Laboratories
 *  Computer Research Center
 *  Palo Alto, California
 *
 *  *** HP Company Confidential ***
 *
 * Log: unasm.c,v
 * Revision 1.5  1994/07/21  22:32:05  mike
 * official HP copyright notice
 *
 * Revision 1.4  1992/07/08  12:19:52  dalton
 * Checkin before split to 1.0.4 release (by LBS).
 *
 * Revision 1.3  92/06/06  16:16:45  dalton
 * *** empty log message ***
 *
 * Revision 1.2  92/06/06  15:42:28  dalton
 * Changed include to be a path relative to hp800.
 *
 * Revision 1.1  92/06/06  14:05:33  dalton
 * Initial revision
 *
 * Revision 1.2  91/04/14  20:29:49  osfrcs
 * 	Initial version.
 * 	[91/03/30  09:20:34  brezak]
 *
 * Revision 1.1.2.2  91/04/02  10:42:50  brezak
 * 	Initial version.
 * 	[91/03/30  09:20:34  brezak]
 *
 * Revision 1.1.1.2  91/03/30  09:20:34  brezak
 * 	Initial version.
 *
 * Revision 6.1  89/09/06  10:39:58  burroughs
 * Added shadow registers for gr0-gr7.
 *     gr0-7 are copied into sh0-7 whenever a trap occurs
 *     the instruction RFIR restores gr0-7 from sh0-7 and returns from
 *     interrupt.
 *     the "sh" command displays the shadow registers
 *     = sh7 0x789 works, too.
 *
 * Revision 6.0  89/09/01  15:46:37  15:46:37  burroughs (Greg Burroughs)
 * baseline for pcx simple offsite
 *
 * Revision 5.2  87/09/02  14:30:23  14:30:23  kent
 * separated stat gathering for indexed vs short.
 * this will NOT work if cache hints ever get used
 * since this field was assumed always zero
 *
 * Revision 5.1  87/02/27  11:12:16  11:12:16  kent (Kent McMullen)
 * update all src to 5.1
 *
 * Revision 5.0  87/02/18  16:31:35  16:31:35  kent (Kent McMullen)
 * update all revision numbers to 5.0 for release
 *
 * Revision 1.1  86/07/15  08:34:57  08:34:57  kent (Kent McMullen)
 * Initial revision
 *
 * Revision 4.1  83/10/25  17:02:34  17:02:34  djm (Daniel J Magenheimer)
 * First release for ACD v4
 *
 * Revision 3.0  83/06/13  10:24:45  djm (Daniel Magenheimer)
 * First release for distribution
 *
 *
 */

/*
 * Changes:
 *   01/30/90 ejf Simplify SPOPn support, now only gives assist emulation trap.
 *   01/19/90 ejf Replace linpak instructions with just FSTQ[SX].
 *   12/19/89 ejf Add PA89 new floating point opcode 0E.
 *   12/18/89 ejf Change 5 ops to PA89 format.
 *   12/01/89 ejf Move additional instructions fmas, fmaa, fld2, fst2 to ssILst
 *   09/22/89 ejf Fix unbalanced comments.
 */


/* ..and modified by hand to remove the load/store short references */
/* ..and modified by hand to make memory management ops conform to the
 *   requirement that all subops of a major opcode begin in the same
 *   place and have the same length */

#define	LDW	0x12, 0x00, 0, 0	/* LOAD WORD */
#define	LDWM	0x13, 0x00, 0, 0	/* LOAD WORD and MODIFY */
#define	LDH	0x11, 0x00, 0, 0	/* LOAD HALFWORD */
#define	LDB	0x10, 0x00, 0, 0	/* LOAD BYTE */
#define	LDO	0x0d, 0x00, 0, 0	/* LOAD OFFSET */
#define	STW	0x1a, 0x00, 0, 0	/* STORE WORD */
#define	STWM	0x1b, 0x00, 0, 0	/* STORE WORD and MODIFY */
#define	STH	0x19, 0x00, 0, 0	/* STORE HALFWORD */
#define	STB	0x18, 0x00, 0, 0	/* STORE BYTE */
#define	LDWX	0x03, 0x02, 19, 7	/* LOAD WORD INDEXED */
#define	LDHX	0x03, 0x01, 19, 7	/* LOAD HALFWORD INDEXED */
#define	LDBX	0x03, 0x00, 19, 7	/* LOAD BYTE INDEXED */
#define	LDWAX	0x03, 0x06, 19, 7	/* LOAD WORD ABSOLUTE INDEXED */
#define	LDCWX	0x03, 0x07, 19, 7	/* LOAD and CLEAR WORD INDEXED */
#define LDWS	0x03, 0x42, 19, 7	/* LOAD WORD SHORT DISP */
#define LDHS	0x03, 0x41, 19, 7	/* LOAD HALFWORD SHORT DISP */
#define LDBS	0x03, 0x40, 19, 7	/* LOAD BYTE SHORT DISP */
#define LDWAS	0x03, 0x46, 19, 7	/* LOAD WORD ABSOLUTE SHORT DISP */
#define LDCWS	0x03, 0x47, 19, 7	/* LOAD and CLEAR WORD SHORT DISP */
#define	STWS	0x03, 0x4a, 19, 7	/* STORE WORD SHORT DISP */
#define	STHS	0x03, 0x49, 19, 7	/* STORE HALFWORD SHORT DISP */
#define	STBS	0x03, 0x48, 19, 7	/* STORE BYTE SHORT DISP */
#define	STWAS	0x03, 0x4e, 19, 7	/* STORE WORD ABSOLUTE SHORT DISP */
#define	STBYS	0x03, 0x4c, 19, 7	/* STORE BYTES SHORT DISP */
#define	LDIL	0x08, 0x00, 0, 0	/* LOAD IMMED LEFT */
#define	ADDIL	0x0a, 0x00, 0, 0	/* ADD IMMED LEFT */
#define	BL	0x3a, 0x00, 16, 3	/* BRANCH [and LINK] */
#define	GATE	0x3a, 0x01, 16, 3	/* GATEWAY */
#define	BLR	0x3a, 0x02, 16, 3	/* BRANCH and LINK REGISTER */
#define	BV	0x3a, 0x06, 16, 3	/* BRANCH VECTORED */
#define	BE	0x38, 0x00, 0, 0	/* BRANCH EXTERNAL */
#define	BLE	0x39, 0x00, 0, 0	/* BRANCH and LINK EXTERNAL */
#define	MOVB	0x32, 0x00, 0, 0	/* MOVE and BRANCH */
#define	MOVIB	0x33, 0x00, 0, 0	/* MOVE IMMED and BRANCH */
#define	COMBT	0x20, 0x00, 0, 0	/* COMPARE and BRANCH if TRUE */
#define	COMBF	0x22, 0x00, 0, 0	/* COMPARE and BRANCH if FALSE */
#define	COMIBT	0x21, 0x00, 0, 0	/* COMPARE IMMED and BRANCH if TRUE */
#define	COMIBF	0x23, 0x00, 0, 0	/* COMPARE IMMED and BRANCH if FALSE */
#define	ADDBT	0x28, 0x00, 0, 0	/* ADD and BRANCH if TRUE */
#define	ADDBF	0x2a, 0x00, 0, 0	/* ADD and BRANCH if FALSE */
#define	ADDIBT	0x29, 0x00, 0, 0	/* ADD IMMED and BRANCH if TRUE */
#define	ADDIBF	0x2b, 0x00, 0, 0	/* ADD IMMED and BRANCH if FALSE */
#define	BVB	0x30, 0x00, 0, 0	/* BRANCH on VARIABLE BIT */
#define	BB	0x31, 0x00, 0, 0	/* BRANCH on BIT */
#define	ADD	0x02, 0x30, 20, 7	/* ADD  */
#define	ADDL	0x02, 0x50, 20, 7	/* ADD LOGICAL */
#define	ADDO	0x02, 0x70, 20, 7	/* ADD and TRAP on OVFLO */
#define	SH1ADD	0x02, 0x32, 20, 7	/* SHIFT 1, ADD  */
#define	SH1ADDL	0x02, 0x52, 20, 7	/* SHIFT 1, ADD LOGICAL */
#define	SH1ADDO	0x02, 0x72, 20, 7	/* SHIFT 1, ADD and TRAP on OVFLO */
#define	SH2ADD	0x02, 0x34, 20, 7	/* SHIFT 2, ADD  */
#define	SH2ADDL	0x02, 0x54, 20, 7	/* SHIFT 2, ADD LOGICAL */
#define	SH2ADDO	0x02, 0x74, 20, 7	/* SHIFT 2, ADD and TRAP on OVFLO */
#define	SH3ADD	0x02, 0x36, 20, 7	/* SHIFT 3, ADD  */
#define	SH3ADDL	0x02, 0x56, 20, 7	/* SHIFT 3, ADD LOGICAL */
#define	SH3ADDO	0x02, 0x76, 20, 7	/* SHIFT 3, ADD and TRAP on OVFLO */
#define	ADDC	0x02, 0x38, 20, 7	/* ADD with CARRY  */
#define	ADDCO	0x02, 0x78, 20, 7	/* ADD with CARRY and TRAP on OVFLO */
#define	SUB	0x02, 0x20, 20, 7	/* SUBTRACT  */
#define	SUBO	0x02, 0x60, 20, 7	/* SUBTRACT and TRAP on OVFLO */
#define	SUBB	0x02, 0x28, 20, 7	/* SUBTRACT with BORROW  */
#define	SUBBO	0x02, 0x68, 20, 7	/* SUBTRACT with BORROW and TRAP on OVFLO */
#define	SUBT	0x02, 0x26, 20, 7	/* SUBTRACT and TRAP on COND */
#define	SUBTO	0x02, 0x66, 20, 7	/* SUBTRACT and TRAP on COND or OVFLO */
#define	DS	0x02, 0x22, 20, 7	/* DIVIDE STEP */
#define	COMCLR	0x02, 0x44, 20, 7	/* COMPARE and CLEAR */
#define	OR	0x02, 0x12, 20, 7	/* INCLUSIVE OR */
#define	XOR	0x02, 0x14, 20, 7	/* EXCLUSIVE OR */
#define	AND	0x02, 0x10, 20, 7	/* AND */
#define	ANDCM	0x02, 0x00, 20, 7	/* AND COMPLEMENT */
#define	UXOR	0x02, 0x1c, 20, 7	/* UNIT XOR */
#define	UADDCM	0x02, 0x4c, 20, 7	/* UNIT ADD COMPLEMENT */
#define	UADDCMT	0x02, 0x4e, 20, 7	/* UNIT ADD COMPLEMENT and TRAP on COND */
#define	DCOR	0x02, 0x5c, 20, 7	/* DECIMAL CORRECT */
#define	IDCOR	0x02, 0x5e, 20, 7	/* INTERMEDIATE DECIMAL CORRECT */
#define	ADDI	0x2d, 0x00, 20, 1	/* ADD to IMMED  */
#define	ADDIO	0x2d, 0x01, 20, 1	/* ADD to IMMED and TRAP on OVFLO */
#define	ADDIT	0x2c, 0x00, 20, 1	/* ADD to IMMED and TRAP on COND */
#define	ADDITO	0x2c, 0x01, 20, 1	/* ADD to IMMED and TRAP on COND or OVFLO */
#define	SUBI	0x25, 0x00, 20, 1	/* SUBTRACT from IMMED  */
#define	SUBIO	0x25, 0x01, 20, 1	/* SUBTRACT from IMMED and TRAP on OVFLO */
#define	COMICLR	0x24, 0x00, 0, 0	/* COMPARE IMMED and CLEAR */
#define	VSHD	0x34, 0x00, 19, 3	/* VARIABLE SHIFT DOUBLE */
#define	SHD	0x34, 0x02, 19, 3	/* SHIFT DOUBLE */
#define	VEXTRU	0x34, 0x04, 19, 3	/* VARIABLE EXTRACT RIGHT UNSIGNED */
#define	VEXTRS	0x34, 0x05, 19, 3	/* VARIABLE EXTRACT RIGHT SIGNED */
#define	EXTRU	0x34, 0x06, 19, 3	/* EXTRACT RIGHT UNSIGNED  */
#define	EXTRS	0x34, 0x07, 19, 3	/* EXTRACT RIGHT SIGNED */
#define	VDEP	0x35, 0x01, 19, 3	/* VARIABLE DEPOSIT */
#define	DEP	0x35, 0x03, 19, 3	/* DEPOSIT */
#define	VDEPI	0x35, 0x05, 19, 3	/* VARIABLE DEPOSIT IMMED */
#define	DEPI	0x35, 0x07, 19, 3	/* DEPOSIT IMMED */
#define	ZVDEP	0x35, 0x00, 19, 3	/* ZERO and VARIABLE DEPOSIT */
#define	ZDEP	0x35, 0x02, 19, 3	/* ZERO and DEPOSIT */
#define	ZVDEPI	0x35, 0x04, 19, 3	/* ZERO and VARIABLE DEPOSIT IMMED */
#define	ZDEPI	0x35, 0x06, 19, 3	/* ZERO and DEPOSIT IMMED */
#define	BREAK	0x00, 0x00, 19, 8	/* BREAK */
#define	RFI	0x00, 0x60, 19, 8	/* RETURN FROM INTERRUPTION */
#define	RFIR	0x00, 0x65, 19, 8	/* RFI & RESTORE SHADOW REGISTERS */
#define	SSM	0x00, 0x6b, 19, 8	/* SET SYSTEM MASK */
#define	RSM	0x00, 0x73, 19, 8	/* RESET SYSTEM MASK */
#define	MTSM	0x00, 0xc3, 19, 8	/* MOVE TO SYSTEM MASK */
#define	LDSID	0x00, 0x85, 19, 8	/* LOAD SPACE IDENTIFIER */
#define	MTSP	0x00, 0xc1, 19, 8	/* MOVE TO SPACE REGISTER */
#define	MTCTL	0x00, 0xc2, 19, 8	/* MOVE TO SYSTEM CONTROL REGISTER */
#define	MFSP	0x00, 0x25, 19, 8	/* MOVE FROM SPACE REGISTER */
#define	MFCTL	0x00, 0x45, 19, 8	/* MOVE FROM SYSTEM CONTROL REGISTER */
#define	SYNC	0x00, 0x20, 19, 8	/* SYNCHRONIZE DATA CACHE */
#define	DIAG	0x05, 0x00, 0, 0	/* DIAGNOSE */
#define	SPOP	0x04, 0x00, 0, 0	/* SPECIAL FUNCTION UNIT */
#define	COPR	0x0c, 0x00, 0, 0	/* COPROCESSOR */
#define	CLDWX	0x09, 0x00, 19, 4	/* COPROCESSOR LOAD WORD INDEXED */
#define	CLDDX	0x0b, 0x00, 19, 4	/* COPROCESSOR LOAD WORD INDEXED */
#define	CSTWX	0x09, 0x01, 19, 4	/* COPROCESSOR STORE WORD INDEXED */
#define	CSTDX	0x0b, 0x01, 19, 4	/* COPROCESSOR STORE WORD INDEXED */
#define CLDWS	0x09, 0x08, 19, 4	/* COPROCESSOR LOAD WORD SHORT */
#define CLDDS	0x0b, 0x08, 19, 4	/* COPROCESSOR LOAD WORD SHORT */
#define CSTWS	0x09, 0x09, 19, 4	/* COPROCESSOR STORE WORD SHORT */
#define CSTDS	0x0b, 0x09, 19, 4	/* COPROCESSOR STORE WORD SHORT */
#define	FLOAT0	0x0e, 0x00, 21, 2	/* FLOATING POINT CLASS 0 */
#define	FLOAT1	0x0e, 0x01, 21, 2	/* FLOATING POINT CLASS 1 */
#define	FLOAT2	0x0e, 0x02, 21, 2	/* FLOATING POINT CLASS 2 */
#define	FLOAT3	0x0e, 0x03, 21, 2	/* FLOATING POINT CLASS 3 */
#define	FMPYSUB	0x26, 0x00, 0, 0	/* FP MULTIPLY AND SUBTRACT */
#define	FMPYADD	0x06, 0x00, 0, 0	/* FP MULTIPLY AND ADD/TRUNCATE */
#define	FSTQX	0x0f, 0x01, 19, 4	/* FLOATING POINT STORE QUAD INDEXED */
#define FSTQS	0x0f, 0x09, 19, 4	/* FLOATING POINT STORE QUAD SHORT */
/* all of the following have been pushed around to conform */
#define	PROBER	0x01, 0x46, 19, 7	/* PROBE READ ACCESS */
#ifdef notdef
#define PROBERI 0x01, 0xc6, 19, 7	/* PROBE READ ACCESS IMMEDIATE */
#endif
#define	PROBEW	0x01, 0x47, 19, 7	/* PROBE WRITE ACCESS */
#ifdef notdef
#define PROBEWI 0x01, 0xc7, 19, 7	/* PROBE WRITE ACCESS IMMEDIATE */
#endif
#define	LPA	0x01, 0x4d, 19, 7	/* LOAD PHYSICAL ADDRESS */
#define	LHA	0x01, 0x4c, 19, 7	/* LOAD HASH ADDRESS */
#define	PDTLB	0x01, 0x48, 19, 7	/* PURGE DATA TRANS LOOKASIDE BUFFER */
#define	PITLB	0x01, 0x08, 19, 7	/* PURGE INST TRANS LOOKASIDE BUFFER */
#define	PDTLBE	0x01, 0x49, 19, 7	/* PURGE DATA TLB ENTRY */
#define	PITLBE	0x01, 0x09, 19, 7	/* PURGE INST TLB ENTRY */
#define	IDTLBA	0x01, 0x41, 19, 7	/* INSERT DATA TLB ADDRESS */
#define	IITLBA	0x01, 0x01, 19, 7	/* INSERT INSTRUCTION TLB ADDRESS */
#define	IDTLBP	0x01, 0x40, 19, 7	/* INSERT DATA TLB PROTECTION */
#define	IITLBP	0x01, 0x00, 19, 7	/* INSERT INSTRUCTION TLB PROTECTION */
#define	PDC	0x01, 0x4e, 19, 7	/* PURGE DATA CACHE */
#define	FDC	0x01, 0x4a, 19, 7	/* FLUSH DATA CACHE */
#define	FIC	0x01, 0x0a, 19, 7	/* FLUSH INSTRUCTION CACHE */
#define	FDCE	0x01, 0x4b, 19, 7	/* FLUSH DATA CACHE ENTRY */
#define	FICE	0x01, 0x0b, 19, 7	/* FLUSH DATA CACHE ENTRY */

/*
 *  Header: /n/schirf/u/baford/CVS/mach4-parisc/kernel_unused/parisc/kdb/unasm.c,v 1.5 1994/07/21 22:32:05 mike Exp
 *
 *  Spectrum Simulator Instruction Set Constants and Datatypes
 *  Dan Magenheimer - 4/28/82
 *  Computer Research Center, Hewlett-Packard Labs
 *
 *  (c) copyright 1982
 *  (p) protected 1982
 *  The Hewlett-Packard Company
 *  Hewlett-Packard Laboratories
 *  Computer Research Center
 *  Palo Alto, California
 *
 *  *** HP Company Confidential ***
 *
 * Log: unasm.c,v
 * Revision 1.5  1994/07/21  22:32:05  mike
 * official HP copyright notice
 *
 * Revision 1.4  1992/07/08  12:19:52  dalton
 * Checkin before split to 1.0.4 release (by LBS).
 *
 * Revision 1.3  92/06/06  16:16:45  dalton
 * *** empty log message ***
 *
 * Revision 1.2  92/06/06  15:42:28  dalton
 * Changed include to be a path relative to hp800.
 *
 * Revision 1.1  92/06/06  14:05:33  dalton
 * Initial revision
 *
 * Revision 1.2  91/04/14  20:29:49  osfrcs
 * 	Initial version.
 * 	[91/03/30  09:20:34  brezak]
 *
 * Revision 1.1.2.2  91/04/02  10:42:50  brezak
 * 	Initial version.
 * 	[91/03/30  09:20:34  brezak]
 *
 * Revision 1.1.1.2  91/03/30  09:20:34  brezak
 * 	Initial version.
 *
;Revision 1.1  88/07/11  14:05:21  14:05:21  ren (Bob Naas)
;Initial revision
;
 * Revision 5.1  87/02/27  11:12:23  11:12:23  kent (Kent McMullen)
 * update all src to 5.1
 *
 * Revision 5.0  87/02/18  16:31:52  16:31:52  kent (Kent McMullen)
 * update all revision numbers to 5.0 for release
 *
 * Revision 1.1  86/07/15  08:35:00  08:35:00  kent (Kent McMullen)
 * Initial revision
 *
 * Revision 4.3  85/11/12  09:28:44  09:28:44  viggy (Viggy Mokkarala)
 * first mpsim version, partially stable
 *
 * Revision 4.2  84/07/16  17:20:57  17:20:57  djm ()
 * Define field macros for COPR and SFU insts
 *
 * Revision 4.1  83/10/25  17:10:14  djm (Daniel Magenheimer)
 * First release for ACD v4
 *
 * Revision 3.1  83/08/03  14:09:59  djm (Daniel Magenheimer)
 * Sys calls, args, -S, bug fixes, etc.
 *
 * Revision 3.0  83/06/13  10:25:13  djm (Daniel Magenheimer)
 * First release for distribution
 *
 *
 */
/*
 * Changes:
 *   12/01/89 ejf Add Rsd(), Rse(), Rtd(), Rte() for 5 ops.
 *   11/30/89 ejf Make instruction use counters shared, not per cpu.
 *   11/28/89 ejf Change majoropcode for quicker extension extract.
 */



/*
 *  Dependencies: std.h, ssDefs.h, bits.h
 */


/* Lookup/Execute structure for instructions */
struct inst {
	u_char	majopc;		/* major opcode of instruction, 0..MAXOPC */
	u_char	opcext;		/* opcode extension, 0 if not applic. */
	u_char	extbs;		/* starting bit pos of extension field */
	u_char	extbl;		/* bit length of extension field */
	u_int	count;		/* frequency counter for analysis */
	char	mnem[8];	/* ascii mnemonic */
				/* disassembly function */
	int	(*dasmfcn)(const struct inst *, OFS, int);
};


#define	NMAJOPCS	64

struct majoropcode {
	const struct inst **subops; /* pointer to table of subops indexed by
				     *  opcode extension */
	u_int	maxsubop;	/* largest opcode extension value or 0 */
	u_int	extshft;	/* right shift amount for extension field */
	u_int	extmask;	/* post shift mask for extension field */
};

#define	OpExt(i,m)	((i >> m->extshft) & m->extmask)	/* extract opcode extension */


/*****************************/
/* Miscellaneous definitions */
/*****************************/

/* Load/Store Indexed Opcode Extension Cache Control */
#define	NOACTION	0
#define	STACKREF	1
#define	SEQPASS		2
#define	PREFETCH	3

/******************************/
/* Fields within instructions */
/******************************/

/* opcode */
#define	Opcode(i)	BitfR(i,0,6,_b06)
/* opcode true/false bit */
#define	OpcTF(i)	BitfR(i,4,1,_b41)
/* register sources */
#define	Rsa(i)		BitfR(i,11,5,_b115)
#define	Rsb(i)		BitfR(i,6,5,_b65)
#define	Rsc(i)		BitfR(i,27,5,_b275)
#define	Rsd(i)		BitfR(i,21,5,_b215)
#define	Rse(i)		BitfR(i,16,5,_b165)
/* register targets */
#define	Rta(i)		BitfR(i,11,5,_b115)
#define	Rtb(i)		BitfR(i,6,5,_b65)
#define	Rtc(i)		BitfR(i,27,5,_b275)
#define	Rtd(i)		BitfR(i,21,5,_b215)
#define	Rte(i)		BitfR(i,16,5,_b165)
/* 5-bit immediates (Magnitude, Sign) */
#define	Imb5(i)		BitfR(i,6,5,_b65)
#define	Ima5M(i)	BitfR(i,11,4,_b114)
#define	Ima5S(i)	BitfR(i,15,1,_b151)
#define	Ima5A(i)	BitfR(i,11,5,_b115)
#define	Imd5(i)		BitfR(i,22,5,_b225)
#define	Imc5M(i)	BitfR(i,27,4,_b274)
#define	Imc5S(i)	BitfR(i,31,1,_b311)
#define	Imc5A(i)	BitfR(i,27,5,_b275)
/* Other immediates */
#define	Im21L(i)	BitfR(i,18,2,_b182)
#define	Im21H(i)	BitfR(i,20,11,_b2011)
#define	Im21M1(i)	BitfR(i,16,2,_b162)
#define	Im21M2(i)	BitfR(i,11,5,_b115)
#define	Im21S(i)	BitfR(i,31,1,_b311)
#define	Im11M(i)	BitfR(i,21,10,_b2110)
#define	Im11S(i)	BitfR(i,31,1,_b311)
/* displacements/offsets */
#define	DispM(i)	BitfR(i,18,13,_b1813)
#define	DispS(i)	BitfR(i,31,1,_b311)
#define	Off5(i)		BitfR(i,11,5,_b115)
#define	Off11H(i)	BitfR(i,19,10,_b1910)
#define	Off11L(i)	BitfR(i,29,1,_b291)
#define	OffS(i)		BitfR(i,31,1,_b311)
/* miscellaneous */
#define	Dss(i)		BitfR(i,16,2,_b162)
#define	Cond(i)		BitfR(i,16,3,_b163)
#define	Cneg(i)		BitfR(i,19,1,_b191)
#define	Cond4(i)	BitfR(i,16,4,_b164)	/* Cond AND Cneg */
#define	Nu(i)		BitfR(i,30,1,_b301)
#define	SrL(i)		BitfR(i,16,2,_b162)
#define	SrH(i)		BitfR(i,18,1,_b181)
#define	ShortDisp(i)	BitfR(i,19,1,_b191)
#define	IndxShft(i)	BitfR(i,18,1,_b181)
#define	ModBefore(i)	BitfR(i,18,1,_b181)
#define	CacheCtrl(i)	BitfR(i,20,2,_b202)
#define	Modify(i)	BitfR(i,26,1,_b261)
#define	ProbeI(i)	BitfR(i,18,1,_b181)
#define	Uid(i)		BitfR(i,23,3,_b233)
#define	Sfu(i)		BitfR(i,23,3,_b233)
#define	CopExt17(i)	BitfR(i,6,17,_b617)
#define	CopExt5(i)	BitfR(i,27,5,_b275)
#define	SpopType(i)	BitfR(i,21,2,_b212)
#define	SpopExt15(i)	BitfR(i,6,15,_b615)
#define	SpopExt10(i)	BitfR(i,11,10,_b1110)
#define	SpopExt5L(i)	BitfR(i,16,5,_b165)
#define	SpopExt5(i)	BitfR(i,27,5,_b275)
#define	NoMajOpc(i)	BitfR(i,6,26,_b626)
#define	Bi1(i)		BitfR(i,27,5,_b275)	/* fields in BREAK */
#define	Bi2(i)		BitfR(i,6,13,_b613)

/* fragmented field collating macros */
#define	Ima5(i)		(Ima5S(i) ? Ima5M(i) | (-1<<4) : Ima5M(i))

#define	Imc5(i)		(Imc5S(i) ? Imc5M(i) | (-1<<4) : Imc5M(i))

#define	Disp(i)		(DispS(i) ?   DispM(i) | (-1<<13) : DispM(i))

#define	Im21(i)		(Im21S(i) << 31 | Im21H(i) << 20 | Im21M1(i) << 18 | \
				Im21M2(i) << 13 | Im21L(i) << 11)

#define	Im11(i)		(Im11S(i) ?   Im11M(i) | (-1<<10) : Im11M(i))

#define	Bdisp(i)	((OffS(i) ? (Off5(i)<<11 | Off11L(i)<<10|Off11H(i)) \
/* branch displacement (bytes) */	| (-1 << 16)			\
				  : (Off5(i)<<11|Off11L(i)<<10|Off11H(i))) << 2)

#define	Cbdisp(i)	((OffS(i) ?   (Off11L(i) << 10 | Off11H(i)) \
 /* compare/branch disp (bytes) */ | (-1 << 11)			\
				  :    Off11L(i) << 10 | Off11H(i)) << 2)

#define	Sr(i)		(SrH(i)<<2 | SrL(i))

/* sfu/copr */
#define	CoprExt1(i)	(CopExt17(i))
#define	CoprExt2(i)	(CopExt5(i))
#define	CoprExt(i)	((CopExt17(i)<<5) | CopExt5(i))
#define	Spop0Ext(i)	((SpopExt15(i)<<5) | SpopExt5(i))
#define	Spop1Ext(i)	(SpopExt15(i))
#define	Spop2Ext(i)	((SpopExt10(i)<<5) | SpopExt5(i))
#define	Spop3Ext(i)	((SpopExt5L(i)<<5) | SpopExt5(i))


/*##################### Globals - Imports ##################################*/

/* Disassembly functions */
int fcoprDasm(int w, u_int op1, u_int);
char *edDCond(u_int cond);
char *unitDCond(u_int cond);
char *addDCond(u_int cond);
char *subDCond(u_int cond);
int blDasm(const struct inst *i, OFS ofs, int w);
int ldDasm(const struct inst *, OFS, int);
int stDasm(const struct inst *i, OFS, int);
int addDasm(const struct inst *i, OFS, int);
int unitDasm(const struct inst *i, OFS, int);
int iaDasm(const struct inst *i, OFS, int);
int shdDasm(const struct inst *i, OFS, int);
int extrDasm(const struct inst *i, OFS, int);
int vextrDasm(const struct inst *i, OFS, int);
int depDasm(const struct inst *i, OFS, int);
int vdepDasm(const struct inst *i, OFS, int);
int depiDasm(const struct inst *i, OFS, int);
int vdepiDasm(const struct inst *i, OFS, int);
int limmDasm(const struct inst *i, OFS, int);
int brkDasm(const struct inst *i, OFS, int);
int lpkDasm(const struct inst *i, OFS, int);
int fmpyaddDasm(const struct inst *i, OFS, int);
int fmpysubDasm(const struct inst *i, OFS, int);
int floatDasm(const struct inst *i, OFS, int);
int coprDasm(const struct inst *i, OFS, int);
int diagDasm(const struct inst *i, OFS, int);
int scDasm(const struct inst *i, OFS, int);
int mmgtDasm(const struct inst *i, OFS, int);
int ldxDasm(const struct inst *i, OFS, int);
int stsDasm(const struct inst *i, OFS, int);
int stbysDasm(const struct inst *i, OFS, int);
int brDasm(const struct inst *i, OFS, int);
int bvDasm(const struct inst *i, OFS, int);
int beDasm(const struct inst *i, OFS, int);
int cbDasm(const struct inst *i,OFS ofs, int);
int cbiDasm(const struct inst *i,OFS ofs, int);
int bbDasm(const struct inst *i,OFS ofs, int);
int ariDasm(const struct inst *i, OFS, int);

/*##################### Globals - Exports ##################################*/
/*##################### Local Variables ####################################*/

static	const char	fmtStrTbl[][5] = { "sgl", "dbl", "sgl", "quad" };
static	const char	condStrTbl[][7] = {
	    "false?", "false", "?", "!<=>", "=", "=t", "?=", "!<>",
	    "!?>=", "<", "?<", "!>=", "!?>", "<=", "?<=", "!>",
	    "!?<=", ">", "?>", "!<=", "!?<", ">=", "?>=", "!<",
	    "!?=", "<>", "!=", "!=t", "!?", "<=>", "true?", "true"
};
static	const char	fsreg[][5] = {
	    "r0L",  "r0R",  "r1L",  "r1R",  "r2L",  "r2R",  "r3L",  "r3R",
	    "r4L",  "r4R",  "r5L",  "r5R",  "r6L",  "r6R",  "r7L",  "r7R",
	    "r8L",  "r8R",  "r9L",  "r9R",  "r10L", "r10R", "r11L", "r11R",
	    "r12L", "r12R", "r13L", "r13R", "r14L", "r14R", "r15L", "r15R",
	    "r16L", "r16R", "r17L", "r17R", "r18L", "r18R", "r19L", "r19R",
	    "r20L", "r20R", "r21L", "r21R", "r22L", "r22R", "r23L", "r23R",
	    "r24L", "r24R", "r25L", "r25R", "r26L", "r26R", "r27L", "r27R",
	    "r28L", "r28R", "r29L", "r29R", "r30L", "r30R", "r31L", "r31R"
};
static	const char	fdreg[][4] = {
	    "r0",   "r0",   "r1",   "r1",   "r2",   "r2",   "r3",   "r3",
	    "r4",   "r4",   "r5",   "r5",   "r6",   "r6",   "r7",   "r7",
	    "r8",   "r8",   "r9",   "r9",   "r10",  "r10",  "r11",  "r11",
	    "r12",  "r12",  "r13",  "r13",  "r14",  "r14",  "r15",  "r15",
	    "r16",  "r16",  "r17",  "r17",  "r18",  "r18",  "r19",  "r19",
	    "r20",  "r20",  "r21",  "r21",  "r22",  "r22",  "r23",  "r23",
	    "r24",  "r24",  "r25",  "r25",  "r26",  "r26",  "r27",  "r27",
	    "r28",  "r28",  "r29",  "r29",  "r30",  "r30",  "r31",  "r31"
};

/*##################### Macros #############################################*/

#define	Match(s)	(strncmp(s,i->mnem,sizeof(s)-1) == 0)

/* bits for assist ops */
#define	AstNu(w)	Modify(w)
#define	Fpi(w)		(Uid(w)>3)

/* bits for 5 ops */
#define	SinglePrec(i)	Modify(i)
#define	Ms1(i)		((Rsb(i)<<1)+(SinglePrec(i)?((Rsb(i)>15)?1:32):0))
#define	Ms2(i)		((Rsa(i)<<1)+(SinglePrec(i)?((Rsa(i)>15)?1:32):0))
#define	Mt(i)		((Rtc(i)<<1)+(SinglePrec(i)?((Rtc(i)>15)?1:32):0))
#define	As(i)		((Rsd(i)<<1)+(SinglePrec(i)?((Rsd(i)>15)?1:32):0))
#define	Ad(i)		((Rte(i)<<1)+(SinglePrec(i)?((Rte(i)>15)?1:32):0))

/*##################### Globals - Exports ##################################*/

/* To replace instr function, do the following:				*/
/*	a) locate the desired entry in instrs[] below			*/
/*	b) change the 3rd field if an alternate mneumonic is 		*/
/*	   desired for window disassembly				*/
/*	c) change the 4th field to the name of the function being	*/
/* 	   used for replacement (i.e. ldwRepl instead of ldw)		*/
/*	d) change the 5th field if an alternate disassembly routine	*/
/*	   is desired (i.e. ldDasmRepl)					*/

static const struct inst instrs[] = {
	{ LDW,    0, "ldw",	ldDasm },
	{ LDH,    0, "ldh",	ldDasm },
	{ LDB,    0, "ldb",	ldDasm },
	{ LDWM,   0, "ldwm",    ldDasm },
	{ LDO,    0, "ldo",     ldDasm },
	{ STW,    0, "stw",     stDasm },
	{ STH,    0, "sth",     stDasm },
	{ STB,    0, "stb",     stDasm },
	{ STWM,   0, "stwm",    stDasm },
	{ LDWX,   0, "ldw",	ldxDasm },
	{ LDHX,   0, "ldh",	ldxDasm },
	{ LDBX,   0, "ldb",	ldxDasm },
	{ LDCWX,  0, "ldcw",	ldxDasm },
	{ LDWAX,  0, "ldwa",	ldxDasm },
	{ LDWS,   0, "ldw",	ldxDasm },
	{ LDHS,   0, "ldh",	ldxDasm },
	{ LDBS,   0, "ldb",	ldxDasm },
	{ LDCWS,  0, "ldcw",	ldxDasm },
	{ LDWAS,  0, "ldwa",	ldxDasm },
	{ STWS,   0, "stws",    stsDasm },
	{ STHS,   0, "sths",    stsDasm },
	{ STBS,   0, "stbs",    stsDasm },
	{ STWAS,  0, "stwas",   stsDasm },
	{ STBYS,  0, "stbys",   stbysDasm },
	{ LDIL,   0, "ldil",    limmDasm },
	{ ADDIL,  0, "addil",   limmDasm },
	{ GATE,   0, "gate",    blDasm },
	{ BL,     0, "b",	blDasm },
	{ BLR,    0, "blr",     brDasm },
	{ BV,     0, "bv",      bvDasm },
	{ BE,     0, "be",      beDasm },
	{ BLE,    0, "ble",     beDasm },
	{ COMBT,  0, "combt",   cbDasm },
	{ COMBF,  0, "combf",   cbDasm },
	{ COMIBT, 0, "comibt",  cbiDasm },
	{ COMIBF, 0, "comibf",  cbiDasm },
	{ ADDBT,  0, "addbt",   cbDasm },
	{ ADDBF,  0, "addbf",   cbDasm },
	{ ADDIBT, 0, "addibt",  cbiDasm },
	{ ADDIBF, 0, "addibf",  cbiDasm },
	{ MOVB,   0, "movb",    cbDasm },
	{ MOVIB,  0, "movib",   cbiDasm },
	{ BB,     0, "bb",      bbDasm },
	{ BVB,    0, "bvb",     bbDasm },
	{ SUBO,   0, "subo",    ariDasm },
	{ ADD,    0, "add",     addDasm },
	{ ADDL,   0, "addl",    addDasm },
	{ ADDO,   0, "addo",    ariDasm },
	{ SH1ADD, 0, "sh1add",  ariDasm },
	{ SH1ADDL,0, "sh1addl", ariDasm },
	{ SH1ADDO,0, "sh1addo", ariDasm },
	{ SH2ADD, 0, "sh2add",  ariDasm },
	{ SH2ADDL,0, "sh2addl", ariDasm },
	{ SH2ADDO,0, "sh2addo", ariDasm },
	{ SH3ADD, 0, "sh3add",  ariDasm },
	{ SH3ADDL,0, "sh3addl", ariDasm },
	{ SH3ADDO,0, "sh3addo", ariDasm },
	{ SUB,    0, "sub",     ariDasm },
	{ ADDCO,  0, "addco",   ariDasm },
	{ SUBBO,  0, "subbo",   ariDasm },
	{ ADDC,   0, "addc",    ariDasm },
	{ SUBB,   0, "subb",    ariDasm },
	{ COMCLR, 0, "comclr",  ariDasm },
	{ OR,     0, "or",      ariDasm },
	{ AND,    0, "and",     ariDasm },
	{ XOR,    0, "xor",     ariDasm },
	{ ANDCM,  0, "andcm",   ariDasm },
	{ DS,     0, "ds",      ariDasm },
	{ UXOR,   0, "uxor",    unitDasm },
	{ UADDCM, 0, "uaddcm",  unitDasm },
	{ UADDCMT,0, "uaddcmt", unitDasm },
	{ SUBTO,  0, "subto",   ariDasm },
	{ SUBT,   0, "subt",    ariDasm },
	{ DCOR,   0, "dcor",    unitDasm },
	{ IDCOR,  0, "idcor",   unitDasm },
	{ ADDIO,  0, "addio",   iaDasm },
	{ SUBIO,  0, "subio",   iaDasm },
	{ ADDI,   0, "addi",    iaDasm },
	{ SUBI,   0, "subi",    iaDasm },
	{ COMICLR,0, "comiclr", iaDasm },
	{ ADDITO, 0, "addito",  iaDasm },
	{ ADDIT,  0, "addit",   iaDasm },
	{ SHD,    0, "shd",     shdDasm },
	{ VSHD,   0, "vshd",    shdDasm },
	{ EXTRU,  0, "extru",   extrDasm },
	{ EXTRS,  0, "extrs",   extrDasm },
	{ VEXTRU, 0, "vextru",  vextrDasm },
	{ VEXTRS, 0, "vextrs",  vextrDasm },
	{ DEP,    0, "dep",     depDasm },
	{ VDEP,   0, "vdep",    vdepDasm },
	{ DEPI,   0, "depi",    depiDasm },
	{ VDEPI,  0, "vdepi",   vdepiDasm },
	{ ZDEP,   0, "zdep",    depDasm },
	{ ZVDEP,  0, "zvdep",   vdepDasm },
	{ ZDEPI,  0, "zdepi",   depiDasm },
	{ ZVDEPI, 0, "zvdepi",  vdepiDasm },
	{ BREAK,  0, "break",   brkDasm },
	{ RFI,    0, "rfi",     0 },
	{ RFIR,   0, "rfir",    0 },
	{ SSM,    0, "ssm",     scDasm },
	{ RSM,    0, "rsm",     scDasm },
	{ MTSM,   0, "mtsm",    scDasm },
	{ PROBER, 0, "prober",  mmgtDasm },
	{ PROBEW, 0, "probew",  mmgtDasm },
	{ LPA,    0, "lpa",     mmgtDasm },
	{ LHA,    0, "lha",     mmgtDasm },
	{ LDSID,  0, "ldsid",   scDasm },
	{ PDTLB,  0, "pdtlb",   mmgtDasm },
	{ PDTLBE, 0, "pdtlbe",  mmgtDasm },
	{ PITLB,  0, "pitlb",   mmgtDasm },
	{ PITLBE, 0, "pitlbe",  mmgtDasm },
	{ IDTLBA, 0, "idtlba",  mmgtDasm },
	{ IITLBA, 0, "iitlba",  mmgtDasm },
	{ IDTLBP, 0, "idtlbp",  mmgtDasm },
	{ IITLBP, 0, "iitlbp",  mmgtDasm },
	{ FIC,    0, "fic",     mmgtDasm },
	{ FICE,   0, "fice",    mmgtDasm },
	{ PDC,    0, "pdc",     mmgtDasm },
	{ FDC,    0, "fdc",     mmgtDasm },
	{ FDCE,   0, "fdce",    mmgtDasm },
	{ SYNC,   0, "sync",    0 },
	{ MTSP,   0, "mtsp",    scDasm },
	{ MTCTL,  0, "mtctl",   scDasm },
	{ MFSP,   0, "mfsp",    scDasm },
	{ MFCTL,  0, "mfctl",   scDasm },
	{ DIAG,   0, "diag",    diagDasm },
	{ SPOP,   0, "???",     0 },
	{ COPR,   0, "copr",    coprDasm },
	{ CLDWX,  0, "cldw",    coprDasm },
	{ CLDDX,  0, "cldd",    coprDasm },
	{ CSTWX,  0, "cstw",    coprDasm },
	{ CSTDX,  0, "cstd",    coprDasm },
	{ CLDWS,  0, "cldw",    coprDasm },
	{ CLDDS,  0, "cldd",    coprDasm },
	{ CSTWS,  0, "cstw",    coprDasm },
	{ CSTDS,  0, "cstd",    coprDasm },
	{ FLOAT0, 0, "f",       floatDasm },
	{ FLOAT1, 0, "fcnv",    floatDasm },
	{ FLOAT2, 0, "f",       floatDasm },
	{ FLOAT3, 0, "f",       floatDasm },
	{ FMPYSUB,0, "fmpy",    fmpysubDasm },
	{ FMPYADD,0, "fmpy",    fmpyaddDasm },
	{ FSTQX,  0, "fstqx",   lpkDasm  },
	{ FSTQS,  0, "fstqs",   lpkDasm  },
	{0}
};


static const struct inst illeg = { 0, 0, 0, 0, 0, "???", 0 };
static const struct inst *so_sysop[0xd0];
static const struct inst *so_mmuop[0x50];
static const struct inst *so_arith[0x80];
static const struct inst *so_loads[0x50];
static const struct inst *so_cldw [0x0A];
static const struct inst *so_cldd [0x0A];
static const struct inst *so_float[0x04];
static const struct inst *so_fstq [0x0A];
static const struct inst *so_ebran[0x08];
static const struct inst *so_addit[0x02];
static const struct inst *so_addi [0x02];
static const struct inst *so_subi [0x02];
static const struct inst *so_shext[0x08];
static const struct inst *so_deps [0x08];

#define ILLEG (const struct inst **)&illeg
#define NENTS(a) (sizeof(a)/sizeof(a[0])-1)
static struct majoropcode majopcs[NMAJOPCS] = {
	{ so_sysop, NENTS(so_sysop) }, /* 00 */
	{ so_mmuop, NENTS(so_mmuop) }, /* 01 */
	{ so_arith, NENTS(so_arith) }, /* 02 */
	{ so_loads, NENTS(so_loads) }, /* 03 */
	{ ILLEG, 1 }, /* 04 */
	{ ILLEG, 1 }, /* 05 */
	{ ILLEG, 1 }, /* 06 */
	{ ILLEG, 1 }, /* 07 */
	{ ILLEG, 1 }, /* 08 */
	{ so_cldw , NENTS(so_cldw ) }, /* 09 */
	{ ILLEG, 1 }, /* 0A */
	{ so_cldd , NENTS(so_cldd ) }, /* 0B */
	{ ILLEG, 1 }, /* 0C */
	{ ILLEG, 1 }, /* 0D */
	{ so_float, NENTS(so_float) }, /* 0E */
	{ so_fstq , NENTS(so_fstq ) }, /* 0F */
	{ ILLEG, 1 }, /* 10 */
	{ ILLEG, 1 }, /* 11 */
	{ ILLEG, 1 }, /* 12 */
	{ ILLEG, 1 }, /* 13 */
	{ ILLEG, 1 }, /* 14 */
	{ ILLEG, 1 }, /* 15 */
	{ ILLEG, 1 }, /* 16 */
	{ ILLEG, 1 }, /* 17 */
	{ ILLEG, 1 }, /* 18 */
	{ ILLEG, 1 }, /* 19 */
	{ ILLEG, 1 }, /* 1A */
	{ ILLEG, 1 }, /* 1B */
	{ ILLEG, 1 }, /* 1C */
	{ ILLEG, 1 }, /* 1D */
	{ ILLEG, 1 }, /* 1E */
	{ ILLEG, 1 }, /* 1F */
	{ ILLEG, 1 }, /* 20 */
	{ ILLEG, 1 }, /* 21 */
	{ ILLEG, 1 }, /* 22 */
	{ ILLEG, 1 }, /* 23 */
	{ ILLEG, 1 }, /* 24 */
	{ so_subi , NENTS(so_subi ) }, /* 25 */
	{ ILLEG, 1 }, /* 26 */
	{ ILLEG, 1 }, /* 27 */
	{ ILLEG, 1 }, /* 28 */
	{ ILLEG, 1 }, /* 29 */
	{ ILLEG, 1 }, /* 2A */
	{ ILLEG, 1 }, /* 2B */
	{ so_addit, NENTS(so_addit) }, /* 2C */
	{ so_addi , NENTS(so_addi ) }, /* 2D */
	{ ILLEG, 1 }, /* 2E */
	{ ILLEG, 1 }, /* 2F */
	{ ILLEG, 1 }, /* 30 */
	{ ILLEG, 1 }, /* 31 */
	{ ILLEG, 1 }, /* 32 */
	{ ILLEG, 1 }, /* 33 */
	{ so_shext, NENTS(so_shext) }, /* 34 */
	{ so_deps , NENTS(so_deps ) }, /* 35 */
	{ ILLEG, 1 }, /* 36 */
	{ ILLEG, 1 }, /* 37 */
	{ ILLEG, 1 }, /* 38 */
	{ ILLEG, 1 }, /* 39 */
	{ so_ebran, NENTS(so_ebran) }, /* 3A */
	{ ILLEG, 1 }, /* 3B */
	{ ILLEG, 1 }, /* 3C */
	{ ILLEG, 1 }, /* 3D */
	{ ILLEG, 1 }, /* 3E */
	{ ILLEG, 1 }, /* 3F */
};
#undef NENTS
#undef ILLEG

int iExInit(void);

/*--------------------------------------------------------------------------
 * instruction$ExecutionInitialize - Initialize the instruction execution
 *  data structures.
 *---------------------------------------------------------------------------*/
int
iExInit(void)
{
	static int unasm_initted = 0;
	register const struct inst *i;
	register struct majoropcode *m;
	u_int	shft, mask;

	if (unasm_initted)
		return 1;

	/*
	 * Determine maxsubop for each major opcode.
	 * Also, check all instructions of a given major opcode
	 * for consistent opcode extension field definition, and
	 * save a converted form of this definition in the majopcs
	 * entry for this major opcode.
	 */
	for (i = &instrs[0]; *i->mnem; i++) {
		m = &majopcs[i->majopc];
		if (m->maxsubop < i->opcext) {
			db_printf("iExInit not enough space for opcode %d",
			    i->majopc);
			return 0;
		}
		shft = 32 - i->extbs - i->extbl;
		mask = (1 << i->extbl) - 1;
		if (m->extshft || m->extmask) {
			if (m->extshft != shft || m->extmask != mask) {
				db_printf("%s - Bad instruction initialization!\n", i->mnem);
				return 0;
			}
		} else {
			m->extshft = shft;
			m->extmask = mask;
		}
	}

	/*
	 * Lastly, fill in all legal subops with the appropriate info.
	 */
	for (i = &instrs[0]; *i->mnem; i++) {
		m = &majopcs[i->majopc];
		if (m->maxsubop == 1)
			m->subops = (const struct inst **)i;
		else
			m->subops[i->opcext] = i;
	}

	unasm_initted++;
	return 1;
}



/*##################### Functions and Subroutines ##########################*/

/**************************************/
/* Miscellaneous Disassembly Routines */
/**************************************/

/* Add instructions */
int
addDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d,%%r%d,%%r%d",addDCond(Cond4(w)),
		Rsa(w),Rsb(w),Rtc(w));
	return (1);
}

/* Unit instructions */
int
unitDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s", unitDCond(Cond4(w)));
	if (Match("dcor") || Match("idcor"))
		db_printf("\t%%r%d,%%r%d",Rsb(w),Rtc(w));
	else
		db_printf("\t%%r%d,%%r%d,%%r%d",Rsa(w),Rsb(w),Rtc(w));
	return (1);
}

/* Immediate Arithmetic instructions */
int
iaDasm(const struct inst *i, OFS ofs, int w)
{
	if (Match("addi"))
		db_printf("%s\t%d,%%r%d,%%r%d",
		    addDCond(Cond4(w)),Im11(w),Rsb(w),Rta(w));
	else
		db_printf("%s\t%d,%%r%d,%%r%d",
		    subDCond(Cond4(w)),Im11(w),Rsb(w),Rta(w));
	return (1);
}

/* Shift double instructions */
int
shdDasm(const struct inst *i, OFS ofs, int w)
{
	if (Match("vshd"))
		db_printf("%s\t%%r%d,%%r%d,%%r%d",
		    edDCond(Cond(w)), Rsa(w),Rsb(w),Rtc(w));
	else
		db_printf("%s\t%%r%d,%%r%d,%d,%%r%d",
		    edDCond(Cond(w)),Rsa(w),Rsb(w),31-Imd5(w),Rtc(w));
	return (1);
}

/* Extract instructions */
int
extrDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d,%d,%d,%%r%d",
	    edDCond(Cond(w)),Rsb(w),Imd5(w),32 - Rsc(w),Rta(w));
	return (1);
}


/* Variable extract instructions */
int
vextrDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d,%d,%%r%d",
	    edDCond(Cond(w)),Rsb(w),32 - Rsc(w),Rta(w));
	return (1);
}


/* Deposit instructions */
int
depDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d,%d,%d,%%r%d",
	    edDCond(Cond(w)),Rsa(w),31 - Imd5(w),32 - Rsc(w),Rtb(w));
	return (1);
}


/* Variable deposit instructions */
int
vdepDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d,%d,%%r%d",
	    edDCond(Cond(w)),Rsa(w),32 - Rsc(w),Rtb(w));
	return (1);
}


/* Deposit Immediate instructions */
int
depiDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%d,%d,%d,%%r%d",
	    edDCond(Cond(w)),Ima5(w),31 - Imd5(w),32 - Imc5A(w),Rtb(w));
	return (1);
}

/* Variable Deposit Immediate instructions */
int
vdepiDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%d,%d,%%r%d",edDCond(Cond(w)),Ima5(w),32-Imc5A(w),Rtb(w));
	return (1);
}

/*---------------------------------------------------------------------------
 * conditionType$DisassembleCondition - Return a string which contains the
 *  ascii description of the passed numeric condition.
 *---------------------------------------------------------------------------*/

char *
subDCond(u_int cond)
{
	switch(cond) {
	case EQZ:	return(",=");
	case LT:	return(",<");
	case LE:	return(",<=");
	case LLT:	return(",<<");
	case LLE:	return(",<<=");
	case SV:	return(",sv");
	case OD:	return(",od");
	case NEQZ:	return(",<>");
	case GE:	return(",>=");
	case GT:	return(",>");
	case LGE:	return(",>>=");
	case LGT:	return(",>>");
	case NSV:	return(",nsv");
	case EV:	return(",ev");
	case TR:	return(",tr");
	case NEV:	return("");
	default:
		return(",<unknown subDCond condition>");
	}
}


/*---------------------------------------------------------------------------
 * conditionType$DisassembleCondition - Return a string which contains the
 *  ascii description of the passed numeric condition.
 *---------------------------------------------------------------------------*/

char *
addDCond(u_int cond)
{
	switch(cond) {
	case EQZ:	return(",=");
	case LT:	return(",<");
	case LE:	return(",<=");
	case NUV:	return(",nuv");
	case ZNV:	return(",znv");
	case SV:	return(",sv");
	case OD:	return(",od");
	case NEQZ:	return(",<>");
	case GE:	return(",>=");
	case GT:	return(",>");
	case UV:	return(",uv");
	case VNZ:	return(",vnz");
	case NSV:	return(",nsv");
	case EV:	return(",ev");
	case TR:	return(",tr");
	case NEV:	return("");
	default:
		return(",<unknown addDCond condition>");
	}
}

char *
unitDCond(u_int cond)
{
	switch(cond) {
	case SHC:	return(",shc");
	case SHZ:	return(",shz");
	case SBC:	return(",sbc");
	case SBZ:	return(",sbz");
	case SDC:	return(",sdc");
	case NHC:	return(",nhc");
	case NHZ:	return(",nhz");
	case NBC:	return(",nbc");
	case NBZ:	return(",nbz");
	case NDC:	return(",ndc");
	case TR:	return(",tr");
	case NEV:	return("");
	default:
		return(",<unknown unitDCond condition>");
	}
}

char *
edDCond(u_int cond)
{
	switch(cond) {
	case XOD:	return(",od");
	case XTR:	return(",tr");
	case XNE:	return(",<>");
	case XLT:	return(",<");
	case XEQ:	return(",=");
	case XGE:	return(",>=");
	case XEV:	return(",ev");
	case NEV:	return("");
	default:
		return(",<unknown edDCond condition>");
	}
}



/****************************************/
/* Format Specific Disassembly Routines */
/****************************************/


/* Load [modify] instructions */
int
ldDasm(const struct inst *i, OFS ofs, int w)
{
	register int d = Disp(w);
	char s[2];

	s[1] = '\0';
	if (d < 0) {
		d = -d;
		s[0] = '-';
	} else
		s[0] = '\0';

	if (Rsb(w) == 0 && Match("ldo")) {
		db_printf("ldi\t%s%X,%%r%d",s,d,Rta(w));
		return (1);
	}
	db_printf("%s\t%s%s%X",i->mnem,(d < 2048? "R'":""), s, d);
	if (Dss(w))
		db_printf("(%%sr%d,%%r%d),%%r%d",Dss(w),Rsb(w),Rta(w));
	else
		db_printf("(%%r%d),%%r%d",Rsb(w),Rta(w));
	return (1);
}

/* Store [modify] instructions */
int
stDasm(const struct inst *i, OFS ofs, int w)
{
	register int d = Disp(w);
	char s[2];

	db_printf("\t%%r%d,",Rta(w));

	s[1] = '\0';
	if (d < 0) {
		d = -d;
		s[0] = '-';
	} else
		s[0] = '\0';

	db_printf("%s%s%X", (d < 2048? "R'":""), s, d);

	if (Dss(w))
		db_printf("(%%sr%d,%%r%d)",Dss(w),Rsb(w));
	else
		db_printf("(%%r%d)",Rsb(w));
	return (1);
}

/* Load indexed instructions */
int
ldxDasm(const struct inst *i, OFS ofs, int w)
{
	register const char *p;

	if (ShortDisp(w)) {
		db_printf("s");
		if (Modify(w))
			db_printf(",m%s", ModBefore(w)? "b": "a");
	} else {
		db_printf("x");
		if (Modify(w))
			db_printf(",%sm", IndxShft(w)? "s":"");
	}
	switch (CacheCtrl(w)) {
	case NOACTION:	p = "";   break;
	case STACKREF:	p = ",c"; break;
	case SEQPASS:	p = ",q"; break;
	case PREFETCH:	p = ",p"; break;
	}
	if (ShortDisp(w))
		db_printf("%s\t%d", p, Ima5(w));
	else
		db_printf("%s\t%%r%d", p, Rsa(w));

	if (Dss(w))
		db_printf("(%%sr%d,%%r%d),%%r%d",Dss(w),Rsb(w),Rtc(w));
	else
		db_printf("(%%r%d),%%r%d",Rsb(w),Rtc(w));
	return (1);
}

/* Store short displacement instructions */
int
stsDasm(const struct inst *i, OFS ofs, int w)
{
	register const char *p;
	if (Modify(w))
		db_printf(",m%s", ModBefore(w)? "b":"a");

	switch (CacheCtrl(w)) {
	case NOACTION:	p = "";   break;
	case STACKREF:	p = ",c"; break;
	case SEQPASS:	p = ",q"; break;
	case PREFETCH:	p = ",p"; break;
	}
	db_printf("%s\t%%r%d,", p, Rta(w));
	if (Dss(w))
		db_printf("%d(%%sr%d,%%r%d)",Imc5(w),Dss(w),Rsb(w));
	else
		db_printf("%d(%%r%d)",Imc5(w),Rsb(w));
	return (1);
}

/* Store Bytes Instruction */
int
stbysDasm(const struct inst *i, OFS ofs, int w)
{
	register const char *p;
	db_printf(ModBefore(w)? ",e":",b");
	if (Modify(w))
		db_printf(",m");
	switch (CacheCtrl(w)) {
	case NOACTION:	p = "";   break;
	case STACKREF:	p = ",f"; break;
	case SEQPASS:	p = ",r"; break;
	case PREFETCH:	p = ",z"; break;
	}
	db_printf("%s\t%%r%d,", p, Rta(w));
	if (Dss(w))
		db_printf("%d(%%sr%d,%%r%d)",Imc5(w),Dss(w),Rsb(w));
	else
		db_printf("%d(%%r%d)",Imc5(w),Rsb(w));
	return (1);
}

/* Long Immediate instructions */
int
limmDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("\tL'%X,%%r%d", Im21(w), Rtb(w));
	return (1);
}


/* Branch and Link instruction(s) (Branch, too!!) */
int
blDasm(const struct inst *i, OFS ofs, int w)
{
	register OFS tgtofs = ofs + 8 + Bdisp(w);
	register u_int link = Rtb(w);

	if (link && !Match("gate"))
		db_printf("l");
	if (Nu(w))
		db_printf(",n");
	db_printf("\t");

	db_printsym((vaddr_t)tgtofs, DB_STGY_ANY, db_printf);

	if (link || Match("gate"))
		db_printf(",%%r%d",link);

	return (1);
}

/* Branch Register instruction */
int
brDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d,%%r%d", Nu(w)?",n":"", Rsa(w), Rtb(w));
	return (1);
}

/* Dispatch instructions */
int
bvDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("%s\t%%r%d(%%r%d)", Nu(w)?",n":"", Rsa(w), Rsb(w));
	return (1);
}

/* Branch External instructions */
int
beDasm(const struct inst *i, OFS ofs, int w)
{
	register int d = Bdisp(w);
	register const char *p;
	char s[2];

	s[1] = '\0';
	if (d < 0) {
		d = -d;
		s[0] = '-';
	} else
		s[0] = '\0';

	p =  Nu(w)? ",n":"";
	db_printf("%s\tR'%s%X(%%sr%d,%%r%d)", p, s, d, Sr(w), Rsb(w));
	return (1);
}


/* Compare/Add and Branch instructions */
int
cbDasm(const struct inst *i, OFS ofs, int w)
{
	register OFS tgtofs = ofs + 8 + Cbdisp(w);

	if (Match("movb"))
		db_printf("%s", edDCond(Cond(w)));
	else if (Match("addb"))
		db_printf("%s", addDCond(Cond(w) << 1));
	else
		db_printf("%s", subDCond(Cond(w) << 1));
	db_printf("%s\t%%r%d,%%r%d,", Nu(w)?",n":"", Rsa(w), Rsb(w));
	db_printsym((vaddr_t)tgtofs, DB_STGY_ANY, db_printf);
	return (1);
}

/* Compare/Add and Branch Immediate instructions */
int
cbiDasm(const struct inst *i, OFS ofs, int w)
{
	register OFS tgtofs = ofs + 8 + Cbdisp(w);

	if (Match("movib"))
		db_printf("%s", edDCond(Cond(w)));
	else if (Match("addib"))
		db_printf("%s", addDCond(Cond(w) << 1));
	else
		db_printf("%s", subDCond(Cond(w) << 1));
	db_printf("%s\t%d,%%r%d,", Nu(w)? ",n":"", Ima5(w), Rsb(w));
	db_printsym((vaddr_t)tgtofs, DB_STGY_ANY, db_printf);
	return (1);
}

/* Branch on Bit instructions */
int
bbDasm(const struct inst *i, OFS ofs, int w)
{
	register OFS tgtofs = ofs + 8 + Cbdisp(w);
	register const char *p;

	db_printf("%s", edDCond(Cond(w)));
	p = Nu(w)? ",n":"";
	if (Match("bvb"))
		db_printf("%s\t%%r%d,", p, Rta(w));
	else
		db_printf("%s\t%%r%d,%d,", p, Rsa(w), Imb5(w));
	db_printsym((vaddr_t)tgtofs, DB_STGY_ANY, db_printf);
	return (1);
}

/* Arithmetic instructions */
int
ariDasm(const struct inst *i, OFS ofs, int w)
{
	if (Match("or") && Rsb(w) == 0 && Cond4(w) == NEV) {
		if (Rsa(w) == 0 && Rtc(w) == 0)
			db_printf("nop");
		else
			db_printf("copy\t%%r%d,%%r%d",Rsa(w),Rtc(w));
	} else
		db_printf("%s%s\t%%r%d,%%r%d,%%r%d", i->mnem,
			  subDCond(Cond4(w)), Rsa(w),Rsb(w),Rtc(w));
	return(1);
}

/* System control operations */
int
scDasm(const struct inst *i, OFS ofs, int w)
{
	if (Match("mtctl")) {
		if (Rtb(w) == 11)
			db_printf("mtsar\t%%r%d",Rsa(w));
		else
			db_printf("mtctl\t%%r%d,%%cr%d",Rsa(w),Rtb(w));
		return (1);
	}
	db_printf("%s", i->mnem);
	if (Match("ssm") || Match("rsm"))
		db_printf("\t%d,%%r%d",Ima5A(w),Rtc(w));
	else if (Match("mtsm")) db_printf("\t%%r%d",Rsa(w));
	else if (Match("ldprid")) db_printf("\t%%r%d",Rtc(w));
	else if (Match("mtsp")) db_printf("\t%%r%d,%%sr%d",Rsa(w),Sr(w));
	else if (Match("mfsp")) db_printf("\t%%sr%d,%%r%d",Sr(w),Rtc(w));
	else if (Match("mfctl")) db_printf("\t%%cr%d,%%r%d",Rsb(w),Rtc(w));
	else if (Match("ldsid")) {
		if (Dss(w))
			db_printf("\t(%%sr%d,%%r%d),%%r%d",Dss(w),Rsb(w),Rtc(w));
		else
			db_printf("\t(%%r%d),%%r%d",Rsb(w),Rtc(w));
	} else
		return (0);

	return (1);
}

/* Instruction cache/tlb control instructions */
int
mmgtDasm(const struct inst *i, OFS ofs, int w)
{
	if (Match("probe")) {
		if (ProbeI(w)) {
			if (Dss(w))
				db_printf("i\t(%%sr%d,%%r%d),%d,%%r%d",
				    Dss(w),Rsb(w),Rsa(w),Rtc(w));
			else
				db_printf("i\t(%%r%d),%d,%%r%d",
				    Rsb(w),Rsa(w),Rtc(w));
		} else {
			if (Dss(w))
				db_printf("\t(%%sr%d,%%r%d),%%r%d,%%r%d",
				    Dss(w),Rsb(w),Rsa(w),Rtc(w));
			else
				db_printf("\t(%%r%d),%%r%d,%%r%d",
				    Rsb(w),Rsa(w),Rtc(w));
		}
	}
	else if (Match("lha") || Match("lpa")) {
		if (Modify(w))
			db_printf(",m");
		if (Dss(w))
			db_printf("\t%%r%d(%%sr%d,%%r%d),%%r%d",
			    Rsa(w),Dss(w),Rsb(w),Rtc(w));
		else
			db_printf("\t%%r%d(%%r%d),%%r%d",Rsa(w),Rsb(w),Rtc(w));
	}
	else if (Match("pdtlb") || Match("pdc") || Match("fdc")) {
		if (Modify(w)) db_printf(",m");
		if (Dss(w))
			db_printf("\t%%r%d(%%sr%d,%%r%d)",Rsa(w),Dss(w),Rsb(w));
		else
			db_printf("\t%%r%d(%%r%d)",Rsa(w),Rsb(w));
	}
	else if (Match("pitlb") || Match("fic")) {
		if (Modify(w))
			db_printf(",m");
		db_printf("\t%%r%d(%%sr%d,%%r%d)",Rsa(w),Sr(w),Rsb(w));
	}
	else if (Match("idtlb")) {
		if (Dss(w))
			db_printf("\t%%r%d,(%%sr%d,%%r%d)",Rsa(w),Dss(w),Rsb(w));
		else
			db_printf("\t%%r%d,(%%r%d)",Rsa(w),Rsb(w));
	}
	else if (Match("iitlb"))
		db_printf("\t%%r%d,(%%sr%d,%%r%d)",Rsa(w),Sr(w),Rsb(w));
	else
		return (0);

	return(1);
}

/* break instruction */
int
brkDasm(const struct inst *i, OFS ofs, int w)
{
	db_printf("\t%d,%d",Bi1(w),Bi2(w));
	return (1);
}

int
floatDasm(const struct inst *i, OFS ofs, int w)
{
	register u_int op1, r1, fmt, t;
	u_int op2, r2, dfmt;
	char *p;

	op1 = CoprExt1(w);
	op2 = CoprExt2(w);
	fmt = (op1 >> 2) & 3;		/* get precision of source  */

#define ST(r) ((fmt & 1)? fdreg[(r)]:fsreg[(r)])
	/*
	 * get first (or only) source register
	 * (independent of class)
	 */
	r1 = (op1 >> 11) & 0x3e;
	if ((fmt & 1) == 0 && (Uid(w) & 2))
		r1++;

	if (op1 & 2) {				/* class 2 or 3 */
		/*
		 * get second source register
		 */
		r2 = (op1 >> 6) & 0x3e;
		if (fmt == 2)
			r2++;

		if ((op1 & 1) == 0) {		/* class 2 */
			/* Opclass 2: 2 sources, no destination */
			switch((op1 >> 4) & 7) {
			case 0:
				p = "cmp";
				break;
			default:
				return(0);
			}
			db_printf("%s,%s",p,fmtStrTbl[fmt]);
			db_printf(",%s\t%%f%s,%%f%s",
			    condStrTbl[op2], ST(r1), ST(r2));
			return (1);
		}
		/*
		 * get target register (class 3)
		 */
		t = (op2 << 1);
		if ((fmt & 1) == 0 && (Uid(w) & 1))
			t++;
		/* Opclass 3: 2 sources, 1 destination */
		switch((op1 >> 4) & 7) {
		case 0: p = "add"; break;
		case 1: p = "sub"; break;
		case 2: p = (Fpi(w)) ? "mpyi" : "mpy"; break;
		case 3: p = "div"; break;
		case 4: p = "rem"; break;
		default: return (0);
		}
		db_printf("%s,%s", p, fmtStrTbl[fmt]);
		db_printf("\t%%f%s,%%f%s,%%f%s",ST(r1),ST(r2),ST(t));
	} else if (op1 & 1) {			/* class 1 */
		dfmt = (op1 >> 4) & 3;
#define DT(r) ((dfmt & 1)? fdreg[(r)]:fsreg[(r)])

		/*
		 * get target register
		 */
		t = (op2 << 1);
		if ((dfmt & 1) == 0 && (Uid(w) & 1))
			t++;
		/* Opclass 1: 1 source, 1 destination conversions */
		switch((op1 >> 6) & 3) {
		case 0: p = "ff"; break;
		case 1: p = "xf"; break;
		case 2: p = "fx"; break;
		case 3: p = "fxt"; break;
		}
		db_printf("%s,%s", p, fmtStrTbl[fmt]);
		db_printf(",%s\t%%f%s,%%f%s",fmtStrTbl[dfmt],ST(r1),DT(t));
	} else {				/* class 0 */
		/*
		 * get target register
		 */
		t = (op2 << 1);
		if ((fmt & 1) == 0 && (Uid(w) & 1))
			t++;
		/* Opclass 0: 1 source, 1 destination */
		switch((op1 >> 4) & 7) {
		case 1: p = "rsqrt"; break;
		case 2: p = "cpy"; break;
		case 3: p = "abs"; break;
		case 4: p = "sqrt"; break;
		case 5: p = "rnd"; break;
		default: return (0);
		}
		db_printf("%s,%s",p,fmtStrTbl[fmt]);
		db_printf("\t%%f%s,%%f%s",ST(r1),ST(t));
	}
	return (1);
}

int
fcoprDasm(int w, u_int op1, u_int op2)
{
	register u_int r1, r2, t, fmt, dfmt;
	register char *p;

	if (AstNu(w) && op1 == ((1<<4) | 2)) {
		if (op2 == 0 || op2 == 1 || op2 == 2) {
			db_printf("ftest");
			if (op2 == 1)
				db_printf(",acc");
			else if (op2 == 2)
				db_printf(",rej");
			return (1);
		}
		return (0);
	} else if (0 == op1 && 0 == op2) {
		db_printf("fcopr identify");
		return (1);
	}
	switch(op1 & 3) {
	    case 0:
		/* Opclass 0: 1 source, 1 destination */
		r1 = (op1 >> 12) & 0x1f; t = op2; fmt = (op1 >> 2) & 3;
		switch((op1 >> 4) & 7) {
		case 1: p = "rsqrt"; break;
		case 2: p = "cpy"; break;
		case 3: p = "abs"; break;
		case 4: p = "sqrt"; break;
		case 5: p = "rnd"; break;
		default: return(0);
		}
		db_printf("f%s,%s\t%%fr%d,%%fr%d", p, fmtStrTbl[fmt], r1, t);
		break;
	    case 1:
		/* Opclass 1: 1 source, 1 destination conversions */
		r1 = (op1 >> 12) & 0x1f; t = op2;
		fmt = (op1 >> 2) & 3; dfmt = (op1 >> 4) & 3;
		switch((op1 >> 6) & 3) {
		case 0: p = "ff"; break;
		case 1: p = "xf"; break;
		case 2: p = "fx"; break;
		case 3: p = "fxt"; break;
		}
		db_printf("fcnv%s,%s,%s\t%%fr%d,%%fr%d",
		    p, fmtStrTbl[fmt], fmtStrTbl[dfmt], r1, t);
		break;
	    case 2:
		/* Opclass 2: 2 sources, no destination */
		r1 = (op1 >> 12) & 0x1f; r2 = (op1 >> 7) & 0x1f;
		fmt = (op1 >> 2) & 3;
		switch((op1 >> 4) & 7) {
		case 0: p = "fcmp"; break;
		default: return (0);
		}
		db_printf("%s,%s,%s\t%%fr%d,%%fr%d",
		    p,fmtStrTbl[fmt],condStrTbl[op2],r1,r2);
		break;
	    case 3:
		/* Opclass 3: 2 sources, 1 destination */
		r1 = (op1 >> 12) & 0x1f; r2 = (op1 >> 7) & 0x1f; t = op2;
		fmt = (op1 >> 2) & 3;
		switch((op1 >> 4) & 7) {
		case 0: p = "add"; break;
		case 1: p = "sub"; break;
		case 2: p = "mpy"; break;
		case 3: p = "div"; break;
		case 4: p = "rem"; break;
		default: return (0);
		}
		db_printf("f%s,%s\t%%fr%d,%%fr%d,%%fr%d",
		    p, fmtStrTbl[fmt], r1, r2, t);
		break;
	    default:
		    return(0);
	}
	return (1);
}

int
coprDasm(const struct inst *i, OFS ofs, int w)
{
	register u_int uid = Uid(w);
	register int load = 0;
	register char *pfx = uid > 1 ? "c" : "f";
	register int dreg = 0;

	if (Match("copr")) {
		if (uid) {
			db_printf("copr,%d,0x%x",uid,CoprExt(w));
			if (AstNu(w))
				db_printf(",n");
			return (1);
		}
		return fcoprDasm(w, CoprExt1(w),CoprExt2(w));
	}
	if (Match("cldd")) {
		dreg = 1;
		load = 1;
		db_printf("%sldd",pfx);
	} else if (Match("cldw")) {
		load = 1;
		db_printf("%sldw",pfx);
	} else if (Match("cstd")) {
		dreg = 1;
		db_printf("%sstd",pfx);
	} else if (Match("cstw"))
		db_printf("%sstw",pfx);
	else
		return (0);

	if (ShortDisp(w)) {
		db_printf("s");
		if (AstNu(w))
			db_printf(",m%s", ModBefore(w)?"b":"a");
	}
	else {
		db_printf("x");
		if (AstNu(w))
			db_printf(",%sm", IndxShft(w)?"s":"");
		else if (IndxShft(w))
			db_printf(",s");
	}
	switch (CacheCtrl(w)) {
	case NOACTION:	break;
	case STACKREF:	db_printf(",c"); break;
	case SEQPASS:	db_printf(",q"); break;
	case PREFETCH:	db_printf(",p"); break;
	}
	if (load) {
		register const char *p;

		if (dreg)
			p = fdreg[(Rtc(w)<<1)+(uid&1)];
		else
			p = fsreg[(Rtc(w)<<1)+(uid&1)];

		if (ShortDisp(w))
			db_printf("\t%d",Ima5(w));
		else
			db_printf("\t%%r%d",Rsa(w));
		if (Dss(w))
			db_printf("(%%sr%d,%%r%d),%%f%s", Dss(w),Rsb(w), p);
		else
			db_printf("(%%r%d),%%f%s",Rsb(w), p);
	} else {
		register const char *p;

		if (dreg)
			p = fdreg[(Rsc(w)<<1)+(uid&1)];
		else
			p = fsreg[(Rsc(w)<<1)+(uid&1)];

		if (ShortDisp(w))
			db_printf("\t%%f%s,%d", p, Ima5(w));
		else
			db_printf("\t%%f%s,%%r%d", p, Rta(w));
		if (Dss(w))
			db_printf("(%%sr%d,%%r%d)",Dss(w),Rsb(w));
		else
			db_printf("(%%r%d)",Rsb(w));
	}
	return (1);
}

int
lpkDasm(const struct inst *i, OFS ofs, int w)
{
	/*
	 * Floating point STore Quad
	 * Short or Indexed
	 */
	if (ShortDisp(w)) {
		if (Modify(w))
			db_printf(",m%s", ModBefore(w)?"b":"a");
	} else {
		if (Modify(w))
			db_printf(",%sm", IndxShft(w)? "s":"");
		else if (IndxShft(w))
			db_printf(",s");
	}
	switch (CacheCtrl(w)) {
	case NOACTION:	break;
	case STACKREF:	db_printf(",c"); break;
	case SEQPASS:	db_printf(",q"); break;
	case PREFETCH:	db_printf(",p"); break;
	}
	if (ShortDisp(w))
		db_printf("\t%%fr%d,%d",Rsc(w),Ima5(w));
	else
		db_printf("\t%%fr%d,%%r%d",Rsc(w),Rta(w));
	if (Dss(w))
		db_printf("(%%sr%d,%%r%d)",Dss(w),Rsb(w));
	else
		db_printf("(%%r%d)",Rsb(w));
	return (1);
}

int
diagDasm(const struct inst *i, OFS ofs, int w)
{
	if (0x0b0 == BitfR(w,19,8,_b198))	/* mtcpu */
		db_printf("mtcpu\t%%r%d,%%dr%d", Rsa(w), Rtb(w));
	else if (0x0d0 == BitfR(w,19,8,_b198))	/* mfcpu */
		db_printf("mfcpu\t%%dr%d,%%r%d", Rsb(w), Rta(w));
	else {
		db_printf("%s", i->mnem);
		if (Match("diag"))
			db_printf("\t0x%X",w & 0x03ffffff);
		else
			return (0);
	}
	return (1);
}

int
fmpysubDasm(const struct inst *i, OFS ofs, int w)
{
	if (SinglePrec(w))
		db_printf("SUB,SGL\t%%f%s,%%f%s,%%f%s,%%f%s,%%f%s",
		    fsreg[Ms1(w)], fsreg[Ms2(w)], fsreg[Mt(w)],
		    fsreg[As(w)], fsreg[Ad(w)]);
	else
		db_printf("SUB,DBL\t%%f%s,%%f%s,%%f%s,%%f%s,%%f%s",
		    fdreg[Ms1(w)], fdreg[Ms2(w)], fdreg[Mt(w)],
		    fdreg[As(w)], fdreg[Ad(w)]);
	return (1);
}

int
fmpyaddDasm(const struct inst *i, OFS ofs, int w)
{
	register const char
		*ms1 = SinglePrec(w) ? fsreg[Ms1(w)] : fdreg[Ms1(w)],
		*ms2 = SinglePrec(w) ? fsreg[Ms2(w)] : fdreg[Ms2(w)],
		*mt  = SinglePrec(w) ? fsreg[Mt(w)]  : fdreg[Mt(w)],
		*as  = SinglePrec(w) ? fsreg[As(w)]  : fdreg[As(w)],
		*ad  = SinglePrec(w) ? fsreg[Ad(w)]  : fdreg[Ad(w)];

	if (Rsd(w) == 0)
		db_printf("\t%%fcfxt,%s,%%f%s,%%f%s,%%f%s",
		    ((SinglePrec(w)) ? "sgl" : "dbl"), ms1, ms2, mt);
	else
		db_printf("add%s\t%%f%s,%%f%s,%%f%s,%%f%s,%%f%s",
		    ((SinglePrec(w)) ? "sgl" : "dbl"), ms1, ms2, mt, as, ad);

	return (1);
}

vaddr_t
db_disasm(vaddr_t loc, int flag)
{
	register const struct inst *i;
	register const struct majoropcode *m;
	register u_int ext;
	int ok;
	uint32_t instruct;
	OFS ofs = loc;

	if (loc == PC_REGS(&ddb_regs) && ddb_regs.tf_iir)
		instruct = ddb_regs.tf_iir;
	else if (USERMODE(loc)) {
		if (copyinsn(NULL, (uint32_t *)(loc &~ HPPA_PC_PRIV_MASK),
		    &instruct))
			instruct = 0;
	} else
		instruct = *(int *)loc;

	ok = 0;
	if (iExInit() != 0) {
		m = &majopcs[Opcode(instruct)];
		ext = OpExt(instruct, m);
		if (ext <= m->maxsubop) {
			/* special hack for majopcs table layout */
			if (m->maxsubop == 1)
				i = (const struct inst *)m->subops;
			else
				i = m->subops[ext];

			if (i && i->mnem[0] != '?') {
				if (i->dasmfcn != coprDasm &&
				    i->dasmfcn != diagDasm &&
				    i->dasmfcn != ariDasm &&
				    i->dasmfcn != scDasm &&
				    i->dasmfcn != ldDasm)
					db_printf("%s", i->mnem);
				if (i->dasmfcn)
					ok = (*i->dasmfcn)(i, ofs, instruct);
			}
		}
	}

	if (!ok)
		db_printf("<%08x>", instruct);

	db_printf("\n");
	return (loc + sizeof(instruct));
}
