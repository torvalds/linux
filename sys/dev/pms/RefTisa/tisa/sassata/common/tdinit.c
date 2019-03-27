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
 * This file contains the SAS/SATA TD layer initialization functions
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

LOCAL bit32 tdsaGetCardID(tiRoot_t * tiRoot);


bit32 tdCardIDList[TD_MAX_CARD_NUM] = {
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE , 
  TD_CARD_ID_FREE , TD_CARD_ID_FREE
};

/*****************************************************************************
*
* tdsaGetCardID
*
*  Purpose:  
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*
*  Return: 
*   tiSuccess : CardIDString was successfully read
*   tiError   : CardIDString reading failed
*
*****************************************************************************/ 
bit32 tdsaGetCardID(tiRoot_t * tiRoot)
{
  bit32 i;
  bit32 RetVal = 0xFFFFFFFF;

  for (i = 0 ; i < TD_MAX_CARD_NUM ; i++)
  {
    if (tdCardIDList[i] == TD_CARD_ID_FREE)
    {
      tdCardIDList[i] = TD_CARD_ID_ALLOC;
      RetVal = i;
      break;
    }
  }

  return RetVal;

} /* tdsaGetCardID() */

/*****************************************************************************
*
* tdsaFreeCardID
*
*  Purpose:  
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*
*  Return: 
*   tiSuccess : CardIDString was successfully read
*   tiError   : CardIDString reading failed
*
*****************************************************************************/ 
osGLOBAL void 
tdsaFreeCardID(tiRoot_t *tiRoot, bit32 CardID)
{
  OS_ASSERT(CardID < TD_MAX_CARD_NUM, "Invalid CardID\n");

  tdCardIDList[CardID] = TD_CARD_ID_FREE;

  return;

} /* tdFreeCardID() */

/*****************************************************************************
*
* tdsaGetCardIDString
*
*  Purpose:  
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*
*  Return: 
*   tiSuccess : CardIDString was successfully read
*   tiError   : CardIDString reading failed
*
*****************************************************************************/ 
bit32 tdsaGetCardIDString(tiRoot_t *tiRoot)
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  bit32          ret = tiError;
  bit32          thisCardID = tdsaGetCardID(tiRoot);
  char           CardNum[10];
    
  TI_DBG3(("tdsaGetCardIDString: start\n"));

  TI_DBG3(("tdsaGetCardIDString: thisCardID 0x%x\n", thisCardID));
  

  if (thisCardID == 0xFFFFFFFF)
  {
    TI_DBG1(("tdGetCardIDString: No more CardIDs available\n"));
    ret = tiError;
  }
  else
  {
    tdsaAllShared->CardID = thisCardID;
    osti_sprintf(CardNum,"CardNum%d", thisCardID);
    TI_DBG3(("tdsaGetCardIDString: CardNum is %s\n", CardNum));  
    osti_strcpy(tdsaAllShared->CardIDString, CardNum);
    TI_DBG3(("tdsaGetCardIDString: tdsaAllShared->CardIDString is %s\n", tdsaAllShared->CardIDString));    
    ret = tiSuccess;
  
  }  
  return ret;
}
/*****************************************************************************
*! \brief tiCOMGetResource
*
*  Purpose:  This function is called to determine the Transport
*            Dependent Layer internal resource requirement.
*            This function will internally call the initiator specific,
*            target specific and shared TD resource requirement calls.
*
* \param   tiRoot:             Pointer to driver/port instance.
* \param   loResource:         Pointer to low level TSDK resource requirement.
* \param   initiatorResource:  Pointer to initiator functionality memory and
*                              option requirement.
* \param  targetResource:      Pointer to target functionality memory and
*                              option requirement.
* \param  tdSharedMem:         Pointer to cached memory required by the
*                              target/initiator shared functionality.
*
*  \return None
*
*  \note - This function only return the memory requirement in the tiMem_t
*          structure in loResource, initiatorResource, targetResource
*          and tdSharedMem. It does not allocate memory, so the address
*          fields in tiMem_t are not used.
*
*****************************************************************************/
osGLOBAL void
tiCOMGetResource(
                 tiRoot_t              *tiRoot,
                 tiLoLevelResource_t   *loResource,
                 tiInitiatorResource_t *initiatorResource,
                 tiTargetResource_t    *targetResource,
                 tiTdSharedMem_t       *tdSharedMem
                 )
{
  TI_DBG6(("tiCOMGetResource start\n"));
  TI_DBG6(("tiCOMGetResource: loResource %p\n", loResource));
  
  if(loResource != agNULL)
  {
    tdsaLoLevelGetResource(tiRoot, loResource);
  }
  if(tdSharedMem != agNULL)
  {
    tdsaSharedMemCalculate(tiRoot, loResource, tdSharedMem);
  }
  
#ifdef INITIATOR_DRIVER
  /* initiator */
  if(initiatorResource != agNULL)
  {
    itdssGetResource(tiRoot, initiatorResource);
    /* 
     * for the time being set the initiator usecsPerTick
     * same as lolevel usecsPerTick
     */
    if (loResource == agNULL)
    {
      TI_DBG1(("tiCOMGetResource: loResource is NULL, wrong\n"));
      return;
    }
  }
#endif
  
#ifdef TARGET_DRIVER
  /* target */
  if(targetResource != agNULL)
  {
    ttdssGetResource(tiRoot, targetResource);
  }
#endif
  
  return;
}


/*****************************************************************************
*! \brief tiCOMInit
*
*  Purpose:  This function is called to initialize Transport Dependent Layer.
*            This function will internally call the initiator specific,
*            target specific and shared TD initialization calls.
*
*  \param  tiRoot:             Pointer to target driver/port instance.
*  \param  loResource:         Pointer to low level TSDK resource requirement.
*  \param  initiatorResource:  Pointer to initiator functionality memory and
*                              option requirement.
*  \param  targetResource:     Pointer to target functionality memory and
*                              option requirement.
*  \param  tdSharedMem:        Pointer to cached memory required by the
*                              target/initiator shared functionality.
*
*  \return: tiSuccess  - if successful
*           tiError    - if failed
*
*****************************************************************************/
osGLOBAL bit32
tiCOMInit(
           tiRoot_t              *tiRoot,
           tiLoLevelResource_t   *loResource,
           tiInitiatorResource_t *initiatorResource,
           tiTargetResource_t    *targetResource,
           tiTdSharedMem_t       *tdSharedMem )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaPortContext_t  *tdsaPortContext;
  tdsaDeviceData_t   *tdsaDeviceData;
  
#ifdef TD_INT_COALESCE
  tdsaIntCoalesceContext_t *tdsaIntCoalCxt;
#endif
  
#ifdef TD_DISCOVER
  tdsaExpander_t     *tdsaExpander;
#endif
  
  bit32         status = tiSuccess;
  void          *IniAddr = agNULL;
  void          *TgtAddr = agNULL;
  tdsaContext_t *tdsaAllShared;
#if defined(TD_INT_COALESCE) || defined(TD_DISCOVER) || defined(TD_INTERNAL_DEBUG)
  bit32         MaxTargets;
#endif  
#ifdef TD_INTERNAL_DEBUG  /* for debugging only */
  tdsaEsglAllInfo_t  *pEsglAllInfo;
  tdList_t           *tdlist_to_fill;
  tdsaEsglPageInfo_t *page_to_fill;
#endif  
  bit32          i;
#ifdef FDS_DM
  dmSwConfig_t                   dmSwConfig;
  static dmMemoryRequirement_t   dmMemRequirement;
  bit32                          dmUsecsPerTick = 0;
  bit32                          dmMaxNumLocks = 0;
#endif  
 #ifdef FDS_SM
  smSwConfig_t                   smSwConfig;
  static smMemoryRequirement_t   smMemRequirement;
  bit32                          smUsecsPerTick = 0;
  bit32                          smMaxNumLocks = 0;
#endif  
 
  
  /* for memory analysis */
  TI_DBG6(("ticominit: tdsaroot\n"));
  TI_DBG6(("ticominit: tdsaRoot_t %d\n", (int)sizeof(tdsaRoot_t)));
  TI_DBG6(("ticominit: tdsaEsglAllInfo_t %d\n", (int)sizeof(tdsaEsglAllInfo_t)));
  TI_DBG6(("ticominit: portcontext\n"));
  TI_DBG6(("ticominit: tdsaPortContext_t %d\n", (int)sizeof(tdsaPortContext_t)));
  TI_DBG6(("ticominit: device data\n"));
  TI_DBG6(("ticominit: tdsaDeviceData_t  %d\n", (int)sizeof(tdsaDeviceData_t)));
  TI_DBG6(("ticominit: agsaSASDeviceInfo_t  %d\n", (int)sizeof(agsaSASDeviceInfo_t)));
  TI_DBG6(("ticominit: satDeviceData_t  %d\n", (int)sizeof(satDeviceData_t)));
  TI_DBG6(("ticominit: agsaSATAIdentifyData_t  %d\n", (int)sizeof(agsaSATAIdentifyData_t)));
  
  TI_DBG6(("ticominit: IO request body\n"));
  TI_DBG6(("ticominit: tdIORequestBody_t %d\n", (int)sizeof(tdIORequestBody_t)));
  TI_DBG6(("ticominit: tdssIOCompleted_t %d\n", (int)sizeof(tdssIOCompleted_t)));
  TI_DBG6(("ticominit: agsaIORequest_t %d\n", (int)sizeof(agsaIORequest_t)));
  
  TI_DBG6(("ticominit: FOR SAS\n"));
  TI_DBG6(("ticominit: agsaSASRequestBody_t %d\n", (int)sizeof(agsaSASRequestBody_t)));
  TI_DBG6(("ticominit: FOR SATA\n"));
  TI_DBG6(("ticominit: agsaSATAInitiatorRequest_t %d\n", (int)sizeof(agsaSATAInitiatorRequest_t)));
  TI_DBG6(("ticominit: scsiRspSense_t %d\n", (int)sizeof(scsiRspSense_t)));
  TI_DBG6(("ticominit: tiSenseData_t %d\n", (int)sizeof(tiSenseData_t)));
  TI_DBG6(("ticominit: satIOContext_t %d\n", (int)sizeof(satIOContext_t)));
  TI_DBG6(("ticominit: satInternalIo_t %d\n", (int)sizeof(satInternalIo_t)));
  
  
  /*************************************************************************
  * TD SHARED AREA
  *************************************************************************/

  TI_DBG6(("ticominit: start\n"));

  
#if defined(TD_INT_COALESCE) && defined(TD_DISCOVER)

  /* Let's start from the tdsaRoot */
  tdsaRoot = tdSharedMem->tdSharedCachedMem1.virtPtr;
  tdsaPortContext = (tdsaPortContext_t *)((bitptr)tdSharedMem->tdSharedCachedMem1.virtPtr + sizeof(tdsaRoot_t));
  tdsaDeviceData = (tdsaDeviceData_t *)((bitptr)tdsaPortContext + (sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT));

  /* the following fn fills in MaxTargets */
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tiCOMInit: MaxTargets %d\n", MaxTargets));

  tdsaIntCoalCxt   =
    (tdsaIntCoalesceContext_t *)((bitptr)tdsaDeviceData
                                 + (sizeof(tdsaDeviceData_t) * MaxTargets));

  tdsaExpander  =
    (tdsaExpander_t *)((bitptr)tdsaIntCoalCxt
                       + (sizeof(tdsaIntCoalesceContext_t) * TD_MAX_INT_COALESCE));
    

#elif defined(TD_INT_COALESCE)


  
  /* Let's start from the tdsaRoot */
  tdsaRoot = tdSharedMem->tdSharedCachedMem1.virtPtr;
  tdsaPortContext = (tdsaPortContext_t *)((bitptr)tdSharedMem->tdSharedCachedMem1.virtPtr + sizeof(tdsaRoot_t));
  tdsaDeviceData = (tdsaDeviceData_t *)((bitptr)tdsaPortContext + (sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT));

  /* the following fn fills in MaxTargets */
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tiCOMInit: MaxTargets %d\n", MaxTargets));

  tdsaIntCoalCxt   =
    (tdsaIntCoalesceContext_t *)((bitptr)tdsaDeviceData
                                 + (sizeof(tdsaDeviceData_t) * MaxTargets));

  
#elif defined(TD_DISCOVER)

  
  /* Let's start from the tdsaRoot */
  tdsaRoot = tdSharedMem->tdSharedCachedMem1.virtPtr;
  tdsaPortContext = (tdsaPortContext_t *)((bitptr)tdSharedMem->tdSharedCachedMem1.virtPtr + sizeof(tdsaRoot_t));
  tdsaDeviceData = (tdsaDeviceData_t *)((bitptr)tdsaPortContext + (sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT));

  /* the following fn fills in MaxTargets */
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tiCOMInit: MaxTargets %d\n", MaxTargets));

  tdsaExpander  =
    (tdsaExpander_t *)((bitptr)tdsaDeviceData
                      + (sizeof(tdsaDeviceData_t) * MaxTargets));


  
