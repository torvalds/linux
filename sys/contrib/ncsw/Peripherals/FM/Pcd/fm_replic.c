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
 @File          fm_replic.c

 @Description   FM frame replicator
*//***************************************************************************/
#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "debug_ext.h"
#include "fm_pcd_ext.h"
#include "fm_muram_ext.h"
#include "fm_common.h"
#include "fm_hc.h"
#include "fm_replic.h"
#include "fm_cc.h"
#include "list_ext.h"


/****************************************/
/*       static functions               */
/****************************************/
static uint8_t  GetMemberPosition(t_FmPcdFrmReplicGroup *p_ReplicGroup,
                                  uint32_t              memberIndex,
                                  bool                  isAddOperation)
{
    uint8_t     memberPosition;
    uint32_t    lastMemberIndex;

    ASSERT_COND(p_ReplicGroup);

    /* the last member index is different between add and remove operation -
    in case of remove - this is exactly the last member index
    in case of add - this is the last member index + 1 - e.g.
    if we have 4 members, the index of the actual last member is 3(because the
    index starts from 0) therefore in order to add a new member as the last
    member we shall use memberIndex = 4 and not 3
    */
    if (isAddOperation)
        lastMemberIndex = p_ReplicGroup->numOfEntries;
    else
        lastMemberIndex = p_ReplicGroup->numOfEntries-1;

    /* last */
    if (memberIndex == lastMemberIndex)
        memberPosition = FRM_REPLIC_LAST_MEMBER_INDEX;
    else
    {
        /* first */
        if (memberIndex == 0)
            memberPosition = FRM_REPLIC_FIRST_MEMBER_INDEX;
        else
        {
            /* middle */
            ASSERT_COND(memberIndex < lastMemberIndex);
            memberPosition = FRM_REPLIC_MIDDLE_MEMBER_INDEX;
        }
    }
    return memberPosition;
}

static t_Error MemberCheckParams(t_Handle                  h_FmPcd,
                                 t_FmPcdCcNextEngineParams *p_MemberParams)
{
    t_Error         err;


    if ((p_MemberParams->nextEngine != e_FM_PCD_DONE) &&
        (p_MemberParams->nextEngine != e_FM_PCD_KG)   &&
        (p_MemberParams->nextEngine != e_FM_PCD_PLCR))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Next engine of a member should be MatchTable(cc) or Done or Policer"));

    /* check the regular parameters of the next engine */
    err = ValidateNextEngineParams(h_FmPcd, p_MemberParams, e_FM_PCD_CC_STATS_MODE_NONE);
    if (err)
        RETURN_ERROR(MAJOR, err, ("member next engine parameters"));

    return E_OK;
}

static t_Error CheckParams(t_Handle                     h_FmPcd,
                           t_FmPcdFrmReplicGroupParams *p_ReplicGroupParam)
{
    int             i;
    t_Error         err;

    /* check that max num of entries is at least 2 */
    if (!IN_RANGE(2, p_ReplicGroupParam->maxNumOfEntries, FM_PCD_FRM_REPLIC_MAX_NUM_OF_ENTRIES))
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, ("maxNumOfEntries in the frame replicator parameters should be 2-%d",FM_PCD_FRM_REPLIC_MAX_NUM_OF_ENTRIES));

    /* check that number of entries is greater than zero */
    if (!p_ReplicGroupParam->numOfEntries)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("numOFEntries in the frame replicator group should be greater than zero"));

    /* check that max num of entries is equal or greater than number of entries */
    if (p_ReplicGroupParam->maxNumOfEntries < p_ReplicGroupParam->numOfEntries)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("maxNumOfEntries should be equal or greater than numOfEntries"));

    for (i=0; i<p_ReplicGroupParam->numOfEntries; i++)
    {
        err = MemberCheckParams(h_FmPcd, &p_ReplicGroupParam->nextEngineParams[i]);
        if (err)
            RETURN_ERROR(MAJOR, err, ("member check parameters"));
    }
    return E_OK;
}

static t_FmPcdFrmReplicMember *GetAvailableMember(t_FmPcdFrmReplicGroup *p_ReplicGroup)
{
    t_FmPcdFrmReplicMember  *p_ReplicMember = NULL;
    t_List                  *p_Next;

    if (!LIST_IsEmpty(&p_ReplicGroup->availableMembersList))
    {
        p_Next = LIST_FIRST(&p_ReplicGroup->availableMembersList);
        p_ReplicMember = LIST_OBJECT(p_Next, t_FmPcdFrmReplicMember, node);
        ASSERT_COND(p_ReplicMember);
        LIST_DelAndInit(p_Next);
    }
    return p_ReplicMember;
}

