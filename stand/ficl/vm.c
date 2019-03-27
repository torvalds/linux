/*******************************************************************
** v m . c
** Forth Inspired Command Language - virtual machine methods
** Author: John Sadler (john_sadler@alum.mit.edu)
** Created: 19 July 1997
** $Id: vm.c,v 1.13 2001/12/05 07:21:34 jsadler Exp $
*******************************************************************/
/*
** This file implements the virtual machine of FICL. Each virtual
** machine retains the state of an interpreter. A virtual machine
** owns a pair of stacks for parameters and return addresses, as
** well as a pile of state variables and the two dedicated registers
** of the interp.
*/
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

#ifdef TESTMAIN
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#else
#include <stand.h>
#endif
#include <stdarg.h>
#include <string.h>
#include "ficl.h"

static char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";


/**************************************************************************
                        v m B r a n c h R e l a t i v e 
** 
**************************************************************************/
void vmBranchRelative(FICL_VM *pVM, int offset)
{
    pVM->ip += offset;
    return;
}


/**************************************************************************
                        v m C r e a t e
** Creates a virtual machine either from scratch (if pVM is NULL on entry)
** or by resizing and reinitializing an existing VM to the specified stack
** sizes.
**************************************************************************/
FICL_VM *vmCreate(FICL_VM *pVM, unsigned nPStack, unsigned nRStack)
{
    if (pVM == NULL)
    {
        pVM = (FICL_VM *)ficlMalloc(sizeof (FICL_VM));
        assert (pVM);
        memset(pVM, 0, sizeof (FICL_VM));
    }

    if (pVM->pStack)
        stackDelete(pVM->pStack);
    pVM->pStack = stackCreate(nPStack);

    if (pVM->rStack)
        stackDelete(pVM->rStack);
    pVM->rStack = stackCreate(nRStack);

#if FICL_WANT_FLOAT
    if (pVM->fStack)
        stackDelete(pVM->fStack);
    pVM->fStack = stackCreate(nPStack);
#endif

    pVM->textOut = ficlTextOut;

    vmReset(pVM);
    return pVM;
}


/**************************************************************************
                        v m D e l e t e
** Free all memory allocated to the specified VM and its subordinate 
** structures.
**************************************************************************/
void vmDelete (FICL_VM *pVM)
{
    if (pVM)
    {
        ficlFree(pVM->pStack);
        ficlFree(pVM->rStack);
#if FICL_WANT_FLOAT
        ficlFree(pVM->fStack);
#endif
        ficlFree(pVM);
    }

    return;
}


/**************************************************************************
                        v m E x e c u t e
** Sets up the specified word to be run by the inner interpreter.
** Executes the word's code part immediately, but in the case of
** colon definition, the definition itself needs the inner interp
** to complete. This does not happen until control reaches ficlExec
**************************************************************************/
void vmExecute(FICL_VM *pVM, FICL_WORD *pWord)
{
    pVM->runningWord = pWord;
    pWord->code(pVM);
    return;
}


/**************************************************************************
                        v m I n n e r L o o p
** the mysterious inner interpreter...
** This loop is the address interpreter that makes colon definitions
** work. Upon entry, it assumes that the IP points to an entry in 
** a definition (the body of a colon word). It runs one word at a time
** until something does vmThrow. The catcher for this is expected to exist
** in the calling code.
** vmThrow gets you out of this loop with a longjmp()
** Visual C++ 5 chokes on this loop in Release mode. Aargh.
**************************************************************************/
#if INLINE_INNER_LOOP == 0
void vmInnerLoop(FICL_VM *pVM)
{
    M_INNER_LOOP(pVM);
}
#endif
#if 0
/*
** Recast inner loop that inlines tokens for control structures, arithmetic and stack operations, 
** as well as create does> : ; and various literals
*/
typedef enum
{
    PATCH = 0,
    L0,
    L1,
    L2,
    LMINUS1,
    LMINUS2,
    DROP,
    SWAP,
    DUP,
    PICK,
    ROLL,
    FETCH,
    STORE,
    BRANCH,
    CBRANCH,
    LEAVE,
    TO_R,
    R_FROM,
    EXIT;
} OPCODE;

typedef CELL *IPTYPE;

