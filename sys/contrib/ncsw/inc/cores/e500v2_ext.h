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
 @File          e500v2_ext.h

 @Description   E500 external definitions prototypes
                This file is not included by the E500
                source file as it is an assembly file. It is used
                only for prototypes exposure, for inclusion
                by user and other modules.
*//***************************************************************************/

#ifndef __E500V2_EXT_H
#define __E500V2_EXT_H

#include "std_ext.h"


/* Layer 1 Cache Manipulations
 *==============================
 * Should not be called directly by the user.
 */
void        L1DCache_Invalidate (void);
void        L1ICache_Invalidate(void);
void        L1DCache_Enable(void);
void        L1ICache_Enable(void);
void        L1DCache_Disable(void);
void        L1ICache_Disable(void);
void        L1DCache_Flush(void);
void        L1ICache_Flush(void);
uint32_t    L1ICache_IsEnabled(void);
uint32_t    L1DCache_IsEnabled(void);
/*
 *
 */
uint32_t    L1DCache_LineLock(uint32_t addr);
uint32_t    L1ICache_LineLock(uint32_t addr);
void        L1Cache_BroadCastEnable(void);
void        L1Cache_BroadCastDisable(void);


#define CORE_DCacheEnable       E500_DCacheEnable
#define CORE_ICacheEnable       E500_ICacheEnable
#define CORE_DCacheDisable      E500_DCacheDisable
#define CORE_ICacheDisable      E500_ICacheDisable
#define CORE_GetId              E500_GetId
#define CORE_TestAndSet         E500_TestAndSet
#define CORE_MemoryBarrier      E500_MemoryBarrier
#define CORE_InstructionSync    E500_InstructionSync

#define CORE_SetDozeMode        E500_SetDozeMode
#define CORE_SetNapMode         E500_SetNapMode
#define CORE_SetSleepMode       E500_SetSleepMode
#define CORE_SetJogMode         E500_SetJogMode
#define CORE_SetDeepSleepMode   E500_SetDeepSleepMode

#define CORE_RecoverDozeMode    E500_RecoverDozeMode
#define CORE_RecoverNapMode     E500_RecoverNapMode
#define CORE_RecoverSleepMode   E500_RecoverSleepMode
#define CORE_RecoverJogMode     E500_RecoverJogMode

void E500_SetDozeMode(void);
void E500_SetNapMode(void);
void E500_SetSleepMode(void);
void E500_SetJogMode(void);
t_Error E500_SetDeepSleepMode(uint32_t bptrAddress);

void E500_RecoverDozeMode(void);
void E500_RecoverNapMode(void);
void E500_RecoverSleepMode(void);
void E500_RecoverJogMode(void);


/**************************************************************************//**
 @Group         E500_id E500 Application Programming Interface

 @Description   E500 API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         E500_init_grp E500 Initialization Unit

 @Description   E500 initialization unit API functions, definitions and enums

 @{
*//***************************************************************************/


/**************************************************************************//**
 @Function      E500_DCacheEnable

 @Description   Enables the data cache for memory pages that are
                not cache inhibited.

 @Return        None.
*//***************************************************************************/
void E500_DCacheEnable(void);

/**************************************************************************//**
 @Function      E500_ICacheEnable

 @Description   Enables the instruction cache for memory pages that are
                not cache inhibited.

 @Return        None.
*//***************************************************************************/
void E500_ICacheEnable(void);

/**************************************************************************//**
 @Function      E500_DCacheDisable

 @Description   Disables the data cache.

 @Return        None.
*//***************************************************************************/
void E500_DCacheDisable(void);

/**************************************************************************//**
 @Function      E500_ICacheDisable

 @Description   Disables the instruction cache.

 @Return        None.
*//***************************************************************************/
void E500_ICacheDisable(void);

/**************************************************************************//**
 @Function      E500_DCacheFlush

 @Description   Flushes the data cache

 @Return        None.
*//***************************************************************************/
void E500_DCacheFlush(void);

/**************************************************************************//**
 @Function      E500_ICacheFlush

 @Description   Flushes the instruction cache.

 @Return        None.
*//***************************************************************************/
void E500_ICacheFlush(void);