static void PutAvailableMember(t_FmPcdFrmReplicGroup    *p_ReplicGroup,
                               t_FmPcdFrmReplicMember   *p_ReplicMember)
{
    LIST_AddToTail(&p_ReplicMember->node, &p_ReplicGroup->availableMembersList);
}

static void AddMemberToList(t_FmPcdFrmReplicGroup   *p_ReplicGroup,
                            t_FmPcdFrmReplicMember  *p_CurrentMember,
                            t_List                  *p_ListHead)
{
    LIST_Add(&p_CurrentMember->node, p_ListHead);

    p_ReplicGroup->numOfEntries++;
}

static void RemoveMemberFromList(t_FmPcdFrmReplicGroup  *p_ReplicGroup,
                                 t_FmPcdFrmReplicMember *p_CurrentMember)
{
    ASSERT_COND(p_ReplicGroup->numOfEntries);
    LIST_DelAndInit(&p_CurrentMember->node);
    p_ReplicGroup->numOfEntries--;
}

static void LinkSourceToMember(t_FmPcdFrmReplicGroup    *p_ReplicGroup,
                               t_AdOfTypeContLookup     *p_SourceTd,
                               t_FmPcdFrmReplicMember   *p_ReplicMember)
{
    t_FmPcd             *p_FmPcd;

    ASSERT_COND(p_SourceTd);
    ASSERT_COND(p_ReplicMember);
    ASSERT_COND(p_ReplicGroup);
    ASSERT_COND(p_ReplicGroup->h_FmPcd);

    /* Link the first member in the group to the source TD */
    p_FmPcd = p_ReplicGroup->h_FmPcd;

    WRITE_UINT32(p_SourceTd->matchTblPtr,
        (uint32_t)(XX_VirtToPhys(p_ReplicMember->p_MemberAd) -
                        p_FmPcd->physicalMuramBase));
}

static void LinkMemberToMember(t_FmPcdFrmReplicGroup    *p_ReplicGroup,
                               t_FmPcdFrmReplicMember   *p_CurrentMember,
                               t_FmPcdFrmReplicMember   *p_NextMember)
{
    t_AdOfTypeResult    *p_CurrReplicAd = (t_AdOfTypeResult*)p_CurrentMember->p_MemberAd;
    t_AdOfTypeResult    *p_NextReplicAd = NULL;
    t_FmPcd             *p_FmPcd;
    uint32_t            offset = 0;

    /* Check if the next member exists or it's NULL (- means that this is the last member) */
    if (p_NextMember)
    {
        p_NextReplicAd = (t_AdOfTypeResult*)p_NextMember->p_MemberAd;
        p_FmPcd = p_ReplicGroup->h_FmPcd;
        offset = (XX_VirtToPhys(p_NextReplicAd) - (p_FmPcd->physicalMuramBase));
        offset = ((offset>>NEXT_FRM_REPLIC_ADDR_SHIFT)<< NEXT_FRM_REPLIC_MEMBER_INDEX_SHIFT);
    }

    /* link the current AD to point to the AD of the next member */
    WRITE_UINT32(p_CurrReplicAd->res, offset);
}

static t_Error ModifyDescriptor(t_FmPcdFrmReplicGroup   *p_ReplicGroup,
                                void                    *p_OldDescriptor,
                                void                    *p_NewDescriptor)
{
    t_Handle            h_Hc;
    t_Error             err;
    t_FmPcd             *p_FmPcd;

    ASSERT_COND(p_ReplicGroup);
    ASSERT_COND(p_ReplicGroup->h_FmPcd);
    ASSERT_COND(p_OldDescriptor);
    ASSERT_COND(p_NewDescriptor);

    p_FmPcd = p_ReplicGroup->h_FmPcd;
    h_Hc = FmPcdGetHcHandle(p_FmPcd);
    if (!h_Hc)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("Host command"));

    err = FmHcPcdCcDoDynamicChange(h_Hc,
                                   (uint32_t)(XX_VirtToPhys(p_OldDescriptor) - p_FmPcd->physicalMuramBase),
                                   (uint32_t)(XX_VirtToPhys(p_NewDescriptor) - p_FmPcd->physicalMuramBase));
    if (err)
        RETURN_ERROR(MAJOR, err, ("Dynamic change host command"));

    return E_OK;
}