void vmInnerLoop(FICL_VM *pVM)
{
    IPTYPE ip = pVM->ip;
    FICL_STACK *pStack = pVM->pStack;

    for (;;)
    {
        OPCODE o = (*ip++).i;
        CELL c;
        switch (o)
        {
        case L0:
            stackPushINT(pStack, 0);
            break;
        case L1:
            stackPushINT(pStack, 1);
            break;
        case L2:
            stackPushINT(pStack, 2);
            break;
        case LMINUS1:
            stackPushINT(pStack, -1);
            break;
        case LMINUS2:
            stackPushINT(pStack, -2);
            break;
        case DROP:
            stackDrop(pStack, 1);
            break;
        case SWAP:
            stackRoll(pStack, 1);
            break;
        case DUP:
            stackPick(pStack, 0);
            break;
        case PICK:
            c = *ip++;
            stackPick(pStack, c.i);
            break;
        case ROLL:
            c = *ip++;
            stackRoll(pStack, c.i);
            break;
        case EXIT:
            return;
        }
    }

    return;
}
#endif



/**************************************************************************
                        v m G e t D i c t
** Returns the address dictionary for this VM's system
**************************************************************************/
FICL_DICT  *vmGetDict(FICL_VM *pVM)
{
	assert(pVM);
	return pVM->pSys->dp;
}


/**************************************************************************
                        v m G e t S t r i n g
** Parses a string out of the VM input buffer and copies up to the first
** FICL_STRING_MAX characters to the supplied destination buffer, a
** FICL_STRING. The destination string is NULL terminated.
** 
** Returns the address of the first unused character in the dest buffer.
**************************************************************************/
char *vmGetString(FICL_VM *pVM, FICL_STRING *spDest, char delimiter)
{
    STRINGINFO si = vmParseStringEx(pVM, delimiter, 0);

    if (SI_COUNT(si) > FICL_STRING_MAX)
    {
        SI_SETLEN(si, FICL_STRING_MAX);
    }

    strncpy(spDest->text, SI_PTR(si), SI_COUNT(si));
    spDest->text[SI_COUNT(si)] = '\0';
    spDest->count = (FICL_COUNT)SI_COUNT(si);

    return spDest->text + SI_COUNT(si) + 1;
}


/**************************************************************************
                        v m G e t W o r d
** vmGetWord calls vmGetWord0 repeatedly until it gets a string with 
** non-zero length.
**************************************************************************/
STRINGINFO vmGetWord(FICL_VM *pVM)
{
    STRINGINFO si = vmGetWord0(pVM);

    if (SI_COUNT(si) == 0)
    {
        vmThrow(pVM, VM_RESTART);
    }

    return si;
}


/**************************************************************************
                        v m G e t W o r d 0
** Skip leading whitespace and parse a space delimited word from the tib.
** Returns the start address and length of the word. Updates the tib
** to reflect characters consumed, including the trailing delimiter.
** If there's nothing of interest in the tib, returns zero. This function
** does not use vmParseString because it uses isspace() rather than a
** single  delimiter character.
**************************************************************************/
STRINGINFO vmGetWord0(FICL_VM *pVM)
{
    char *pSrc      = vmGetInBuf(pVM);
    char *pEnd      = vmGetInBufEnd(pVM);
    STRINGINFO si;
    FICL_UNS count = 0;
    char ch = 0;

    pSrc = skipSpace(pSrc, pEnd);
    SI_SETPTR(si, pSrc);

/*
    for (ch = *pSrc; (pEnd != pSrc) && !isspace(ch); ch = *++pSrc)
    {
        count++;
    }
*/

    /* Changed to make Purify happier.  --lch */
    for (;;)
    {
        if (pEnd == pSrc)
            break;
        ch = *pSrc;
        if (isspace(ch))
            break;
        count++;
        pSrc++;
    }

    SI_SETLEN(si, count);

    if ((pEnd != pSrc) && isspace(ch))    /* skip one trailing delimiter */
        pSrc++;

    vmUpdateTib(pVM, pSrc);

    return si;
}


/**************************************************************************
                        v m G e t W o r d T o P a d
** Does vmGetWord and copies the result to the pad as a NULL terminated
** string. Returns the length of the string. If the string is too long 
** to fit in the pad, it is truncated.
**************************************************************************/
int vmGetWordToPad(FICL_VM *pVM)
{
    STRINGINFO si;
    char *cp = (char *)pVM->pad;
    si = vmGetWord(pVM);

    if (SI_COUNT(si) > nPAD)
        SI_SETLEN(si, nPAD);

    strncpy(cp, SI_PTR(si), SI_COUNT(si));
    cp[SI_COUNT(si)] = '\0';
    return (int)(SI_COUNT(si));
}


/**************************************************************************
                        v m P a r s e S t r i n g
** Parses a string out of the input buffer using the delimiter
** specified. Skips leading delimiters, marks the start of the string,
** and counts characters to the next delimiter it encounters. It then 
** updates the vm input buffer to consume all these chars, including the
** trailing delimiter. 
** Returns the address and length of the parsed string, not including the
** trailing delimiter.
**************************************************************************/
STRINGINFO vmParseString(FICL_VM *pVM, char delim)
{ 
    return vmParseStringEx(pVM, delim, 1);
}

