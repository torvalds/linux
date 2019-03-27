/*******************************************************************
** m a t h 6 4 . c
** Forth Inspired Command Language - 64 bit math support routines
** Author: John Sadler (john_sadler@alum.mit.edu)
** Created: 25 January 1998
** Rev 2.03: Support for 128 bit DP math. This file really ouught to
** be renamed!
** $Id: math64.c,v 1.9 2001/12/05 07:21:34 jsadler Exp $
*******************************************************************/
/*
** Copyright (c) 1997-2001 John Sadler (john_sadler@alum.mit.edu)
** All rights reserved.
**
** Get the latest Ficl release at http://ficl.sourceforge.net
**
** I am interested in hearing from anyone who uses ficl. If you have
** a problem, a success story, a defect, an enhancement request, or
** if you would like to contribute to the ficl release, please
** contact me by email at the address above.
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

#include "ficl.h"
#include "math64.h"


/**************************************************************************
                        m 6 4 A b s
** Returns the absolute value of an DPINT
**************************************************************************/
DPINT m64Abs(DPINT x)
{
    if (m64IsNegative(x))
        x = m64Negate(x);

    return x;
}


/**************************************************************************
                        m 6 4 F l o o r e d D i v I
** 
** FROM THE FORTH ANS...
** Floored division is integer division in which the remainder carries
** the sign of the divisor or is zero, and the quotient is rounded to
** its arithmetic floor. Symmetric division is integer division in which
** the remainder carries the sign of the dividend or is zero and the
** quotient is the mathematical quotient rounded towards zero or
** truncated. Examples of each are shown in tables 3.3 and 3.4. 
** 
** Table 3.3 - Floored Division Example
** Dividend        Divisor Remainder       Quotient
** --------        ------- ---------       --------
**  10                7       3                1
** -10                7       4               -2
**  10               -7      -4               -2
** -10               -7      -3                1
** 
** 
** Table 3.4 - Symmetric Division Example
** Dividend        Divisor Remainder       Quotient
** --------        ------- ---------       --------
**  10                7       3                1
** -10                7      -3               -1
**  10               -7       3               -1
** -10               -7      -3                1
**************************************************************************/
INTQR m64FlooredDivI(DPINT num, FICL_INT den)
{
    INTQR qr;
    UNSQR uqr;
    int signRem = 1;
    int signQuot = 1;

    if (m64IsNegative(num))
    {
        num = m64Negate(num);
        signQuot = -signQuot;
    }

    if (den < 0)
    {
        den      = -den;
        signRem  = -signRem;
        signQuot = -signQuot;
    }

    uqr = ficlLongDiv(m64CastIU(num), (FICL_UNS)den);
    qr = m64CastQRUI(uqr);
    if (signQuot < 0)
    {
        qr.quot = -qr.quot;
        if (qr.rem != 0)
        {
            qr.quot--;
            qr.rem = den - qr.rem;
        }
    }

    if (signRem < 0)
        qr.rem = -qr.rem;

    return qr;
}


/**************************************************************************
                        m 6 4 I s N e g a t i v e
** Returns TRUE if the specified DPINT has its sign bit set.
**************************************************************************/
int m64IsNegative(DPINT x)
{
    return (x.hi < 0);
}


/**************************************************************************
                        m 6 4 M a c
** Mixed precision multiply and accumulate primitive for number building.
** Multiplies DPUNS u by FICL_UNS mul and adds FICL_UNS add. Mul is typically
** the numeric base, and add represents a digit to be appended to the 
** growing number. 
** Returns the result of the operation
**************************************************************************/
DPUNS m64Mac(DPUNS u, FICL_UNS mul, FICL_UNS add)
{
    DPUNS resultLo = ficlLongMul(u.lo, mul);
    DPUNS resultHi = ficlLongMul(u.hi, mul);
    resultLo.hi += resultHi.lo;
    resultHi.lo = resultLo.lo + add;

    if (resultHi.lo < resultLo.lo)
        resultLo.hi++;

    resultLo.lo = resultHi.lo;

    return resultLo;
}


/**************************************************************************
                        m 6 4 M u l I
** Multiplies a pair of FICL_INTs and returns an DPINT result.
**************************************************************************/
DPINT m64MulI(FICL_INT x, FICL_INT y)
{
    DPUNS prod;
    int sign = 1;

    if (x < 0)
    {
        sign = -sign;
        x = -x;
    }

    if (y < 0)
    {
        sign = -sign;
        y = -y;
    }

    prod = ficlLongMul(x, y);
    if (sign > 0)
        return m64CastUI(prod);
    else
        return m64Negate(m64CastUI(prod));
}


