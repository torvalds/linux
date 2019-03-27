/*******************************************************************************
 *Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 ********************************************************************************/
/*******************************************************************************/
/** \file
 *
 * $RCSfile: ttdinit.c,v $
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
 * $Author: vempatin $
 * $Revision: 113679 $
 * $Date: 2012-04-16 14:35:19 -0700 (Mon, 16 Apr 2012) $
 *
 * This file contains initiator IO related functions in TD layer
 *
 */
#include <osenv.h>
#include <ostypes.h>
#include <osdebug.h>

#include <sa.h>
#include <saapi.h>
#include <saosapi.h>

#include <titypes.h>
#include <ostiapi.h>
#include <tiapi.h>
#include <tiglobal.h>

#include <tdtypes.h>
#include <osstring.h>
#include <tdutil.h>

#ifdef INITIATOR_DRIVER
#include <itdtypes.h>
#include <itddefs.h>
#include <itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include "ttdglobl.h"
#include "ttdtxchg.h"
#include "ttdtypes.h"
#endif

#include <tdsatypes.h>
#include <tdproto.h>

/* io trace only */
extern void TDTraceInit(void);
/* io trace only */


osGLOBAL bit32
ttdssInit(
        tiRoot_t              *tiRoot,
        tiTargetResource_t    *targetResource,
        tiTdSharedMem_t       *tdSharedMem
)
{
    tdsaRoot_t                *tdsaRoot  = (tdsaRoot_t *)tiRoot->tdData;
    tiTargetMem_t             *tgtMem;
    ttdsaTgt_t                *Target;
    ttdssOperatingOption_t    *OperatingOption;
    char                      *buffer;
    bit32                     buffLen;
    bit32                     lenRecv = 0;
    char                      *pLastUsedChar = agNULL;
    char                      tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
    char                      globalStr[]     = "OSParms";

    TI_DBG5(("ttdssInit: start\n"));

    /*
     first set the values to Default values
     Then, overwrite them using ostiGetTransportParam()
     */

    /* to remove compiler warnings */
    buffer          = &tmpBuffer[0];
    buffLen         = sizeof (tmpBuffer);

    osti_memset(buffer, 0, buffLen);

    tgtMem = &targetResource->targetMem;

    /*
     * Cached mem for target Transport Dependent Layer main functionality
     */
    Target = tgtMem->tdMem[0].virtPtr;

    OperatingOption = &Target->OperatingOption;
    /*
     * Get default parameters from the OS Specific area
     * and reads parameters from the configuration file
     */
    ttdssGetOperatingOptionParams(tiRoot, OperatingOption);


    /*
     * Update TD operating options
     */
    OperatingOption->UsecsPerTick =
            targetResource->targetOption.usecsPerTick;
    OperatingOption->numXchgs = tgtMem->tdMem[1].numElements;


    if (ttdsaXchgInit(tiRoot,
            &Target->ttdsaXchgData,
            tgtMem,
            OperatingOption->numXchgs
    ) == agFALSE)
    {
        TI_DBG1(("ttdInit: ttdsaXchgInit failed\n"));
        return tiError;
    }

    /* Get number of AutoGoodResponse entry */
    if ((ostiGetTransportParam(
                                tiRoot,
                                globalStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "AutoGoodResponse",
                                buffer,
                                buffLen,
                                &lenRecv
                              ) == tiSuccess) && (lenRecv != 0))
    {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
            tdsaRoot->autoGoodRSP = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
            tdsaRoot->autoGoodRSP = osti_strtoul (buffer, &pLastUsedChar, 10);
        }

    }

    return tiSuccess;
}

/*
  this combines ttdGetDefaultParams and ttdGetTargetParms

 */