static void FillReplicAdOfTypeResult(void *p_ReplicAd, bool last)
{
    t_AdOfTypeResult    *p_CurrReplicAd = (t_AdOfTypeResult*)p_ReplicAd;
    uint32_t            tmp;

    tmp = GET_UINT32(p_CurrReplicAd->plcrProfile);
    if (last)
        /* clear the NL bit in case it's the last member in the group*/
        WRITE_UINT32(p_CurrReplicAd->plcrProfile,(tmp & ~FRM_REPLIC_NL_BIT));
    else
        /* set the NL bit in case it's not the last member in the group */
        WRITE_UINT32(p_CurrReplicAd->plcrProfile, (tmp |FRM_REPLIC_NL_BIT));

    /* set FR bit in the action descriptor */
    tmp = GET_UINT32(p_CurrReplicAd->nia);
    WRITE_UINT32(p_CurrReplicAd->nia,
        (tmp | FRM_REPLIC_FR_BIT | FM_PCD_AD_RESULT_EXTENDED_MODE ));
}

static void BuildSourceTd(void *p_Ad)
{
    t_AdOfTypeContLookup    *p_SourceTd;

    ASSERT_COND(p_Ad);

    p_SourceTd = (t_AdOfTypeContLookup *)p_Ad;

    IOMemSet32((uint8_t*)p_SourceTd, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* initialize the source table descriptor */
    WRITE_UINT32(p_SourceTd->ccAdBase,     FM_PCD_AD_CONT_LOOKUP_TYPE);
    WRITE_UINT32(p_SourceTd->pcAndOffsets, FRM_REPLIC_SOURCE_TD_OPCODE);
}

static t_Error BuildShadowAndModifyDescriptor(t_FmPcdFrmReplicGroup   *p_ReplicGroup,
                                              t_FmPcdFrmReplicMember  *p_NextMember,
                                              t_FmPcdFrmReplicMember  *p_CurrentMember,
                                              bool                    sourceDescriptor,
                                              bool                    last)
{
    t_FmPcd                 *p_FmPcd;
    t_FmPcdFrmReplicMember  shadowMember;
    t_Error                 err;

    ASSERT_COND(p_ReplicGroup);
    ASSERT_COND(p_ReplicGroup->h_FmPcd);

    p_FmPcd = p_ReplicGroup->h_FmPcd;
    ASSERT_COND(p_FmPcd->p_CcShadow);

    if (!TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
        return ERROR_CODE(E_BUSY);

    if (sourceDescriptor)
    {
        BuildSourceTd(p_FmPcd->p_CcShadow);
        LinkSourceToMember(p_ReplicGroup, p_FmPcd->p_CcShadow, p_NextMember);

        /* Modify the source table descriptor according to the prepared shadow descriptor */
        err = ModifyDescriptor(p_ReplicGroup,
                               p_ReplicGroup->p_SourceTd,
                               p_FmPcd->p_CcShadow/* new prepared source td */);

        RELEASE_LOCK(p_FmPcd->shadowLock);
        if (err)
            RETURN_ERROR(MAJOR, err, ("Modify source Descriptor in BuildShadowAndModifyDescriptor"));

    }
    else
    {
        IO2IOCpy32(p_FmPcd->p_CcShadow,
                   p_CurrentMember->p_MemberAd,
                   FM_PCD_CC_AD_ENTRY_SIZE);

        /* update the last bit in the shadow ad */
        FillReplicAdOfTypeResult(p_FmPcd->p_CcShadow, last);

        shadowMember.p_MemberAd = p_FmPcd->p_CcShadow;

        /* update the next FR member index */
        LinkMemberToMember(p_ReplicGroup, &shadowMember, p_NextMember);

        /* Modify the next member according to the prepared shadow descriptor */
        err = ModifyDescriptor(p_ReplicGroup,
                               p_CurrentMember->p_MemberAd,
                               p_FmPcd->p_CcShadow);

        RELEASE_LOCK(p_FmPcd->shadowLock);
        if (err)
            RETURN_ERROR(MAJOR, err, ("Modify Descriptor in BuildShadowAndModifyDescriptor"));
    }


    return E_OK;
}

static t_FmPcdFrmReplicMember* GetMemberByIndex(t_FmPcdFrmReplicGroup   *p_ReplicGroup,
                                                uint16_t                memberIndex)
{
    int                     i=0;
    t_List                  *p_Pos;
    t_FmPcdFrmReplicMember  *p_Member = NULL;

    LIST_FOR_EACH(p_Pos, &p_ReplicGroup->membersList)
    {
        if (i == memberIndex)
        {
            p_Member = LIST_OBJECT(p_Pos, t_FmPcdFrmReplicMember, node);
            return p_Member;
        }
        i++;
    }
    return p_Member;
}

static t_Error AllocMember(t_FmPcdFrmReplicGroup *p_ReplicGroup)
{
    t_FmPcdFrmReplicMember  *p_CurrentMember;
    t_Handle                h_Muram;

    ASSERT_COND(p_ReplicGroup);

    h_Muram = FmPcdGetMuramHandle(p_ReplicGroup->h_FmPcd);
    ASSERT_COND(h_Muram);

    /* Initialize an internal structure of a member to add to the available members list */
    p_CurrentMember = (t_FmPcdFrmReplicMember *)XX_Malloc(sizeof(t_FmPcdFrmReplicMember));
    if (!p_CurrentMember)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Frame replicator member"));

    memset(p_CurrentMember, 0 ,sizeof(t_FmPcdFrmReplicMember));

    /* Allocate the member AD */
    p_CurrentMember->p_MemberAd =
        (t_AdOfTypeResult*)FM_MURAM_AllocMem(h_Muram,
                                             FM_PCD_CC_AD_ENTRY_SIZE,
                                             FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_CurrentMember->p_MemberAd)
    {
        XX_Free(p_CurrentMember);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("member AD table"));
    }
    IOMemSet32((uint8_t*)p_CurrentMember->p_MemberAd, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* Add the new member to the available members list */
    LIST_AddToTail(&p_CurrentMember->node, &(p_ReplicGroup->availableMembersList));

    return E_OK;
}

static t_FmPcdFrmReplicMember* InitMember(t_FmPcdFrmReplicGroup     *p_ReplicGroup,
                                          t_FmPcdCcNextEngineParams *p_MemberParams,
                                          bool                      last)
{
    t_FmPcdFrmReplicMember  *p_CurrentMember = NULL;

    ASSERT_COND(p_ReplicGroup);

    /* Get an available member from the internal members list */
    p_CurrentMember = GetAvailableMember(p_ReplicGroup);
    if (!p_CurrentMember)
    {
        REPORT_ERROR(MAJOR, E_NOT_FOUND, ("Available member"));
        return NULL;
    }
    p_CurrentMember->h_Manip = NULL;

    /* clear the Ad of the new member */
    IOMemSet32((uint8_t*)p_CurrentMember->p_MemberAd, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    INIT_LIST(&p_CurrentMember->node);

    /* Initialize the Ad of the member */
    NextStepAd(p_CurrentMember->p_MemberAd,
               NULL,
               p_MemberParams,
               p_ReplicGroup->h_FmPcd);

    /* save Manip handle (for free needs) */
    if (p_MemberParams->h_Manip)
        p_CurrentMember->h_Manip = p_MemberParams->h_Manip;

    /* Initialize the relevant frame replicator fields in the AD */
    FillReplicAdOfTypeResult(p_CurrentMember->p_MemberAd, last);

    return p_CurrentMember;
}

static void FreeMember(t_FmPcdFrmReplicGroup    *p_ReplicGroup,
                       t_FmPcdFrmReplicMember   *p_Member)
{
    /* Note: Can't free the member AD just returns the member to the available
       member list - therefore only memset the AD */

    /* zero the AD */
    IOMemSet32(p_Member->p_MemberAd, 0, FM_PCD_CC_AD_ENTRY_SIZE);


    /* return the member to the available members list */
    PutAvailableMember(p_ReplicGroup, p_Member);
}

static t_Error RemoveMember(t_FmPcdFrmReplicGroup   *p_ReplicGroup,
                            uint16_t                memberIndex)
{
    t_FmPcd                 *p_FmPcd = NULL;
    t_FmPcdFrmReplicMember  *p_CurrentMember = NULL, *p_PreviousMember = NULL, *p_NextMember = NULL;
    t_Error                 err;
    uint8_t                 memberPosition;

    p_FmPcd         = p_ReplicGroup->h_FmPcd;
    ASSERT_COND(p_FmPcd);
    UNUSED(p_FmPcd);

    p_CurrentMember = GetMemberByIndex(p_ReplicGroup, memberIndex);
    ASSERT_COND(p_CurrentMember);

    /* determine the member position in the group */
    memberPosition = GetMemberPosition(p_ReplicGroup,
                                       memberIndex,
                                       FALSE/*remove operation*/);

    switch (memberPosition)
    {
        case FRM_REPLIC_FIRST_MEMBER_INDEX:
            p_NextMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)(memberIndex+1));
            ASSERT_COND(p_NextMember);

            /* update the source td itself by using a host command */
            err = BuildShadowAndModifyDescriptor(p_ReplicGroup,
                                                 p_NextMember,
                                                 NULL,
                                                 TRUE/*sourceDescriptor*/,
                                                 FALSE/*last*/);
            break;

        case FRM_REPLIC_MIDDLE_MEMBER_INDEX:
            p_PreviousMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)(memberIndex-1));
            ASSERT_COND(p_PreviousMember);

            p_NextMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)(memberIndex+1));
            ASSERT_COND(p_NextMember);

            err = BuildShadowAndModifyDescriptor(p_ReplicGroup,
                                                 p_NextMember,
                                                 p_PreviousMember,
                                                 FALSE/*sourceDescriptor*/,
                                                 FALSE/*last*/);

            break;

        case FRM_REPLIC_LAST_MEMBER_INDEX:
            p_PreviousMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)(memberIndex-1));
            ASSERT_COND(p_PreviousMember);

            err = BuildShadowAndModifyDescriptor(p_ReplicGroup,
                                                 NULL,
                                                 p_PreviousMember,
                                                 FALSE/*sourceDescriptor*/,
                                                 TRUE/*last*/);
            break;

        default:
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("member position in remove member"));
    }

    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    if (p_CurrentMember->h_Manip)
    {
        FmPcdManipUpdateOwner(p_CurrentMember->h_Manip, FALSE);
        p_CurrentMember->h_Manip = NULL;
    }

    /* remove the member from the driver internal members list */
    RemoveMemberFromList(p_ReplicGroup, p_CurrentMember);

    /* return the member to the available members list */
    FreeMember(p_ReplicGroup, p_CurrentMember);

    return E_OK;
}