/**************************************************************************
                        m 6 4 N e g a t e
** Negates an DPINT by complementing and incrementing.
**************************************************************************/
DPINT m64Negate(DPINT x)
{
    x.hi = ~x.hi;
    x.lo = ~x.lo;
    x.lo ++;
    if (x.lo == 0)
        x.hi++;

    return x;
}


/**************************************************************************
                        m 6 4 P u s h
** Push an DPINT onto the specified stack in the order required
** by ANS Forth (most significant cell on top)
** These should probably be macros...
**************************************************************************/
void  i64Push(FICL_STACK *pStack, DPINT i64)
{
    stackPushINT(pStack, i64.lo);
    stackPushINT(pStack, i64.hi);
    return;
}

void  u64Push(FICL_STACK *pStack, DPUNS u64)
{
    stackPushINT(pStack, u64.lo);
    stackPushINT(pStack, u64.hi);
    return;
}


/**************************************************************************
                        m 6 4 P o p
** Pops an DPINT off the stack in the order required by ANS Forth
** (most significant cell on top)
** These should probably be macros...
**************************************************************************/
DPINT i64Pop(FICL_STACK *pStack)
{
    DPINT ret;
    ret.hi = stackPopINT(pStack);
    ret.lo = stackPopINT(pStack);
    return ret;
}

DPUNS u64Pop(FICL_STACK *pStack)
{
    DPUNS ret;
    ret.hi = stackPopINT(pStack);
    ret.lo = stackPopINT(pStack);
    return ret;
}


/**************************************************************************
                        m 6 4 S y m m e t r i c D i v
** Divide an DPINT by a FICL_INT and return a FICL_INT quotient and a
** FICL_INT remainder. The absolute values of quotient and remainder are not
** affected by the signs of the numerator and denominator (the operation
** is symmetric on the number line)
**************************************************************************/
INTQR m64SymmetricDivI(DPINT num, FICL_INT den)
{
    INTQR qr;
    UNSQR uqr;
    int signRem = 1;
    int signQuot = 1;

    if (m64IsNegative(num))
    {
        num = m64Negate(num);
        signRem  = -signRem;
        signQuot = -signQuot;
    }

    if (den < 0)
    {
        den      = -den;
        signQuot = -signQuot;
    }

    uqr = ficlLongDiv(m64CastIU(num), (FICL_UNS)den);
    qr = m64CastQRUI(uqr);
    if (signRem < 0)
        qr.rem = -qr.rem;

    if (signQuot < 0)
        qr.quot = -qr.quot;

    return qr;
}


/**************************************************************************
                        m 6 4 U M o d
** Divides a DPUNS by base (an UNS16) and returns an UNS16 remainder.
** Writes the quotient back to the original DPUNS as a side effect.
** This operation is typically used to convert an DPUNS to a text string
** in any base. See words.c:numberSignS, for example.
** Mechanics: performs 4 ficlLongDivs, each of which produces 16 bits
** of the quotient. C does not provide a way to divide an FICL_UNS by an
** UNS16 and get an FICL_UNS quotient (ldiv is closest, but it's signed,
** unfortunately), so I've used ficlLongDiv.
**************************************************************************/
#if (BITS_PER_CELL == 32)

#define UMOD_SHIFT 16
#define UMOD_MASK 0x0000ffff

#elif (BITS_PER_CELL == 64)

#define UMOD_SHIFT 32
#define UMOD_MASK 0x00000000ffffffff

#endif

UNS16 m64UMod(DPUNS *pUD, UNS16 base)
{
    DPUNS ud;
    UNSQR qr;
    DPUNS result;

    result.hi = result.lo = 0;

    ud.hi = 0;
    ud.lo = pUD->hi >> UMOD_SHIFT;
    qr = ficlLongDiv(ud, (FICL_UNS)base);
    result.hi = qr.quot << UMOD_SHIFT;

    ud.lo = (qr.rem << UMOD_SHIFT) | (pUD->hi & UMOD_MASK);
    qr = ficlLongDiv(ud, (FICL_UNS)base);
    result.hi |= qr.quot & UMOD_MASK;

    ud.lo = (qr.rem << UMOD_SHIFT) | (pUD->lo >> UMOD_SHIFT);
    qr = ficlLongDiv(ud, (FICL_UNS)base);
    result.lo = qr.quot << UMOD_SHIFT;

    ud.lo = (qr.rem << UMOD_SHIFT) | (pUD->lo & UMOD_MASK);
    qr = ficlLongDiv(ud, (FICL_UNS)base);
    result.lo |= qr.quot & UMOD_MASK;

    *pUD = result;

    return (UNS16)(qr.rem);
}