STRINGINFO vmParseStringEx(FICL_VM *pVM, char delim, char fSkipLeading)
{
    STRINGINFO si;
    char *pSrc      = vmGetInBuf(pVM);
    char *pEnd      = vmGetInBufEnd(pVM);
    char ch;

    if (fSkipLeading)
    {                       /* skip lead delimiters */
        while ((pSrc != pEnd) && (*pSrc == delim))
            pSrc++;
    }

    SI_SETPTR(si, pSrc);    /* mark start of text */

    for (ch = *pSrc; (pSrc != pEnd)
                  && (ch != delim)
                  && (ch != '\r') 
                  && (ch != '\n'); ch = *++pSrc)
    {
        ;                   /* find next delimiter or end of line */
    }

                            /* set length of result */
    SI_SETLEN(si, pSrc - SI_PTR(si));

    if ((pSrc != pEnd) && (*pSrc == delim))     /* gobble trailing delimiter */
        pSrc++;

    vmUpdateTib(pVM, pSrc);
    return si;
}


/**************************************************************************
                        v m P o p
** 
**************************************************************************/
CELL vmPop(FICL_VM *pVM)
{
    return stackPop(pVM->pStack);
}


/**************************************************************************
                        v m P u s h
** 
**************************************************************************/
void vmPush(FICL_VM *pVM, CELL c)
{
    stackPush(pVM->pStack, c);
    return;
}


/**************************************************************************
                        v m P o p I P
** 
**************************************************************************/
void vmPopIP(FICL_VM *pVM)
{
    pVM->ip = (IPTYPE)(stackPopPtr(pVM->rStack));
    return;
}


/**************************************************************************
                        v m P u s h I P
** 
**************************************************************************/
void vmPushIP(FICL_VM *pVM, IPTYPE newIP)
{
    stackPushPtr(pVM->rStack, (void *)pVM->ip);
    pVM->ip = newIP;
    return;
}


/**************************************************************************
                        v m P u s h T i b
** Binds the specified input string to the VM and clears >IN (the index)
**************************************************************************/
void vmPushTib(FICL_VM *pVM, char *text, FICL_INT nChars, TIB *pSaveTib)
{
    if (pSaveTib)
    {
        *pSaveTib = pVM->tib;
    }

    pVM->tib.cp = text;
    pVM->tib.end = text + nChars;
    pVM->tib.index = 0;
}


void vmPopTib(FICL_VM *pVM, TIB *pTib)
{
    if (pTib)
    {
        pVM->tib = *pTib;
    }
    return;
}


/**************************************************************************
                        v m Q u i t
** 
**************************************************************************/
void vmQuit(FICL_VM *pVM)
{
    stackReset(pVM->rStack);
    pVM->fRestart    = 0;
    pVM->ip          = NULL;
    pVM->runningWord = NULL;
    pVM->state       = INTERPRET;
    pVM->tib.cp      = NULL;
    pVM->tib.end     = NULL;
    pVM->tib.index   = 0;
    pVM->pad[0]      = '\0';
    pVM->sourceID.i  = 0;
    return;
}


/**************************************************************************
                        v m R e s e t 
** 
**************************************************************************/
void vmReset(FICL_VM *pVM)
{
    vmQuit(pVM);
    stackReset(pVM->pStack);
#if FICL_WANT_FLOAT
    stackReset(pVM->fStack);
#endif
    pVM->base        = 10;
    return;
}


/**************************************************************************
                        v m S e t T e x t O u t
** Binds the specified output callback to the vm. If you pass NULL,
** binds the default output function (ficlTextOut)
**************************************************************************/
void vmSetTextOut(FICL_VM *pVM, OUTFUNC textOut)
{
    if (textOut)
        pVM->textOut = textOut;
    else
        pVM->textOut = ficlTextOut;

    return;
}


/**************************************************************************
                        v m T e x t O u t
** Feeds text to the vm's output callback
**************************************************************************/
void vmTextOut(FICL_VM *pVM, char *text, int fNewline)
{
    assert(pVM);
    assert(pVM->textOut);
    (pVM->textOut)(pVM, text, fNewline);

    return;
}


/**************************************************************************
                        v m T h r o w
** 
**************************************************************************/
void vmThrow(FICL_VM *pVM, int except)
{
    if (pVM->pState)
        longjmp(*(pVM->pState), except);
}


void vmThrowErr(FICL_VM *pVM, char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vsprintf(pVM->pad, fmt, va);
    vmTextOut(pVM, pVM->pad, 1);
    va_end(va);
    longjmp(*(pVM->pState), VM_ERREXIT);
}


