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

**
********************************************************************************/
/*******************************************************************************/
/** \file
 *
 *
 * This file contains ESGL realted functions
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>
#endif

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/freebsd/driver/common/osstring.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdutil.h>

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itddefs.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdglobl.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdxchg.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>

/* no more esgl related functions */
#ifdef REMOVED
/*****************************************************************************
*! \brief  tdsaEsglInit
*
*  Purpose: This function initializes the linked list of ESGL pool
*
*  \param  tiRoot:  Pointer to root data structure.
*
*  \return: None
*
*  \note 
*
*****************************************************************************/
osGLOBAL void 
tdsaEsglInit(
             tiRoot_t *tiRoot
             )
{
  tdsaRoot_t               *tdsaRoot    = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t            *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaEsglAllInfo_t        *pEsglAllInfo = (tdsaEsglAllInfo_t *)&(tdsaAllShared->EsglAllInfo);
  tdsaEsglPagePool_t       *pEsglPagePool;

  bit32 pageno;
  bit32 PagePhysAddrUpper;
  bit32 PagePhysAddrLower;
  bit32 prev_PagePhysAddrLower;
  tdsaEsglPageInfo_t *pEsglPageInfo;
  void *PageVirtAddr;
  bit32 PageSizeInBytes;
  
  /* for memory index requirement */
  agsaRoot_t          agRoot;
  bit32               maxSALocks = 0;
  bit32               usecsPerTick = 0;
  agsaSwConfig_t      SwConfig;
  agsaMemoryRequirement_t memRequirement;
  agsaQueueConfig_t   *QueueConfig;
  bit32                i;

  TI_DBG6(("tdsaEsglInit: start\n"));
  
  tdsaGetSwConfigParams(tiRoot);
  QueueConfig = &tdsaAllShared->QueueConfig;

  for(i=0;i<QueueConfig->numInboundQueues;i++)
  {
    QueueConfig->inboundQueues[i].elementCount = tdsaAllShared->InboundQueueSize;
    QueueConfig->inboundQueues[i].elementSize = tdsaAllShared->InboundQueueEleSize;
    QueueConfig->inboundQueues[i].priority = tdsaAllShared->InboundQueuePriority[i];
    QueueConfig->inboundQueues[i].reserved = 0;
  }
  for(i=0;i<QueueConfig->numOutboundQueues;i++)
  {
    QueueConfig->outboundQueues[i].elementCount = tdsaAllShared->OutboundQueueSize;
    QueueConfig->outboundQueues[i].elementSize = tdsaAllShared->OutboundQueueEleSize;
    QueueConfig->outboundQueues[i].interruptDelay = tdsaAllShared->OutboundQueueInterruptDelay[i]; /* default 0; no interrupt delay */
    QueueConfig->outboundQueues[i].interruptCount = tdsaAllShared->OutboundQueueInterruptCount[i]; /* default 1*/
    QueueConfig->outboundQueues[i].interruptVectorIndex = 0;
  }
  
  /*
    hardcoded Queue numbers
  */
  QueueConfig->sasHwEventQueue = 0;
  QueueConfig->sataNCQErrorEventQueue = 0;
  SwConfig.sizefEventLog1 = HOST_EVENT_LOG_SIZE;
  SwConfig.sizefEventLog2 = HOST_EVENT_LOG_SIZE;
  SwConfig.eventLog1Option = 0;
  SwConfig.eventLog2Option = 0;
  SwConfig.fatalErrorInterrtuptEnable = 1;
  SwConfig.fatalErrorInterruptVector = 1;
  SwConfig.reserved = 0;
   
  
  SwConfig.param3 = (void *)&(tdsaAllShared->QueueConfig);
  /* to find out memRequirement */
  saGetRequirements(&agRoot, &SwConfig, &memRequirement, &usecsPerTick, &maxSALocks);

  /* initializes tdsaEsglAllInfo_t */
  pEsglAllInfo->physAddrUpper = tdsaAllShared->loResource.loLevelMem.mem[memRequirement.count].physAddrUpper;
  pEsglAllInfo->physAddrLower = tdsaAllShared->loResource.loLevelMem.mem[memRequirement.count].physAddrLower;
  pEsglAllInfo->virtPtr       = tdsaAllShared->loResource.loLevelMem.mem[memRequirement.count].virtPtr;
  pEsglAllInfo->NumEsglPages  = tdsaAllShared->loResource.loLevelMem.mem[memRequirement.count].numElements; /*   NUM_ESGL_PAGES;  number of esgl pages; configurable */
  pEsglAllInfo->EsglPageSize  = tdsaAllShared->loResource.loLevelMem.mem[memRequirement.count].singleElementLength; /* sizeof(agsaEsgl_t) */
  pEsglAllInfo->NumFreeEsglPages = pEsglAllInfo->NumEsglPages;
  pEsglPagePool = pEsglAllInfo->EsglPagePool;

  TI_DBG6(("tdsaEsglInit: pEsglPagePool %p\n", pEsglPagePool));
  TI_DBG6(("tdsaEsglInit: tdsaAllShared->loResource.loLevelMem.mem[18].singleElementLength %d\n", tdsaAllShared->loResource.loLevelMem.mem[18].singleElementLength));
  TI_DBG6(("tdsaEsglInit: NumEsglPage %d EsglPageSize %d\n", pEsglAllInfo->NumEsglPages, pEsglAllInfo->EsglPageSize)); /* ?, 128 */
  TI_DBG6(("tdsaEsglInit: NumFreeEsglPages %d\n", pEsglAllInfo->NumFreeEsglPages));  
  /* initialize the linked lists */
  TDLIST_INIT_HDR(&pEsglAllInfo->freelist);

  
  PageVirtAddr      = pEsglAllInfo->virtPtr;
  PagePhysAddrUpper = pEsglAllInfo->physAddrUpper;
  PagePhysAddrLower = pEsglAllInfo->physAddrLower;
  PageSizeInBytes   = pEsglAllInfo->EsglPageSize;

  TI_DBG6(("tdsaEsglInit:  PageSizeInBytes 0x%x\n",  PageSizeInBytes));
  for (pageno = 0 ; pageno < pEsglAllInfo->NumEsglPages ; pageno++)
  {
    pEsglPageInfo = &(pEsglPagePool->EsglPages[pageno]);
    OSSA_WRITE_LE_32(agRoot, pEsglPageInfo, OSSA_OFFSET_OF(pEsglPageInfo, physAddressUpper), PagePhysAddrUpper);
    OSSA_WRITE_LE_32(agRoot, pEsglPageInfo, OSSA_OFFSET_OF(pEsglPageInfo, physAddressLower), PagePhysAddrLower);
    pEsglPageInfo->len = PageSizeInBytes;
    /* for debugging onlye*/
    pEsglPageInfo->id = pageno+123;
    pEsglPageInfo->agEsgl = (agsaEsgl_t *)PageVirtAddr;
    
    /* for debugging only */
    TI_DBG6(("tdsaEsglInit: index %d upper 0x%8x lower 0x%8x PageVirtAddr %p\n", pageno, PagePhysAddrUpper, PagePhysAddrLower, PageVirtAddr));
    
    
    /* updates addresses */
    prev_PagePhysAddrLower = PagePhysAddrLower;
    PagePhysAddrLower += pEsglAllInfo->EsglPageSize;
    /* if lower wraps around, increment upper */
    if (PagePhysAddrLower <= prev_PagePhysAddrLower)
    {
      PagePhysAddrUpper++;
    }
    
    if (pageno == pEsglAllInfo->NumEsglPages - 1) /* last page */
    {
      pEsglPageInfo->agEsgl->descriptor[MAX_ESGL_ENTRIES-1].len = 0;
      /* set bit31 to zero */
      CLEAR_ESGL_EXTEND(pEsglPageInfo->agEsgl->descriptor[MAX_ESGL_ENTRIES-1].extReserved);
    }
    else
    {
      /* first and so on */
      pEsglPageInfo->agEsgl->descriptor[MAX_ESGL_ENTRIES-1].sgLower = PagePhysAddrLower;
      pEsglPageInfo->agEsgl->descriptor[MAX_ESGL_ENTRIES-1].sgUpper = PagePhysAddrUpper;
      pEsglPageInfo->agEsgl->descriptor[MAX_ESGL_ENTRIES-1].len = PageSizeInBytes; /* sizeof (agsaEsgl_t)*/
      /* set bit31 to one */
      SET_ESGL_EXTEND(pEsglPageInfo->agEsgl->descriptor[MAX_ESGL_ENTRIES-1].extReserved);
    }
    
    TDLIST_INIT_ELEMENT(&pEsglPageInfo->tdlist);
    tdsaSingleThreadedEnter(tiRoot, TD_ESGL_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&pEsglPageInfo->tdlist, &pEsglAllInfo->freelist);
    tdsaSingleThreadedLeave(tiRoot, TD_ESGL_LOCK);

    PageVirtAddr = (bit8 *)PageVirtAddr + PageSizeInBytes;
  } /* end for */

  
  
#ifdef TD_INTERNAL_DEBUG /* for debugging only, for keep now */
  for (pageno = 0 ; pageno < pEsglAllInfo->NumEsglPages ; pageno++)
  {
    TI_DBG6(("tdsaEsglInit: index %d EsglPages %p\n", pageno, &pEsglPagePool->EsglPages[pageno]));
    TI_DBG6(("tdsaEsglInit: nextupper 0x%8x nextlower 0x%8x\n", pEsglPagePool->EsglPages[pageno].agEsgl->nextPageUpper, pEsglPagePool->EsglPages[pageno].agEsgl->nextPageLower));
  }
  TI_DBG6(("tdsaEsglInit:  tdsaEsglPageInfo_t size %d 0x%x\n", sizeof(tdsaEsglPageInfo_t), sizeof(tdsaEsglPageInfo_t)));
  TI_DBG6(("tdsaEsglInit: sizeof(SASG_DESCRIPTOR) %d 0x%x\n", sizeof(SASG_DESCRIPTOR), sizeof(SASG_DESCRIPTOR)));
#endif
  
  return;
}


