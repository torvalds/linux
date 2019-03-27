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
 @File          fm_manip.c

 @Description   FM PCD manip ...
 *//***************************************************************************/
#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "debug_ext.h"
#include "fm_pcd_ext.h"
#include "fm_port_ext.h"
#include "fm_muram_ext.h"
#include "memcpy_ext.h"

#include "fm_common.h"
#include "fm_hc.h"
#include "fm_manip.h"

/****************************************/
/*       static functions               */
/****************************************/
static t_Handle GetManipInfo(t_FmPcdManip *p_Manip, e_ManipInfo manipInfo)
{
    t_FmPcdManip *p_CurManip = p_Manip;

    if (!MANIP_IS_UNIFIED(p_Manip))
        p_CurManip = p_Manip;
    else
    {
        /* go to first unified */
        while (MANIP_IS_UNIFIED_NON_FIRST(p_CurManip))
            p_CurManip = p_CurManip->h_PrevManip;
    }

    switch (manipInfo)
    {
        case (e_MANIP_HMCT):
            return p_CurManip->p_Hmct;
        case (e_MANIP_HMTD):
            return p_CurManip->h_Ad;
        case (e_MANIP_HANDLER_TABLE_OWNER):
            return (t_Handle)p_CurManip;
        default:
            return NULL;
    }
}

static uint16_t GetHmctSize(t_FmPcdManip *p_Manip)
{
    uint16_t size = 0;
    t_FmPcdManip *p_CurManip = p_Manip;

    if (!MANIP_IS_UNIFIED(p_Manip))
        return p_Manip->tableSize;

    /* accumulate sizes, starting with the first node */
    while (MANIP_IS_UNIFIED_NON_FIRST(p_CurManip))
        p_CurManip = p_CurManip->h_PrevManip;

    while (MANIP_IS_UNIFIED_NON_LAST(p_CurManip))
    {
        size += p_CurManip->tableSize;
        p_CurManip = (t_FmPcdManip *)p_CurManip->h_NextManip;
    }
    size += p_CurManip->tableSize; /* add last size */

    return (size);
}

static uint16_t GetDataSize(t_FmPcdManip *p_Manip)
{
    uint16_t size = 0;
    t_FmPcdManip *p_CurManip = p_Manip;

    if (!MANIP_IS_UNIFIED(p_Manip))
        return p_Manip->dataSize;

    /* accumulate sizes, starting with the first node */
    while (MANIP_IS_UNIFIED_NON_FIRST(p_CurManip))
        p_CurManip = p_CurManip->h_PrevManip;

    while (MANIP_IS_UNIFIED_NON_LAST(p_CurManip))
    {
        size += p_CurManip->dataSize;
        p_CurManip = (t_FmPcdManip *)p_CurManip->h_NextManip;
    }
    size += p_CurManip->dataSize; /* add last size */

    return (size);
}

static t_Error CalculateTableSize(t_FmPcdManipParams *p_FmPcdManipParams,
                                  uint16_t *p_TableSize, uint8_t *p_DataSize)
{
    uint8_t localDataSize, remain, tableSize = 0, dataSize = 0;

    if (p_FmPcdManipParams->u.hdr.rmv)
    {
        switch (p_FmPcdManipParams->u.hdr.rmvParams.type)
        {
            case (e_FM_PCD_MANIP_RMV_GENERIC):
                tableSize += HMCD_BASIC_SIZE;
                break;
            case (e_FM_PCD_MANIP_RMV_BY_HDR):
                switch (p_FmPcdManipParams->u.hdr.rmvParams.u.byHdr.type)
                {
                    case (e_FM_PCD_MANIP_RMV_BY_HDR_SPECIFIC_L2):
#if (DPAA_VERSION >= 11)
                    case (e_FM_PCD_MANIP_RMV_BY_HDR_CAPWAP):
                    case (e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START):
#endif /* (DPAA_VERSION >= 11) */
                        tableSize += HMCD_BASIC_SIZE;
                        break;
                    default:
                        RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                                     ("Unknown byHdr.type"));
                }
                break;
            default:
                RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                             ("Unknown rmvParams.type"));
        }
    }

    if (p_FmPcdManipParams->u.hdr.insrt)
    {
        switch (p_FmPcdManipParams->u.hdr.insrtParams.type)
        {
            case (e_FM_PCD_MANIP_INSRT_GENERIC):
                remain =
                        (uint8_t)(p_FmPcdManipParams->u.hdr.insrtParams.u.generic.size
                                % 4);
                if (remain)
                    localDataSize =
                            (uint8_t)(p_FmPcdManipParams->u.hdr.insrtParams.u.generic.size
                                    + 4 - remain);
                else
                    localDataSize =
                            p_FmPcdManipParams->u.hdr.insrtParams.u.generic.size;
                tableSize += (uint8_t)(HMCD_BASIC_SIZE + localDataSize);
                break;
            case (e_FM_PCD_MANIP_INSRT_BY_HDR):
            {
                switch (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.type)
                {

                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_SPECIFIC_L2):
                        tableSize += HMCD_BASIC_SIZE + HMCD_PTR_SIZE;
                        switch (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.specificL2Params.specificL2)
                        {
                            case (e_FM_PCD_MANIP_HDR_INSRT_MPLS):
                            case (e_FM_PCD_MANIP_HDR_INSRT_PPPOE):
                                dataSize +=
                                        p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.specificL2Params.size;
                                break;
                            default:
                                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
                        }
                        break;
#if (DPAA_VERSION >= 11)
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_IP):
                        tableSize +=
                                (HMCD_BASIC_SIZE + HMCD_PTR_SIZE
                                        + HMCD_PARAM_SIZE
                                        + p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.size);
                        dataSize += 2;
                        break;

                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_UDP):
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_UDP_LITE):
                        tableSize += (HMCD_BASIC_SIZE + HMCD_L4_HDR_SIZE);

                        break;

                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_CAPWAP):
                        tableSize +=
                                (HMCD_BASIC_SIZE
                                        + p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size);
                        break;
#endif /* (DPAA_VERSION >= 11) */
                    default:
                        RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                                     ("Unknown byHdr.type"));
                }
            }
                break;
            default:
                RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                             ("Unknown insrtParams.type"));
        }
    }

    if (p_FmPcdManipParams->u.hdr.fieldUpdate)
    {
        switch (p_FmPcdManipParams->u.hdr.fieldUpdateParams.type)
        {
            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN):
                tableSize += HMCD_BASIC_SIZE;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.vlan.updateType
                        == e_FM_PCD_MANIP_HDR_FIELD_UPDATE_DSCP_TO_VLAN)
                {
                    tableSize += HMCD_PTR_SIZE;
                    dataSize += DSCP_TO_VLAN_TABLE_SIZE;
                }
                break;
            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV4):
                tableSize += HMCD_BASIC_SIZE;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_ID)
                {
                    tableSize += HMCD_PARAM_SIZE;
                    dataSize += 2;
                }
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_SRC)
                    tableSize += HMCD_IPV4_ADDR_SIZE;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_DST)
                    tableSize += HMCD_IPV4_ADDR_SIZE;
                break;
            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV6):
                tableSize += HMCD_BASIC_SIZE;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV6_SRC)
                    tableSize += HMCD_IPV6_ADDR_SIZE;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV6_DST)
                    tableSize += HMCD_IPV6_ADDR_SIZE;
                break;
            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_TCP_UDP):
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.validUpdates
                        == HDR_MANIP_TCP_UDP_CHECKSUM)
                    /* we implement this case with the update-checksum descriptor */
                    tableSize += HMCD_BASIC_SIZE;
                else
                    /* we implement this case with the TCP/UDP-update descriptor */
                    tableSize += HMCD_BASIC_SIZE + HMCD_PARAM_SIZE;
                break;
            default:
                RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                             ("Unknown fieldUpdateParams.type"));
        }
    }

    if (p_FmPcdManipParams->u.hdr.custom)
    {
        switch (p_FmPcdManipParams->u.hdr.customParams.type)
        {
            case (e_FM_PCD_MANIP_HDR_CUSTOM_IP_REPLACE):
            {
                tableSize += HMCD_BASIC_SIZE + HMCD_PARAM_SIZE + HMCD_PARAM_SIZE;
                dataSize +=
                        p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.hdrSize;
                if ((p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.replaceType
                        == e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV6_BY_IPV4)
                        && (p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.updateIpv4Id))
                    dataSize += 2;
            }
                break;
            case (e_FM_PCD_MANIP_HDR_CUSTOM_GEN_FIELD_REPLACE):
                tableSize += HMCD_BASIC_SIZE + HMCD_PARAM_SIZE;
            break;
            default:
                RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                             ("Unknown customParams.type"));
        }
    }

    *p_TableSize = tableSize;
    *p_DataSize = dataSize;

    return E_OK;
}

static t_Error GetPrOffsetByHeaderOrField(t_FmManipHdrInfo *p_HdrInfo,
                                          uint8_t *parseArrayOffset)
{
    e_NetHeaderType hdr = p_HdrInfo->hdr;
    e_FmPcdHdrIndex hdrIndex = p_HdrInfo->hdrIndex;
    bool byField = p_HdrInfo->byField;
    t_FmPcdFields field;

    if (byField)
        field = p_HdrInfo->fullField;

    if (byField)
    {
        switch (hdr)
        {
            case (HEADER_TYPE_ETH):
                switch (field.eth)
                {
                    case (NET_HEADER_FIELD_ETH_TYPE):
                        *parseArrayOffset = CC_PC_PR_ETYPE_LAST_OFFSET;
                        break;
                    default:
                        RETURN_ERROR(
                                MAJOR,
                                E_NOT_SUPPORTED,
                                ("Header manipulation of the type Ethernet with this field not supported"));
                }
                break;
            case (HEADER_TYPE_VLAN):
                switch (field.vlan)
                {
                    case (NET_HEADER_FIELD_VLAN_TCI):
                        if ((hdrIndex == e_FM_PCD_HDR_INDEX_NONE)
                                || (hdrIndex == e_FM_PCD_HDR_INDEX_1))
                            *parseArrayOffset = CC_PC_PR_VLAN1_OFFSET;
                        else
                            if (hdrIndex == e_FM_PCD_HDR_INDEX_LAST)
                                *parseArrayOffset = CC_PC_PR_VLAN2_OFFSET;
                        break;
                    default:
                        RETURN_ERROR(
                                MAJOR,
                                E_NOT_SUPPORTED,
                                ("Header manipulation of the type VLAN with this field not supported"));
                }
                break;
            default:
                RETURN_ERROR(
                        MAJOR,
                        E_NOT_SUPPORTED,
                        ("Header manipulation of this header by field not supported"));
        }
    }
    else
    {
        switch (hdr)
        {
            case (HEADER_TYPE_ETH):
                *parseArrayOffset = (uint8_t)CC_PC_PR_ETH_OFFSET;
                break;
            case (HEADER_TYPE_USER_DEFINED_SHIM1):
                *parseArrayOffset = (uint8_t)CC_PC_PR_USER_DEFINED_SHIM1_OFFSET;
                break;
            case (HEADER_TYPE_USER_DEFINED_SHIM2):
                *parseArrayOffset = (uint8_t)CC_PC_PR_USER_DEFINED_SHIM2_OFFSET;
                break;
            case (HEADER_TYPE_LLC_SNAP):
                *parseArrayOffset = CC_PC_PR_USER_LLC_SNAP_OFFSET;
                break;
            case (HEADER_TYPE_PPPoE):
                *parseArrayOffset = CC_PC_PR_PPPOE_OFFSET;
                break;
            case (HEADER_TYPE_MPLS):
                if ((hdrIndex == e_FM_PCD_HDR_INDEX_NONE)
                        || (hdrIndex == e_FM_PCD_HDR_INDEX_1))
                    *parseArrayOffset = CC_PC_PR_MPLS1_OFFSET;
                else
                    if (hdrIndex == e_FM_PCD_HDR_INDEX_LAST)
                        *parseArrayOffset = CC_PC_PR_MPLS_LAST_OFFSET;
                break;
            case (HEADER_TYPE_IPv4):
            case (HEADER_TYPE_IPv6):
                if ((hdrIndex == e_FM_PCD_HDR_INDEX_NONE)
                        || (hdrIndex == e_FM_PCD_HDR_INDEX_1))
                    *parseArrayOffset = CC_PC_PR_IP1_OFFSET;
                else
                    if (hdrIndex == e_FM_PCD_HDR_INDEX_2)
                        *parseArrayOffset = CC_PC_PR_IP_LAST_OFFSET;
                break;
            case (HEADER_TYPE_MINENCAP):
                *parseArrayOffset = CC_PC_PR_MINENC_OFFSET;
                break;
            case (HEADER_TYPE_GRE):
                *parseArrayOffset = CC_PC_PR_GRE_OFFSET;
                break;
            case (HEADER_TYPE_TCP):
            case (HEADER_TYPE_UDP):
            case (HEADER_TYPE_IPSEC_AH):
            case (HEADER_TYPE_IPSEC_ESP):
            case (HEADER_TYPE_DCCP):
            case (HEADER_TYPE_SCTP):
                *parseArrayOffset = CC_PC_PR_L4_OFFSET;
                break;
            case (HEADER_TYPE_CAPWAP):
            case (HEADER_TYPE_CAPWAP_DTLS):
                *parseArrayOffset = CC_PC_PR_NEXT_HEADER_OFFSET;
                break;
            default:
                RETURN_ERROR(
                        MAJOR,
                        E_NOT_SUPPORTED,
                        ("Header manipulation of this header is not supported"));
        }
    }
    return E_OK;
}

static t_Error BuildHmct(t_FmPcdManip *p_Manip,
                         t_FmPcdManipParams *p_FmPcdManipParams,
                         uint8_t *p_DestHmct, uint8_t *p_DestData, bool new)
{
    uint32_t *p_TmpHmct = (uint32_t*)p_DestHmct, *p_LocalData;
    uint32_t tmpReg = 0, *p_Last = NULL, tmp_ipv6_addr;
    uint8_t remain, i, size = 0, origSize, *p_UsrData = NULL, *p_TmpData =
            p_DestData;
    t_Handle h_FmPcd = p_Manip->h_FmPcd;
    uint8_t j = 0;

    if (p_FmPcdManipParams->u.hdr.rmv)
    {
        if (p_FmPcdManipParams->u.hdr.rmvParams.type
                == e_FM_PCD_MANIP_RMV_GENERIC)
        {
            /* initialize HMCD */
            tmpReg = (uint32_t)(HMCD_OPCODE_GENERIC_RMV) << HMCD_OC_SHIFT;
            /* tmp, should be conditional */
            tmpReg |= p_FmPcdManipParams->u.hdr.rmvParams.u.generic.offset
                    << HMCD_RMV_OFFSET_SHIFT;
            tmpReg |= p_FmPcdManipParams->u.hdr.rmvParams.u.generic.size
                    << HMCD_RMV_SIZE_SHIFT;
        }
        else
            if (p_FmPcdManipParams->u.hdr.rmvParams.type
                    == e_FM_PCD_MANIP_RMV_BY_HDR)
            {
                switch (p_FmPcdManipParams->u.hdr.rmvParams.u.byHdr.type)
                {
                    case (e_FM_PCD_MANIP_RMV_BY_HDR_SPECIFIC_L2):
                    {
                        uint8_t hmcdOpt;

                        /* initialize HMCD */
                        tmpReg = (uint32_t)(HMCD_OPCODE_L2_RMV) << HMCD_OC_SHIFT;

                        switch (p_FmPcdManipParams->u.hdr.rmvParams.u.byHdr.u.specificL2)
                        {
                            case (e_FM_PCD_MANIP_HDR_RMV_ETHERNET):
                                hmcdOpt = HMCD_RMV_L2_ETHERNET;
                                break;
                            case (e_FM_PCD_MANIP_HDR_RMV_STACKED_QTAGS):
                                hmcdOpt = HMCD_RMV_L2_STACKED_QTAGS;
                                break;
                            case (e_FM_PCD_MANIP_HDR_RMV_ETHERNET_AND_MPLS):
                                hmcdOpt = HMCD_RMV_L2_ETHERNET_AND_MPLS;
                                break;
                            case (e_FM_PCD_MANIP_HDR_RMV_MPLS):
                                hmcdOpt = HMCD_RMV_L2_MPLS;
                                break;
                            case (e_FM_PCD_MANIP_HDR_RMV_PPPOE):
                                hmcdOpt = HMCD_RMV_L2_PPPOE;
                                break;
                            default:
                                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
                        }
                        tmpReg |= hmcdOpt << HMCD_L2_MODE_SHIFT;
                        break;
                    }
#if (DPAA_VERSION >= 11)
                    case (e_FM_PCD_MANIP_RMV_BY_HDR_CAPWAP):
                        tmpReg = (uint32_t)(HMCD_OPCODE_CAPWAP_RMV)
                                << HMCD_OC_SHIFT;
                        break;
                    case (e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START):
                    {
                        uint8_t prsArrayOffset;
                        t_Error err = E_OK;

                        tmpReg = (uint32_t)(HMCD_OPCODE_RMV_TILL)
                                << HMCD_OC_SHIFT;

                        err =
                                GetPrOffsetByHeaderOrField(
                                        &p_FmPcdManipParams->u.hdr.rmvParams.u.byHdr.u.hdrInfo,
                                        &prsArrayOffset);
                        ASSERT_COND(!err);
                        /* was previously checked */

                        tmpReg |= ((uint32_t)prsArrayOffset << 16);
                    }
                        break;
#endif /* (DPAA_VERSION >= 11) */
                    default:
                        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                                     ("manip header remove by hdr type!"));
                }
            }

        WRITE_UINT32(*p_TmpHmct, tmpReg);
        /* save a pointer to the "last" indication word */
        p_Last = p_TmpHmct;
        /* advance to next command */
        p_TmpHmct += HMCD_BASIC_SIZE / 4;
    }

    if (p_FmPcdManipParams->u.hdr.insrt)
    {
        if (p_FmPcdManipParams->u.hdr.insrtParams.type
                == e_FM_PCD_MANIP_INSRT_GENERIC)
        {
            /* initialize HMCD */
            if (p_FmPcdManipParams->u.hdr.insrtParams.u.generic.replace)
                tmpReg = (uint32_t)(HMCD_OPCODE_GENERIC_REPLACE)
                        << HMCD_OC_SHIFT;
            else
                tmpReg = (uint32_t)(HMCD_OPCODE_GENERIC_INSRT) << HMCD_OC_SHIFT;

            tmpReg |= p_FmPcdManipParams->u.hdr.insrtParams.u.generic.offset
                    << HMCD_INSRT_OFFSET_SHIFT;
            tmpReg |= p_FmPcdManipParams->u.hdr.insrtParams.u.generic.size
                    << HMCD_INSRT_SIZE_SHIFT;

            size = p_FmPcdManipParams->u.hdr.insrtParams.u.generic.size;
            p_UsrData = p_FmPcdManipParams->u.hdr.insrtParams.u.generic.p_Data;

            WRITE_UINT32(*p_TmpHmct, tmpReg);
            /* save a pointer to the "last" indication word */
            p_Last = p_TmpHmct;

            p_TmpHmct += HMCD_BASIC_SIZE / 4;

            /* initialize data to be inserted */
            /* if size is not a multiple of 4, padd with 0's */
            origSize = size;
            remain = (uint8_t)(size % 4);
            if (remain)
            {
                size += (uint8_t)(4 - remain);
                p_LocalData = (uint32_t *)XX_Malloc(size);
                memset((uint8_t *)p_LocalData, 0, size);
                memcpy((uint8_t *)p_LocalData, p_UsrData, origSize);
            }
            else
                p_LocalData = (uint32_t*)p_UsrData;

            /* initialize data and advance pointer to next command */
            MemCpy8(p_TmpHmct, p_LocalData, size);
            p_TmpHmct += size / sizeof(uint32_t);

            if (remain)
                XX_Free(p_LocalData);
        }

        else
            if (p_FmPcdManipParams->u.hdr.insrtParams.type
                    == e_FM_PCD_MANIP_INSRT_BY_HDR)
            {
                switch (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.type)
                {
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_SPECIFIC_L2):
                    {
                        uint8_t hmcdOpt;

                        /* initialize HMCD */
                        tmpReg = (uint32_t)(HMCD_OPCODE_L2_INSRT)
                                << HMCD_OC_SHIFT;

                        switch (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.specificL2Params.specificL2)
                        {
                            case (e_FM_PCD_MANIP_HDR_INSRT_MPLS):
                                if (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.specificL2Params.update)
                                    hmcdOpt = HMCD_INSRT_N_UPDATE_L2_MPLS;
                                else
                                    hmcdOpt = HMCD_INSRT_L2_MPLS;
                                break;
                            case (e_FM_PCD_MANIP_HDR_INSRT_PPPOE):
                                hmcdOpt = HMCD_INSRT_L2_PPPOE;
                                break;
                            default:
                                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
                        }
                        tmpReg |= hmcdOpt << HMCD_L2_MODE_SHIFT;

                        WRITE_UINT32(*p_TmpHmct, tmpReg);
                        /* save a pointer to the "last" indication word */
                        p_Last = p_TmpHmct;

                        p_TmpHmct += HMCD_BASIC_SIZE / 4;

                        /* set size and pointer of user's data */
                        size =
                                (uint8_t)p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.specificL2Params.size;

                        ASSERT_COND(p_TmpData);
                        MemCpy8(
                                p_TmpData,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.specificL2Params.p_Data,
                                size);
                        tmpReg =
                                (size << HMCD_INSRT_L2_SIZE_SHIFT)
                                        | (uint32_t)(XX_VirtToPhys(p_TmpData)
                                                - (((t_FmPcd*)h_FmPcd)->physicalMuramBase));
                        WRITE_UINT32(*p_TmpHmct, tmpReg);
                        p_TmpHmct += HMCD_PTR_SIZE / 4;
                        p_TmpData += size;
                    }
                        break;
#if (DPAA_VERSION >= 11)
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_IP):
                        tmpReg = (uint32_t)(HMCD_OPCODE_IP_INSRT)
                                << HMCD_OC_SHIFT;
                        if (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.calcL4Checksum)
                            tmpReg |= HMCD_IP_L4_CS_CALC;
                        if (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.mappingMode
                                == e_FM_PCD_MANIP_HDR_QOS_MAPPING_AS_IS)
                            tmpReg |= HMCD_IP_OR_QOS;
                        tmpReg |=
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.lastPidOffset
                                        & HMCD_IP_LAST_PID_MASK;
                        tmpReg |=
                                ((p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.size
                                        << HMCD_IP_SIZE_SHIFT)
                                        & HMCD_IP_SIZE_MASK);
                        if (p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.dontFragOverwrite)
                            tmpReg |= HMCD_IP_DF_MODE;

                        WRITE_UINT32(*p_TmpHmct, tmpReg);

                        /* save a pointer to the "last" indication word */
                        p_Last = p_TmpHmct;

                        p_TmpHmct += HMCD_BASIC_SIZE / 4;

                        /* set IP id */
                        ASSERT_COND(p_TmpData);
                        WRITE_UINT16(
                                *(uint16_t*)p_TmpData,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.id);
                        WRITE_UINT32(
                                *p_TmpHmct,
                                (uint32_t)(XX_VirtToPhys(p_TmpData) - (((t_FmPcd*)p_Manip->h_FmPcd)->physicalMuramBase)));
                        p_TmpData += 2;
                        p_TmpHmct += HMCD_PTR_SIZE / 4;

                        WRITE_UINT8(*p_TmpHmct, p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.lastDstOffset);
                        p_TmpHmct += HMCD_PARAM_SIZE / 4;

                        MemCpy8(
                                p_TmpHmct,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.p_Data,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.size);
                        p_TmpHmct +=
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.size
                                        / 4;
                        break;
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_UDP_LITE):
                        tmpReg = HMCD_INSRT_UDP_LITE;
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_UDP):
                        tmpReg |= (uint32_t)(HMCD_OPCODE_UDP_INSRT)
                                << HMCD_OC_SHIFT;

                        WRITE_UINT32(*p_TmpHmct, tmpReg);

                        /* save a pointer to the "last" indication word */
                        p_Last = p_TmpHmct;

                        p_TmpHmct += HMCD_BASIC_SIZE / 4;

                        MemCpy8(
                                p_TmpHmct,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.p_Data,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size);
                        p_TmpHmct +=
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size
                                        / 4;
                        break;
                    case (e_FM_PCD_MANIP_INSRT_BY_HDR_CAPWAP):
                        tmpReg = (uint32_t)(HMCD_OPCODE_CAPWAP_INSRT)
                                << HMCD_OC_SHIFT;
                        tmpReg |= HMCD_CAPWAP_INSRT;

                        WRITE_UINT32(*p_TmpHmct, tmpReg);

                        /* save a pointer to the "last" indication word */
                        p_Last = p_TmpHmct;

                        p_TmpHmct += HMCD_BASIC_SIZE / 4;

                        MemCpy8(
                                p_TmpHmct,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.p_Data,
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size);
                        p_TmpHmct +=
                                p_FmPcdManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size
                                        / 4;
                        break;