static void DeleteGroup(t_FmPcdFrmReplicGroup *p_ReplicGroup)
{
    int                     i, j;
    t_Handle                h_Muram;
    t_FmPcdFrmReplicMember  *p_Member, *p_CurrentMember;

    if (p_ReplicGroup)
    {
        ASSERT_COND(p_ReplicGroup->h_FmPcd);
        h_Muram = FmPcdGetMuramHandle(p_ReplicGroup->h_FmPcd);
        ASSERT_COND(h_Muram);

        /* free the source table descriptor */
        if (p_ReplicGroup->p_SourceTd)
        {
            FM_MURAM_FreeMem(h_Muram, p_ReplicGroup->p_SourceTd);
            p_ReplicGroup->p_SourceTd = NULL;
        }

        /* Remove all members from the members linked list (hw and sw) and
           return the members to the available members list */
        if (p_ReplicGroup->numOfEntries)
        {
            j = p_ReplicGroup->numOfEntries-1;

            /* manually removal of the member because there are no owners of
               this group */
            for (i=j; i>=0; i--)
            {
                p_CurrentMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)i/*memberIndex*/);
                ASSERT_COND(p_CurrentMember);

                if (p_CurrentMember->h_Manip)
                {
                    FmPcdManipUpdateOwner(p_CurrentMember->h_Manip, FALSE);
                    p_CurrentMember->h_Manip = NULL;
                }

                /* remove the member from the internal driver members list */
                RemoveMemberFromList(p_ReplicGroup, p_CurrentMember);

                /* return the member to the available members list */
                FreeMember(p_ReplicGroup, p_CurrentMember);
            }
        }

        /* Free members AD */
        for (i=0; i<p_ReplicGroup->maxNumOfEntries; i++)
        {
            p_Member = GetAvailableMember(p_ReplicGroup);
            ASSERT_COND(p_Member);
            if (p_Member->p_MemberAd)
            {
                FM_MURAM_FreeMem(h_Muram, p_Member->p_MemberAd);
                p_Member->p_MemberAd = NULL;
            }
            XX_Free(p_Member);
        }

        /* release the group lock */
        if (p_ReplicGroup->p_Lock)
            FmPcdReleaseLock(p_ReplicGroup->h_FmPcd, p_ReplicGroup->p_Lock);

        /* free the replicator group */
        XX_Free(p_ReplicGroup);
    }
}


