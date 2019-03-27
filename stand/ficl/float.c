/*******************************************************************
** f l o a t . c
** Forth Inspired Command Language
** ANS Forth FLOAT word-set written in C
** Author: Guy Carver & John Sadler (john_sadler@alum.mit.edu)
** Created: Apr 2001
** $Id: float.c,v 1.8 2001/12/05 07:21:34 jsadler Exp $
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

#if FICL_WANT_FLOAT
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/*******************************************************************
** Do float addition r1 + r2.
** f+ ( r1 r2 -- r )
*******************************************************************/
static void Fadd(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 1);
#endif

    f = POPFLOAT();
    f += GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do float subtraction r1 - r2.
** f- ( r1 r2 -- r )
*******************************************************************/
static void Fsub(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 1);
#endif

    f = POPFLOAT();
    f = GETTOPF().f - f;
    SETTOPF(f);
}

/*******************************************************************
** Do float multiplication r1 * r2.
** f* ( r1 r2 -- r )
*******************************************************************/
static void Fmul(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 1);
#endif

    f = POPFLOAT();
    f *= GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do float negation.
** fnegate ( r -- r )
*******************************************************************/
static void Fnegate(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 1);
#endif

    f = -GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do float division r1 / r2.
** f/ ( r1 r2 -- r )
*******************************************************************/
static void Fdiv(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 1);
#endif

    f = POPFLOAT();
    f = GETTOPF().f / f;
    SETTOPF(f);
}

/*******************************************************************
** Do float + integer r + n.
** f+i ( r n -- r )
*******************************************************************/
static void Faddi(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    f = (FICL_FLOAT)POPINT();
    f += GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do float - integer r - n.
** f-i ( r n -- r )
*******************************************************************/
static void Fsubi(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    f = GETTOPF().f;
    f -= (FICL_FLOAT)POPINT();
    SETTOPF(f);
}

/*******************************************************************
** Do float * integer r * n.
** f*i ( r n -- r )
*******************************************************************/
static void Fmuli(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    f = (FICL_FLOAT)POPINT();
    f *= GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do float / integer r / n.
** f/i ( r n -- r )
*******************************************************************/
static void Fdivi(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    f = GETTOPF().f;
    f /= (FICL_FLOAT)POPINT();
    SETTOPF(f);
}

/*******************************************************************
** Do integer - float n - r.
** i-f ( n r -- r )
*******************************************************************/
static void isubf(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    f = (FICL_FLOAT)POPINT();
    f -= GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do integer / float n / r.
** i/f ( n r -- r )
*******************************************************************/
static void idivf(FICL_VM *pVM)
{
    FICL_FLOAT f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1,1);
    vmCheckStack(pVM, 1, 0);
#endif

    f = (FICL_FLOAT)POPINT();
    f /= GETTOPF().f;
    SETTOPF(f);
}

/*******************************************************************
** Do integer to float conversion.
** int>float ( n -- r )
*******************************************************************/
static void itof(FICL_VM *pVM)
{
    float f;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 0);
    vmCheckFStack(pVM, 0, 1);
#endif

    f = (float)POPINT();
    PUSHFLOAT(f);
}

/*******************************************************************
** Do float to integer conversion.
** float>int ( r -- n )
*******************************************************************/
static void Ftoi(FICL_VM *pVM)
{
    FICL_INT i;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 0, 1);
    vmCheckFStack(pVM, 1, 0);
#endif

    i = (FICL_INT)POPFLOAT();
    PUSHINT(i);
}

/*******************************************************************
** Floating point constant execution word.
*******************************************************************/
void FconstantParen(FICL_VM *pVM)
{
    FICL_WORD *pFW = pVM->runningWord;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 0, 1);
#endif

    PUSHFLOAT(pFW->param[0].f);
}

/*******************************************************************
** Create a floating point constant.
** fconstant ( r -"name"- )
*******************************************************************/
static void Fconstant(FICL_VM *pVM)
{
    FICL_DICT *dp = vmGetDict(pVM);
    STRINGINFO si = vmGetWord(pVM);

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
#endif

    dictAppendWord2(dp, si, FconstantParen, FW_DEFAULT);
    dictAppendCell(dp, stackPop(pVM->fStack));
}

/*******************************************************************
** Display a float in decimal format.
** f. ( r -- )
*******************************************************************/
static void FDot(FICL_VM *pVM)
{
    float f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
#endif

    f = POPFLOAT();
    sprintf(pVM->pad,"%#f ",f);
    vmTextOut(pVM, pVM->pad, 0);
}