#endif /* (DPAA_VERSION >= 11) */
                    default:
                        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                                     ("manip header insert by header type!"));

                }
            }
    }

    if (p_FmPcdManipParams->u.hdr.fieldUpdate)
    {
        switch (p_FmPcdManipParams->u.hdr.fieldUpdateParams.type)
        {
            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN):
                /* set opcode */
                tmpReg = (uint32_t)(HMCD_OPCODE_VLAN_PRI_UPDATE)
                        << HMCD_OC_SHIFT;

                /* set mode & table pointer */
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.vlan.updateType
                        == e_FM_PCD_MANIP_HDR_FIELD_UPDATE_DSCP_TO_VLAN)
                {
                    /* set Mode */
                    tmpReg |= (uint32_t)(HMCD_VLAN_PRI_UPDATE_DSCP_TO_VPRI)
                            << HMCD_VLAN_PRI_REP_MODE_SHIFT;
                    /* set VPRI default */
                    tmpReg |=
                            p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.vlan.u.dscpToVpri.vpriDefVal;
                    WRITE_UINT32(*p_TmpHmct, tmpReg);
                    /* save a pointer to the "last" indication word */
                    p_Last = p_TmpHmct;
                    /* write the table pointer into the Manip descriptor */
                    p_TmpHmct += HMCD_BASIC_SIZE / 4;

                    tmpReg = 0;
                    ASSERT_COND(p_TmpData);
                    for (i = 0; i < HMCD_DSCP_VALUES; i++)
                    {
                        /* first we build from each 8 values a 32bit register */
                        tmpReg |=
                                (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.vlan.u.dscpToVpri.dscpToVpriTable[i])
                                        << (32 - 4 * (j + 1));
                        j++;
                        /* Than we write this register to the next table word
                         * (i=7-->word 0, i=15-->word 1,... i=63-->word 7) */
                        if ((i % 8) == 7)
                        {
                            WRITE_UINT32(*((uint32_t*)p_TmpData + (i+1)/8-1),
                                         tmpReg);
                            tmpReg = 0;
                            j = 0;
                        }
                    }

                    WRITE_UINT32(
                            *p_TmpHmct,
                            (uint32_t)(XX_VirtToPhys(p_TmpData) - (((t_FmPcd*)h_FmPcd)->physicalMuramBase)));
                    p_TmpHmct += HMCD_PTR_SIZE / 4;

                    p_TmpData += DSCP_TO_VLAN_TABLE_SIZE;
                }
                else
                    if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.vlan.updateType
                            == e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN_VPRI)
                    {
                        /* set Mode */
                        /* line commented out as it has no-side-effect ('0' value). */
                        /*tmpReg |= HMCD_VLAN_PRI_UPDATE << HMCD_VLAN_PRI_REP_MODE_SHIFT*/;
                        /* set VPRI parameter */
                        tmpReg |=
                                p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.vlan.u.vpri;
                        WRITE_UINT32(*p_TmpHmct, tmpReg);
                        /* save a pointer to the "last" indication word */
                        p_Last = p_TmpHmct;
                        p_TmpHmct += HMCD_BASIC_SIZE / 4;
                    }
                break;

            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV4):
                /* set opcode */
                tmpReg = (uint32_t)(HMCD_OPCODE_IPV4_UPDATE) << HMCD_OC_SHIFT;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_TTL)
                    tmpReg |= HMCD_IPV4_UPDATE_TTL;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_TOS)
                {
                    tmpReg |= HMCD_IPV4_UPDATE_TOS;
                    tmpReg |=
                            p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.tos
                                    << HMCD_IPV4_UPDATE_TOS_SHIFT;
                }
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_ID)
                    tmpReg |= HMCD_IPV4_UPDATE_ID;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_SRC)
                    tmpReg |= HMCD_IPV4_UPDATE_SRC;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_DST)
                    tmpReg |= HMCD_IPV4_UPDATE_DST;
                /* write the first 4 bytes of the descriptor */
                WRITE_UINT32(*p_TmpHmct, tmpReg);
                /* save a pointer to the "last" indication word */
                p_Last = p_TmpHmct;

                p_TmpHmct += HMCD_BASIC_SIZE / 4;

                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_ID)
                {
                    ASSERT_COND(p_TmpData);
                    WRITE_UINT16(
                            *(uint16_t*)p_TmpData,
                            p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.id);
                    WRITE_UINT32(
                            *p_TmpHmct,
                            (uint32_t)(XX_VirtToPhys(p_TmpData) - (((t_FmPcd*)p_Manip->h_FmPcd)->physicalMuramBase)));
                    p_TmpData += 2;
                    p_TmpHmct += HMCD_PTR_SIZE / 4;
                }

                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_SRC)
                {
                    WRITE_UINT32(
                            *p_TmpHmct,
                            p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.src);
                    p_TmpHmct += HMCD_IPV4_ADDR_SIZE / 4;
                }

                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.validUpdates
                        & HDR_MANIP_IPV4_DST)
                {
                    WRITE_UINT32(
                            *p_TmpHmct,
                            p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv4.dst);
                    p_TmpHmct += HMCD_IPV4_ADDR_SIZE / 4;
                }
                break;

            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV6):
                /* set opcode */
                tmpReg = (uint32_t)(HMCD_OPCODE_IPV6_UPDATE) << HMCD_OC_SHIFT;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.validUpdates
                        & HDR_MANIP_IPV6_HL)
                    tmpReg |= HMCD_IPV6_UPDATE_HL;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.validUpdates
                        & HDR_MANIP_IPV6_TC)
                {
                    tmpReg |= HMCD_IPV6_UPDATE_TC;
                    tmpReg |=
                            p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.trafficClass
                                    << HMCD_IPV6_UPDATE_TC_SHIFT;
                }
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.validUpdates
                        & HDR_MANIP_IPV6_SRC)
                    tmpReg |= HMCD_IPV6_UPDATE_SRC;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.validUpdates
                        & HDR_MANIP_IPV6_DST)
                    tmpReg |= HMCD_IPV6_UPDATE_DST;
                /* write the first 4 bytes of the descriptor */
                WRITE_UINT32(*p_TmpHmct, tmpReg);
                /* save a pointer to the "last" indication word */
                p_Last = p_TmpHmct;

                p_TmpHmct += HMCD_BASIC_SIZE / 4;
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.validUpdates
                        & HDR_MANIP_IPV6_SRC)
                {
                    for (i = 0; i < NET_HEADER_FIELD_IPv6_ADDR_SIZE; i += 4)
                    {
                        memcpy(&tmp_ipv6_addr,
                               &p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.src[i],
                               sizeof(uint32_t));
                        WRITE_UINT32(*p_TmpHmct, tmp_ipv6_addr);
                        p_TmpHmct += HMCD_PTR_SIZE / 4;
                    }
                }
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.validUpdates
                        & HDR_MANIP_IPV6_DST)
                {
                    for (i = 0; i < NET_HEADER_FIELD_IPv6_ADDR_SIZE; i += 4)
                    {
                        memcpy(&tmp_ipv6_addr,
                               &p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.ipv6.dst[i],
                               sizeof(uint32_t));
                        WRITE_UINT32(*p_TmpHmct, tmp_ipv6_addr);
                        p_TmpHmct += HMCD_PTR_SIZE / 4;
                    }
                }
                break;

            case (e_FM_PCD_MANIP_HDR_FIELD_UPDATE_TCP_UDP):
                if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.validUpdates
                        == HDR_MANIP_TCP_UDP_CHECKSUM)
                {
                    /* we implement this case with the update-checksum descriptor */
                    /* set opcode */
                    tmpReg = (uint32_t)(HMCD_OPCODE_TCP_UDP_CHECKSUM)
                            << HMCD_OC_SHIFT;
                    /* write the first 4 bytes of the descriptor */
                    WRITE_UINT32(*p_TmpHmct, tmpReg);
                    /* save a pointer to the "last" indication word */
                    p_Last = p_TmpHmct;

                    p_TmpHmct += HMCD_BASIC_SIZE / 4;
                }
                else
                {
                    /* we implement this case with the TCP/UDP update descriptor */
                    /* set opcode */
                    tmpReg = (uint32_t)(HMCD_OPCODE_TCP_UDP_UPDATE)
                            << HMCD_OC_SHIFT;
                    if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.validUpdates
                            & HDR_MANIP_TCP_UDP_DST)
                        tmpReg |= HMCD_TCP_UDP_UPDATE_DST;
                    if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.validUpdates
                            & HDR_MANIP_TCP_UDP_SRC)
                        tmpReg |= HMCD_TCP_UDP_UPDATE_SRC;
                    /* write the first 4 bytes of the descriptor */
                    WRITE_UINT32(*p_TmpHmct, tmpReg);
                    /* save a pointer to the "last" indication word */
                    p_Last = p_TmpHmct;

                    p_TmpHmct += HMCD_BASIC_SIZE / 4;

                    tmpReg = 0;
                    if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.validUpdates
                            & HDR_MANIP_TCP_UDP_SRC)
                        tmpReg |=
                                ((uint32_t)p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.src)
                                        << HMCD_TCP_UDP_UPDATE_SRC_SHIFT;
                    if (p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.validUpdates
                            & HDR_MANIP_TCP_UDP_DST)
                        tmpReg |=
                                ((uint32_t)p_FmPcdManipParams->u.hdr.fieldUpdateParams.u.tcpUdp.dst);
                    WRITE_UINT32(*p_TmpHmct, tmpReg);
                    p_TmpHmct += HMCD_PTR_SIZE / 4;
                }
                break;

            default:
                RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                             ("Unknown fieldUpdateParams.type"));
        }
    }

    if (p_FmPcdManipParams->u.hdr.custom)
    {
        switch (p_FmPcdManipParams->u.hdr.customParams.type)
        {
            case (e_FM_PCD_MANIP_HDR_CUSTOM_IP_REPLACE):
                /* set opcode */
                tmpReg = (uint32_t)(HMCD_OPCODE_REPLACE_IP) << HMCD_OC_SHIFT;

                if (p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.decTtlHl)
                    tmpReg |= HMCD_IP_REPLACE_TTL_HL;
                if (p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.replaceType
                        == e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV4_BY_IPV6)
                    /* line commented out as it has no-side-effect ('0' value). */
                    /*tmpReg |= HMCD_IP_REPLACE_REPLACE_IPV4*/;
                else
                    if (p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.replaceType
                            == e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV6_BY_IPV4)
                    {
                        tmpReg |= HMCD_IP_REPLACE_REPLACE_IPV6;
                        if (p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.updateIpv4Id)
                            tmpReg |= HMCD_IP_REPLACE_ID;
                    }
                    else
                        RETURN_ERROR(
                                MINOR,
                                E_NOT_SUPPORTED,
                                ("One flag out of HDR_MANIP_IP_REPLACE_IPV4, HDR_MANIP_IP_REPLACE_IPV6 - must be set."));

                /* write the first 4 bytes of the descriptor */
                WRITE_UINT32(*p_TmpHmct, tmpReg);
                /* save a pointer to the "last" indication word */
                p_Last = p_TmpHmct;

                p_TmpHmct += HMCD_BASIC_SIZE / 4;

                size =
                        p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.hdrSize;
                ASSERT_COND(p_TmpData);
                MemCpy8(
                        p_TmpData,
                        p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.hdr,
                        size);
                tmpReg = (uint32_t)(size << HMCD_IP_REPLACE_L3HDRSIZE_SHIFT);
                tmpReg |= (uint32_t)(XX_VirtToPhys(p_TmpData)
                        - (((t_FmPcd*)h_FmPcd)->physicalMuramBase));
                WRITE_UINT32(*p_TmpHmct, tmpReg);
                p_TmpHmct += HMCD_PTR_SIZE / 4;
                p_TmpData += size;

                if ((p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.replaceType
                        == e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV6_BY_IPV4)
                        && (p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.updateIpv4Id))
                {
                    WRITE_UINT16(
                            *(uint16_t*)p_TmpData,
                            p_FmPcdManipParams->u.hdr.customParams.u.ipHdrReplace.id);
                    WRITE_UINT32(
                            *p_TmpHmct,
                            (uint32_t)(XX_VirtToPhys(p_TmpData) - (((t_FmPcd*)h_FmPcd)->physicalMuramBase)));
                    p_TmpData += 2;
                }
                p_TmpHmct += HMCD_PTR_SIZE / 4;
                break;
            case (e_FM_PCD_MANIP_HDR_CUSTOM_GEN_FIELD_REPLACE):
                /* set opcode */
                tmpReg = (uint32_t)(HMCD_OPCODE_GEN_FIELD_REPLACE) << HMCD_OC_SHIFT;
                tmpReg |= p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.size << HMCD_GEN_FIELD_SIZE_SHIFT;
                tmpReg |= p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.srcOffset << HMCD_GEN_FIELD_SRC_OFF_SHIFT;
                tmpReg |= p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.dstOffset << HMCD_GEN_FIELD_DST_OFF_SHIFT;
                if (p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.mask)
                    tmpReg |= HMCD_GEN_FIELD_MASK_EN;

                /* write the first 4 bytes of the descriptor */
                WRITE_UINT32(*p_TmpHmct, tmpReg);
                /* save a pointer to the "last" indication word */
                p_Last = p_TmpHmct;

                p_TmpHmct += HMCD_BASIC_SIZE/4;

                if (p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.mask)
                {
                    tmpReg = p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.mask << HMCD_GEN_FIELD_MASK_SHIFT;
                    tmpReg |= p_FmPcdManipParams->u.hdr.customParams.u.genFieldReplace.maskOffset << HMCD_GEN_FIELD_MASK_OFF_SHIFT;
                    /* write the next 4 bytes of the descriptor */
                    WRITE_UINT32(*p_TmpHmct, tmpReg);
                }
                p_TmpHmct += HMCD_PARAM_SIZE/4;
                break;
            default:
                RETURN_ERROR(MINOR, E_INVALID_SELECTION,
                             ("Unknown customParams.type"));
        }
    }

    /* If this node has a nextManip, and no parsing is required, the old table must be copied to the new table
     the old table and should be freed */
    if (p_FmPcdManipParams->h_NextManip
            && (p_Manip->nextManipType == e_FM_PCD_MANIP_HDR)
            && (MANIP_DONT_REPARSE(p_Manip)))
    {
        if (new)
        {
            /* If this is the first time this manip is created we need to free unused memory. If it
             * is a dynamic changes case, the memory used is either the CC shadow or the existing
             * table - no allocation, no free */
            MANIP_UPDATE_UNIFIED_POSITION(p_FmPcdManipParams->h_NextManip);

            p_Manip->unifiedPosition = e_MANIP_UNIFIED_FIRST;
        }
    }
    else
    {
        ASSERT_COND(p_Last);
        /* set the "last" indication on the last command of the current table */
        WRITE_UINT32(*p_Last, GET_UINT32(*p_Last) | HMCD_LAST);
    }

    return E_OK;
}

static t_Error CreateManipActionNew(t_FmPcdManip *p_Manip,
                                    t_FmPcdManipParams *p_FmPcdManipParams)
{
    t_FmPcdManip *p_CurManip;
    t_Error err;
    uint32_t nextSize = 0, totalSize;
    uint16_t tmpReg;
    uint8_t *p_OldHmct, *p_TmpHmctPtr, *p_TmpDataPtr;

    /* set Manip structure */

    p_Manip->dontParseAfterManip =
            p_FmPcdManipParams->u.hdr.dontParseAfterManip;

    if (p_FmPcdManipParams->h_NextManip)
    {   /* Next Header manipulation exists */
        p_Manip->nextManipType = MANIP_GET_TYPE(p_FmPcdManipParams->h_NextManip);

        if ((p_Manip->nextManipType == e_FM_PCD_MANIP_HDR) && p_Manip->dontParseAfterManip)
            nextSize = (uint32_t)(GetHmctSize(p_FmPcdManipParams->h_NextManip)
                    + GetDataSize(p_FmPcdManipParams->h_NextManip));
        else /* either parsing is required or next manip is Frag; no table merging. */
            p_Manip->cascaded = TRUE;
        /* pass up the "cascaded" attribute. The whole chain is cascaded
         * if something is cascaded along the way. */
        if (MANIP_IS_CASCADED(p_FmPcdManipParams->h_NextManip))
            p_Manip->cascaded = TRUE;
    }

    /* Allocate new table */
    /* calculate table size according to manip parameters */
    err = CalculateTableSize(p_FmPcdManipParams, &p_Manip->tableSize,
                             &p_Manip->dataSize);
    if (err)
        RETURN_ERROR(MINOR, err, NO_MSG);

    totalSize = (uint16_t)(p_Manip->tableSize + p_Manip->dataSize + nextSize);

    p_Manip->p_Hmct = (uint8_t*)FM_MURAM_AllocMem(
            ((t_FmPcd *)p_Manip->h_FmPcd)->h_FmMuram, totalSize, 4);
    if (!p_Manip->p_Hmct)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc failed"));

    if (p_Manip->dataSize)
        p_Manip->p_Data =
                (uint8_t*)PTR_MOVE(p_Manip->p_Hmct, (p_Manip->tableSize + nextSize));

    /* update shadow size to allow runtime replacement of Header manipulation */
    /* The allocated shadow is divided as follows:
     0 . . .       16 . . .
     --------------------------------
     |   Shadow   |   Shadow HMTD   |
     |   HMTD     |   Match Table   |
     | (16 bytes) | (maximal size)  |
     --------------------------------
     */

    err = FmPcdUpdateCcShadow(p_Manip->h_FmPcd, (uint32_t)(totalSize + 16),
                              (uint16_t)FM_PCD_CC_AD_TABLE_ALIGN);
    if (err != E_OK)
    {
        FM_MURAM_FreeMem(p_Manip->h_FmPcd, p_Manip->p_Hmct);
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM allocation for HdrManip node shadow"));
    }

    if (p_FmPcdManipParams->h_NextManip
            && (p_Manip->nextManipType == e_FM_PCD_MANIP_HDR)
            && (MANIP_DONT_REPARSE(p_Manip)))
    {
        p_OldHmct = (uint8_t *)GetManipInfo(p_FmPcdManipParams->h_NextManip,
                                            e_MANIP_HMCT);
        p_CurManip = p_FmPcdManipParams->h_NextManip;
        /* Run till the last Manip (which is the first to configure) */
        while (MANIP_IS_UNIFIED_NON_LAST(p_CurManip))
            p_CurManip = p_CurManip->h_NextManip;

        while (p_CurManip)
        {
            /* If this is a unified table, point to the part of the table
             * which is the relative offset in HMCT.
             */
            p_TmpHmctPtr = (uint8_t*)PTR_MOVE(p_Manip->p_Hmct,
                    (p_Manip->tableSize +
                            (PTR_TO_UINT(p_CurManip->p_Hmct) -
                                    PTR_TO_UINT(p_OldHmct))));
            if (p_CurManip->p_Data)
                p_TmpDataPtr = (uint8_t*)PTR_MOVE(p_Manip->p_Hmct,
                        (p_Manip->tableSize +
                                (PTR_TO_UINT(p_CurManip->p_Data) -
                                        PTR_TO_UINT(p_OldHmct))));
            else
                p_TmpDataPtr = NULL;

            BuildHmct(p_CurManip, &p_CurManip->manipParams, p_TmpHmctPtr,
                      p_TmpDataPtr, FALSE);
            /* update old manip table pointer */
            MANIP_SET_HMCT_PTR(p_CurManip, p_TmpHmctPtr);
            MANIP_SET_DATA_PTR(p_CurManip, p_TmpDataPtr);

            p_CurManip = p_CurManip->h_PrevManip;
        }
        /* We copied the HMCT to create a new large HMCT so we can free the old one */
        FM_MURAM_FreeMem(MANIP_GET_MURAM(p_FmPcdManipParams->h_NextManip),
                         p_OldHmct);
    }

    /* Fill table */
    err = BuildHmct(p_Manip, p_FmPcdManipParams, p_Manip->p_Hmct,
                    p_Manip->p_Data, TRUE);
    if (err)
    {
        FM_MURAM_FreeMem(p_Manip->h_FmPcd, p_Manip->p_Hmct);
        RETURN_ERROR(MINOR, err, NO_MSG);
    }

    /* Build HMTD (table descriptor) */
     tmpReg = HMTD_CFG_TYPE; /* NADEN = 0 */

     /* add parseAfterManip */
      if (!p_Manip->dontParseAfterManip)
          tmpReg |= HMTD_CFG_PRS_AFTER_HM;

    /* create cascade */
    /*if (p_FmPcdManipParams->h_NextManip
            && (!MANIP_DONT_REPARSE(p_Manip) || (p_Manip->nextManipType != e_FM_PCD_MANIP_HDR)))*/
    if (p_Manip->cascaded)
    {
        uint16_t nextAd;
        /* indicate that there's another HM table descriptor */
        tmpReg |= HMTD_CFG_NEXT_AD_EN;
        /* get address of next HMTD (table descriptor; h_Ad).
         * If the next HMTD was removed due to table unifing, get the address
         * of the "next next" as written in the h_Ad of the next h_Manip node.
         */
        if (p_Manip->unifiedPosition != e_MANIP_UNIFIED_FIRST)
            nextAd = (uint16_t)((uint32_t)(XX_VirtToPhys(MANIP_GET_HMTD_PTR(p_FmPcdManipParams->h_NextManip)) - (((t_FmPcd*)p_Manip->h_FmPcd)->physicalMuramBase)) >> 4);
        else
            nextAd = ((t_Hmtd *)((t_FmPcdManip *)p_FmPcdManipParams->h_NextManip)->h_Ad)->nextAdIdx;

        WRITE_UINT16(((t_Hmtd *)p_Manip->h_Ad)->nextAdIdx, nextAd);
    }

    WRITE_UINT16(((t_Hmtd *)p_Manip->h_Ad)->cfg, tmpReg);
    WRITE_UINT32(
            ((t_Hmtd *)p_Manip->h_Ad)->hmcdBasePtr,
            (uint32_t)(XX_VirtToPhys(p_Manip->p_Hmct) - (((t_FmPcd*)p_Manip->h_FmPcd)->physicalMuramBase)));

    WRITE_UINT8(((t_Hmtd *)p_Manip->h_Ad)->opCode, HMAN_OC);

    if (p_Manip->unifiedPosition == e_MANIP_UNIFIED_FIRST)
    {
        /* The HMTD of the next Manip is never going to be used */
        if (((t_FmPcdManip *)p_FmPcdManipParams->h_NextManip)->muramAllocate)
            FM_MURAM_FreeMem(
                    ((t_FmPcd *)((t_FmPcdManip *)p_FmPcdManipParams->h_NextManip)->h_FmPcd)->h_FmMuram,
                    ((t_FmPcdManip *)p_FmPcdManipParams->h_NextManip)->h_Ad);
        else
            XX_Free(((t_FmPcdManip *)p_FmPcdManipParams->h_NextManip)->h_Ad);
        ((t_FmPcdManip *)p_FmPcdManipParams->h_NextManip)->h_Ad = NULL;
    }

    return E_OK;
}

static t_Error CreateManipActionShadow(t_FmPcdManip *p_Manip,
                                       t_FmPcdManipParams *p_FmPcdManipParams)
{
    uint8_t *p_WholeHmct, *p_TmpHmctPtr, newDataSize, *p_TmpDataPtr = NULL;
    uint16_t newSize;
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_Manip->h_FmPcd;
    t_Error err;
    t_FmPcdManip *p_CurManip = p_Manip;

    err = CalculateTableSize(p_FmPcdManipParams, &newSize, &newDataSize);
    if (err)
        RETURN_ERROR(MINOR, err, NO_MSG);

    /* check coherency of new table parameters */
    if (newSize > p_Manip->tableSize)
        RETURN_ERROR(
                MINOR,
                E_INVALID_VALUE,
                ("New Hdr Manip configuration requires larger size than current one (command table)."));
    if (newDataSize > p_Manip->dataSize)
        RETURN_ERROR(
                MINOR,
                E_INVALID_VALUE,
                ("New Hdr Manip configuration requires larger size than current one (data)."));
    if (p_FmPcdManipParams->h_NextManip)
        RETURN_ERROR(
                MINOR, E_INVALID_VALUE,
                ("New Hdr Manip configuration can not contain h_NextManip."));
    if (MANIP_IS_UNIFIED(p_Manip) && (newSize != p_Manip->tableSize))
        RETURN_ERROR(
                MINOR,
                E_INVALID_VALUE,
                ("New Hdr Manip configuration in a chained manipulation requires different size than current one."));
    if (p_Manip->dontParseAfterManip
            != p_FmPcdManipParams->u.hdr.dontParseAfterManip)
        RETURN_ERROR(
                MINOR,
                E_INVALID_VALUE,
                ("New Hdr Manip configuration differs in dontParseAfterManip value."));

    p_Manip->tableSize = newSize;
    p_Manip->dataSize = newDataSize;

    /* Build the new table in the shadow */
    if (!MANIP_IS_UNIFIED(p_Manip))
    {
        p_TmpHmctPtr = (uint8_t*)PTR_MOVE(p_FmPcd->p_CcShadow, 16);
        if (p_Manip->p_Data)
            p_TmpDataPtr =
                    (uint8_t*)PTR_MOVE(p_TmpHmctPtr,
                            (PTR_TO_UINT(p_Manip->p_Data) - PTR_TO_UINT(p_Manip->p_Hmct)));

        BuildHmct(p_Manip, p_FmPcdManipParams, p_TmpHmctPtr, p_Manip->p_Data,
                  FALSE);
    }
    else
    {
        p_WholeHmct = (uint8_t *)GetManipInfo(p_Manip, e_MANIP_HMCT);
        ASSERT_COND(p_WholeHmct);

        /* Run till the last Manip (which is the first to configure) */
        while (MANIP_IS_UNIFIED_NON_LAST(p_CurManip))
            p_CurManip = p_CurManip->h_NextManip;

        while (p_CurManip)
        {
            /* If this is a non-head node in a unified table, point to the part of the shadow
             * which is the relative offset in HMCT.
             * else, point to the beginning of the
             * shadow table (we save 16 for the HMTD.
             */
            p_TmpHmctPtr =
                    (uint8_t*)PTR_MOVE(p_FmPcd->p_CcShadow,
                            (16 + PTR_TO_UINT(p_CurManip->p_Hmct) - PTR_TO_UINT(p_WholeHmct)));
            if (p_CurManip->p_Data)
                p_TmpDataPtr =
                        (uint8_t*)PTR_MOVE(p_FmPcd->p_CcShadow,
                                (16 + PTR_TO_UINT(p_CurManip->p_Data) - PTR_TO_UINT(p_WholeHmct)));

            BuildHmct(p_CurManip, &p_CurManip->manipParams, p_TmpHmctPtr,
                      p_TmpDataPtr, FALSE);
            p_CurManip = p_CurManip->h_PrevManip;
        }
    }

    return E_OK;
}

static t_Error CreateManipActionBackToOrig(
        t_FmPcdManip *p_Manip, t_FmPcdManipParams *p_FmPcdManipParams)
{
    uint8_t *p_WholeHmct = NULL, *p_TmpHmctPtr, *p_TmpDataPtr;
    t_FmPcdManip *p_CurManip = p_Manip;

    /* Build the new table in the shadow */
    if (!MANIP_IS_UNIFIED(p_Manip))
        BuildHmct(p_Manip, p_FmPcdManipParams, p_Manip->p_Hmct, p_Manip->p_Data,
                  FALSE);
    else
    {
        p_WholeHmct = (uint8_t *)GetManipInfo(p_Manip, e_MANIP_HMCT);
        ASSERT_COND(p_WholeHmct);

        /* Run till the last Manip (which is the first to configure) */
        while (MANIP_IS_UNIFIED_NON_LAST(p_CurManip))
            p_CurManip = p_CurManip->h_NextManip;

        while (p_CurManip)
        {
            /* If this is a unified table, point to the part of the table
             * which is the relative offset in HMCT.
             */
            p_TmpHmctPtr = p_CurManip->p_Hmct; /*- (uint32_t)p_WholeHmct*/
            p_TmpDataPtr = p_CurManip->p_Data; /*- (uint32_t)p_WholeHmct*/

            BuildHmct(p_CurManip, &p_CurManip->manipParams, p_TmpHmctPtr,
                      p_TmpDataPtr, FALSE);

            p_CurManip = p_CurManip->h_PrevManip;
        }
    }

    return E_OK;
}

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
static t_Error UpdateManipIc(t_Handle h_Manip, uint8_t icOffset)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    t_Handle p_Ad;
    uint32_t tmpReg32 = 0;
    SANITY_CHECK_RETURN_ERROR(h_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad, E_INVALID_HANDLE);

    switch (p_Manip->opcode)
    {
        case (HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX):
        p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
        if (p_Manip->updateParams & INTERNAL_CONTEXT_OFFSET)
        {
            tmpReg32 =
            *(uint32_t *)&((t_AdOfTypeContLookup *)p_Ad)->pcAndOffsets;
            tmpReg32 |= (uint32_t)((uint32_t)icOffset << 16);
            *(uint32_t *)&((t_AdOfTypeContLookup *)p_Ad)->pcAndOffsets =
            tmpReg32;
            p_Manip->updateParams &= ~INTERNAL_CONTEXT_OFFSET;
            p_Manip->icOffset = icOffset;
        }
        else
        {
            if (p_Manip->icOffset != icOffset)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("this manipulation was updated previously by different value"););
        }
        break;
        case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):
        if (p_Manip->h_Frag)
        {
            if (p_Manip->updateParams & INTERNAL_CONTEXT_OFFSET)
            {
                p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
                tmpReg32 |= GET_UINT32(((t_AdOfTypeContLookup *)p_Ad)->pcAndOffsets);
                tmpReg32 |= (uint32_t)((uint32_t)icOffset << 16);
                WRITE_UINT32(((t_AdOfTypeContLookup *)p_Ad)->pcAndOffsets, tmpReg32);
                p_Manip->updateParams &= ~INTERNAL_CONTEXT_OFFSET;
                p_Manip->icOffset = icOffset;
            }
            else
            {
                if (p_Manip->icOffset != icOffset)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("this manipulation was updated previousely by different value"););
            }
        }
        break;
    }

    return E_OK;
}

static t_Error UpdateInitMvIntFrameHeaderFromFrameToBufferPrefix(
        t_Handle h_FmPort, t_FmPcdManip *p_Manip, t_Handle h_Ad, bool validate)
{

    t_AdOfTypeContLookup *p_Ad = (t_AdOfTypeContLookup *)h_Ad;
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    t_Error err;
    uint32_t tmpReg32;

    memset(&fmPortGetSetCcParams, 0, sizeof(t_FmPortGetSetCcParams));

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(
            (p_Manip->opcode & HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX),
            E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Manip->muramAllocate, E_INVALID_STATE);

    if (p_Manip->updateParams)
    {
        if ((!(p_Manip->updateParams & OFFSET_OF_PR))
                || (p_Manip->shadowUpdateParams & OFFSET_OF_PR))
        RETURN_ERROR(
                MAJOR, E_INVALID_STATE,
                ("in this stage parameters from Port has not be updated"));

        fmPortGetSetCcParams.getCcParams.type = p_Manip->updateParams;
        fmPortGetSetCcParams.setCcParams.type = UPDATE_PSO;
        fmPortGetSetCcParams.setCcParams.psoSize = 16;

        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);
        if (fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_PR)
        RETURN_ERROR(
                MAJOR, E_INVALID_STATE,
                ("Parser result offset wasn't configured previousely"));
#ifdef FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004
        ASSERT_COND(!(fmPortGetSetCcParams.getCcParams.prOffset % 16));
#endif
    }
    else
    if (validate)
    {
        if ((!(p_Manip->shadowUpdateParams & OFFSET_OF_PR))
                || (p_Manip->updateParams & OFFSET_OF_PR))
        RETURN_ERROR(
                MAJOR, E_INVALID_STATE,
                ("in this stage parameters from Port has be updated"));
        fmPortGetSetCcParams.getCcParams.type = p_Manip->shadowUpdateParams;
        fmPortGetSetCcParams.setCcParams.type = UPDATE_PSO;
        fmPortGetSetCcParams.setCcParams.psoSize = 16;

        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);
        if (fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_PR)
        RETURN_ERROR(
                MAJOR, E_INVALID_STATE,
                ("Parser result offset wasn't configured previousely"));

    }

    ASSERT_COND(p_Ad);

    if (p_Manip->updateParams & OFFSET_OF_PR)
    {
        tmpReg32 = 0;
        tmpReg32 |= fmPortGetSetCcParams.getCcParams.prOffset;
        WRITE_UINT32(p_Ad->matchTblPtr,
                (GET_UINT32(p_Ad->matchTblPtr) | tmpReg32));
        p_Manip->updateParams &= ~OFFSET_OF_PR;
        p_Manip->shadowUpdateParams |= OFFSET_OF_PR;
    }
    else
    if (validate)
    {
        tmpReg32 = GET_UINT32(p_Ad->matchTblPtr);
        if ((uint8_t)tmpReg32 != fmPortGetSetCcParams.getCcParams.prOffset)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("this manipulation was updated previousely by different value"););
    }

    return E_OK;
}

