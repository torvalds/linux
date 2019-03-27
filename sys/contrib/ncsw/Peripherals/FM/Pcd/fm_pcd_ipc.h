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
 @File          fm_pcd_ipc.h

 @Description   FM PCD Inter-Partition prototypes, structures and definitions.
*//***************************************************************************/
#ifndef __FM_PCD_IPC_H
#define __FM_PCD_IPC_H

#include "std_ext.h"


/**************************************************************************//**
 @Group         FM_grp Frame Manager API

 @Description   FM API functions, definitions and enums

 @{
*//***************************************************************************/


#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

/**************************************************************************//**
 @Description   Structure for getting a sw parser address according to a label
                Fields commented 'IN' are passed by the port module to be used
                by the FM module.
                Fields commented 'OUT' will be filled by FM before returning to port.
*//***************************************************************************/
typedef _Packed struct t_FmPcdIpcSwPrsLable
{
    uint32_t    enumHdr;                        /**< IN. The existence of this header will invoke
                                                     the sw parser code. */
    uint8_t     indexPerHdr;                    /**< IN. Normally 0, if more than one sw parser
                                                     attachments for the same header, use this

                                                   index to distinguish between them. */
} _PackedType t_FmPcdIpcSwPrsLable;

/**************************************************************************//**
 @Description   Structure for port-PCD communication.
                Fields commented 'IN' are passed by the port module to be used
                by the FM module.
                Fields commented 'OUT' will be filled by FM before returning to port.
                Some fields are optional (depending on configuration) and
                will be analized by the port and FM modules accordingly.
*//***************************************************************************/

typedef  struct t_FmPcdIpcKgSchemesParams
{
    uint8_t     guestId;
    uint8_t     numOfSchemes;
    uint8_t     schemesIds[FM_PCD_KG_NUM_OF_SCHEMES];
} _PackedType t_FmPcdIpcKgSchemesParams;

typedef  struct t_FmPcdIpcKgClsPlanParams
{
    uint8_t     guestId;
    uint16_t    numOfClsPlanEntries;
    uint8_t     clsPlanBase;
} _PackedType t_FmPcdIpcKgClsPlanParams;

typedef _Packed struct t_FmPcdIpcPrsIncludePort
{
    uint8_t     hardwarePortId;
    bool        include;
} _PackedType t_FmPcdIpcPrsIncludePort;


#define FM_PCD_MAX_REPLY_SIZE           16
#define FM_PCD_MAX_MSG_SIZE             36
#define FM_PCD_MAX_REPLY_BODY_SIZE      36

typedef _Packed struct {
    uint32_t    msgId;
    uint8_t     msgBody[FM_PCD_MAX_MSG_SIZE];
} _PackedType t_FmPcdIpcMsg;

typedef _Packed struct t_FmPcdIpcReply {
    uint32_t    error;
    uint8_t     replyBody[FM_PCD_MAX_REPLY_BODY_SIZE];
} _PackedType t_FmPcdIpcReply;

typedef _Packed struct t_FmIpcResourceAllocParams {
    uint8_t     guestId;
    uint16_t    base;
    uint16_t    num;
}_PackedType t_FmIpcResourceAllocParams;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */



/**************************************************************************//**
 @Function      FM_PCD_ALLOC_KG_SCHEMES

 @Description   Used by FM PCD front-end in order to allocate KG resources

 @Param[in/out] t_FmPcdIpcKgAllocParams Pointer
*//***************************************************************************/
#define FM_PCD_ALLOC_KG_SCHEMES                 3

/**************************************************************************//**
 @Function      FM_PCD_FREE_KG_SCHEMES

 @Description   Used by FM PCD front-end in order to Free KG resources

 @Param[in/out] t_FmPcdIpcKgSchemesParams Pointer
*//***************************************************************************/
#define FM_PCD_FREE_KG_SCHEMES                  4

/**************************************************************************//**
 @Function      FM_PCD_ALLOC_PROFILES

 @Description   Used by FM PCD front-end in order to allocate Policer profiles

 @Param[in/out] t_FmIpcResourceAllocParams Pointer
*//***************************************************************************/
#define FM_PCD_ALLOC_PROFILES                   5

