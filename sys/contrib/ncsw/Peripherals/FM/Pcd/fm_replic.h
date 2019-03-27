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
 @File          fm_replic.h

 @Description   FM frame replicator
*//***************************************************************************/
#ifndef __FM_REPLIC_H
#define __FM_REPLIC_H

#include "std_ext.h"
#include "error_ext.h"


#define FRM_REPLIC_SOURCE_TD_OPCODE           0x75
#define NEXT_FRM_REPLIC_ADDR_SHIFT            4
#define NEXT_FRM_REPLIC_MEMBER_INDEX_SHIFT    16
#define FRM_REPLIC_FR_BIT                     0x08000000
#define FRM_REPLIC_NL_BIT                     0x10000000
#define FRM_REPLIC_INVALID_MEMBER_INDEX       0xffff
#define FRM_REPLIC_FIRST_MEMBER_INDEX         0

#define FRM_REPLIC_MIDDLE_MEMBER_INDEX        1
#define FRM_REPLIC_LAST_MEMBER_INDEX          2

#define SOURCE_TD_ITSELF_OPTION               0x01
#define SOURCE_TD_COPY_OPTION                 0x02
#define SOURCE_TD_ITSELF_AND_COPY_OPTION      SOURCE_TD_ITSELF_OPTION | SOURCE_TD_COPY_OPTION
#define SOURCE_TD_NONE                        0x04

/*typedef enum e_SourceTdOption
{
    e_SOURCE_TD_NONE = 0,
    e_SOURCE_TD_ITSELF_OPTION = 1,
    e_SOURCE_TD_COPY_OPTION = 2,
    e_SOURCE_TD_ITSELF_AND_COPY_OPTION = e_SOURCE_TD_ITSELF_OPTION | e_SOURCE_TD_COPY_OPTION
} e_SourceTdOption;
*/

typedef struct
{
    volatile uint32_t type;
    volatile uint32_t frGroupPointer;
    volatile uint32_t operationCode;
    volatile uint32_t reserved;
} t_FrmReplicGroupSourceAd;

typedef struct t_FmPcdFrmReplicMember
{
    void                        *p_MemberAd;    /**< pointer to the member AD */
    void                        *p_StatisticsAd;/**< pointer to the statistics AD of the member */
    t_Handle                    h_Manip;        /**< manip handle - need for free routines */
    t_List                      node;
} t_FmPcdFrmReplicMember;

typedef struct t_FmPcdFrmReplicGroup
{
    t_Handle                    h_FmPcd;

    uint8_t                     maxNumOfEntries;/**< maximal number of members in the group */
    uint8_t                     numOfEntries;   /**< actual number of members in the group */
    uint16_t                    owners;         /**< how many keys share this frame replicator group */
    void                        *p_SourceTd;     /**< pointer to the frame replicator source table descriptor */
    t_List                      membersList;    /**< the members list - should reflect the order of the members as in the hw linked list*/
    t_List                      availableMembersList;/**< list of all the available members in the group */
    t_FmPcdLock                 *p_Lock;
} t_FmPcdFrmReplicGroup;


#endif /* __FM_REPLIC_H */