static t_Error UpdateModifyCapwapFragmenation(t_FmPcdManip *p_Manip, t_Handle h_Ad, bool validate,t_Handle h_FmTree)
{
    t_AdOfTypeContLookup *p_Ad = (t_AdOfTypeContLookup *)h_Ad;
    t_FmPcdCcSavedManipParams *p_SavedManipParams = NULL;
    uint32_t tmpReg32 = 0;

    SANITY_CHECK_RETURN_ERROR(p_Manip,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Frag,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->frag,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((p_Manip->opcode == HMAN_OC_CAPWAP_FRAGMENTATION) || (p_Manip->opcode == HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER)), E_INVALID_STATE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Frag;

    if (p_Manip->updateParams)
    {

        if ((!(p_Manip->updateParams & OFFSET_OF_DATA)) ||
                ((p_Manip->shadowUpdateParams & OFFSET_OF_DATA)))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("in this stage parameters from Port has not be updated"));
        p_SavedManipParams = FmPcdCcTreeGetSavedManipParams(h_FmTree);
        if (!p_SavedManipParams)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("for this manipulation tree has to be configured previosely with this type"));
        p_Manip->capwapFragParams.dataOffset = p_SavedManipParams->capwapParams.dataOffset;

        tmpReg32 = GET_UINT32(p_Ad->pcAndOffsets);
        tmpReg32 |= ((uint32_t)p_Manip->capwapFragParams.dataOffset<< 16);
        WRITE_UINT32(p_Ad->pcAndOffsets,tmpReg32);

        p_Manip->updateParams &= ~OFFSET_OF_DATA;
        p_Manip->shadowUpdateParams |= OFFSET_OF_DATA;
    }
    else if (validate)
    {

        p_SavedManipParams = FmPcdCcTreeGetSavedManipParams(h_FmTree);
        if (!p_SavedManipParams)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("for this manipulation tree has to be configured previosely with this type"));
        if (p_Manip->capwapFragParams.dataOffset != p_SavedManipParams->capwapParams.dataOffset)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("this manipulation was updated previousely by different value"));
    }

    return E_OK;
}

static t_Error UpdateInitCapwapFragmentation(t_Handle h_FmPort,
        t_FmPcdManip *p_Manip,
        t_Handle h_Ad,
        bool validate,
        t_Handle h_FmTree)
{
    t_AdOfTypeContLookup *p_Ad;
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    t_Error err;
    uint32_t tmpReg32 = 0;
    t_FmPcdCcSavedManipParams *p_SavedManipParams;

    UNUSED(h_Ad);

    SANITY_CHECK_RETURN_ERROR(p_Manip,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Frag,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->frag,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((p_Manip->opcode == HMAN_OC_CAPWAP_FRAGMENTATION) ||
                    (p_Manip->opcode == HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER)), E_INVALID_STATE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Frag;

    if (p_Manip->updateParams)
    {
        if ((!(p_Manip->updateParams & OFFSET_OF_DATA)) ||
                ((p_Manip->shadowUpdateParams & OFFSET_OF_DATA)))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("in this stage parameters from Port has not be updated"));
        fmPortGetSetCcParams.getCcParams.type = p_Manip->updateParams;
        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNEN | UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY;
        fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_POP_TO_N_STEP | NIA_ENG_FM_CTL;
        /* For CAPWAP Rassembly used FMAN_CTRL2 hardcoded - so for fragmentation its better to use FMAN_CTRL1 */
        fmPortGetSetCcParams.setCcParams.orFmanCtrl = FPM_PORT_FM_CTL1;

        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

        if (fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_DATA)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Data offset wasn't configured previousely"));

        p_SavedManipParams = (t_FmPcdCcSavedManipParams *)XX_Malloc(sizeof(t_FmPcdCcSavedManipParams));
        p_SavedManipParams->capwapParams.dataOffset = fmPortGetSetCcParams.getCcParams.dataOffset;

#ifdef FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004
        ASSERT_COND(!(p_SavedManipParams->capwapParams.dataOffset % 16));
#endif /* FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004 */

        FmPcdCcTreeSetSavedManipParams(h_FmTree, (t_Handle)p_SavedManipParams);
    }
    else if (validate)
    {
        if ((!(p_Manip->shadowUpdateParams & OFFSET_OF_DATA)) ||
                ((p_Manip->updateParams & OFFSET_OF_DATA)))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("in this stage parameters from Port has be updated"));
        fmPortGetSetCcParams.getCcParams.type = p_Manip->shadowUpdateParams;
        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNEN | UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY;
        fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_POP_TO_N_STEP | NIA_ENG_FM_CTL;
        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

        if (fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_DATA)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Data offset wasn't configured previousely"));
    }

    if (p_Manip->updateParams)
    {
        tmpReg32 = GET_UINT32(p_Ad->pcAndOffsets);
        tmpReg32 |= ((uint32_t)fmPortGetSetCcParams.getCcParams.dataOffset<< 16);
        WRITE_UINT32(p_Ad->pcAndOffsets,tmpReg32);

        p_Manip->updateParams &= ~OFFSET_OF_DATA;
        p_Manip->shadowUpdateParams |= OFFSET_OF_DATA;
        p_Manip->capwapFragParams.dataOffset = fmPortGetSetCcParams.getCcParams.dataOffset;
    }
    else if (validate)
    {
        if (p_Manip->capwapFragParams.dataOffset != fmPortGetSetCcParams.getCcParams.dataOffset)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("this manipulation was updated previousely by different value"));
    }

    return E_OK;
}

static t_Error UpdateInitCapwapReasm(t_Handle h_FmPcd,
        t_Handle h_FmPort,
        t_FmPcdManip *p_Manip,
        t_Handle h_Ad,
        bool validate)
{
    t_CapwapReasmPram *p_ReassmTbl;
    t_Error err;
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    uint8_t i = 0;
    uint16_t size;
    uint32_t tmpReg32;
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_FmPcdCcCapwapReassmTimeoutParams ccCapwapReassmTimeoutParams;

    SANITY_CHECK_RETURN_ERROR(p_Manip,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Frag,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Manip->frag,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Manip->opcode == HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST), E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcd,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc,E_INVALID_HANDLE);

    if (p_Manip->h_FmPcd != h_FmPcd)
    RETURN_ERROR(MAJOR, E_INVALID_STATE,
            ("handler of PCD previously was initiated by different value"));

    UNUSED(h_Ad);

    memset(&fmPortGetSetCcParams, 0, sizeof(t_FmPortGetSetCcParams));
    p_ReassmTbl = (t_CapwapReasmPram *)p_Manip->h_Frag;

    if (p_Manip->updateParams)
    {
        if ((!(p_Manip->updateParams & NUM_OF_TASKS) &&
                        !(p_Manip->updateParams & OFFSET_OF_DATA) &&
                        !(p_Manip->updateParams & OFFSET_OF_PR) &&
                        !(p_Manip->updateParams & HW_PORT_ID)) ||
                ((p_Manip->shadowUpdateParams & NUM_OF_TASKS) ||
                        (p_Manip->shadowUpdateParams & OFFSET_OF_DATA) || (p_Manip->shadowUpdateParams & OFFSET_OF_PR) ||
                        (p_Manip->shadowUpdateParams & HW_PORT_ID)))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("in this stage parameters from Port has not be updated"));

        fmPortGetSetCcParams.getCcParams.type = p_Manip->updateParams;
        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNEN;
        fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_POP_TO_N_STEP | NIA_ENG_FM_CTL;

        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

        if (fmPortGetSetCcParams.getCcParams.type & NUM_OF_TASKS)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Num of tasks wasn't configured previousely"));
        if (fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_DATA)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("offset of the data  wasn't configured previousely"));
        if (fmPortGetSetCcParams.getCcParams.type & HW_PORT_ID)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("hwPortId wasn't updated"));
#ifdef FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004
        ASSERT_COND((fmPortGetSetCcParams.getCcParams.dataOffset % 16) == 0);
#endif /* FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004 */
    }
    else if (validate)
    {
        if ((!(p_Manip->shadowUpdateParams & NUM_OF_TASKS) &&
                        !(p_Manip->shadowUpdateParams & OFFSET_OF_DATA) &&
                        !(p_Manip->shadowUpdateParams & OFFSET_OF_PR) &&
                        !(p_Manip->shadowUpdateParams & HW_PORT_ID)) &&
                ((p_Manip->updateParams & NUM_OF_TASKS) ||
                        (p_Manip->updateParams & OFFSET_OF_DATA) || (p_Manip->updateParams & OFFSET_OF_PR) ||
                        (p_Manip->updateParams & HW_PORT_ID)))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("in this stage parameters from Port has be updated"));

        fmPortGetSetCcParams.getCcParams.type = p_Manip->shadowUpdateParams;
        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNEN;
        fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_POP_TO_N_STEP | NIA_ENG_FM_CTL;

        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

        if (fmPortGetSetCcParams.getCcParams.type & NUM_OF_TASKS)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("NumOfTasks wasn't configured previously"));
        if (fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_DATA)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("offset of the data  wasn't configured previously"));
        if (fmPortGetSetCcParams.getCcParams.type & HW_PORT_ID)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("hwPortId wasn't updated"));
    }

    if (p_Manip->updateParams)
    {
        if (p_Manip->updateParams & NUM_OF_TASKS)
        {
            /*recommendation of Microcode team - (maxNumFramesInProcess * 2) */
            size = (uint16_t)(p_Manip->capwapFragParams.maxNumFramesInProcess*2 + fmPortGetSetCcParams.getCcParams.numOfTasks);
            if (size > 255)
            RETURN_ERROR(MAJOR,E_INVALID_VALUE, ("numOfOpenReassmEntries + numOfTasks per port can not be greater than 256"));

            p_Manip->capwapFragParams.numOfTasks = fmPortGetSetCcParams.getCcParams.numOfTasks;

            /*p_ReassmFrmDescrIndxPoolTbl*/
            p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl =
            (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                    (uint32_t)(size + 1),
                    4);
            if (!p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for CAPWAP Reassembly frame buffer index pool table"));

            MemSet8(p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl, 0, (uint32_t)(size + 1));

            for ( i = 0; i < size; i++)
            WRITE_UINT8(*(uint8_t *)PTR_MOVE(p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl, i), (uint8_t)(i+1));

            tmpReg32 = (uint32_t)(XX_VirtToPhys(p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl) - p_FmPcd->physicalMuramBase);

            WRITE_UINT32(p_ReassmTbl->reasmFrmDescIndexPoolTblPtr, tmpReg32);

            /*p_ReassmFrmDescrPoolTbl*/
            p_Manip->capwapFragParams.p_ReassmFrmDescrPoolTbl =
            (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                    (uint32_t)((size + 1) * FM_PCD_MANIP_CAPWAP_REASM_RFD_SIZE),
                    4);

            if (!p_Manip->capwapFragParams.p_ReassmFrmDescrPoolTbl)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for CAPWAP Reassembly frame buffer pool table"));

            MemSet8(p_Manip->capwapFragParams.p_ReassmFrmDescrPoolTbl, 0, (uint32_t)((size +1)* FM_PCD_MANIP_CAPWAP_REASM_RFD_SIZE));

            tmpReg32 = (uint32_t)(XX_VirtToPhys(p_Manip->capwapFragParams.p_ReassmFrmDescrPoolTbl) - p_FmPcd->physicalMuramBase);

            WRITE_UINT32(p_ReassmTbl->reasmFrmDescPoolTblPtr, tmpReg32);

            /*p_TimeOutTbl*/

            p_Manip->capwapFragParams.p_TimeOutTbl =
            (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                    (uint32_t)((size + 1)* FM_PCD_MANIP_CAPWAP_REASM_TIME_OUT_ENTRY_SIZE),
                    4);

            if (!p_Manip->capwapFragParams.p_TimeOutTbl)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for CAPWAP Reassembly timeout table"));

            MemSet8(p_Manip->capwapFragParams.p_TimeOutTbl, 0, (uint16_t)((size + 1)*FM_PCD_MANIP_CAPWAP_REASM_TIME_OUT_ENTRY_SIZE));

            tmpReg32 = (uint32_t)(XX_VirtToPhys(p_Manip->capwapFragParams.p_TimeOutTbl) - p_FmPcd->physicalMuramBase);
            WRITE_UINT32(p_ReassmTbl->timeOutTblPtr, tmpReg32);

            p_Manip->updateParams &= ~NUM_OF_TASKS;
            p_Manip->shadowUpdateParams |= NUM_OF_TASKS;
        }

        if (p_Manip->updateParams & OFFSET_OF_DATA)
        {
            p_Manip->capwapFragParams.dataOffset = fmPortGetSetCcParams.getCcParams.dataOffset;
            tmpReg32 = GET_UINT32(p_ReassmTbl->mode);
            tmpReg32|= p_Manip->capwapFragParams.dataOffset;
            WRITE_UINT32(p_ReassmTbl->mode, tmpReg32);
            p_Manip->updateParams &= ~OFFSET_OF_DATA;
            p_Manip->shadowUpdateParams |= OFFSET_OF_DATA;
        }

        if (!(fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_PR))
        {
            p_Manip->capwapFragParams.prOffset = fmPortGetSetCcParams.getCcParams.prOffset;

            tmpReg32 = GET_UINT32(p_ReassmTbl->mode);
            tmpReg32|= FM_PCD_MANIP_CAPWAP_REASM_PR_COPY;
            WRITE_UINT32(p_ReassmTbl->mode, tmpReg32);

            tmpReg32 = GET_UINT32(p_ReassmTbl->intStatsTblPtr);
            tmpReg32 |= (uint32_t)p_Manip->capwapFragParams.prOffset << 24;
            WRITE_UINT32(p_ReassmTbl->intStatsTblPtr, tmpReg32);
            p_Manip->updateParams &= ~OFFSET_OF_PR;
            p_Manip->shadowUpdateParams |= OFFSET_OF_PR;
        }
        else
        {
            p_Manip->capwapFragParams.prOffset = 0xff;
            p_Manip->updateParams &= ~OFFSET_OF_PR;
            p_Manip->shadowUpdateParams |= OFFSET_OF_PR;
        }

        p_Manip->capwapFragParams.hwPortId = fmPortGetSetCcParams.getCcParams.hardwarePortId;
        p_Manip->updateParams &= ~HW_PORT_ID;
        p_Manip->shadowUpdateParams |= HW_PORT_ID;

        /*timeout hc */
        ccCapwapReassmTimeoutParams.fqidForTimeOutFrames = p_Manip->capwapFragParams.fqidForTimeOutFrames;
        ccCapwapReassmTimeoutParams.portIdAndCapwapReassmTbl = (uint32_t)p_Manip->capwapFragParams.hwPortId << 24;
        ccCapwapReassmTimeoutParams.portIdAndCapwapReassmTbl |= (uint32_t)((XX_VirtToPhys(p_ReassmTbl) - p_FmPcd->physicalMuramBase));
        ccCapwapReassmTimeoutParams.timeoutRequestTime = (((uint32_t)1<<p_Manip->capwapFragParams.bitFor1Micro) * p_Manip->capwapFragParams.timeoutRoutineRequestTime)/2;
        return FmHcPcdCcCapwapTimeoutReassm(p_FmPcd->h_Hc,&ccCapwapReassmTimeoutParams);
    }

    else if (validate)
    {
        if (fmPortGetSetCcParams.getCcParams.hardwarePortId != p_Manip->capwapFragParams.hwPortId)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Reassembly manipulation previously was assigned to another port"));
        if (fmPortGetSetCcParams.getCcParams.numOfTasks != p_Manip->capwapFragParams.numOfTasks)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("numOfTasks for this manipulation previously was defined by another value "));

        if (!(fmPortGetSetCcParams.getCcParams.type & OFFSET_OF_PR))
        {
            if (p_Manip->capwapFragParams.prOffset != fmPortGetSetCcParams.getCcParams.prOffset)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Parse result offset previously was defined by another value "));
        }
        else
        {
            if (p_Manip->capwapFragParams.prOffset != 0xff)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Parse result offset previously was defined by another value "));
        }
        if (fmPortGetSetCcParams.getCcParams.dataOffset != p_Manip->capwapFragParams.dataOffset)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Data offset previously was defined by another value "));
    }

    return E_OK;
}
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */

t_Error FmPcdRegisterReassmPort(t_Handle h_FmPcd, t_Handle h_ReasmCommonPramTbl)
{
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_FmPcdCcReassmTimeoutParams ccReassmTimeoutParams = { 0 };
    t_Error err = E_OK;
    uint8_t result;
    uint32_t bitFor1Micro, tsbs, log2num;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(h_ReasmCommonPramTbl);

    bitFor1Micro = FmGetTimeStampScale(p_FmPcd->h_Fm);
    if (bitFor1Micro == 0)
        RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("Timestamp scale"));

    bitFor1Micro = 32 - bitFor1Micro;
    LOG2(FM_PCD_MANIP_REASM_TIMEOUT_THREAD_THRESH, log2num);
    tsbs = bitFor1Micro - log2num;

    ccReassmTimeoutParams.iprcpt = (uint32_t)(XX_VirtToPhys(
            h_ReasmCommonPramTbl) - p_FmPcd->physicalMuramBase);
    ccReassmTimeoutParams.tsbs = (uint8_t)tsbs;
    ccReassmTimeoutParams.activate = TRUE;
    if ((err = FmHcPcdCcTimeoutReassm(p_FmPcd->h_Hc, &ccReassmTimeoutParams,
                                      &result)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    switch (result)
    {
        case (0):
            return E_OK;
        case (1):
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("failed to allocate TNUM"));
        case (2):
            RETURN_ERROR(
                    MAJOR, E_NO_MEMORY,
                    ("failed to allocate internal buffer from the HC-Port"));
        case (3):
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         ("'Disable Timeout Task' with invalid IPRCPT"));
        case (4):
            RETURN_ERROR(MAJOR, E_FULL, ("too many timeout tasks"));
        case (5):
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("invalid sub command"));
        default:
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
    }
    return E_OK;
}

static t_Error CreateReassCommonTable(t_FmPcdManip *p_Manip)
{
    uint32_t tmpReg32 = 0, i, bitFor1Micro;
    uint64_t tmpReg64, size;
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_Manip->h_FmPcd;
    t_Error err = E_OK;

    bitFor1Micro = FmGetTimeStampScale(p_FmPcd->h_Fm);
    if (bitFor1Micro == 0)
        RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("Timestamp scale"));

    /* Allocation of the Reassembly Common Parameters table. This table is located in the
     MURAM. Its size is 64 bytes and its base address should be 8-byte aligned. */
    p_Manip->reassmParams.p_ReassCommonTbl =
            (t_ReassCommonTbl *)FM_MURAM_AllocMem(
                    p_FmPcd->h_FmMuram,
                    FM_PCD_MANIP_REASM_COMMON_PARAM_TABLE_SIZE,
                    FM_PCD_MANIP_REASM_COMMON_PARAM_TABLE_ALIGN);

    if (!p_Manip->reassmParams.p_ReassCommonTbl)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM alloc for Reassembly common parameters table"));

    MemSet8(p_Manip->reassmParams.p_ReassCommonTbl, 0,
               FM_PCD_MANIP_REASM_COMMON_PARAM_TABLE_SIZE);

    /* Setting the TimeOut Mode.*/
    tmpReg32 = 0;
    if (p_Manip->reassmParams.timeOutMode
            == e_FM_PCD_MANIP_TIME_OUT_BETWEEN_FRAMES)
        tmpReg32 |= FM_PCD_MANIP_REASM_TIME_OUT_BETWEEN_FRAMES;

    /* Setting TimeOut FQID - Frames that time out are enqueued to this FQID.
     In order to cause TimeOut frames to be discarded, this queue should be configured accordingly*/
    tmpReg32 |= p_Manip->reassmParams.fqidForTimeOutFrames;
    WRITE_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->timeoutModeAndFqid,
                 tmpReg32);

    /* Calculation the size of IP Reassembly Frame Descriptor - number of frames that are allowed to be reassembled simultaneously + 129.*/
    size = p_Manip->reassmParams.maxNumFramesInProcess + 129;

    /*Allocation of IP Reassembly Frame Descriptor Indexes Pool - This pool resides in the MURAM */
    p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr =
            PTR_TO_UINT(FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                            (uint32_t)(size * 2),
                            256));
    if (!p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr)
        RETURN_ERROR(
                MAJOR, E_NO_MEMORY,
                ("MURAM alloc for Reassembly frame descriptor indexes pool"));

    MemSet8(UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr),
               0, (uint32_t)(size * 2));

    /* The entries in IP Reassembly Frame Descriptor Indexes Pool contains indexes starting with 1 up to
     the maximum number of frames that are allowed to be reassembled simultaneously + 128.
     The last entry in this pool must contain the index zero*/
    for (i = 0; i < (size - 1); i++)
        WRITE_UINT16(
                *(uint16_t *)PTR_MOVE(UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr), (i<<1)),
                (uint16_t)(i+1));

    /* Sets the IP Reassembly Frame Descriptor Indexes Pool offset from MURAM */
    tmpReg32 = (uint32_t)(XX_VirtToPhys(
            UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr))
            - p_FmPcd->physicalMuramBase);
    WRITE_UINT32(
            p_Manip->reassmParams.p_ReassCommonTbl->reassFrmDescIndexPoolTblPtr,
            tmpReg32);

    /* Allocation of the Reassembly Frame Descriptors Pool - This pool resides in external memory.
     The number of entries in this pool should be equal to the number of entries in IP Reassembly Frame Descriptor Indexes Pool.*/
    p_Manip->reassmParams.reassFrmDescrPoolTblAddr =
            PTR_TO_UINT(XX_MallocSmart((uint32_t)(size * 64), p_Manip->reassmParams.dataMemId, 64));

    if (!p_Manip->reassmParams.reassFrmDescrPoolTblAddr)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory allocation FAILED"));

    MemSet8(UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrPoolTblAddr), 0,
               (uint32_t)(size * 64));

    /* Sets the Reassembly Frame Descriptors Pool and liodn offset*/
    tmpReg64 = (uint64_t)(XX_VirtToPhys(
            UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrPoolTblAddr)));
    tmpReg64 |= ((uint64_t)(p_Manip->reassmParams.dataLiodnOffset
            & FM_PCD_MANIP_REASM_LIODN_MASK)
            << (uint64_t)FM_PCD_MANIP_REASM_LIODN_SHIFT);
    tmpReg64 |= ((uint64_t)(p_Manip->reassmParams.dataLiodnOffset
            & FM_PCD_MANIP_REASM_ELIODN_MASK)
            << (uint64_t)FM_PCD_MANIP_REASM_ELIODN_SHIFT);
    WRITE_UINT32(
            p_Manip->reassmParams.p_ReassCommonTbl->liodnAndReassFrmDescPoolPtrHi,
            (uint32_t)(tmpReg64 >> 32));
    WRITE_UINT32(
            p_Manip->reassmParams.p_ReassCommonTbl->reassFrmDescPoolPtrLow,
            (uint32_t)tmpReg64);

    /*Allocation of the TimeOut table - This table resides in the MURAM.
     The number of entries in this table is identical to the number of entries in the Reassembly Frame Descriptors Pool*/
    p_Manip->reassmParams.timeOutTblAddr =
            PTR_TO_UINT(FM_MURAM_AllocMem(p_FmPcd->h_FmMuram, (uint32_t)(size * 8),8));

    if (!p_Manip->reassmParams.timeOutTblAddr)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM alloc for Reassembly timeout table"));

    MemSet8(UINT_TO_PTR(p_Manip->reassmParams.timeOutTblAddr), 0,
               (uint16_t)(size * 8));

    /* Sets the TimeOut table offset from MURAM */
    tmpReg32 = (uint32_t)(XX_VirtToPhys(
            UINT_TO_PTR(p_Manip->reassmParams.timeOutTblAddr))
            - p_FmPcd->physicalMuramBase);
    WRITE_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->timeOutTblPtr,
                 tmpReg32);

    /* Sets the Expiration Delay */
    tmpReg32 = 0;
    tmpReg32 |= (((uint32_t)(1 << bitFor1Micro))
            * p_Manip->reassmParams.timeoutThresholdForReassmProcess);
    WRITE_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->expirationDelay,
                 tmpReg32);

    err = FmPcdRegisterReassmPort(p_FmPcd,
                                  p_Manip->reassmParams.p_ReassCommonTbl);
    if (err != E_OK)
    {
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                         p_Manip->reassmParams.p_ReassCommonTbl);
        RETURN_ERROR(MAJOR, err, ("port registration"));
    }

    return err;
}

static t_Error CreateReassTable(t_FmPcdManip *p_Manip, e_NetHeaderType hdr)
{
    t_FmPcd *p_FmPcd = p_Manip->h_FmPcd;
    uint32_t tmpReg32, autoLearnHashTblSize;
    uint32_t numOfWays, setSize, setSizeCode, keySize;
    uint32_t waySize, numOfSets, numOfEntries;
    uint64_t tmpReg64;
    uint16_t minFragSize;
    uint16_t maxReassemSize;
    uintptr_t *p_AutoLearnHashTblAddr, *p_AutoLearnSetLockTblAddr;
    t_ReassTbl **p_ReassTbl;

    switch (hdr)
    {
        case HEADER_TYPE_IPv4:
            p_ReassTbl = &p_Manip->reassmParams.ip.p_Ipv4ReassTbl;
            p_AutoLearnHashTblAddr =
                    &p_Manip->reassmParams.ip.ipv4AutoLearnHashTblAddr;
            p_AutoLearnSetLockTblAddr =
                    &p_Manip->reassmParams.ip.ipv4AutoLearnSetLockTblAddr;
            minFragSize = p_Manip->reassmParams.ip.minFragSize[0];
            maxReassemSize = 0;
            numOfWays = p_Manip->reassmParams.ip.numOfFramesPerHashEntry[0];
            keySize = 4 + 4 + 1 + 2; /* 3-tuple + IP-Id */
            break;
        case HEADER_TYPE_IPv6:
            p_ReassTbl = &p_Manip->reassmParams.ip.p_Ipv6ReassTbl;
            p_AutoLearnHashTblAddr =
                    &p_Manip->reassmParams.ip.ipv6AutoLearnHashTblAddr;
            p_AutoLearnSetLockTblAddr =
                    &p_Manip->reassmParams.ip.ipv6AutoLearnSetLockTblAddr;
            minFragSize = p_Manip->reassmParams.ip.minFragSize[1];
            maxReassemSize = 0;
            numOfWays = p_Manip->reassmParams.ip.numOfFramesPerHashEntry[1];
            keySize = 16 + 16 + 4; /* 2-tuple + IP-Id */
            if (numOfWays > e_FM_PCD_MANIP_SIX_WAYS_HASH)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("num of ways"));
            break;
        case HEADER_TYPE_CAPWAP:
            p_ReassTbl = &p_Manip->reassmParams.capwap.p_ReassTbl;
            p_AutoLearnHashTblAddr =
                    &p_Manip->reassmParams.capwap.autoLearnHashTblAddr;
            p_AutoLearnSetLockTblAddr =
                    &p_Manip->reassmParams.capwap.autoLearnSetLockTblAddr;
            minFragSize = 0;
            maxReassemSize = p_Manip->reassmParams.capwap.maxRessembledsSize;
            numOfWays = p_Manip->reassmParams.capwap.numOfFramesPerHashEntry;
            keySize = 4;
            break;
        default:
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("header type"));
    }
    keySize += 2; /* 2 bytes reserved for RFDIndex */
#if (DPAA_VERSION >= 11)
    keySize += 2; /* 2 bytes reserved */
