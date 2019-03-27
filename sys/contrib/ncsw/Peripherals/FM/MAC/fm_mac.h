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
 @File          fm_mac.h

 @Description   FM MAC ...
*//***************************************************************************/
#ifndef __FM_MAC_H
#define __FM_MAC_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"
#include "fm_mac_ext.h"
#include "fm_common.h"


#define __ERR_MODULE__  MODULE_FM_MAC

/**************************************************************************//**
 @Description       defaults
*//***************************************************************************/


#define DEFAULT_halfDuplex                  FALSE
#define DEFAULT_padAndCrcEnable             TRUE
#define DEFAULT_resetOnInit                 FALSE


typedef struct {
    uint64_t addr;      /* Ethernet Address  */
    t_List   node;
} t_EthHashEntry;
#define ETH_HASH_ENTRY_OBJ(ptr) NCSW_LIST_OBJECT(ptr, t_EthHashEntry, node)

typedef struct {
    uint16_t    size;
    t_List      *p_Lsts;
} t_EthHash;

typedef struct {
    t_Error (*f_FM_MAC_Init) (t_Handle h_FmMac);
    t_Error (*f_FM_MAC_Free) (t_Handle h_FmMac);

    t_Error (*f_FM_MAC_SetStatistics) (t_Handle h_FmMac, e_FmMacStatisticsLevel statisticsLevel);
    t_Error (*f_FM_MAC_ConfigLoopback) (t_Handle h_FmMac, bool newVal);
    t_Error (*f_FM_MAC_ConfigMaxFrameLength) (t_Handle h_FmMac, uint16_t newVal);
    t_Error (*f_FM_MAC_ConfigWan) (t_Handle h_FmMac, bool flag);
    t_Error (*f_FM_MAC_ConfigPadAndCrc) (t_Handle h_FmMac, bool newVal);
    t_Error (*f_FM_MAC_ConfigHalfDuplex) (t_Handle h_FmMac, bool newVal);
    t_Error (*f_FM_MAC_ConfigLengthCheck) (t_Handle h_FmMac, bool newVal);
    t_Error (*f_FM_MAC_ConfigTbiPhyAddr) (t_Handle h_FmMac, uint8_t newVal);
    t_Error (*f_FM_MAC_ConfigException) (t_Handle h_FmMac, e_FmMacExceptions, bool enable);
    t_Error (*f_FM_MAC_ConfigResetOnInit) (t_Handle h_FmMac, bool enable);
#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
    t_Error (*f_FM_MAC_ConfigSkipFman11Workaround) (t_Handle h_FmMac);
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */

    t_Error (*f_FM_MAC_SetException) (t_Handle h_FmMac, e_FmMacExceptions ex, bool enable);

    t_Error (*f_FM_MAC_Enable)  (t_Handle h_FmMac,  e_CommMode mode);
    t_Error (*f_FM_MAC_Disable) (t_Handle h_FmMac, e_CommMode mode);
    t_Error (*f_FM_MAC_Resume)  (t_Handle h_FmMac);
    t_Error (*f_FM_MAC_Enable1588TimeStamp) (t_Handle h_FmMac);
    t_Error (*f_FM_MAC_Disable1588TimeStamp) (t_Handle h_FmMac);
    t_Error (*f_FM_MAC_Reset)   (t_Handle h_FmMac, bool wait);

    t_Error (*f_FM_MAC_SetTxAutoPauseFrames) (t_Handle h_FmMac,
                                              uint16_t pauseTime);
    t_Error (*f_FM_MAC_SetTxPauseFrames) (t_Handle h_FmMac,
                                          uint8_t  priority,
                                          uint16_t pauseTime,
                                          uint16_t threshTime);
    t_Error (*f_FM_MAC_SetRxIgnorePauseFrames) (t_Handle h_FmMac, bool en);

    t_Error (*f_FM_MAC_ResetCounters) (t_Handle h_FmMac);
    t_Error (*f_FM_MAC_GetStatistics) (t_Handle h_FmMac, t_FmMacStatistics *p_Statistics);

    t_Error (*f_FM_MAC_ModifyMacAddr) (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);
    t_Error (*f_FM_MAC_AddHashMacAddr) (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);
    t_Error (*f_FM_MAC_RemoveHashMacAddr) (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);
    t_Error (*f_FM_MAC_AddExactMatchMacAddr) (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);
    t_Error (*f_FM_MAC_RemovelExactMatchMacAddr) (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr);

    t_Error (*f_FM_MAC_SetPromiscuous) (t_Handle h_FmMac, bool newVal);
    t_Error (*f_FM_MAC_AdjustLink)     (t_Handle h_FmMac, e_EnetSpeed speed, bool fullDuplex);
    t_Error (*f_FM_MAC_RestartAutoneg) (t_Handle h_FmMac);

    t_Error (*f_FM_MAC_SetWakeOnLan)   (t_Handle h_FmMac, bool en);

    t_Error (*f_FM_MAC_GetId) (t_Handle h_FmMac, uint32_t *macId);

    t_Error (*f_FM_MAC_GetVersion) (t_Handle h_FmMac, uint32_t *macVersion);

    uint16_t (*f_FM_MAC_GetMaxFrameLength) (t_Handle h_FmMac);

    t_Error (*f_FM_MAC_MII_WritePhyReg)(t_Handle h_FmMac, uint8_t phyAddr, uint8_t reg, uint16_t data);
    t_Error (*f_FM_MAC_MII_ReadPhyReg)(t_Handle h_FmMac,  uint8_t phyAddr, uint8_t reg, uint16_t *p_Data);

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
    t_Error (*f_FM_MAC_DumpRegs) (t_Handle h_FmMac);
#endif /* (defined(DEBUG_ERRORS) && ... */

    t_Handle            h_Fm;
    t_FmRevisionInfo    fmRevInfo;
    e_EnetMode          enetMode;
    uint8_t             macId;
    bool                resetOnInit;
    uint16_t            clkFreq;
} t_FmMacControllerDriver;