osGLOBAL void
ttdssGetOperatingOptionParams(
        tiRoot_t                *tiRoot,
        ttdssOperatingOption_t  *OperatingOption
)
{
    char    *key = agNULL;
    char    *subkey1 = agNULL;
    char    *subkey2 = agNULL;
    char    *buffer;
    bit32   buffLen;
    bit32   lenRecv = 0;
    char    *pLastUsedChar = agNULL;
    char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
    char    globalStr[]     = "Global";
    char    iniParmsStr[]   = "TargetParms";

    TI_DBG5(("ttdssGetOperatingOptionParams: start\n"));

    /*
     first set the values to Default values
     Then, overwrite them using ostiGetTransportParam()
     */


    /* to remove compiler warnings */
    pLastUsedChar   = pLastUsedChar;
    lenRecv         = lenRecv;
    subkey2         = subkey2;
    subkey1         = subkey1;
    key             = key;
    buffer          = &tmpBuffer[0];
    buffLen         = sizeof (tmpBuffer);

    osti_memset(buffer, 0, buffLen);


    /* in ttgglobl.h */
    OperatingOption->numXchgs = DEFAULT_XCHGS;
    OperatingOption->UsecsPerTick = DEFAULT_TGT_TIMER_TICK; /* 1 sec */
    OperatingOption->MaxTargets = DEFAULT_MAX_TARGETS;
    OperatingOption->BlockSize = DEFAULT_BLOCK_SIZE;


    /* defaults are overwritten in the following */
    /* Get number of exchanges */
    if ((ostiGetTransportParam(
            tiRoot,
            globalStr,
            iniParmsStr,
            agNULL,
            agNULL,
            agNULL,
            agNULL,
            "NumberExchanges",
            buffer,
            buffLen,
            &lenRecv
    ) == tiSuccess) && (lenRecv != 0))
    {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
            OperatingOption->numXchgs = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
            OperatingOption->numXchgs = osti_strtoul (buffer, &pLastUsedChar, 10);
        }

    }

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    /* Get number of MaxTargets */
    if ((ostiGetTransportParam(
            tiRoot,
            globalStr,
            iniParmsStr,
            agNULL,
            agNULL,
            agNULL,
            agNULL,
            "MaxTargets",
            buffer,
            buffLen,
            &lenRecv
    ) == tiSuccess) && (lenRecv != 0))
    {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
            OperatingOption->MaxTargets = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
            OperatingOption->MaxTargets = osti_strtoul (buffer, &pLastUsedChar, 10);
        }

    }
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    /* Get number of BlockSize */
    if ((ostiGetTransportParam(
            tiRoot,
            globalStr,
            iniParmsStr,
            agNULL,
            agNULL,
            agNULL,
            agNULL,
            "BlockSize",
            buffer,
            buffLen,
            &lenRecv
    ) == tiSuccess) && (lenRecv != 0))
    {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
            OperatingOption->BlockSize = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
            OperatingOption->BlockSize = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
    }
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;



    TI_DBG5(("ttdssGetOperatingOptionParams: NumberExchanges %d UsecsPerTick %d MaxTargets %d BlockSize %d\n", OperatingOption->numXchgs, OperatingOption->UsecsPerTick, OperatingOption->MaxTargets, OperatingOption->BlockSize));

    return;
}