/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/

/* NOTE: the inter-module routines are locked by cc in case of using them */
void * FrmReplicGroupGetSourceTableDescriptor(t_Handle h_ReplicGroup)
{
    t_FmPcdFrmReplicGroup   *p_ReplicGroup = (t_FmPcdFrmReplicGroup *)h_ReplicGroup;
    ASSERT_COND(p_ReplicGroup);

    return (p_ReplicGroup->p_SourceTd);
}

void FrmReplicGroupUpdateAd(t_Handle  h_ReplicGroup,
                            void      *p_Ad,
                            t_Handle  *h_AdNew)
{
    t_FmPcdFrmReplicGroup   *p_ReplicGroup = (t_FmPcdFrmReplicGroup *)h_ReplicGroup;
    t_AdOfTypeResult    *p_AdResult = (t_AdOfTypeResult*)p_Ad;
    t_FmPcd             *p_FmPcd;

    ASSERT_COND(p_ReplicGroup);
    p_FmPcd = p_ReplicGroup->h_FmPcd;

    /* build a bypass ad */
    WRITE_UINT32(p_AdResult->fqid, FM_PCD_AD_BYPASS_TYPE |
        (uint32_t)((XX_VirtToPhys(p_ReplicGroup->p_SourceTd)) - p_FmPcd->physicalMuramBase));

    *h_AdNew = NULL;
}

