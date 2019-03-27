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
 @File          core_ext.h

 @Description   Generic interface to basic core operations.

                The system integrator must ensure that this interface is
                mapped to a specific core implementation, by including the
                appropriate header file.
*//***************************************************************************/
#ifndef __CORE_EXT_H
#define __CORE_EXT_H

#ifdef CONFIG_FMAN_ARM
#include "arm_ext.h"
#include <linux/smp.h>
#else
#ifdef NCSW_PPC_CORE
#include "ppc_ext.h"
#elif defined(NCSW_VXWORKS)
#include "core_vxw_ext.h"
#else
#error "Core is not defined!"
#endif /* NCSW_CORE */

#if (!defined(CORE_IS_LITTLE_ENDIAN) && !defined(CORE_IS_BIG_ENDIAN))
#error "Must define core as little-endian or big-endian!"
#endif /* (!defined(CORE_IS_LITTLE_ENDIAN) && ... */

#ifndef CORE_CACHELINE_SIZE
#error "Must define the core cache-line size!"
#endif /* !CORE_CACHELINE_SIZE */

#endif /* CONFIG_FMAN_ARM */


/**************************************************************************//**
 @Function      CORE_GetId

 @Description   Returns the core ID in the system.

 @Return        Core ID.
*//***************************************************************************/
uint32_t CORE_GetId(void);

/**************************************************************************//**
 @Function      CORE_MemoryBarrier

 @Description   This routine will cause the core to stop executing any commands
                until all previous memory read/write commands are completely out
                of the core's pipeline.

 @Return        None.
*//***************************************************************************/
void CORE_MemoryBarrier(void);
#define fsl_mem_core_barrier() CORE_MemoryBarrier()

#endif /* __CORE_EXT_H */