/*****************************************************************************
*! \brief  tdsaGetEsglPages
*
*  Purpose: This function prepares linked list of ESGL pages from
*           the given scatter-gather list.
*
*  \param tiRoot:       Pointer to root data structure.
*  \param EsglListHdr:  pointer to list header where the list needs to be stored.
*  \param ptiSgl:       Pointer to scatter-gather list.
*  \param virtSgl:      virtual pointer to scatter-gather list.
*
*  \return None
*  
*  \note - 
*       1. If we are out of ESGL pages, then no pages will be added to the list
*          pointed to by EsglListHdr. The list should be empty before calling 
*          this function, so that after returning from this function, the 
*          function can check for the emptyness of the list and find out if
*          any pages were added or not.
*
*****************************************************************************/
osGLOBAL void
tdsaGetEsglPages(
                 tiRoot_t *tiRoot,
                 tdList_t *EsglListHdr,
                 tiSgl_t  *ptiSgl,
                 tiSgl_t  *virtSgl
                 )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  tdsaEsglAllInfo_t *pEsglAllInfo = &(tdsaAllShared->EsglAllInfo);
  bit32 numSgElements = ptiSgl->len;
  bit32 numEntriesPerPage = MAX_ESGL_ENTRIES;
  bit32 numPagesRequired = ((numSgElements - 1) / numEntriesPerPage) + 1;
  bit32 i, j;
  tdList_t *tdlist_to_fill;
  tdsaEsglPageInfo_t *page_to_fill;
  tiSgl_t *tmp_tiSgl = (tiSgl_t *)virtSgl;
  agsaSgl_t *pDesc;
  agsaEsgl_t *agEsgl, *PrevagEsgl = agNULL;

  TI_DBG6(("tdsaGetEsglPages: start\n"));
  TI_DBG6(("tdsaGetEsglPages: pEsglPagePool %p\n", pEsglAllInfo->EsglPagePool));
  TI_DBG6(("tdsaGetEsglPages: &(pEsglAllInfo->freelist) %p\n", &pEsglAllInfo->freelist));
  TI_DBG6(("tdsaGetEsglPages: numSgElements %d numEntriesPerPage %d\n", numSgElements, numEntriesPerPage)); /* ?,  10 */
  TI_DBG6(("tdsaGetEsglPages: numPagesRequired %d NumFreeEsglPages %d\n", numPagesRequired, pEsglAllInfo->NumFreeEsglPages)); /* 1, 2 */
  TI_DBG6(("tdsaGetEsglPages: free Pages %d\n", pEsglAllInfo->NumFreeEsglPages));

  if (numPagesRequired > pEsglAllInfo->NumFreeEsglPages)
  {
    TI_DBG1(("tdsaGetEsglPages:don't have enough freepages. required %d free %d\n", numPagesRequired, pEsglAllInfo->NumFreeEsglPages));
    return;
  }
  tdsaSingleThreadedEnter(tiRoot, TD_ESGL_LOCK);
  pEsglAllInfo->NumFreeEsglPages -= numPagesRequired;
  tdsaSingleThreadedLeave(tiRoot, TD_ESGL_LOCK);



