/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *

 **************************************************************************/
/**************************************************************************//**
 @File          bm_ipc.h

 @Description   BM Inter-Partition prototypes, structures and definitions.
*//***************************************************************************/
#ifndef __BM_IPC_H
#define __BM_IPC_H

#include "error_ext.h"
#include "std_ext.h"


/**************************************************************************//**
 @Group         BM_grp Frame Manager API

 @Description   BM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         BM_IPC_grp BM Inter-Partition messaging Unit

 @Description   BM Inter-Partition messaging unit API definitions and enums.

 @{
*//***************************************************************************/

#define BM_SET_POOL_THRESH          1
#define BM_UNSET_POOL_THRESH        2
#define BM_GET_COUNTER              3
#define BM_GET_REVISION             4
#define BM_FORCE_BPID               5
#define BM_PUT_BPID                 6
#define BM_MASTER_IS_ALIVE          7

#define BM_IPC_MAX_REPLY_BODY_SIZE  16
#define BM_IPC_MAX_REPLY_SIZE       (BM_IPC_MAX_REPLY_BODY_SIZE + sizeof(uint32_t))
#define BM_IPC_MAX_MSG_SIZE         30


#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */
#define MEM_MAP_START

typedef _Packed struct t_BmIpcMsg
{
    uint32_t    msgId;
    uint8_t     msgBody[BM_IPC_MAX_MSG_SIZE];
} _PackedType t_BmIpcMsg;

typedef _Packed struct t_BmIpcReply
{
    uint32_t    error;
    uint8_t     replyBody[BM_IPC_MAX_REPLY_BODY_SIZE];
} _PackedType t_BmIpcReply;

typedef _Packed struct t_BmIpcPoolThreshParams
{
    uint8_t     bpid;                                   /**< IN */
    uint32_t    thresholds[MAX_DEPLETION_THRESHOLDS];   /**< IN */
} _PackedType t_BmIpcPoolThreshParams;

typedef _Packed struct t_BmIpcGetCounter
{
    uint8_t         bpid;       /**< IN */
    uint32_t        enumId;     /**< IN */
} _PackedType t_BmIpcGetCounter;

typedef _Packed struct t_BmIpcBpidParams
{
    uint8_t         bpid;       /**< IN */
} _PackedType t_BmIpcBpidParams;

typedef _Packed struct t_BmIpcRevisionInfo {
    uint8_t         majorRev;               /**< Major revision */
    uint8_t         minorRev;               /**< Minor revision */
} _PackedType t_BmIpcRevisionInfo;

#define MEM_MAP_END
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */

/** @} */ /* end of BM_IPC_grp group */
/** @} */ /* end of BM_grp group */


#endif /* __BM_IPC_H */