/* not yet */
osGLOBAL void
ttdssGetResource(
        tiRoot_t              *tiRoot,
        tiTargetResource_t    *targetResource
)
{
    tiTargetMem_t            *tgtMem;
    int i;
    ttdssOperatingOption_t   OperatingOption;
    bit32                     xchgSize;
    bit32                     respSize;
    bit32                     smprespSize;

    TI_DBG4(("ttdssGetResource: start\n"));

    tgtMem = &targetResource->targetMem;

    /*
    only 4 memory descriptors are used
     */
    tgtMem->count = 4;

    /* initiailization */
    for (i = 0 ; i < 10 ; i++)
    {
        tgtMem->tdMem[i].singleElementLength  = 0;
        tgtMem->tdMem[i].numElements          = 0;
        tgtMem->tdMem[i].totalLength          = 0;
        tgtMem->tdMem[i].alignment            = 0;
        tgtMem->tdMem[i].type                 = TI_CACHED_MEM;
        tgtMem->tdMem[i].reserved             = 0;
        tgtMem->tdMem[i].virtPtr               = agNULL;
        tgtMem->tdMem[i].osHandle              = agNULL;
        tgtMem->tdMem[i].physAddrUpper         = 0;
        tgtMem->tdMem[i].physAddrLower         = 0;
    }

    /*
     * Get default parameters from the OS Specific area
     * and reads parameters from the configuration file
     */
    ttdssGetOperatingOptionParams(tiRoot, &OperatingOption);

    /* target */
    tgtMem->tdMem[0].singleElementLength  = sizeof(ttdsaTgt_t);
    tgtMem->tdMem[0].numElements          = 1;
    tgtMem->tdMem[0].totalLength          =
            tgtMem->tdMem[0].singleElementLength *
            tgtMem->tdMem[0].numElements;
    tgtMem->tdMem[0].alignment            = sizeof (void *);
    tgtMem->tdMem[0].type                 = TI_CACHED_MEM;
    tgtMem->tdMem[0].reserved             = 0;
    tgtMem->tdMem[0].virtPtr               = agNULL;
    tgtMem->tdMem[0].osHandle              = agNULL;
    tgtMem->tdMem[0].physAddrUpper         = 0;
    tgtMem->tdMem[0].physAddrLower         = 0;

    /*
     * Cached memory for I/O exchange structures
     */
    xchgSize = sizeof(ttdsaXchg_t);
    xchgSize = AG_ALIGNSIZE(xchgSize, 8);

    tgtMem->tdMem[1].singleElementLength = xchgSize;
    tgtMem->tdMem[1].numElements         = OperatingOption.numXchgs;
    tgtMem->tdMem[1].totalLength         = tgtMem->tdMem[1].singleElementLength *
            tgtMem->tdMem[1].numElements;
    tgtMem->tdMem[1].alignment           = sizeof(void *);
    tgtMem->tdMem[1].type                = TI_CACHED_MEM;
    tgtMem->tdMem[1].reserved             = 0;
    tgtMem->tdMem[1].virtPtr               = agNULL;
    tgtMem->tdMem[1].osHandle              = agNULL;
    tgtMem->tdMem[1].physAddrUpper         = 0;
    tgtMem->tdMem[1].physAddrLower         = 0;

    /*
     * Uncached memory for response buffer structures
     */
    TI_DBG4(("ttdssGetResource: sas_resp_t size 0x%x %d\n",
            (unsigned int)sizeof(sas_resp_t), (int)sizeof(sas_resp_t)));

    respSize = (sizeof(sas_resp_t) + AG_WORD_ALIGN_ADD) & AG_WORD_ALIGN_MASK;
    TI_DBG4(("ttdssGetResource: response size 0x%x %d\n", respSize,respSize));
    respSize = AG_ALIGNSIZE(respSize, 8);
    TI_DBG4(("ttdssGetResource: response size 0x%x %d\n", respSize,respSize));
    tgtMem->tdMem[2].singleElementLength = 0x1000; /* respSize; 0x1000;  */
    tgtMem->tdMem[2].numElements         = OperatingOption.numXchgs;  /* Same as num of xchg */
    tgtMem->tdMem[2].totalLength         = tgtMem->tdMem[2].singleElementLength *
            tgtMem->tdMem[2].numElements;
    /* 8;4;16;256;sizeof(void *); all worked */
    tgtMem->tdMem[2].alignment           = 16;
    tgtMem->tdMem[2].type                = TI_DMA_MEM;  /* uncached memory */
    tgtMem->tdMem[2].reserved             = 0;
    tgtMem->tdMem[2].virtPtr               = agNULL;
    tgtMem->tdMem[2].osHandle              = agNULL;
    tgtMem->tdMem[2].physAddrUpper         = 0;
    tgtMem->tdMem[2].physAddrLower         = 0;

    /*
     * Uncached memory for SMP response buffer structures
     */
    smprespSize = sizeof(smp_resp_t);
    smprespSize = AG_ALIGNSIZE(smprespSize, 8);
    TI_DBG4(("ttdssGetResource: SMP response size 0x%x %d\n", smprespSize,smprespSize));

    tgtMem->tdMem[3].singleElementLength = smprespSize; /*0x1000; smprespSize; */
    tgtMem->tdMem[3].numElements         = OperatingOption.numXchgs;  /* Same as num of xchg */
    tgtMem->tdMem[3].totalLength
    = tgtMem->tdMem[3].singleElementLength * tgtMem->tdMem[3].numElements;
    tgtMem->tdMem[3].alignment           = 16; /* 4; 256; 16; sizeof(void *); */
    tgtMem->tdMem[3].type                = TI_DMA_MEM;  /* uncached memory */
    tgtMem->tdMem[3].reserved             = 0;
    tgtMem->tdMem[3].virtPtr               = agNULL;
    tgtMem->tdMem[3].osHandle              = agNULL;
    tgtMem->tdMem[3].physAddrUpper         = 0;
    tgtMem->tdMem[3].physAddrLower         = 0;



    targetResource->targetOption.usecsPerTick = OperatingOption.UsecsPerTick;
    targetResource->targetOption.pageSize     = 0; /* not applicable to SAS/SATA */
    targetResource->targetOption.numLgns      = 0; /* not applicable to SAS/SATA */
    targetResource->targetOption.numSessions  = 0; /* not applicable to SAS/SATA */
    targetResource->targetOption.numXchgs     = OperatingOption.numXchgs;


    /*
    This is not used in OS like Linux which supports dynamic memeory allocation
    In short, this is for Windows
     */
    /* Estimate dynamic DMA memory */
    targetResource->targetOption.dynamicDmaMem.alignment = sizeof(void *);

    targetResource->targetOption.dynamicDmaMem.numElements = 128;
    targetResource->targetOption.dynamicDmaMem.singleElementLength = sizeof(tdssSMPRequestBody_t);
    targetResource->targetOption.dynamicDmaMem.totalLength =
            targetResource->targetOption.dynamicDmaMem.numElements *
            targetResource->targetOption.dynamicDmaMem.singleElementLength;

    /* Estimate dynamic cached memory */
    targetResource->targetOption.dynamicCachedMem.alignment =  sizeof(void *);
    targetResource->targetOption.dynamicCachedMem.numElements = 128;
    targetResource->targetOption.dynamicCachedMem.singleElementLength = sizeof(tdssSMPRequestBody_t);
    targetResource->targetOption.dynamicCachedMem.totalLength =
            targetResource->targetOption.dynamicCachedMem.numElements *
            targetResource->targetOption.dynamicCachedMem.singleElementLength;


    return;
}