#ifdef TD_INTERNAL_DEBUG  /* for debugging only */
  for (i=0; i < 2; i++)
  {
    /* remove one page from freelist */
    tdsaSingleThreadedEnter(tiRoot, TD_ESGL_LOCK);
    TDLIST_DEQUEUE_FROM_HEAD(&tdlist_to_fill, &pEsglAllInfo->freelist);
    tdsaSingleThreadedLeave(tiRoot, TD_ESGL_LOCK);
  
    /* get the pointer to the page from list pointer */
    page_to_fill = TDLIST_OBJECT_BASE(tdsaEsglPageInfo_t, tdlist, tdlist_to_fill);
    /* for debugging */
    TI_DBG6(("tdsaGetEsglPages:page ID %d\n", page_to_fill->id));
    agEsgl = page_to_fill->agEsgl;
    
    pDesc = (SASG_DESCRIPTOR *)agEsgl;
  
    for (j=0; j <numEntriesPerPage; j++)
    {
      TI_DBG6(("tdsaGetEsglPages: lower %d  upper %d\n", pDesc->sgLower, pDesc->sgUpper));
      TI_DBG6(("tdsaGetEsglPages: len %d\n", pDesc->len));
      pDesc++;
    }
    TI_DBG6(("tdsaGetEsglPages: next lower %d next upper %d\n", agEsgl->nextPageLower, agEsgl->nextPageUpper));
    
  }