#else

  /* Let's start from the tdsaRoot */
  tdsaRoot = tdSharedMem->tdSharedCachedMem1.virtPtr;
  tdsaPortContext = (tdsaPortContext_t *)((bitptr)tdSharedMem->tdSharedCachedMem1.virtPtr + sizeof(tdsaRoot_t));
  tdsaDeviceData = (tdsaDeviceData_t *)((bitptr)tdsaPortContext + (sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT));

#endif

  TI_DBG6(("tiCOMInit: ******* tdsaRoot %p tdsaPortContext %p tdsaDeviceData %p\n", tdsaRoot, tdsaPortContext, tdsaDeviceData));

  
  tiRoot->tdData    = tdsaRoot;
  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  
  
#ifdef INITIATOR_DRIVER
  /**< Initialize initiator; itdssIni_t itself */
  if(initiatorResource)
  {
    IniAddr = initiatorResource->initiatorMem.tdCachedMem[0].virtPtr;
    tdsaRoot->itdsaIni = IniAddr;
    tdsaRoot->itdsaIni->tdsaAllShared = tdsaAllShared;
    tdsaAllShared->itdsaIni = tdsaRoot->itdsaIni;
  }
#endif
  
#ifdef TARGET_DRIVER
  /**< Initialize target; ttdssTgt_t itself */
  if(targetResource)
  {
    TgtAddr = targetResource->targetMem.tdMem[0].virtPtr;
    tdsaRoot->ttdsaTgt = TgtAddr;
    tdsaRoot->ttdsaTgt->tdsaAllShared = tdsaAllShared;
    tdsaAllShared->ttdsaTgt = tdsaRoot->ttdsaTgt;
  }
#endif /* target driver */
  
  TI_DBG5(("tiCOMInit: IniAddr %p TgtAddr %p\n", IniAddr, TgtAddr));

  TI_DBG3(("tiCOMInit: tdsaRoot %p tdsaAllShared %p \n",tdsaRoot, tdsaAllShared));  

  /**<  Initialize the OS data part of the interrupt context agRoot */
  tdsaAllShared->agRootOsDataForInt.tiRoot     = tiRoot;
  tdsaAllShared->agRootOsDataForInt.tdsaAllShared   = (void *) tdsaAllShared;
  tdsaAllShared->agRootOsDataForInt.itdsaIni      = (void *) IniAddr;
  tdsaAllShared->agRootOsDataForInt.ttdsaTgt      = (void *) TgtAddr;
  /* for sata */
  tdsaAllShared->agRootOsDataForInt.tdstHost = agNULL;
  tdsaAllShared->agRootOsDataForInt.tdstDevice = agNULL;
  
  /* tiInterruptContext is an enum value */
  tdsaAllShared->agRootOsDataForInt.IntContext = tiInterruptContext;
  /* queueId or lockid in TD layer; LL maxlock + 1 since TD uses only one lock */
  
  /* agsaRoot_t */
  tdsaAllShared->agRootInt.osData              = 
    (void *) &(tdsaAllShared->agRootOsDataForInt);
  tdsaAllShared->agRootInt.sdkData              = agNULL;

  /**< Initialize the OS data part of the non-interrupt context agRoot */
  tdsaAllShared->agRootOsDataForNonInt.tiRoot     = tiRoot;
  tdsaAllShared->agRootOsDataForNonInt.tdsaAllShared   = (void *) tdsaAllShared;
  tdsaAllShared->agRootOsDataForNonInt.itdsaIni      = (void *) IniAddr;
  tdsaAllShared->agRootOsDataForNonInt.ttdsaTgt      = (void *) TgtAddr;
  /* for sata */
  tdsaAllShared->agRootOsDataForNonInt.tdstHost = agNULL;
  tdsaAllShared->agRootOsDataForNonInt.tdstDevice = agNULL;
  
  tdsaAllShared->agRootOsDataForNonInt.IntContext = tiNonInterruptContext;
  /* queueId or lockid in TD layer; LL maxlock + 1 since TD uses only one lock */

  /* agsaRoot_t */
  tdsaAllShared->agRootNonInt.osData              = 
    (void *) &(tdsaAllShared->agRootOsDataForNonInt);
  tdsaAllShared->agRootNonInt.sdkData              = agNULL;

  tdsaAllShared->loResource = *loResource;

  tdsaAllShared->PortContextMem = tdsaPortContext;
  tdsaAllShared->DeviceMem = tdsaDeviceData;

  tdsaAllShared->IBQnumber = 0;
  tdsaAllShared->OBQnumber = 0;
    
#ifdef TD_INT_COALESCE
  tdsaAllShared->IntCoalesce = tdsaIntCoalCxt;
#endif

#ifdef TD_DISCOVER
  tdsaAllShared->ExpanderHead = tdsaExpander;
#endif

  tdsaAllShared->MaxNumLocks = loResource->loLevelOption.numOfQueuesPerPort;

  tdsaAllShared->MaxNumOSLocks = loResource->loLevelOption.maxNumOSLocks;
  
#if defined(FDS_DM) && defined(FDS_SM)
  dmGetRequirements(agNULL, 
                    &dmSwConfig, 
                    &dmMemRequirement, 
                    &dmUsecsPerTick, 
                    &dmMaxNumLocks
                    );
  
  tdsaAllShared->MaxNumDMLocks = dmMaxNumLocks;
  TI_DBG2(("tiCOMInit: DM MaxNumDMLocks 0x%x\n", tdsaAllShared->MaxNumDMLocks));
  
  smGetRequirements(agNULL, 
                    &smSwConfig, 
                    &smMemRequirement, 
                    &smUsecsPerTick, 
                    &smMaxNumLocks
                    );
  
  tdsaAllShared->MaxNumSMLocks = smMaxNumLocks;
  TI_DBG2(("tiCOMInit: SM MaxNumSMLocks 0x%x\n", tdsaAllShared->MaxNumSMLocks));
  
  tdsaAllShared->MaxNumLLLocks = tdsaAllShared->MaxNumLocks - TD_MAX_LOCKS - tdsaAllShared->MaxNumDMLocks - tdsaAllShared->MaxNumSMLocks;
  TI_DBG2(("tiCOMInit: LL MaxNumLLLocks 0x%x\n", tdsaAllShared->MaxNumLLLocks));

#elif defined(FDS_DM)
  dmGetRequirements(agNULL, 
                    &dmSwConfig, 
                    &dmMemRequirement, 
                    &dmUsecsPerTick, 
                    &dmMaxNumLocks
                    );
  
  tdsaAllShared->MaxNumDMLocks = dmMaxNumLocks;
  TI_DBG2(("tiCOMInit: DM MaxNumDMLocks 0x%x\n", tdsaAllShared->MaxNumDMLocks));

  tdsaAllShared->MaxNumLLLocks = tdsaAllShared->MaxNumLocks - TD_MAX_LOCKS - tdsaAllShared->MaxNumDMLocks;
  TI_DBG2(("tiCOMInit: LL MaxNumLLLocks 0x%x\n", tdsaAllShared->MaxNumLLLocks));
#elif defined(FDS_SM)
  smGetRequirements(agNULL, 
                    &smSwConfig, 
                    &smMemRequirement, 
                    &smUsecsPerTick, 
                    &smMaxNumLocks
                    );
  
  tdsaAllShared->MaxNumSMLocks = smMaxNumLocks;
  TI_DBG2(("tiCOMInit: SM MaxNumSMLocks 0x%x\n", tdsaAllShared->MaxNumSMLocks));

  tdsaAllShared->MaxNumLLLocks = tdsaAllShared->MaxNumLocks - TD_MAX_LOCKS - tdsaAllShared->MaxNumSMLocks;
  TI_DBG2(("tiCOMInit: LL MaxNumLLLocks 0x%x\n", tdsaAllShared->MaxNumLLLocks));
#else
  tdsaAllShared->MaxNumLLLocks = tdsaAllShared->MaxNumLocks - TD_MAX_LOCKS;
  TI_DBG2(("tiCOMInit: LL MaxNumLLLocks 0x%x\n", tdsaAllShared->MaxNumLLLocks));
#endif
    
#ifdef TBD
  tdsaAllShared->MaxNumLLLocks = loResource->loLevelOption.numOfQueuesPerPort - TD_MAX_LOCKS;
#endif
  
  tdsaAllShared->resetCount = 0;

  /* used for saHwEventAck() and ossaDeregisterDeviceHandleCB() */
//  tdsaAllShared->EventValid = agFALSE;
  for(i=0; i<TD_MAX_NUM_PHYS; i++)
  {
    tdsaAllShared->eventSource[i].EventValid =  agFALSE;
    tdsaAllShared->eventSource[i].Source.agPortContext =  agNULL;
    tdsaAllShared->eventSource[i].Source.event =  0;
    /* phy ID */
    tdsaAllShared->eventSource[i].Source.param =  0xFF;
  } 


#ifdef TD_INTERNAL_DEBUG  /* for debugging only */
  pEsglAllInfo = (tdsaEsglAllInfo_t *)&(tdsaAllShared->EsglAllInfo);
#endif

  /* initialize CardIDString */
  osti_strcpy(tdsaAllShared->CardIDString,"");


#ifdef FDS_DM
  tdsaAllShared->dmRoot.tdData = tdsaRoot;
#endif    
	    
#ifdef FDS_SM
  tdsaAllShared->smRoot.tdData = tdsaRoot;
#endif    
  
  /* get card ID */
  if (tdsaGetCardIDString(tiRoot) == tiError)
  {
    TI_DBG1(("tdsaGetCardIDString() failed\n"));
    return tiError;
  }
    
  /**< initializes jumptable */
  tdsaJumpTableInit(tiRoot);
  
  /**< initializes tdsaPortStartInfo_s including flags */
  tdssInitSASPortStartInfo(tiRoot);
  
  /* resets all the relevant flags */
  tdsaResetComMemFlags(tiRoot);

  /**< initializes timers */
  tdsaInitTimers(tiRoot);

  TI_DBG6(("ticominit: ******* before tdsaRoot %p tdsaPortContext %p tdsaDeviceData %p\n", tdsaRoot, tdsaPortContext, tdsaDeviceData));
  

  /**< initializes tdsaPortContext_t */
  tdsaPortContextInit(tiRoot);

  /**< initializes devicelist in tdsaPortContext_t */
  tdsaDeviceDataInit(tiRoot);
  
#ifdef TD_INT_COALESCE
  tdsaIntCoalCxtInit(tiRoot);
#endif
  
#ifdef TD_DISCOVER
  tdsaExpanderInit(tiRoot);
#endif  

  tdsaQueueConfigInit(tiRoot);  
  
#ifdef TD_INTERNAL_DEBUG /* for debugging only */
  TI_DBG6(("ticominit: temp 1\n"));
  TDLIST_DEQUEUE_FROM_HEAD(&tdlist_to_fill, &pEsglAllInfo->freelist);
  /* get the pointer to the page from list pointer */
  page_to_fill = TDLIST_OBJECT_BASE(tdsaEsglPageInfo_t, tdlist, tdlist_to_fill);
  TI_DBG6(("ticominit: pageinfo ID %d\n", page_to_fill->id));
  /* this does not work */
  TDLIST_ENQUEUE_AT_HEAD(tdlist_to_fill, &pEsglAllInfo->freelist); 

  TI_DBG6(("ticominit: devide\n"));
  TDLIST_DEQUEUE_FROM_HEAD(&tdlist_to_fill, &pEsglAllInfo->freelist);
  /* get the pointer to the page from list pointer */
  page_to_fill = TDLIST_OBJECT_BASE(tdsaEsglPageInfo_t, tdlist, tdlist_to_fill);
  TDINIT_PRINT("ticominit: second pageinfo ID %d\n", page_to_fill->id);

  TDLIST_ENQUEUE_AT_HEAD(tdlist_to_fill, &pEsglAllInfo->freelist);
  
#endif 
  

#ifdef INITIATOR_DRIVER
  if(initiatorResource != agNULL)
  {
    tdsaAllShared->currentOperation |= TD_OPERATION_INITIATOR;
    TI_DBG5(("tiCOMInit: calling itdssInit\n"));
    status = itdssInit(tiRoot, initiatorResource, tdSharedMem);
    
    if(status != tiSuccess)
    {
      TI_DBG1(("tiCOMInit: itdInit FAILED\n"));
      return status;
    }
  }
#endif

#ifdef TARGET_DRIVER
  if(targetResource != agNULL)
  {
    tdsaAllShared->currentOperation |= TD_OPERATION_TARGET;
    TI_DBG5 (("tiCOMInit: calling ttdssInit\n"));
    status = ttdssInit(tiRoot, targetResource, tdSharedMem);

    if(status != tiSuccess)
    {
      TI_DBG1(("tiCOMInit: ttdInit FAILED\n"));
      return status;
    }
  }
#endif
  
  return status;
}

