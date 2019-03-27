/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
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


/******************************************************************************
 @File          FM_muram.c

 @Description   FM MURAM ...
*//***************************************************************************/
#include "error_ext.h"
#include "std_ext.h"
#include "mm_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "fm_muram_ext.h"
#include "fm_common.h"

#define __ERR_MODULE__  MODULE_FM_MURAM


typedef struct
{
    t_Handle    h_Mem;
    uintptr_t   baseAddr;
    uint32_t    size;
} t_FmMuram;


void FmMuramClear(t_Handle h_FmMuram)
{
    t_FmMuram   *p_FmMuram = ( t_FmMuram *)h_FmMuram;

    SANITY_CHECK_RETURN(h_FmMuram, E_INVALID_HANDLE);
    IOMemSet32(UINT_TO_PTR(p_FmMuram->baseAddr), 0, p_FmMuram->size);
}


t_Handle FM_MURAM_ConfigAndInit(uintptr_t baseAddress, uint32_t size)
{
    t_Handle    h_Mem;
    t_FmMuram   *p_FmMuram;

    if (!baseAddress)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("baseAddress 0 is not supported"));
        return NULL;
    }

    if (baseAddress%4)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("baseAddress not 4 bytes aligned!"));
        return NULL;
    }

    /* Allocate FM MURAM structure */
    p_FmMuram = (t_FmMuram *) XX_Malloc(sizeof(t_FmMuram));
    if (!p_FmMuram)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MURAM driver structure"));
        return NULL;
    }
    memset(p_FmMuram, 0, sizeof(t_FmMuram));


    if ((MM_Init(&h_Mem, baseAddress, size) != E_OK) || (!h_Mem))
    {
        XX_Free(p_FmMuram);
        REPORT_ERROR(MAJOR, E_INVALID_HANDLE, ("FM-MURAM partition!!!"));
        return NULL;
    }

    /* Initialize FM MURAM parameters which will be kept by the driver */
    p_FmMuram->baseAddr = baseAddress;
    p_FmMuram->size = size;
    p_FmMuram->h_Mem = h_Mem;

    return p_FmMuram;
}

t_Error FM_MURAM_Free(t_Handle h_FmMuram)
{
    t_FmMuram   *p_FmMuram = ( t_FmMuram *)h_FmMuram;

    if (p_FmMuram->h_Mem)
        MM_Free(p_FmMuram->h_Mem);

    XX_Free(h_FmMuram);

    return E_OK;
}

void  * FM_MURAM_AllocMem(t_Handle h_FmMuram, uint32_t size, uint32_t align)
{
    t_FmMuram   *p_FmMuram = ( t_FmMuram *)h_FmMuram;
    uintptr_t   addr;

    SANITY_CHECK_RETURN_VALUE(h_FmMuram, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_FmMuram->h_Mem, E_INVALID_HANDLE, NULL);

    addr = (uintptr_t)MM_Get(p_FmMuram->h_Mem, size, align ,"FM MURAM");

    if (addr == ILLEGAL_BASE)
        return NULL;

    return UINT_TO_PTR(addr);
}

void  * FM_MURAM_AllocMemForce(t_Handle h_FmMuram, uint64_t base, uint32_t size)
{
    t_FmMuram   *p_FmMuram = ( t_FmMuram *)h_FmMuram;
    uintptr_t   addr;

    SANITY_CHECK_RETURN_VALUE(h_FmMuram, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_FmMuram->h_Mem, E_INVALID_HANDLE, NULL);

    addr = (uintptr_t)MM_GetForce(p_FmMuram->h_Mem, base, size, "FM MURAM");

    if (addr == ILLEGAL_BASE)
        return NULL;

    return UINT_TO_PTR(addr);
}

t_Error FM_MURAM_FreeMem(t_Handle h_FmMuram, void *ptr)
{
    t_FmMuram   *p_FmMuram = ( t_FmMuram *)h_FmMuram;

    SANITY_CHECK_RETURN_ERROR(h_FmMuram, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMuram->h_Mem, E_INVALID_HANDLE);

    if (MM_Put(p_FmMuram->h_Mem, PTR_TO_UINT(ptr)) == 0)
        RETURN_ERROR(MINOR, E_INVALID_ADDRESS, ("memory pointer!!!"));

    return E_OK;
}

uint64_t FM_MURAM_GetFreeMemSize(t_Handle h_FmMuram)
{
    t_FmMuram   *p_FmMuram = ( t_FmMuram *)h_FmMuram;

    SANITY_CHECK_RETURN_VALUE(h_FmMuram, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(p_FmMuram->h_Mem, E_INVALID_HANDLE, 0);

    return MM_GetFreeMemSize(p_FmMuram->h_Mem);
}