#endif /* for debugging only  */  
  
  for (i = 0 ; i < numPagesRequired; i++)
  {
    /* remove one page from freelist */
    tdsaSingleThreadedEnter(tiRoot, TD_ESGL_LOCK);
    TDLIST_DEQUEUE_FROM_HEAD(&tdlist_to_fill, &pEsglAllInfo->freelist);
    tdsaSingleThreadedLeave(tiRoot, TD_ESGL_LOCK);
    
    /* get the pointer to the page from list pointer */
    page_to_fill = TDLIST_OBJECT_BASE(tdsaEsglPageInfo_t, tdlist, tdlist_to_fill);
    /* for debugging */
    TI_DBG6(("tdsaGetEsglPages:page ID %d\n", page_to_fill->id));
    
    agEsgl = page_to_fill->agEsgl;
    pDesc = (agsaSgl_t *)agEsgl;
    
    /*
      adjust next page's address in the followings so that
      the last entry must be (0,0,0)
    */
    if (i == numPagesRequired - 1) /* only one page of last page */
    {
      for (j=0; j < numSgElements; j++)
      {
        OSSA_WRITE_LE_32(agRoot, pDesc, OSSA_OFFSET_OF(pDesc, sgLower), tmp_tiSgl->lower);
        OSSA_WRITE_LE_32(agRoot, pDesc, OSSA_OFFSET_OF(pDesc, sgUpper), tmp_tiSgl->upper);
        OSSA_WRITE_LE_32(agRoot, pDesc, OSSA_OFFSET_OF(pDesc, len), tmp_tiSgl->len);
        CLEAR_ESGL_EXTEND(pDesc->extReserved);
        pDesc++;
        tmp_tiSgl++;
      }
      for (j=numSgElements; j < numEntriesPerPage; j++) 
      {
        /* left over(unused) in the page */
        pDesc->sgLower = 0x0;
        pDesc->sgUpper = 0x0;
        pDesc->len = 0x0;
        CLEAR_ESGL_EXTEND(pDesc->extReserved);
        pDesc++;
      }
    }
    else 
    {
      /* in case of muliple pages, first and later, except one page only or last page */
      for (j=0; j <numEntriesPerPage - 1; j++) /* else */
      {
        /* do this till (last - 1) */
        OSSA_WRITE_LE_32(agRoot, pDesc, OSSA_OFFSET_OF(pDesc, sgLower), tmp_tiSgl->lower);
        OSSA_WRITE_LE_32(agRoot, pDesc, OSSA_OFFSET_OF(pDesc, sgUpper), tmp_tiSgl->upper);
        OSSA_WRITE_LE_32(agRoot, pDesc, OSSA_OFFSET_OF(pDesc, len), tmp_tiSgl->len);
        CLEAR_ESGL_EXTEND(pDesc->extReserved);
        pDesc++;
        tmp_tiSgl++;
      }
      numSgElements -= (numEntriesPerPage - 1);
    }
    if (PrevagEsgl != agNULL)
    {
      /* subsequent pages (second or later pages) */
      PrevagEsgl->descriptor[MAX_ESGL_ENTRIES-1].sgLower = page_to_fill->physAddressLower;
      PrevagEsgl->descriptor[MAX_ESGL_ENTRIES-1].sgUpper = page_to_fill->physAddressUpper;
      PrevagEsgl->descriptor[MAX_ESGL_ENTRIES-1].len = numSgElements; 
      /* set bit31 to one */
      SET_ESGL_EXTEND(PrevagEsgl->descriptor[MAX_ESGL_ENTRIES-1].extReserved);
    }
    PrevagEsgl = agEsgl;
    /* put ESGL onto the EsglListHdr */
    tdsaSingleThreadedEnter(tiRoot, TD_ESGL_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(tdlist_to_fill, EsglListHdr);
    tdsaSingleThreadedLeave(tiRoot, TD_ESGL_LOCK);
    
    
  } /* end for */
  return;
}