/**************************************************************************
** Contributed by
** Michael A. Gauland   gaulandm@mdhost.cse.tek.com  
**************************************************************************/
#if PORTABLE_LONGMULDIV != 0
/**************************************************************************
                        m 6 4 A d d
** 
**************************************************************************/
DPUNS m64Add(DPUNS x, DPUNS y)
{
    DPUNS result;
    int carry;
    
    result.hi = x.hi + y.hi;
    result.lo = x.lo + y.lo;


    carry  = ((x.lo | y.lo) & CELL_HI_BIT) && !(result.lo & CELL_HI_BIT);
    carry |= ((x.lo & y.lo) & CELL_HI_BIT);

    if (carry)
    {
        result.hi++;
    }

    return result;
}


/**************************************************************************
                        m 6 4 S u b
** 
**************************************************************************/
DPUNS m64Sub(DPUNS x, DPUNS y)
{
    DPUNS result;
    
    result.hi = x.hi - y.hi;
    result.lo = x.lo - y.lo;

    if (x.lo < y.lo) 
    {
        result.hi--;
    }

    return result;
}


/**************************************************************************
                        m 6 4 A S L
** 64 bit left shift
**************************************************************************/
DPUNS m64ASL( DPUNS x )
{
    DPUNS result;
    
    result.hi = x.hi << 1;
    if (x.lo & CELL_HI_BIT) 
    {
        result.hi++;
    }

    result.lo = x.lo << 1;

    return result;
}


/**************************************************************************
                        m 6 4 A S R
** 64 bit right shift (unsigned - no sign extend)
**************************************************************************/
DPUNS m64ASR( DPUNS x )
{
    DPUNS result;
    
    result.lo = x.lo >> 1;
    if (x.hi & 1) 
    {
        result.lo |= CELL_HI_BIT;
    }

    result.hi = x.hi >> 1;
    return result;
}


/**************************************************************************
                        m 6 4 O r
** 64 bit bitwise OR
**************************************************************************/
DPUNS m64Or( DPUNS x, DPUNS y )
{
    DPUNS result;
    
    result.hi = x.hi | y.hi;
    result.lo = x.lo | y.lo;
    
    return result;
}


/**************************************************************************
                        m 6 4 C o m p a r e
** Return -1 if x < y; 0 if x==y, and 1 if x > y.
**************************************************************************/
int m64Compare(DPUNS x, DPUNS y)
{
    int result;
    
    if (x.hi > y.hi) 
    {
        result = +1;
    } 
    else if (x.hi < y.hi) 
    {
        result = -1;
    } 
    else 
    {
        /* High parts are equal */
        if (x.lo > y.lo) 
        {
            result = +1;
        } 
        else if (x.lo < y.lo) 
        {
            result = -1;
        } 
        else 
        {
            result = 0;
        }
    }
    
    return result;
}


/**************************************************************************
                        f i c l L o n g M u l
** Portable versions of ficlLongMul and ficlLongDiv in C
** Contributed by:
** Michael A. Gauland   gaulandm@mdhost.cse.tek.com  
**************************************************************************/
DPUNS ficlLongMul(FICL_UNS x, FICL_UNS y)
{
    DPUNS result = { 0, 0 };
    DPUNS addend;
    
    addend.lo = y;
    addend.hi = 0; /* No sign extension--arguments are unsigned */
    
    while (x != 0) 
    {
        if ( x & 1) 
        {
            result = m64Add(result, addend);
        }
        x >>= 1;
        addend = m64ASL(addend);
    }
    return result;
}


/**************************************************************************
                        f i c l L o n g D i v
** Portable versions of ficlLongMul and ficlLongDiv in C
** Contributed by:
** Michael A. Gauland   gaulandm@mdhost.cse.tek.com  
**************************************************************************/
UNSQR ficlLongDiv(DPUNS q, FICL_UNS y)
{
    UNSQR result;
    DPUNS quotient;
    DPUNS subtrahend;
    DPUNS mask;

    quotient.lo = 0;
    quotient.hi = 0;
    
    subtrahend.lo = y;
    subtrahend.hi = 0;
    
    mask.lo = 1;
    mask.hi = 0;
    
    while ((m64Compare(subtrahend, q) < 0) &&
           (subtrahend.hi & CELL_HI_BIT) == 0)
    {
        mask = m64ASL(mask);
        subtrahend = m64ASL(subtrahend);
    }
    
    while (mask.lo != 0 || mask.hi != 0) 
    {
        if (m64Compare(subtrahend, q) <= 0) 
        {
            q = m64Sub( q, subtrahend);
            quotient = m64Or(quotient, mask);
        }
        mask = m64ASR(mask);
        subtrahend = m64ASR(subtrahend);
    }
    
    result.quot = quotient.lo;
    result.rem = q.lo;
    return result;
}

#endif