/**************************************************************************//**
 @Function      E500_DCacheSetStashId

 @Description   Set Stash Id for data cache

 @Param[in]     stashId     the stash id to be set.

 @Return        None.
*//***************************************************************************/
void E500_DCacheSetStashId(uint8_t stashId);

/**************************************************************************//**
 @Description   E500mc L2 Cache Operation Mode
*//***************************************************************************/
typedef enum e_E500mcL2CacheMode
{
    e_L2_CACHE_MODE_DATA_ONLY      = 0x00000001,   /**< Cache data only */
    e_L2_CACHE_MODE_INST_ONLY      = 0x00000002,   /**< Cache instructions only */
    e_L2_CACHE_MODE_DATA_AND_INST  = 0x00000003    /**< Cache data and instructions */
} e_E500mcL2CacheMode;

#if defined(CORE_E500MC) || defined(CORE_E5500)
/**************************************************************************//**
 @Function      E500_L2CacheEnable

 @Description   Enables the cache for memory pages that are not cache inhibited.

 @param[in]     mode - L2 cache mode: data only, instruction only or instruction and data.

 @Return        None.

 @Cautions      This routine must be call only ONCE for both caches. I.e. it is
                not possible to call this routine for i-cache and than to call
                again for d-cache; The second call will override the first one.
*//***************************************************************************/
void E500_L2CacheEnable(e_E500mcL2CacheMode mode);

/**************************************************************************//**
 @Function      E500_L2CacheDisable

 @Description   Disables the cache (data instruction or both).

 @Return        None.

*//***************************************************************************/
void E500_L2CacheDisable(void);

/**************************************************************************//**
 @Function      E500_L2CacheFlush

 @Description   Flushes the cache.

 @Return        None.
*//***************************************************************************/
void E500_L2CacheFlush(void);

/**************************************************************************//**
 @Function      E500_L2SetStashId

 @Description   Set Stash Id

 @Param[in]     stashId     the stash id to be set.

 @Return        None.
*//***************************************************************************/
void E500_L2SetStashId(uint8_t stashId);
#endif /* defined(CORE_E500MC) || defined(CORE_E5500) */

#ifdef CORE_E6500
/**************************************************************************//**
 @Function      E6500_L2CacheEnable

 @Description   Enables the cache for memory pages that are not cache inhibited.

 @param[in]     mode - L2 cache mode: support data & instruction only.

 @Return        None.

 @Cautions      This routine must be call only ONCE for both caches. I.e. it is
                not possible to call this routine for i-cache and than to call
                again for d-cache; The second call will override the first one.
*//***************************************************************************/
void E6500_L2CacheEnable(uintptr_t clusterBase);

/**************************************************************************//**
 @Function      E6500_L2CacheDisable

 @Description   Disables the cache (data instruction or both).

 @Return        None.

*//***************************************************************************/
void E6500_L2CacheDisable(uintptr_t clusterBase);

/**************************************************************************//**
 @Function      E6500_L2CacheFlush

 @Description   Flushes the cache.

 @Return        None.
*//***************************************************************************/
void E6500_L2CacheFlush(uintptr_t clusterBase);

/**************************************************************************//**
 @Function      E6500_L2SetStashId

 @Description   Set Stash Id

 @Param[in]     stashId     the stash id to be set.

 @Return        None.
*//***************************************************************************/
void E6500_L2SetStashId(uintptr_t clusterBase, uint8_t stashId);

/**************************************************************************//**
 @Function      E6500_GetCcsrBase

 @Description   Obtain SoC CCSR base address

 @Param[in]     None.

 @Return        Physical CCSR base address.
*//***************************************************************************/
physAddress_t E6500_GetCcsrBase(void);
#endif /* CORE_E6500 */

/**************************************************************************//**
 @Function      E500_AddressBusStreamingEnable

 @Description   Enables address bus streaming on the CCB.

                This setting, along with the ECM streaming configuration
                parameters, enables address bus streaming on the CCB.

 @Return        None.
*//***************************************************************************/
void E500_AddressBusStreamingEnable(void);