/*******************************************************************
** Display a float in engineering format.
** fe. ( r -- )
*******************************************************************/
static void EDot(FICL_VM *pVM)
{
    float f;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
#endif

    f = POPFLOAT();
    sprintf(pVM->pad,"%#e ",f);
    vmTextOut(pVM, pVM->pad, 0);
}

/**************************************************************************
                        d i s p l a y FS t a c k
** Display the parameter stack (code for "f.s")
** f.s ( -- )
**************************************************************************/
static void displayFStack(FICL_VM *pVM)
{
    int d = stackDepth(pVM->fStack);
    int i;
    CELL *pCell;

    vmCheckFStack(pVM, 0, 0);

    vmTextOut(pVM, "F:", 0);

    if (d == 0)
        vmTextOut(pVM, "[0]", 0);
    else
    {
        ltoa(d, &pVM->pad[1], pVM->base);
        pVM->pad[0] = '[';
        strcat(pVM->pad,"] ");
        vmTextOut(pVM,pVM->pad,0);

        pCell = pVM->fStack->sp - d;
        for (i = 0; i < d; i++)
        {
            sprintf(pVM->pad,"%#f ",(*pCell++).f);
            vmTextOut(pVM,pVM->pad,0);
        }
    }
}

/*******************************************************************
** Do float stack depth.
** fdepth ( -- n )
*******************************************************************/
static void Fdepth(FICL_VM *pVM)
{
    int i;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 0, 1);
#endif

    i = stackDepth(pVM->fStack);
    PUSHINT(i);
}

/*******************************************************************
** Do float stack drop.
** fdrop ( r -- )
*******************************************************************/
static void Fdrop(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
#endif

    DROPF(1);
}

/*******************************************************************
** Do float stack 2drop.
** f2drop ( r r -- )
*******************************************************************/
static void FtwoDrop(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 0);
#endif

    DROPF(2);
}

/*******************************************************************
** Do float stack dup.
** fdup ( r -- r r )
*******************************************************************/
static void Fdup(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 2);
#endif

    PICKF(0);
}

/*******************************************************************
** Do float stack 2dup.
** f2dup ( r1 r2 -- r1 r2 r1 r2 )
*******************************************************************/
static void FtwoDup(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 4);
#endif

    PICKF(1);
    PICKF(1);
}

/*******************************************************************
** Do float stack over.
** fover ( r1 r2 -- r1 r2 r1 )
*******************************************************************/
static void Fover(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 3);
#endif

    PICKF(1);
}

/*******************************************************************
** Do float stack 2over.
** f2over ( r1 r2 r3 -- r1 r2 r3 r1 r2 )
*******************************************************************/
static void FtwoOver(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 4, 6);
#endif

    PICKF(3);
    PICKF(3);
}

/*******************************************************************
** Do float stack pick.
** fpick ( n -- r )
*******************************************************************/
static void Fpick(FICL_VM *pVM)
{
    CELL c = POP();

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, c.i+1, c.i+2);
#endif

    PICKF(c.i);
}

/*******************************************************************
** Do float stack ?dup.
** f?dup ( r -- r )
*******************************************************************/
static void FquestionDup(FICL_VM *pVM)
{
    CELL c;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 2);
#endif

    c = GETTOPF();
    if (c.f != 0)
        PICKF(0);
}

/*******************************************************************
** Do float stack roll.
** froll ( n -- )
*******************************************************************/
static void Froll(FICL_VM *pVM)
{
    int i = POP().i;
    i = (i > 0) ? i : 0;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, i+1, i+1);
#endif

    ROLLF(i);
}

/*******************************************************************
** Do float stack -roll.
** f-roll ( n -- )
*******************************************************************/
static void FminusRoll(FICL_VM *pVM)
{
    int i = POP().i;
    i = (i > 0) ? i : 0;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, i+1, i+1);
#endif

    ROLLF(-i);
}

/*******************************************************************
** Do float stack rot.
** frot ( r1 r2 r3  -- r2 r3 r1 )
*******************************************************************/
static void Frot(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 3, 3);
#endif

    ROLLF(2);
}

/*******************************************************************
** Do float stack -rot.
** f-rot ( r1 r2 r3  -- r3 r1 r2 )
*******************************************************************/
static void Fminusrot(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 3, 3);
#endif

    ROLLF(-2);
}

/*******************************************************************
** Do float stack swap.
** fswap ( r1 r2 -- r2 r1 )
*******************************************************************/
static void Fswap(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 2);
#endif

    ROLLF(1);
}