/*****************************************************************************
*! \brief  tdsaFreeEsglPages
*
*  Purpose: This function frees the ESGL pages pointed to by EsglListHdr
*           and puts them back onto the free list.
*
*  \param  tiRoot:       Pointer to root data structure.
*  \param  EsglListHdr:  pointer to list header where the pages to be freed
*                        are stored.
*
*  \return:     None
*  
*  \note -
*   1. This function removes all the pages from the list until the list 
*      empty and chains them at the end of the free list.
*****************************************************************************/
osGLOBAL void
tdsaFreeEsglPages(
                  tiRoot_t *tiRoot,
                  tdList_t *EsglListHdr
                  )
{
  tdsaRoot_t         *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t      *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaEsglAllInfo_t  *pEsglAllInfo = (tdsaEsglAllInfo_t *)&(tdsaAllShared->EsglAllInfo);
  tdList_t           *tdlist_to_free;

  TI_DBG6(("tdsaFreeEsglPages: start\n"));
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsaFreeEsglPages: tiRoot is NULL\n"));
    return;
  }
  
  if (EsglListHdr == agNULL)
  {
    TI_DBG1(("tdsaFreeEsglPages: EsglListHdr is NULL\n"));
    return;
  }
  
  TI_DBG6(("tdsaFreeEsglPages: EsglListHdr %p\n", EsglListHdr));
  tdsaSingleThreadedEnter(tiRoot, TD_ESGL_LOCK);
  while (TDLIST_NOT_EMPTY(EsglListHdr))
  {
    TDLIST_DEQUEUE_FROM_HEAD(&tdlist_to_free, EsglListHdr);
    TDLIST_ENQUEUE_AT_TAIL(tdlist_to_free, &pEsglAllInfo->freelist);
    pEsglAllInfo->NumFreeEsglPages++;
  }
  tdsaSingleThreadedLeave(tiRoot, TD_ESGL_LOCK);
  TI_DBG6(("tdsaFreeEsglPages: NumFreeEsglPages  %d\n", pEsglAllInfo->NumFreeEsglPages));
  return;  
}


/*****************************************************************************
*! \brief  tdsaGetEsglPagesInfo
*
*  Purpose: This function gets the information about the size of ESGL pages
*           and number pages to be configured.
*
*  \param tiRoot:     Pointer to root data structure.
*  \param pPageSize:  pointer to bit32 where pagesize information is to be
*                     stored
*  \param pNumPages:  Pointer to bit32 where number of pages information is
*                     to be stored
*
*  \return:     None
*
*  \note -
*
*****************************************************************************/
osGLOBAL void
tdsaGetEsglPagesInfo(
                     tiRoot_t *tiRoot, 
                     bit32    *pPageSize,
                     bit32    *pNumPages
                     )
{
  char    *buffer;
  bit32   buffLen;
  bit32   lenRecv = 0;
  char    *pLastUsedChar = agNULL;
  char    globalStr[]     = "Global";
  char    SwParmsStr[]   = "ESGLParms";
  char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  /* default value, defined in tdsatypes.h */
  bit32   NumEsglPages = NUM_ESGL_PAGES;  
  TI_DBG6(("tdsaGetEsglPagesInfo: start \n"));

  /*
    calls ostiGetTransportParam which parses the configuration file to get
    parameters.
  */
  
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);
  
  osti_memset(buffer, 0, buffLen);

  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "NumESGLPg", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    
    NumEsglPages = osti_strtoul(buffer, &pLastUsedChar, 10);
  }
  
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  TI_DBG6(("tdsaGetEsglPagesInfo: esgl page number %d\n",NumEsglPages));
  *pPageSize = ESGL_PAGES_SIZE;/* sizeof(agsaEsgl_t); defined in tdsatypes.h */
  *pNumPages = NumEsglPages; 
  
  return;
}
#endif