#endif /* (DPAA_VERSION >= 11) */
    waySize = ROUND_UP(keySize, 8);

    /* Allocates the Reassembly Parameters Table - This table is located in the MURAM.*/
    *p_ReassTbl = (t_ReassTbl *)FM_MURAM_AllocMem(
            p_FmPcd->h_FmMuram, FM_PCD_MANIP_REASM_TABLE_SIZE,
            FM_PCD_MANIP_REASM_TABLE_ALIGN);
    if (!*p_ReassTbl)
        RETURN_ERROR( MAJOR, E_NO_MEMORY,
                     ("MURAM alloc for Reassembly specific parameters table"));
    memset(*p_ReassTbl, 0, sizeof(t_ReassTbl));

    /* Sets the Reassembly common Parameters table offset from MURAM in the Reassembly Table descriptor*/
    tmpReg32 = (uint32_t)(XX_VirtToPhys(p_Manip->reassmParams.p_ReassCommonTbl)
            - p_FmPcd->physicalMuramBase);
    WRITE_UINT32((*p_ReassTbl)->reassCommonPrmTblPtr, tmpReg32);

    /* Calculate set size (set size is rounded-up to next power of 2) */
    NEXT_POWER_OF_2(numOfWays * waySize, setSize);

    /* Get set size code */
    LOG2(setSize, setSizeCode);

    /* Sets ways number and set size code */
    WRITE_UINT16((*p_ReassTbl)->waysNumAndSetSize,
                 (uint16_t)((numOfWays << 8) | setSizeCode));

    /* It is recommended that the total number of entries in this table
     (number of sets * number of ways) will be twice the number of frames that
     are expected to be reassembled simultaneously.*/
    numOfEntries = (uint32_t)(p_Manip->reassmParams.maxNumFramesInProcess * 2);

    /* sets number calculation - number of entries = number of sets * number of ways */
    numOfSets = numOfEntries / numOfWays;

    /* Sets AutoLearnHashKeyMask*/
    NEXT_POWER_OF_2(numOfSets, numOfSets);

    WRITE_UINT16((*p_ReassTbl)->autoLearnHashKeyMask,
                 (uint16_t)(numOfSets - 1));

    /* Allocation of Reassembly Automatic Learning Hash Table - This table resides in external memory.
     The size of this table is determined by the number of sets and the set size.
     Table size = set size * number of sets
     This table base address should be aligned to SetSize.*/
    autoLearnHashTblSize = numOfSets * setSize;

    *p_AutoLearnHashTblAddr =
            PTR_TO_UINT(XX_MallocSmart(autoLearnHashTblSize, p_Manip->reassmParams.dataMemId, setSize));
    if (!*p_AutoLearnHashTblAddr)
    {
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, *p_ReassTbl);
        *p_ReassTbl = NULL;
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory allocation FAILED"));
    }
    MemSet8(UINT_TO_PTR(*p_AutoLearnHashTblAddr), 0, autoLearnHashTblSize);

    /* Sets the Reassembly Automatic Learning Hash Table and liodn offset */
    tmpReg64 = ((uint64_t)(p_Manip->reassmParams.dataLiodnOffset
            & FM_PCD_MANIP_REASM_LIODN_MASK)
            << (uint64_t)FM_PCD_MANIP_REASM_LIODN_SHIFT);
    tmpReg64 |= ((uint64_t)(p_Manip->reassmParams.dataLiodnOffset
            & FM_PCD_MANIP_REASM_ELIODN_MASK)
            << (uint64_t)FM_PCD_MANIP_REASM_ELIODN_SHIFT);
    tmpReg64 |= XX_VirtToPhys(UINT_TO_PTR(*p_AutoLearnHashTblAddr));
    WRITE_UINT32( (*p_ReassTbl)->liodnAlAndAutoLearnHashTblPtrHi,
                 (uint32_t)(tmpReg64 >> 32));
    WRITE_UINT32((*p_ReassTbl)->autoLearnHashTblPtrLow, (uint32_t)tmpReg64);

    /* Allocation of the Set Lock table - This table resides in external memory
     The size of this table is (number of sets in the Reassembly Automatic Learning Hash table)*4 bytes.
     This table resides in external memory and its base address should be 4-byte aligned */
    *p_AutoLearnSetLockTblAddr =
            PTR_TO_UINT(XX_MallocSmart((uint32_t)(numOfSets * 4), p_Manip->reassmParams.dataMemId, 4));
    if (!*p_AutoLearnSetLockTblAddr)
    {
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, *p_ReassTbl);
        *p_ReassTbl = NULL;
        XX_FreeSmart(UINT_TO_PTR(*p_AutoLearnHashTblAddr));
        *p_AutoLearnHashTblAddr = 0;
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory allocation FAILED"));
    }
    MemSet8(UINT_TO_PTR(*p_AutoLearnSetLockTblAddr), 0, (numOfSets * 4));

    /* sets Set Lock table pointer and liodn offset*/
    tmpReg64 = ((uint64_t)(p_Manip->reassmParams.dataLiodnOffset
            & FM_PCD_MANIP_REASM_LIODN_MASK)
            << (uint64_t)FM_PCD_MANIP_REASM_LIODN_SHIFT);
    tmpReg64 |= ((uint64_t)(p_Manip->reassmParams.dataLiodnOffset
            & FM_PCD_MANIP_REASM_ELIODN_MASK)
            << (uint64_t)FM_PCD_MANIP_REASM_ELIODN_SHIFT);
    tmpReg64 |= XX_VirtToPhys(UINT_TO_PTR(*p_AutoLearnSetLockTblAddr));
    WRITE_UINT32( (*p_ReassTbl)->liodnSlAndAutoLearnSetLockTblPtrHi,
                 (uint32_t)(tmpReg64 >> 32));
    WRITE_UINT32((*p_ReassTbl)->autoLearnSetLockTblPtrLow, (uint32_t)tmpReg64);

    /* Sets user's requested minimum fragment size (in Bytes) for First/Middle fragment */
    WRITE_UINT16((*p_ReassTbl)->minFragSize, minFragSize);

    WRITE_UINT16((*p_ReassTbl)->maxReassemblySize, maxReassemSize);

    return E_OK;
}

static t_Error UpdateInitReasm(t_Handle h_FmPcd, t_Handle h_PcdParams,
                               t_Handle h_FmPort, t_FmPcdManip *p_Manip,
                               t_Handle h_Ad, bool validate)
{
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    uint32_t tmpReg32;
    t_Error err;
    t_FmPortPcdParams *p_PcdParams = (t_FmPortPcdParams *)h_PcdParams;
#if (DPAA_VERSION >= 11)
    t_FmPcdCtrlParamsPage *p_ParamsPage;
#endif /* (DPAA_VERSION >= 11) */

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Manip->frag, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(
            (p_Manip->opcode == HMAN_OC_IP_REASSEMBLY) || (p_Manip->opcode == HMAN_OC_CAPWAP_REASSEMBLY),
            E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Manip->updateParams || h_PcdParams,
                              E_INVALID_HANDLE);

    UNUSED(h_Ad);

    if (!p_Manip->updateParams)
        return E_OK;

    if (p_Manip->h_FmPcd != h_FmPcd)
        RETURN_ERROR(
                MAJOR, E_INVALID_STATE,
                ("handler of PCD previously was initiated by different value"));

    if (p_Manip->updateParams)
    {
        if ((!(p_Manip->updateParams
                & (NUM_OF_TASKS | NUM_OF_EXTRA_TASKS | DISCARD_MASK)))
                || ((p_Manip->shadowUpdateParams
                        & (NUM_OF_TASKS | NUM_OF_EXTRA_TASKS | DISCARD_MASK))))
            RETURN_ERROR(
                    MAJOR, E_INVALID_STATE,
                    ("in this stage parameters from Port has not be updated"));

        fmPortGetSetCcParams.setCcParams.type = 0;
        if (p_Manip->opcode == HMAN_OC_CAPWAP_REASSEMBLY)
        {
            fmPortGetSetCcParams.setCcParams.type |= UPDATE_OFP_DPTE;
            fmPortGetSetCcParams.setCcParams.ofpDpde = 0xF;
        }
        fmPortGetSetCcParams.getCcParams.type = p_Manip->updateParams | FM_REV;
        if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams))
                != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        if (fmPortGetSetCcParams.getCcParams.type
                & (NUM_OF_TASKS | NUM_OF_EXTRA_TASKS | DISCARD_MASK | FM_REV))
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("offset of the data wasn't configured previously"));
        if (p_Manip->updateParams
                & (NUM_OF_TASKS | NUM_OF_EXTRA_TASKS | DISCARD_MASK))
        {
            t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
            uint8_t *p_Ptr, i, totalNumOfTnums;

            totalNumOfTnums =
                    (uint8_t)(fmPortGetSetCcParams.getCcParams.numOfTasks
                            + fmPortGetSetCcParams.getCcParams.numOfExtraTasks);

            p_Manip->reassmParams.internalBufferPoolAddr =
                    PTR_TO_UINT(FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                                    (uint32_t)(totalNumOfTnums * BMI_FIFO_UNITS),
                                    BMI_FIFO_UNITS));
            if (!p_Manip->reassmParams.internalBufferPoolAddr)
                RETURN_ERROR(
                        MAJOR, E_NO_MEMORY,
                        ("MURAM alloc for Reassembly internal buffers pool"));
            MemSet8(
                    UINT_TO_PTR(p_Manip->reassmParams.internalBufferPoolAddr),
                    0, (uint32_t)(totalNumOfTnums * BMI_FIFO_UNITS));

            p_Manip->reassmParams.internalBufferPoolManagementIndexAddr =
                    PTR_TO_UINT(FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                                    (uint32_t)(5 + totalNumOfTnums),
                                    4));
            if (!p_Manip->reassmParams.internalBufferPoolManagementIndexAddr)
                RETURN_ERROR(
                        MAJOR,
                        E_NO_MEMORY,
                        ("MURAM alloc for Reassembly internal buffers management"));

            p_Ptr =
                    (uint8_t*)UINT_TO_PTR(p_Manip->reassmParams.internalBufferPoolManagementIndexAddr);
            WRITE_UINT32(
                    *(uint32_t*)p_Ptr,
                    (uint32_t)(XX_VirtToPhys(UINT_TO_PTR(p_Manip->reassmParams.internalBufferPoolAddr)) - p_FmPcd->physicalMuramBase));
            for (i = 0, p_Ptr += 4; i < totalNumOfTnums; i++, p_Ptr++)
                WRITE_UINT8(*p_Ptr, i);
            WRITE_UINT8(*p_Ptr, 0xFF);

            tmpReg32 =
                    (4 << FM_PCD_MANIP_REASM_COMMON_INT_BUFFER_IDX_SHIFT)
                            | ((uint32_t)(XX_VirtToPhys(
                                    UINT_TO_PTR(p_Manip->reassmParams.internalBufferPoolManagementIndexAddr))
                                    - p_FmPcd->physicalMuramBase));
            WRITE_UINT32(
                    p_Manip->reassmParams.p_ReassCommonTbl->internalBufferManagement,
                    tmpReg32);

            p_Manip->updateParams &= ~(NUM_OF_TASKS | NUM_OF_EXTRA_TASKS
                    | DISCARD_MASK);
            p_Manip->shadowUpdateParams |= (NUM_OF_TASKS | NUM_OF_EXTRA_TASKS
                    | DISCARD_MASK);
        }
    }

    if (p_Manip->opcode == HMAN_OC_CAPWAP_REASSEMBLY)
    {
        if (p_Manip->reassmParams.capwap.h_Scheme)
        {
            p_PcdParams->p_KgParams->h_Schemes[p_PcdParams->p_KgParams->numOfSchemes] =
                    p_Manip->reassmParams.capwap.h_Scheme;
            p_PcdParams->p_KgParams->numOfSchemes++;
        }

    }
    else
    {
        if (p_Manip->reassmParams.ip.h_Ipv4Scheme)
        {
            p_PcdParams->p_KgParams->h_Schemes[p_PcdParams->p_KgParams->numOfSchemes] =
                    p_Manip->reassmParams.ip.h_Ipv4Scheme;
            p_PcdParams->p_KgParams->numOfSchemes++;
        }
        if (p_Manip->reassmParams.ip.h_Ipv6Scheme)
        {
            p_PcdParams->p_KgParams->h_Schemes[p_PcdParams->p_KgParams->numOfSchemes] =
                    p_Manip->reassmParams.ip.h_Ipv6Scheme;
            p_PcdParams->p_KgParams->numOfSchemes++;
        }
#if (DPAA_VERSION >= 11)
        if (fmPortGetSetCcParams.getCcParams.revInfo.majorRev >= 6)
        {
            if ((err = FmPortSetGprFunc(h_FmPort, e_FM_PORT_GPR_MURAM_PAGE,
                                        (void**)&p_ParamsPage)) != E_OK)
                RETURN_ERROR(MAJOR, err, NO_MSG);

            tmpReg32 = NIA_ENG_KG;
            if (p_Manip->reassmParams.ip.h_Ipv4Scheme)
            {
                tmpReg32 |= NIA_KG_DIRECT;
                tmpReg32 |= NIA_KG_CC_EN;
                tmpReg32 |= FmPcdKgGetSchemeId(
                        p_Manip->reassmParams.ip.h_Ipv4Scheme);
                WRITE_UINT32(p_ParamsPage->iprIpv4Nia, tmpReg32);
            }
            if (p_Manip->reassmParams.ip.h_Ipv6Scheme)
            {
                tmpReg32 &= ~NIA_AC_MASK;
                tmpReg32 |= NIA_KG_DIRECT;
                tmpReg32 |= NIA_KG_CC_EN;
                tmpReg32 |= FmPcdKgGetSchemeId(
                        p_Manip->reassmParams.ip.h_Ipv6Scheme);
                WRITE_UINT32(p_ParamsPage->iprIpv6Nia, tmpReg32);
            }
        }
#else
        if (fmPortGetSetCcParams.getCcParams.revInfo.majorRev < 6)
        {
            WRITE_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->discardMask,
                    fmPortGetSetCcParams.getCcParams.discardMask);
        }
#endif /* (DPAA_VERSION >= 11) */
    }
    return E_OK;
}

#if (DPAA_VERSION == 10)
static t_Error FmPcdFragHcScratchPoolFill(t_Handle h_FmPcd, uint8_t scratchBpid)
{
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_FmPcdCcFragScratchPoolCmdParams fmPcdCcFragScratchPoolCmdParams;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    memset(&fmPcdCcFragScratchPoolCmdParams, 0, sizeof(t_FmPcdCcFragScratchPoolCmdParams));

    fmPcdCcFragScratchPoolCmdParams.numOfBuffers = NUM_OF_SCRATCH_POOL_BUFFERS;
    fmPcdCcFragScratchPoolCmdParams.bufferPoolId = scratchBpid;
    if ((err = FmHcPcdCcIpFragScratchPollCmd(p_FmPcd->h_Hc, TRUE, &fmPcdCcFragScratchPoolCmdParams)) != E_OK)
    RETURN_ERROR(MAJOR, err, NO_MSG);

    if (fmPcdCcFragScratchPoolCmdParams.numOfBuffers != 0)
    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Fill scratch pool failed,"
                    "Failed to release %d buffers to the BM (missing FBPRs)",
                    fmPcdCcFragScratchPoolCmdParams.numOfBuffers));

    return E_OK;
}

static t_Error FmPcdFragHcScratchPoolEmpty(t_Handle h_FmPcd, uint8_t scratchBpid)
{
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_FmPcdCcFragScratchPoolCmdParams fmPcdCcFragScratchPoolCmdParams;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    memset(&fmPcdCcFragScratchPoolCmdParams, 0, sizeof(t_FmPcdCcFragScratchPoolCmdParams));

    fmPcdCcFragScratchPoolCmdParams.bufferPoolId = scratchBpid;
    if ((err = FmHcPcdCcIpFragScratchPollCmd(p_FmPcd->h_Hc, FALSE, &fmPcdCcFragScratchPoolCmdParams)) != E_OK)
    RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}
#endif /* (DPAA_VERSION == 10) */

static void ReleaseManipHandler(t_FmPcdManip *p_Manip, t_FmPcd *p_FmPcd)
{
    if (p_Manip->h_Ad)
    {
        if (p_Manip->muramAllocate)
            FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, p_Manip->h_Ad);
        else
            XX_Free(p_Manip->h_Ad);
        p_Manip->h_Ad = NULL;
    }
    if (p_Manip->p_Template)
    {
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, p_Manip->p_Template);
        p_Manip->p_Template = NULL;
    }
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
    if (p_Manip->h_Frag)
    {
        if (p_Manip->capwapFragParams.p_AutoLearnHashTbl)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                p_Manip->capwapFragParams.p_AutoLearnHashTbl);
        if (p_Manip->capwapFragParams.p_ReassmFrmDescrPoolTbl)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                p_Manip->capwapFragParams.p_ReassmFrmDescrPoolTbl);
        if (p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                p_Manip->capwapFragParams.p_ReassmFrmDescrIndxPoolTbl);
        if (p_Manip->capwapFragParams.p_TimeOutTbl)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                p_Manip->capwapFragParams.p_TimeOutTbl);
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, p_Manip->h_Frag);

    }
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
    if (p_Manip->frag)
    {
        if (p_Manip->fragParams.p_Frag)
        {
#if (DPAA_VERSION == 10)
            FmPcdFragHcScratchPoolEmpty((t_Handle)p_FmPcd, p_Manip->fragParams.scratchBpid);
#endif /* (DPAA_VERSION == 10) */

            FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, p_Manip->fragParams.p_Frag);
        }
    }
    else
        if (p_Manip->reassm)
        {
            FmPcdUnregisterReassmPort(p_FmPcd,
                                      p_Manip->reassmParams.p_ReassCommonTbl);

            if (p_Manip->reassmParams.timeOutTblAddr)
                FM_MURAM_FreeMem(
                        p_FmPcd->h_FmMuram,
                        UINT_TO_PTR(p_Manip->reassmParams.timeOutTblAddr));
            if (p_Manip->reassmParams.reassFrmDescrPoolTblAddr)
                XX_FreeSmart(
                        UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrPoolTblAddr));
            if (p_Manip->reassmParams.p_ReassCommonTbl)
                FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                                 p_Manip->reassmParams.p_ReassCommonTbl);
            if (p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr)
                FM_MURAM_FreeMem(
                        p_FmPcd->h_FmMuram,
                        UINT_TO_PTR(p_Manip->reassmParams.reassFrmDescrIndxPoolTblAddr));
            if (p_Manip->reassmParams.internalBufferPoolManagementIndexAddr)
                FM_MURAM_FreeMem(
                        p_FmPcd->h_FmMuram,
                        UINT_TO_PTR(p_Manip->reassmParams.internalBufferPoolManagementIndexAddr));
            if (p_Manip->reassmParams.internalBufferPoolAddr)
                FM_MURAM_FreeMem(
                        p_FmPcd->h_FmMuram,
                        UINT_TO_PTR(p_Manip->reassmParams.internalBufferPoolAddr));
            if (p_Manip->reassmParams.hdr == HEADER_TYPE_CAPWAP)
            {

            }
            else
            {
                if (p_Manip->reassmParams.ip.ipv4AutoLearnHashTblAddr)
                    XX_FreeSmart(
                            UINT_TO_PTR(p_Manip->reassmParams.ip.ipv4AutoLearnHashTblAddr));
                if (p_Manip->reassmParams.ip.ipv6AutoLearnHashTblAddr)
                    XX_FreeSmart(
                            UINT_TO_PTR(p_Manip->reassmParams.ip.ipv6AutoLearnHashTblAddr));
                if (p_Manip->reassmParams.ip.ipv4AutoLearnSetLockTblAddr)
                    XX_FreeSmart(
                            UINT_TO_PTR(p_Manip->reassmParams.ip.ipv4AutoLearnSetLockTblAddr));
                if (p_Manip->reassmParams.ip.ipv6AutoLearnSetLockTblAddr)
                    XX_FreeSmart(
                            UINT_TO_PTR(p_Manip->reassmParams.ip.ipv6AutoLearnSetLockTblAddr));
                if (p_Manip->reassmParams.ip.p_Ipv4ReassTbl)
                    FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                                     p_Manip->reassmParams.ip.p_Ipv4ReassTbl);
                if (p_Manip->reassmParams.ip.p_Ipv6ReassTbl)
                    FM_MURAM_FreeMem(p_FmPcd->h_FmMuram,
                                     p_Manip->reassmParams.ip.p_Ipv6ReassTbl);
                if (p_Manip->reassmParams.ip.h_Ipv6Ad)
                    XX_FreeSmart(p_Manip->reassmParams.ip.h_Ipv6Ad);
                if (p_Manip->reassmParams.ip.h_Ipv4Ad)
                    XX_FreeSmart(p_Manip->reassmParams.ip.h_Ipv4Ad);
            }
        }

    if (p_Manip->p_StatsTbl)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, p_Manip->p_StatsTbl);
}

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
static t_Error CheckManipParamsAndSetType(t_FmPcdManip *p_Manip, t_FmPcdManipParams *p_ManipParams)
{
    if (p_ManipParams->u.hdr.rmv)
    {
        switch (p_ManipParams->u.hdr.rmvParams.type)
        {
            case (e_FM_PCD_MANIP_RMV_BY_HDR):
            switch (p_ManipParams->u.hdr.rmvParams.u.byHdr.type)
            {
                case (e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START) :
                if (p_ManipParams->u.hdr.rmvParams.u.byHdr.u.fromStartByHdr.include)
                {
                    switch (p_ManipParams->u.hdr.rmvParams.u.byHdr.u.fromStartByHdr.hdrInfo.hdr)
                    {
                        case (HEADER_TYPE_CAPWAP_DTLS) :
                        p_Manip->opcode = HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST;
                        p_Manip->muramAllocate = TRUE;
                        if (p_ManipParams->u.hdr.insrt)
                        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("for  CAPWAP_DTLS_HDR remove can not be insrt manipualtion after"));
                        if (p_ManipParams->fragOrReasm)
                        {
                            if (!p_ManipParams->fragOrReasmParams.frag)
                            {
                                switch (p_ManipParams->fragOrReasmParams.hdr)
                                {
                                    case (HEADER_TYPE_CAPWAP):
                                    p_Manip->opcode = HMAN_OC_CAPWAP_REASSEMBLY;
                                    break;
                                    default:
                                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("unsupported header for Reassembly"));
                                }
                            }
                            else
                            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("for this type of manipulation frag can not be TRUE"));
                        }
                        break;
                        default:
                        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("non valid net header of remove location"));
                    }
                }
                else
                {
                    switch (p_ManipParams->u.hdr.rmvParams.u.byHdr.u.fromStartByHdr.hdrInfo.hdr)
                    {
                        case (HEADER_TYPE_CAPWAP_DTLS) :
                        case (HEADER_TYPE_CAPWAP) :
                        if (p_ManipParams->fragOrReasm || p_ManipParams->u.hdr.insrt)
                        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("for the type of remove e_FM_PCD_MANIP_RMV_FROM_START_OF_FRAME_TILL_CAPWAP can not be insert or fragOrReasm TRUE"));
                        p_Manip->opcode = HMAN_OC_RMV_N_OR_INSRT_INT_FRM_HDR;
                        p_Manip->muramAllocate = TRUE;
                        p_ManipParams->u.hdr.insrt = TRUE; //internal frame header
                        break;
                        default :
                        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("invalid type of remove manipulation"));
                    }
                }
                break;
                default :
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("invalid type of remove manipulation"));
            }
            break;
            default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("invalid type of remove manipulation"));
        }
    }
    else if (p_ManipParams->u.hdr.insrt)
    {
        switch (p_ManipParams->u.hdr.insrtParams.type)
        {
            case (e_FM_PCD_MANIP_INSRT_BY_TEMPLATE) :

            p_Manip->opcode = HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER;
            p_Manip->muramAllocate = FALSE;
            if (p_ManipParams->fragOrReasm)
            {
                if (p_ManipParams->fragOrReasmParams.frag)
                {
                    switch (p_ManipParams->fragOrReasmParams.hdr)
                    {
                        case (HEADER_TYPE_CAPWAP):
                        p_Manip->opcode = HMAN_OC_CAPWAP_FRAGMENTATION;
                        break;
                        default:
                        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid header for fragmentation"));
                    }
                }
                else
                RETURN_ERROR(MAJOR, E_INVALID_STATE,("can not reach this point"));
            }
            break;

            default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("for only isert manipulation unsupported type"));
        }
    }
    else if (p_ManipParams->fragOrReasm)
    {
        if (p_ManipParams->fragOrReasmParams.frag)
        {
            switch (p_ManipParams->fragOrReasmParams.hdr)
            {
                case (HEADER_TYPE_CAPWAP):
                p_Manip->opcode = HMAN_OC_CAPWAP_FRAGMENTATION;
                p_Manip->muramAllocate = FALSE;
                break;
                default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Unsupported header for fragmentation"));
            }
        }
        else
        {
            switch (p_ManipParams->fragOrReasmParams.hdr)
            {
                case (HEADER_TYPE_CAPWAP):
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Reassembly has to be with additional operation - rmv = TRUE, type of remove - e_FM_PCD_MANIP_RMV_FROM_START_OF_FRAME_INCLUDE_SPECIFIC_LOCATION,type = e_FM_PCD_MANIP_LOC_BY_HDR, hdr = HEADER_TYPE_CAPWAP_DTLS"));
                default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Unsupported header for reassembly"));
            }
        }

    }
    else
    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("User didn't ask for any manipulation"));

    p_Manip->insrt = p_ManipParams->u.hdr.insrt;
    p_Manip->rmv = p_ManipParams->u.hdr.rmv;

    return E_OK;
}

#else /* not (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
static t_Error CheckManipParamsAndSetType(t_FmPcdManip *p_Manip,
                                          t_FmPcdManipParams *p_ManipParams)
{
    switch (p_ManipParams->type)
    {
        case e_FM_PCD_MANIP_HDR:
            /* Check that next-manip is not already used */
            if (p_ManipParams->h_NextManip)
            {
                if (!MANIP_IS_FIRST(p_ManipParams->h_NextManip))
                    RETURN_ERROR(
                            MAJOR, E_INVALID_STATE,
                            ("h_NextManip is already a part of another chain"));
                if ((MANIP_GET_TYPE(p_ManipParams->h_NextManip)
                        != e_FM_PCD_MANIP_HDR) &&
                        (MANIP_GET_TYPE(p_ManipParams->h_NextManip)
                        != e_FM_PCD_MANIP_FRAG))
                    RETURN_ERROR(
                            MAJOR,
                            E_NOT_SUPPORTED,
                            ("For a Header Manipulation node - no support of h_NextManip of type other than Header Manipulation or Fragmentation."));
            }

            if (p_ManipParams->u.hdr.rmv)
            {
                switch (p_ManipParams->u.hdr.rmvParams.type)
                {
                    case (e_FM_PCD_MANIP_RMV_BY_HDR):
                        switch (p_ManipParams->u.hdr.rmvParams.u.byHdr.type)
                        {
                            case (e_FM_PCD_MANIP_RMV_BY_HDR_SPECIFIC_L2):
                                break;
#if (DPAA_VERSION >= 11)
                            case (e_FM_PCD_MANIP_RMV_BY_HDR_CAPWAP):
                                break;
                            case (e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START):
                            {
                                t_Error err;
                                uint8_t prsArrayOffset;

                                err =
                                        GetPrOffsetByHeaderOrField(
                                                &p_ManipParams->u.hdr.rmvParams.u.byHdr.u.hdrInfo,
                                                &prsArrayOffset);
                                if (err)
                                    RETURN_ERROR(MAJOR, err, NO_MSG);
                                break;
                            }
#endif /* (DPAA_VERSION >= 11) */
                            default:
                                RETURN_ERROR(
                                        MAJOR,
                                        E_INVALID_STATE,
                                        ("invalid type of remove manipulation"));
                        }
                        break;
                    case (e_FM_PCD_MANIP_RMV_GENERIC):
                        break;
                    default:
                        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                                     ("invalid type of remove manipulation"));
                }
                p_Manip->opcode = HMAN_OC;
                p_Manip->muramAllocate = TRUE;
                p_Manip->rmv = TRUE;
            }
            else
                if (p_ManipParams->u.hdr.insrt)
                {
                    switch (p_ManipParams->u.hdr.insrtParams.type)
                    {
                        case (e_FM_PCD_MANIP_INSRT_BY_HDR):
                        {
                            switch (p_ManipParams->u.hdr.insrtParams.u.byHdr.type)
                            {
                                case (e_FM_PCD_MANIP_INSRT_BY_HDR_SPECIFIC_L2):
                                    /* nothing to check */
                                    break;
#if (DPAA_VERSION >= 11)
                                case (e_FM_PCD_MANIP_INSRT_BY_HDR_IP):
                                    if (p_ManipParams->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.size
                                            % 4)
                                        RETURN_ERROR(
                                                MAJOR,
                                                E_INVALID_VALUE,
                                                ("IP inserted header must be of size which is a multiple of four bytes"));
                                    break;
                                case (e_FM_PCD_MANIP_INSRT_BY_HDR_CAPWAP):
                                    if (p_ManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size
                                            % 4)
                                        RETURN_ERROR(
                                                MAJOR,
                                                E_INVALID_VALUE,
                                                ("CAPWAP inserted header must be of size which is a multiple of four bytes"));
                                    break;
                                case (e_FM_PCD_MANIP_INSRT_BY_HDR_UDP):
                                case (e_FM_PCD_MANIP_INSRT_BY_HDR_UDP_LITE):
                                    if (p_ManipParams->u.hdr.insrtParams.u.byHdr.u.insrt.size
                                            != 8)
                                        RETURN_ERROR(
                                                MAJOR,
                                                E_INVALID_VALUE,
                                                ("Inserted header must be of size 8"));
                                    break;
#endif /* (DPAA_VERSION >= 11) */
                                default:
                                    RETURN_ERROR(
                                            MAJOR,
                                            E_INVALID_STATE,
                                            ("unsupported insert by header type"));
                            }
                        }
                        case (e_FM_PCD_MANIP_INSRT_GENERIC):
                            break;
                        default:
                            RETURN_ERROR(
                                    MAJOR,
                                    E_INVALID_STATE,
                                    ("for only insert manipulation unsupported type"));
                    }
                    p_Manip->opcode = HMAN_OC;
                    p_Manip->muramAllocate = TRUE;
                    p_Manip->insrt = TRUE;
                }
                else
                    if (p_ManipParams->u.hdr.fieldUpdate)
                    {
                        /* Check parameters */
                        if (p_ManipParams->u.hdr.fieldUpdateParams.type
                                == e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN)
                        {
                            if ((p_ManipParams->u.hdr.fieldUpdateParams.u.vlan.updateType
                                    == e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN_VPRI)
                                    && (p_ManipParams->u.hdr.fieldUpdateParams.u.vlan.u.vpri
                                            > 7))
                                RETURN_ERROR(
                                        MAJOR, E_INVALID_VALUE,
                                        ("vpri should get values of 0-7 "));
                            if (p_ManipParams->u.hdr.fieldUpdateParams.u.vlan.updateType
                                    == e_FM_PCD_MANIP_HDR_FIELD_UPDATE_DSCP_TO_VLAN)
                            {
                                int i;

                                if (p_ManipParams->u.hdr.fieldUpdateParams.u.vlan.u.dscpToVpri.vpriDefVal
                                        > 7)
                                    RETURN_ERROR(
                                            MAJOR,
                                            E_INVALID_VALUE,
                                            ("vpriDefVal should get values of 0-7 "));
                                for (i = 0; i < FM_PCD_MANIP_DSCP_TO_VLAN_TRANS;
                                        i++)
                                    if (p_ManipParams->u.hdr.fieldUpdateParams.u.vlan.u.dscpToVpri.dscpToVpriTable[i]
                                            & 0xf0)
                                        RETURN_ERROR(
                                                MAJOR,
                                                E_INVALID_VALUE,
                                                ("dscpToVpriTabl value out of range (0-15)"));
                            }

                        }

                        p_Manip->opcode = HMAN_OC;
                        p_Manip->muramAllocate = TRUE;
                        p_Manip->fieldUpdate = TRUE;
                    }
                    else
                        if (p_ManipParams->u.hdr.custom)
                        {
                            if (p_ManipParams->u.hdr.customParams.type == e_FM_PCD_MANIP_HDR_CUSTOM_GEN_FIELD_REPLACE)
                            {

                            if ((p_ManipParams->u.hdr.customParams.u.genFieldReplace.size == 0) ||
                                    (p_ManipParams->u.hdr.customParams.u.genFieldReplace.size > 8))
                                RETURN_ERROR(
                                        MAJOR, E_INVALID_VALUE,
                                        ("size should get values of 1-8 "));

                            if (p_ManipParams->u.hdr.customParams.u.genFieldReplace.srcOffset > 7)
                                RETURN_ERROR(
                                        MAJOR, E_INVALID_VALUE,
                                        ("srcOffset should be <= 7"));

                            if ((p_ManipParams->u.hdr.customParams.u.genFieldReplace.srcOffset +
                                    p_ManipParams->u.hdr.customParams.u.genFieldReplace.size) > 8)
                                RETURN_ERROR(
                                        MAJOR, E_INVALID_VALUE,
                                        ("(srcOffset + size) should be <= 8"));

                            if ((p_ManipParams->u.hdr.customParams.u.genFieldReplace.dstOffset +
                                    p_ManipParams->u.hdr.customParams.u.genFieldReplace.size) > 256)
                                RETURN_ERROR(
                                        MAJOR, E_INVALID_VALUE,
                                        ("(dstOffset + size) should be <= 256"));

                            }

                            p_Manip->opcode = HMAN_OC;
                            p_Manip->muramAllocate = TRUE;
                            p_Manip->custom = TRUE;
                        }
            break;
        case e_FM_PCD_MANIP_REASSEM:
            if (p_ManipParams->h_NextManip)
                RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                             ("next manip with reassembly"));
            switch (p_ManipParams->u.reassem.hdr)
            {
                case (HEADER_TYPE_IPv4):
                    p_Manip->reassmParams.hdr = HEADER_TYPE_IPv4;
                    p_Manip->opcode = HMAN_OC_IP_REASSEMBLY;
                    break;
                case (HEADER_TYPE_IPv6):
                    p_Manip->reassmParams.hdr = HEADER_TYPE_IPv6;
                    p_Manip->opcode = HMAN_OC_IP_REASSEMBLY;
                    break;
#if (DPAA_VERSION >= 11)
                case (HEADER_TYPE_CAPWAP):
                    p_Manip->reassmParams.hdr = HEADER_TYPE_CAPWAP;
                    p_Manip->opcode = HMAN_OC_CAPWAP_REASSEMBLY;
                    break;
#endif /* (DPAA_VERSION >= 11) */
                default:
                    RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                                 ("header for reassembly"));
            }
            break;
        case e_FM_PCD_MANIP_FRAG:
            if (p_ManipParams->h_NextManip)
                RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                             ("next manip with fragmentation"));
            switch (p_ManipParams->u.frag.hdr)
            {
                case (HEADER_TYPE_IPv4):
                case (HEADER_TYPE_IPv6):
                    p_Manip->opcode = HMAN_OC_IP_FRAGMENTATION;
                    break;
#if (DPAA_VERSION >= 11)
                case (HEADER_TYPE_CAPWAP):
                    p_Manip->opcode = HMAN_OC_CAPWAP_FRAGMENTATION;
                    break;
#endif /* (DPAA_VERSION >= 11) */
                default:
                    RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                                 ("header for fragmentation"));
            }
            p_Manip->muramAllocate = TRUE;
            break;
        case e_FM_PCD_MANIP_SPECIAL_OFFLOAD:
            switch (p_ManipParams->u.specialOffload.type)
            {
                case (e_FM_PCD_MANIP_SPECIAL_OFFLOAD_IPSEC):
                    p_Manip->opcode = HMAN_OC_IPSEC_MANIP;
                    p_Manip->muramAllocate = TRUE;
                    break;
#if (DPAA_VERSION >= 11)
                case (e_FM_PCD_MANIP_SPECIAL_OFFLOAD_CAPWAP):
                    p_Manip->opcode = HMAN_OC_CAPWAP_MANIP;
                    p_Manip->muramAllocate = TRUE;
                    break;
#endif /* (DPAA_VERSION >= 11) */
                default:
                    RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                                 ("special offload type"));
            }
            break;
        default:
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("manip type"));
    }

    return E_OK;
}
#endif /* not (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))