/**************************************************************************
                        w o r d I s I m m e d i a t e
** 
**************************************************************************/
int wordIsImmediate(FICL_WORD *pFW)
{
    return ((pFW != NULL) && (pFW->flags & FW_IMMEDIATE));
}


/**************************************************************************
                        w o r d I s C o m p i l e O n l y
** 
**************************************************************************/
int wordIsCompileOnly(FICL_WORD *pFW)
{
    return ((pFW != NULL) && (pFW->flags & FW_COMPILE));
}


/**************************************************************************
                        s t r r e v
** 
**************************************************************************/
char *strrev( char *string )    
{                               /* reverse a string in-place */
    int i = strlen(string);
    char *p1 = string;          /* first char of string */
    char *p2 = string + i - 1;  /* last non-NULL char of string */
    char c;

    if (i > 1)
    {
        while (p1 < p2)
        {
            c = *p2;
            *p2 = *p1;
            *p1 = c;
            p1++; p2--;
        }
    }
        
    return string;
}


/**************************************************************************
                        d i g i t _ t o _ c h a r
** 
**************************************************************************/
char digit_to_char(int value)
{
    return digits[value];
}


/**************************************************************************
                        i s P o w e r O f T w o
** Tests whether supplied argument is an integer power of 2 (2**n)
** where 32 > n > 1, and returns n if so. Otherwise returns zero.
**************************************************************************/
int isPowerOfTwo(FICL_UNS u)
{
    int i = 1;
    FICL_UNS t = 2;

    for (; ((t <= u) && (t != 0)); i++, t <<= 1)
    {
        if (u == t)
            return i;
    }

    return 0;
}


/**************************************************************************
                        l t o a
** 
**************************************************************************/
char *ltoa( FICL_INT value, char *string, int radix )
{                               /* convert long to string, any base */
    char *cp = string;
    int sign = ((radix == 10) && (value < 0));
    int pwr;

    assert(radix > 1);
    assert(radix < 37);
    assert(string);

    pwr = isPowerOfTwo((FICL_UNS)radix);

    if (sign)
        value = -value;

    if (value == 0)
        *cp++ = '0';
    else if (pwr != 0)
    {
        FICL_UNS v = (FICL_UNS) value;
        FICL_UNS mask = (FICL_UNS) ~(-1 << pwr);
        while (v)
        {
            *cp++ = digits[v & mask];
            v >>= pwr;
        }
    }
    else
    {
        UNSQR result;
        DPUNS v;
        v.hi = 0;
        v.lo = (FICL_UNS)value;
        while (v.lo)
        {
            result = ficlLongDiv(v, (FICL_UNS)radix);
            *cp++ = digits[result.rem];
            v.lo = result.quot;
        }
    }

    if (sign)
        *cp++ = '-';

    *cp++ = '\0';

    return strrev(string);
}


/**************************************************************************
                        u l t o a
** 
**************************************************************************/
char *ultoa(FICL_UNS value, char *string, int radix )
{                               /* convert long to string, any base */
    char *cp = string;
    DPUNS ud;
    UNSQR result;

    assert(radix > 1);
    assert(radix < 37);
    assert(string);

    if (value == 0)
        *cp++ = '0';
    else
    {
        ud.hi = 0;
        ud.lo = value;
        result.quot = value;

        while (ud.lo)
        {
            result = ficlLongDiv(ud, (FICL_UNS)radix);
            ud.lo = result.quot;
            *cp++ = digits[result.rem];
        }
    }

    *cp++ = '\0';

    return strrev(string);
}


/**************************************************************************
                        c a s e F o l d
** Case folds a NULL terminated string in place. All characters
** get converted to lower case.
**************************************************************************/
char *caseFold(char *cp)
{
    char *oldCp = cp;

    while (*cp)
    {
        if (isupper(*cp))
            *cp = (char)tolower(*cp);
        cp++;
    }

    return oldCp;
}


/**************************************************************************
                        s t r i n c m p
** (jws) simplified the code a bit in hopes of appeasing Purify
**************************************************************************/
int strincmp(char *cp1, char *cp2, FICL_UNS count)
{
    int i = 0;

    for (; 0 < count; ++cp1, ++cp2, --count)
    {
        i = tolower(*cp1) - tolower(*cp2);
        if (i != 0)
            return i;
        else if (*cp1 == '\0')
            return 0;
    }
    return 0;
}

/**************************************************************************
                        s k i p S p a c e
** Given a string pointer, returns a pointer to the first non-space
** char of the string, or to the NULL terminator if no such char found.
** If the pointer reaches "end" first, stop there. Pass NULL to 
** suppress this behavior.
**************************************************************************/
char *skipSpace(char *cp, char *end)
{
    assert(cp);

    while ((cp != end) && isspace(*cp))
        cp++;

    return cp;
}