/*****************************************************************************
*! \brief tdsaLoLevelGetResource
*
*  Purpose:  This function is called to determine the Transport 
*            Dependent Layer internal resource requirement used by the 
*            lower layer TSDK.
*
*  \param  tiRoot:             Pointer to driver/port instance.
*  \param  loResource:         Pointer to low level TSDK resource requirement.
*
*  \return: None
*
*  \note -  currenlty mem[0] - mem[18] are being used
*
*****************************************************************************/
/*
  this calls ostiGetTransportParam which parses the configuration file to get
  parameters.
*/
osGLOBAL void
tdsaLoLevelGetResource(
                       tiRoot_t              * tiRoot, 
                       tiLoLevelResource_t   * loResource)
{
  agsaRoot_t          agRoot;
  bit32               usecsPerTick = 0;
  agsaSwConfig_t      SwConfig;
  static agsaQueueConfig_t   QueueConfig;
  static agsaMemoryRequirement_t memRequirement;
  bit32  maxQueueSets = 0;
  bit32  maxNumOSLocks = 0;
  bit32  i;
  
  char    *buffer;
  bit32   buffLen;
  bit32   lenRecv = 0;
  char    *pLastUsedChar = agNULL;
  char    globalStr[]     = "Global";
  char    iniParmsStr[]   = "InitiatorParms";
  char    SwParmsStr[]   = "SWParms";
  char    OBQueueProps[] = "OBQueueProps";
  char    IBQueueProps[] = "IBQueueProps";
  
  static char   IBQueueSize[30];
  static char   OBQueueSize[30];
  static char   IBQueueEleSize[30];
  static char   OBQueueEleSize[30];
    
  static char    OBQueueInterruptCount[30]; 
  static char    OBQueueInterruptDelay[30]; 
  static char    OBQueueInterruptEnable[30]; 
  static char    IBQueuePriority[30];

      
  static char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  static bit32   InboundQueueSize[AGSA_MAX_OUTBOUND_Q];
  static bit32   OutboundQueueSize[AGSA_MAX_OUTBOUND_Q];
  static bit32   InboundQueueEleSize[AGSA_MAX_OUTBOUND_Q];
  static bit32   OutboundQueueEleSize[AGSA_MAX_OUTBOUND_Q];
  static bit32   InboundQueuePriority[AGSA_MAX_INBOUND_Q];
  static bit32   OutboundQueueInterruptDelay[AGSA_MAX_OUTBOUND_Q];
  static bit32   OutboundQueueInterruptCount[AGSA_MAX_OUTBOUND_Q];
  static bit32   OutboundQueueInterruptEnable[AGSA_MAX_OUTBOUND_Q];
  static bit32 cardID = 0;
  char    CardNum[10];
  
#ifdef FDS_DM
  dmRoot_t                     dmRoot;
  dmSwConfig_t                 dmSwConfig;
  static dmMemoryRequirement_t dmMemRequirement;
  bit32                        dmUsecsPerTick = 0;
  bit32                        dmMaxNumLocks = 0;
#endif  

#ifdef FDS_SM
  smRoot_t                     smRoot;
  smSwConfig_t                 smSwConfig;
  static smMemoryRequirement_t smMemRequirement;
  bit32                        smUsecsPerTick = 0;
  bit32                        smMaxNumLocks = 0;
#endif  
    
  TI_DBG1(("tdsaLoLevelGetResource: start \n"));
  TI_DBG6(("tdsaLoLevelGetResource: loResource %p\n", loResource));

  osti_memset(&agRoot, 0, sizeof(agsaRoot_t));
  osti_memset(&QueueConfig, 0, sizeof(QueueConfig));
  osti_memset(&memRequirement, 0, sizeof(memRequirement));
  osti_memset(InboundQueueSize, 0, sizeof(InboundQueueSize));
  osti_memset(OutboundQueueSize, 0, sizeof(OutboundQueueSize));
  osti_memset(InboundQueueEleSize, 0, sizeof(InboundQueueEleSize));
  osti_memset(OutboundQueueEleSize, 0, sizeof(OutboundQueueEleSize));

  memRequirement.count = 0;

  /* default values which are overwritten later */
  /* The followings are default values */
  SwConfig.maxActiveIOs = DEFAULT_MAX_ACTIVE_IOS;
  SwConfig.numDevHandles = DEFAULT_MAX_DEV;
  SwConfig.smpReqTimeout = DEFAULT_SMP_TIMEOUT; /* DEFAULT_VALUE; */
  SwConfig.numberOfEventRegClients = DEFAULT_NUM_REG_CLIENTS; 
  SwConfig.sizefEventLog1 = HOST_EVENT_LOG_SIZE; /* in KBytes */
  SwConfig.sizefEventLog2 = HOST_EVENT_LOG_SIZE; /* in KBytes */
  SwConfig.eventLog1Option = DEFAULT_EVENT_LOG_OPTION;
  SwConfig.eventLog2Option = DEFAULT_EVENT_LOG_OPTION;
  SwConfig.fatalErrorInterruptEnable = 1;
  SwConfig.fatalErrorInterruptVector = 0; /* Was 1 */
  SwConfig.hostDirectAccessSupport = 0;
  SwConfig.hostDirectAccessMode = 0;
  SwConfig.FWConfig = 0;
  SwConfig.enableDIF = agFALSE;
  SwConfig.enableEncryption = agFALSE;

#ifdef SA_CONFIG_MDFD_REGISTRY
  SwConfig.disableMDF = agFALSE;
#endif /*SA_CONFIG_MDFD_REGISTRY*/

#if defined(SALLSDK_DEBUG)  
  SwConfig.sallDebugLevel = 1; /* DEFAULT_VALUE; */
#endif


#ifdef SA_ENABLE_PCI_TRIGGER
  SwConfig.PCI_trigger = 0; /* DEFAULT_VALUE; */
 #endif /* SA_ENABLE_PCI_TRIGGER */
 
#ifdef FDS_DM
  /* defaults */
  dmMemRequirement.count = 0;
  dmSwConfig.numDevHandles = DEFAULT_MAX_DEV;
#ifdef DM_DEBUG
  dmSwConfig.DMDebugLevel = 1;
#endif  
#endif
  
#ifdef FDS_SM
  /* defaults */
  smMemRequirement.count = 0;
  smSwConfig.maxActiveIOs = DEFAULT_MAX_ACTIVE_IOS;
  smSwConfig.numDevHandles = DEFAULT_MAX_DEV;
#ifdef SM_DEBUG
  smSwConfig.SMDebugLevel = 1;
#endif  
#endif
  
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);

  /* param3 points to QueueConfig; tdsaAllShared does not exit at this point yet */
  SwConfig.param3 = (void *)&QueueConfig;

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
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
      SwConfig.numDevHandles = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.numDevHandles = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
   TI_DBG2(("tdsaLoLevelGetResource: MaxTargets %d\n",  SwConfig.numDevHandles));
  }
  
   
  /*                                                              
   * read the NumInboundQueue parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  QueueConfig.numInboundQueues = DEFAULT_NUM_INBOUND_QUEUE;  /* default 1 Inbound queue */

  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "NumInboundQueues", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      QueueConfig.numInboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      QueueConfig.numInboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
    }

    if (QueueConfig.numInboundQueues > AGSA_MAX_INBOUND_Q)
    {
      QueueConfig.numInboundQueues = AGSA_MAX_INBOUND_Q;
    }
  }

  /*                                                              
   * read the NumOutboundQueue parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  QueueConfig.numOutboundQueues = DEFAULT_NUM_OUTBOUND_QUEUE;  /* default 1 Outbound queue */

  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "NumOutboundQueues", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      QueueConfig.numOutboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      QueueConfig.numOutboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
    }

    if (QueueConfig.numOutboundQueues > AGSA_MAX_OUTBOUND_Q)
    {
      QueueConfig.numOutboundQueues = AGSA_MAX_OUTBOUND_Q;
    }
  }
       
  /*                                                              
   * read the MaxActiveIO parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "MaxActiveIO", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.maxActiveIOs = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.maxActiveIOs = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }


  
  /*                                                              
   * read the SMPTO parameter (SMP Timeout)
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "SMPTO", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.smpReqTimeout = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.smpReqTimeout = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  
  /*                                                              
   * read the NumRegClients parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "NumRegClients", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.numberOfEventRegClients = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.numberOfEventRegClients = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

#if defined(SALLSDK_DEBUG)  
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "LLDebugLevel", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.sallDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.sallDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
#endif  


#if defined(DM_DEBUG)  
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "DMDebugLevel", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      dmSwConfig.DMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      dmSwConfig.DMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
#endif  

#if defined(SM_DEBUG)  
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "SMDebugLevel", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      smSwConfig.SMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      smSwConfig.SMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
#endif  
        
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig.numInboundQueues;i++)
  {
    osti_sprintf(IBQueueSize,"IBQueueNumElements%d", i);
    osti_sprintf(IBQueueEleSize,"IBQueueElementSize%d", i);
    osti_sprintf(IBQueuePriority,"IBQueuePriority%d", i);
    
    
    
    /*
     * read the IBQueueSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    InboundQueueSize[i] = DEFAULT_INBOUND_QUEUE_SIZE;  /* default 256 Inbound queue size */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             IBQueueSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d IB queue size %d\n", i, InboundQueueSize[i]));        
      }
    }


    /*
     * read the IBQueueEleSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    InboundQueueEleSize[i] = DEFAULT_INBOUND_QUEUE_ELE_SIZE;  /* default 128 Inbound queue element */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             IBQueueEleSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d IB queue ele size %d\n", i, InboundQueueEleSize[i]));        
      }
    }
   
    /*
     * read the IBQueuePriority
     */
  
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    InboundQueuePriority[i] = DEFAULT_INBOUND_QUEUE_PRIORITY; /* default 0 Inbound queue priority */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             IBQueuePriority, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d priority %d\n", i, InboundQueuePriority[i]));        
      }
    }
      
    /**********************************************/            
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
  }/* end of loop */
    

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig.numOutboundQueues;i++)
  {
    osti_sprintf(OBQueueSize,"OBQueueNumElements%d", i);
    osti_sprintf(OBQueueEleSize,"OBQueueElementSize%d", i);
    osti_sprintf(OBQueueInterruptDelay,"OBQueueInterruptDelay%d", i);
    osti_sprintf(OBQueueInterruptCount,"OBQueueInterruptCount%d", i);
    osti_sprintf(OBQueueInterruptEnable,"OBQueueInterruptEnable%d", i);

    /*
     * read the OBQueueSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    OutboundQueueSize[i] = DEFAULT_OUTBOUND_QUEUE_SIZE;  /* default 256 Outbound queue size */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d OB queue size %d\n", i, OutboundQueueSize[i]));        
      }
    }


    /*
     * read the OBQueueEleSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    OutboundQueueEleSize[i] = DEFAULT_OUTBOUND_QUEUE_ELE_SIZE;  /* default 128 Outbound queue element */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueEleSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d OB queue ele size %d\n", i, OutboundQueueEleSize[i]));        
      }
    }

    /*
     * read the OBQueueInterruptDelay
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    OutboundQueueInterruptDelay[i] = DEFAULT_OUTBOUND_QUEUE_INTERRUPT_DELAY;  /* default 1 Outbound interrupt delay */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueInterruptDelay, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d interrupt delay %d\n", i, OutboundQueueInterruptDelay[i]));        
      }
    }
  
    /*
     * read the OBQueueInterruptCount
     */
  
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    OutboundQueueInterruptCount[i] = DEFAULT_OUTBOUND_QUEUE_INTERRUPT_COUNT;  /* default 1 Outbound interrupt count */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueInterruptCount, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d interrupt count %d\n", i, OutboundQueueInterruptCount[i]));        
      }
    }
    
    /*
     * read the OBQueueInterruptEnable
     */
     
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
    
    OutboundQueueInterruptEnable[i] = DEFAULT_OUTBOUND_INTERRUPT_ENABLE;  /* default 1 Outbound interrupt is enabled */
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueInterruptEnable, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
#ifdef SPC_POLLINGMODE
        OutboundQueueInterruptEnable[i] = 0;