static t_Error UpdateIndxStats(t_Handle h_FmPcd,
        t_Handle h_FmPort,
        t_FmPcdManip *p_Manip)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint32_t tmpReg32 = 0;
    t_AdOfTypeContLookup *p_Ad;
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Manip,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad,E_INVALID_HANDLE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
    if (p_Manip->h_FmPcd != h_FmPcd)
    RETURN_ERROR(MAJOR, E_INVALID_STATE,
            ("handler of PCD previously was initiated by different value"));

    memset(&fmPortGetSetCcParams, 0, sizeof(t_FmPortGetSetCcParams));

    if (!p_Manip->p_StatsTbl)
    {

        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNDN;
        fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_CC;
        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

        tmpReg32 = GET_UINT32(p_Ad->ccAdBase);

        p_Manip->p_StatsTbl =
        (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                (uint32_t)p_Manip->owner * FM_PCD_MANIP_INDEXED_STATS_ENTRY_SIZE,
                4);
        if (!p_Manip->p_StatsTbl)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for Manipulation indexed statistics table"));

        MemSet8(p_Manip->p_StatsTbl, 0, (uint32_t)(p_Manip->owner * 4));

        tmpReg32 |= (uint32_t)(XX_VirtToPhys(p_Manip->p_StatsTbl) - p_FmPcd->physicalMuramBase);

        if (p_Manip->cnia)
        tmpReg32 |= FM_PCD_MANIP_INDEXED_STATS_CNIA;

        tmpReg32 |= FM_PCD_MANIP_INDEXED_STATS_DPD;
        WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);
    }
    else
    {
        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNDN;
        fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_CC;
        err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    return E_OK;
}

static t_Error RmvHdrTillSpecLocNOrInsrtIntFrmHdr(t_FmPcdManipHdrRmvParams *p_ManipParams, t_FmPcdManip *p_Manip)
{
    t_AdOfTypeContLookup *p_Ad;
    uint32_t tmpReg32 = 0;
    uint8_t prsArrayOffset = 0;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Manip,E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_ManipParams,E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad,E_INVALID_HANDLE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
    if (p_Manip->rmv)
    {
        err = GetPrOffsetByHeaderOrField(&p_ManipParams->u.byHdr.u.fromStartByHdr.hdrInfo, &prsArrayOffset);
        if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

        tmpReg32 |= (uint32_t)prsArrayOffset << 24;
        tmpReg32 |= HMAN_RMV_HDR;
    }

    if (p_Manip->insrt)
    tmpReg32 |= HMAN_INSRT_INT_FRM_HDR;

    tmpReg32 |= (uint32_t)HMAN_OC_RMV_N_OR_INSRT_INT_FRM_HDR;

    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    return E_OK;
}

static t_Error MvIntFrameHeaderFromFrameToBufferPrefix(t_FmPcdManip *p_Manip,
        bool caamUsed)
{
    t_AdOfTypeContLookup *p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
    uint32_t tmpReg32 = 0;

    SANITY_CHECK_RETURN_ERROR(p_Ad, E_INVALID_HANDLE);

    p_Manip->updateParams |= OFFSET_OF_PR | INTERNAL_CONTEXT_OFFSET;

    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    *(uint32_t *)&p_Ad->ccAdBase = tmpReg32;

    tmpReg32 = 0;
    tmpReg32 |= HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX;
    tmpReg32 |= (uint32_t)0x16 << 16;
    *(uint32_t *)&p_Ad->pcAndOffsets = tmpReg32;

    if (caamUsed)
    *(uint32_t *)&p_Ad->gmask = 0xf0000000;

    return E_OK;
}

static t_Error CapwapRmvDtlsHdr(t_FmPcd *p_FmPcd, t_FmPcdManip *p_Manip)
{
    t_AdOfTypeContLookup *p_Ad;
    uint32_t tmpReg32 = 0;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad,E_INVALID_HANDLE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

    tmpReg32 = 0;
    tmpReg32 |= (uint32_t)HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST;
    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;


    if (p_Manip->h_Frag)
    {
        p_Manip->updateParams |= INTERNAL_CONTEXT_OFFSET;
        tmpReg32 |= (uint32_t)(XX_VirtToPhys(p_Manip->h_Frag) - (p_FmPcd->physicalMuramBase));
    }

    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    return err;
}

static t_Error CapwapReassembly(t_CapwapReassemblyParams *p_ManipParams,
        t_FmPcdManip *p_Manip,
        t_FmPcd *p_FmPcd,
        uint8_t poolId)
{
    t_Handle p_Table;
    uint32_t tmpReg32 = 0;
    int i = 0;
    uint8_t log2Num;
    uint8_t numOfSets;
    uint32_t j = 0;
    uint32_t bitFor1Micro;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!p_FmPcd->h_Hc)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,("hc port has to be initialized in this mode"));
    if (!POWER_OF_2(p_ManipParams->timeoutRoutineRequestTime))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("timeoutRoutineRequestTime has to be power of 2"));
    if (!POWER_OF_2(p_ManipParams->maxNumFramesInProcess))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,("maxNumFramesInProcess has to be power of 2"));
    if (!p_ManipParams->timeoutRoutineRequestTime && p_ManipParams->timeoutThresholdForReassmProcess)
        DBG(WARNING, ("if timeoutRoutineRequestTime 0,  timeoutThresholdForReassmProcess is uselessly"));
    if (p_ManipParams->numOfFramesPerHashEntry == e_FM_PCD_MANIP_FOUR_WAYS_HASH)
    {
        if ((p_ManipParams->maxNumFramesInProcess < 4) ||
                (p_ManipParams->maxNumFramesInProcess > 512))
        RETURN_ERROR(MAJOR,E_INVALID_VALUE, ("In the case of numOfFramesPerHashEntry = e_FM_PCD_MANIP_EIGHT_WAYS_HASH maxNumFramesInProcess has to be in the range 4-512"));
    }
    else
    {
        if ((p_ManipParams->maxNumFramesInProcess < 8) ||
                (p_ManipParams->maxNumFramesInProcess > 2048))
        RETURN_ERROR(MAJOR,E_INVALID_VALUE, ("In the case of numOfFramesPerHashEntry = e_FM_PCD_MANIP_FOUR_WAYS_HASH maxNumFramesInProcess has to be in the range 8-2048"));
    }

    bitFor1Micro = FmGetTimeStampScale(p_FmPcd->h_Fm);
    if (bitFor1Micro == 0)
        RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("Timestamp scale"));

    p_Manip->updateParams |= (NUM_OF_TASKS | OFFSET_OF_PR | OFFSET_OF_DATA | HW_PORT_ID);

    p_Manip->h_Frag = (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
            FM_PCD_MANIP_CAPWAP_REASM_TABLE_SIZE,
            FM_PCD_MANIP_CAPWAP_REASM_TABLE_ALIGN);
    if (!p_Manip->h_Frag)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc CAPWAP reassembly parameters table"));

    MemSet8(p_Manip->h_Frag, 0, FM_PCD_MANIP_CAPWAP_REASM_TABLE_SIZE);

    p_Table = (t_CapwapReasmPram *)p_Manip->h_Frag;

    p_Manip->capwapFragParams.p_AutoLearnHashTbl =
    (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
            (uint32_t)(p_ManipParams->maxNumFramesInProcess * 2 * FM_PCD_MANIP_CAPWAP_REASM_AUTO_LEARNING_HASH_ENTRY_SIZE),
            FM_PCD_MANIP_CAPWAP_REASM_TABLE_ALIGN);

    if (!p_Manip->capwapFragParams.p_AutoLearnHashTbl)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,("MURAM alloc for CAPWAP automatic learning hash table"));

    MemSet8(p_Manip->capwapFragParams.p_AutoLearnHashTbl, 0, (uint32_t)(p_ManipParams->maxNumFramesInProcess * 2 * FM_PCD_MANIP_CAPWAP_REASM_AUTO_LEARNING_HASH_ENTRY_SIZE));

    tmpReg32 = (uint32_t)(XX_VirtToPhys(p_Manip->capwapFragParams.p_AutoLearnHashTbl) - p_FmPcd->physicalMuramBase);

    WRITE_UINT32(((t_CapwapReasmPram *)p_Table)->autoLearnHashTblPtr, tmpReg32);

    tmpReg32 = 0;
    if (p_ManipParams->timeOutMode == e_FM_PCD_MANIP_TIME_OUT_BETWEEN_FRAMES)
        tmpReg32 |= FM_PCD_MANIP_CAPWAP_REASM_TIME_OUT_BETWEEN_FRAMES;
    if (p_ManipParams->haltOnDuplicationFrag)
        tmpReg32 |= FM_PCD_MANIP_CAPWAP_REASM_HALT_ON_DUPLICATE_FRAG;
    if (p_ManipParams->numOfFramesPerHashEntry == e_FM_PCD_MANIP_EIGHT_WAYS_HASH)
    {
        i = 8;
        tmpReg32 |= FM_PCD_MANIP_CAPWAP_REASM_AUTOMATIC_LEARNIN_HASH_8_WAYS;
    }
    else
    i = 4;

    numOfSets = (uint8_t)((p_ManipParams->maxNumFramesInProcess * 2) / i);
    LOG2(numOfSets, log2Num);
    tmpReg32 |= (uint32_t)(log2Num - 1) << 24;

    WRITE_UINT32(((t_CapwapReasmPram *)p_Table)->mode, tmpReg32);

    for (j=0; j<p_ManipParams->maxNumFramesInProcess*2; j++)
        if (((j / i) % 2)== 0)
            WRITE_UINT32(*(uint32_t *)PTR_MOVE(p_Manip->capwapFragParams.p_AutoLearnHashTbl, j * FM_PCD_MANIP_CAPWAP_REASM_AUTO_LEARNING_HASH_ENTRY_SIZE), 0x80000000);

    tmpReg32 = 0x00008000;
    tmpReg32 |= (uint32_t)poolId << 16;
    WRITE_UINT32(((t_CapwapReasmPram *)p_Table)->bufferPoolIdAndRisc1SetIndexes, tmpReg32);
    WRITE_UINT32(((t_CapwapReasmPram *)p_Table)->risc23SetIndexes, 0x80008000);
    WRITE_UINT32(((t_CapwapReasmPram *)p_Table)->risc4SetIndexesAndExtendedStatsTblPtr, 0x80000000);

    p_Manip->capwapFragParams.maxNumFramesInProcess = p_ManipParams->maxNumFramesInProcess;

    p_Manip->capwapFragParams.sgBpid = poolId;

    p_Manip->capwapFragParams.fqidForTimeOutFrames = p_ManipParams->fqidForTimeOutFrames;
    p_Manip->capwapFragParams.timeoutRoutineRequestTime = p_ManipParams->timeoutRoutineRequestTime;
    p_Manip->capwapFragParams.bitFor1Micro = bitFor1Micro;

    tmpReg32 = 0;
    tmpReg32 |= (((uint32_t)1<<p_Manip->capwapFragParams.bitFor1Micro) * p_ManipParams->timeoutThresholdForReassmProcess);
    WRITE_UINT32(((t_CapwapReasmPram *)p_Table)->expirationDelay, tmpReg32);

    return E_OK;
}

static t_Error CapwapFragmentation(t_CapwapFragmentationParams *p_ManipParams,
        t_FmPcdManip *p_Manip,
        t_FmPcd *p_FmPcd,
        uint8_t poolId)
{
    t_AdOfTypeContLookup *p_Ad;
    uint32_t tmpReg32 = 0;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad,E_INVALID_HANDLE);

    p_Manip->updateParams |= OFFSET_OF_DATA;

    p_Manip->frag = TRUE;

    p_Manip->h_Frag = (t_Handle)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
            FM_PCD_CC_AD_ENTRY_SIZE,
            FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_Manip->h_Frag)
    RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for CAPWAP fragmentation table descriptor"));

    MemSet8(p_Manip->h_Frag, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Frag;

    tmpReg32 = 0;
    tmpReg32 |= (uint32_t)HMAN_OC_CAPWAP_FRAGMENTATION;

    if (p_ManipParams->headerOptionsCompr)
    tmpReg32 |= FM_PCD_MANIP_CAPWAP_FRAG_COMPR_OPTION_FIELD_EN;
    tmpReg32 |= ((uint32_t)poolId << 8);
    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    p_Manip->sizeForFragmentation = p_ManipParams->sizeForFragmentation;
    p_Manip->capwapFragParams.sgBpid = poolId;

    return E_OK;
}

static t_Error IndxStats(t_FmPcdStatsParams *p_StatsParams,t_FmPcdManip *p_Manip,t_FmPcd *p_FmPcd)
{
    t_AdOfTypeContLookup *p_Ad;
    uint32_t tmpReg32 = 0;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad,E_INVALID_HANDLE);

    UNUSED(p_FmPcd);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

    tmpReg32 = 0;
    tmpReg32 |= (uint32_t)HMAN_OC_CAPWAP_INDEXED_STATS;
    if (p_StatsParams->type == e_FM_PCD_STATS_PER_FLOWID)
    tmpReg32 |= (uint32_t)0x16 << 16;
    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    return E_OK;
}

static t_Error InsrtHdrByTempl(t_FmPcdManipHdrInsrtParams *p_ManipParams, t_FmPcdManip *p_Manip, t_FmPcd *p_FmPcd)
{
    t_FmPcdManipHdrInsrtByTemplateParams *p_InsrtByTemplate = &p_ManipParams->u.byTemplate;
    uint8_t tmpReg8 = 0xff;
    t_AdOfTypeContLookup *p_Ad;
    bool ipModify = FALSE;
    uint32_t tmpReg32 = 0, tmpRegNia = 0;
    uint16_t tmpReg16 = 0;
    t_Error err = E_OK;
    uint8_t extraAddedBytes = 0, blockSize = 0, extraAddedBytesAlignedToBlockSize = 0, log2Num = 0;
    uint8_t *p_Template = NULL;

    SANITY_CHECK_RETURN_ERROR(p_ManipParams,E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_Manip,E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad,E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd,E_NULL_POINTER);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
    if (p_Manip->insrt)
    {
        if ((!p_InsrtByTemplate->size && p_InsrtByTemplate->modifyOuterIp) ||
                (!p_InsrtByTemplate->size && p_InsrtByTemplate->modifyOuterVlan))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inconsistent parameters : asking for header template modifications with no template for insertion (template size)"));

        if (p_InsrtByTemplate->size && p_InsrtByTemplate->modifyOuterIp && (p_InsrtByTemplate->size <= p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inconsistent parameters : size of template < ipOuterOffset"));

        if (p_InsrtByTemplate->size > 128)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Size of header template for insertion can not be more than 128"));

        if (p_InsrtByTemplate->size)
        {
            p_Manip->p_Template = (uint8_t *)FM_MURAM_AllocMem(p_FmPcd->h_FmMuram,
                    p_InsrtByTemplate->size,
                    FM_PCD_CC_AD_TABLE_ALIGN);
            if(!p_Manip->p_Template)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory allocation in MURAM FAILED"));

            tmpReg32 = (uint32_t)(XX_VirtToPhys(p_Manip->p_Template) - (p_FmPcd->physicalMuramBase));
            tmpReg32 |= (uint32_t)p_InsrtByTemplate->size << 24;
            *(uint32_t *)&p_Ad->matchTblPtr = tmpReg32;
        }

        tmpReg32 = 0;

        p_Template = (uint8_t *)XX_Malloc(p_InsrtByTemplate->size * sizeof(uint8_t));

        if (!p_Template)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("XX_Malloc allocation FAILED"));

        memcpy(p_Template, p_InsrtByTemplate->hdrTemplate, p_InsrtByTemplate->size * sizeof(uint8_t));

        if (p_InsrtByTemplate->modifyOuterIp)
        {
            ipModify = TRUE;

            tmpReg8 = (uint8_t)p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset];

            if((tmpReg8 & 0xf0) == 0x40)
            tmpReg8 = 4;
            else if((tmpReg8 & 0xf0) == 0x60)
            tmpReg8 = 6;
            else
            tmpReg8 = 0xff;

            if (tmpReg8 != 0xff)
            {
                if(p_InsrtByTemplate->modifyOuterIpParams.dscpEcn & 0xff00)
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inconsistent parameters : IPV4 present in header template, dscpEcn has to be only 1 byte"));
                if(p_InsrtByTemplate->modifyOuterIpParams.recalculateLength)
                {

                    if((p_InsrtByTemplate->modifyOuterIpParams.recalculateLengthParams.extraBytesAddedAlignedToBlockSize + p_InsrtByTemplate->modifyOuterIpParams.recalculateLengthParams.extraBytesAddedNotAlignedToBlockSize) > 255)
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("extra Byte added can not be more than 256 bytes"));
                    extraAddedBytes = (uint8_t) (p_InsrtByTemplate->modifyOuterIpParams.recalculateLengthParams.extraBytesAddedAlignedToBlockSize + p_InsrtByTemplate->modifyOuterIpParams.recalculateLengthParams.extraBytesAddedNotAlignedToBlockSize);
                    blockSize = p_InsrtByTemplate->modifyOuterIpParams.recalculateLengthParams.blockSize;
                    extraAddedBytesAlignedToBlockSize = p_InsrtByTemplate->modifyOuterIpParams.recalculateLengthParams.extraBytesAddedAlignedToBlockSize;
                    /*IP header template - IP totalLength -
                     (1 byte) extraByteForIp = headerTemplateSize - ipOffset + insertedBytesAfterThisStage ,
                     in the case of SEC insertedBytesAfterThisStage - SEC trailer (21/31) + header(13)
                     second byte - extraByteForIp = headerTemplate - ipOffset + insertedBytesAfterThisStage*/
                }
                if (blockSize)
                {
                    if (!POWER_OF_2(blockSize))
                    RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("inputFrmPaddingUpToBlockSize has to be power of 2"));
                }

            }
            if (tmpReg8 == 4)
            {
                if ((IPv4_HDRCHECKSUM_FIELD_OFFSET_FROM_IP + p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset) > p_InsrtByTemplate->size)
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inconsistent parameters : IP present in header template, user asked for IP modifications but ipOffset + ipTotalLengthFieldOffset in header template bigger than template size"));

                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_DSCECN_FIELD_OFFSET_FROM_IP] = (uint8_t)p_InsrtByTemplate->modifyOuterIpParams.dscpEcn;

                if (blockSize)
                blockSize -= 1;

                if ((p_InsrtByTemplate->size - p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + extraAddedBytes) > 255)
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("p_InsrtByTemplate->size - p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + extraAddedBytes has to be less than 255"));

                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_TOTALLENGTH_FIELD_OFFSET_FROM_IP + 1] = blockSize; // IPV6 - in AD instead of SEQ IND
                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_TOTALLENGTH_FIELD_OFFSET_FROM_IP] = (uint8_t)(p_InsrtByTemplate->size - p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + extraAddedBytes);// for IPV6 decrement additional 40 bytes of IPV6 heade size

                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_ID_FIELD_OFFSET_FROM_IP] = 0x00;
                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_ID_FIELD_OFFSET_FROM_IP + 1] = extraAddedBytesAlignedToBlockSize;

                /*IP header template - relevant only for ipv4 CheckSum = 0*/
                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_HDRCHECKSUM_FIELD_OFFSET_FROM_IP] = 0x00;
                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv4_HDRCHECKSUM_FIELD_OFFSET_FROM_IP + 1] = 0x00;

                /*UDP checksum has to be 0*/
                if (p_InsrtByTemplate->modifyOuterIpParams.udpPresent)
                {
                    if ((p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP + UDP_CHECKSUM_FIELD_SIZE) > p_InsrtByTemplate->size)
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inconsistent parameters : UDP present according to user but (UDP offset + UDP header size) < size of header template"));

                    p_Template[p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP ] = 0x00;
                    p_Template[p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP + 1] = 0x00;

                }

                if (p_InsrtByTemplate->modifyOuterIpParams.ipIdentGenId > 7)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("ipIdentGenId has to be one out of 8 sequence number generators (0 - 7) for IP identification field"));

                tmpRegNia |= (uint32_t)p_InsrtByTemplate->modifyOuterIpParams.ipIdentGenId<<24;
            }
            else if (tmpReg8 == 6)
            {
                /*TODO - add check for maximum value of blockSize;*/
                if (blockSize)
                LOG2(blockSize, log2Num);
                tmpRegNia |= (uint32_t)log2Num << 24;

                // for IPV6 decrement additional 40 bytes of IPV6 heade size - because IPV6 header size is not included in payloadLength
                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv6_PAYLOAD_LENGTH_OFFSET_FROM_IP] = (uint8_t)(p_InsrtByTemplate->size - p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + extraAddedBytes - 40);
                p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv6_PAYLOAD_LENGTH_OFFSET_FROM_IP + 1] = extraAddedBytesAlignedToBlockSize;
                if (p_InsrtByTemplate->modifyOuterIpParams.udpPresent)
                {
                    if ((p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP + UDP_CHECKSUM_FIELD_SIZE) > p_InsrtByTemplate->size)
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inconsistent parameters : UDP present according to user but (UDP offset + UDP header size) < size of header template"));
                    if (p_Template[p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset + IPv6_NEXT_HEADER_OFFSET_FROM_IP] != 0x88)
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("OUr suppport is only IPv6/UDPLite"));
                    p_Template[p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_LENGTH_FIELD_OFFSET_FROM_UDP] = 0x00;
                    p_Template[p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_LENGTH_FIELD_OFFSET_FROM_UDP + 1] = 0x08;
                    p_Template[p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP] = 0x00;
                    p_Template[p_InsrtByTemplate->modifyOuterIpParams.udpOffset + UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP + 1] = 0x00;
                }
            }
            else
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("IP version supported only IPV4"));
        }

        tmpReg32 = tmpReg16 = tmpReg8 = 0;
        /*TODO - check it*/
        if (p_InsrtByTemplate->modifyOuterVlan)
        {
            if (p_InsrtByTemplate->modifyOuterVlanParams.vpri & ~0x07)
            RETURN_ERROR(MAJOR, E_INVALID_STATE,("Inconsistent parameters : user asked for VLAN modifications but VPRI more than 3 bits"));

            memcpy(&tmpReg16, &p_Template[VLAN_TAG_FIELD_OFFSET_FROM_ETH], 2*(sizeof(uint8_t)));
            if ((tmpReg16 != 0x9100) && (tmpReg16!= 0x9200) && (tmpReg16 != 0x8100))
            RETURN_ERROR(MAJOR, E_INVALID_STATE,("Inconsistent parameters : user asked for VLAN modifications but Tag Protocol identifier is not VLAN "));

            memcpy(&tmpReg8, &p_Template[14],1*(sizeof(uint8_t)));
            tmpReg8 &= 0x1f;
            tmpReg8 |= (uint8_t)(p_InsrtByTemplate->modifyOuterVlanParams.vpri << 5);

            p_Template[14] = tmpReg8;
        }

        MemCpy8(p_Manip->p_Template, p_Template, p_InsrtByTemplate->size);

        XX_Free(p_Template);
    }

    tmpReg32 = 0;
    if (p_Manip->h_Frag)
    {
        tmpRegNia |= (uint32_t)(XX_VirtToPhys(p_Manip->h_Frag) - (p_FmPcd->physicalMuramBase));
        tmpReg32 |= (uint32_t)p_Manip->sizeForFragmentation << 16;
    }
    else
    tmpReg32 = 0xffff0000;

    if (ipModify)
    tmpReg32 |= (uint32_t)p_InsrtByTemplate->modifyOuterIpParams.ipOuterOffset << 8;
    else
    tmpReg32 |= (uint32_t)0x0000ff00;

    tmpReg32 |= (uint32_t)HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER;
    *(uint32_t *)&p_Ad->pcAndOffsets = tmpReg32;

    tmpRegNia |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    *(uint32_t *)&p_Ad->ccAdBase = tmpRegNia;

    return err;
}

static t_Error CheckStatsParamsAndSetType(t_FmPcdManip *p_Manip, t_FmPcdStatsParams *p_StatsParams)
{

    switch (p_StatsParams->type)
    {
        case (e_FM_PCD_STATS_PER_FLOWID):
        p_Manip->opcode = HMAN_OC_CAPWAP_INDEXED_STATS;
        p_Manip->muramAllocate = TRUE;
        break;
        default:
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Unsupported statistics type"));
    }

    return E_OK;
}
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */

static t_Error FillReassmManipParams(t_FmPcdManip *p_Manip, e_NetHeaderType hdr)
{
    t_AdOfTypeContLookup *p_Ad;
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_Manip->h_FmPcd;
    uint32_t tmpReg32;
    t_Error err = E_OK;

    /* Creates the Reassembly Parameters table. It contains parameters that are specific to either the IPv4 reassembly
     function or to the IPv6 reassembly function. If both IPv4 reassembly and IPv6 reassembly are required, then
     two separate IP Reassembly Parameter tables are required.*/
    if ((err = CreateReassTable(p_Manip, hdr)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /* Sets the first Ad register (ccAdBase) - Action Descriptor Type and Pointer to the Reassembly Parameters Table offset from MURAM*/
    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;

    /* Gets the required Action descriptor table pointer */
    switch (hdr)
    {
        case HEADER_TYPE_IPv4:
            p_Ad = (t_AdOfTypeContLookup *)p_Manip->reassmParams.ip.h_Ipv4Ad;
            tmpReg32 |= (uint32_t)(XX_VirtToPhys(
                    p_Manip->reassmParams.ip.p_Ipv4ReassTbl)
                    - (p_FmPcd->physicalMuramBase));
            break;
        case HEADER_TYPE_IPv6:
            p_Ad = (t_AdOfTypeContLookup *)p_Manip->reassmParams.ip.h_Ipv6Ad;
            tmpReg32 |= (uint32_t)(XX_VirtToPhys(
                    p_Manip->reassmParams.ip.p_Ipv6ReassTbl)
                    - (p_FmPcd->physicalMuramBase));
            break;
        case HEADER_TYPE_CAPWAP:
            p_Ad = (t_AdOfTypeContLookup *)p_Manip->reassmParams.capwap.h_Ad;
            tmpReg32 |= (uint32_t)(XX_VirtToPhys(
                    p_Manip->reassmParams.capwap.p_ReassTbl)
                    - (p_FmPcd->physicalMuramBase));
            break;
        default:
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("header type"));
    }

    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    /* Sets the second Ad register (matchTblPtr) - Buffer pool ID (BPID for V2) and Scatter/Gather table offset*/
    /* mark the Scatter/Gather table offset to be set later on when the port will be known */
    p_Manip->updateParams = (NUM_OF_TASKS | NUM_OF_EXTRA_TASKS | DISCARD_MASK);

    if ((hdr == HEADER_TYPE_IPv6) || (hdr == HEADER_TYPE_IPv4))
    {
#if (DPAA_VERSION == 10)
        tmpReg32 = (uint32_t)(p_Manip->reassmParams.sgBpid << 8);
        WRITE_UINT32(p_Ad->matchTblPtr, tmpReg32);
#endif /* (DPAA_VERSION == 10) */
#if (DPAA_VERSION >= 11)
        if (p_Manip->reassmParams.ip.nonConsistentSpFqid != 0)
        {
            tmpReg32 = FM_PCD_AD_NCSPFQIDM_MASK
                    | (uint32_t)(p_Manip->reassmParams.ip.nonConsistentSpFqid);
            WRITE_UINT32(p_Ad->gmask, tmpReg32);
        }
#endif /* (DPAA_VERSION >= 11) */
        /* Sets the third Ad register (pcAndOffsets)- IP Reassemble Operation Code*/
        tmpReg32 = 0;
        tmpReg32 |= (uint32_t)HMAN_OC_IP_REASSEMBLY;
    }
#if (DPAA_VERSION >= 11)
    else
        if (hdr == HEADER_TYPE_CAPWAP)
        {
            tmpReg32 = 0;
            tmpReg32 |= (uint32_t)HMAN_OC_CAPWAP_REASSEMBLY;
        }
#endif /* (DPAA_VERSION >= 11) */

    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    p_Manip->reassm = TRUE;

    return E_OK;
}

static t_Error SetIpv4ReassmManip(t_FmPcdManip *p_Manip)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_Manip->h_FmPcd;

    /* Allocation if IPv4 Action descriptor */
    p_Manip->reassmParams.ip.h_Ipv4Ad = (t_Handle)XX_MallocSmart(
            FM_PCD_CC_AD_ENTRY_SIZE, p_Manip->reassmParams.dataMemId,
            FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_Manip->reassmParams.ip.h_Ipv4Ad)
    {
        ReleaseManipHandler(p_Manip, p_FmPcd);
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("Allocation of IPv4 table descriptor"));
    }

    memset(p_Manip->reassmParams.ip.h_Ipv4Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* Fill reassembly manipulation parameter in the IP Reassembly Action Descriptor */
    return FillReassmManipParams(p_Manip, HEADER_TYPE_IPv4);
}

static t_Error SetIpv6ReassmManip(t_FmPcdManip *p_Manip)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_Manip->h_FmPcd;

    /* Allocation if IPv6 Action descriptor */
    p_Manip->reassmParams.ip.h_Ipv6Ad = (t_Handle)XX_MallocSmart(
            FM_PCD_CC_AD_ENTRY_SIZE, p_Manip->reassmParams.dataMemId,
            FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_Manip->reassmParams.ip.h_Ipv6Ad)
    {
        ReleaseManipHandler(p_Manip, p_FmPcd);
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("Allocation of IPv6 table descriptor"));
    }

    memset(p_Manip->reassmParams.ip.h_Ipv6Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* Fill reassembly manipulation parameter in the IP Reassembly Action Descriptor */
    return FillReassmManipParams(p_Manip, HEADER_TYPE_IPv6);
}

static t_Error IpReassembly(t_FmPcdManipReassemParams *p_ManipReassmParams,
                            t_FmPcdManip *p_Manip)
{
    uint32_t maxSetNumber = 10000;
    t_FmPcdManipReassemIpParams reassmManipParams =
            p_ManipReassmParams->u.ipReassem;
    t_Error res;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((t_FmPcd *)p_Manip->h_FmPcd)->h_Hc,
                              E_INVALID_HANDLE);

    /* Check validation of user's parameter.*/
    if ((reassmManipParams.timeoutThresholdForReassmProcess < 1000)
            || (reassmManipParams.timeoutThresholdForReassmProcess > 8000000))
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("timeoutThresholdForReassmProcess should be 1msec - 8sec"));
    /* It is recommended that the total number of entries in this table (number of sets * number of ways)
     will be twice the number of frames that are expected to be reassembled simultaneously.*/
    if (reassmManipParams.maxNumFramesInProcess
            > (reassmManipParams.maxNumFramesInProcess * maxSetNumber / 2))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("maxNumFramesInProcess has to be less than (maximun set number * number of ways / 2)"));

    if ((p_ManipReassmParams->hdr == HEADER_TYPE_IPv6)
            && (reassmManipParams.minFragSize[1] < 256))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("minFragSize[1] must be >= 256"));

    /* Saves user's reassembly manipulation parameters */
    p_Manip->reassmParams.ip.relativeSchemeId[0] =
            reassmManipParams.relativeSchemeId[0];
    p_Manip->reassmParams.ip.relativeSchemeId[1] =
            reassmManipParams.relativeSchemeId[1];
    p_Manip->reassmParams.ip.numOfFramesPerHashEntry[0] =
            reassmManipParams.numOfFramesPerHashEntry[0];
    p_Manip->reassmParams.ip.numOfFramesPerHashEntry[1] =
            reassmManipParams.numOfFramesPerHashEntry[1];
    p_Manip->reassmParams.ip.minFragSize[0] = reassmManipParams.minFragSize[0];
    p_Manip->reassmParams.ip.minFragSize[1] = reassmManipParams.minFragSize[1];
    p_Manip->reassmParams.maxNumFramesInProcess =
            reassmManipParams.maxNumFramesInProcess;
    p_Manip->reassmParams.timeOutMode = reassmManipParams.timeOutMode;
    p_Manip->reassmParams.fqidForTimeOutFrames =
            reassmManipParams.fqidForTimeOutFrames;
    p_Manip->reassmParams.timeoutThresholdForReassmProcess =
            reassmManipParams.timeoutThresholdForReassmProcess;
    p_Manip->reassmParams.dataMemId = reassmManipParams.dataMemId;
    p_Manip->reassmParams.dataLiodnOffset = reassmManipParams.dataLiodnOffset;
#if (DPAA_VERSION == 10)
    p_Manip->reassmParams.sgBpid = reassmManipParams.sgBpid;
#endif /* (DPAA_VERSION == 10) */
#if (DPAA_VERSION >= 11)
    if (reassmManipParams.nonConsistentSpFqid != 0)
    {
        p_Manip->reassmParams.ip.nonConsistentSpFqid =
                reassmManipParams.nonConsistentSpFqid;
    }
#endif /* (DPAA_VERSION >= 11) */

    /* Creates and initializes the IP Reassembly common parameter table */
    CreateReassCommonTable(p_Manip);

    /* Creation of IPv4 reassembly manipulation */
    if ((p_Manip->reassmParams.hdr == HEADER_TYPE_IPv6)
            || (p_Manip->reassmParams.hdr == HEADER_TYPE_IPv4))
    {
        res = SetIpv4ReassmManip(p_Manip);
        if (res != E_OK)
            return res;
    }

    /* Creation of IPv6 reassembly manipulation */
    if (p_Manip->reassmParams.hdr == HEADER_TYPE_IPv6)
    {
        res = SetIpv6ReassmManip(p_Manip);
        if (res != E_OK)
            return res;
    }

    return E_OK;
}

static void setIpReassmSchemeParams(t_FmPcd* p_FmPcd,
                                    t_FmPcdKgSchemeParams *p_Scheme,
                                    t_Handle h_CcTree, bool ipv4,
                                    uint8_t groupId)
{
    uint32_t j;
    uint8_t res;

    /* Configures scheme's network environment parameters */
    p_Scheme->netEnvParams.numOfDistinctionUnits = 2;
    if (ipv4)
        res = FmPcdNetEnvGetUnitId(
                p_FmPcd, FmPcdGetNetEnvId(p_Scheme->netEnvParams.h_NetEnv),
                HEADER_TYPE_IPv4, FALSE, 0);
    else
        res = FmPcdNetEnvGetUnitId(
                p_FmPcd, FmPcdGetNetEnvId(p_Scheme->netEnvParams.h_NetEnv),
                HEADER_TYPE_IPv6, FALSE, 0);
    ASSERT_COND(res != FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS);
    p_Scheme->netEnvParams.unitIds[0] = res;

    res = FmPcdNetEnvGetUnitId(
            p_FmPcd, FmPcdGetNetEnvId(p_Scheme->netEnvParams.h_NetEnv),
            HEADER_TYPE_USER_DEFINED_SHIM2, FALSE, 0);
    ASSERT_COND(res != FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS);
    p_Scheme->netEnvParams.unitIds[1] = res;

    /* Configures scheme's next engine parameters*/
    p_Scheme->nextEngine = e_FM_PCD_CC;
    p_Scheme->kgNextEngineParams.cc.h_CcTree = h_CcTree;
    p_Scheme->kgNextEngineParams.cc.grpId = groupId;
    p_Scheme->useHash = TRUE;

    /* Configures scheme's key*/
    if (ipv4 == TRUE)
    {
        p_Scheme->keyExtractAndHashParams.numOfUsedExtracts = 4;
        p_Scheme->keyExtractAndHashParams.extractArray[0].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[0].extractByHdr.type =
                e_FM_PCD_EXTRACT_FULL_FIELD;
        p_Scheme->keyExtractAndHashParams.extractArray[0].extractByHdr.hdr =
                HEADER_TYPE_IPv4;
        p_Scheme->keyExtractAndHashParams.extractArray[0].extractByHdr.extractByHdrType.fullField.ipv4 =
                NET_HEADER_FIELD_IPv4_DST_IP;
        p_Scheme->keyExtractAndHashParams.extractArray[1].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[1].extractByHdr.type =
                e_FM_PCD_EXTRACT_FULL_FIELD;
        p_Scheme->keyExtractAndHashParams.extractArray[1].extractByHdr.hdr =
                HEADER_TYPE_IPv4;
        p_Scheme->keyExtractAndHashParams.extractArray[1].extractByHdr.extractByHdrType.fullField.ipv4 =
                NET_HEADER_FIELD_IPv4_SRC_IP;
        p_Scheme->keyExtractAndHashParams.extractArray[2].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.type =
                e_FM_PCD_EXTRACT_FULL_FIELD;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.hdr =
                HEADER_TYPE_IPv4;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.extractByHdrType.fullField.ipv4 =
                NET_HEADER_FIELD_IPv4_PROTO;
        p_Scheme->keyExtractAndHashParams.extractArray[3].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[3].extractByHdr.hdr =
                HEADER_TYPE_IPv4;
        p_Scheme->keyExtractAndHashParams.extractArray[3].extractByHdr.type =
                e_FM_PCD_EXTRACT_FROM_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[3].extractByHdr.ignoreProtocolValidation =
                FALSE;
        p_Scheme->keyExtractAndHashParams.extractArray[3].extractByHdr.extractByHdrType.fromHdr.size =
                2;
        p_Scheme->keyExtractAndHashParams.extractArray[3].extractByHdr.extractByHdrType.fromHdr.offset =
                4;
    }
    else /* IPv6 */
    {
        p_Scheme->keyExtractAndHashParams.numOfUsedExtracts = 3;
        p_Scheme->keyExtractAndHashParams.extractArray[0].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[0].extractByHdr.type =
                e_FM_PCD_EXTRACT_FULL_FIELD;
        p_Scheme->keyExtractAndHashParams.extractArray[0].extractByHdr.hdr =
                HEADER_TYPE_IPv6;
        p_Scheme->keyExtractAndHashParams.extractArray[0].extractByHdr.extractByHdrType.fullField.ipv6 =
                NET_HEADER_FIELD_IPv6_DST_IP;
        p_Scheme->keyExtractAndHashParams.extractArray[1].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[1].extractByHdr.type =
                e_FM_PCD_EXTRACT_FULL_FIELD;
        p_Scheme->keyExtractAndHashParams.extractArray[1].extractByHdr.hdr =
                HEADER_TYPE_IPv6;
        p_Scheme->keyExtractAndHashParams.extractArray[1].extractByHdr.extractByHdrType.fullField.ipv6 =
                NET_HEADER_FIELD_IPv6_SRC_IP;
        p_Scheme->keyExtractAndHashParams.extractArray[2].type =
                e_FM_PCD_EXTRACT_BY_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.hdr =
                HEADER_TYPE_USER_DEFINED_SHIM2;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.type =
                e_FM_PCD_EXTRACT_FROM_HDR;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.extractByHdrType.fromHdr.size =
                4;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.extractByHdrType.fromHdr.offset =
                4;
        p_Scheme->keyExtractAndHashParams.extractArray[2].extractByHdr.ignoreProtocolValidation =
                TRUE;
    }

    p_Scheme->keyExtractAndHashParams.privateDflt0 = 0x01020304;
    p_Scheme->keyExtractAndHashParams.privateDflt1 = 0x11121314;
    p_Scheme->keyExtractAndHashParams.numOfUsedDflts =
            FM_PCD_KG_NUM_OF_DEFAULT_GROUPS;
    for (j = 0; j < FM_PCD_KG_NUM_OF_DEFAULT_GROUPS; j++)
    {
        p_Scheme->keyExtractAndHashParams.dflts[j].type =
                (e_FmPcdKgKnownFieldsDfltTypes)j; /* all types */
        p_Scheme->keyExtractAndHashParams.dflts[j].dfltSelect =
                e_FM_PCD_KG_DFLT_GBL_0;
    }
}

static t_Error IpReassemblyStats(t_FmPcdManip *p_Manip,
                                 t_FmPcdManipReassemIpStats *p_Stats)
{
    ASSERT_COND(p_Manip);
    ASSERT_COND(p_Stats);
    ASSERT_COND(p_Manip->reassmParams.p_ReassCommonTbl);

    p_Stats->timeout =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalTimeOutCounter);
    p_Stats->rfdPoolBusy =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalRfdPoolBusyCounter);
    p_Stats->internalBufferBusy =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalInternalBufferBusy);
    p_Stats->externalBufferBusy =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalExternalBufferBusy);
    p_Stats->sgFragments =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalSgFragmentCounter);
    p_Stats->dmaSemaphoreDepletion =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalDmaSemaphoreDepletionCounter);
#if (DPAA_VERSION >= 11)
    p_Stats->nonConsistentSp =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalNCSPCounter);
#endif /* (DPAA_VERSION >= 11) */

    if (p_Manip->reassmParams.ip.p_Ipv4ReassTbl)
    {
        p_Stats->specificHdrStatistics[0].successfullyReassembled =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalSuccessfullyReasmFramesCounter);
        p_Stats->specificHdrStatistics[0].validFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalValidFragmentCounter);
        p_Stats->specificHdrStatistics[0].processedFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalProcessedFragCounter);
        p_Stats->specificHdrStatistics[0].malformedFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalMalformdFragCounter);
        p_Stats->specificHdrStatistics[0].autoLearnBusy =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalSetBusyCounter);
        p_Stats->specificHdrStatistics[0].discardedFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalDiscardedFragsCounter);
        p_Stats->specificHdrStatistics[0].moreThan16Fragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv4ReassTbl->totalMoreThan16FramesCounter);
    }
    if (p_Manip->reassmParams.ip.p_Ipv6ReassTbl)
    {
        p_Stats->specificHdrStatistics[1].successfullyReassembled =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalSuccessfullyReasmFramesCounter);
        p_Stats->specificHdrStatistics[1].validFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalValidFragmentCounter);
        p_Stats->specificHdrStatistics[1].processedFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalProcessedFragCounter);
        p_Stats->specificHdrStatistics[1].malformedFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalMalformdFragCounter);
        p_Stats->specificHdrStatistics[1].autoLearnBusy =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalSetBusyCounter);
        p_Stats->specificHdrStatistics[1].discardedFragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalDiscardedFragsCounter);
        p_Stats->specificHdrStatistics[1].moreThan16Fragments =
                GET_UINT32(p_Manip->reassmParams.ip.p_Ipv6ReassTbl->totalMoreThan16FramesCounter);
    }
    return E_OK;
}

static t_Error IpFragmentationStats(t_FmPcdManip *p_Manip,
                                    t_FmPcdManipFragIpStats *p_Stats)
{
    t_AdOfTypeContLookup *p_Ad;

    ASSERT_COND(p_Manip);
    ASSERT_COND(p_Stats);
    ASSERT_COND(p_Manip->h_Ad);
    ASSERT_COND(p_Manip->fragParams.p_Frag);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

    p_Stats->totalFrames = GET_UINT32(p_Ad->gmask);
    p_Stats->fragmentedFrames = GET_UINT32(p_Manip->fragParams.p_Frag->ccAdBase)
            & 0x00ffffff;
    p_Stats->generatedFragments =
            GET_UINT32(p_Manip->fragParams.p_Frag->matchTblPtr);

    return E_OK;
}

static t_Error IpFragmentation(t_FmPcdManipFragIpParams *p_ManipParams,
                               t_FmPcdManip *p_Manip)
{
    uint32_t pcAndOffsetsReg = 0, ccAdBaseReg = 0, gmaskReg = 0;
    t_FmPcd *p_FmPcd;
#if (DPAA_VERSION == 10)
    t_Error err = E_OK;
#endif /* (DPAA_VERSION == 10) */

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ManipParams->sizeForFragmentation != 0xFFFF,
                              E_INVALID_VALUE);

    p_FmPcd = p_Manip->h_FmPcd;
    /* Allocation of fragmentation Action Descriptor */
    p_Manip->fragParams.p_Frag = (t_AdOfTypeContLookup *)FM_MURAM_AllocMem(
            p_FmPcd->h_FmMuram, FM_PCD_CC_AD_ENTRY_SIZE,
            FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_Manip->fragParams.p_Frag)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM alloc for Fragmentation table descriptor"));
    MemSet8(p_Manip->fragParams.p_Frag, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* Prepare the third Ad register (pcAndOffsets)- OperationCode */
    pcAndOffsetsReg = (uint32_t)HMAN_OC_IP_FRAGMENTATION;

    /* Prepare the first Ad register (ccAdBase) - Don't frag action and Action descriptor type*/
    ccAdBaseReg = FM_PCD_AD_CONT_LOOKUP_TYPE;
    ccAdBaseReg |= (p_ManipParams->dontFragAction
            << FM_PCD_MANIP_IP_FRAG_DF_SHIFT);


    /* Set Scatter/Gather BPid */
    if (p_ManipParams->sgBpidEn)
    {
        ccAdBaseReg |= FM_PCD_MANIP_IP_FRAG_SG_BDID_EN;
        pcAndOffsetsReg |= ((p_ManipParams->sgBpid
                << FM_PCD_MANIP_IP_FRAG_SG_BDID_SHIFT)
                & FM_PCD_MANIP_IP_FRAG_SG_BDID_MASK);
    }

    /* Prepare the first Ad register (gmask) - scratch buffer pool id and Pointer to fragment ID */
    gmaskReg = (uint32_t)(XX_VirtToPhys(UINT_TO_PTR(p_FmPcd->ipv6FrameIdAddr))
            - p_FmPcd->physicalMuramBase);
#if (DPAA_VERSION == 10)
    gmaskReg |= p_ManipParams->scratchBpid << FM_PCD_MANIP_IP_FRAG_SCRATCH_BPID;
#else
    gmaskReg |= (0xFF) << FM_PCD_MANIP_IP_FRAG_SCRATCH_BPID;
#endif /* (DPAA_VERSION == 10) */

    /* Set all Ad registers */
    WRITE_UINT32(p_Manip->fragParams.p_Frag->pcAndOffsets, pcAndOffsetsReg);
    WRITE_UINT32(p_Manip->fragParams.p_Frag->ccAdBase, ccAdBaseReg);
    WRITE_UINT32(p_Manip->fragParams.p_Frag->gmask, gmaskReg);

    /* Saves user's fragmentation manipulation parameters */
    p_Manip->frag = TRUE;
    p_Manip->sizeForFragmentation = p_ManipParams->sizeForFragmentation;

#if (DPAA_VERSION == 10)
    p_Manip->fragParams.scratchBpid = p_ManipParams->scratchBpid;

    /* scratch buffer pool initialization */
    if ((err = FmPcdFragHcScratchPoolFill((t_Handle)p_FmPcd, p_ManipParams->scratchBpid)) != E_OK)
    {
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, p_Manip->fragParams.p_Frag);
        p_Manip->fragParams.p_Frag = NULL;
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
#endif /* (DPAA_VERSION == 10) */

    return E_OK;
}

static t_Error IPManip(t_FmPcdManip *p_Manip)
{
    t_Error err = E_OK;
    t_FmPcd *p_FmPcd;
    t_AdOfTypeContLookup *p_Ad;
    uint32_t tmpReg32 = 0, tmpRegNia = 0;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    p_FmPcd = p_Manip->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

    tmpReg32 = FM_PCD_MANIP_IP_NO_FRAGMENTATION;
    if (p_Manip->frag == TRUE)
    {
        tmpRegNia = (uint32_t)(XX_VirtToPhys(p_Manip->fragParams.p_Frag)
                - (p_FmPcd->physicalMuramBase));
        tmpReg32 = (uint32_t)p_Manip->sizeForFragmentation
                << FM_PCD_MANIP_IP_MTU_SHIFT;
    }

    tmpRegNia |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    tmpReg32 |= HMAN_OC_IP_MANIP;

#if (DPAA_VERSION >= 11)
    tmpRegNia |= FM_PCD_MANIP_IP_CNIA;
#endif /* (DPAA_VERSION >= 11) */

    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);
    WRITE_UINT32(p_Ad->ccAdBase, tmpRegNia);
    WRITE_UINT32(p_Ad->gmask, 0);
    /* Total frame counter - MUST be initialized to zero.*/

    return err;
}

static t_Error UpdateInitIpFrag(t_Handle h_FmPcd, t_Handle h_PcdParams,
                                t_Handle h_FmPort, t_FmPcdManip *p_Manip,
                                t_Handle h_Ad, bool validate)
{
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Manip->opcode == HMAN_OC_IP_FRAGMENTATION),
                              E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);

    UNUSED(h_FmPcd);
    UNUSED(h_Ad);
    UNUSED(h_PcdParams);
    UNUSED(validate);
    UNUSED(p_Manip);

    fmPortGetSetCcParams.setCcParams.type = 0;
    fmPortGetSetCcParams.getCcParams.type = MANIP_EXTRA_SPACE;
    if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    if (!fmPortGetSetCcParams.getCcParams.internalBufferOffset)
        DBG(WARNING, ("manipExtraSpace must be larger than '0'"));

    return E_OK;
}

static t_Error IPSecManip(t_FmPcdManipParams *p_ManipParams,
                          t_FmPcdManip *p_Manip)
{
    t_AdOfTypeContLookup *p_Ad;
    t_FmPcdManipSpecialOffloadIPSecParams *p_IPSecParams;
    t_Error err = E_OK;
    uint32_t tmpReg32 = 0;
    uint32_t power;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ManipParams, E_INVALID_HANDLE);

    p_IPSecParams = &p_ManipParams->u.specialOffload.u.ipsec;

    SANITY_CHECK_RETURN_ERROR(
            !p_IPSecParams->variableIpHdrLen || p_IPSecParams->decryption,
            E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(
            !p_IPSecParams->variableIpVersion || !p_IPSecParams->decryption,
            E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(
            !p_IPSecParams->variableIpVersion || p_IPSecParams->outerIPHdrLen,
            E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(
            !p_IPSecParams->arwSize || p_IPSecParams->arwAddr,
            E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(
            !p_IPSecParams->arwSize || p_IPSecParams->decryption,
            E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR((p_IPSecParams->arwSize % 16) == 0, E_INVALID_VALUE);

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    tmpReg32 |= (p_IPSecParams->decryption) ? FM_PCD_MANIP_IPSEC_DEC : 0;
    tmpReg32 |= (p_IPSecParams->ecnCopy) ? FM_PCD_MANIP_IPSEC_ECN_EN : 0;
    tmpReg32 |= (p_IPSecParams->dscpCopy) ? FM_PCD_MANIP_IPSEC_DSCP_EN : 0;
    tmpReg32 |=
            (p_IPSecParams->variableIpHdrLen) ? FM_PCD_MANIP_IPSEC_VIPL_EN : 0;
    tmpReg32 |=
            (p_IPSecParams->variableIpVersion) ? FM_PCD_MANIP_IPSEC_VIPV_EN : 0;
    if (p_IPSecParams->arwSize)
        tmpReg32 |= (uint32_t)((XX_VirtToPhys(UINT_TO_PTR(p_IPSecParams->arwAddr))-FM_MM_MURAM)
                & (FM_MURAM_SIZE-1));
    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    tmpReg32 = 0;
    if (p_IPSecParams->arwSize) {
        NEXT_POWER_OF_2((p_IPSecParams->arwSize + 32), power);
        LOG2(power, power);
        tmpReg32 = (p_IPSecParams->arwSize | (power - 5)) << FM_PCD_MANIP_IPSEC_ARW_SIZE_SHIFT;
    }

    if (p_ManipParams->h_NextManip)
        tmpReg32 |=
                (uint32_t)(XX_VirtToPhys(((t_FmPcdManip *)p_ManipParams->h_NextManip)->h_Ad)-
                        (((t_FmPcd *)p_Manip->h_FmPcd)->physicalMuramBase)) >> 4;
    WRITE_UINT32(p_Ad->matchTblPtr, tmpReg32);

    tmpReg32 = HMAN_OC_IPSEC_MANIP;
    tmpReg32 |= p_IPSecParams->outerIPHdrLen
            << FM_PCD_MANIP_IPSEC_IP_HDR_LEN_SHIFT;
    if (p_ManipParams->h_NextManip)
        tmpReg32 |= FM_PCD_MANIP_IPSEC_NADEN;
    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    return err;
}

static t_Error SetCapwapReassmManip(t_FmPcdManip *p_Manip)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_Manip->h_FmPcd;

    /* Allocation if CAPWAP Action descriptor */
    p_Manip->reassmParams.capwap.h_Ad = (t_Handle)XX_MallocSmart(
            FM_PCD_CC_AD_ENTRY_SIZE, p_Manip->reassmParams.dataMemId,
            FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_Manip->reassmParams.capwap.h_Ad)
    {
        ReleaseManipHandler(p_Manip, p_FmPcd);
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("Allocation of CAPWAP table descriptor"));
    }

    memset(p_Manip->reassmParams.capwap.h_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* Fill reassembly manipulation parameter in the Reassembly Action Descriptor */
    return FillReassmManipParams(p_Manip, HEADER_TYPE_CAPWAP);
}

static void setCapwapReassmSchemeParams(t_FmPcd* p_FmPcd,
                                        t_FmPcdKgSchemeParams *p_Scheme,
                                        t_Handle h_CcTree, uint8_t groupId)
{
    uint8_t res;

    /* Configures scheme's network environment parameters */
    p_Scheme->netEnvParams.numOfDistinctionUnits = 1;
    res = FmPcdNetEnvGetUnitId(
            p_FmPcd, FmPcdGetNetEnvId(p_Scheme->netEnvParams.h_NetEnv),
            HEADER_TYPE_USER_DEFINED_SHIM2, FALSE, 0);
    ASSERT_COND(res != FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS);
    p_Scheme->netEnvParams.unitIds[0] = res;

    /* Configures scheme's next engine parameters*/
    p_Scheme->nextEngine = e_FM_PCD_CC;
    p_Scheme->kgNextEngineParams.cc.h_CcTree = h_CcTree;
    p_Scheme->kgNextEngineParams.cc.grpId = groupId;
    p_Scheme->useHash = TRUE;

    /* Configures scheme's key*/
    p_Scheme->keyExtractAndHashParams.numOfUsedExtracts = 2;
    p_Scheme->keyExtractAndHashParams.extractArray[0].type =
            e_FM_PCD_EXTRACT_NON_HDR;
    p_Scheme->keyExtractAndHashParams.extractArray[0].extractNonHdr.src =
            e_FM_PCD_EXTRACT_FROM_PARSE_RESULT;
    p_Scheme->keyExtractAndHashParams.extractArray[0].extractNonHdr.action =
            e_FM_PCD_ACTION_NONE;
    p_Scheme->keyExtractAndHashParams.extractArray[0].extractNonHdr.offset = 20;
    p_Scheme->keyExtractAndHashParams.extractArray[0].extractNonHdr.size = 4;
    p_Scheme->keyExtractAndHashParams.extractArray[1].type =
            e_FM_PCD_EXTRACT_NON_HDR;
    p_Scheme->keyExtractAndHashParams.extractArray[1].extractNonHdr.src =
            e_FM_PCD_EXTRACT_FROM_DFLT_VALUE;
    p_Scheme->keyExtractAndHashParams.extractArray[1].extractNonHdr.action =
            e_FM_PCD_ACTION_NONE;
    p_Scheme->keyExtractAndHashParams.extractArray[1].extractNonHdr.offset = 0;
    p_Scheme->keyExtractAndHashParams.extractArray[1].extractNonHdr.size = 1;

    p_Scheme->keyExtractAndHashParams.privateDflt0 = 0x0;
    p_Scheme->keyExtractAndHashParams.privateDflt1 = 0x0;
    p_Scheme->keyExtractAndHashParams.numOfUsedDflts = 1;
    p_Scheme->keyExtractAndHashParams.dflts[0].type = e_FM_PCD_KG_GENERIC_NOT_FROM_DATA;
    p_Scheme->keyExtractAndHashParams.dflts[0].dfltSelect = e_FM_PCD_KG_DFLT_PRIVATE_0;
}

#if (DPAA_VERSION >= 11)
static t_Error CapwapReassemblyStats(t_FmPcdManip *p_Manip,
                                     t_FmPcdManipReassemCapwapStats *p_Stats)
{
    ASSERT_COND(p_Manip);
    ASSERT_COND(p_Stats);
    ASSERT_COND(p_Manip->reassmParams.p_ReassCommonTbl);

    p_Stats->timeout =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalTimeOutCounter);
    p_Stats->rfdPoolBusy =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalRfdPoolBusyCounter);
    p_Stats->internalBufferBusy =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalInternalBufferBusy);
    p_Stats->externalBufferBusy =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalExternalBufferBusy);
    p_Stats->sgFragments =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalSgFragmentCounter);
    p_Stats->dmaSemaphoreDepletion =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalDmaSemaphoreDepletionCounter);
    p_Stats->exceedMaxReassemblyFrameLen =
            GET_UINT32(p_Manip->reassmParams.p_ReassCommonTbl->totalNCSPCounter);

    p_Stats->successfullyReassembled =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalSuccessfullyReasmFramesCounter);
    p_Stats->validFragments =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalValidFragmentCounter);
    p_Stats->processedFragments =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalProcessedFragCounter);
    p_Stats->malformedFragments =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalMalformdFragCounter);
    p_Stats->autoLearnBusy =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalSetBusyCounter);
    p_Stats->discardedFragments =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalDiscardedFragsCounter);
    p_Stats->moreThan16Fragments =
            GET_UINT32(p_Manip->reassmParams.capwap.p_ReassTbl->totalMoreThan16FramesCounter);

    return E_OK;
}