/*******************************************************************
** Do float stack 2swap
** f2swap ( r1 r2 r3 r4  -- r3 r4 r1 r2 )
*******************************************************************/
static void FtwoSwap(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 4, 4);
#endif

    ROLLF(3);
    ROLLF(3);
}

/*******************************************************************
** Get a floating point number from a variable.
** f@ ( n -- r )
*******************************************************************/
static void Ffetch(FICL_VM *pVM)
{
    CELL *pCell;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 0, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    pCell = (CELL *)POPPTR();
    PUSHFLOAT(pCell->f);
}

/*******************************************************************
** Store a floating point number into a variable.
** f! ( r n -- )
*******************************************************************/
static void Fstore(FICL_VM *pVM)
{
    CELL *pCell;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
    vmCheckStack(pVM, 1, 0);
#endif

    pCell = (CELL *)POPPTR();
    pCell->f = POPFLOAT();
}

/*******************************************************************
** Add a floating point number to contents of a variable.
** f+! ( r n -- )
*******************************************************************/
static void FplusStore(FICL_VM *pVM)
{
    CELL *pCell;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 0);
    vmCheckFStack(pVM, 1, 0);
#endif

    pCell = (CELL *)POPPTR();
    pCell->f += POPFLOAT();
}

/*******************************************************************
** Floating point literal execution word.
*******************************************************************/
static void fliteralParen(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckStack(pVM, 0, 1);
#endif

    PUSHFLOAT(*(float*)(pVM->ip));
    vmBranchRelative(pVM, 1);
}

/*******************************************************************
** Compile a floating point literal.
*******************************************************************/
static void fliteralIm(FICL_VM *pVM)
{
    FICL_DICT *dp = vmGetDict(pVM);
    FICL_WORD *pfLitParen = ficlLookup(pVM->pSys, "(fliteral)");

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
#endif

    dictAppendCell(dp, LVALUEtoCELL(pfLitParen));
    dictAppendCell(dp, stackPop(pVM->fStack));
}

/*******************************************************************
** Do float 0= comparison r = 0.0.
** f0= ( r -- T/F )
*******************************************************************/
static void FzeroEquals(FICL_VM *pVM)
{
    CELL c;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);                   /* Make sure something on float stack. */
    vmCheckStack(pVM, 0, 1);                    /* Make sure room for result. */
#endif

    c.i = FICL_BOOL(POPFLOAT() == 0);
    PUSH(c);
}

/*******************************************************************
** Do float 0< comparison r < 0.0.
** f0< ( r -- T/F )
*******************************************************************/
static void FzeroLess(FICL_VM *pVM)
{
    CELL c;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);                   /* Make sure something on float stack. */
    vmCheckStack(pVM, 0, 1);                    /* Make sure room for result. */
#endif

    c.i = FICL_BOOL(POPFLOAT() < 0);
    PUSH(c);
}

/*******************************************************************
** Do float 0> comparison r > 0.0.
** f0> ( r -- T/F )
*******************************************************************/
static void FzeroGreater(FICL_VM *pVM)
{
    CELL c;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
    vmCheckStack(pVM, 0, 1);
#endif

    c.i = FICL_BOOL(POPFLOAT() > 0);
    PUSH(c);
}

/*******************************************************************
** Do float = comparison r1 = r2.
** f= ( r1 r2 -- T/F )
*******************************************************************/
static void FisEqual(FICL_VM *pVM)
{
    float x, y;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 0);
    vmCheckStack(pVM, 0, 1);
#endif

    x = POPFLOAT();
    y = POPFLOAT();
    PUSHINT(FICL_BOOL(x == y));
}

/*******************************************************************
** Do float < comparison r1 < r2.
** f< ( r1 r2 -- T/F )
*******************************************************************/
static void FisLess(FICL_VM *pVM)
{
    float x, y;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 0);
    vmCheckStack(pVM, 0, 1);
#endif

    y = POPFLOAT();
    x = POPFLOAT();
    PUSHINT(FICL_BOOL(x < y));
}

/*******************************************************************
** Do float > comparison r1 > r2.
** f> ( r1 r2 -- T/F )
*******************************************************************/
static void FisGreater(FICL_VM *pVM)
{
    float x, y;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 2, 0);
    vmCheckStack(pVM, 0, 1);
#endif

    y = POPFLOAT();
    x = POPFLOAT();
    PUSHINT(FICL_BOOL(x > y));
}


/*******************************************************************
** Move float to param stack (assumes they both fit in a single CELL)
** f>s 
*******************************************************************/
static void FFrom(FICL_VM *pVM)
{
    CELL c;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 1, 0);
    vmCheckStack(pVM, 0, 1);
