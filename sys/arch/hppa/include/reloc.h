/*	$OpenBSD: reloc.h,v 1.4 2011/11/10 22:48:13 deraadt Exp $	*/

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

#ifndef _MACHINE_RELOC_H_
#define _MACHINE_RELOC_H_

/* pa1 compatibility */
#define	RELOC_DLTREL21L	RELOC_GPREL21L
#define	RELOC_DLTREL14R	RELOC_GPREL14R
#define	RELOC_DLTIND21L	RELOC_LTOFF21L
#define	RELOC_DLTIND14R	RELOC_LTOFF14R
#define	RELOC_DLTIND14F	RELOC_LTOFF14F
#define	RELOC_DLTREL14WR	RELOC_GPREL14WR
#define	RELOC_DLTREL14DR	RELOC_GPREL14DR
#define	RELOC_DLTIND14WR	RELOC_LTOFF14WR
#define	RELOC_DLTIND14DR	RELOC_LTOFF14DR


enum reloc_type {
	RELOC_NONE = 0,
	RELOC_DIR32,		/*	symbol + addend*/
	RELOC_DIR21L,		/*	LR(symbol, addend)  */
	RELOC_DIR17R,		/*	RR(symbol, addend) */
	RELOC_DIR17F,		/*	symbol + addend */
	RELOC_DIR14R = 6,	/*	RR(symbol, addend) */
	RELOC_PCREL32 = 9,	/*	pa2: symbol - PC - 8 + addend */
	RELOC_PCREL21L,		/*	L(symbol - PC - 8 + addend */
	RELOC_PCREL17R,		/*	R(symbol - PC - 8 + addend */
	RELOC_PCREL17F,		/*	symbol - PC - 8 + addend */
	RELOC_PCREL17C,		/* pa1: symbol - PC - 8 + addend */
	RELOC_PCREL14R,		/*	R(symbol - PC - 8 + addend */
	RELOC_DPREL21L = 18,	/* pa1: LR(symbol - GP, addend */
	RELOC_DPREL14WR,	/* pa1: RR(symbol - GP, addend */
	RELOC_DPREL14DR,	/* pa1: RR(symbol - GP, addend */
	RELOC_DPREL14R,		/* pa1: RR(symbol - GP, addend */
	RELOC_GPREL21L = 26,	/*	LR(symbol - GP, addend */
	RELOC_GPREL14R = 30,	/*	RR(symbol - GP, addend */
	RELOC_LTOFF21L = 34,	/*	L(ltoff(symbol + addend)) */
	RELOC_LTOFF14R = 38,	/*	R(ltoff(symbol + addend)) */
	RELOC_LTOFF14F,		/* pa1: ltoff(symbol + addend) */
	RELOC_SETBASE,		/*	no relocation; base = symbol */
	RELOC_SECREL32,		/*	symbol - SECT + addend */
	RELOC_BASEREL21L,	/* pa1: LR(symbol - base, addend) */
	RELOC_BASEREL17R,	/* pa1: RR(symbol - base, addend) */
	RELOC_BASEREL14R = 46,	/* pa1: RR(symbol - base, addend) */
	RELOC_SEGBASE = 48,	/*	no relocation; SB = symbol */
	RELOC_SEGREL32,		/*	symbol - SB + addend */
	RELOC_PLTOFF21L,	/*	LR(pltoff(symbol), addend */
	RELOC_PLTOFF14R = 54,	/*	RR(pltoff(symbol), addend */
	RELOC_PLTOFF14F,	/* pa1: pltoff(symbol) + addend */
	RELOC_LTOFF_FPTR32 = 57,/* pa2: ltoff(fptr(symbol + addend)) */
	RELOC_LTOFF_FPTR21L,	/* pa2: L(ltoff(fptr(symbol + addend))) */
	RELOC_LTOFF_FPTR14R= 62,/* pa2: R(ltoff(fptr(symbol + addend))) */
	RELOC_FPTR64 = 64,	/* pa2: fptr(symbol + addend) */
	RELOC_PLABEL32,		/* pa1: fptr(symbol) */
	RELOC_PCREL64 = 72,	/* pa2: symbol - PC - 8 + addend */
	RELOC_PCREL22C,		/* pa1: symbol - PC - 8 + addend */
	RELOC_PCREL22F,		/*	symbol - PC - 8 + addend */
	RELOC_PCREL14WR,	/*	R(symbol - PC - 8 + addend) */
	RELOC_PCREL14DR,	/*	R(symbol - PC - 8 + addend) */
	RELOC_PCREL16F,		/* pa2: symbol - PC - 8 + addend */
	RELOC_PCREL16WF,	/* pa2: symbol - PC - 8 + addend */
	RELOC_PCREL16DF,	/* pa2: symbol - PC - 8 + addend */
	RELOC_DIR64,		/* pa2: symbol + addend */
	RELOC_DIR14WR = 83,	/*	RR(symbol, addend) */
	RELOC_DIR14DR,		/*	RR(symbol, addend) */
	RELOC_DIR16F,		/* pa2: symbol + addend */
	RELOC_DIR16WF,		/* pa2: symbol + addend */
	RELOC_DIR16DF,		/* pa2: symbol + addend */
	RELOC_GPREL64,		/* pa2: symbol - GP + addend */
	RELOC_GPREL14WR = 91,	/*	RR(symbol - GP, addend) */
	RELOC_GPREL14DR,	/*	RR(symbol - GP, addend) */
	RELOC_GPREL16F,		/* pa2: symbol - GP + addend */
	RELOC_GPREL16WF,	/* pa2: symbol - GP + addend */
	RELOC_GPREL16DF,	/* pa2: symbol - GP + addend */
	RELOC_LTOFF64 = 96,	/* pa2: ltoff(symbol + addend) */
	RELOC_LTOFF14WR = 99,	/*	R(ltoff(symbol + addend)) */
	RELOC_LTOFF14DR,	/*	R(ltoff(symbol + addend)) */
	RELOC_LTOFF16F,		/* pa2: ltoff(symbol + addend) */
	RELOC_LTOFF16WF,	/* pa2: ltoff(symbol + addend) */
	RELOC_LTOFF16DF,	/* pa2: ltoff(symbol + addend) */
	RELOC_SECREL64,		/* pa2: symbol - SECT + addend */
	RELOC_BASEREL14WR=107,	/* pa1: RR(symbol - base, addend */
	RELOC_BASEREL14DR,	/* pa1: RR(symbol - base, addend */
	RELOC_SEGREL64 = 112,	/* pa2: symbol - SB + addend */
	RELOC_PLTOFF14WR =115,	/*	RR(pltoff(symbol) + addend) */
	RELOC_PLTOFF14DR,	/*	RR(pltoff(symbol) + addend) */
	RELOC_PLTOFF16F,	/* pa2: pltoff(symbol) + addend */
	RELOC_PLTOFF16WF,	/* pa2: pltoff(symbol) + addend */
	RELOC_PLTOFF16DF,	/* pa2: pltoff(symbol) + addend */
	RELOC_LTOFF_FPTR64,	/* pa2: ltoff(fptr(symbol + addend)) */
	RELOC_LTOFF_FPTR14WR=123,/* pa2: R(ltoff(fptr(symbol + addend) */
	RELOC_LTOFF_FPTR14DR,	/* pa2: R(ltoff(fptr(symbol + addend) */
	RELOC_LTOFF_FPTR16F,	/* pa2: ltoff(fptr(symbol + addend)) */
	RELOC_LTOFF_FPTR16WF,	/* pa2: ltoff(fptr(symbol + addend)) */
	RELOC_LTOFF_FPTR16DF,	/* pa2: ltoff(fptr(symbol + addend)) */
	RELOC_LORESERVE,	/*	reserved for environment-specific use */
	RELOC_COPY = 128,
	RELOC_IPLT,
	RELOC_EPLT,
	RELOC_GDATA,
	RELOC_JMPSLOT,
	RELOC_RELATIVE,
	RELOC_HIRESERVE = 255
};

#endif /* _MACHINE_RELOC_H_ */
