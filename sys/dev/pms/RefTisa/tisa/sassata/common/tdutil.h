/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/** \file
 *
 * The file defines utilities for SAS/SATA TD layer
 *
 */

#ifndef __TDUTIL_H__
#define __TDUTIL_H__

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/tisa/api/tidefs.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdlist.h>


#define HEXDIGIT2CHAR(x)     (((x) < 10) ? ('0' + (x)) : ('A' + ((x) - 10)))
/*****************************************************************************
*! \brief tdDecimal2String
*
*  Purpose:  This function converts a given number into a decimal string.
*            
*  \param s:                  string to be generated
*  \param num:                number to be converted
*
*  \return None
*
*  \note - string s should be large enough to store decimal string of
*        num and a '\0' character
*
*****************************************************************************/
void 
tdDecimal2String(
  char *s, 
  bit32 num
  );

void
tdHexToString (
  char  *String,
  bit32  Value1,
  bit32  Value2,
  bit32  Strlength
  );

bit8 tdStr2Bit8 (char *buffer);

bit32 tdStr2ALPA (char *buffer);

void tdStr2WWN (char *buffer, bit8 * NodeName);

void tdWWN2Str (char *buffer, bit8 * NodeName);

/*****************************************************************************
*! \brief tdNextPowerOf2
*
*  Purpose:  This function is called to calculate the next power of 2
*            value of given value.
*            
*
*  \param  Value:             The value for which next power of 2 is requested
*
*  \return: The next power of 2 value of given Value
*
*****************************************************************************/
bit32 
tdNextPowerOf2 (
  bit32 Value
  );

osGLOBAL agBOOLEAN
tdListElementOnList(
    tdList_t *toFindHdr,
    tdList_t *listHdr
    );


#endif /* __TDUTIL_H__ */