/* not in use */
osGLOBAL void
ttdssGetTargetParams(
        tiRoot_t          *tiRoot
)
{
    TI_DBG6(("ttdssGetTargetParams: start\n"));
    return;
}

osGLOBAL agBOOLEAN
ttdsaXchgInit(
        tiRoot_t           *tiRoot,
        ttdsaXchgData_t    *ttdsaXchgData,
        tiTargetMem_t      *tgtMem,
        bit32              maxNumXchgs
)
{
    ttdsaXchg_t       *ttdsaXchg;
    bit32             i, respLen;
    bit8              *virtualAddr;
    bit32             phyAddrLower, phyAddrUpper;
    bit32             smprespLen;
    bit32             smpphyAddrLower, smpphyAddrUpper;
    bit8              *smpvirtualAddr;



    TI_DBG5(("ttdsaXchgInit: start\n"));
    /* io trace only */
    TDTraceInit();
    /* io trace only */

    /*
     * Set and initialize some global exchange information
     */
    TDLIST_INIT_HDR(&ttdsaXchgData->xchgFreeList);
    TDLIST_INIT_HDR(&ttdsaXchgData->xchgBusyList);

    ttdsaXchgData->maxNumXchgs = maxNumXchgs;

    /* Initialize exchange and response buffer structures */
    ttdsaXchg = (ttdsaXchg_t *) tgtMem->tdMem[1].virtPtr;

    /* Initialize response buffer */
    virtualAddr  = tgtMem->tdMem[2].virtPtr;
    phyAddrUpper = tgtMem->tdMem[2].physAddrUpper;
    phyAddrLower = tgtMem->tdMem[2].physAddrLower;
    respLen      = tgtMem->tdMem[2].singleElementLength;

    ttdsaXchg->resp.virtAddr      = virtualAddr;
    ttdsaXchg->resp.phyAddrUpper = phyAddrUpper;
    ttdsaXchg->resp.phyAddrLower = phyAddrLower;
    ttdsaXchg->resp.length = respLen;

    /* Initialize SMP response buffer */
    smpvirtualAddr  = tgtMem->tdMem[3].virtPtr;
    smpphyAddrUpper = tgtMem->tdMem[3].physAddrUpper;
    smpphyAddrLower = tgtMem->tdMem[3].physAddrLower;
    smprespLen      = tgtMem->tdMem[3].singleElementLength;

    ttdsaXchg->smpresp.virtAddr      = smpvirtualAddr;
    ttdsaXchg->smpresp.phyAddrUpper = smpphyAddrUpper;
    ttdsaXchg->smpresp.phyAddrLower = smpphyAddrLower;
    ttdsaXchg->smpresp.length = smprespLen;

    /* Initialization of callback and etc */
    for (i=0;i<maxNumXchgs;i++)
    {
        ttdsaXchg->id = i;
        ttdsaXchg->usedEsgl = agFALSE;
        ttdsaXchg->io_found = agTRUE;
        ttdsaXchg->DeviceData = agNULL;
        /* callback for IO(ssp) and SMP */
        ttdsaXchg->IORequestBody.IOCompletionFunc = ttdsaIOCompleted;
        ttdsaXchg->SMPRequestBody.SMPCompletionFunc = ttdsaSMPCompleted;


        TDLIST_INIT_ELEMENT(&ttdsaXchg->XchgLinks );

        ttdsaXchg->IORequestBody.agIORequest.osData = (void *)ttdsaXchg;
        ttdsaXchg->IORequestBody.tiIORequest
        = &(ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest);

        /* Init the tdData portion of tiIORequest context for this exchange */
        ttdsaXchg->IORequestBody.tiIORequest->tdData = ttdsaXchg;

        /* SMP */
        ttdsaXchg->SMPRequestBody.agIORequest.osData = (void *)ttdsaXchg;
        /* ttdsaXchg->SMPRequestBody.agIORequest.osData = (void *)&ttdsaXchg->SMPRequestBody; */
        /*ttdsaXchg->SMPRequestBody.tiIORequest.tdData = (void *)&ttdsaXchg->SMPRequestBody; */




        /* Initialize the CDB and LUN addresses */
        ttdsaXchg->tiTgtScsiCmnd.reqCDB  = &(ttdsaXchg->agSSPCmndIU.cdb[0]);
        ttdsaXchg->tiTgtScsiCmnd.scsiLun = &(ttdsaXchg->agSSPCmndIU.lun[0]);

        ttdsaXchg->index = i;
        ttdsaXchg->respLen = respLen; /* 100 */
        ttdsaXchg->smprespLen = smprespLen; /* 100 */
        ttdsaXchg->TLR = 0;
        TD_XCHG_SET_STATE(ttdsaXchg, TD_XCHG_STATE_INACTIVE);
        ttdsaXchg->retries = 0;

        ttdsaXchgLinkInit(tiRoot,ttdsaXchg);

        /* Save current response payload/buffer address */
        virtualAddr  = ttdsaXchg->resp.virtAddr;
        phyAddrLower = ttdsaXchg->resp.phyAddrLower;
        smpvirtualAddr  = ttdsaXchg->smpresp.virtAddr;
        smpphyAddrLower = ttdsaXchg->smpresp.phyAddrLower;

        TI_DBG5(("ttdsaXchgInit: +1 before\n"));
        if (i == (maxNumXchgs - 1))
        {
            /* at the last one */
            TI_DBG5(("ttdsaXchgInit: last one break\n"));
            break;
        }

        /* Advance to next exchange */
        ttdsaXchg = ttdsaXchg + 1;
        TI_DBG5(("ttdsaXchgInit: +1 after\n"));

        /* Update response payload/buffer address */
        ttdsaXchg->resp.virtAddr      = virtualAddr + respLen;
        TI_DBG5(("ttdsaXchgInit: pos 1\n"));
        ttdsaXchg->resp.phyAddrUpper = phyAddrUpper;
        TI_DBG5(("ttdsaXchgInit: pos 2\n"));
        ttdsaXchg->resp.phyAddrLower = phyAddrLower + respLen;
        TI_DBG5(("ttdsaXchgInit: pos 3\n"));
        ttdsaXchg->resp.length = respLen;
        TI_DBG5(("ttdsaXchgInit: pos 4\n"));

        /* Update SMP response payload/buffer address */
        ttdsaXchg->smpresp.virtAddr      = smpvirtualAddr + smprespLen;
        ttdsaXchg->smpresp.phyAddrUpper = smpphyAddrUpper;
        ttdsaXchg->smpresp.phyAddrLower = smpphyAddrLower + smprespLen;
        ttdsaXchg->smpresp.length = smprespLen;

    }

    /* Reinitialize counters.
     * This must be done at the end
     */
    TD_XCHG_CONTEXT_NO_USED(tiRoot)            = 0;
    TD_XCHG_CONTEXT_NO_FREED(tiRoot)           = 0;
    TD_XCHG_CONTEXT_NO_CMD_RCVD(tiRoot)        = 0;
    TD_XCHG_CONTEXT_NO_START_IO(tiRoot)        = 0;
    TD_XCHG_CONTEXT_NO_SEND_RSP(tiRoot)        = 0;
    TD_XCHG_CONTEXT_NO_IO_COMPLETED(tiRoot)    = 0;

    TI_DBG5(("ttdsaXchgInit: end\n"));
    return agTRUE;
}