#if (DPAA_VERSION == 10)
t_Handle    DTSEC_Config(t_FmMacParams *p_FmMacParam);
t_Handle    TGEC_Config(t_FmMacParams *p_FmMacParams);
#else
t_Handle    MEMAC_Config(t_FmMacParams *p_FmMacParam);
#endif /* (DPAA_VERSION == 10) */
uint16_t    FM_MAC_GetMaxFrameLength(t_Handle FmMac);


/* ........................................................................... */

static __inline__ t_EthHashEntry *DequeueAddrFromHashEntry(t_List *p_AddrLst)
{
   t_EthHashEntry *p_HashEntry = NULL;
    if (!NCSW_LIST_IsEmpty(p_AddrLst))
    {
        p_HashEntry = ETH_HASH_ENTRY_OBJ(p_AddrLst->p_Next);
        NCSW_LIST_DelAndInit(&p_HashEntry->node);
    }
    return p_HashEntry;
}

/* ........................................................................... */

static __inline__ void FreeHashTable(t_EthHash *p_Hash)
{
    t_EthHashEntry  *p_HashEntry;
    int             i = 0;

    if (p_Hash)
    {
        if  (p_Hash->p_Lsts)
        {
            for (i=0; i<p_Hash->size; i++)
            {
                p_HashEntry = DequeueAddrFromHashEntry(&p_Hash->p_Lsts[i]);
                while (p_HashEntry)
                {
                    XX_Free(p_HashEntry);
                    p_HashEntry = DequeueAddrFromHashEntry(&p_Hash->p_Lsts[i]);
                }
            }

            XX_Free(p_Hash->p_Lsts);
        }

        XX_Free(p_Hash);
    }
}

/* ........................................................................... */

static __inline__ t_EthHash * AllocHashTable(uint16_t size)
{
    uint32_t    i;
    t_EthHash *p_Hash;

    /* Allocate address hash table */
    p_Hash = (t_EthHash *)XX_Malloc(sizeof(t_EthHash));
    if (!p_Hash)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Address hash table"));
        return NULL;
    }
    p_Hash->size = size;

    p_Hash->p_Lsts = (t_List *)XX_Malloc(p_Hash->size*sizeof(t_List));
    if (!p_Hash->p_Lsts)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Address hash table"));
        XX_Free(p_Hash);
        return NULL;
    }

    for (i=0 ; i<p_Hash->size; i++)
        INIT_LIST(&p_Hash->p_Lsts[i]);

    return p_Hash;
}


#endif /* __FM_MAC_H */