/**************************************************************************//**
 @Function      E500_AddressBusStreamingDisable

 @Description   Disables address bus streaming on the CCB.

 @Return        None.
*//***************************************************************************/
void E500_AddressBusStreamingDisable(void);

/**************************************************************************//**
 @Function      E500_AddressBroadcastEnable

 @Description   Enables address broadcast.

                The e500 broadcasts cache management instructions (dcbst, dcblc
                (CT = 1), icblc (CT = 1), dcbf, dcbi, mbar, msync, tlbsync, icbi)
                based on ABE. ABE must be set to allow management of external
                L2 caches.

 @Return        None.
*//***************************************************************************/
void E500_AddressBroadcastEnable(void);

/**************************************************************************//**
 @Function      E500_AddressBroadcastDisable

 @Description   Disables address broadcast.

                The e500 broadcasts cache management instructions (dcbst, dcblc
                (CT = 1), icblc (CT = 1), dcbf, dcbi, mbar, msync, tlbsync, icbi)
                based on ABE. ABE must be set to allow management of external
                L2 caches.

 @Return        None.
*//***************************************************************************/
void E500_AddressBroadcastDisable(void);

/**************************************************************************//**
 @Function      E500_IsTaskletSupported

 @Description   Checks if tasklets are supported by the e500 interrupt handler.

 @Retval        TRUE    - Tasklets are supported.
 @Retval        FALSE   - Tasklets are not supported.
*//***************************************************************************/
bool E500_IsTaskletSupported(void);

void E500_EnableTimeBase(void);
void E500_DisableTimeBase(void);

uint64_t E500_GetTimeBaseTime(void);

void E500_GenericIntrInit(void);

t_Error E500_SetIntr(int        ppcIntrSrc,
                     void       (* Isr)(t_Handle handle),
                     t_Handle   handle);

t_Error E500_ClearIntr(int ppcIntrSrc);

/**************************************************************************//**
 @Function      E500_GenericIntrHandler

 @Description   This is the general e500 interrupt handler.

                It is called by the main assembly interrupt handler
                when an exception occurs and no other function has been
                assigned to this exception.

 @Param         intrEntry   - (In) The exception interrupt vector entry.
*//***************************************************************************/
void E500_GenericIntrHandler(uint32_t intrEntry);

/**************************************************************************//**
 @Function      CriticalIntr

 @Description   This is the specific critical e500 interrupt handler.

                It is called by the main assembly interrupt handler
                when an critical interrupt.

 @Param         intrEntry   - (In) The exception interrupt vector entry.
*//***************************************************************************/
void CriticalIntr(uint32_t intrEntry);


/**************************************************************************//**
 @Function      E500_GetId

 @Description   Returns the core ID in the system.

 @Return        Core ID.
*//***************************************************************************/
uint32_t E500_GetId(void);

/**************************************************************************//**
 @Function      E500_TestAndSet

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
int E500_TestAndSet(volatile int *p);

/**************************************************************************//**
 @Function      E500_MemoryBarrier

 @Description   This routine will cause the core to stop executing any commands
                until all previous memory read/write commands are completely out
                of the core's pipeline.

 @Return        None.
*//***************************************************************************/
static __inline__ void E500_MemoryBarrier(void)
{
#ifndef CORE_E500V2
    __asm__ ("mbar 1");
#else  /* CORE_E500V2 */
    /**** ERRATA WORK AROUND START ****/
    /* ERRATA num:  CPU1 */
    /* Description: "mbar MO = 1" instruction fails to order caching-inhibited
                    guarded loads and stores. */

    /* "msync" instruction is used instead */

    __asm__ ("msync");

    /**** ERRATA WORK AROUND END ****/
#endif /* CORE_E500V2 */
}

/**************************************************************************//**
 @Function      E500_InstructionSync

 @Description   This routine will cause the core to wait for previous instructions
                (including any interrupts they generate) to complete before the
                synchronization command executes, which purges all instructions
                from the processor's pipeline and refetches the next instruction.

 @Return        None.
*//***************************************************************************/
static __inline__ void E500_InstructionSync(void)
{
    __asm__ ("isync");
}


/** @} */ /* end of E500_init_grp group */
/** @} */ /* end of E500_grp group */


#endif /* __E500V2_EXT_H */