void  FrmReplicGroupUpdateOwner(t_Handle                   h_ReplicGroup,
                                bool                       add)
{
    t_FmPcdFrmReplicGroup   *p_ReplicGroup = (t_FmPcdFrmReplicGroup *)h_ReplicGroup;
    ASSERT_COND(p_ReplicGroup);

    /* update the group owner counter */
    if (add)
        p_ReplicGroup->owners++;
    else
    {
        ASSERT_COND(p_ReplicGroup->owners);
        p_ReplicGroup->owners--;
    }
}

t_Error FrmReplicGroupTryLock(t_Handle h_ReplicGroup)
{
    t_FmPcdFrmReplicGroup *p_ReplicGroup = (t_FmPcdFrmReplicGroup *)h_ReplicGroup;

    ASSERT_COND(h_ReplicGroup);

    if (FmPcdLockTryLock(p_ReplicGroup->p_Lock))
        return E_OK;

    return ERROR_CODE(E_BUSY);
}

void FrmReplicGroupUnlock(t_Handle h_ReplicGroup)
{
    t_FmPcdFrmReplicGroup *p_ReplicGroup = (t_FmPcdFrmReplicGroup *)h_ReplicGroup;

    ASSERT_COND(h_ReplicGroup);

    FmPcdLockUnlock(p_ReplicGroup->p_Lock);
}
/*********************** End of inter-module routines ************************/


/****************************************/
/*       API Init unit functions        */
/****************************************/
t_Handle FM_PCD_FrmReplicSetGroup(t_Handle                    h_FmPcd,
                                  t_FmPcdFrmReplicGroupParams *p_ReplicGroupParam)
{
    t_FmPcdFrmReplicGroup       *p_ReplicGroup;
    t_FmPcdFrmReplicMember      *p_CurrentMember, *p_NextMember = NULL;
    int                         i;
    t_Error                     err;
    bool                        last = FALSE;
    t_Handle                    h_Muram;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_ReplicGroupParam, E_INVALID_HANDLE, NULL);

    if (!FmPcdIsAdvancedOffloadSupported(h_FmPcd))
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Advanced-offload must be enabled"));
        return NULL;
    }

    err = CheckParams(h_FmPcd, p_ReplicGroupParam);
    if (err)
    {
        REPORT_ERROR(MAJOR, err, (NO_MSG));
        return NULL;
    }

    p_ReplicGroup = (t_FmPcdFrmReplicGroup*)XX_Malloc(sizeof(t_FmPcdFrmReplicGroup));
    if (!p_ReplicGroup)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("No memory"));
        return NULL;
    }
    memset(p_ReplicGroup, 0, sizeof(t_FmPcdFrmReplicGroup));

    /* initialize lists for internal driver use */
    INIT_LIST(&p_ReplicGroup->availableMembersList);
    INIT_LIST(&p_ReplicGroup->membersList);

    p_ReplicGroup->h_FmPcd = h_FmPcd;

    h_Muram = FmPcdGetMuramHandle(p_ReplicGroup->h_FmPcd);
    ASSERT_COND(h_Muram);

    /* initialize the group lock */
    p_ReplicGroup->p_Lock = FmPcdAcquireLock(p_ReplicGroup->h_FmPcd);
    if (!p_ReplicGroup->p_Lock)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Replic group lock"));
        DeleteGroup(p_ReplicGroup);
        return NULL;
    }

    /* Allocate the frame replicator source table descriptor */
    p_ReplicGroup->p_SourceTd =
        (t_Handle)FM_MURAM_AllocMem(h_Muram,
                                    FM_PCD_CC_AD_ENTRY_SIZE,
                                    FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_ReplicGroup->p_SourceTd)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("frame replicator source table descriptor"));
        DeleteGroup(p_ReplicGroup);
        return NULL;
    }

    /* update the shadow size - required for the host commands */
    err = FmPcdUpdateCcShadow(p_ReplicGroup->h_FmPcd,
                              FM_PCD_CC_AD_ENTRY_SIZE,
                              FM_PCD_CC_AD_TABLE_ALIGN);
    if (err)
    {
        REPORT_ERROR(MAJOR, err, ("Update CC shadow"));
        DeleteGroup(p_ReplicGroup);
        return NULL;
    }

    p_ReplicGroup->maxNumOfEntries  = p_ReplicGroupParam->maxNumOfEntries;

    /* Allocate the maximal number of members ADs and Statistics AD for the group
       It prevents allocation of Muram in run-time */
    for (i=0; i<p_ReplicGroup->maxNumOfEntries; i++)
    {
        err = AllocMember(p_ReplicGroup);
        if (err)
        {
            REPORT_ERROR(MAJOR, err, ("allocate a new member"));
            DeleteGroup(p_ReplicGroup);
            return NULL;
        }
    }

    /* Initialize the members linked lists:
      (hw - the one that is used by the FMan controller and
       sw - the one that is managed by the driver internally) */
    for (i=(p_ReplicGroupParam->numOfEntries-1); i>=0; i--)
    {
        /* check if this is the last member in the group */
        if (i == (p_ReplicGroupParam->numOfEntries-1))
            last = TRUE;
        else
            last = FALSE;

        /* Initialize a new member */
        p_CurrentMember = InitMember(p_ReplicGroup,
                                     &(p_ReplicGroupParam->nextEngineParams[i]),
                                     last);
        if (!p_CurrentMember)
        {
            REPORT_ERROR(MAJOR, E_INVALID_HANDLE, ("No available member"));
            DeleteGroup(p_ReplicGroup);
            return NULL;
        }

        /* Build the members group - link two consecutive members in the hw linked list */
        LinkMemberToMember(p_ReplicGroup, p_CurrentMember, p_NextMember);

        /* update the driver internal members list to be compatible to the hw members linked list */
        AddMemberToList(p_ReplicGroup, p_CurrentMember, &p_ReplicGroup->membersList);

        p_NextMember = p_CurrentMember;
    }

    /* initialize the source table descriptor */
    BuildSourceTd(p_ReplicGroup->p_SourceTd);

    /* link the source table descriptor to point to the first member in the group */
    LinkSourceToMember(p_ReplicGroup, p_ReplicGroup->p_SourceTd, p_NextMember);

    return p_ReplicGroup;
}