static t_Error CapwapFragmentationStats(t_FmPcdManip *p_Manip,
		t_FmPcdManipFragCapwapStats *p_Stats)
{
	t_AdOfTypeContLookup *p_Ad;

	ASSERT_COND(p_Manip);
	ASSERT_COND(p_Stats);
	ASSERT_COND(p_Manip->h_Ad);
	ASSERT_COND(p_Manip->fragParams.p_Frag);

	p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

	p_Stats->totalFrames = GET_UINT32(p_Ad->gmask);

	return E_OK;
}

static t_Error CapwapReassembly(t_FmPcdManipReassemParams *p_ManipReassmParams,
                                t_FmPcdManip *p_Manip)
{
    uint32_t maxSetNumber = 10000;
    t_FmPcdManipReassemCapwapParams reassmManipParams =
            p_ManipReassmParams->u.capwapReassem;
    t_Error res;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((t_FmPcd *)p_Manip->h_FmPcd)->h_Hc,
                              E_INVALID_HANDLE);

    /* Check validation of user's parameter.*/
    if ((reassmManipParams.timeoutThresholdForReassmProcess < 1000)
            || (reassmManipParams.timeoutThresholdForReassmProcess > 8000000))
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("timeoutThresholdForReassmProcess should be 1msec - 8sec"));
    /* It is recommended that the total number of entries in this table (number of sets * number of ways)
     will be twice the number of frames that are expected to be reassembled simultaneously.*/
    if (reassmManipParams.maxNumFramesInProcess
            > (reassmManipParams.maxNumFramesInProcess * maxSetNumber / 2))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("maxNumFramesInProcess has to be less than (maximun set number * number of ways / 2)"));

    /* Saves user's reassembly manipulation parameters */
    p_Manip->reassmParams.capwap.relativeSchemeId =
            reassmManipParams.relativeSchemeId;
    p_Manip->reassmParams.capwap.numOfFramesPerHashEntry =
            reassmManipParams.numOfFramesPerHashEntry;
    p_Manip->reassmParams.capwap.maxRessembledsSize =
            reassmManipParams.maxReassembledFrameLength;
    p_Manip->reassmParams.maxNumFramesInProcess =
            reassmManipParams.maxNumFramesInProcess;
    p_Manip->reassmParams.timeOutMode = reassmManipParams.timeOutMode;
    p_Manip->reassmParams.fqidForTimeOutFrames =
            reassmManipParams.fqidForTimeOutFrames;
    p_Manip->reassmParams.timeoutThresholdForReassmProcess =
            reassmManipParams.timeoutThresholdForReassmProcess;
    p_Manip->reassmParams.dataMemId = reassmManipParams.dataMemId;
    p_Manip->reassmParams.dataLiodnOffset = reassmManipParams.dataLiodnOffset;

    /* Creates and initializes the Reassembly common parameter table */
    CreateReassCommonTable(p_Manip);

    res = SetCapwapReassmManip(p_Manip);
    if (res != E_OK)
        return res;

    return E_OK;
}

static t_Error CapwapFragmentation(t_FmPcdManipFragCapwapParams *p_ManipParams,
                                   t_FmPcdManip *p_Manip)
{
    t_FmPcd *p_FmPcd;
    t_AdOfTypeContLookup *p_Ad;
    uint32_t pcAndOffsetsReg = 0, ccAdBaseReg = 0, gmaskReg = 0;
    uint32_t tmpReg32 = 0, tmpRegNia = 0;

    SANITY_CHECK_RETURN_ERROR(p_Manip->h_Ad, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ManipParams->sizeForFragmentation != 0xFFFF,
                              E_INVALID_VALUE);
    p_FmPcd = p_Manip->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    /* Allocation of fragmentation Action Descriptor */
    p_Manip->fragParams.p_Frag = (t_AdOfTypeContLookup *)FM_MURAM_AllocMem(
            p_FmPcd->h_FmMuram, FM_PCD_CC_AD_ENTRY_SIZE,
            FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_Manip->fragParams.p_Frag)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM alloc for Fragmentation table descriptor"));
    MemSet8(p_Manip->fragParams.p_Frag, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* Prepare the third Ad register (pcAndOffsets)- OperationCode */
    pcAndOffsetsReg = (uint32_t)HMAN_OC_CAPWAP_FRAGMENTATION;

    /* Prepare the first Ad register (ccAdBase) - Don't frag action and Action descriptor type*/
    ccAdBaseReg = FM_PCD_AD_CONT_LOOKUP_TYPE;
    ccAdBaseReg |=
            (p_ManipParams->compressModeEn) ? FM_PCD_MANIP_CAPWAP_FRAG_COMPRESS_EN :
                    0;

    /* Set Scatter/Gather BPid */
    if (p_ManipParams->sgBpidEn)
    {
        ccAdBaseReg |= FM_PCD_MANIP_CAPWAP_FRAG_SG_BDID_EN;
        pcAndOffsetsReg |= ((p_ManipParams->sgBpid
                << FM_PCD_MANIP_CAPWAP_FRAG_SG_BDID_SHIFT)
                & FM_PCD_MANIP_CAPWAP_FRAG_SG_BDID_MASK);
    }

    /* Prepare the first Ad register (gmask) - scratch buffer pool id and Pointer to fragment ID */
    gmaskReg = (uint32_t)(XX_VirtToPhys(UINT_TO_PTR(p_FmPcd->capwapFrameIdAddr))
            - p_FmPcd->physicalMuramBase);
    gmaskReg |= (0xFF) << FM_PCD_MANIP_IP_FRAG_SCRATCH_BPID;

    /* Set all Ad registers */
    WRITE_UINT32(p_Manip->fragParams.p_Frag->pcAndOffsets, pcAndOffsetsReg);
    WRITE_UINT32(p_Manip->fragParams.p_Frag->ccAdBase, ccAdBaseReg);
    WRITE_UINT32(p_Manip->fragParams.p_Frag->gmask, gmaskReg);

    /* Saves user's fragmentation manipulation parameters */
    p_Manip->frag = TRUE;
    p_Manip->sizeForFragmentation = p_ManipParams->sizeForFragmentation;

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;

    tmpRegNia = (uint32_t)(XX_VirtToPhys(p_Manip->fragParams.p_Frag)
            - (p_FmPcd->physicalMuramBase));
    tmpReg32 = (uint32_t)p_Manip->sizeForFragmentation
            << FM_PCD_MANIP_CAPWAP_FRAG_CHECK_MTU_SHIFT;

    tmpRegNia |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    tmpReg32 |= HMAN_OC_CAPWAP_FRAG_CHECK;

    tmpRegNia |= FM_PCD_MANIP_CAPWAP_FRAG_CHECK_CNIA;

    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);
    WRITE_UINT32(p_Ad->ccAdBase, tmpRegNia);
    WRITE_UINT32(p_Ad->gmask, 0);
    /* Total frame counter - MUST be initialized to zero.*/

    return E_OK;
}

static t_Error UpdateInitCapwapFrag(t_Handle h_FmPcd, t_Handle h_PcdParams,
                                    t_Handle h_FmPort, t_FmPcdManip *p_Manip,
                                    t_Handle h_Ad, bool validate)
{
    t_FmPortGetSetCcParams fmPortGetSetCcParams;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Manip->opcode == HMAN_OC_CAPWAP_FRAGMENTATION),
                              E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);

    UNUSED(h_FmPcd);
    UNUSED(h_Ad);
    UNUSED(h_PcdParams);
    UNUSED(validate);
    UNUSED(p_Manip);

    fmPortGetSetCcParams.setCcParams.type = 0;
    fmPortGetSetCcParams.getCcParams.type = MANIP_EXTRA_SPACE;
    if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    if (!fmPortGetSetCcParams.getCcParams.internalBufferOffset)
        DBG(WARNING, ("manipExtraSpace must be larger than '0'"));

    return E_OK;
}

static t_Error CapwapManip(t_FmPcdManipParams *p_ManipParams,
                           t_FmPcdManip *p_Manip)
{
    t_AdOfTypeContLookup *p_Ad;
    t_FmPcdManipSpecialOffloadCapwapParams *p_Params;
    t_Error err = E_OK;
    uint32_t tmpReg32 = 0;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ManipParams, E_INVALID_HANDLE);

    p_Params = &p_ManipParams->u.specialOffload.u.capwap;

    p_Ad = (t_AdOfTypeContLookup *)p_Manip->h_Ad;
    tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
    tmpReg32 |= (p_Params->dtls) ? FM_PCD_MANIP_CAPWAP_DTLS : 0;
    /* TODO - add 'qosSrc' */
    WRITE_UINT32(p_Ad->ccAdBase, tmpReg32);

    tmpReg32 = HMAN_OC_CAPWAP_MANIP;
    if (p_ManipParams->h_NextManip)
    {
        WRITE_UINT32(
                p_Ad->matchTblPtr,
                (uint32_t)(XX_VirtToPhys(((t_FmPcdManip *)p_ManipParams->h_NextManip)->h_Ad)- (((t_FmPcd *)p_Manip->h_FmPcd)->physicalMuramBase)) >> 4);

        tmpReg32 |= FM_PCD_MANIP_CAPWAP_NADEN;
    }

    WRITE_UINT32(p_Ad->pcAndOffsets, tmpReg32);

    return err;
}
#endif /* (DPAA_VERSION >= 11) */

static t_Handle ManipOrStatsSetNode(t_Handle h_FmPcd, t_Handle *p_Params,
                                    bool stats)
{
    t_FmPcdManip *p_Manip;
    t_Error err;
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;

    p_Manip = (t_FmPcdManip*)XX_Malloc(sizeof(t_FmPcdManip));
    if (!p_Manip)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("No memory"));
        return NULL;
    }
    memset(p_Manip, 0, sizeof(t_FmPcdManip));

    p_Manip->type = ((t_FmPcdManipParams *)p_Params)->type;
    memcpy((uint8_t*)&p_Manip->manipParams, p_Params,
           sizeof(p_Manip->manipParams));

    if (!stats)
        err = CheckManipParamsAndSetType(p_Manip,
                                         (t_FmPcdManipParams *)p_Params);
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
    else
        err = CheckStatsParamsAndSetType(p_Manip, (t_FmPcdStatsParams *)p_Params);
#else /* not (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
    else
    {
        REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Statistics node!"));
        XX_Free(p_Manip);
        return NULL;
    }
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
    if (err)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Invalid header manipulation type"));
        XX_Free(p_Manip);
        return NULL;
    }

    if ((p_Manip->opcode != HMAN_OC_IP_REASSEMBLY) && (p_Manip->opcode != HMAN_OC_CAPWAP_REASSEMBLY))
    {
        /* In Case of reassembly manipulation the reassembly action descriptor will
         be defines later on */
        if (p_Manip->muramAllocate)
        {
            p_Manip->h_Ad = (t_Handle)FM_MURAM_AllocMem(
                    p_FmPcd->h_FmMuram, FM_PCD_CC_AD_ENTRY_SIZE,
                    FM_PCD_CC_AD_TABLE_ALIGN);
            if (!p_Manip->h_Ad)
            {
                REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for Manipulation action descriptor"));
                ReleaseManipHandler(p_Manip, p_FmPcd);
                XX_Free(p_Manip);
                return NULL;
            }

            MemSet8(p_Manip->h_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);
        }
        else
        {
            p_Manip->h_Ad = (t_Handle)XX_Malloc(
                    FM_PCD_CC_AD_ENTRY_SIZE * sizeof(uint8_t));
            if (!p_Manip->h_Ad)
            {
                REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Allocation of Manipulation action descriptor"));
                ReleaseManipHandler(p_Manip, p_FmPcd);
                XX_Free(p_Manip);
                return NULL;
            }

            memset(p_Manip->h_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE * sizeof(uint8_t));
        }
    }

    p_Manip->h_FmPcd = h_FmPcd;

    return p_Manip;
}

static void UpdateAdPtrOfNodesWhichPointsOnCrntMdfManip(
        t_FmPcdManip *p_CrntMdfManip, t_List *h_NodesLst)
{
    t_CcNodeInformation *p_CcNodeInformation;
    t_FmPcdCcNode *p_NodePtrOnCurrentMdfManip = NULL;
    t_List *p_Pos;
    int i = 0;
    t_Handle p_AdTablePtOnCrntCurrentMdfNode/*, p_AdTableNewModified*/;
    t_CcNodeInformation ccNodeInfo;

    NCSW_LIST_FOR_EACH(p_Pos, &p_CrntMdfManip->nodesLst)
    {
        p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);
        p_NodePtrOnCurrentMdfManip =
                (t_FmPcdCcNode *)p_CcNodeInformation->h_CcNode;

        ASSERT_COND(p_NodePtrOnCurrentMdfManip);

        /* Search in the previous node which exact index points on this current modified node for getting AD */
        for (i = 0; i < p_NodePtrOnCurrentMdfManip->numOfKeys + 1; i++)
        {
            if (p_NodePtrOnCurrentMdfManip->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                    == e_FM_PCD_CC)
            {
                if (p_NodePtrOnCurrentMdfManip->keyAndNextEngineParams[i].nextEngineParams.h_Manip
                        == (t_Handle)p_CrntMdfManip)
                {
                    if (p_NodePtrOnCurrentMdfManip->keyAndNextEngineParams[i].p_StatsObj)
                        p_AdTablePtOnCrntCurrentMdfNode =
                                p_NodePtrOnCurrentMdfManip->keyAndNextEngineParams[i].p_StatsObj->h_StatsAd;
                    else
                        p_AdTablePtOnCrntCurrentMdfNode =
                                PTR_MOVE(p_NodePtrOnCurrentMdfManip->h_AdTable, i*FM_PCD_CC_AD_ENTRY_SIZE);

                    memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                    ccNodeInfo.h_CcNode = p_AdTablePtOnCrntCurrentMdfNode;
                    EnqueueNodeInfoToRelevantLst(h_NodesLst, &ccNodeInfo, NULL);
                }
            }
        }

        ASSERT_COND(i != p_NodePtrOnCurrentMdfManip->numOfKeys);
    }
}

static void BuildHmtd(uint8_t *p_Dest, uint8_t *p_Src, uint8_t *p_Hmcd,
                      t_FmPcd *p_FmPcd)
{
    t_Error err;

    /* Copy the HMTD */
    MemCpy8(p_Dest, (uint8_t*)p_Src, 16);
    /* Replace the HMCT table pointer  */
    WRITE_UINT32(
            ((t_Hmtd *)p_Dest)->hmcdBasePtr,
            (uint32_t)(XX_VirtToPhys(p_Hmcd) - ((t_FmPcd*)p_FmPcd)->physicalMuramBase));
    /* Call Host Command to replace HMTD by a new HMTD */
    err = FmHcPcdCcDoDynamicChange(
            p_FmPcd->h_Hc,
            (uint32_t)(XX_VirtToPhys(p_Src) - p_FmPcd->physicalMuramBase),
            (uint32_t)(XX_VirtToPhys(p_Dest) - p_FmPcd->physicalMuramBase));
    if (err)
        REPORT_ERROR(MINOR, err, ("Failed in dynamic manip change, continued to the rest of the owners."));
}

static t_Error FmPcdManipInitUpdate(t_Handle h_FmPcd, t_Handle h_PcdParams,
                                    t_Handle h_FmPort, t_Handle h_Manip,
                                    t_Handle h_Ad, bool validate, int level,
                                    t_Handle h_FmTree)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_Manip, E_INVALID_HANDLE);

    UNUSED(level);
    UNUSED(h_FmTree);

    switch (p_Manip->opcode)
    {
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
        case (HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX):
        err = UpdateInitMvIntFrameHeaderFromFrameToBufferPrefix(h_FmPort,
                p_Manip,
                h_Ad,
                validate);
        break;
        case (HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER):
        if (!p_Manip->h_Frag)
        break;
        case (HMAN_OC_CAPWAP_FRAGMENTATION):
        err = UpdateInitCapwapFragmentation(h_FmPort, p_Manip, h_Ad, validate, h_FmTree);
        break;
        case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):
        if (p_Manip->h_Frag)
        err = UpdateInitCapwapReasm(h_FmPcd, h_FmPort, p_Manip, h_Ad, validate);
        break;
        case (HMAN_OC_CAPWAP_INDEXED_STATS):
        err = UpdateIndxStats(h_FmPcd, h_FmPort, p_Manip);
        break;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
        case (HMAN_OC_IP_REASSEMBLY):
            err = UpdateInitReasm(h_FmPcd, h_PcdParams, h_FmPort, p_Manip, h_Ad,
                                  validate);
            break;
        case (HMAN_OC_IP_FRAGMENTATION):
            err = UpdateInitIpFrag(h_FmPcd, h_PcdParams, h_FmPort, p_Manip,
                                   h_Ad, validate);
            break;
#if (DPAA_VERSION >= 11)
        case (HMAN_OC_CAPWAP_FRAGMENTATION):
            err = UpdateInitCapwapFrag(h_FmPcd, h_PcdParams, h_FmPort, p_Manip,
                                       h_Ad, validate);
            break;
        case (HMAN_OC_CAPWAP_REASSEMBLY):
            err = UpdateInitReasm(h_FmPcd, h_PcdParams, h_FmPort, p_Manip, h_Ad,
                                  validate);
            break;
#endif /* (DPAA_VERSION >= 11) */
        default:
            return E_OK;
    }

    return err;
}

static t_Error FmPcdManipModifyUpdate(t_Handle h_Manip, t_Handle h_Ad,
                                      bool validate, int level,
                                      t_Handle h_FmTree)
{

    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    t_Error err = E_OK;

    UNUSED(level);

    switch (p_Manip->opcode)
    {
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
        case (HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX):
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("modify node with this type of manipulation  is not suppported"));
        case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):

        if (p_Manip->h_Frag)
        {
            if (!(p_Manip->shadowUpdateParams & NUM_OF_TASKS)
                    && !(p_Manip->shadowUpdateParams & OFFSET_OF_DATA)
                    && !(p_Manip->shadowUpdateParams & OFFSET_OF_PR))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_STATE,
                    ("modify node with this type of manipulation requires manipulation be updated previously in SetPcd function"));
        }
        break;
        case (HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER):
        if (p_Manip->h_Frag)
        err = UpdateModifyCapwapFragmenation(p_Manip, h_Ad, validate, h_FmTree);
        break;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
        default:
            return E_OK;
    }

    return err;
}

/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/

t_Error FmPcdManipUpdate(t_Handle h_FmPcd, t_Handle h_PcdParams,
                         t_Handle h_FmPort, t_Handle h_Manip, t_Handle h_Ad,
                         bool validate, int level, t_Handle h_FmTree,
                         bool modify)
{
    t_Error err;

    if (!modify)
        err = FmPcdManipInitUpdate(h_FmPcd, h_PcdParams, h_FmPort, h_Manip,
                                   h_Ad, validate, level, h_FmTree);
    else
        err = FmPcdManipModifyUpdate(h_Manip, h_Ad, validate, level, h_FmTree);

    return err;
}

void FmPcdManipUpdateOwner(t_Handle h_Manip, bool add)
{

    uint32_t intFlags;

    intFlags = XX_LockIntrSpinlock(((t_FmPcdManip *)h_Manip)->h_Spinlock);
    if (add)
        ((t_FmPcdManip *)h_Manip)->owner++;
    else
    {
        ASSERT_COND(((t_FmPcdManip *)h_Manip)->owner);
        ((t_FmPcdManip *)h_Manip)->owner--;
    }
    XX_UnlockIntrSpinlock(((t_FmPcdManip *)h_Manip)->h_Spinlock, intFlags);
}

t_List *FmPcdManipGetNodeLstPointedOnThisManip(t_Handle h_Manip)
{
    ASSERT_COND(h_Manip);
    return &((t_FmPcdManip *)h_Manip)->nodesLst;
}

t_List *FmPcdManipGetSpinlock(t_Handle h_Manip)
{
    ASSERT_COND(h_Manip);
    return ((t_FmPcdManip *)h_Manip)->h_Spinlock;
}

t_Error FmPcdManipCheckParamsForCcNextEngine(
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams,
        uint32_t *requiredAction)
{
    t_FmPcdManip *p_Manip;
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
    t_Error err = E_OK;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))*/
    bool pointFromCc = TRUE;

    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams->h_Manip,
                              E_NULL_POINTER);

    p_Manip = (t_FmPcdManip *)(p_FmPcdCcNextEngineParams->h_Manip);
    *requiredAction = 0;

    while (p_Manip)
    {
        switch (p_Manip->opcode)
        {
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
            case (HMAN_OC_CAPWAP_INDEXED_STATS):
                if (p_FmPcdCcNextEngineParams->nextEngine != e_FM_PCD_DONE)
                    RETURN_ERROR(MAJOR,	E_INVALID_STATE, ("For this type of header manipulation has to be nextEngine e_FM_PCD_DONE"));
                if (p_FmPcdCcNextEngineParams->params.enqueueParams.overrideFqid)
                    p_Manip->cnia = TRUE;
            case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):
                *requiredAction = UPDATE_NIA_ENQ_WITHOUT_DMA;
            case (HMAN_OC_RMV_N_OR_INSRT_INT_FRM_HDR):
                p_Manip->ownerTmp++;
                break;
            case (HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER):
                if ((p_FmPcdCcNextEngineParams->nextEngine != e_FM_PCD_DONE)
                    && !p_FmPcdCcNextEngineParams->params.enqueueParams.overrideFqid)
                    RETURN_ERROR(
                        MAJOR,
                        E_INVALID_STATE,
                        ("For this type of header manipulation has to be nextEngine e_FM_PCD_DONE with fqidForCtrlFlow FALSE"));
                p_Manip->ownerTmp++;
                break;
            case (HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX):
                if ((p_FmPcdCcNextEngineParams->nextEngine != e_FM_PCD_CC)
                    && (FmPcdCcGetParseCode(p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode)
                        != CC_PC_GENERIC_IC_HASH_INDEXED))
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("For this type of header manipulation next engine has to be CC and action = e_FM_PCD_ACTION_INDEXED_LOOKUP"));
                err = UpdateManipIc(p_FmPcdCcNextEngineParams->h_Manip,
                                    FmPcdCcGetOffset(p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode));
                if (err)
                    RETURN_ERROR(MAJOR, err, NO_MSG);
                *requiredAction = UPDATE_NIA_ENQ_WITHOUT_DMA;
                break;
 #endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
            case (HMAN_OC_IP_FRAGMENTATION):
            case (HMAN_OC_IP_REASSEMBLY):
#if (DPAA_VERSION >= 11)
            case (HMAN_OC_CAPWAP_REASSEMBLY):
            case (HMAN_OC_CAPWAP_FRAGMENTATION):
#endif /* (DPAA_VERSION >= 11) */
                if (p_FmPcdCcNextEngineParams->nextEngine != e_FM_PCD_DONE)
                    RETURN_ERROR(
                            MAJOR,
                            E_INVALID_STATE,
                            ("For this type of header manipulation has to be nextEngine e_FM_PCD_DONE"));
                p_Manip->ownerTmp++;
                break;
            case (HMAN_OC_IPSEC_MANIP):
#if (DPAA_VERSION >= 11)
            case (HMAN_OC_CAPWAP_MANIP):
#endif /* (DPAA_VERSION >= 11) */
                p_Manip->ownerTmp++;
                break;
            case (HMAN_OC):
                if ((p_FmPcdCcNextEngineParams->nextEngine == e_FM_PCD_CC)
                        && MANIP_IS_CASCADED(p_Manip))
                    RETURN_ERROR(
                            MINOR,
                            E_INVALID_STATE,
                            ("Can't have a cascaded manipulation when and Next Engine is CC"));
                if (!MANIP_IS_FIRST(p_Manip) && pointFromCc)
                    RETURN_ERROR(
                            MAJOR,
                            E_INVALID_STATE,
                            ("h_Manip is already used and may not be shared (no sharing of non-head manip nodes)"));
                break;
            default:
                RETURN_ERROR(
                        MAJOR, E_INVALID_STATE,
                        ("invalid type of header manipulation for this state"));
        }
        p_Manip = p_Manip->h_NextManip;
        pointFromCc = FALSE;
    }
    return E_OK;
}


t_Error FmPcdManipCheckParamsWithCcNodeParams(t_Handle h_Manip,
                                              t_Handle h_FmPcdCcNode)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcdCcNode, E_INVALID_HANDLE);

    switch (p_Manip->opcode)
    {
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
        case (HMAN_OC_CAPWAP_INDEXED_STATS):
        if (p_Manip->ownerTmp != FmPcdCcGetNumOfKeys(h_FmPcdCcNode))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("The manipulation of the type statistics flowId if exist has to be pointed by all numOfKeys"));
        break;
        case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):
        if (p_Manip->h_Frag)
        {
            if (p_Manip->ownerTmp != FmPcdCcGetNumOfKeys(h_FmPcdCcNode))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("The manipulation of the type remove DTLS if exist has to be pointed by all numOfKeys"));
            err = UpdateManipIc(h_Manip, FmPcdCcGetOffset(h_FmPcdCcNode));
            if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
        break;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
        default:
            break;
    }

    return err;
}