#endif /* SPC_POLLINGMODE */

      }
      else
      {
        OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
#ifdef SPC_POLLINGMODE
        OutboundQueueInterruptEnable[i] = 0;
#endif /* SPC_POLLINGMODE */
      }
    TI_DBG2(("tdsaLoLevelGetResource: queue number %d interrupt enable %d\n", i, OutboundQueueInterruptEnable[i]));        
    }
    
    /**********************************************/            
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

  }/* end of loop */   
      
      
      
  /************************************************************
   * READ CARD Specific
  */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig.numInboundQueues;i++)
  {
    osti_sprintf(CardNum,"CardNum%d", cardID);  
    osti_sprintf(IBQueueSize,"IBQueueNumElements%d", i);
    osti_sprintf(IBQueueEleSize,"IBQueueElementSize%d", i);
    osti_sprintf(IBQueuePriority,"IBQueuePriority%d", i);

    /*
     * read the IBQueueSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             IBQueueSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d IB queue size %d\n", i, InboundQueueSize[i]));        
      }
    }


    /*
     * read the IBQueueEleSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             IBQueueEleSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d IB queue size %d\n", i, InboundQueueEleSize[i]));        
      }
    }

    /*
     * read the IBQueuePriority
     */
  
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             IBQueuePriority, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: card number %d queue number %d priority %d\n", cardID, i, InboundQueuePriority[i]));        
      }
    }
      
    /**********************************************/            
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
  }/* end of loop */
          

                              
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig.numOutboundQueues;i++)
  {
    osti_sprintf(CardNum,"CardNum%d", cardID);  
    osti_sprintf(OBQueueSize,"OBQueueNumElements%d", i);
    osti_sprintf(OBQueueEleSize,"OBQueueElementSize%d", i);
    osti_sprintf(OBQueueInterruptDelay,"OBQueueInterruptDelay%d", i);
    osti_sprintf(OBQueueInterruptCount,"OBQueueInterruptCount%d", i);
    osti_sprintf(OBQueueInterruptEnable,"OBQueueInterruptEnable%d", i);
    
    /*
     * read the OBQueueSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d OB queue size %d\n", i, OutboundQueueSize[i]));        
      }
    }


    /*
     * read the OBQueueEleSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
	
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueEleSize, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: queue number %d OB queue ele size %d\n", i, OutboundQueueEleSize[i]));        
      }
    }

    /*
     * read the OBQueueInterruptDelay
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
  
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueInterruptDelay, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: card number %d queue number %d interrupt delay %d\n", cardID, i, OutboundQueueInterruptDelay[i]));        
      }
    }
  
    /*
     * read the OBQueueInterruptCount
     */
  
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueInterruptCount, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaLoLevelGetResource: card number %d queue number %d interrupt count %d\n", cardID, i, OutboundQueueInterruptCount[i]));        
      }
    }
    
    /*
     * read the OBQueueInterruptEnable
     */
  
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
    if ((ostiGetTransportParam(
                             tiRoot, 
                             CardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             OBQueueInterruptEnable, /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      { 
        OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
#ifdef SPC_POLLINGMODE
        OutboundQueueInterruptEnable[i] = 0;
#endif /* SPC_POLLINGMODE */

      }
      else
      {
        OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
#ifdef SPC_POLLINGMODE
        OutboundQueueInterruptEnable[i] = 0;
#endif /* SPC_POLLINGMODE */
      }
      TI_DBG2(("tdsaLoLevelGetResource: card number %d queue number %d interrupt count %d\n", cardID, i, OutboundQueueInterruptEnable[i]));        
    }
    
    
    /**********************************************/            
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

  }/* end of loop */   
                                               
                                                                                                                                                                                                                                                                                                 
  TI_DBG6(("tdsaLoLevelGetResource: \n"));
  tdsaPrintSwConfig(&SwConfig);

  /* fills in queue related parameters */
  for (i=0;i<QueueConfig.numInboundQueues;i++)
  {
    QueueConfig.inboundQueues[i].elementCount = InboundQueueSize[i];
    QueueConfig.inboundQueues[i].elementSize = InboundQueueEleSize[i];
    QueueConfig.inboundQueues[i].priority = InboundQueuePriority[i];    
  }
  for (i=0;i<QueueConfig.numOutboundQueues;i++)
  {
    QueueConfig.outboundQueues[i].elementCount = OutboundQueueSize[i];
    QueueConfig.outboundQueues[i].elementSize = OutboundQueueEleSize[i]; 
    QueueConfig.outboundQueues[i].interruptDelay = OutboundQueueInterruptDelay[i]; 
    QueueConfig.outboundQueues[i].interruptCount = OutboundQueueInterruptCount[i]; 
    QueueConfig.outboundQueues[i].interruptEnable = OutboundQueueInterruptEnable[i]; 
  }
  
  
  /* process event log related parameters */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "EventLogSize1", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.sizefEventLog1 = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.sizefEventLog1 = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "EventLogOption1", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.eventLog1Option = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.eventLog1Option = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "EventLogSize2", /* valueName */ /* size in K Dwords  */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.sizefEventLog2 = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.sizefEventLog2 = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "EventLogOption2", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.eventLog2Option = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.eventLog2Option = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /* end of event log related parameters */

  /*
    HDA parameters
  */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "HDASupport", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.hostDirectAccessSupport = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.hostDirectAccessSupport = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /***********************************************************************/
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "HDAMode", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.hostDirectAccessMode = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.hostDirectAccessMode = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /***********************************************************************/
  /* the end of HDA parameters */


  /* FW configuration */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "FWConfig", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.FWConfig = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.FWConfig = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /* The end of FW configuration */



#ifdef SA_ENABLE_TRACE_FUNCTIONS

 TI_DBG2(("tdsaLoLevelGetResource:  SA_ENABLE_TRACE_FUNCTIONS\n"));

/*
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
*/

  SwConfig.TraceBufferSize = 0;
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "TraceBufferSize", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig.TraceBufferSize = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig.TraceBufferSize = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaLoLevelGetResource: SwConfig.TraceBufferSize %d\n",SwConfig.TraceBufferSize));
  }

#endif /*# SA_ENABLE_TRACE_FUNCTIONS */
      
  SwConfig.mpiContextTable = agNULL;
  SwConfig.mpiContextTablelen = 0;
               
  /* default */
  for (i=0;i<8;i++)
  {
    QueueConfig.sasHwEventQueue[i] = 0;
    QueueConfig.sataNCQErrorEventQueue[i] = 0;
  }
  
#ifdef TARGET_DRIVER
  for (i=0;i<8;i++)
  {
    QueueConfig.tgtITNexusEventQueue[i] = 0;
    QueueConfig.tgtSSPEventQueue[i] = 0;
    QueueConfig.tgtSMPEventQueue[i] = 0;
  }
#endif
    
  QueueConfig.iqNormalPriorityProcessingDepth = 0;
  QueueConfig.iqHighPriorityProcessingDepth = 0;
  QueueConfig.generalEventQueue = 0;
  
  /* 
   * can agRoot be agNULL below? Yes. 
   * saGetRequirements(agRoot, IN, OUT, OUT, OUT); 
   */
  saGetRequirements(&agRoot, 
                    &SwConfig, 
                    &memRequirement, 
                    &usecsPerTick, 
                    &maxQueueSets
                    );
#ifdef FDS_DM
  dmGetRequirements(&dmRoot, 
                    &dmSwConfig, 
                    &dmMemRequirement, 
                    &dmUsecsPerTick, 
                    &dmMaxNumLocks
                    );


#endif

#ifdef FDS_SM
  smGetRequirements(
                    &smRoot,
                    &smSwConfig,
                    &smMemRequirement,
                    &smUsecsPerTick,
                    &smMaxNumLocks
                   );

#endif

 /* initialization */
 maxNumOSLocks = loResource->loLevelOption.maxNumOSLocks;
 /*
   MAX_LL_LAYER_MEM_DESCRIPTORS is 24. see tidefs.h and tiLoLevelMem_t
   in titypes.h
 */