t_Error FM_PCD_FrmReplicDeleteGroup(t_Handle h_ReplicGroup)
{
    t_FmPcdFrmReplicGroup   *p_ReplicGroup = (t_FmPcdFrmReplicGroup *)h_ReplicGroup;

    SANITY_CHECK_RETURN_ERROR(p_ReplicGroup, E_INVALID_HANDLE);

    if (p_ReplicGroup->owners)
        RETURN_ERROR(MAJOR,
                     E_INVALID_STATE,
                     ("the group has owners and can't be deleted"));

    DeleteGroup(p_ReplicGroup);

    return E_OK;
}


/*****************************************************************************/
/*       API Run-time Frame replicator Control unit functions                */
/*****************************************************************************/
t_Error FM_PCD_FrmReplicAddMember(t_Handle                  h_ReplicGroup,
                                  uint16_t                  memberIndex,
                                  t_FmPcdCcNextEngineParams *p_MemberParams)
{
    t_FmPcdFrmReplicGroup       *p_ReplicGroup = (t_FmPcdFrmReplicGroup*) h_ReplicGroup;
    t_FmPcdFrmReplicMember      *p_NewMember, *p_CurrentMember = NULL, *p_PreviousMember = NULL;
    t_Error                     err;
    uint8_t                     memberPosition;

    SANITY_CHECK_RETURN_ERROR(p_ReplicGroup, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_MemberParams, E_INVALID_HANDLE);

    /* group lock */
    err = FrmReplicGroupTryLock(p_ReplicGroup);
    if (GET_ERROR_TYPE(err) == E_BUSY)
        return ERROR_CODE(E_BUSY);

    if (memberIndex > p_ReplicGroup->numOfEntries)
    {
        /* unlock */
        FrmReplicGroupUnlock(p_ReplicGroup);
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                     ("memberIndex is greater than the members in the list"));
    }

    if (memberIndex >= p_ReplicGroup->maxNumOfEntries)
    {
        /* unlock */
        FrmReplicGroupUnlock(p_ReplicGroup);
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("memberIndex is greater than the allowed number of members in the group"));
    }

    if ((p_ReplicGroup->numOfEntries + 1) > FM_PCD_FRM_REPLIC_MAX_NUM_OF_ENTRIES)
    {
        /* unlock */
        FrmReplicGroupUnlock(p_ReplicGroup);
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("numOfEntries with new entry can not be larger than %d\n",
                      FM_PCD_FRM_REPLIC_MAX_NUM_OF_ENTRIES));
    }

    err = MemberCheckParams(p_ReplicGroup->h_FmPcd, p_MemberParams);
    if (err)
    {
        /* unlock */
        FrmReplicGroupUnlock(p_ReplicGroup);
        RETURN_ERROR(MAJOR, err, ("member check parameters in add operation"));
    }
    /* determine the member position in the group */
    memberPosition = GetMemberPosition(p_ReplicGroup,
                                       memberIndex,
                                       TRUE/* add operation */);

    /* Initialize a new member */
    p_NewMember = InitMember(p_ReplicGroup,
                             p_MemberParams,
                             (memberPosition == FRM_REPLIC_LAST_MEMBER_INDEX ? TRUE : FALSE));
    if (!p_NewMember)
    {
        /* unlock */
        FrmReplicGroupUnlock(p_ReplicGroup);
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("No available member"));
    }

    switch (memberPosition)
    {
        case FRM_REPLIC_FIRST_MEMBER_INDEX:
            p_CurrentMember = GetMemberByIndex(p_ReplicGroup, memberIndex);
            ASSERT_COND(p_CurrentMember);

            LinkMemberToMember(p_ReplicGroup, p_NewMember, p_CurrentMember);

            /* update the internal group source TD */
            LinkSourceToMember(p_ReplicGroup,
                               p_ReplicGroup->p_SourceTd,
                               p_NewMember);

            /* add member to the internal sw member list */
            AddMemberToList(p_ReplicGroup,
                            p_NewMember,
                            &p_ReplicGroup->membersList);
            break;

        case FRM_REPLIC_MIDDLE_MEMBER_INDEX:
            p_CurrentMember = GetMemberByIndex(p_ReplicGroup, memberIndex);
            ASSERT_COND(p_CurrentMember);

            p_PreviousMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)(memberIndex-1));
            ASSERT_COND(p_PreviousMember);

            LinkMemberToMember(p_ReplicGroup, p_NewMember, p_CurrentMember);
            LinkMemberToMember(p_ReplicGroup, p_PreviousMember, p_NewMember);

            AddMemberToList(p_ReplicGroup, p_NewMember, &p_PreviousMember->node);
            break;

        case FRM_REPLIC_LAST_MEMBER_INDEX:
            p_PreviousMember = GetMemberByIndex(p_ReplicGroup, (uint16_t)(memberIndex-1));
            ASSERT_COND(p_PreviousMember);

            LinkMemberToMember(p_ReplicGroup, p_PreviousMember, p_NewMember);
            FillReplicAdOfTypeResult(p_PreviousMember->p_MemberAd, FALSE/*last*/);

            /* add the new member to the internal sw member list */
            AddMemberToList(p_ReplicGroup, p_NewMember, &p_PreviousMember->node);
           break;

        default:
            /* unlock */
            FrmReplicGroupUnlock(p_ReplicGroup);
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("member position in add member"));

    }

    /* unlock */
    FrmReplicGroupUnlock(p_ReplicGroup);

    return E_OK;
}