osGLOBAL void
ttdsaXchgLinkInit(
        tiRoot_t           *tiRoot,
        ttdsaXchg_t        *ttdsaXchg
)
{
    tdsaRoot_t        *tdsaRoot    = (tdsaRoot_t *)tiRoot->tdData;
    tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    ttdsaTgt_t         *Target = (ttdsaTgt_t *)tdsaAllShared->ttdsaTgt;
    bit32              i;
    bit8               *data;

    TI_DBG5(("ttdsaXchgLinkInit: start\n"));
    TI_DBG5(("ttdsaXchgLinkInit: xchg %p\n",ttdsaXchg));
    TI_DBG5(("ttdsaXchgLinkInit: resp %p\n",ttdsaXchg->resp.virtAddr));
    TI_DBG5(("ttdsaXchgLinkInit: smpresp %p\n",ttdsaXchg->smpresp.virtAddr));

    if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_ACTIVE)
    {
        TI_DBG1(("ttdsaXchgLinkInit: active xchg *****************; wrong\n"));
        return;
    }

    ttdsaXchg->tag = 0xFFFF;
    ttdsaXchg->IORequestBody.agIORequest.sdkData  = agNULL;
    ttdsaXchg->SMPRequestBody.agIORequest.sdkData  = agNULL;
    ttdsaXchg->statusSent        = agFALSE;
    ttdsaXchg->responseSent      = agFALSE;
    ttdsaXchg->readRspCollapsed  = agFALSE;
    ttdsaXchg->wrtRspCollapsed  = agFALSE;
    ttdsaXchg->pTMResp           = agNULL;
    ttdsaXchg->oustandingIos     = 0;
    ttdsaXchg->isAborting        = agFALSE;
    ttdsaXchg->oslayerAborting   = agFALSE;
    ttdsaXchg->isTMRequest       = agFALSE;
    ttdsaXchg->io_found          = agTRUE;
    ttdsaXchg->tiIOToBeAbortedRequest          = agNULL;
    ttdsaXchg->XchgToBeAborted          = agNULL;

    osti_memset((void *)ttdsaXchg->resp.virtAddr, 0, ttdsaXchg->respLen);
    osti_memset((void *)ttdsaXchg->smpresp.virtAddr, 0, ttdsaXchg->smprespLen);

    data = (bit8 *)ttdsaXchg->resp.virtAddr;
    for (i = 0; i< ttdsaXchg->respLen; i++)
    {
        if (data[i] != 0)
        {
            TI_DBG5(("!! ttdsaXchgLinkInit: data[%d] 0x%x\n", i, data[i]));
        }
    }

    ttdsaXchg->resp.length       = 0;

    ttdsaXchg->DeviceData = agNULL;
    TI_DBG5(("ttdsaXchgLinkInit: id %d\n", ttdsaXchg->id));

    TD_XCHG_SET_STATE(ttdsaXchg, TD_XCHG_STATE_INACTIVE);
    tdsaSingleThreadedEnter(tiRoot, TD_TGT_LOCK);
    TDLIST_ENQUEUE_AT_TAIL( &ttdsaXchg->XchgLinks, &Target->ttdsaXchgData.xchgFreeList);
    tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);


    TD_XCHG_CONTEXT_NO_FREED(tiRoot)           = TD_XCHG_CONTEXT_NO_FREED(tiRoot) +1;
    TI_DBG5(("ttdsaXchgLinkInit: end\n"));
    return;
}