#if defined (FDS_DM) && defined (FDS_SM)
  /* for LL */
  TI_DBG1(("tdsaLoLevelGetResource:MAX_LL_LAYER_MEM_DESCRIPTORS %d\n", MAX_LL_LAYER_MEM_DESCRIPTORS)); 
  for(i=0;i<MAX_LL_LAYER_MEM_DESCRIPTORS;i++)
  {
    loResource->loLevelMem.mem[i].numElements           = 0;
    loResource->loLevelMem.mem[i].totalLength           = 0;
    loResource->loLevelMem.mem[i].singleElementLength   = 0;
    loResource->loLevelMem.mem[i].alignment             = 0;
    loResource->loLevelMem.mem[i].type                  = 0;
    loResource->loLevelMem.mem[i].reserved              = 0;
    loResource->loLevelMem.mem[i].virtPtr               = agNULL;
    loResource->loLevelMem.mem[i].osHandle              = agNULL;
    loResource->loLevelMem.mem[i].physAddrUpper         = 0;
    loResource->loLevelMem.mem[i].physAddrLower         = 0;
  }

  TI_DBG1(("tdsaLoLevelGetResource:memRequirement.count %d\n", memRequirement.count)); 
  /* using the returned value from saGetRequirements */
  for (i=0;i< memRequirement.count;i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = memRequirement.agMemory[i].numElements;
    loResource->loLevelMem.mem[i].totalLength = memRequirement.agMemory[i].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = memRequirement.agMemory[i].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = memRequirement.agMemory[i].alignment;
    TI_DBG2(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }

  /* for DM */
  TI_DBG1(("tdsaLoLevelGetResource:dmMemRequirement.count %d\n", dmMemRequirement.count)); 
  /* using the returned value from dmGetRequirements */
  for (i=memRequirement.count;i< (memRequirement.count + dmMemRequirement.count);i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = dmMemRequirement.dmMemory[i-memRequirement.count].numElements;
    loResource->loLevelMem.mem[i].totalLength = dmMemRequirement.dmMemory[i-memRequirement.count].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = dmMemRequirement.dmMemory[i-memRequirement.count].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = dmMemRequirement.dmMemory[i-memRequirement.count].alignment;
    TI_DBG2(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == dmMemRequirement.dmMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == dmMemRequirement.dmMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == dmMemRequirement.dmMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }

  /* for SM */
  TI_DBG1(("tdsaLoLevelGetResource:smMemRequirement.count %d\n", smMemRequirement.count)); 
  /* using the returned value from dmGetRequirements */
  for (i=(memRequirement.count + dmMemRequirement.count);i< (memRequirement.count + dmMemRequirement.count + smMemRequirement.count);i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].numElements;
    loResource->loLevelMem.mem[i].totalLength = smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].alignment;
    TI_DBG2(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == smMemRequirement.smMemory[i-memRequirement.count-dmMemRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }

  /* sets the low level options */
  loResource->loLevelOption.usecsPerTick       = MIN(MIN(usecsPerTick, dmUsecsPerTick), smUsecsPerTick);
  loResource->loLevelOption.numOfQueuesPerPort = maxQueueSets + dmMaxNumLocks + smMaxNumLocks + TD_MAX_LOCKS + maxNumOSLocks;
  loResource->loLevelOption.mutexLockUsage     = tiOneMutexLockPerQueue;
  /* no more ESGL */
  loResource->loLevelMem.count = memRequirement.count + dmMemRequirement.count + smMemRequirement.count;
  /* setting interrupt requirements */ 
  loResource->loLevelOption.maxInterruptVectors = SwConfig.max_MSIX_InterruptVectors;
  loResource->loLevelOption.max_MSI_InterruptVectors = SwConfig.max_MSI_InterruptVectors;
  loResource->loLevelOption.flag = SwConfig.legacyInt_X;
  TI_DBG2(("tdsaLoLevelGetResource: asking maxInterruptVectors(MSIX) %d \n", loResource->loLevelOption.maxInterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking max_MSI_InterruptVectors %d \n", loResource->loLevelOption.max_MSI_InterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking flag - legacyInt_X %d \n", loResource->loLevelOption.flag));

//  TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n",memRequirement.count, loResource->loLevelMem.mem[memRequirement.count].numElements,loResource->loLevelMem.mem[memRequirement.count].totalLength, loResource->loLevelMem.mem[memRequirement.count].singleElementLength,loResource->loLevelMem.mem[memRequirement.count].alignment ));
  TI_DBG6(("tdsaLoLevelGetResource: total memRequirement count %d TI_DMA_MEM\n", loResource->loLevelMem.count));

#elif defined(FDS_DM)
  TI_DBG1(("tdsaLoLevelGetResource:MAX_LL_LAYER_MEM_DESCRIPTORS %d\n", MAX_LL_LAYER_MEM_DESCRIPTORS)); 
  for(i=0;i<MAX_LL_LAYER_MEM_DESCRIPTORS;i++)
  {
    loResource->loLevelMem.mem[i].numElements           = 0;
    loResource->loLevelMem.mem[i].totalLength           = 0;
    loResource->loLevelMem.mem[i].singleElementLength   = 0;
    loResource->loLevelMem.mem[i].alignment             = 0;
    loResource->loLevelMem.mem[i].type                  = 0;
    loResource->loLevelMem.mem[i].reserved              = 0;
    loResource->loLevelMem.mem[i].virtPtr               = agNULL;
    loResource->loLevelMem.mem[i].osHandle              = agNULL;
    loResource->loLevelMem.mem[i].physAddrUpper         = 0;
    loResource->loLevelMem.mem[i].physAddrLower         = 0;
  }

  TI_DBG1(("tdsaLoLevelGetResource:memRequirement.count %d\n", memRequirement.count)); 
  /* using the returned value from saGetRequirements */
  for (i=0;i< memRequirement.count;i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = memRequirement.agMemory[i].numElements;
    loResource->loLevelMem.mem[i].totalLength = memRequirement.agMemory[i].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = memRequirement.agMemory[i].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = memRequirement.agMemory[i].alignment;
    TI_DBG2(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }
  
  TI_DBG1(("tdsaLoLevelGetResource:dmMemRequirement.count %d\n", dmMemRequirement.count)); 
  /* using the returned value from dmGetRequirements */
  for (i=memRequirement.count;i< (memRequirement.count + dmMemRequirement.count);i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = dmMemRequirement.dmMemory[i-memRequirement.count].numElements;
    loResource->loLevelMem.mem[i].totalLength = dmMemRequirement.dmMemory[i-memRequirement.count].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = dmMemRequirement.dmMemory[i-memRequirement.count].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = dmMemRequirement.dmMemory[i-memRequirement.count].alignment;
    TI_DBG2(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == dmMemRequirement.dmMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == dmMemRequirement.dmMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == dmMemRequirement.dmMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }
  
 
  
  /* sets the low level options */
  loResource->loLevelOption.usecsPerTick       = MIN(usecsPerTick, dmUsecsPerTick);
  loResource->loLevelOption.numOfQueuesPerPort = maxQueueSets + dmMaxNumLocks + TD_MAX_LOCKS + maxNumOSLocks;
  loResource->loLevelOption.mutexLockUsage     = tiOneMutexLockPerQueue;
  /* no more ESGL */
  loResource->loLevelMem.count = memRequirement.count + dmMemRequirement.count;
  /* setting interrupt requirements */ 
  loResource->loLevelOption.maxInterruptVectors = SwConfig.max_MSIX_InterruptVectors;
  loResource->loLevelOption.max_MSI_InterruptVectors = SwConfig.max_MSI_InterruptVectors;
  loResource->loLevelOption.flag = SwConfig.legacyInt_X;
  TI_DBG2(("tdsaLoLevelGetResource: asking maxInterruptVectors(MSIX) %d \n", loResource->loLevelOption.maxInterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking max_MSI_InterruptVectors %d \n", loResource->loLevelOption.max_MSI_InterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking flag - legacyInt_X %d \n", loResource->loLevelOption.flag));

//  TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n",memRequirement.count, loResource->loLevelMem.mem[memRequirement.count].numElements,loResource->loLevelMem.mem[memRequirement.count].totalLength, loResource->loLevelMem.mem[memRequirement.count].singleElementLength,loResource->loLevelMem.mem[memRequirement.count].alignment ));
  TI_DBG6(("tdsaLoLevelGetResource: total memRequirement count %d TI_DMA_MEM\n", loResource->loLevelMem.count));
  
#elif defined(FDS_SM)
  TI_DBG1(("tdsaLoLevelGetResource:MAX_LL_LAYER_MEM_DESCRIPTORS %d\n", MAX_LL_LAYER_MEM_DESCRIPTORS)); 
  for(i=0;i<MAX_LL_LAYER_MEM_DESCRIPTORS;i++)
  {
    loResource->loLevelMem.mem[i].numElements           = 0;
    loResource->loLevelMem.mem[i].totalLength           = 0;
    loResource->loLevelMem.mem[i].singleElementLength   = 0;
    loResource->loLevelMem.mem[i].alignment             = 0;
    loResource->loLevelMem.mem[i].type                  = 0;
    loResource->loLevelMem.mem[i].reserved              = 0;
    loResource->loLevelMem.mem[i].virtPtr               = agNULL;
    loResource->loLevelMem.mem[i].osHandle              = agNULL;
    loResource->loLevelMem.mem[i].physAddrUpper         = 0;
    loResource->loLevelMem.mem[i].physAddrLower         = 0;
  }

  TI_DBG1(("tdsaLoLevelGetResource:memRequirement.count %d\n", memRequirement.count)); 
  /* using the returned value from saGetRequirements */
  for (i=0;i< memRequirement.count;i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = memRequirement.agMemory[i].numElements;
    loResource->loLevelMem.mem[i].totalLength = memRequirement.agMemory[i].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = memRequirement.agMemory[i].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = memRequirement.agMemory[i].alignment;
    TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }
  
  TI_DBG1(("tdsaLoLevelGetResource:smMemRequirement.count %d\n", smMemRequirement.count)); 
  /* using the returned value from smGetRequirements */
  for (i=memRequirement.count;i< (memRequirement.count + smMemRequirement.count);i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = smMemRequirement.smMemory[i-memRequirement.count].numElements;
    loResource->loLevelMem.mem[i].totalLength = smMemRequirement.smMemory[i-memRequirement.count].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = smMemRequirement.smMemory[i-memRequirement.count].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = smMemRequirement.smMemory[i-memRequirement.count].alignment;
    TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == smMemRequirement.smMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == smMemRequirement.smMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == smMemRequirement.smMemory[i-memRequirement.count].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }
  
 
  
  /* sets the low level options */
  loResource->loLevelOption.usecsPerTick       = MIN(usecsPerTick, smUsecsPerTick);
  loResource->loLevelOption.numOfQueuesPerPort = maxQueueSets + smMaxNumLocks + TD_MAX_LOCKS + maxNumOSLocks;
  loResource->loLevelOption.mutexLockUsage     = tiOneMutexLockPerQueue;
  /* no more ESGL */
  loResource->loLevelMem.count = memRequirement.count + smMemRequirement.count;
  /* setting interrupt requirements */ 
  loResource->loLevelOption.maxInterruptVectors = SwConfig.max_MSIX_InterruptVectors;
  loResource->loLevelOption.max_MSI_InterruptVectors = SwConfig.max_MSI_InterruptVectors;
  loResource->loLevelOption.flag = SwConfig.legacyInt_X;
  TI_DBG2(("tdsaLoLevelGetResource: asking maxInterruptVectors(MSIX) %d \n", loResource->loLevelOption.maxInterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking max_MSI_InterruptVectors %d \n", loResource->loLevelOption.max_MSI_InterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking flag - legacyInt_X %d \n", loResource->loLevelOption.flag));

//  TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n",memRequirement.count, loResource->loLevelMem.mem[memRequirement.count].numElements,loResource->loLevelMem.mem[memRequirement.count].totalLength, loResource->loLevelMem.mem[memRequirement.count].singleElementLength,loResource->loLevelMem.mem[memRequirement.count].alignment ));
  TI_DBG6(("tdsaLoLevelGetResource: total memRequirement count %d TI_DMA_MEM\n", loResource->loLevelMem.count));
  

#else
  TI_DBG6(("tdsaLoLevelGetResource:MAX_LL_LAYER_MEM_DESCRIPTORS %d\n", MAX_LL_LAYER_MEM_DESCRIPTORS)); 
  for(i=0;i<MAX_LL_LAYER_MEM_DESCRIPTORS;i++)
  {
    loResource->loLevelMem.mem[i].numElements           = 0;
    loResource->loLevelMem.mem[i].totalLength           = 0;
    loResource->loLevelMem.mem[i].singleElementLength   = 0;
    loResource->loLevelMem.mem[i].alignment             = 0;
    loResource->loLevelMem.mem[i].type                  = 0;
    loResource->loLevelMem.mem[i].reserved              = 0;
    loResource->loLevelMem.mem[i].virtPtr               = agNULL;
    loResource->loLevelMem.mem[i].osHandle              = agNULL;
    loResource->loLevelMem.mem[i].physAddrUpper         = 0;
    loResource->loLevelMem.mem[i].physAddrLower         = 0;
  }
  
  /* using the returned value from saGetRequirements */
  for (i=0;i< memRequirement.count;i++)
  {
    /* hardcoded values for now */
    loResource->loLevelMem.mem[i].numElements = memRequirement.agMemory[i].numElements;
    loResource->loLevelMem.mem[i].totalLength = memRequirement.agMemory[i].totalLength;
    loResource->loLevelMem.mem[i].singleElementLength = memRequirement.agMemory[i].singleElementLength;
    loResource->loLevelMem.mem[i].alignment = memRequirement.agMemory[i].alignment;
    TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].numElements, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].singleElementLength,loResource->loLevelMem.mem[i].alignment ));
    if ( AGSA_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_DMA_MEM;
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_DMA_MEM\n", i));
      
    }
    else if ( AGSA_CACHED_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_MEM\n", i));
    }
    else if ( AGSA_CACHED_DMA_MEM == memRequirement.agMemory[i].type )
    {
      loResource->loLevelMem.mem[i].type = TI_CACHED_DMA_MEM;      
      TI_DBG6(("tdsaLoLevelGetResource: index %d TI_CACHED_DMA_MEM\n", i));
    }
  }
  
 
  
  /* sets the low level options */
  loResource->loLevelOption.usecsPerTick       = usecsPerTick;
  loResource->loLevelOption.numOfQueuesPerPort = maxQueueSets + TD_MAX_LOCKS + maxNumOSLocks;
  loResource->loLevelOption.mutexLockUsage     = tiOneMutexLockPerQueue;
  /* no more ESGL */
  loResource->loLevelMem.count = memRequirement.count;
  /* setting interrupt requirements */ 
  loResource->loLevelOption.maxInterruptVectors = SwConfig.max_MSIX_InterruptVectors;
  loResource->loLevelOption.max_MSI_InterruptVectors = SwConfig.max_MSI_InterruptVectors;
  loResource->loLevelOption.flag = SwConfig.legacyInt_X;
  TI_DBG2(("tdsaLoLevelGetResource: asking maxInterruptVectors(MSIX) %d \n", loResource->loLevelOption.maxInterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking max_MSI_InterruptVectors %d \n", loResource->loLevelOption.max_MSI_InterruptVectors));
  TI_DBG2(("tdsaLoLevelGetResource: asking flag - legacyInt_X %d \n", loResource->loLevelOption.flag));

  TI_DBG6(("tdsaLoLevelGetResource: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n",memRequirement.count, loResource->loLevelMem.mem[memRequirement.count].numElements,loResource->loLevelMem.mem[memRequirement.count].totalLength, loResource->loLevelMem.mem[memRequirement.count].singleElementLength,loResource->loLevelMem.mem[memRequirement.count].alignment ));
  TI_DBG6(("tdsaLoLevelGetResource: memRequirement.count %d TI_DMA_MEM\n", memRequirement.count));
#endif 



 return;
}

/*****************************************************************************
*! \brief tdsaSharedMemCalculate
*
*  Purpose:  This function is called to determine the Transport 
*            Dependent Layer internal resource requirement 
*            for shared memory between target and initiator
*            functionality.
*
*  \param  tiRoot:             Pointer to driver/port instance.
*  \param  tdSharedMem:        Pointer to shared memory structure
*
*  \return: None
*
*  \note - The shared memory is composed of like the followings
*          sizeof(tdsaRoot_t)
*          + sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT
*          + sizeof(tdsaDeviceData_t) * MaxTargets
*          + sizeof(tdsaEsglPageInfo_t) * NumEsglPages
*
*****************************************************************************/
osGLOBAL void
tdsaSharedMemCalculate(
                       tiRoot_t              * tiRoot,
                       tiLoLevelResource_t   * loResource,
                       tiTdSharedMem_t       * tdSharedMem
                       )
{
  bit32 MaxTargets;
  
  /* the following fn fills in MaxTargets */
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tdsaSharedMemCalculate: MaxTargets %d\n", MaxTargets));
   
  /*
   * Cached mem for the shared TD Layer functionality
   */
  tdSharedMem->tdSharedCachedMem1.singleElementLength =
    sizeof(tdsaRoot_t) + (sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT) +
    (sizeof(tdsaDeviceData_t) * MaxTargets);
  
#ifdef TD_INT_COALESCE
  /* adding TD interrupt coalesce data structure to the shared TD layer */
  /* TD_MAX_INT_COALESCE is defined to be 512 */
  tdSharedMem->tdSharedCachedMem1.singleElementLength +=
    sizeof(tdsaIntCoalesceContext_t) * TD_MAX_INT_COALESCE;
#endif

#ifdef TD_DISCOVER
  /* adding expander data strutures */
  tdSharedMem->tdSharedCachedMem1.singleElementLength +=
    sizeof(tdsaExpander_t) * MaxTargets;
#endif

  tdSharedMem->tdSharedCachedMem1.numElements = 1;

  tdSharedMem->tdSharedCachedMem1.totalLength =
      tdSharedMem->tdSharedCachedMem1.singleElementLength *
      tdSharedMem->tdSharedCachedMem1.numElements;

  tdSharedMem->tdSharedCachedMem1.alignment = 8;

  tdSharedMem->tdSharedCachedMem1.type = TI_CACHED_MEM;
  
  tdSharedMem->tdSharedCachedMem1.virtPtr = agNULL;
  tdSharedMem->tdSharedCachedMem1.osHandle = agNULL;
  tdSharedMem->tdSharedCachedMem1.physAddrUpper = 0;
  tdSharedMem->tdSharedCachedMem1.physAddrLower = 0;
  tdSharedMem->tdSharedCachedMem1.reserved = 0;

  return;
}


/*****************************************************************************
*! \biref tdResetComMemFlags
*
*  Purpose:  This function is called to reset all the flags for the port
*
*  \param  tiRoot:             Pointer to driver/port instance.
*
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
tdsaResetComMemFlags(
                     tiRoot_t *tiRoot
                     )
{
  tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef TD_DEBUG_ENABLE
  tdsaPortContext_t *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContextMem;
  TI_DBG6(("tdsaResetComMemFlags: start\n"));
  TI_DBG6(("tdsaResetComMemFlag:: ******* tdsaRoot %p \n", tdsaRoot));
  TI_DBG6(("tdsaResetComMemFlag:: ******* tdsaPortContext %p \n",tdsaPortContext));
#endif
  
  tdsaAllShared->flags.sysIntsActive              = agFALSE;
  tdsaAllShared->flags.resetInProgress            = agFALSE;
  
  return;
}

/*****************************************************************************
*! \biref tdssInitSASPortStartInfo
*
*  Purpose:  This function sets information related to staring a port
*
*  \param  tiRoot:             Pointer to driver/port instance.
*
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void 
tdssInitSASPortStartInfo(
                         tiRoot_t *tiRoot
                         )
{
  tdsaRoot_t        *tdsaRoot = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  int i;
#ifdef TD_DEBUG_ENABLE
  tdsaPortContext_t *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContextMem;
  TI_DBG6(("tdssInitSASPortStartInfo: start\n"));
  
  TI_DBG6(("tdssInitSASPortStartInfo: ******* tdsaRoot %p \n", tdsaRoot));
  TI_DBG6(("tdssInitSASPortStartInfo: ******* tdsaPortContext %p \n",tdsaPortContext));
#endif
  
  for(i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    tdsaAllShared->Ports[i].tiPortalContext = agNULL;
    tdsaAllShared->Ports[i].portContext = agNULL;
    tdsaAllShared->Ports[i].SASID.sasAddressHi[0] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressHi[1] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressHi[2] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressHi[3] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressLo[0] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressLo[1] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressLo[2] = 0;
    tdsaAllShared->Ports[i].SASID.sasAddressLo[3] = 0;
    tdsaAllShared->Ports[i].SASID.phyIdentifier = (bit8) i;
    /* continue .... */
    
    tdsaAllShared->Ports[i].flags.portStarted = agFALSE; 
    tdsaAllShared->Ports[i].flags.portInitialized = agFALSE;
    tdsaAllShared->Ports[i].flags.portReadyForDiscoverySent = agFALSE;
    tdsaAllShared->Ports[i].flags.portStoppedByOSLayer = agFALSE;
    tdsaAllShared->Ports[i].flags.failPortInit = agFALSE;
  }
  
  return;
}


