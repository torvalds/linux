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


/**************************************************************************//**
 @File          fm_muram_ext.h

 @Description   FM MURAM Application Programming Interface.
*//***************************************************************************/
#ifndef __FM_MURAM_EXT
#define __FM_MURAM_EXT

#include "error_ext.h"
#include "std_ext.h"


/**************************************************************************//**

 @Group         FM_grp Frame Manager API

 @Description   FM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         FM_muram_grp FM MURAM

 @Description   FM MURAM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         FM_muram_init_grp FM MURAM Initialization Unit

 @Description   FM MURAM initialization API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MURAM_ConfigAndInit

 @Description   Creates partition in the MURAM.

                The routine returns a handle (descriptor) to the MURAM partition.
                This descriptor must be passed as first parameter to all other
                FM-MURAM function calls.

                No actual initialization or configuration of FM_MURAM hardware is
                done by this routine.

 @Param[in]     baseAddress - Pointer to base of memory mapped FM-MURAM.
 @Param[in]     size        - Size of the FM-MURAM partition.

 @Return        Handle to FM-MURAM object, or NULL for Failure.
*//***************************************************************************/
t_Handle FM_MURAM_ConfigAndInit(uintptr_t baseAddress, uint32_t size);

/**************************************************************************//**
 @Function      FM_MURAM_Free

 @Description   Frees all resources that were assigned to FM-MURAM module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_FmMuram - FM-MURAM module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error  FM_MURAM_Free(t_Handle h_FmMuram);

/** @} */ /* end of FM_muram_init_grp group */


/**************************************************************************//**
 @Group         FM_muram_ctrl_grp FM MURAM Control Unit

 @Description   FM MURAM control API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MURAM_AllocMem

 @Description   Allocate some memory from FM-MURAM partition.

 @Param[in]     h_FmMuram - FM-MURAM module descriptor.
 @Param[in]     size      - size of the memory to be allocated.
 @Param[in]     align     - Alignment of the memory.

 @Return        address of the allocated memory; NULL otherwise.
*//***************************************************************************/
void  * FM_MURAM_AllocMem(t_Handle h_FmMuram, uint32_t size, uint32_t align);

/**************************************************************************//**
 @Function      FM_MURAM_AllocMemForce

 @Description   Allocate some specific memory from FM-MURAM partition (according
                to base).

 @Param[in]     h_FmMuram - FM-MURAM module descriptor.
 @Param[in]     base      - the desired base-address to be allocated.
 @Param[in]     size      - size of the memory to be allocated.

 @Return        address of the allocated memory; NULL otherwise.
*//***************************************************************************/
void  * FM_MURAM_AllocMemForce(t_Handle h_FmMuram, uint64_t base, uint32_t size);

/**************************************************************************//**
 @Function      FM_MURAM_FreeMem

 @Description   Free an allocated memory from FM-MURAM partition.

 @Param[in]     h_FmMuram - FM-MURAM module descriptor.
 @Param[in]     ptr       - A pointer to an allocated memory.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MURAM_FreeMem(t_Handle h_FmMuram, void *ptr);

/**************************************************************************//**
 @Function      FM_MURAM_GetFreeMemSize

 @Description   Returns the size (in bytes) of free MURAM memory.

 @Param[in]     h_FmMuram - FM-MURAM module descriptor.

 @Return        Free MURAM memory size in bytes.
*//***************************************************************************/
uint64_t FM_MURAM_GetFreeMemSize(t_Handle h_FmMuram);

/** @} */ /* end of FM_muram_ctrl_grp group */
/** @} */ /* end of FM_muram_grp group */
/** @} */ /* end of FM_grp group */



#endif /* __FM_MURAM_EXT */