/*
   before: ttdsaXchg is in xchgBusyList
   after: ttdsaXchg is in xchgFreeList
 */
osGLOBAL void
ttdsaXchgFreeStruct(
        tiRoot_t           *tiRoot,
        ttdsaXchg_t        *ttdsaXchg
)
{
    tdsaRoot_t        *tdsaRoot    = (tdsaRoot_t *)tiRoot->tdData;
    tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    ttdsaTgt_t        *Target = (ttdsaTgt_t *)tdsaAllShared->ttdsaTgt;
    bit32             i;
    bit8              *data;

    TI_DBG5(("ttdsaXchgFreeStruct: start\n"));
    TI_DBG5(("ttdsaXchgFreeStruct: xchg %p\n",ttdsaXchg));
    TI_DBG5(("ttdsaXchgFreeStruct: resp %p\n",ttdsaXchg->resp.virtAddr));
    TI_DBG5(("ttdsaXchgFreeStruct: smpresp %p\n",ttdsaXchg->smpresp.virtAddr));

    if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_INACTIVE)
    {
        TI_DBG1(("tdsaXchgFreeStruct: INACTIVE xchg *****************, wrong\n"));
        return;
    }

    ttdsaXchg->tag = 0xFFFF;
    ttdsaXchg->IORequestBody.agIORequest.sdkData  = agNULL;
    ttdsaXchg->SMPRequestBody.agIORequest.sdkData  = agNULL;
    ttdsaXchg->statusSent        = agFALSE;
    ttdsaXchg->responseSent      = agFALSE;
    ttdsaXchg->readRspCollapsed  = agFALSE;
    ttdsaXchg->wrtRspCollapsed  = agFALSE;
    ttdsaXchg->pTMResp           = agNULL;
    ttdsaXchg->oustandingIos     = 0;
    ttdsaXchg->isAborting        = agFALSE;
    ttdsaXchg->oslayerAborting   = agFALSE;
    ttdsaXchg->isTMRequest       = agFALSE;
    ttdsaXchg->io_found          = agTRUE;
    ttdsaXchg->tiIOToBeAbortedRequest          = agNULL;
    ttdsaXchg->XchgToBeAborted          = agNULL;

    osti_memset((void *)ttdsaXchg->resp.virtAddr, 0, ttdsaXchg->respLen);
    osti_memset((void *)ttdsaXchg->smpresp.virtAddr, 0, ttdsaXchg->smprespLen);

    data = (bit8 *)ttdsaXchg->resp.virtAddr;
    for (i = 0; i< ttdsaXchg->respLen; i++)
    {
        if (data[i] != 0)
        {
            TI_DBG5(("!! ttdsaXchgFreeStruct: data[%d] 0x%x\n", i, data[i]));
        }
    }

    ttdsaXchg->resp.length       = 0;

    ttdsaXchg->DeviceData = agNULL;
    TI_DBG5(("ttdsaXchgFreeStruct: id %d\n", ttdsaXchg->id));

    tdsaSingleThreadedEnter(tiRoot, TD_TGT_LOCK);
    TD_XCHG_SET_STATE(ttdsaXchg, TD_XCHG_STATE_INACTIVE);
    TDLIST_DEQUEUE_THIS(&ttdsaXchg->XchgLinks);
    TDLIST_ENQUEUE_AT_TAIL( &ttdsaXchg->XchgLinks, &Target->ttdsaXchgData.xchgFreeList);
    tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);

    TD_XCHG_CONTEXT_NO_FREED(tiRoot)           = TD_XCHG_CONTEXT_NO_FREED(tiRoot) +1;
    TI_DBG5(("ttdsaXchgFreeStruct: end\n"));
    return;
}


