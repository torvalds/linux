/*	$OpenBSD: reloc.h,v 1.4 2017/08/13 14:56:09 visa Exp $	*/

/*
 * Copyright (c) 1996-2004 Per Fogelstrom, Opsycon AB
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MIPS64_RELOC_H_
#define _MIPS64_RELOC_H_

/*
 * Mips relocations.
 */

#define	R_MIPS_NONE	0	/* No reloc */
#define	R_MIPS_16	1	/* Direct 16 bit */
#define	R_MIPS_32	2	/* Direct 32 bit */
#define	R_MIPS_REL32	3	/* PC relative 32 bit */
#define	R_MIPS_26	4	/* Direct 26 bit shifted */
#define	R_MIPS_HI16	5	/* High 16 bit */
#define	R_MIPS_LO16	6	/* Low 16 bit */
#define	R_MIPS_GPREL16	7	/* GP relative 16 bit */
#define	R_MIPS_LITERAL	8	/* 16 bit literal entry */
#define	R_MIPS_GOT16	9	/* 16 bit GOT entry */
#define	R_MIPS_PC16	10	/* PC relative 16 bit */
#define	R_MIPS_CALL16	11	/* 16 bit GOT entry for function */
#define	R_MIPS_GPREL32	12	/* GP relative 32 bit */

#define	R_MIPS_64	18

#define	R_MIPS_REL32_64	((R_MIPS_64 << 8) | R_MIPS_REL32)

#endif /* !_MIPS64_RELOC_H_ */
