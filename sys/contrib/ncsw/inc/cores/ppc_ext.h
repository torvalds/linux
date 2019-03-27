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
 @File          ppc_ext.h

 @Description   Core API for PowerPC cores

                These routines must be implemented by each specific PowerPC
                core driver.
*//***************************************************************************/
#ifndef __PPC_EXT_H
#define __PPC_EXT_H

#include "part_ext.h"


#define CORE_IS_BIG_ENDIAN

#if defined(CORE_E300) || defined(CORE_E500V2)
#define CORE_CACHELINE_SIZE     32
#elif defined(CORE_E500MC) || defined(CORE_E5500) || defined(CORE_E6500)
#define CORE_CACHELINE_SIZE     64
#else
#error "Core not defined!"
#endif /* defined(CORE_E300) || ... */


/**************************************************************************//**
 @Function      CORE_TestAndSet

 @Description   This routine tries to atomically test-and-set an integer
                in memory to a non-zero value.

                The memory will be set only if it is tested as zero, in which
                case the routine returns the new non-zero value; otherwise the
                routine returns zero.

 @Param[in]     p - pointer to a volatile int in memory, on which test-and-set
                    operation should be made.

 @Retval        Zero        - Operation failed - memory was already set.
 @Retval        Non-zero    - Operation succeeded - memory has been set.
*//***************************************************************************/
int CORE_TestAndSet(volatile int *p);

/**************************************************************************//**
 @Function      CORE_InstructionSync

 @Description   This routine will cause the core to wait for previous instructions
                (including any interrupts they generate) to complete before the
                synchronization command executes, which purges all instructions
                from the processor's pipeline and refetches the next instruction.

 @Return        None.
*//***************************************************************************/
void CORE_InstructionSync(void);

/**************************************************************************//**
 @Function      CORE_DCacheEnable

 @Description   Enables the data cache for memory pages that are
                not cache inhibited.

 @Return        None.
*//***************************************************************************/
void CORE_DCacheEnable(void);

/**************************************************************************//**
 @Function      CORE_ICacheEnable

 @Description   Enables the instruction cache for memory pages that are
                not cache inhibited.

 @Return        None.
*//***************************************************************************/
void CORE_ICacheEnable(void);

/**************************************************************************//**
 @Function      CORE_DCacheDisable

 @Description   Disables the data cache.

 @Return        None.
*//***************************************************************************/
void CORE_DCacheDisable(void);

/**************************************************************************//**
 @Function      CORE_ICacheDisable

 @Description   Disables the instruction cache.

 @Return        None.
*//***************************************************************************/
void CORE_ICacheDisable(void);



#include "e500v2_ext.h"

#endif /* __PPC_EXT_H */