/*
   before: ttdsaXchg is in xchgFreeList
   after: ttdsaXchg is in xchgBusyList
 */
osGLOBAL ttdsaXchg_t *ttdsaXchgGetStruct(agsaRoot_t *agRoot)
{
    tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
    ttdsaTgt_t             *Target = (ttdsaTgt_t *)osData->ttdsaTgt;
    tdList_t               *Link;
    ttdsaXchg_t            *ttdsaXchg = agNULL;

    TI_DBG3 (("ttdsaXchgGetStruct: enter\n"));

    tdsaSingleThreadedEnter(tiRoot, TD_TGT_LOCK);
    if (TDLIST_EMPTY(&(Target->ttdsaXchgData.xchgFreeList)))
    {
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);
        TI_DBG1(("ttdsaXchgGetStruct: no free ttdsaXchgData\n"));
        //    ttdsaDumpallXchg(tiRoot);
        return agNULL;
    }

    TDLIST_DEQUEUE_FROM_HEAD(&Link, &Target->ttdsaXchgData.xchgFreeList);
    if ( Link == agNULL )
    {
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);
        TI_DBG1(("ttdsaXchgGetStruct: Link NULL: PRBLM \n"));
        return agNULL;
    }

    ttdsaXchg = TDLIST_OBJECT_BASE(ttdsaXchg_t, XchgLinks, Link);

    if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_ACTIVE)
    {
        TI_DBG1(("ttdsaXchgGetStruct: ACTIVE xchg *****************, wrong\n"));
        TDLIST_DEQUEUE_THIS(&ttdsaXchg->XchgLinks);
        TDLIST_ENQUEUE_AT_TAIL(&ttdsaXchg->XchgLinks, &Target->ttdsaXchgData.xchgFreeList);
        TD_XCHG_SET_STATE(ttdsaXchg, TD_XCHG_STATE_INACTIVE);
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);

        return agNULL;
    }

    TDLIST_DEQUEUE_THIS(&ttdsaXchg->XchgLinks);
    TDLIST_ENQUEUE_AT_TAIL(&ttdsaXchg->XchgLinks, &Target->ttdsaXchgData.xchgBusyList);
    TD_XCHG_SET_STATE(ttdsaXchg, TD_XCHG_STATE_ACTIVE);
    tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);

    TD_XCHG_CONTEXT_NO_USED(tiRoot)           = TD_XCHG_CONTEXT_NO_USED(tiRoot) +1;
    TI_DBG5(("ttdsaXchgGetStruct: id %d\n", ttdsaXchg->id));
    return ttdsaXchg;
}