#endif

    c = stackPop(pVM->fStack);
    stackPush(pVM->pStack, c);
    return;
}

static void ToF(FICL_VM *pVM)
{
    CELL c;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 0, 1);
    vmCheckStack(pVM, 1, 0);
#endif

    c = stackPop(pVM->pStack);
    stackPush(pVM->fStack, c);
    return;
}


/**************************************************************************
                     F l o a t P a r s e S t a t e
** Enum to determine the current segement of a floating point number
** being parsed.
**************************************************************************/
#define NUMISNEG 1
#define EXPISNEG 2

typedef enum _floatParseState
{
    FPS_START,
    FPS_ININT,
    FPS_INMANT,
    FPS_STARTEXP,
    FPS_INEXP
} FloatParseState;

/**************************************************************************
                     f i c l P a r s e F l o a t N u m b e r
** pVM -- Virtual Machine pointer.
** si -- String to parse.
** Returns 1 if successful, 0 if not.
**************************************************************************/
int ficlParseFloatNumber( FICL_VM *pVM, STRINGINFO si )
{
    unsigned char ch, digit;
    char *cp;
    FICL_COUNT count;
    float power;
    float accum = 0.0f;
    float mant = 0.1f;
    FICL_INT exponent = 0;
    char flag = 0;
    FloatParseState estate = FPS_START;

#if FICL_ROBUST > 1
    vmCheckFStack(pVM, 0, 1);
#endif

    /*
    ** floating point numbers only allowed in base 10 
    */
    if (pVM->base != 10)
        return(0);


    cp = SI_PTR(si);
    count = (FICL_COUNT)SI_COUNT(si);

    /* Loop through the string's characters. */
    while ((count--) && ((ch = *cp++) != 0))
    {
        switch (estate)
        {
            /* At start of the number so look for a sign. */
            case FPS_START:
            {
                estate = FPS_ININT;
                if (ch == '-')
                {
                    flag |= NUMISNEG;
                    break;
                }
                if (ch == '+')
                {
                    break;
                }
            } /* Note!  Drop through to FPS_ININT */
            /*
            **Converting integer part of number.
            ** Only allow digits, decimal and 'E'. 
            */
            case FPS_ININT:
            {
                if (ch == '.')
                {
                    estate = FPS_INMANT;
                }
                else if ((ch == 'e') || (ch == 'E'))
                {
                    estate = FPS_STARTEXP;
                }
                else
                {
                    digit = (unsigned char)(ch - '0');
                    if (digit > 9)
                        return(0);

                    accum = accum * 10 + digit;

                }
                break;
            }
            /*
            ** Processing the fraction part of number.
            ** Only allow digits and 'E' 
            */
            case FPS_INMANT:
            {
                if ((ch == 'e') || (ch == 'E'))
                {
                    estate = FPS_STARTEXP;
                }
                else
                {
                    digit = (unsigned char)(ch - '0');
                    if (digit > 9)
                        return(0);

                    accum += digit * mant;
                    mant *= 0.1f;
                }
                break;
            }
            /* Start processing the exponent part of number. */
            /* Look for sign. */
            case FPS_STARTEXP:
            {
                estate = FPS_INEXP;

                if (ch == '-')
                {
                    flag |= EXPISNEG;
                    break;
                }
                else if (ch == '+')
                {
                    break;
                }
            }       /* Note!  Drop through to FPS_INEXP */
            /*
            ** Processing the exponent part of number.
            ** Only allow digits. 
            */
            case FPS_INEXP:
            {
                digit = (unsigned char)(ch - '0');
                if (digit > 9)
                    return(0);

                exponent = exponent * 10 + digit;

                break;
            }
        }
    }

    /* If parser never made it to the exponent this is not a float. */
    if (estate < FPS_STARTEXP)
        return(0);

    /* Set the sign of the number. */
    if (flag & NUMISNEG)
        accum = -accum;

    /* If exponent is not 0 then adjust number by it. */
    if (exponent != 0)
    {
        /* Determine if exponent is negative. */
        if (flag & EXPISNEG)
        {
            exponent = -exponent;
        }
        /* power = 10^x */
        power = (float)pow(10.0, exponent);
        accum *= power;
    }

    PUSHFLOAT(accum);
    if (pVM->state == COMPILE)
        fliteralIm(pVM);

    return(1);
}

#endif  /* FICL_WANT_FLOAT */

