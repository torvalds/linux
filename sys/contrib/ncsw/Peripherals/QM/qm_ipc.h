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
 @File          QM_ipc.h

 @Description   QM Inter-Partition prototypes, structures and definitions.
*//***************************************************************************/
#ifndef __QM_IPC_H
#define __QM_IPC_H

#include "error_ext.h"
#include "std_ext.h"


/**************************************************************************//**
 @Group         QM_grp Frame Manager API

 @Description   QM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         QM_IPC_grp Qm Inter-Partition messaging Unit

 @Description   QM Inter-Partition messaging unit API definitions and enums.

 @{
*//***************************************************************************/

#define QM_FORCE_FQID               1
#define QM_PUT_FQID                 2
#define QM_GET_COUNTER              3
#define QM_GET_SET_PORTAL_PARAMS    4
#define QM_GET_REVISION             5
#define QM_MASTER_IS_ALIVE          6

#define QM_IPC_MAX_REPLY_BODY_SIZE  16
#define QM_IPC_MAX_REPLY_SIZE       (QM_IPC_MAX_REPLY_BODY_SIZE + sizeof(uint32_t))
#define QM_IPC_MAX_MSG_SIZE         30

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */
#define MEM_MAP_START

typedef _Packed struct t_QmIpcMsg
{
    uint32_t    msgId;
    uint8_t     msgBody[QM_IPC_MAX_MSG_SIZE];
} _PackedType   t_QmIpcMsg;

typedef _Packed struct t_QmIpcReply
{
    uint32_t    error;
    uint8_t     replyBody[QM_IPC_MAX_REPLY_BODY_SIZE];
} _PackedType t_QmIpcReply;

typedef _Packed struct t_QmIpcGetCounter
{
    uint32_t        enumId;     /**< IN */
} _PackedType t_QmIpcGetCounter;

typedef _Packed struct t_QmIpcFqidParams
{
    uint32_t         fqid;       /**< IN */
    uint32_t         size;       /**< IN */
} _PackedType t_QmIpcFqidParams;

typedef _Packed struct t_QmIpcPortalInitParams {
    uint8_t             portalId;       /**< IN */
    uint8_t             stashDestQueue; /**< IN */
    uint16_t            liodn;          /**< IN */
    uint16_t            dqrrLiodn;      /**< IN */
    uint16_t            fdFqLiodn;      /**< IN */
} _PackedType t_QmIpcPortalInitParams;

typedef _Packed struct t_QmIpcRevisionInfo {
    uint8_t         majorRev;               /**< OUT: Major revision */
    uint8_t         minorRev;               /**< OUT: Minor revision */
} _PackedType t_QmIpcRevisionInfo;

#define MEM_MAP_END
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */

/** @} */ /* end of QM_IPC_grp group */
/** @} */ /* end of QM_grp group */


#endif /* __QM_IPC_H */