t_Error FM_PCD_FrmReplicRemoveMember(t_Handle   h_ReplicGroup,
                                     uint16_t   memberIndex)
{
    t_FmPcdFrmReplicGroup   *p_ReplicGroup = (t_FmPcdFrmReplicGroup*) h_ReplicGroup;
    t_Error                 err;

    SANITY_CHECK_RETURN_ERROR(p_ReplicGroup, E_INVALID_HANDLE);

    /* lock */
    err = FrmReplicGroupTryLock(p_ReplicGroup);
    if (GET_ERROR_TYPE(err) == E_BUSY)
        return ERROR_CODE(E_BUSY);

    if (memberIndex >= p_ReplicGroup->numOfEntries)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("member index to remove"));

    /* Design decision: group must contain at least one member
       No possibility to remove the last member from the group */
    if (p_ReplicGroup->numOfEntries == 1)
        RETURN_ERROR(MAJOR, E_CONFLICT, ("Can't remove the last member. At least one member should be related to a group."));

    err = RemoveMember(p_ReplicGroup, memberIndex);

    /* unlock */
    FrmReplicGroupUnlock(p_ReplicGroup);

    switch (GET_ERROR_TYPE(err))
    {
        case E_OK:
            return E_OK;

        case E_BUSY:
            DBG(TRACE, ("E_BUSY error"));
            return ERROR_CODE(E_BUSY);

        default:
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

/*********************** End of API routines ************************/


