/* Copyright (c) 2008-2011 Freescale Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************//**

 @File          list.c

 @Description   Implementation of list.
*//***************************************************************************/
#include "std_ext.h"
#include "list_ext.h"


void NCSW_LIST_Append(t_List *p_NewList, t_List *p_Head)
{
    t_List *p_First = NCSW_LIST_FIRST(p_NewList);

    if (p_First != p_NewList)
    {
        t_List *p_Last  = NCSW_LIST_LAST(p_NewList);
        t_List *p_Cur   = NCSW_LIST_NEXT(p_Head);

        NCSW_LIST_PREV(p_First) = p_Head;
        NCSW_LIST_FIRST(p_Head) = p_First;
        NCSW_LIST_NEXT(p_Last)  = p_Cur;
        NCSW_LIST_LAST(p_Cur)   = p_Last;
    }
}


int NCSW_LIST_NumOfObjs(t_List *p_List)
{
    t_List *p_Tmp;
    int    numOfObjs = 0;

    if (!NCSW_LIST_IsEmpty(p_List))
        NCSW_LIST_FOR_EACH(p_Tmp, p_List)
            numOfObjs++;

    return numOfObjs;
}