/*****************************************************************************
*! \brief tdsaInitTimers
*
*  Purpose: This function is called to initialize the timers
*           for initiator
*
*  \param   tiRoot: pointer to the driver instance
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/ 

osGLOBAL void
tdsaInitTimers(
               tiRoot_t *tiRoot 
               )
{
  tdsaRoot_t               *tdsaRoot    = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t            *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef TD_DEBUG_ENABLE
  tdsaPortContext_t *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContextMem;
  
  TI_DBG6(("tdsaInitTimers: start \n"));
  TI_DBG6(("tdsaInitTimers: ******* tdsaRoot %p \n", tdsaRoot));
  TI_DBG6(("tdsaInitTimers: ******* tdsaPortContext %p \n",tdsaPortContext));
#endif
  
  /* initialize the timerlist */
  TDLIST_INIT_HDR(&(tdsaAllShared->timerlist));

  return;
}


/*****************************************************************************
*! \brief tdsaJumpTableInit
*
*  Purpose: This function initializes SAS related callback functions
*
*  \param   tiRoot: pointer to the driver instance
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/
osGLOBAL void
tdsaJumpTableInit(
                  tiRoot_t *tiRoot
                  )
{
  
  tdsaRoot_t        *tdsaRoot    = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef TD_DEBUG_ENABLE
  tdsaPortContext_t *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContextMem;
  
  TI_DBG6(("tdsaJumpTableInit: start \n"));
  TI_DBG6(("tdsaJumpTableInit:: ******* tdsaRoot %p \n", tdsaRoot));
  TI_DBG6(("tdsaJumpTableInit:: ******* tdsaPortContext %p \n",tdsaPortContext));
#endif

  /* tdtype.h */
  /*
    For combo,
    pSSPIOCompleted, pSMPCompleted; use callback 
    pSSPReqReceive, pSMPReqReceived; use jumptable
  */

#ifdef INITIATOR_DRIVER
  tdsaAllShared->tdJumpTable.pSSPIOCompleted = agNULL; /* initiator */
  tdsaAllShared->tdJumpTable.pSMPCompleted =agNULL; /* initiator */
#endif
#ifdef TARGET_DRIVER
  tdsaAllShared->tdJumpTable.pSSPIOCompleted = agNULL;
  tdsaAllShared->tdJumpTable.pSSPReqReceived = &ttdsaSSPReqReceived;
  tdsaAllShared->tdJumpTable.pSMPReqReceived = &ttdsaSMPReqReceived;
  tdsaAllShared->tdJumpTable.pSMPCompleted =agNULL; 
#endif
  tdsaAllShared->tdJumpTable.pGetSGLChunk = agNULL;
  return;

}


/*****************************************************************************
*! \brief tdsaPortContextInit
*
*  Purpose: This function initializes port contexts.
*
*  \param   tiRoot: pointer to the driver instance
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/
osGLOBAL void
tdsaPortContextInit(
                    tiRoot_t *tiRoot 
                    )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaPortContext_t *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContextMem;
  int i = 0;
  int j = 0;

  TI_DBG6(("tdsaPortContextInit: start\n"));
  TI_DBG6(("tdsaPortContextInit: ******* sizeof(tdsaPortContext) %d %x\n", (int)sizeof(tdsaPortContext_t), (unsigned int)sizeof(tdsaPortContext_t)));
  TI_DBG6(("tdsaPortContextInit: ******* tdsaRoot %p \n", tdsaRoot));
  TI_DBG6(("tdsaPortContextInit: ******* tdsaPortContext %p \n",tdsaPortContext)); 
  TI_DBG6(("tdsaPortContextInit: ******* tdsaPortContext+1 %p \n",tdsaPortContext + 1)); 
  TI_DBG6(("tdsaPortContextInit: ******* &tdsaPortContext[0] %p  &tdsaPortContext[1] %p\n", &(tdsaPortContext[0]), &(tdsaPortContext[1])));

  TDLIST_INIT_HDR(&(tdsaAllShared->MainPortContextList));
  TDLIST_INIT_HDR(&(tdsaAllShared->FreePortContextList));

  for(i=0;i<TD_MAX_PORT_CONTEXT;i++)
  {
    TDLIST_INIT_ELEMENT(&(tdsaPortContext[i].FreeLink));
    TDLIST_INIT_ELEMENT(&(tdsaPortContext[i].MainLink));

#ifdef TD_DISCOVER
    TDLIST_INIT_HDR(&(tdsaPortContext[i].discovery.discoveringExpanderList));
    TDLIST_INIT_HDR(&(tdsaPortContext[i].discovery.UpdiscoveringExpanderList));
    tdsaPortContext[i].discovery.type = TDSA_DISCOVERY_OPTION_FULL_START;
    tdsaInitTimerRequest(tiRoot, &(tdsaPortContext[i].discovery.discoveryTimer));
    tdsaInitTimerRequest(tiRoot, &(tdsaPortContext[i].discovery.configureRouteTimer));
    tdsaInitTimerRequest(tiRoot, &(tdsaPortContext[i].discovery.deviceRegistrationTimer));
    tdsaInitTimerRequest(tiRoot, &(tdsaPortContext[i].discovery.SMPBusyTimer));
    tdsaInitTimerRequest(tiRoot, &(tdsaPortContext[i].discovery.BCTimer));
    tdsaInitTimerRequest(tiRoot, &(tdsaPortContext[i].discovery.DiscoverySMPTimer));
    tdsaPortContext[i].discovery.retries = 0;  
    tdsaPortContext[i].discovery.configureRouteRetries = 0;  
    tdsaPortContext[i].discovery.deviceRetistrationRetries = 0;  
    tdsaPortContext[i].discovery.pendingSMP = 0;  
    tdsaPortContext[i].discovery.SeenBC = agFALSE;  
    tdsaPortContext[i].discovery.forcedOK = agFALSE;  
    tdsaPortContext[i].discovery.SMPRetries = 0;  
//    tdsaPortContext[i].discovery.doIncremental = agFALSE;  
    tdsaPortContext[i].discovery.ResetTriggerred = agFALSE;  
#endif

    
#ifdef INITIATOR_DRIVER  
    tdsaPortContext[i].DiscoveryState = ITD_DSTATE_NOT_STARTED;
    tdsaPortContext[i].nativeSATAMode = agFALSE;
    tdsaPortContext[i].directAttatchedSAS = agFALSE;
    tdsaPortContext[i].DiscoveryRdyGiven = agFALSE;
    tdsaPortContext[i].SeenLinkUp = agFALSE;
    
#endif      
    tdsaPortContext[i].id = i;
    tdsaPortContext[i].agPortContext = agNULL;
    tdsaPortContext[i].LinkRate = 0;
    tdsaPortContext[i].Count = 0;
    tdsaPortContext[i].valid = agFALSE;
    for (j=0;j<TD_MAX_NUM_PHYS;j++)
    {
      tdsaPortContext[i].PhyIDList[j] = agFALSE;
    }
    tdsaPortContext[i].RegisteredDevNums = 0;
    tdsaPortContext[i].eventPhyID = 0xFF;
    tdsaPortContext[i].Transient = agFALSE;
    tdsaPortContext[i].PortRecoverPhyID = 0xFF;
    tdsaPortContext[i].DiscFailNSeenBC = agFALSE;
#ifdef FDS_DM
    tdsaPortContext[i].dmPortContext.tdData = &(tdsaPortContext[i]);
    tdsaPortContext[i].DMDiscoveryState = dmDiscCompleted;
    tdsaPortContext[i].UseDM = agFALSE;    
    tdsaPortContext[i].UpdateMCN = agFALSE;    
#endif
    /* add more variables later */
    TDLIST_ENQUEUE_AT_TAIL(&(tdsaPortContext[i].FreeLink), &(tdsaAllShared->FreePortContextList));
  }

#ifdef TD_INTERNAL_DEBUG  /* for debugging only */
  for(i=0;i<TD_MAX_PORT_CONTEXT;i++)
  {
    TI_DBG6(("tdsaPortContextInit: index %d  &tdsaPortContext[] %p\n", i, &(tdsaPortContext[i])));
  }
  TI_DBG6(("tdsaPortContextInit: sizeof(tdsaPortContext_t) %d 0x%x\n", sizeof(tdsaPortContext_t), sizeof(tdsaPortContext_t)));
#endif
  return;
}

/*****************************************************************************
*! \brief tdsaPortContextReInit
*
*  Purpose: This function re-initializes port contexts for reuse.
*
*  \param   tiRoot:         pointer to the driver instance
*  \param   onePortContext: pointer to the portcontext
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/
osGLOBAL void
tdsaPortContextReInit(
                      tiRoot_t             *tiRoot,
                      tdsaPortContext_t    *onePortContext		     
                    )
{
  int               j=0;
#ifdef TD_DISCOVER
  tdsaDiscovery_t   *discovery;
#endif

  TI_DBG3(("tdsaPortContextReInit: start\n"));
  
#ifdef TD_DISCOVER
  discovery = &(onePortContext->discovery);
  
    onePortContext->discovery.type = TDSA_DISCOVERY_OPTION_FULL_START;
    onePortContext->discovery.retries = 0;  
    onePortContext->discovery.configureRouteRetries = 0;  
    onePortContext->discovery.deviceRetistrationRetries = 0;  
    onePortContext->discovery.pendingSMP = 0;  
    onePortContext->discovery.SeenBC = agFALSE;  
    onePortContext->discovery.forcedOK = agFALSE;  
    onePortContext->discovery.SMPRetries = 0;  
    onePortContext->discovery.ResetTriggerred = agFALSE;
    /* free expander lists */
    tdsaFreeAllExp(tiRoot, onePortContext);
    /* kill the discovery-related timers if they are running */  
    if (discovery->discoveryTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->discoveryTimer
                   );
    }
    if (discovery->configureRouteTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->configureRouteTimer
                   );
    }
    if (discovery->deviceRegistrationTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->deviceRegistrationTimer
                   );
    }
    if (discovery->BCTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->BCTimer
                   );
    }
    if (discovery->SMPBusyTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->SMPBusyTimer
                   );
    }    
    if (discovery->DiscoverySMPTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->DiscoverySMPTimer
                   );
    }    