/**************************************************************************
** Add float words to a system's dictionary.
** pSys -- Pointer to the FICL sytem to add float words to.
**************************************************************************/
void ficlCompileFloat(FICL_SYSTEM *pSys)
{
    FICL_DICT *dp = pSys->dp;
    assert(dp);

#if FICL_WANT_FLOAT
    dictAppendWord(dp, ">float",    ToF,            FW_DEFAULT);
    /* d>f */
    dictAppendWord(dp, "f!",        Fstore,         FW_DEFAULT);
    dictAppendWord(dp, "f*",        Fmul,           FW_DEFAULT);
    dictAppendWord(dp, "f+",        Fadd,           FW_DEFAULT);
    dictAppendWord(dp, "f-",        Fsub,           FW_DEFAULT);
    dictAppendWord(dp, "f/",        Fdiv,           FW_DEFAULT);
    dictAppendWord(dp, "f0<",       FzeroLess,      FW_DEFAULT);
    dictAppendWord(dp, "f0=",       FzeroEquals,    FW_DEFAULT);
    dictAppendWord(dp, "f<",        FisLess,        FW_DEFAULT);
 /* 
    f>d 
 */
    dictAppendWord(dp, "f@",        Ffetch,         FW_DEFAULT);
 /* 
    falign 
    faligned 
 */
    dictAppendWord(dp, "fconstant", Fconstant,      FW_DEFAULT);
    dictAppendWord(dp, "fdepth",    Fdepth,         FW_DEFAULT);
    dictAppendWord(dp, "fdrop",     Fdrop,          FW_DEFAULT);
    dictAppendWord(dp, "fdup",      Fdup,           FW_DEFAULT);
    dictAppendWord(dp, "fliteral",  fliteralIm,     FW_IMMEDIATE);
/*
    float+
    floats
    floor
    fmax
    fmin
*/
    dictAppendWord(dp, "f?dup",     FquestionDup,   FW_DEFAULT);
    dictAppendWord(dp, "f=",        FisEqual,       FW_DEFAULT);
    dictAppendWord(dp, "f>",        FisGreater,     FW_DEFAULT);
    dictAppendWord(dp, "f0>",       FzeroGreater,   FW_DEFAULT);
    dictAppendWord(dp, "f2drop",    FtwoDrop,       FW_DEFAULT);
    dictAppendWord(dp, "f2dup",     FtwoDup,        FW_DEFAULT);
    dictAppendWord(dp, "f2over",    FtwoOver,       FW_DEFAULT);
    dictAppendWord(dp, "f2swap",    FtwoSwap,       FW_DEFAULT);
    dictAppendWord(dp, "f+!",       FplusStore,     FW_DEFAULT);
    dictAppendWord(dp, "f+i",       Faddi,          FW_DEFAULT);
    dictAppendWord(dp, "f-i",       Fsubi,          FW_DEFAULT);
    dictAppendWord(dp, "f*i",       Fmuli,          FW_DEFAULT);
    dictAppendWord(dp, "f/i",       Fdivi,          FW_DEFAULT);
    dictAppendWord(dp, "int>float", itof,           FW_DEFAULT);
    dictAppendWord(dp, "float>int", Ftoi,           FW_DEFAULT);
    dictAppendWord(dp, "f.",        FDot,           FW_DEFAULT);
    dictAppendWord(dp, "f.s",       displayFStack,  FW_DEFAULT);
    dictAppendWord(dp, "fe.",       EDot,           FW_DEFAULT);
    dictAppendWord(dp, "fover",     Fover,          FW_DEFAULT);
    dictAppendWord(dp, "fnegate",   Fnegate,        FW_DEFAULT);
    dictAppendWord(dp, "fpick",     Fpick,          FW_DEFAULT);
    dictAppendWord(dp, "froll",     Froll,          FW_DEFAULT);
    dictAppendWord(dp, "frot",      Frot,           FW_DEFAULT);
    dictAppendWord(dp, "fswap",     Fswap,          FW_DEFAULT);
    dictAppendWord(dp, "i-f",       isubf,          FW_DEFAULT);
    dictAppendWord(dp, "i/f",       idivf,          FW_DEFAULT);

    dictAppendWord(dp, "float>",    FFrom,          FW_DEFAULT);

    dictAppendWord(dp, "f-roll",    FminusRoll,     FW_DEFAULT);
    dictAppendWord(dp, "f-rot",     Fminusrot,      FW_DEFAULT);
    dictAppendWord(dp, "(fliteral)", fliteralParen, FW_COMPILE);

    ficlSetEnv(pSys, "floating",       FICL_FALSE);  /* not all required words are present */
    ficlSetEnv(pSys, "floating-ext",   FICL_FALSE);
    ficlSetEnv(pSys, "floating-stack", FICL_DEFAULT_STACK);
#endif
    return;
}