/**************************************************************************//**
 @Function      FM_PCD_FREE_PROFILES

 @Description   Used by FM PCD front-end in order to Free Policer profiles

 @Param[in/out] t_FmIpcResourceAllocParams Pointer
*//***************************************************************************/
#define FM_PCD_FREE_PROFILES                    6

/**************************************************************************//**
 @Function      FM_PCD_SET_PORT_PROFILES

 @Description   Used by FM PCD front-end in order to allocate Policer profiles
                for specific port

 @Param[in/out] t_FmIpcResourceAllocParams Pointer
*//***************************************************************************/
#define FM_PCD_SET_PORT_PROFILES                7

/**************************************************************************//**
 @Function      FM_PCD_CLEAR_PORT_PROFILES

 @Description   Used by FM PCD front-end in order to allocate Policer profiles
                for specific port

 @Param[in/out] t_FmIpcResourceAllocParams Pointer
*//***************************************************************************/
#define FM_PCD_CLEAR_PORT_PROFILES              8

/**************************************************************************//**
 @Function      FM_PCD_GET_PHYS_MURAM_BASE

 @Description   Used by FM PCD front-end in order to get MURAM base address

 @Param[in/out] t_FmPcdIcPhysAddr Pointer
*//***************************************************************************/
#define FM_PCD_GET_PHYS_MURAM_BASE              9

/**************************************************************************//**
 @Function      FM_PCD_GET_SW_PRS_OFFSET

 @Description   Used by FM front-end to get the SW parser offset of the start of
                code relevant to a given label.

 @Param[in/out] t_FmPcdIpcSwPrsLable Pointer
*//***************************************************************************/
#define FM_PCD_GET_SW_PRS_OFFSET                10

/**************************************************************************//**
 @Function      FM_PCD_MASTER_IS_ENABLED

 @Description   Used by FM front-end in order to verify
                PCD enablement.

 @Param[in]     bool Pointer
*//***************************************************************************/
#define FM_PCD_MASTER_IS_ENABLED                15

/**************************************************************************//**
 @Function      FM_PCD_GUEST_DISABLE

 @Description   Used by FM front-end to inform back-end when
                front-end PCD is disabled

 @Param[in]     None
*//***************************************************************************/
#define FM_PCD_GUEST_DISABLE                    16

/**************************************************************************//**
 @Function      FM_PCD_FREE_KG_CLSPLAN

 @Description   Used by FM PCD front-end in order to Free KG classification plan entries

 @Param[in/out] t_FmPcdIpcKgClsPlanParams Pointer
*//***************************************************************************/
#define FM_PCD_FREE_KG_CLSPLAN                  22

/**************************************************************************//**
 @Function      FM_PCD_ALLOC_KG_CLSPLAN

 @Description   Used by FM PCD front-end in order to allocate KG classification plan entries

 @Param[in/out] t_FmPcdIpcKgClsPlanParams Pointer
*//***************************************************************************/
#define FM_PCD_ALLOC_KG_CLSPLAN                 23

/**************************************************************************//**
 @Function      FM_PCD_MASTER_IS_ALIVE

 @Description   Used by FM front-end to check that back-end exists

 @Param[in]     None
*//***************************************************************************/
#define FM_PCD_MASTER_IS_ALIVE                  24

/**************************************************************************//**
 @Function      FM_PCD_GET_COUNTER

 @Description   Used by FM front-end to read PCD counters

 @Param[in/out] t_FmPcdIpcGetCounter Pointer
*//***************************************************************************/
#define FM_PCD_GET_COUNTER                      25

/**************************************************************************//**
 @Function      FM_PCD_PRS_INC_PORT_STATS

 @Description   Used by FM front-end to set/clear statistics for port

 @Param[in/out] t_FmPcdIpcPrsIncludePort Pointer
*//***************************************************************************/
#define FM_PCD_PRS_INC_PORT_STATS               26

#if (DPAA_VERSION >= 11)
/* TODO - doc */
#define FM_PCD_ALLOC_SP                         27
#endif /* (DPAA_VERSION >= 11) */


/** @} */ /* end of FM_PCD_IPC_grp group */
/** @} */ /* end of FM_grp group */


#endif /* __FM_PCD_IPC_H */