/* for debugging */
osGLOBAL void
ttdsaDumpallXchg(tiRoot_t           *tiRoot)
{
    tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    ttdsaTgt_t        *Target = (ttdsaTgt_t *)tdsaAllShared->ttdsaTgt;
    ttdsaTgt_t        *tmpTarget;
    tdList_t          *XchgList;
#ifdef TD_DEBUG_ENABLE
    ttdsaXchg_t       *ttdsaXchg = agNULL;
#endif

    tmpTarget = Target;

    tdsaSingleThreadedEnter(tiRoot, TD_TGT_LOCK);
    if (TDLIST_EMPTY(&(tmpTarget->ttdsaXchgData.xchgFreeList)))
    {
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);
        TI_DBG1(("ttdsaDumpallXchg: no FREE ttdsaXchgData\n"));
    }
    else
    {
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);
        XchgList = tmpTarget->ttdsaXchgData.xchgFreeList.flink;

        while(XchgList != &(tmpTarget->ttdsaXchgData.xchgFreeList))
        {
#ifdef TD_DEBUG_ENABLE
            ttdsaXchg = TDLIST_OBJECT_BASE(ttdsaXchg_t, XchgLinks, XchgList);
#endif
            TI_DBG1(("ttdsaDumpallXchg: FREE id %d state %d\n", ttdsaXchg->id, TD_XCHG_GET_STATE(ttdsaXchg)));
            XchgList = XchgList->flink;
        }
    }

    tdsaSingleThreadedEnter(tiRoot, TD_TGT_LOCK);
    if (TDLIST_EMPTY(&(tmpTarget->ttdsaXchgData.xchgBusyList)))
    {
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);
        TI_DBG1(("ttdsaDumpallXchg: no BUSY ttdsaXchgData\n"));
    }
    else
    {
        tdsaSingleThreadedLeave(tiRoot, TD_TGT_LOCK);
        XchgList = tmpTarget->ttdsaXchgData.xchgBusyList.flink;

        while(XchgList != &(tmpTarget->ttdsaXchgData.xchgBusyList))
        {
#ifdef TD_DEBUG_ENABLE
            ttdsaXchg = TDLIST_OBJECT_BASE(ttdsaXchg_t, XchgLinks, XchgList);
#endif
            TI_DBG1(("ttdsaDumpallXchg: BUSY id %d state %d\n", ttdsaXchg->id, TD_XCHG_GET_STATE(ttdsaXchg)));
            XchgList = XchgList->flink;
        }
    }


    return;
}


#ifdef PASSTHROUGH

osGLOBAL bit32
tiTGTPassthroughCmndRegister(
        tiRoot_t                        *tiRoot,
        tiPortalContext_t               *tiportalContext,
        tiPassthroughProtocol_t        tiProtocol,
        tiPassthroughSubProtocol_t        tiSubProtocol,
        tiPassthroughFrameType_t        tiFrameType,
        ostiProcessPassthroughCmnd_t    agPasthroughCB
)
{
    tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    ttdsaTgt_t                *Target = (ttdsaTgt_t *)tdsaAllShared->ttdsaTgt;

    TI_DBG1(("tiTGTPassthroughCmndRegister: start\n"));
    /* error checking */
    if (tiProtocol != tiSASATA)
    {
        TI_DBG1(("tiTGTPassthroughCmndRegister: not supported protocol %d\n", tiProtocol));
        return tiError;
    }

    if (tiSubProtocol != tiSSP || tiSubProtocol != tiSTP || tiSubProtocol != tiSMP)
    {
        TI_DBG1(("tiTGTPassthroughCmndRegister: not supported sub protocol %d\n", tiSubProtocol));
        return tiError;
    }


    if (tiFrameType == tiSMPResponse)
    {
        TI_DBG1(("tiTGTPassthroughCmndRegister: SMP response frametype %d\n"));
        Target->PasthroughCB = agPasthroughCB;
    }

    else if (tiFrameType == tiSSPPMC)
    {
        TI_DBG1(("tiTGTPassthroughCmndRegister: RMC response frametype %d\n"));
        Target->PasthroughCB = agPasthroughCB;
    }
    else
    {
        TI_DBG1(("tiTGTPassthroughCmndRegister: not supported frametype %d\n", tiFrameType));
        return tiError;
    }


    return tiSuccess;
}

#endif
