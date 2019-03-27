/*******************************************************************
** m a t h 6 4 . h
** Forth Inspired Command Language - 64 bit math support routines
** Author: John Sadler (john_sadler@alum.mit.edu)
** Created: 25 January 1998
** $Id: math64.h,v 1.9 2001/12/05 07:21:34 jsadler Exp $
*******************************************************************/
/*
** Copyright (c) 1997-2001 John Sadler (john_sadler@alum.mit.edu)
** All rights reserved.
**
** I am interested in hearing from anyone who uses ficl. If you have
** a problem, a success story, a defect, an enhancement request, or
** if you would like to contribute to the ficl release, please 
** contact me by email at the address above.
**
** Get the latest Ficl release at http://ficl.sourceforge.net
**
** L I C E N S E  and  D I S C L A I M E R
** 
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

/* $FreeBSD$ */

#if !defined (__MATH64_H__)
#define __MATH64_H__

#ifdef __cplusplus
extern "C" {
#endif

DPINT   m64Abs(DPINT x);
int     m64IsNegative(DPINT x);
DPUNS   m64Mac(DPUNS u, FICL_UNS mul, FICL_UNS add);
DPINT   m64MulI(FICL_INT x, FICL_INT y);
DPINT   m64Negate(DPINT x);
INTQR   m64FlooredDivI(DPINT num, FICL_INT den);
void    i64Push(FICL_STACK *pStack, DPINT i64);
DPINT   i64Pop(FICL_STACK *pStack);
void    u64Push(FICL_STACK *pStack, DPUNS u64);
DPUNS   u64Pop(FICL_STACK *pStack);
INTQR   m64SymmetricDivI(DPINT num, FICL_INT den);
UNS16   m64UMod(DPUNS *pUD, UNS16 base);


#if PORTABLE_LONGMULDIV != 0   /* see sysdep.h */
DPUNS   m64Add(DPUNS x, DPUNS y);
DPUNS   m64ASL( DPUNS x );
DPUNS   m64ASR( DPUNS x );
int     m64Compare(DPUNS x, DPUNS y);
DPUNS   m64Or( DPUNS x, DPUNS y );
DPUNS   m64Sub(DPUNS x, DPUNS y);
#endif

#define i64Extend(i64) (i64).hi = ((i64).lo < 0) ? -1L : 0 
#define m64CastIU(i64) (*(DPUNS *)(&(i64)))
#define m64CastUI(u64) (*(DPINT *)(&(u64)))
#define m64CastQRIU(iqr) (*(UNSQR *)(&(iqr)))
#define m64CastQRUI(uqr) (*(INTQR *)(&(uqr)))

#define CELL_HI_BIT (1L << (BITS_PER_CELL-1))

#ifdef __cplusplus
}
#endif

#endif

