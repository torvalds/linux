/*******************************************************************
** s t a c k . c
** Forth Inspired Command Language
** Author: John Sadler (john_sadler@alum.mit.edu)
** Created: 16 Oct 1997
** $Id: stack.c,v 1.10 2001/12/05 07:21:34 jsadler Exp $
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

#ifdef TESTMAIN
#include <stdlib.h>
#else
#include <stand.h>
#endif
#include "ficl.h"

#define STKDEPTH(s) ((s)->sp - (s)->base)

/*
** N O T E: Stack convention:
**
** sp points to the first available cell
** push: store value at sp, increment sp
** pop:  decrement sp, fetch value at sp
** Stack grows from low to high memory
*/

/*******************************************************************
                    v m C h e c k S t a c k
** Check the parameter stack for underflow or overflow.
** nCells controls the type of check: if nCells is zero,
** the function checks the stack state for underflow and overflow.
** If nCells > 0, checks to see that the stack has room to push
** that many cells. If less than zero, checks to see that the
** stack has room to pop that many cells. If any test fails,
** the function throws (via vmThrow) a VM_ERREXIT exception.
*******************************************************************/
void vmCheckStack(FICL_VM *pVM, int popCells, int pushCells)
{
    FICL_STACK *pStack = pVM->pStack;
    int nFree = pStack->base + pStack->nCells - pStack->sp;

    if (popCells > STKDEPTH(pStack))
    {
        vmThrowErr(pVM, "Error: stack underflow");
    }

    if (nFree < pushCells - popCells)
    {
        vmThrowErr(pVM, "Error: stack overflow");
    }

    return;
}

#if FICL_WANT_FLOAT
void vmCheckFStack(FICL_VM *pVM, int popCells, int pushCells)
{
    FICL_STACK *fStack = pVM->fStack;
    int nFree = fStack->base + fStack->nCells - fStack->sp;

    if (popCells > STKDEPTH(fStack))
    {
        vmThrowErr(pVM, "Error: float stack underflow");
    }

    if (nFree < pushCells - popCells)
    {
        vmThrowErr(pVM, "Error: float stack overflow");
    }
}
#endif

/*******************************************************************
                    s t a c k C r e a t e
** 
*******************************************************************/

FICL_STACK *stackCreate(unsigned nCells)
{
    size_t size = sizeof (FICL_STACK) + nCells * sizeof (CELL);
    FICL_STACK *pStack = ficlMalloc(size);

#if FICL_ROBUST
    assert (nCells != 0);
    assert (pStack != NULL);
#endif

    pStack->nCells = nCells;
    pStack->sp     = pStack->base;
    pStack->pFrame = NULL;
    return pStack;
}


/*******************************************************************
                    s t a c k D e l e t e
** 
*******************************************************************/

void stackDelete(FICL_STACK *pStack)
{
    if (pStack)
        ficlFree(pStack);
    return;
}


/*******************************************************************
                    s t a c k D e p t h 
** 
*******************************************************************/

int stackDepth(FICL_STACK *pStack)
{
    return STKDEPTH(pStack);
}

/*******************************************************************
                    s t a c k D r o p
** 
*******************************************************************/

void stackDrop(FICL_STACK *pStack, int n)
{
#if FICL_ROBUST
    assert(n > 0);
#endif
    pStack->sp -= n;
    return;
}


/*******************************************************************
                    s t a c k F e t c h
** 
*******************************************************************/

CELL stackFetch(FICL_STACK *pStack, int n)
{
    return pStack->sp[-n-1];
}

void stackStore(FICL_STACK *pStack, int n, CELL c)
{
    pStack->sp[-n-1] = c;
    return;
}


/*******************************************************************
                    s t a c k G e t T o p
** 
*******************************************************************/

CELL stackGetTop(FICL_STACK *pStack)
{
    return pStack->sp[-1];
}


/*******************************************************************
                    s t a c k L i n k
** Link a frame using the stack's frame pointer. Allot space for
** nCells cells in the frame
** 1) Push pFrame
** 2) pFrame = sp
** 3) sp += nCells
*******************************************************************/

void stackLink(FICL_STACK *pStack, int nCells)
{
    stackPushPtr(pStack, pStack->pFrame);
    pStack->pFrame = pStack->sp;
    pStack->sp += nCells;
    return;
}


/*******************************************************************
                    s t a c k U n l i n k
** Unink a stack frame previously created by stackLink
** 1) sp = pFrame
** 2) pFrame = pop()
*******************************************************************/

void stackUnlink(FICL_STACK *pStack)
{
    pStack->sp = pStack->pFrame;
    pStack->pFrame = stackPopPtr(pStack);
    return;
}


/*******************************************************************
                    s t a c k P i c k
** 
*******************************************************************/

void stackPick(FICL_STACK *pStack, int n)
{
    stackPush(pStack, stackFetch(pStack, n));
    return;
}


/*******************************************************************
                    s t a c k P o p
** 
*******************************************************************/

CELL stackPop(FICL_STACK *pStack)
{
    return *--pStack->sp;
}

void *stackPopPtr(FICL_STACK *pStack)
{
    return (*--pStack->sp).p;
}

FICL_UNS stackPopUNS(FICL_STACK *pStack)
{
    return (*--pStack->sp).u;
}

FICL_INT stackPopINT(FICL_STACK *pStack)
{
    return (*--pStack->sp).i;
}

#if (FICL_WANT_FLOAT)
float stackPopFloat(FICL_STACK *pStack)
{
    return (*(--pStack->sp)).f;
}
#endif

/*******************************************************************
                    s t a c k P u s h
** 
*******************************************************************/

void stackPush(FICL_STACK *pStack, CELL c)
{
    *pStack->sp++ = c;
}

void stackPushPtr(FICL_STACK *pStack, void *ptr)
{
    *pStack->sp++ = LVALUEtoCELL(ptr);
}

void stackPushUNS(FICL_STACK *pStack, FICL_UNS u)
{
    *pStack->sp++ = LVALUEtoCELL(u);
}

void stackPushINT(FICL_STACK *pStack, FICL_INT i)
{
    *pStack->sp++ = LVALUEtoCELL(i);
}

#if (FICL_WANT_FLOAT)
void stackPushFloat(FICL_STACK *pStack, FICL_FLOAT f)
{
    *pStack->sp++ = LVALUEtoCELL(f);
}
#endif

/*******************************************************************
                    s t a c k R e s e t
** 
*******************************************************************/

void stackReset(FICL_STACK *pStack)
{
    pStack->sp = pStack->base;
    return;
}


/*******************************************************************
                    s t a c k R o l l 
** Roll nth stack entry to the top (counting from zero), if n is 
** >= 0. Drop other entries as needed to fill the hole.
** If n < 0, roll top-of-stack to nth entry, pushing others
** upward as needed to fill the hole.
*******************************************************************/

void stackRoll(FICL_STACK *pStack, int n)
{
    CELL c;
    CELL *pCell;

    if (n == 0)
        return;
    else if (n > 0)
    {
        pCell = pStack->sp - n - 1;
        c = *pCell;

        for (;n > 0; --n, pCell++)
        {
            *pCell = pCell[1];
        }

        *pCell = c;
    }
    else
    {
        pCell = pStack->sp - 1;
        c = *pCell;

        for (; n < 0; ++n, pCell--)
        {
            *pCell = pCell[-1];
        }

        *pCell = c;
    }
    return;
}


/*******************************************************************
                    s t a c k S e t T o p
** 
*******************************************************************/

void stackSetTop(FICL_STACK *pStack, CELL c)
{
    pStack->sp[-1] = c;
    return;
}