#endif

#ifdef INITIATOR_DRIVER  
    onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
    onePortContext->nativeSATAMode = agFALSE;
    onePortContext->directAttatchedSAS = agFALSE;
    onePortContext->DiscoveryRdyGiven = agFALSE;
    onePortContext->SeenLinkUp = agFALSE;
#endif
    onePortContext->agPortContext->osData = agNULL;
    onePortContext->agPortContext = agNULL;
    onePortContext->tiPortalContext = agNULL;
    onePortContext->agRoot = agNULL;
    onePortContext->LinkRate = 0;
    onePortContext->Count = 0;
    onePortContext->valid = agFALSE;
    for (j=0;j<TD_MAX_NUM_PHYS;j++)
    {
      onePortContext->PhyIDList[j] = agFALSE;
    }
    onePortContext->RegisteredDevNums = 0;
    onePortContext->eventPhyID = 0xFF;
    onePortContext->Transient = agFALSE;
    onePortContext->PortRecoverPhyID = 0xFF;
    onePortContext->DiscFailNSeenBC = agFALSE;

#ifdef FDS_DM
    onePortContext->dmPortContext.tdData = onePortContext;
    onePortContext->DMDiscoveryState = dmDiscCompleted;
    onePortContext->UseDM = agFALSE;
    onePortContext->UpdateMCN = agFALSE;
#endif
  return;
}
		    
/*****************************************************************************
*! \brief tdsaDeviceDataInit
*
*  Purpose: This function initializes devices
*
*  \param   tiRoot: pointer to the driver instance
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/
osGLOBAL void
tdsaDeviceDataInit(
                   tiRoot_t *tiRoot 
                   )
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef TD_DEBUG_ENABLE
  tdsaPortContext_t *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContextMem;
#endif
  tdsaDeviceData_t  *tdsaDeviceData =
    (tdsaDeviceData_t *)tdsaAllShared->DeviceMem;
  int i;
#ifdef  SATA_ENABLE
  bit32             j;
  satInternalIo_t   *satIntIO;
#endif
  bit32             MaxTargets;
  
  TI_DBG6(("tdsaDeviceDataInit: start\n"));
  TI_DBG6(("tdsaDeviceDataInit: ******* tdsaPortContext %p \n",tdsaPortContext));
  TI_DBG6(("tdsaDeviceDataInit: ******* tdsaDeviceData %p\n", tdsaDeviceData));
  TI_DBG6(("tdsaDeviceDataInit: ******* tdsaDeviceData+1 %p\n", tdsaDeviceData+1));
  TI_DBG6(("tdsaDeviceDataInit: ******* &tdsaDeviceData[0] %p  &tdsaDeviceData[1] %p\n", &(tdsaDeviceData[0]), &(tdsaDeviceData[1])));
  
  /* the following fn fills in MaxTargets */
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tdsaDeviceDataInit: MaxTargets %d\n", MaxTargets));
  
  TDLIST_INIT_HDR(&(tdsaAllShared->MainDeviceList));
  TDLIST_INIT_HDR(&(tdsaAllShared->FreeDeviceList));

  for(i=0;i<(int)MaxTargets;i++)
  {
    TDLIST_INIT_ELEMENT(&(tdsaDeviceData[i].FreeLink));
    TDLIST_INIT_ELEMENT(&(tdsaDeviceData[i].MainLink));
    TDLIST_INIT_ELEMENT(&(tdsaDeviceData[i].IncDisLink));
    tdsaDeviceData[i].id = i;
    tdsaDeviceData[i].InQID = 0;
    tdsaDeviceData[i].OutQID = 0;
    tdsaDeviceData[i].DeviceType = TD_DEFAULT_DEVICE;
    tdsaDeviceData[i].agRoot = agNULL;
    tdsaDeviceData[i].agDevHandle = agNULL;
    
    tdsaDeviceData[i].pJumpTable = &(tdsaAllShared->tdJumpTable);
    tdsaDeviceData[i].tiDeviceHandle.osData = agNULL;
    tdsaDeviceData[i].tiDeviceHandle.tdData = &(tdsaDeviceData[i]);
    tdsaDeviceData[i].tdPortContext = agNULL;
    tdsaDeviceData[i].tdExpander = agNULL;
    tdsaDeviceData[i].ExpDevice = agNULL;
    tdsaDeviceData[i].phyID = 0xFF;
    tdsaDeviceData[i].SASAddressID.sasAddressHi = 0;
    tdsaDeviceData[i].SASAddressID.sasAddressLo = 0;
    tdsaDeviceData[i].valid = agFALSE;
    tdsaDeviceData[i].valid2 = agFALSE;
    tdsaDeviceData[i].processed = agFALSE;
    tdsaDeviceData[i].initiator_ssp_stp_smp = 0;
    tdsaDeviceData[i].target_ssp_stp_smp = 0;
    tdsaDeviceData[i].numOfPhys = 0;
    tdsaDeviceData[i].registered = agFALSE;
    tdsaDeviceData[i].directlyAttached = agFALSE;
    tdsaDeviceData[i].SASSpecDeviceType = 0xFF;
    tdsaDeviceData[i].IOStart = 0;
    tdsaDeviceData[i].IOResponse = 0;
    tdsaDeviceData[i].agDeviceResetContext.osData = agNULL;
    tdsaDeviceData[i].agDeviceResetContext.sdkData = agNULL;
    tdsaDeviceData[i].TRflag = agFALSE;
    tdsaDeviceData[i].ResetCnt = 0;
    tdsaDeviceData[i].OSAbortAll = agFALSE;
    
#ifdef FDS_DM
    tdsaDeviceData[i].devMCN = 1;
    tdsaDeviceData[i].finalMCN = 1;
#endif

#ifdef FDS_SM
    tdsaDeviceData[i].SMNumOfFCA = 0;
    tdsaDeviceData[i].SMNumOfID = 0;
#endif
    
#ifdef  SATA_ENABLE
    TDLIST_INIT_HDR(&(tdsaDeviceData[i].satDevData.satIoLinkList));
    TDLIST_INIT_HDR(&(tdsaDeviceData[i].satDevData.satFreeIntIoLinkList));
    TDLIST_INIT_HDR(&(tdsaDeviceData[i].satDevData.satActiveIntIoLinkList));
    
    /* default */
    tdsaDeviceData[i].satDevData.satDriveState = SAT_DEV_STATE_NORMAL;
    tdsaDeviceData[i].satDevData.satNCQMaxIO =SAT_NCQ_MAX;
    tdsaDeviceData[i].satDevData.satPendingIO = 0;
    tdsaDeviceData[i].satDevData.satPendingNCQIO = 0;
    tdsaDeviceData[i].satDevData.satPendingNONNCQIO = 0;
    tdsaDeviceData[i].satDevData.IDDeviceValid = agFALSE;
    tdsaDeviceData[i].satDevData.freeSATAFDMATagBitmap = 0;
    tdsaDeviceData[i].satDevData.NumOfFCA = 0;
    tdsaDeviceData[i].satDevData.NumOfIDRetries = 0;
    tdsaDeviceData[i].satDevData.ID_Retries = 0;
    tdsaDeviceData[i].satDevData.IDPending = agFALSE;
    tdsaInitTimerRequest(tiRoot, &(tdsaDeviceData[i].SATAIDDeviceTimer));
#ifdef FDS_SM
    tdsaInitTimerRequest(tiRoot, &(tdsaDeviceData[i].tdIDTimer));
#endif   
    osti_memset(tdsaDeviceData[i].satDevData.satMaxLBA, 0, sizeof(tdsaDeviceData[i].satDevData.satMaxLBA));

    tdsaDeviceData[i].satDevData.satSaDeviceData = &tdsaDeviceData[i];
    satIntIO = &tdsaDeviceData[i].satDevData.satIntIo[0];
    for (j = 0; j < SAT_MAX_INT_IO; j++)
    {
      TDLIST_INIT_ELEMENT (&satIntIO->satIntIoLink);
      TDLIST_ENQUEUE_AT_TAIL (&satIntIO->satIntIoLink, 
                              &tdsaDeviceData[i].satDevData.satFreeIntIoLinkList);
      satIntIO->satOrgTiIORequest = agNULL;
      satIntIO->id = j;
      satIntIO = satIntIO + 1;
    }
#endif
    /* some other variables */
    TDLIST_ENQUEUE_AT_TAIL(&(tdsaDeviceData[i].FreeLink), &(tdsaAllShared->FreeDeviceList)); 
  }

#ifdef TD_INTERNAL_DEBUG  /* for debugging only */
  for(i=0;i<MaxTargets;i++)
  {
    TI_DBG6(("tdsaDeviceDataInit: index %d  &tdsaDeviceData[] %p\n", i, &(tdsaDeviceData[i])));
    
  }
  TI_DBG6(("tdsaDeviceDataInit: sizeof(tdsaDeviceData_t) %d 0x%x\n", sizeof(tdsaDeviceData_t), sizeof(tdsaDeviceData_t)));
#endif  
  return;
}

/*****************************************************************************
*! \brief tdsaDeviceDataReInit
*
*  Purpose: This function re-initializes device data for reuse.
*
*  \param   tiRoot:         pointer to the driver instance
*  \param   onePortContext: pointer to the device data
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/
osGLOBAL void
tdsaDeviceDataReInit(
                   tiRoot_t             *tiRoot, 
                   tdsaDeviceData_t     *oneDeviceData
                   )
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef  SATA_ENABLE    
  int               j=0;
  satInternalIo_t   *satIntIO;
#endif
  
  TI_DBG3(("tdsaDeviceDataReInit: start\n"));
  
    oneDeviceData->InQID = 0;
    oneDeviceData->OutQID = 0;
    oneDeviceData->DeviceType = TD_DEFAULT_DEVICE;
    oneDeviceData->agDevHandle = agNULL;
    
    oneDeviceData->pJumpTable = &(tdsaAllShared->tdJumpTable);
    oneDeviceData->tiDeviceHandle.osData = agNULL;
    oneDeviceData->tiDeviceHandle.tdData = oneDeviceData;
    oneDeviceData->tdPortContext = agNULL;
    oneDeviceData->tdExpander = agNULL;
    oneDeviceData->ExpDevice = agNULL;
    oneDeviceData->phyID = 0xFF;
    oneDeviceData->SASAddressID.sasAddressHi = 0;
    oneDeviceData->SASAddressID.sasAddressLo = 0;
    oneDeviceData->valid = agFALSE;
    oneDeviceData->valid2 = agFALSE;
    oneDeviceData->processed = agFALSE;
    oneDeviceData->initiator_ssp_stp_smp = 0;
    oneDeviceData->target_ssp_stp_smp = 0;
    oneDeviceData->numOfPhys = 0;
    oneDeviceData->registered = agFALSE;
    oneDeviceData->directlyAttached = agFALSE;
    oneDeviceData->SASSpecDeviceType = 0xFF;
    oneDeviceData->IOStart = 0;
    oneDeviceData->IOResponse = 0;
    oneDeviceData->agDeviceResetContext.osData = agNULL;
    oneDeviceData->agDeviceResetContext.sdkData = agNULL;
    oneDeviceData->TRflag = agFALSE;
    oneDeviceData->ResetCnt = 0;   
    oneDeviceData->OSAbortAll = agFALSE;   

#ifdef FDS_DM
    oneDeviceData->devMCN = 1;
    oneDeviceData->finalMCN = 1;
#endif
    
#ifdef FDS_SM
    oneDeviceData->SMNumOfFCA = 0;
    oneDeviceData->SMNumOfID = 0;
    if (oneDeviceData->tdIDTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &oneDeviceData->tdIDTimer
                    );
    }
#endif

#ifdef  SATA_ENABLE    
    /* default */
    oneDeviceData->satDevData.satDriveState = SAT_DEV_STATE_NORMAL;
    oneDeviceData->satDevData.satNCQMaxIO =SAT_NCQ_MAX;
    oneDeviceData->satDevData.satPendingIO = 0;
    oneDeviceData->satDevData.satPendingNCQIO = 0;
    oneDeviceData->satDevData.satPendingNONNCQIO = 0;
    oneDeviceData->satDevData.IDDeviceValid = agFALSE;
    oneDeviceData->satDevData.freeSATAFDMATagBitmap = 0;
    oneDeviceData->satDevData.NumOfFCA = 0;
    oneDeviceData->satDevData.NumOfIDRetries = 0;
    oneDeviceData->satDevData.ID_Retries = 0;
    oneDeviceData->satDevData.IDPending = agFALSE;
    
    osti_memset(oneDeviceData->satDevData.satMaxLBA, 0, sizeof(oneDeviceData->satDevData.satMaxLBA));
    osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));

    oneDeviceData->satDevData.satSaDeviceData = oneDeviceData;
    
    satIntIO = (satInternalIo_t *)&(oneDeviceData->satDevData.satIntIo[0]);
    for (j = 0; j < SAT_MAX_INT_IO; j++)
    {
      TI_DBG3(("tdsaDeviceDataReInit: in loop of internal io free, id %d\n", satIntIO->id));
      satFreeIntIoResource(tiRoot, &(oneDeviceData->satDevData), satIntIO);    
      satIntIO = satIntIO + 1;    
    }