void FmPcdManipUpdateAdResultForCc(
        t_Handle h_Manip, t_FmPcdCcNextEngineParams *p_CcNextEngineParams,
        t_Handle p_Ad, t_Handle *p_AdNewPtr)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;

    /* This routine creates a Manip AD and can return in "p_AdNewPtr"
     * either the new descriptor or NULL if it writes the Manip AD into p_AD (into the match table) */

    ASSERT_COND(p_Manip);
    ASSERT_COND(p_CcNextEngineParams);
    ASSERT_COND(p_Ad);
    ASSERT_COND(p_AdNewPtr);

    FmPcdManipUpdateOwner(h_Manip, TRUE);

    /* According to "type", either build & initialize a new AD (p_AdNew) or initialize
     * p_Ad ( the AD in the match table) and set p_AdNew = NULL. */
    switch (p_Manip->opcode)
    {
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
        case (HMAN_OC_RMV_N_OR_INSRT_INT_FRM_HDR):
        case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):
        case (HMAN_OC_CAPWAP_INDEXED_STATS):
        *p_AdNewPtr = p_Manip->h_Ad;
        break;
        case (HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER):
        case (HMAN_OC_CAPWAP_FRAGMENTATION):
        WRITE_UINT32(((t_AdOfTypeResult *)p_Ad)->fqid,
                ((t_AdOfTypeResult *)(p_Manip->h_Ad))->fqid);
        WRITE_UINT32(((t_AdOfTypeResult *)p_Ad)->plcrProfile,
                ((t_AdOfTypeResult *)(p_Manip->h_Ad))->plcrProfile);
        WRITE_UINT32(((t_AdOfTypeResult *)p_Ad)->nia,
                ((t_AdOfTypeResult *)(p_Manip->h_Ad))->nia);
        *p_AdNewPtr = NULL;
        break;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
        case (HMAN_OC_IPSEC_MANIP):
#if (DPAA_VERSION >= 11)
        case (HMAN_OC_CAPWAP_MANIP):
#endif /* (DPAA_VERSION >= 11) */
            *p_AdNewPtr = p_Manip->h_Ad;
            break;
        case (HMAN_OC_IP_FRAGMENTATION):
#if (DPAA_VERSION >= 11)
        case (HMAN_OC_CAPWAP_FRAGMENTATION):
#endif /* (DPAA_VERSION >= 11) */
            if ((p_CcNextEngineParams->nextEngine == e_FM_PCD_DONE)
                    && (!p_CcNextEngineParams->params.enqueueParams.overrideFqid))
            {
                memcpy((uint8_t *)p_Ad, (uint8_t *)p_Manip->h_Ad,
                       sizeof(t_AdOfTypeContLookup));
#if (DPAA_VERSION >= 11)
                WRITE_UINT32(
                        ((t_AdOfTypeContLookup *)p_Ad)->ccAdBase,
                        GET_UINT32(((t_AdOfTypeContLookup *)p_Ad)->ccAdBase) & ~FM_PCD_MANIP_IP_CNIA);
#endif /* (DPAA_VERSION >= 11) */
                *p_AdNewPtr = NULL;
            }
            else
                *p_AdNewPtr = p_Manip->h_Ad;
            break;
        case (HMAN_OC_IP_REASSEMBLY):
            if (FmPcdManipIpReassmIsIpv6Hdr(p_Manip))
            {
                if (!p_Manip->reassmParams.ip.ipv6Assigned)
                {
                    *p_AdNewPtr = p_Manip->reassmParams.ip.h_Ipv6Ad;
                    p_Manip->reassmParams.ip.ipv6Assigned = TRUE;
                    FmPcdManipUpdateOwner(h_Manip, FALSE);
                }
                else
                {
                    *p_AdNewPtr = p_Manip->reassmParams.ip.h_Ipv4Ad;
                    p_Manip->reassmParams.ip.ipv6Assigned = FALSE;
                }
            }
            else
                *p_AdNewPtr = p_Manip->reassmParams.ip.h_Ipv4Ad;
            memcpy((uint8_t *)p_Ad, (uint8_t *)*p_AdNewPtr,
                   sizeof(t_AdOfTypeContLookup));
            *p_AdNewPtr = NULL;
            break;
#if (DPAA_VERSION >= 11)
        case (HMAN_OC_CAPWAP_REASSEMBLY):
            *p_AdNewPtr = p_Manip->reassmParams.capwap.h_Ad;
            memcpy((uint8_t *)p_Ad, (uint8_t *)*p_AdNewPtr,
                   sizeof(t_AdOfTypeContLookup));
            *p_AdNewPtr = NULL;
            break;
#endif /* (DPAA_VERSION >= 11) */
        case (HMAN_OC):
            /* Allocate and initialize HMTD */
            *p_AdNewPtr = p_Manip->h_Ad;
            break;
        default:
            break;
    }
}

void FmPcdManipUpdateAdContLookupForCc(t_Handle h_Manip, t_Handle p_Ad,
                                       t_Handle *p_AdNewPtr,
                                       uint32_t adTableOffset)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;

    /* This routine creates a Manip AD and can return in "p_AdNewPtr"
     * either the new descriptor or NULL if it writes the Manip AD into p_AD (into the match table) */
    ASSERT_COND(p_Manip);

    FmPcdManipUpdateOwner(h_Manip, TRUE);

    switch (p_Manip->opcode)
    {
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
        case (HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX):
        WRITE_UINT32(((t_AdOfTypeContLookup *)p_Ad)->ccAdBase,
                ((t_AdOfTypeContLookup *)(p_Manip->h_Ad))->ccAdBase);
        WRITE_UINT32(
                ((t_AdOfTypeContLookup *)p_Ad)->matchTblPtr,
                ((t_AdOfTypeContLookup *)(p_Manip->h_Ad))->matchTblPtr);
        WRITE_UINT32(
                ((t_AdOfTypeContLookup *)p_Ad)->pcAndOffsets,
                ((t_AdOfTypeContLookup *)(p_Manip->h_Ad))->pcAndOffsets);
        WRITE_UINT32(((t_AdOfTypeContLookup *)p_Ad)->gmask,
                ((t_AdOfTypeContLookup *)(p_Manip->h_Ad))->gmask);
        WRITE_UINT32(
                ((t_AdOfTypeContLookup *)p_Ad)->ccAdBase,
                (GET_UINT32(((t_AdOfTypeContLookup *)p_Ad)->ccAdBase) | adTableOffset));
        *p_AdNewPtr = NULL;
        break;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
        case (HMAN_OC):
            /* Initialize HMTD within the match table*/
            MemSet8(p_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);
            /* copy the existing HMTD *//* ask Alla - memcpy??? */
            memcpy((uint8_t*)p_Ad, p_Manip->h_Ad, sizeof(t_Hmtd));
            /* update NADEN to be "1"*/
            WRITE_UINT16(
                    ((t_Hmtd *)p_Ad)->cfg,
                    (uint16_t)(GET_UINT16(((t_Hmtd *)p_Ad)->cfg) | HMTD_CFG_NEXT_AD_EN));
            /* update next action descriptor */
            WRITE_UINT16(((t_Hmtd *)p_Ad)->nextAdIdx,
                         (uint16_t)(adTableOffset >> 4));
            /* mark that Manip's HMTD is not used */
            *p_AdNewPtr = NULL;
            break;

        default:
            break;
    }
}

t_Error FmPcdManipBuildIpReassmScheme(t_FmPcd *p_FmPcd, t_Handle h_NetEnv,
                                      t_Handle h_CcTree, t_Handle h_Manip,
                                      bool isIpv4, uint8_t groupId)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    t_FmPcdKgSchemeParams *p_SchemeParams = NULL;
    t_Handle h_Scheme;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(h_NetEnv);
    ASSERT_COND(p_Manip);

    /* scheme was already build, no need to check for IPv6 */
    if (p_Manip->reassmParams.ip.h_Ipv4Scheme)
        return E_OK;

    if (isIpv4) {
        h_Scheme = FmPcdKgGetSchemeHandle(p_FmPcd, p_Manip->reassmParams.ip.relativeSchemeId[0]);
        if (h_Scheme) {
            /* scheme was found */
            p_Manip->reassmParams.ip.h_Ipv4Scheme = h_Scheme;
            return E_OK;
        }
    } else {
        h_Scheme = FmPcdKgGetSchemeHandle(p_FmPcd, p_Manip->reassmParams.ip.relativeSchemeId[1]);
        if (h_Scheme) {
            /* scheme was found */
            p_Manip->reassmParams.ip.h_Ipv6Scheme = h_Scheme;
            return E_OK;
        }
    }

     p_SchemeParams = XX_Malloc(sizeof(t_FmPcdKgSchemeParams));
    if (!p_SchemeParams)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("Memory allocation failed for scheme"));

    /* Configures the IPv4 or IPv6 scheme*/
    memset(p_SchemeParams, 0, sizeof(t_FmPcdKgSchemeParams));
    p_SchemeParams->netEnvParams.h_NetEnv = h_NetEnv;
    p_SchemeParams->id.relativeSchemeId = (uint8_t)(
            (isIpv4 == TRUE) ? p_Manip->reassmParams.ip.relativeSchemeId[0] :
                    p_Manip->reassmParams.ip.relativeSchemeId[1]);
    p_SchemeParams->schemeCounter.update = TRUE;
#if (DPAA_VERSION >= 11)
    p_SchemeParams->alwaysDirect = TRUE;
    p_SchemeParams->bypassFqidGeneration = TRUE;
#else
    p_SchemeParams->keyExtractAndHashParams.hashDistributionNumOfFqids = 1;
    p_SchemeParams->baseFqid = 0xFFFFFF; /*TODO- baseFqid*/
#endif /* (DPAA_VERSION >= 11) */

    setIpReassmSchemeParams(p_FmPcd, p_SchemeParams, h_CcTree, isIpv4, groupId);

    /* Sets the new scheme */
    if (isIpv4)
        p_Manip->reassmParams.ip.h_Ipv4Scheme = FM_PCD_KgSchemeSet(
                p_FmPcd, p_SchemeParams);
    else
        p_Manip->reassmParams.ip.h_Ipv6Scheme = FM_PCD_KgSchemeSet(
                p_FmPcd, p_SchemeParams);

    XX_Free(p_SchemeParams);

    return E_OK;
}

t_Error FmPcdManipDeleteIpReassmSchemes(t_Handle h_Manip)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;

    ASSERT_COND(p_Manip);

    if ((p_Manip->reassmParams.ip.h_Ipv4Scheme) &&
        !FmPcdKgIsSchemeHasOwners(p_Manip->reassmParams.ip.h_Ipv4Scheme))
        FM_PCD_KgSchemeDelete(p_Manip->reassmParams.ip.h_Ipv4Scheme);

    if ((p_Manip->reassmParams.ip.h_Ipv6Scheme) &&
        !FmPcdKgIsSchemeHasOwners(p_Manip->reassmParams.ip.h_Ipv6Scheme))
        FM_PCD_KgSchemeDelete(p_Manip->reassmParams.ip.h_Ipv6Scheme);

    return E_OK;
}

bool FmPcdManipIpReassmIsIpv6Hdr(t_Handle h_Manip)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;

    ASSERT_COND(p_Manip);

    return (p_Manip->reassmParams.hdr == HEADER_TYPE_IPv6);
}

t_Error FmPcdManipBuildCapwapReassmScheme(t_FmPcd *p_FmPcd, t_Handle h_NetEnv,
                                          t_Handle h_CcTree, t_Handle h_Manip,
                                          uint8_t groupId)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    t_FmPcdKgSchemeParams *p_SchemeParams = NULL;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(h_NetEnv);
    ASSERT_COND(p_Manip);

    /* scheme was already build, no need to check for IPv6 */
    if (p_Manip->reassmParams.capwap.h_Scheme)
        return E_OK;

    p_SchemeParams = XX_Malloc(sizeof(t_FmPcdKgSchemeParams));
    if (!p_SchemeParams)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("Memory allocation failed for scheme"));

    memset(p_SchemeParams, 0, sizeof(t_FmPcdKgSchemeParams));
    p_SchemeParams->netEnvParams.h_NetEnv = h_NetEnv;
    p_SchemeParams->id.relativeSchemeId =
            (uint8_t)p_Manip->reassmParams.capwap.relativeSchemeId;
    p_SchemeParams->schemeCounter.update = TRUE;
    p_SchemeParams->bypassFqidGeneration = TRUE;

    setCapwapReassmSchemeParams(p_FmPcd, p_SchemeParams, h_CcTree, groupId);

    p_Manip->reassmParams.capwap.h_Scheme = FM_PCD_KgSchemeSet(p_FmPcd,
                                                               p_SchemeParams);

    XX_Free(p_SchemeParams);

    return E_OK;
}

t_Error FmPcdManipDeleteCapwapReassmSchemes(t_Handle h_Manip)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;

    ASSERT_COND(p_Manip);

    if (p_Manip->reassmParams.capwap.h_Scheme)
        FM_PCD_KgSchemeDelete(p_Manip->reassmParams.capwap.h_Scheme);

    return E_OK;
}

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
t_Handle FmPcdManipApplSpecificBuild(void)
{
    t_FmPcdManip *p_Manip;

    p_Manip = (t_FmPcdManip*)XX_Malloc(sizeof(t_FmPcdManip));
    if (!p_Manip)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("No memory"));
        return NULL;
    }
    memset(p_Manip, 0, sizeof(t_FmPcdManip));

    p_Manip->opcode = HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX;
    p_Manip->muramAllocate = FALSE;

    p_Manip->h_Ad = (t_Handle)XX_Malloc(FM_PCD_CC_AD_ENTRY_SIZE * sizeof(uint8_t));
    if (!p_Manip->h_Ad)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Allocation of Manipulation action descriptor"));
        XX_Free(p_Manip);
        return NULL;
    }

    memset(p_Manip->h_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE * sizeof(uint8_t));

    /*treatFdStatusFieldsAsErrors = TRUE hardcoded - assumption its always come after CAAM*/
    /*Application specific = type of flowId index, move internal frame header from data to IC,
     SEC errors check*/
    if (MvIntFrameHeaderFromFrameToBufferPrefix(p_Manip, TRUE)!= E_OK)
    {
        XX_Free(p_Manip->h_Ad);
        XX_Free(p_Manip);
        return NULL;
    }
    return p_Manip;
}

bool FmPcdManipIsCapwapApplSpecific(t_Handle h_Manip)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip;
    ASSERT_COND(h_Manip);

    return (bool)((p_Manip->opcode == HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST) ? TRUE : FALSE);
}
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
/*********************** End of inter-module routines ************************/

/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle FM_PCD_ManipNodeSet(t_Handle h_FmPcd,
                             t_FmPcdManipParams *p_ManipParams)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_FmPcdManip *p_Manip;
    t_Error err;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_ManipParams, E_INVALID_HANDLE, NULL);

    p_Manip = ManipOrStatsSetNode(h_FmPcd, (t_Handle)p_ManipParams, FALSE);
    if (!p_Manip)
        return NULL;

    if (((p_Manip->opcode == HMAN_OC_IP_REASSEMBLY)
            || (p_Manip->opcode == HMAN_OC_IP_FRAGMENTATION)
            || (p_Manip->opcode == HMAN_OC)
            || (p_Manip->opcode == HMAN_OC_IPSEC_MANIP)
#if (DPAA_VERSION >= 11)
            || (p_Manip->opcode == HMAN_OC_CAPWAP_MANIP)
            || (p_Manip->opcode == HMAN_OC_CAPWAP_FRAGMENTATION)
            || (p_Manip->opcode == HMAN_OC_CAPWAP_REASSEMBLY)
#endif /* (DPAA_VERSION >= 11) */
            ) && (!FmPcdIsAdvancedOffloadSupported(p_FmPcd)))
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Advanced-offload must be enabled"));
        XX_Free(p_Manip);
        return NULL;
    }
    p_Manip->h_Spinlock = XX_InitSpinlock();
    if (!p_Manip->h_Spinlock)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("UNSUPPORTED HEADER MANIPULATION TYPE"));
        ReleaseManipHandler(p_Manip, p_FmPcd);
        XX_Free(p_Manip);
        return NULL;
    }INIT_LIST(&p_Manip->nodesLst);

    switch (p_Manip->opcode)
    {
        case (HMAN_OC_IP_REASSEMBLY):
            /* IpReassembly */
            err = IpReassembly(&p_ManipParams->u.reassem, p_Manip);
            break;
        case (HMAN_OC_IP_FRAGMENTATION):
            /* IpFragmentation */
            err = IpFragmentation(&p_ManipParams->u.frag.u.ipFrag, p_Manip);
            if (err)
                break;
            err = IPManip(p_Manip);
            break;
        case (HMAN_OC_IPSEC_MANIP):
            err = IPSecManip(p_ManipParams, p_Manip);
            break;
#if (DPAA_VERSION >= 11)
        case (HMAN_OC_CAPWAP_REASSEMBLY):
            /* CapwapReassembly */
            err = CapwapReassembly(&p_ManipParams->u.reassem, p_Manip);
            break;
        case (HMAN_OC_CAPWAP_FRAGMENTATION):
            /* CapwapFragmentation */
            err = CapwapFragmentation(&p_ManipParams->u.frag.u.capwapFrag,
                                      p_Manip);
            break;
        case (HMAN_OC_CAPWAP_MANIP):
            err = CapwapManip(p_ManipParams, p_Manip);
            break;
#endif /* (DPAA_VERSION >= 11) */
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
            case (HMAN_OC_RMV_N_OR_INSRT_INT_FRM_HDR):
            /* HmanType1 */
            err = RmvHdrTillSpecLocNOrInsrtIntFrmHdr(&p_ManipParams->u.hdr.rmvParams, p_Manip);
            break;
            case (HMAN_OC_CAPWAP_FRAGMENTATION):
            err = CapwapFragmentation(&p_ManipParams->fragOrReasmParams.u.capwapFragParams,
                    p_Manip,
                    p_FmPcd,
                    p_ManipParams->fragOrReasmParams.sgBpid);
            if (err)
            {
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("UNSUPPORTED HEADER MANIPULATION TYPE"));
                ReleaseManipHandler(p_Manip, p_FmPcd);
                XX_Free(p_Manip);
                return NULL;
            }
            if (p_Manip->insrt)
            p_Manip->opcode = HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER;
            case (HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER):
            /* HmanType2 + if user asked only for fragmentation still need to allocate HmanType2 */
            err = InsrtHdrByTempl(&p_ManipParams->u.hdr.insrtParams, p_Manip, p_FmPcd);
            break;
            case (HMAN_OC_CAPWAP_REASSEMBLY):
            err = CapwapReassembly(&p_ManipParams->fragOrReasmParams.u.capwapReasmParams,
                    p_Manip,
                    p_FmPcd,
                    p_ManipParams->fragOrReasmParams.sgBpid);
            if (err)
            {
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("UNSUPPORTED HEADER MANIPULATION TYPE"));
                ReleaseManipHandler(p_Manip, p_FmPcd);
                XX_Free(p_Manip);
                return NULL;
            }
            if (p_Manip->rmv)
            p_Manip->opcode = HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST;
            case (HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST):
            /*CAPWAP decapsulation + if user asked only for reassembly still need to allocate CAPWAP decapsulation*/
            err = CapwapRmvDtlsHdr(p_FmPcd, p_Manip);
            break;
            case (HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX):
            /*Application Specific type 1*/
            err = MvIntFrameHeaderFromFrameToBufferPrefix(p_Manip, TRUE);
            break;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
        case (HMAN_OC):
            /* New Manip */
            err = CreateManipActionNew(p_Manip, p_ManipParams);
            break;
        default:
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("UNSUPPORTED HEADER MANIPULATION TYPE"));
            ReleaseManipHandler(p_Manip, p_FmPcd);
            XX_Free(p_Manip);
            return NULL;
    }

    if (err)
    {
        REPORT_ERROR(MAJOR, err, NO_MSG);
        ReleaseManipHandler(p_Manip, p_FmPcd);
        XX_Free(p_Manip);
        return NULL;
    }

    if (p_ManipParams->h_NextManip)
    {
        /* in the check routine we've verified that h_NextManip has no owners
         * and that only supported types are allowed. */
        p_Manip->h_NextManip = p_ManipParams->h_NextManip;
        /* save a "prev" pointer in h_NextManip */
        MANIP_SET_PREV(p_Manip->h_NextManip, p_Manip);
        FmPcdManipUpdateOwner(p_Manip->h_NextManip, TRUE);
    }

    return p_Manip;
}

t_Error FM_PCD_ManipNodeReplace(t_Handle h_Manip,
                                t_FmPcdManipParams *p_ManipParams)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_Manip, *p_FirstManip;
    t_FmPcd *p_FmPcd = (t_FmPcd *)(p_Manip->h_FmPcd);
    t_Error err;
    uint8_t *p_WholeHmct = NULL, *p_ShadowHmct = NULL, *p_Hmtd = NULL;
    t_List lstOfNodeshichPointsOnCrntMdfManip, *p_Pos;
    t_CcNodeInformation *p_CcNodeInfo;
    SANITY_CHECK_RETURN_ERROR(h_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ManipParams, E_INVALID_HANDLE);

    INIT_LIST(&lstOfNodeshichPointsOnCrntMdfManip);

    if ((p_ManipParams->type != e_FM_PCD_MANIP_HDR)
            || (p_Manip->type != e_FM_PCD_MANIP_HDR))
        RETURN_ERROR(
                MINOR,
                E_NOT_SUPPORTED,
                ("FM_PCD_ManipNodeReplace Functionality supported only for Header Manipulation."));

    ASSERT_COND(p_Manip->opcode == HMAN_OC);
    ASSERT_COND(p_Manip->manipParams.h_NextManip == p_Manip->h_NextManip);
    memcpy((uint8_t*)&p_Manip->manipParams, p_ManipParams,
           sizeof(p_Manip->manipParams));
    p_Manip->manipParams.h_NextManip = p_Manip->h_NextManip;

    /* The replacement of the HdrManip depends on the node type.*/
    /*
     * (1) If this is an independent node, all its owners should be updated.
     *
     * (2) If it is the head of a cascaded chain (it does not have a "prev" but
     * it has a "next" and it has a "cascaded" indication), the next
     * node remains unchanged, and the behavior is as in (1).
     *
     * (3) If it is not the head, but a part of a cascaded chain, in can be
     * also replaced as a regular node with just one owner.
     *
     * (4) If it is a part of a chain implemented as a unified table, the
     * whole table is replaced and the owners of the head node must be updated.
     *
     */
    /* lock shadow */
    if (!p_FmPcd->p_CcShadow)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("CC Shadow not allocated"));

    if (!TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
        return ERROR_CODE(E_BUSY);

    /* this routine creates a new manip action in the CC Shadow. */
    err = CreateManipActionShadow(p_Manip, p_ManipParams);
    if (err)
        RETURN_ERROR(MINOR, err, NO_MSG);

    /* If the owners list is empty (these are NOT the "owners" counter, but pointers from CC)
     * replace only HMTD and no lcok is required. Otherwise
     * lock the whole PCD
     * In case 4 MANIP_IS_UNIFIED_NON_FIRST(p_Manip) - Use the head node instead. */
    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    p_ShadowHmct = (uint8_t*)PTR_MOVE(p_FmPcd->p_CcShadow, 16);

    p_FirstManip = (t_FmPcdManip*)GetManipInfo(p_Manip,
                                               e_MANIP_HANDLER_TABLE_OWNER);
    ASSERT_COND(p_FirstManip);

    if (!NCSW_LIST_IsEmpty(&p_FirstManip->nodesLst))
        UpdateAdPtrOfNodesWhichPointsOnCrntMdfManip(
                p_FirstManip, &lstOfNodeshichPointsOnCrntMdfManip);

    p_Hmtd = (uint8_t *)GetManipInfo(p_Manip, e_MANIP_HMTD);
    ASSERT_COND(p_Hmtd);
    BuildHmtd(p_FmPcd->p_CcShadow, (uint8_t *)p_Hmtd, p_ShadowHmct,
              ((t_FmPcd*)(p_Manip->h_FmPcd)));

    NCSW_LIST_FOR_EACH(p_Pos, &lstOfNodeshichPointsOnCrntMdfManip)
    {
        p_CcNodeInfo = CC_NODE_F_OBJECT(p_Pos);
        BuildHmtd(p_FmPcd->p_CcShadow, (uint8_t *)p_CcNodeInfo->h_CcNode,
                  p_ShadowHmct, ((t_FmPcd*)(p_Manip->h_FmPcd)));
    }

    p_WholeHmct = (uint8_t *)GetManipInfo(p_Manip, e_MANIP_HMCT);
    ASSERT_COND(p_WholeHmct);

    /* re-build the HMCT n the original location */
    err = CreateManipActionBackToOrig(p_Manip, p_ManipParams);
    if (err)
    {
        RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MINOR, err, NO_MSG);
    }

    p_Hmtd = (uint8_t *)GetManipInfo(p_Manip, e_MANIP_HMTD);
    ASSERT_COND(p_Hmtd);
    BuildHmtd(p_FmPcd->p_CcShadow, (uint8_t *)p_Hmtd, p_WholeHmct,
              ((t_FmPcd*)p_Manip->h_FmPcd));

    /* If NCSW_LIST > 0, create a list of p_Ad's that point to the HMCT. Join also t_HMTD to this list.
     * For each p_Hmct (from list+fixed):
     * call Host Command to replace HMTD by a new one */NCSW_LIST_FOR_EACH(p_Pos, &lstOfNodeshichPointsOnCrntMdfManip)
    {
        p_CcNodeInfo = CC_NODE_F_OBJECT(p_Pos);
        BuildHmtd(p_FmPcd->p_CcShadow, (uint8_t *)p_CcNodeInfo->h_CcNode,
                  p_WholeHmct, ((t_FmPcd*)(p_Manip->h_FmPcd)));
    }


    ReleaseLst(&lstOfNodeshichPointsOnCrntMdfManip);

    FmPcdLockUnlockAll(p_FmPcd);

    /* unlock shadow */
    RELEASE_LOCK(p_FmPcd->shadowLock);

    return E_OK;
}

t_Error FM_PCD_ManipNodeDelete(t_Handle h_ManipNode)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_ManipNode;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);

    if (p_Manip->owner)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("This manipulation node not be removed because this node is occupied, first - unbind this node "));

    if (p_Manip->h_NextManip)
    {
        MANIP_SET_PREV(p_Manip->h_NextManip, NULL);
        FmPcdManipUpdateOwner(p_Manip->h_NextManip, FALSE);
    }

    if (p_Manip->p_Hmct
            && (MANIP_IS_UNIFIED_FIRST(p_Manip) || !MANIP_IS_UNIFIED(p_Manip)))
        FM_MURAM_FreeMem(((t_FmPcd *)p_Manip->h_FmPcd)->h_FmMuram,
                         p_Manip->p_Hmct);

    if (p_Manip->h_Spinlock)
    {
        XX_FreeSpinlock(p_Manip->h_Spinlock);
        p_Manip->h_Spinlock = NULL;
    }

    ReleaseManipHandler(p_Manip, p_Manip->h_FmPcd);

    XX_Free(h_ManipNode);

    return E_OK;
}

t_Error FM_PCD_ManipGetStatistics(t_Handle h_ManipNode,
                                  t_FmPcdManipStats *p_FmPcdManipStats)
{
    t_FmPcdManip *p_Manip = (t_FmPcdManip *)h_ManipNode;

    SANITY_CHECK_RETURN_ERROR(p_Manip, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcdManipStats, E_NULL_POINTER);

    switch (p_Manip->opcode)
    {
        case (HMAN_OC_IP_REASSEMBLY):
            return IpReassemblyStats(p_Manip,
                                     &p_FmPcdManipStats->u.reassem.u.ipReassem);
        case (HMAN_OC_IP_FRAGMENTATION):
            return IpFragmentationStats(p_Manip,
                                        &p_FmPcdManipStats->u.frag.u.ipFrag);
#if (DPAA_VERSION >= 11)
        case (HMAN_OC_CAPWAP_REASSEMBLY):
            return CapwapReassemblyStats(
                    p_Manip, &p_FmPcdManipStats->u.reassem.u.capwapReassem);
	case (HMAN_OC_CAPWAP_FRAGMENTATION):
		return CapwapFragmentationStats(
			p_Manip, &p_FmPcdManipStats->u.frag.u.capwapFrag);
#endif /* (DPAA_VERSION >= 11) */
        default:
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                         ("no statistics to this type of manip"));
    }

    return E_OK;
}

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
t_Handle FM_PCD_StatisticsSetNode(t_Handle h_FmPcd, t_FmPcdStatsParams *p_StatsParams)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_FmPcdManip *p_Manip;
    t_Error err;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd,E_INVALID_HANDLE,NULL);
    SANITY_CHECK_RETURN_VALUE(p_StatsParams,E_INVALID_HANDLE,NULL);

    p_Manip = ManipOrStatsSetNode(h_FmPcd, (t_Handle)p_StatsParams, TRUE);
    if (!p_Manip)
    return NULL;

    switch (p_Manip->opcode)
    {
        case (HMAN_OC_CAPWAP_INDEXED_STATS):
        /* Indexed statistics */
        err = IndxStats(p_StatsParams, p_Manip, p_FmPcd);
        break;
        default:
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("UNSUPPORTED Statistics type"));
        ReleaseManipHandler(p_Manip, p_FmPcd);
        XX_Free(p_Manip);
        return NULL;
    }

    if (err)
    {
        REPORT_ERROR(MAJOR, err, NO_MSG);
        ReleaseManipHandler(p_Manip, p_FmPcd);
        XX_Free(p_Manip);
        return NULL;
    }

    return p_Manip;
}
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
