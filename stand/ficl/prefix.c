/*******************************************************************
** p r e f i x . c
** Forth Inspired Command Language
** Parser extensions for Ficl
** Authors: Larry Hastings & John Sadler (john_sadler@alum.mit.edu)
** Created: April 2001
** $Id: prefix.c,v 1.6 2001/12/05 07:21:34 jsadler Exp $
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

#include <string.h>
#include <ctype.h>
#include "ficl.h"
#include "math64.h"

/*
** (jws) revisions: 
** A prefix is a word in a dedicated wordlist (name stored in list_name below)
** that is searched in a special way by the prefix parse step. When a prefix
** matches the beginning of an incoming token, push the non-prefix part of the
** token back onto the input stream and execute the prefix code.
**
** The parse step is called ficlParsePrefix. 
** Storing prefix entries in the dictionary greatly simplifies
** the process of matching and dispatching prefixes, avoids the
** need to clean up a dynamically allocated prefix list when the system
** goes away, but still allows prefixes to be allocated at runtime.
*/

static char list_name[] = "<prefixes>";

/**************************************************************************
                        f i c l P a r s e P r e f i x
** This is the parse step for prefixes - it checks an incoming word
** to see if it starts with a prefix, and if so runs the corrseponding
** code against the remainder of the word and returns true.
**************************************************************************/
int ficlParsePrefix(FICL_VM *pVM, STRINGINFO si)
{
    int i;
    FICL_HASH *pHash;
    FICL_WORD *pFW = ficlLookup(pVM->pSys, list_name);

    /* 
    ** Make sure we found the prefix dictionary - otherwise silently fail
    ** If forth-wordlist is not in the search order, we won't find the prefixes.
    */
    if (!pFW)
        return FICL_FALSE;

    pHash = (FICL_HASH *)(pFW->param[0].p);
    /*
    ** Walk the list looking for a match with the beginning of the incoming token
    */
    for (i = 0; i < (int)pHash->size; i++)
    {
        pFW = pHash->table[i];
        while (pFW != NULL)
        {
            int n;
            n = pFW->nName;
            /*
            ** If we find a match, adjust the TIB to give back the non-prefix characters
            ** and execute the prefix word.
            */
            if (!strincmp(SI_PTR(si), pFW->name, (FICL_UNS)n))
            {
                /* (sadler) fixed off-by-one error when the token has no trailing space in the TIB */
				vmSetTibIndex(pVM, si.cp + n - pVM->tib.cp );
                vmExecute(pVM, pFW);

                return (int)FICL_TRUE;
            }
            pFW = pFW->link;
        }
    }

    return FICL_FALSE;
}


static void tempBase(FICL_VM *pVM, int base)
{
    int oldbase = pVM->base;
    STRINGINFO si = vmGetWord0(pVM);

    pVM->base = base;
    if (!ficlParseNumber(pVM, si)) 
    {
        int i = SI_COUNT(si);
        vmThrowErr(pVM, "%.*s not recognized", i, SI_PTR(si));
    }

    pVM->base = oldbase;
    return;
}

static void fTempBase(FICL_VM *pVM)
{
    int base = stackPopINT(pVM->pStack);
    tempBase(pVM, base);
    return;
}

static void prefixHex(FICL_VM *pVM)
{
    tempBase(pVM, 16);
}

static void prefixTen(FICL_VM *pVM)
{
    tempBase(pVM, 10);
}


/**************************************************************************
                        f i c l C o m p i l e P r e f i x
** Build prefix support into the dictionary and the parser
** Note: since prefixes always execute, they are effectively IMMEDIATE.
** If they need to generate code in compile state you must add
** this code explicitly.
**************************************************************************/
void ficlCompilePrefix(FICL_SYSTEM *pSys)
{
    FICL_DICT *dp = pSys->dp;
    FICL_HASH *pHash;
    FICL_HASH *pPrevCompile = dp->pCompile;
#if (FICL_EXTENDED_PREFIX)
    FICL_WORD *pFW;
#endif
    
    /*
    ** Create a named wordlist for prefixes to reside in...
    ** Since we're doing a special kind of search, make it
    ** a single bucket hashtable - hashing does not help here.
    */
    pHash = dictCreateWordlist(dp, 1);
    pHash->name = list_name;
    dictAppendWord(dp, list_name, constantParen, FW_DEFAULT);
    dictAppendCell(dp, LVALUEtoCELL(pHash));

	/*
	** Put __tempbase in the forth-wordlist
	*/
    dictAppendWord(dp, "__tempbase", fTempBase, FW_DEFAULT);

    /*
    ** Temporarily make the prefix list the compile wordlist so that
    ** we can create some precompiled prefixes.
    */
    dp->pCompile = pHash;
    dictAppendWord(dp, "0x", prefixHex, FW_DEFAULT);
    dictAppendWord(dp, "0d", prefixTen, FW_DEFAULT);
#if (FICL_EXTENDED_PREFIX)
    pFW = ficlLookup(pSys, "\\");
    if (pFW)
    {
        dictAppendWord(dp, "//", pFW->code, FW_DEFAULT);
    }
#endif
    dp->pCompile = pPrevCompile;

    return;
}