#endif
  return;
}		   

#ifdef TD_INT_COALESCE
/*****************************************************************************
*! \brief tdsaIntCoalCxtInit(
*
*  Purpose: This function initializes interrupt coalesce contexts.
*
*  \param   tiRoot: pointer to the driver instance
*
*  \return: None
*
*  \note: 
*
*****************************************************************************/
osGLOBAL void
tdsaIntCoalCxtInit(
                    tiRoot_t *tiRoot 
                    )
{
  tdsaRoot_t               *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t            *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaPortContext_t        *tdsaPortContext = (tdsaPortContext_t *)tdsaAllShared->PortContext;
  tdsaDeviceData_t         *tdsaDeviceData  = (tdsaDeviceData_t *)tdsaAllShared->DeviceDataHead;
  tdsaIntCoalesceContext_t *tdsaIntCoalCxt  = (tdsaIntCoalesceContext_t *)tdsaAllShared->IntCoalesce;
  int i = 0;
  int j = 0;
  bit32             MaxTargets;
   
  TI_DBG2(("tdsaIntCoalCxtInit: start\n"));
  TI_DBG6(("tdsaIntCoalCxtInit: ******* sizeof(tdsaPortContext) %d 0x%x\n", sizeof(tdsaPortContext_t), sizeof(tdsaPortContext_t)));
  TI_DBG6(("tdsaIntCoalCxtInit: ******* sizeof(tdsaIntCoalCxt) %d 0x%x\n", sizeof(tdsaDeviceData_t), sizeof(tdsaDeviceData_t)));
  TI_DBG6(("tdsaIntCoalCxtInit: ******* sizeof(tdsaIntCoalCxt) %d 0x%x\n", sizeof(tdsaIntCoalesceContext_t), sizeof(tdsaIntCoalesceContext_t)));
  TI_DBG6(("tdsaIntCoalCxtInit: ******* tdsaRoot %p \n", tdsaRoot));
  TI_DBG6(("tdsaIntCoalCxtInit: ******* tdsaPortContext %p \n",tdsaPortContext));
  TI_DBG6(("tdsaDeviceDataInit: ******* tdsaDeviceData %p\n", tdsaDeviceData));
  TI_DBG6(("tdsaIntCoalCxtInit: ******* tdsaIntCoalCxt+1 %p \n", tdsaIntCoalCxt + 1)); 
  TI_DBG6(("tdsaIntCoalCxtInit: ******* &tdsaIntCoalCxt[0] %p  &tdsaIntCoalCxt[1] %p\n", &(tdsaIntCoalCxt[0]), &(tdsaIntCoalCxt[1])));

  /* for debug */
  TI_DBG6(("tdsaIntCoalCxtInit: TD_MAX_PORT_CONTEXT %d\n", TD_MAX_PORT_CONTEXT));
  /* the following fn fills in MaxTargets */
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tdsaIntCoalCxtInit: MaxTargets %d\n", MaxTargets));
  
  TI_DBG6(("tdsaIntCoalCxtInit: portcontext in sum 0x%x\n", sizeof(tdsaPortContext_t) * TD_MAX_PORT_CONTEXT));
  TI_DBG6(("tdsaIntCoalCxtInit: devicedata in sum 0x%x\n", sizeof(tdsaDeviceData_t) * MaxTargets));
  
  /* 
     tdsaIntCoalCx[0] is just head, not an element
  */
  TDLIST_INIT_HDR(&(tdsaIntCoalCxt[0].MainLink));
  TDLIST_INIT_HDR(&(tdsaIntCoalCxt[0].FreeLink));

  tdsaIntCoalCxt[0].tdsaAllShared = tdsaAllShared;
  tdsaIntCoalCxt[0].tiIntCoalesceCxt = agNULL;
  tdsaIntCoalCxt[0].id = 0;

  
  for(i=1;i<TD_MAX_INT_COALESCE;i++)
  {
    TDLIST_INIT_ELEMENT(&(tdsaIntCoalCxt[i].FreeLink));
    TDLIST_INIT_ELEMENT(&(tdsaIntCoalCxt[i].MainLink));
    
    tdsaIntCoalCxt[i].tdsaAllShared = tdsaAllShared;
    tdsaIntCoalCxt[i].tiIntCoalesceCxt = agNULL;
    tdsaIntCoalCxt[i].id = i;

    /* enqueue */
    TDLIST_ENQUEUE_AT_TAIL(&(tdsaIntCoalCxt[i].FreeLink), &(tdsaIntCoalCxt[0].FreeLink));
  }
  return;
}
#endif /* TD_INT_COALESCE */


osGLOBAL void
tdsaExpanderInit(
                 tiRoot_t *tiRoot 
                 )
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  tdsaExpander_t    *tdsaExpander =
    (tdsaExpander_t *)tdsaAllShared->ExpanderHead;
  bit32             MaxTargets;

  
  int i;

  TI_DBG6(("tdsaExpanderInit: start\n"));
  tdssGetMaxTargetsParams(tiRoot, &MaxTargets);
  TI_DBG6(("tdsaExpanderInit: MaxTargets %d\n", MaxTargets));
  
  //  TDLIST_INIT_HDR(&(tdsaAllShared->discoveringExpanderList));
  TDLIST_INIT_HDR(&(tdsaAllShared->freeExpanderList));
  
  for(i=0;i<(int)MaxTargets;i++)
  {
    TDLIST_INIT_ELEMENT(&(tdsaExpander[i].linkNode));
    TDLIST_INIT_ELEMENT(&(tdsaExpander[i].upNode));
    /* initialize expander fields */
    tdsaExpander[i].tdDevice = agNULL;
    tdsaExpander[i].tdUpStreamExpander = agNULL;
    tdsaExpander[i].tdDeviceToProcess = agNULL;
    tdsaExpander[i].tdCurrentDownStreamExpander = agNULL;
    tdsaExpander[i].hasUpStreamDevice = agFALSE;
    tdsaExpander[i].numOfUpStreamPhys = 0;
    tdsaExpander[i].currentUpStreamPhyIndex = 0;
    tdsaExpander[i].numOfDownStreamPhys = 0;
    tdsaExpander[i].currentDownStreamPhyIndex = 0;
    tdsaExpander[i].discoveringPhyId = 0;
    tdsaExpander[i].underDiscovering = agFALSE;
    tdsaExpander[i].id = i;
    tdsaExpander[i].tdReturnginExpander = agNULL;
    tdsaExpander[i].discoverSMPAllowed = agTRUE;
    osti_memset( &(tdsaExpander[i].currentIndex), 0, sizeof(tdsaExpander[i].currentIndex));
    osti_memset( &(tdsaExpander[i].upStreamPhys), 0, sizeof(tdsaExpander[i].upStreamPhys));
    osti_memset( &(tdsaExpander[i].downStreamPhys), 0, sizeof(tdsaExpander[i].downStreamPhys));
    osti_memset( &(tdsaExpander[i].routingAttribute), 0, sizeof(tdsaExpander[i].routingAttribute));
    tdsaExpander[i].configSASAddrTableIndex = 0;
    osti_memset( &(tdsaExpander[i].configSASAddressHiTable), 0, sizeof(tdsaExpander[i].configSASAddressHiTable));
    osti_memset( &(tdsaExpander[i].configSASAddressLoTable), 0, sizeof(tdsaExpander[i].configSASAddressLoTable));
    
    
    TDLIST_ENQUEUE_AT_TAIL(&(tdsaExpander[i].linkNode), &(tdsaAllShared->freeExpanderList)); 
  }
  return;
}

osGLOBAL void 
tdsaQueueConfigInit(
             tiRoot_t *tiRoot
             )
{
  tdsaRoot_t     *tdsaRoot    = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  
  /* for memory index requirement */
  agsaQueueConfig_t   *QueueConfig;
  bit32                i;

  TI_DBG2(("tdsaQueueConfigInit: start\n"));
  tdsaGetSwConfigParams(tiRoot);
  QueueConfig = &tdsaAllShared->QueueConfig;

  for(i=0;i<QueueConfig->numInboundQueues;i++)
  {
    QueueConfig->inboundQueues[i].elementCount = tdsaAllShared->InboundQueueSize[i];
    QueueConfig->inboundQueues[i].elementSize = tdsaAllShared->InboundQueueEleSize[i];
    QueueConfig->inboundQueues[i].priority = tdsaAllShared->InboundQueuePriority[i];
    QueueConfig->inboundQueues[i].reserved = 0;
  }
  for(i=0;i<QueueConfig->numOutboundQueues;i++)
  {
    QueueConfig->outboundQueues[i].elementCount = tdsaAllShared->OutboundQueueSize[i];
    QueueConfig->outboundQueues[i].elementSize = tdsaAllShared->OutboundQueueEleSize[i];
    QueueConfig->outboundQueues[i].interruptDelay = tdsaAllShared->OutboundQueueInterruptDelay[i]; /* default 0; no interrupt delay */
    QueueConfig->outboundQueues[i].interruptCount = tdsaAllShared->OutboundQueueInterruptCount[i]; /* default 1*/
    QueueConfig->outboundQueues[i].interruptEnable = tdsaAllShared->OutboundQueueInterruptEnable[i]; /* default 1*/
    QueueConfig->outboundQueues[i].interruptVectorIndex = 0;
  }
  /*  default  */
  for (i=0;i<8;i++)
  {
    QueueConfig->sasHwEventQueue[i] = 0;
    QueueConfig->sataNCQErrorEventQueue[i] = 0;
  }

#ifdef TARGET_DRIVER
  for (i=0;i<8;i++)
  {
    QueueConfig->tgtITNexusEventQueue[i] = 0;
    QueueConfig->tgtSSPEventQueue[i] = 0;
    QueueConfig->tgtSMPEventQueue[i] = 0;
  }
#endif
  QueueConfig->iqNormalPriorityProcessingDepth = 0;
  QueueConfig->iqHighPriorityProcessingDepth = 0;
  QueueConfig->generalEventQueue = 0;

  return;
}

/*****************************************************************************
*! \brief  tdssGetMaxTargetsParams
*
*  Purpose: This function is called to get default parameters from the 
*           OS Specific area. This function is called in the context of 
*           tiCOMGetResource() and tiCOMInit().
*
*
*  \param  tiRoot:   Pointer to initiator driver/port instance.
*  \param  option:   Pointer to bit32 where the max target number is saved
*
*  \return: None
*
*  \note -
*
*****************************************************************************/
osGLOBAL void 
tdssGetMaxTargetsParams(
                      tiRoot_t                *tiRoot, 
                      bit32                   *pMaxTargets
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
  char    iniParmsStr[]   = "InitiatorParms";
  bit32   MaxTargets;

  TI_DBG6(("tdssGetMaxTargetsParams: start\n"));
  
  *pMaxTargets = DEFAULT_MAX_DEV;
 
  /* to remove compiler warnings */ 
  pLastUsedChar   = pLastUsedChar;
  lenRecv         = lenRecv;
  subkey2         = subkey2;
  subkey1         = subkey1;
  key             = key;
  buffer          = &tmpBuffer[0];
  buffLen         = sizeof (tmpBuffer);

  osti_memset(buffer, 0, buffLen); 

  /* defaults are overwritten in the following */
  /* Get MaxTargets */ 
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
      MaxTargets = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      MaxTargets = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    *pMaxTargets = MaxTargets;
    TI_DBG2(("tdssGetMaxTargetsParams: MaxTargets %d\n", MaxTargets ));
  }
  
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  return;
}

/* temporary to distinguish SAS and SATA mode  */
osGLOBAL void 
tdssGetSATAOnlyModeParams(
                      tiRoot_t                *tiRoot, 
                      bit32                   *pSATAOnlyMode
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
  char    iniParmsStr[]   = "InitiatorParms";
  bit32   SATAOnlyMode;

  TI_DBG6(("tdssGetSATAOnlyModeParams: start\n"));
  
  *pSATAOnlyMode = agFALSE; /* default SAS and SATA */
 
  /* to remove compiler warnings */ 
  pLastUsedChar   = pLastUsedChar;
  lenRecv         = lenRecv;
  subkey2         = subkey2;
  subkey1         = subkey1;
  key             = key;
  buffer          = &tmpBuffer[0];
  buffLen         = sizeof (tmpBuffer);

  osti_memset(buffer, 0, buffLen); 

  /* defaults are overwritten in the following */
  /* Get SATAOnlyMode */ 
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "SATAOnlyMode",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SATAOnlyMode = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SATAOnlyMode = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    *pSATAOnlyMode = SATAOnlyMode;
  }
  
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  return;
}


