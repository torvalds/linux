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
 *
 *
 * This file contains CB functions used by lower layer in SAS/SATA TD layer
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

#ifdef ECHO_TESTING
/* temporary to test saEchoCommand() */
extern bit8 gEcho;
#endif

#if defined(SALLSDK_DEBUG)
extern bit32 gLLDebugLevel;
#endif


#include <dev/pms/RefTisa/sallsdk/spc/mpidebug.h>

#ifdef SA_ENABLE_TRACE_FUNCTIONS

#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'R'
#endif
/*
  functions that are common to SAS and SATA
*/

FORCEINLINE
void ossaCacheInvalidate(
                         agsaRoot_t  *agRoot,
                         void        *osMemHandle,
                         void        *virtPtr,
                         bit32       length
                         )
{
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG6(("ossaCacheInvalidate: start\n"));
  ostiCacheInvalidate(tiRoot, osMemHandle, virtPtr, length);
  return;
}

FORCEINLINE
void ossaCacheFlush(
               agsaRoot_t  *agRoot,
               void        *osMemHandle,
               void        *virtPtr,
               bit32       length
               )
{
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG6(("ossaCacheFlush: start\n"));
  ostiCacheFlush(tiRoot, osMemHandle, virtPtr, length);
  return;
}

FORCEINLINE
void ossaCachePreFlush(
                  agsaRoot_t  *agRoot,
                  void        *osMemHandle,
                  void        *virtPtr,
                  bit32       length
                   )

{
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG6(("ossaCachePreFlush: start\n"));
  ostiCachePreFlush(tiRoot, osMemHandle, virtPtr, length);
  return;
}

/*****************************************************************************
*! \brief ossaDeviceHandleAccept
*
*  Purpose:  This function is called by lower layer to inform TD layer of
*            a new SAS device arrival. Used only at the target
*
*
*  \param   agRoot         Pointer to chip/driver Instance.
*  \param   agDevHandle    Pointer to the device handle of the device
*  \param   agDevInfo      Pointer to the device info structure
*  \param   agPortContext  Pointer to a port context
*
*  \return:
*          OSSA_RC_REJECT  A device is accpeted
*          OSSA_RC_ACCEPT  A device is rejected
*
*  \note -  For details, refer to SAS/SATA Low-Level API Specification
*
*****************************************************************************/
osGLOBAL bit32 ossaDeviceHandleAccept(
                       agsaRoot_t          *agRoot,
                       agsaDevHandle_t     *agDevHandle,
                       agsaSASDeviceInfo_t *agDevInfo,
                       agsaPortContext_t   *agPortContext,
                       bit32               *hostAssignedDeviceId
                       )
{
#ifdef TARGET_DRIVER
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t           *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  tdsaPortContext_t    *onePortContext = agNULL;
  tiPortalContext_t    *tiPortalContext = agNULL;
  tdsaDeviceData_t     *oneDeviceData = agNULL;
  tiDeviceHandle_t     *tiDeviceHandle = agNULL;
  tdsaSASSubID_t       agSASSubID;
  bit32                option;
  bit32                param;
  /*
    at target only
    by default TD layer accpets all devices
  */
  /*
    at this point,
    by LINK_UP event tdsaPortContext should have been created
  */
  smTraceFuncEnter(hpDBG_VERY_LOUD, "Y0");
  TI_DBG1(("ossaDeviceHandleAccept: start hostAssignedDeviceId 0x%X\n",*hostAssignedDeviceId));


  if (agPortContext == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleAccept: NULL agsaPortContext; wrong\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y0");
    return OSSA_RC_REJECT;
  }


  onePortContext = (tdsaPortContext_t *)agPortContext->osData;

  if (onePortContext == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleAccept: NULL oneportcontext; wrong\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Y0");
    return OSSA_RC_REJECT;
  }

  tiPortalContext = (tiPortalContext_t *)onePortContext->tiPortalContext;

  if (tiPortalContext == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleAccept: NULL tiPortalContext; wrong\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Y0");
    return OSSA_RC_REJECT;
  }

  /*
    add the device to device list
    cf) OSSA_DISCOVER_FOUND_DEVICE
  */
  TI_DBG4(("ossaDeviceHandleAccept: sasAddressHi 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSHI(&agDevInfo->commonDevInfo)));
  TI_DBG4(("ossaDeviceHandleAccept: sasAddressLo 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSLO(&agDevInfo->commonDevInfo)));
  TI_DBG4(("ossaDeviceHandleAccept: device type  0x%x\n", DEVINFO_GET_DEVICETTYPE(&agDevInfo->commonDevInfo)));
  TI_DBG4(("ossaDeviceHandleAccept: phys %d\n", agDevInfo->numOfPhys));
  TI_DBG4(("ossaDeviceHandleAccept: pid %d\n", onePortContext->id));

  if (DEVINFO_GET_DEVICETTYPE(&agDevInfo->commonDevInfo) == SAS_END_DEVICE)
  {
    TI_DBG4(("ossaDeviceHandleAccept: SAS_END_DEVICE\n"));
  }
  else if (DEVINFO_GET_DEVICETTYPE(&agDevInfo->commonDevInfo) == SAS_EDGE_EXPANDER_DEVICE)
  {
    TI_DBG4(("ossaDeviceHandleAccept: SAS_EDGE_EXPANDER_DEVICE\n"));
  }
  else /* SAS_FANOUT_EXPANDER_DEVICE */
  {
    TI_DBG4(("ossaDeviceHandleAccept: SAS_FANOUT_EXPANDER_DEVICE\n"));
  }
  agSASSubID.sasAddressHi = SA_DEVINFO_GET_SAS_ADDRESSHI(&agDevInfo->commonDevInfo);
  agSASSubID.sasAddressLo = SA_DEVINFO_GET_SAS_ADDRESSLO(&agDevInfo->commonDevInfo);
  agSASSubID.initiator_ssp_stp_smp = agDevInfo->initiator_ssp_stp_smp;
  agSASSubID.target_ssp_stp_smp = agDevInfo->target_ssp_stp_smp;


  tdssAddSASToSharedcontext(
                            onePortContext,
                            agRoot,
                            agDevHandle,
                            &agSASSubID,
                            agTRUE,
                            0xFF,
                            TD_OPERATION_TARGET
                            );

  /* at this point devicedata for new device exists */
  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleAccept: NULL oneDeviceData; wrong\n"));
    return OSSA_RC_REJECT;
  }

  oneDeviceData->registered = agTRUE;

  tiDeviceHandle = &(oneDeviceData->tiDeviceHandle);

  if (tiDeviceHandle == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleAccept: NULL tiDeviceHandle; wrong\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "Y0");
    return OSSA_RC_REJECT;
  }

  /* setting MCN in agsaDeviceInfo_t*/
  agDevInfo->commonDevInfo.flag = agDevInfo->commonDevInfo.flag | (tdsaAllShared->MCN << 16);
  /* increment RegisteredDevNums */
  onePortContext->RegisteredDevNums++;

  *hostAssignedDeviceId |= 0xBEEF0000;

  TI_DBG1(("ossaDeviceHandleAccept: Now hostAssignedDeviceId 0x%X\n", *hostAssignedDeviceId));


  /* no login in SAS */
  /*
    osGLOBAL bit32 ostiTargetEvent (
                        tiRoot_t          *tiRoot,
                        tiPortalContext_t *portalContext,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        tiTgtEventType_t  eventType,
                        bit32             eventStatus,
                        void              *parm
                        );
  */

  ostiTargetEvent(
                  tiRoot,
                  tiPortalContext,
                  tiDeviceHandle,
                  tiTgtEventTypeDeviceChange,
                  tiDeviceArrival,
                  agNULL
                  );
  /* set MCN and initiator role bit using saSetDeviceInfo */
  option = 24; /* setting MCN and initiator role 1 1000b*/
  param = (1 << 18) | (tdsaAllShared->MCN << 24);
  TI_DBG1(("ossaDeviceHandleAccept: option 0x%x param 0x%x MCN 0x%x\n", option, param, tdsaAllShared->MCN));
  saSetDeviceInfo(agRoot, agNULL, 0, agDevHandle, option, param, ossaSetDeviceInfoCB);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "Y0");
  return OSSA_RC_ACCEPT;
#endif

#ifdef INITIATOR_DRIVER
  /* this function is not used in case of Initiator */
  return OSSA_RC_ACCEPT;
#endif
}

#ifdef INITIATOR_DRIVER
/*****************************************************************************
*! \brief ossaDiscoverSasCB
*
*  Purpose:  This function is called by lower layer to inform TD layer of
*            SAS discovery results
*
*
*  \param   agRoot         Pointer to chip/driver Instance.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   event          event type
*  \param   pParm1         Pointer to data associated with event
*  \param   pParm2         Pointer to data associated with event
*
*  \return: none
*
*  \note -  For details, refer to SAS/SATA Low-Level API Specification
*
*****************************************************************************/
osGLOBAL void ossaDiscoverSasCB(agsaRoot_t        *agRoot,
                  agsaPortContext_t *agPortContext,
                  bit32             event,
                  void              *pParm1,
                  void              *pParm2
                  )
{
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)osData->tdsaAllShared;

  tdsaPortContext_t    *onePortContext = agNULL;
  tdsaDeviceData_t     *oneDeviceData = agNULL;

  agsaDevHandle_t      *agDevHandle = agNULL;
  agsaSASDeviceInfo_t  *agDeviceInfo = agNULL;
  tiPortalContext_t    *tiPortalContext = agNULL;
  tdList_t             *DeviceListList;
  tdsaSASSubID_t       agSASSubID;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y1");
  TI_DBG2(("ossaDiscoverSasCB: start\n"));

  if (agPortContext == agNULL)
  {
    TI_DBG1(("ossaDiscoverSasCB: NULL agsaPortContext; wrong\n"));
    return;
  }

  onePortContext = (tdsaPortContext_t *)agPortContext->osData;
  tiPortalContext = (tiPortalContext_t *)onePortContext->tiPortalContext;

  switch ( event )
  {
  case OSSA_DISCOVER_STARTED:
  {
    TI_DBG3(("ossaDiscoverSasCB: STARTED pid %d\n", onePortContext->id));
    /*
      invalidate all devices in current device list
    */
    DeviceListList = tdsaAllShared->MainDeviceList.flink;
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      TI_DBG3(("ossaDiscoverSasCB: loop did %d\n", oneDeviceData->id));
      TI_DBG3(("ossaDiscoverSasCB: loop sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG6(("ossaDiscoverSasCB: loop sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
      if (oneDeviceData->tdPortContext == onePortContext)
      {
        TI_DBG3(("ossaDiscoverSasCB: did %d is invalidated \n", oneDeviceData->id));
        /* temporary solution: only for sata direct attached */
      }
      DeviceListList = DeviceListList->flink;
    }
    onePortContext->DiscoveryState = ITD_DSTATE_STARTED;
    break;
  }

  case OSSA_DISCOVER_FOUND_DEVICE:
  {
    TI_DBG4(("ossaDiscoverSasCB: $$$$$ FOUND_DEVICE pid %d\n", onePortContext->id));
    agDevHandle = (agsaDevHandle_t *)pParm1;
    agDeviceInfo = (agsaSASDeviceInfo_t *)pParm2;
    TI_DBG5(("ossaDiscoverSasCB: sasAddressHi 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSHI(&agDeviceInfo->commonDevInfo)));
    TI_DBG5(("ossaDiscoverSasCB: sasAddressLo 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSLO(&agDeviceInfo->commonDevInfo)));
    TI_DBG5(("ossaDiscoverSasCB: device type  0x%x\n", DEVINFO_GET_DEVICETTYPE(&agDeviceInfo->commonDevInfo)));

    TI_DBG6(("ossaDiscoverSasCB: phys %d\n", agDeviceInfo->numOfPhys));
    TI_DBG4(("ossaDiscoverSasCB: pid %d\n", onePortContext->id));


    /* Add only target devices; do not add expander device */
    if (DEVINFO_GET_DEVICETTYPE(&agDeviceInfo->commonDevInfo) == SAS_END_DEVICE)
    {
      agSASSubID.sasAddressHi = SA_DEVINFO_GET_SAS_ADDRESSHI(&agDeviceInfo->commonDevInfo);
      agSASSubID.sasAddressLo = SA_DEVINFO_GET_SAS_ADDRESSLO(&agDeviceInfo->commonDevInfo);
      agSASSubID.initiator_ssp_stp_smp = agDeviceInfo->initiator_ssp_stp_smp;
      agSASSubID.target_ssp_stp_smp = agDeviceInfo->target_ssp_stp_smp;

      TI_DBG2(("ossaDiscoverSasCB: adding ....\n"));

      tdssAddSASToSharedcontext(
                                onePortContext,
                                agRoot,
                                agDevHandle,
                                &agSASSubID,
                                agTRUE,
                                agDeviceInfo->phyIdentifier, 
                                TD_OPERATION_INITIATOR
                                );
      ostiInitiatorEvent(
                         tiRoot,
                         tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceArrival,
                         agNULL
                         );
    }
    else
    {
      TI_DBG5(("ossaDiscoverSasCB: $$$$$ not end device. not adding....\n"));
    }


    break;
  }

  case OSSA_DISCOVER_REMOVED_DEVICE:
  {
    TI_DBG3(("ossaDiscoverSasCB: REMOVED_DEVICE\n"));
    agDevHandle = (agsaDevHandle_t *)pParm1;
    agDeviceInfo = (agsaSASDeviceInfo_t *)pParm2;
    oneDeviceData = (tdsaDeviceData_t *) agDevHandle->osData;

    TI_DBG6(("ossaDiscoverSasCB: sasAddressHi 0x%08x\n",
             SA_DEVINFO_GET_SAS_ADDRESSHI(&agDeviceInfo->commonDevInfo)));
    TI_DBG6(("ossaDiscoverSasCB: sasAddressLo 0x%08x\n",
             SA_DEVINFO_GET_SAS_ADDRESSLO(&agDeviceInfo->commonDevInfo)));
    TI_DBG6(("ossaDiscoverSasCB: phys %d\n", agDeviceInfo->numOfPhys));
    TI_DBG6(("ossaDiscoverSasCB: onePortContext->id %d\n", onePortContext->id));

    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("ossaDiscoverSasCB: Wrong. DevHandle->osData is NULL but is being removed\n"));
    }
    else
    {
      tdssRemoveSASFromSharedcontext(onePortContext,
                                     oneDeviceData,
                                     agRoot);
      agDevHandle->osData = agNULL;
      ostiInitiatorEvent(
                         tiRoot,
                         tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceRemoval,
                         agNULL
                         );
    }

    break;
  }
  case OSSA_DISCOVER_COMPLETE:
  {
    TI_DBG2(("ossaDiscoverSasCB: SAS COMPLETE pid %d\n", onePortContext->id));
    /*
      note:
      SAS discovery must be called before SATA discovery
      "onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED" is
      in ossaDiscoverSataCB not in ossaDiscoverSasCB when SATA_ENABLE
    */
#ifndef SATA_ENABLE
    onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
    TI_DBG6(("ossaDiscoverSasCB: COMPLETE pid %d\n", onePortContext->id));
#endif

#ifdef SATA_ENABLE
    TI_DBG2(("ossaDiscoverSasCB: calling SATA discovery\n"));

    /* Continue with SATA discovery */
    saDiscover(agRoot, agPortContext, AG_SA_DISCOVERY_TYPE_SATA,
               onePortContext->discoveryOptions);

#else /* SATA not enable */

#ifdef TD_INTERNAL_DEBUG /* for debugging */
    /* dump device list */
    DeviceListList = tdsaAllShared->MainPortContextList.flink;

    while (DeviceListList != &(tdsaAllShared->MainPortContextList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      TI_DBG2(("ossaDiscoverSasCB: did %d valid %d\n", oneDeviceData->id, oneDeviceData->valid));
      TI_DBG2(("ossaDiscoverSasCB: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG2(("ossaDiscoverSasCB: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
      DeviceListList = DeviceListList->flink;
    }
#endif

    /* letting OS layer know discovery has been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscOK,
                       agNULL
                       );
#endif  /* SATA_ENABLE */

    break;
  }
  case OSSA_DISCOVER_ABORT:
  {
    TI_DBG3(("ossaDiscoverSasCB: ABORT\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_1:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 1\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }

  case OSSA_DISCOVER_ABORT_ERROR_2:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 2\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }

  case OSSA_DISCOVER_ABORT_ERROR_3:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 3\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_4:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 4\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
        break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_5:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 5\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_6:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 6\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_7:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 7\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_8:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 8\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  case OSSA_DISCOVER_ABORT_ERROR_9:
  {
    TI_DBG3(("ossaDiscoverSasCB: ERROR 9\n"));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  }
  default:
    TI_DBG3(("ossaDiscoverSasCB: ERROR default event 0x%x\n", event));
    /* letting OS layer know discovery has not been successfully complete */
    ostiInitiatorEvent(
                       tiRoot,
                       tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
    break;
  } /* end of switch */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y1");
  return;
}
#endif // #ifdef INITIATOR_DRIVER

osGLOBAL void ossaLogTrace0(
               agsaRoot_t           *agRoot,
               bit32               traceCode
               )
{
  return;
}

osGLOBAL void ossaLogTrace1(
               agsaRoot_t           *agRoot,
               bit32               traceCode,
               bit32               value1
               )
{
  return;
}

osGLOBAL void ossaLogTrace2(
               agsaRoot_t           *agRoot,
               bit32               traceCode,
               bit32               value1,
               bit32               value2
               )
{
  return;
}

osGLOBAL void ossaLogTrace3(
               agsaRoot_t           *agRoot,
               bit32               traceCode,
               bit32               value1,
               bit32               value2,
               bit32               value3
               )
{
  return;
}


osGLOBAL void
ossaLogTrace4(
               agsaRoot_t           *agRoot,
               bit32               traceCode,
               bit32               value1,
               bit32               value2,
               bit32               value3,
               bit32               value4
               )
{
  return;
}


/*****************************************************************************
*! \brief ossaHwCB
*
*  Purpose:  This function is called by lower layer to inform TD layer of
*            HW related results
*
*  \param   agRoot         Pointer to chip/driver Instance.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   event          event type
*  \param   eventParm1     event-specific parameter
*  \param   eventParm2     event-specific parameter
*  \param   eventParm3     event-specific parameter of pointer type
*
*  \return: none
*
*  \note -  For details, refer to SAS/SATA Low-Level API Specification
*
*****************************************************************************/
osGLOBAL void ossaHwCB(
         agsaRoot_t        *agRoot,
         agsaPortContext_t *agPortContext,
         bit32             event,
         bit32             eventParm1,
         void              *eventParm2,
         void              *eventParm3
         )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaContext_t       *tdsaAllShared = (tdsaContext_t *)osData->tdsaAllShared;
  tdList_t            *PortContextList = agNULL;
  tdsaPortContext_t   *onePortContext = agNULL;
  agsaDevHandle_t     *agDevHandle = agNULL;
  agsaSASIdentify_t   *IDframe = agNULL;
  int                 i = 0;
#ifdef INITIATOR_DRIVER
  tdsaSASSubID_t      agSASSubID;
#endif
  bit32               PhyID;
  bit32               PhyStatus;
  bit32               LinkRate;
  bit32               PortState;
  bit32               HwAckSatus = AGSA_RC_SUCCESS;

// #ifdef INITIATOR_DRIVER
#ifdef INITIATOR_DRIVER
  agsaFisRegDeviceToHost_t *RegD2H = agNULL;
  tdsaDeviceData_t         *oneDeviceData = agNULL;
  tdList_t                 *DeviceListList;
#endif
#ifdef REMOVED
  bit32                    found = agFALSE;
#endif
  agsaHWEventEncrypt_t     *pEncryptCBData;
  agsaEncryptInfo_t        *pEncryptInfo;
  agsaHWEventMode_t        *pModeEvent;
  tiEncryptPort_t          encryptEventData;
  tiEncryptInfo_t          encryptInfo;
  bit32                    *pModePage;
  bit32                    securityMode;
  bit32                    cipherMode;
  bit32                    encryptStatus;
  bit32                    securitySetModeStatus;
  bit32                    securityModeStatus;

// #endif /* INITIATOR_DRIVER */
  agsaPhyErrCountersPage_t *agPhyErrCountersPage;
  agsaEventSource_t        eventSource;

#ifdef FDS_DM
  dmRoot_t                 *dmRoot = &(tdsaAllShared->dmRoot);
  dmPortContext_t          *dmPortContext = agNULL;
  bit32                    status = DM_RC_FAILURE;
  dmPortInfo_t             dmPortInfo;
//  bit32                    discStatus = dmDiscInProgress;
#endif

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y2");

  TI_DBG2(("ossaHwCB: agPortContext %p event 0x%x eventParm1 0x%x eventParm2 %p eventParm3 %p\n",
                      agPortContext,event,eventParm1,eventParm2,eventParm3 ));

  switch ( event )
  {
  case OSSA_HW_EVENT_SAS_PHY_UP:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    LinkRate = TD_GET_LINK_RATE(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agDevHandle = agNULL;
    IDframe = (agsaSASIdentify_t *)eventParm3;


    TI_DBG2(("ossaHwCB: Phy%d SAS link Up\n", PhyID));

    if (agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: agPortContext null, wrong\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y2");
      return;
    }
    if (agDevHandle == agNULL)
    {
      TI_DBG3(("ossaHwCB: agDevHandle null by design change\n"));
    }

    if (IDframe == agNULL)
    {
      TI_DBG1(("ossaHwCB: IDframe null, wrong\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Y2");
      return;
    }
    /* debugging only */
    if (LinkRate == 0x01)
    {
      TI_DBG1(("ossaHwCB: SAS Link Rate is 1.5 Gbps PhyID %d\n",PhyID));
    }
    if (LinkRate == 0x02)
    {
      TI_DBG1(("ossaHwCB: SAS Link Rate is 3.0 Gbps PhyID %d\n",PhyID));
    }
    if (LinkRate == 0x04)
    {
      TI_DBG1(("ossaHwCB: SAS Link Rate is 6.0 Gbps PhyID %d\n",PhyID));
    }
    if (LinkRate == 0x08)
    {
      TI_DBG1(("ossaHwCB: SAS Link Rate is 12.0 Gbps PhyID %d\n",PhyID));
    }

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with SAS link up\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Y2");
      return;
    }

    if ( agPortContext->osData == agNULL)
    {/* if */
      TI_DBG6 (("ossaHwCB: PhyID %d tdsaAllShared %p\n", PhyID, tdsaAllShared));
      if (tdsaAllShared->Ports[PhyID].tiPortalContext == agNULL)
      {
        TI_DBG6(("ossaHwCB: NULL portalcontext\n"));
      }
      else
      {
        TI_DBG6(("ossaHwCB: NOT NULL portalcontext\n"));
      }

      if (IDframe == agNULL)
      {
        TI_DBG1(("ossaHwCB: IDFrame is NULL; SATA !!!!\n"));
      }
      else
      {
        TI_DBG3(("ossaHwCB: IDframe->sasAddressHi 0x%08x \n",
                 SA_IDFRM_GET_SAS_ADDRESSHI(IDframe)));
        TI_DBG3(("ossaHwCB: IDframe->sasAddressLo 0x%08x \n",
                 SA_IDFRM_GET_SAS_ADDRESSLO(IDframe)));

      }
      /*
        setting tdsaPortContext fields
        take the head from the FreeLink of tdsaPortContext_t
        then modify it
        then put it in MainLink of tdsaPortContext_t
      */
      tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
      if (TDLIST_NOT_EMPTY(&(tdsaAllShared->FreePortContextList)))
      {
        TDLIST_DEQUEUE_FROM_HEAD(&PortContextList, &(tdsaAllShared->FreePortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, FreeLink, PortContextList);
        TI_DBG2(("ossaHwCB: pid %d\n", onePortContext->id));
        TI_DBG6(("ossaHwCB: onePortContext %p\n", onePortContext));
        if (onePortContext == agNULL)
        {
          TI_DBG1(("ossaHwCB: onePortContext is NULL in allocation, wrong!\n"));
          return;
        }

        /* sets fields of tdsaportcontext */
#ifdef INITIATOR_DRIVER
        onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
        onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_FULL_START;
#endif
        onePortContext->PhyIDList[PhyID] = agTRUE;
        if (IDframe == agNULL)
        {
          onePortContext->sasRemoteAddressHi = 0xFFFFFFFF;
          onePortContext->sasRemoteAddressLo = 0xFFFFFFFF;
          onePortContext->directAttatchedSAS = agTRUE;
        }
        else
        {
          onePortContext->sasRemoteAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(IDframe);
          onePortContext->sasRemoteAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(IDframe);
          /* Create ID frame and storing ID frame */
          osti_memcpy(&onePortContext->sasIDframe, IDframe, sizeof(agsaSASIdentify_t));
          tdhexdump("ossaHWCB: sasIDframe", (bit8 *)(&onePortContext->sasIDframe), sizeof(agsaSASIdentify_t));
          if (SA_IDFRM_GET_DEVICETTYPE(IDframe) == SAS_END_DEVICE)
          {
            onePortContext->directAttatchedSAS = agTRUE;
          }
#ifdef FDS_DM
          if (SA_IDFRM_GET_DEVICETTYPE(IDframe) == SAS_EDGE_EXPANDER_DEVICE ||
              SA_IDFRM_GET_DEVICETTYPE(IDframe) == SAS_FANOUT_EXPANDER_DEVICE
             )
          {
            onePortContext->UseDM = agTRUE;
          }
#endif
        }

        onePortContext->sasLocalAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&tdsaAllShared->Ports[PhyID].SASID);
        onePortContext->sasLocalAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&tdsaAllShared->Ports[PhyID].SASID);
        onePortContext->tiPortalContext = tdsaAllShared->Ports[PhyID].tiPortalContext;
        onePortContext->agRoot = agRoot;
        onePortContext->agPortContext = agPortContext;
        tdsaAllShared->Ports[PhyID].portContext = onePortContext;
        agPortContext->osData = onePortContext;
        onePortContext->valid = agTRUE;
        if (LinkRate == 0x01)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_1_5G;
        }
        else if (LinkRate == 0x02)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_3_0G;
        }
        else if (LinkRate == 0x04)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_6_0G;
        }
        else /* (LinkRate == 0x08) */
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_12_0G;
        }

        tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
        TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->MainLink), &(tdsaAllShared->MainPortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
#ifdef FDS_DM
        dmPortContext = &(onePortContext->dmPortContext);
        dmPortContext->tdData = onePortContext;
        /* set up dmPortInfo_t */
        PORTINFO_PUT_SAS_REMOTE_ADDRESSLO(&dmPortInfo, onePortContext->sasRemoteAddressLo);
        PORTINFO_PUT_SAS_REMOTE_ADDRESSHI(&dmPortInfo, onePortContext->sasRemoteAddressHi);
        PORTINFO_PUT_SAS_LOCAL_ADDRESSLO(&dmPortInfo, onePortContext->sasLocalAddressLo);
        PORTINFO_PUT_SAS_LOCAL_ADDRESSHI(&dmPortInfo, onePortContext->sasLocalAddressHi);

        TI_DBG2(("ossaHwCB: phy %d hi 0x%x lo 0x%x\n", PhyID,
                 SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[PhyID].SASID)),
                 SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[PhyID].SASID))));
        TI_DBG2(("ossaHwCB: LocalAddrHi 0x%08x LocaAddrLo 0x%08x\n", onePortContext->sasLocalAddressHi, onePortContext->sasLocalAddressLo));

        dmPortInfo.flag = onePortContext->LinkRate;

        if (onePortContext->UseDM == agTRUE)
        {
          TI_DBG1(("ossaHwCB: calling dmCreatePort\n"));
          status = dmCreatePort(dmRoot, dmPortContext, &dmPortInfo);
          if (status != DM_RC_SUCCESS)
          {
            TI_DBG1(("ossaHwCB: dmCreatePort failed!!! 0x%x\n", status));
          }
        }
#endif

      }
      else
      {
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        TI_DBG1(("\nossaHwCB: Attention!!! no more free PortContext.\n"));
      }
#ifdef TD_INTERNAL_DEBUG     /* for debugging only */

      print_tdlist_flink(&(tdsaPortContext->FreeLink), 1, 1);
      print_tdlist_flink(&(tdsaPortContext->MainLink), 1, 2);
      print_tdlist_flink(&(tdsaDeviceData->FreeLink), 2, 1);
      print_tdlist_flink(&(tdsaDeviceData->MainLink), 2, 2);
#endif

#ifdef TD_INTERNAL_DEBUG      /* for debugging */
      PortContextList = tdsaPortContext->MainLink.flink;
      while (PortContextList != &(tdsaPortContext->MainLink))
      {
        twoPortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
        TI_DBG6(("ossaHwCB: in while portContext ID %d\n", twoPortContext->id));
        TI_DBG6(("ossaHwCB: in while PortContext %p\n", twoPortContext));
        PortContextList = PortContextList->flink;
      }
#endif
      /* add agDevHandle */
      if (SA_IDFRM_GET_DEVICETTYPE(IDframe) != SAS_NO_DEVICE)
      {
#ifdef INITIATOR_DRIVER
        agSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(IDframe);
        agSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(IDframe);
        agSASSubID.initiator_ssp_stp_smp = IDframe->initiator_ssp_stp_smp;
        agSASSubID.target_ssp_stp_smp = IDframe->target_ssp_stp_smp;
#endif

        TI_DBG2(("ossaHwCB: adding ....\n"));
        /* uses only SASIDframe not agsaSASDeviceInfo_t */
#ifdef INITIATOR_DRIVER
        tdssAddSASToSharedcontext(
                                  onePortContext,
                                  agRoot,
                                  agDevHandle, /* agNULL */
                                  &agSASSubID,
                                  agTRUE,
                                  (bit8)PhyID,
                                  TD_OPERATION_INITIATOR
                                  );
#endif

#ifdef FDS_DM
        if (SA_IDFRM_GET_DEVICETTYPE(IDframe) == SAS_END_DEVICE &&
            SA_IDFRM_IS_SSP_TARGET(IDframe) )
        {
          TI_DBG2(("ossaHwCB: NOTIFY_ENABLE_SPINUP PhyID %d \n", PhyID));
         
          for (i=0;i<TD_MAX_NUM_NOTIFY_SPINUP;i++)
          {
            saLocalPhyControl(agRoot, agNULL, 0, PhyID, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
          }
        }

        /* update MCN */
        tdsaUpdateMCN(dmRoot, onePortContext);
#endif

#ifdef TARGET_DRIVER
        TI_DBG1(("ossaHwCB: target, link up PhyID 0x%x\n",PhyID));

        /* notifying link up */
        ostiPortEvent (
                       tiRoot,
                       tiPortLinkUp,
                       tiSuccess,
                       (void *)tdsaAllShared->Ports[PhyID].tiPortalContext
                       );
#endif
      }
      else
      {
        TI_DBG5(("ossaHwCB: $$$$$ not end device. not adding....\n"));
      }

      saPortControl(agRoot, /* AGSA_PORT_SET_PORT_RECOVERY_TIME */
                    agNULL,
                    0,
                    agPortContext,
                    AGSA_PORT_SET_PORT_RECOVERY_TIME,
                    tdsaAllShared->portTMO, //PORT_RECOVERY_TIMEOUT
                    0
                    );
      /* setting SAS PORT RESET TMO and SATA PORT RESET TMO*/
      if (tIsSPCV12G(agRoot))
      {
        saPortControl(agRoot, /* AGSA_PORT_SET_PORT_RESET_TIME */
                      agNULL,
                      0,
                      agPortContext,
                      AGSA_PORT_SET_PORT_RESET_TIME,
                      SAS_12G_PORT_RESET_TMO, // 800 ms
                      0
                      );
      }
      else
      {
        saPortControl(agRoot, /* AGSA_PORT_SET_PORT_RESET_TIME */
                      agNULL,
                      0,
                      agPortContext,
                      AGSA_PORT_SET_PORT_RESET_TIME,
                      SAS_PORT_RESET_TMO, // 300 ms
                      0
                      );
      }
    }
    else
    {
      /*
        an existing portcontext
        to be tested
      */

      TI_DBG2(("ossaHwCB: SAS existing portcontext returned\n"));

      onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
      if (onePortContext == agNULL)
      {
        TI_DBG1(("ossaHwCB: onePortContext is NULL, wrong!\n"));
        return;
      }
      if (onePortContext->valid == agFALSE)
      {
        /* port has been invalidated; needs to be allocated */
        TI_DBG2(("ossaHwCB: SAS allocating port context\n"));

        tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
        if (TDLIST_NOT_EMPTY(&(tdsaAllShared->FreePortContextList)))
        {
          TDLIST_DEQUEUE_FROM_HEAD(&PortContextList, &(tdsaAllShared->FreePortContextList));
          tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
          onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, FreeLink, PortContextList);
          TI_DBG2(("ossaHwCB: allocating pid %d\n", onePortContext->id));
          TI_DBG6(("ossaHwCB: allocating onePortContext %p\n", onePortContext));
          if (onePortContext == agNULL)
          {
            TI_DBG1(("ossaHwCB: onePortContext is NULL in allocation, wrong!\n"));
            return;
          }
          /* sets fields of tdsaportcontext */
#ifdef INITIATOR_DRIVER
          onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
          onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_FULL_START;
#endif
          onePortContext->PhyIDList[PhyID] = agTRUE;
          if (IDframe == agNULL)
          {
            onePortContext->sasRemoteAddressHi = 0xFFFFFFFF;
            onePortContext->sasRemoteAddressLo = 0xFFFFFFFF;
            onePortContext->directAttatchedSAS = agTRUE;
          }
          else
          {
            onePortContext->sasRemoteAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(IDframe);
            onePortContext->sasRemoteAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(IDframe);
            /* Create ID frame and storing ID frame */
            osti_memcpy(&onePortContext->sasIDframe, IDframe, sizeof(agsaSASIdentify_t));
            tdhexdump("ossaHWCB: sasIDframe", (bit8 *)(&onePortContext->sasIDframe), sizeof(agsaSASIdentify_t));
            if (SA_IDFRM_GET_DEVICETTYPE(IDframe) == SAS_END_DEVICE)
            {
              onePortContext->directAttatchedSAS = agTRUE;
            }
          }

          onePortContext->sasLocalAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&tdsaAllShared->Ports[PhyID].SASID);
          onePortContext->sasLocalAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&tdsaAllShared->Ports[PhyID].SASID);
          onePortContext->tiPortalContext = tdsaAllShared->Ports[PhyID].tiPortalContext;
          onePortContext->agRoot = agRoot;
          onePortContext->agPortContext = agPortContext;
          tdsaAllShared->Ports[PhyID].portContext = onePortContext;
          agPortContext->osData = onePortContext;
          onePortContext->valid = agTRUE;
          if (LinkRate == 0x01)
          {
            onePortContext->LinkRate = SAS_CONNECTION_RATE_1_5G;
          }
          else if (LinkRate == 0x02)
          {
            onePortContext->LinkRate = SAS_CONNECTION_RATE_3_0G;
          }
          else if (LinkRate == 0x04)
          {
            onePortContext->LinkRate = SAS_CONNECTION_RATE_6_0G;
          }
          else /* (LinkRate == 0x08) */
          {
            onePortContext->LinkRate = SAS_CONNECTION_RATE_12_0G;
          }
          tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
          TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->MainLink), &(tdsaAllShared->MainPortContextList));
          tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        }
        else
        {
          tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
          TI_DBG1(("\nossaHwCB: Attention!!! no more free PortContext.\n"));
          smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "Y2");
          return;
        }
      } /* invalidated port */
      else
      {
        /* already alloacated */
        TI_DBG2(("ossaHwCB: SAS already allocated port context\n"));
        if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
        {
          TI_DBG1(("ossaHwCB: wrong!!!  null tdsaPortContext list\n"));
          smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "Y2");
          return;
        }
        if (onePortContext == agNULL)
        {
          TI_DBG1(("ossaHwCB: wrong !!! No corressponding tdsaPortContext\n"));
          smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "Y2");
          return;
        }

        TI_DBG2(("ossaHwCB: existing pid %d\n", onePortContext->id));
        if (tdsaAllShared->Ports[PhyID].portContext == agNULL)
        {
          TI_DBG1(("ossaHwCB: existing allshared pid is NULL\n"));
        }
        else
        {
          TI_DBG2(("ossaHwCB: existing allshared pid %d\n", tdsaAllShared->Ports[PhyID].portContext->id));
        }
        /* updates PhyID belong to a port */
        onePortContext->PhyIDList[PhyID] = agTRUE;
#ifdef FDS_DM
        if (SA_IDFRM_GET_DEVICETTYPE(IDframe) == SAS_END_DEVICE &&
            SA_IDFRM_IS_SSP_TARGET(IDframe) )
        {
          TI_DBG2(("ossaHwCB: NOTIFY_ENABLE_SPINUP PhyID %d \n", PhyID));
          
          for (i=0;i<TD_MAX_NUM_NOTIFY_SPINUP;i++)
          {
            saLocalPhyControl(agRoot, agNULL, 0, PhyID, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
          }
        }

        /* update MCN */
        tdsaUpdateMCN(dmRoot, onePortContext);
#endif
      }
      onePortContext->SeenLinkUp = agTRUE;
    } /* else, old portcontext */

    break;
  }
#ifdef INITIATOR_DRIVER
  case OSSA_HW_EVENT_SATA_PHY_UP:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    LinkRate = TD_GET_LINK_RATE(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agDevHandle = agNULL;
    RegD2H = ( agsaFisRegDeviceToHost_t *)eventParm3;

    TI_DBG2(("ossaHwCB: Phy%d SATA link Up\n", PhyID));

    if (agDevHandle == agNULL)
    {
      TI_DBG3(("ossaHwCB: agDevHandle null by design change\n"));
    }

    if (RegD2H == agNULL)
    {
      TI_DBG1(("ossaHwCB: RegD2H null, wrong\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "Y2");
      return;
    }


    TI_DBG2(("ossaHwCB: agDevHandle %p\n", agDevHandle));
    tdhexdump("ossaHWCB RegD2H", (bit8 *)RegD2H, sizeof(agsaFisRegDeviceToHost_t));
    TI_DBG2(("ossaHwCB: Sector Count %d\n", RegD2H->d.sectorCount));
    TI_DBG2(("ossaHwCB: LBA LOW %d\n", RegD2H->d.lbaLow));
    TI_DBG2(("ossaHwCB: LBA MID  %d\n", RegD2H->d.lbaMid));
    TI_DBG2(("ossaHwCB: LBA HIGH  %d\n", RegD2H->d.lbaHigh));
    TI_DBG2(("ossaHwCB: DEVICE  %d\n", RegD2H->d.device));

    /* debugging only */
    if (LinkRate == 0x01)
    {
      TI_DBG1(("ossaHwCB: SATA Link Rate is 1.5 Gbps PhyID %d\n",PhyID));
    }
    if (LinkRate == 0x02)
    {
      TI_DBG1(("ossaHwCB: SATA Link Rate is 3.0 Gbps PhyID %d\n",PhyID));
    }
    if (LinkRate == 0x04)
    {
      TI_DBG1(("ossaHwCB: SATA Link Rate is 6.0 Gbps PhyID %d\n",PhyID));
    }
    if (LinkRate == 0x08)
    {
      TI_DBG1(("ossaHwCB: SATA Link Rate is 12.0 Gbps PhyID %d\n",PhyID));
    }

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with SATA link up\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "Y2");
      return;
    }

    if ( agPortContext->osData == agNULL)
    {/* if */
      TI_DBG6 (("ossaHwCB: PhyID %d tdsaAllShared %p\n", PhyID, tdsaAllShared));
      tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
      if (TDLIST_NOT_EMPTY(&(tdsaAllShared->FreePortContextList)))
      {
        TDLIST_DEQUEUE_FROM_HEAD(&PortContextList, &(tdsaAllShared->FreePortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, FreeLink, PortContextList);
        TI_DBG2(("ossaHwCB: pid %d\n", onePortContext->id));
        TI_DBG6(("ossaHwCB: onePortContext %p\n", onePortContext));
        if (onePortContext == agNULL)
        {
          TI_DBG1(("ossaHwCB: onePortContext is NULL in allocation, wrong!\n"));
          return;
        }

        /* sets fields of tdsaportcontext */
        onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
        onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_FULL_START;
        onePortContext->PhyIDList[PhyID] = agTRUE;
        /* NO sas address for SATA */
        onePortContext->sasRemoteAddressHi = 0xFFFFFFFF;
        onePortContext->sasRemoteAddressLo = 0xFFFFFFFF;
        /* copying the signature */
        onePortContext->remoteSignature[0] = RegD2H->d.sectorCount;
        onePortContext->remoteSignature[1] = RegD2H->d.lbaLow;
        onePortContext->remoteSignature[2] = RegD2H->d.lbaMid;
        onePortContext->remoteSignature[3] = RegD2H->d.lbaHigh;
        onePortContext->remoteSignature[4] = RegD2H->d.device;

        onePortContext->sasLocalAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&tdsaAllShared->Ports[PhyID].SASID);
        onePortContext->sasLocalAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&tdsaAllShared->Ports[PhyID].SASID);
        onePortContext->tiPortalContext = tdsaAllShared->Ports[PhyID].tiPortalContext;
        onePortContext->agRoot = agRoot;
        onePortContext->agPortContext = agPortContext;
        tdsaAllShared->Ports[PhyID].portContext = onePortContext;
        agPortContext->osData = onePortContext;
        onePortContext->nativeSATAMode = agTRUE;
        onePortContext->valid = agTRUE;
        if (LinkRate == 0x01)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_1_5G;
        }
        else if (LinkRate == 0x02)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_3_0G;
        }
        else if (LinkRate == 0x04)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_6_0G;
        }
        else /* (LinkRate == 0x08) */
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_12_0G;
        }

        tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
        TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->MainLink), &(tdsaAllShared->MainPortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
      }
      else
      {
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        TI_DBG1(("\nossaHwCB: Attention!!! no more free PortContext.\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "Y2");
        return;
      }
#ifdef SATA_ENABLE
      /* tdssAddSATAToSharedcontext() sends identify device data to find out the uniqueness of
         target. In identify device data CB fn (satAddSATAIDDevCB()),
         tiPortLinkUp and tiPortDiscoveryReady happen
      */
      tdssAddSATAToSharedcontext(
                                  onePortContext,
                                  agRoot,
                                  agDevHandle, /* agNULL */
                                  agNULL,
                                  agTRUE,
                                  (bit8)PhyID
                                  );
#endif
      /* setting SAS PORT RESET TMO and SATA PORT RESET TMO*/
      saPortControl(agRoot, /* AGSA_PORT_SET_PORT_RESET_TIME */
                    agNULL,
                    0,
                    agPortContext,
                    AGSA_PORT_SET_PORT_RESET_TIME,
                    0,
                    SATA_PORT_RESET_TMO // 8000 ms
                    );

    }
    else
    {
      /*
        an existing portcontext
        to be tested
      */

      TI_DBG1(("ossaHwCB: SATA existing portcontext returned. need testing\n"));
      onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
      /* for debugging only */
      if (onePortContext->valid == agFALSE)
      {
        /* port has been invalidated; needs to be allocated */
        TI_DBG2(("ossaHwCB: SATA allocating port context\n"));
      }
      else
      {
        /* already alloacated */
        TI_DBG1(("ossaHwCB: Wrong!!! SATA already allocated port context\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "Y2");
        return;
      }

      tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
      if (TDLIST_NOT_EMPTY(&(tdsaAllShared->FreePortContextList)))
      {
        TDLIST_DEQUEUE_FROM_HEAD(&PortContextList, &(tdsaAllShared->FreePortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, FreeLink, PortContextList);
        TI_DBG2(("ossaHwCB: pid %d\n", onePortContext->id));
        TI_DBG6(("ossaHwCB: onePortContext %p\n", onePortContext));
        if (onePortContext == agNULL)
        {
          TI_DBG1(("ossaHwCB: onePortContext is NULL in allocation, wrong!\n"));
          return;
        }

        /* sets fields of tdsaportcontext */
        onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
        onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_FULL_START;
        onePortContext->PhyIDList[PhyID] = agTRUE;
        /* NO sas address for SATA */
        onePortContext->sasRemoteAddressHi = 0xFFFFFFFF;
        onePortContext->sasRemoteAddressLo = 0xFFFFFFFF;
        /* copying the signature */
        onePortContext->remoteSignature[0] = RegD2H->d.sectorCount;
        onePortContext->remoteSignature[1] = RegD2H->d.lbaLow;
        onePortContext->remoteSignature[2] = RegD2H->d.lbaMid;
        onePortContext->remoteSignature[3] = RegD2H->d.lbaHigh;
        onePortContext->remoteSignature[4] = RegD2H->d.device;

        onePortContext->sasLocalAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&tdsaAllShared->Ports[PhyID].SASID);
        onePortContext->sasLocalAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&tdsaAllShared->Ports[PhyID].SASID);
        onePortContext->tiPortalContext = tdsaAllShared->Ports[PhyID].tiPortalContext;
        onePortContext->agRoot = agRoot;
        onePortContext->agPortContext = agPortContext;
        tdsaAllShared->Ports[PhyID].portContext = onePortContext;
        agPortContext->osData = onePortContext;
        onePortContext->nativeSATAMode = agTRUE;
        onePortContext->valid = agTRUE;
        if (LinkRate == 0x01)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_1_5G;
        }
        else if (LinkRate == 0x02)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_3_0G;
        }
        else if (LinkRate == 0x04)
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_6_0G;
        }
        else /* (LinkRate == 0x08) */
        {
          onePortContext->LinkRate = SAS_CONNECTION_RATE_12_0G;
        }

        tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
        TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->MainLink), &(tdsaAllShared->MainPortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
      }
      else
      {
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
        TI_DBG1(("\nossaHwCB: Attention!!! no more free PortContext.\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "Y2");
        return;
      }


      /*hotplug */
#ifdef SATA_ENABLE
      tdssAddSATAToSharedcontext(
                                  onePortContext,
                                  agRoot,
                                  agDevHandle, /* agNULL */
                                  agNULL,
                                  agTRUE,
                                  (bit8)PhyID
                                  );
#endif
    /* end hotplug */
    }

    break;
  }
#endif
  case OSSA_HW_EVENT_SATA_SPINUP_HOLD:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);

    TI_DBG2(("ossaHwCB: spinup hold PhyID %d\n", PhyID));
    break;
  }

  case OSSA_HW_EVENT_PHY_DOWN:
  {
    bit32 AllPhyDown = agTRUE;

    /* 4/15/08 spec */
    PhyID = TD_GET_PHY_ID(eventParm1);
    LinkRate = TD_GET_LINK_RATE(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);


    TI_DBG2(("ossaHwCB: Phy%d link Down\n", PhyID));

    if (agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: agPortContext null, wrong\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'l', "Y2");
      return;
    }

    if ( agPortContext->osData == agNULL)
    { /* if */
      /* PortContext must exit at this point */
      TI_DBG1(("ossaHwCB: NULL portalcontext. Error. Can't be NULL\n"));
    }
    else
    {
      TI_DBG3(("ossaHwCB: NOT NULL portalcontext\n"));
      onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
      if (onePortContext == agNULL)
      {
        TI_DBG1(("ossaHwCB: wrong !!! No corressponding tdsaPortContext\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'm', "Y2");
        return;
      }
      onePortContext->PhyIDList[PhyID] = agFALSE;
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        if (onePortContext->PhyIDList[i] == agTRUE)
        {
          TI_DBG3(("ossaHwCB: Phy %d is still up\n", i));
          AllPhyDown = agFALSE;
          break;
        }
      }

      /* last phy belong to the portcontext */
      if (AllPhyDown == agTRUE)
      {
#ifdef NOT_YET  
        TI_DBG1(("ossaHwCB: calling tiPortLinkDown\n"));
        ostiPortEvent (
                       tiRoot,
                       tiPortLinkDown,
                       tiSuccess,
                       (void *)onePortContext->tiPortalContext
                       );
#endif
      }

      if (PortState == OSSA_PORT_VALID)
      {
        /* do nothing */
        /* no ack for every phy down */
#ifdef FDS_DM
        /* update MCN for all devices belong to this port */
        tdsaUpdateMCN(dmRoot, onePortContext);
#endif
      }
      else if (PortState == OSSA_PORT_LOSTCOMM)
      {
        /*
         1. Mark the port as invalid and stop the io for that port and its device
         No ack here. Otherwise, port will be released by FW.
        */
        TI_DBG2(("ossaHwCB: phy Down and OSSA_PORT_LOSTCOMM\n"));
        /* save eventSource related information in tdsaAllShared */
        tdsaAllShared->eventSource[PhyID].EventValid =  agTRUE;
        tdsaAllShared->eventSource[PhyID].Source.agPortContext =  agPortContext;
        tdsaAllShared->eventSource[PhyID].Source.event =  OSSA_HW_EVENT_PHY_DOWN;
        /* phy ID */
        tdsaAllShared->eventSource[PhyID].Source.param =  PhyID;
        /* phy ID */
        onePortContext->eventPhyID = PhyID;
        /* to stop IO's */
        onePortContext->valid = agFALSE;
        break;
      }
      else if (PortState == OSSA_PORT_IN_RESET)
      {
        TI_DBG2(("ossaHwCB: phy Down and OSSA_PORT_IN_RESET\n"));
        /* save eventSource related information in tdsaAllShared */
        tdsaAllShared->eventSource[PhyID].EventValid =  agTRUE;
        tdsaAllShared->eventSource[PhyID].Source.agPortContext =  agPortContext;
        tdsaAllShared->eventSource[PhyID].Source.event =  OSSA_HW_EVENT_PHY_DOWN;
        /* phy ID */
        tdsaAllShared->eventSource[PhyID].Source.param =  PhyID;
        /* phy ID */
        onePortContext->eventPhyID = PhyID;
        /* to stop IO's */
        onePortContext->valid = agFALSE;
        break;
      }
      else if (PortState == OSSA_PORT_INVALID)
      {
        TI_DBG1(("ossaHwCB: Last phy Down and port invalid OSSA_PORT_INVALID\n"));
        /*
          invalidate port
          then, saHwEventAck() in ossaDeregisterDeviceHandleCB()
        */

        /* save eventSource related information in tdsaAllShared */
        tdsaAllShared->eventSource[PhyID].EventValid =  agTRUE;
        tdsaAllShared->eventSource[PhyID].Source.agPortContext =  agPortContext;
        tdsaAllShared->eventSource[PhyID].Source.event =  OSSA_HW_EVENT_PHY_DOWN;
        /* phy ID */
        tdsaAllShared->eventSource[PhyID].Source.param =  PhyID;
        /* phy ID */
        onePortContext->eventPhyID = PhyID;

        onePortContext->valid = agFALSE;

        TI_DBG2(("ossaHwCB: pid %d\n", onePortContext->id));
#ifdef INITIATOR_DRIVER
        /* notifying link down (all links belonging to a port are down) */
        ostiPortEvent(
                      tiRoot,
                      tiPortStopped,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );
#endif

#ifdef TARGET_DRIVER
        ostiPortEvent(
                      tiRoot,
                      tiPortLinkDown,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );

#endif

#ifdef INITIATOR_DRIVER
        tdssReportRemovals(agRoot,
                         onePortContext,
                         agFALSE
                        );
#endif
#ifdef TARGET_DRIVER
        ttdssReportRemovals(agRoot,
                            onePortContext,
                            agFALSE
                           );

#endif

        /* find a PhyID and reset for portContext in tdssSASShared */
        for(i=0;i<TD_MAX_NUM_PHYS;i++)
        {
          if (onePortContext->PhyIDList[i] == agTRUE)
          {
            tdsaAllShared->Ports[i].portContext = agNULL;
          }
        }
    /* portcontext is removed from MainLink to FreeLink in tdssReportRemovals or
       ossaDeregisterDeviceHandleCB
     */
      }/* OSSA_PORT_INVALID */
      else
      {
        /* other newly defined port state */
        /* do nothing */
        TI_DBG2(("ossaHwCB: portstate 0x%x\n", PortState));
      }
    } /* big else */
    break;
  }
  case OSSA_HW_EVENT_PHY_START_STATUS:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PhyStatus =  TD_GET_PHY_STATUS(eventParm1);

    TI_DBG6(("ossaHwCB: OSSA_HW_EVENT_PHY_START_STATUS\n"));
    if (PhyStatus == 0x00)
    {
      TI_DBG6(("ossaHwCB: OSSA_HW_EVENT_PHY_START_STATUS, SUCCESS\n"));
    }
    else if (PhyStatus == 0x01)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_START_STATUS, INVALID_PHY\n"));
    }
    else if (PhyStatus == 0x02)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_START_STATUS, PHY_NOT_DISABLED\n"));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_START_STATUS, OTHER_FAILURE %d\n", PhyStatus));
    }
    break;
  }
  case OSSA_HW_EVENT_PHY_STOP_STATUS:
  {
    agsaContext_t     *agContext;
    PhyID = TD_GET_PHY_ID(eventParm1);
    PhyStatus =  TD_GET_PHY_STATUS(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS\n"));
    if (PhyStatus == 0x00)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS, SUCCESS\n"));
      agContext = (agsaContext_t *)eventParm2;
      onePortContext  = (tdsaPortContext_t *)agContext->osData;;
      if (onePortContext == agNULL)
      {
        TI_DBG1(("ossaHwCB: onePortContext is null, wrong!!!\n"));
        return;
      }
      onePortContext->PhyIDList[PhyID] = agFALSE;
      if (PortState == OSSA_PORT_INVALID) /* invalid port */
      {
        TI_DBG1(("ossaHwCB: OSSA_PORT_INVALID\n"));
        tdsaAllShared->eventSource[PhyID].EventValid =  NO_ACK;
        onePortContext->eventPhyID = PhyID;
        onePortContext->valid = agFALSE;

        TI_DBG2(("ossaHwCB: pid %d\n", onePortContext->id));
#ifdef INITIATOR_DRIVER
        /* notifying link down (all links belonging to a port are down) */
        ostiPortEvent(
                      tiRoot,
                      tiPortStopped,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );
#endif

#ifdef TARGET_DRIVER
        ostiPortEvent(
                      tiRoot,
                      tiPortLinkDown,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );

#endif

#ifdef INITIATOR_DRIVER
        tdssReportRemovals(agRoot,
                           onePortContext,
                           agFALSE
                          );
#endif
#ifdef TARGET_DRIVER
        ttdssReportRemovals(agRoot,
                            onePortContext,
                            agFALSE
                           );

#endif

        /* find a PhyID and reset for portContext in tdssSASShared */
        for(i=0;i<TD_MAX_NUM_PHYS;i++)
        {
          if (onePortContext->PhyIDList[i] == agTRUE)
          {
            tdsaAllShared->Ports[i].portContext = agNULL;
          }
        }
    /* portcontext is removed from MainLink to FreeLink in tdssReportRemovals or
       ossaDeregisterDeviceHandleCB
     */
      } /* invalid port */
    }
    else if (PhyStatus == 0x01)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS, INVALID_PHY\n"));
    }
    else if (PhyStatus == 0x02)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS, DEVICES_ATTACHED\n"));
    }
    else if (PhyStatus == 0x03)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS, OTHER_FAILURE\n"));
    }
    else if (PhyStatus == 0x04)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS, PHY_NOT_DISABLED\n"));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_STOP_STATUS, Unknown %d\n", PhyStatus));
    }
    break;
  }

  case OSSA_HW_EVENT_RESET_START:
  {
    bit32 new_status = TD_GET_RESET_STATUS(eventParm1);
    TI_DBG2(("ossaHwCB: RESET_START, status %d\n", new_status));
    if (new_status == OSSA_SUCCESS)
    {
      tdsaAllShared->flags.resetInProgress = agTRUE;
      TI_DBG2(("ossaHwCB: RESET_START, SUCCESS\n"));
    }
    else if (new_status == OSSA_FAILURE)
    {
      TI_DBG1(("ossaHwCB: RESET_START, FAILURE\n"));
    }
    else
    {
      TI_DBG1(("ossaHwCB: RESET_START, PENDING\n"));
    }
    break;
  }

  case OSSA_HW_EVENT_RESET_COMPLETE:
  {
    bit32 new_status = TD_GET_RESET_STATUS(eventParm1);
#ifdef SOFT_RESET_TEST
    DbgPrint("Reset Complete\n");
#endif
    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_RESET_COMPLETE, status %d\n", new_status));
    if (new_status == OSSA_SUCCESS)
    {
      /* remove all portcontext and devices */
#ifdef INITIATOR_DRIVER
      tdssRemoveSASSATAFromSharedcontextByReset(agRoot);
#endif
      tdsaAllShared->flags.resetInProgress = agFALSE;
      /*
        a callback notifying reset completion
      */
      ostiPortEvent(
                    tiRoot,
                    tiPortResetComplete,
                    tiSuccess,
                    agNULL
                    );
    }
    else
    {
      /*
        a callback notifying reset completion
      */
      tdsaAllShared->flags.resetInProgress = agFALSE;
      ostiPortEvent(
                    tiRoot,
                    tiPortResetComplete,
                    tiError,
                    agNULL
                    );

    }
    break;
  }

  case OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;

    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC from PhyID %d; to be tested\n", PhyID));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'n', "Y2");
      return;
    }

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: iDw %d rDE %d cV %d lS %d rP %d iCRC %d\n",
                        agPhyErrCountersPage->invalidDword,
                        agPhyErrCountersPage->runningDisparityError,
                        agPhyErrCountersPage->codeViolation,
                        agPhyErrCountersPage->lossOfDwordSynch,
                        agPhyErrCountersPage->phyResetProblem,
                        agPhyErrCountersPage->inboundCRCError ));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC: Error!!!  eventParm2 is NULL\n"));
    }

    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'o', "Y2");
      return;
    }
    break;
  }
#ifdef REMOVED
  case OSSA_HW_EVENT_PORT_INVALID:
  {
    TI_DBG1(("ossaHwCB: PORT_INVALID\n"));

    if ( agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: agPortContext is NULL, wrong.\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'p', "Y2");
      return;
    }
    if ( agPortContext->osData != agNULL)
    {
      TI_DBG1(("ossaHwCB: NOT NULL osDATA\n"));
      /*
        put the old portcontext back to free list
      */
      onePortContext = (tdsaPortContext_t *)agPortContext->osData;
      TI_DBG1(("ossaHwCB: pid %d\n", onePortContext->id));

#ifdef INITIATOR_DRIVER
      /* notifying link down (all links belonging to a port are down) */
      ostiPortEvent (
                     tiRoot,
                     tiPortStopped,
                     tiSuccess,
                     (void *)onePortContext->tiPortalContext

                     );
#endif /* INITIATOR_DRIVER */
#ifdef TARGET_DRIVER
        ostiPortEvent(
                      tiRoot,
                      tiPortLinkDown,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );

#endif /*TARGET_DRIVER  */

      /* find the device belonging to the port and remove it from the device list */
      //tdssRemoveSASSATAFromSharedcontext(agRoot, tdsaDeviceData, onePortContext);


#ifdef INITIATOR_DRIVER
      /* reset the fields of portcontext */
      onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
      tdssReportRemovals(agRoot,
                         onePortContext,
                         agFALSE
                        );

      onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_FULL_START;
      onePortContext->DiscoveryRdyGiven = agFALSE;
      onePortContext->SeenLinkUp = agFALSE;

#endif /* INITIATOR_DRIVER */



      /* for hotplug */

      /* find a PhyID and reset for portContext in tdssSASShared */
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        if (onePortContext->PhyIDList[i] == agTRUE)
        {
          tdsaAllShared->Ports[i].portContext = agNULL;
        }
      }

      /* reset PhyIDList in portcontext */
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        onePortContext->PhyIDList[i] = agFALSE;
      }

//      onePortContext->tiPortalContext = agNULL;
//      onePortContext->agRoot = agNULL;
      onePortContext->agPortContext = agNULL;
      onePortContext->valid = agFALSE;

      TI_DBG4(("ossaHwCB: pid %d count %d\n", onePortContext->id, onePortContext->Count));

      /* resets the number of devices in onePortContext */
      onePortContext->Count = 0;
      onePortContext->discovery.pendingSMP = 0;
      onePortContext->discovery.SeenBC = agFALSE;


      /*
        put all devices belonging to the onePortContext
        back to the free link
      */

      tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
      TDLIST_DEQUEUE_THIS(&(onePortContext->MainLink));
      TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->FreeLink), &(tdsaPortContext->FreeLink));
      tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    }
    else
    {
      TI_DBG1(("ossaHwCB: NULL osDATA: wrong\n"));
    }
    TI_DBG6(("ossaHwCB: PORT_INVALID end\n"));
    break;
  }
#endif /* REMOVED */

  case OSSA_HW_EVENT_BROADCAST_CHANGE:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    TI_DBG1(("ossaHwCB: BROADCAST_CHANGE from PhyID %d\n", PhyID));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  BROADCAST_CHANGE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'q', "Y2");
      return;
    }
    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_BROADCAST_CHANGE;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    TI_DBG4(("ossaHwCB: calling saHwEventAck\n"));

    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'r', "Y2");
      return;
    }
    if (tIsSPC12SATA(agRoot))
    {
      TI_DBG1(("ossaHwCB: BROADCAST_CHANGE received for SATA Controller\n"));
      break;
    }
    /*
     * incremental discovery is to be tested and debugged further
     */

     /* just for testing discovery abort */
#ifdef FDS_DM_NO
    if (agPortContext == agNULL)
    {
      /* this case happens when broadcase is received first before the link up */
      TI_DBG2(("ossaHwCB: agPortContext is NULL. Do nothing.\n"));
    }
    else if ( agPortContext->osData != agNULL)
    {
      dmRoot = &(tdsaAllShared->dmRoot);
      onePortContext = (tdsaPortContext_t *)agPortContext->osData;
      dmPortContext = &(onePortContext->dmPortContext);

      dmQueryDiscovery(dmRoot, dmPortContext);
//      dmDiscover(dmRoot, dmPortContext, DM_DISCOVERY_OPTION_ABORT);

#if 1
      if (onePortContext->DMDiscoveryState == dmDiscInProgress)
      {
        dmDiscover(dmRoot, dmPortContext, DM_DISCOVERY_OPTION_ABORT);
      }
#endif /* 1 */

      TI_DBG2(("ossaHwCB: portcontext pid %d\n", onePortContext->id));
      if (onePortContext->DMDiscoveryState == dmDiscCompleted ||
          onePortContext->DMDiscoveryState == dmDiscAborted ||
          onePortContext->DMDiscoveryState == dmDiscAbortInvalid )
      {
        TI_DBG1(("ossaHwCB: BROADCAST_CHANGE; calling dmNotifyBC and does incremental discovery\n"));
        dmNotifyBC(dmRoot, dmPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE);
        dmDiscover(dmRoot, dmPortContext, DM_DISCOVERY_OPTION_INCREMENTAL_START);

      }
      else
      {
        TI_DBG2(("ossaHwCB: pid %d BROADCAST_CHANGE; updating SeenBC. calling dmNotifyBC\n", onePortContext->id));
        dmNotifyBC(dmRoot, dmPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE);
      }
    }
    else
    {
      TI_DBG1(("ossaHwCB: BROADCAST_CHANGE NULL osDATA wrong !!! \n"));
    }


#endif /* FDS_DM_NO */

#ifdef FDS_DM
    if (agPortContext == agNULL)
    {
      /* this case happens when broadcase is received first before the link up */
      TI_DBG2(("ossaHwCB: agPortContext is NULL. Do nothing.\n"));
    }
    else if ( agPortContext->osData != agNULL)
    {
      dmRoot = &(tdsaAllShared->dmRoot);
      onePortContext = (tdsaPortContext_t *)agPortContext->osData;
      dmPortContext = &(onePortContext->dmPortContext);

      dmQueryDiscovery(dmRoot, dmPortContext);

      TI_DBG2(("ossaHwCB: portcontext pid %d\n", onePortContext->id));
      if (onePortContext->DMDiscoveryState == dmDiscCompleted ||
          onePortContext->DMDiscoveryState == dmDiscAborted ||
          onePortContext->DMDiscoveryState == dmDiscAbortInvalid )
      {
        TI_DBG1(("ossaHwCB: BROADCAST_CHANGE; calling dmNotifyBC and does incremental discovery, pid %d\n", onePortContext->id));
        onePortContext->DiscoveryState = ITD_DSTATE_STARTED;
        dmNotifyBC(dmRoot, dmPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE);
        dmDiscover(dmRoot, dmPortContext, DM_DISCOVERY_OPTION_INCREMENTAL_START);

      }
      else if (onePortContext->DMDiscoveryState == dmDiscFailed )
      {
        TI_DBG1(("ossaHwCB: dmDiscFailed; pid %d BROADCAST_CHANGE; updating SeenBC. calling dmNotifyBC\n", onePortContext->id));
        onePortContext->DiscFailNSeenBC = agTRUE;
        dmNotifyBC(dmRoot, dmPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE);
      }
      else
      {
        TI_DBG2(("ossaHwCB: pid %d BROADCAST_CHANGE; updating SeenBC. calling dmNotifyBC\n", onePortContext->id));
        dmNotifyBC(dmRoot, dmPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE);
      }
    }
    else
    {
      TI_DBG1(("ossaHwCB: BROADCAST_CHANGE NULL osDATA wrong !!! \n"));
    }
#endif /* FDS_DM */

#ifdef FDS_DM_WORKED
    if (agPortContext == agNULL)
    {
      /* this case happens when broadcase is received first before the link up */
      TI_DBG2(("ossaHwCB: agPortContext is NULL. Do nothing.\n"));
    }
    else if ( agPortContext->osData != agNULL)
    {
      onePortContext = (tdsaPortContext_t *)agPortContext->osData;
      TI_DBG2(("ossaHwCB: calling dmNotifyBC\n"));
      dmRoot = &(tdsaAllShared->dmRoot);
      dmPortContext = &(onePortContext->dmPortContext);
      dmNotifyBC(dmRoot, dmPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE);
    }
#endif /* FDS_DM_WORKED */

#ifndef FDS_DM
#ifdef INITIATOR_DRIVER
    if (agPortContext == agNULL)
    {
      /* this case happens when broadcase is received first before the link up */
      TI_DBG2(("ossaHwCB: agPortContext is NULL. Do nothing.\n"));
    }
    else if ( agPortContext->osData != agNULL)
    {
      onePortContext = (tdsaPortContext_t *)agPortContext->osData;
      TI_DBG2(("ossaHwCB: portcontext pid %d\n", onePortContext->id));
      if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED)
      {
        TI_DBG1(("ossaHwCB: BROADCAST_CHANGE; does incremental discovery\n"));
        onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
        onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_INCREMENTAL_START;
        /* processed broadcast change */
        onePortContext->discovery.SeenBC = agFALSE;
#ifdef TD_DISCOVER
        if (tdsaAllShared->ResetInDiscovery != 0 &&
            onePortContext->discovery.ResetTriggerred == agTRUE)
        {
          TI_DBG2(("ossaHwCB: tdsaBCTimer\n"));
          tdsaBCTimer(tiRoot, onePortContext);
        }
        else
        {
          tdsaDiscover(
                     tiRoot,
                     onePortContext,
                     TDSA_DISCOVERY_TYPE_SAS,
                     TDSA_DISCOVERY_OPTION_INCREMENTAL_START
                    );
        }
#else
        saDiscover(agRoot,
                   agPortContext,
                   AG_SA_DISCOVERY_TYPE_SAS,
                   onePortContext->discoveryOptions);
#endif
      }
      else
      {
        TI_DBG2(("ossaHwCB: pid %d BROADCAST_CHANGE; updating SeenBC. Do nothing.\n", onePortContext->id));
        onePortContext->discovery.SeenBC = agTRUE;
      }
    }
    else
    {
      TI_DBG1(("ossaHwCB: BROADCAST_CHANGE NULL osDATA wrong !!! \n"));
    }
#endif
#endif /* ifndef FDS_DM */

    break;
  }

  case OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    /*
      1. tear town the portcontext just like link down last phy down
      2. ack
      port state must be invalid
    */

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO\n"));

    if (PortState == OSSA_PORT_VALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 's', "Y2");
      return;
    }

    TD_ASSERT(agPortContext, "agPortContext");
    if ( agPortContext->osData == agNULL)
    { /* if */
      /* PortContext must exit at this point */
      TI_DBG1(("ossaHwCB: NULL portalcontext. Error. Can't be NULL\n"));
    }
    else
    {
      onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
      onePortContext->valid = agFALSE;

      TI_DBG1(("ossaHwCB: tiPortStopped pid %d\n", onePortContext->id));
#ifdef INITIATOR_DRIVER
      /* notifying link down (all links belonging to a port are down) */
      ostiPortEvent(
                    tiRoot,
                    tiPortStopped,
                    tiSuccess,
                    (void *)onePortContext->tiPortalContext
                    );
#endif

#ifdef TARGET_DRIVER
        ostiPortEvent(
                      tiRoot,
                      tiPortLinkDown,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );

#endif

#ifdef INITIATOR_DRIVER
      tdssReportRemovals(agRoot,
                         onePortContext,
                         agFALSE
                         );
#endif
#ifdef TARGET_DRIVER
      ttdssReportRemovals(agRoot,
                          onePortContext,
                          agFALSE
                         );

#endif
      /* find a PhyID and reset for portContext in tdssSASShared */
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        if (onePortContext->PhyIDList[i] == agTRUE)
        {
          tdsaAllShared->Ports[i].portContext = agNULL;
        }
      }
      /* portcontext is removed from MainLink to FreeLink in tdssReportRemovals or
         ossaDeregisterDeviceHandleCB
       */
    }

    break;
  }

  case OSSA_HW_EVENT_PORT_RESET_TIMER_TMO:
  {
    /*
       clean up
    */
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PORT_RESET_TIMER_TMO\n"));

    if (PortState == OSSA_PORT_VALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 't', "Y2");
      return;
    }

    if (agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: agPortContext is NULL, error\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'u', "Y2");
      return;
    }

    if ( agPortContext->osData == agNULL)
    { /* if */
      /* PortContext must exit at this point */
      TI_DBG1(("ossaHwCB: NULL portalcontext. Error. Can't be NULL\n"));
    }
    else
    {
      onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
      onePortContext->valid = agFALSE;

      TI_DBG1(("ossaHwCB: pid %d tiPortStopped\n", onePortContext->id));

#ifdef INITIATOR_DRIVER
      /* notifying link down (all links belonging to a port are down) */
      ostiPortEvent(
                    tiRoot,
                    tiPortStopped,
                    tiSuccess,
                    (void *)onePortContext->tiPortalContext
                    );
#endif

#ifdef TARGET_DRIVER
        ostiPortEvent(
                      tiRoot,
                      tiPortLinkDown,
                      tiSuccess,
                      (void *)onePortContext->tiPortalContext
                      );

#endif

#ifdef INITIATOR_DRIVER
      tdssReportRemovals(agRoot,
                         onePortContext,
                         agFALSE
                         );
#endif
#ifdef TARGET_DRIVER
      ttdssReportRemovals(agRoot,
                          onePortContext,
                          agFALSE
                         );

#endif
      /* find a PhyID and reset for portContext in tdssSASShared */
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        if (onePortContext->PhyIDList[i] == agTRUE)
        {
          tdsaAllShared->Ports[i].portContext = agNULL;
        }
      }
      /* portcontext is removed from MainLink to FreeLink in tdssReportRemovals or
         ossaDeregisterDeviceHandleCB
       */
    }

    break;
  }

  case OSSA_HW_EVENT_PORT_RESET_COMPLETE:
  {
#ifdef INITIATOR_DRIVER
    tiIORequest_t *currentTaskTag = agNULL;
#endif

#ifdef REMOVED
    smRoot_t  *smRoot = &(tdsaAllShared->smRoot);
#endif

    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    IDframe = (agsaSASIdentify_t *)eventParm3;

    /* completes for Lun Reset and Target reset for directly attached SATA */
    /* completes for Target reset for directly attached SAS */

    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PORT_RESET_COMPLETE, phyID %d\n", PhyID));

    /* error check */
    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'v', "Y2");
      return;
    }

    if (agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: agPortContext null, wrong\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'w', "Y2");
      return;
    }
    if ( agPortContext->osData == agNULL)
    {
      TI_DBG1(("ossaHwCB: agPortContext->osData null, wrong\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'x', "Y2");
      return;
    }

    /* find a corresponding portcontext */
    onePortContext  = (tdsaPortContext_t *)agPortContext->osData;

    if (onePortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: oneportContext is NULL; wrong??????\n"));
    }
    else
    {
      TI_DBG1(("ossaHwCB: oneportContext %p pid %d\n", onePortContext, onePortContext->id));
      onePortContext->valid = agTRUE;
#ifdef INITIATOR_DRIVER
#ifdef REMOVED
      if (tdsaAllShared->ResetInDiscovery != 0)
      {
        DeviceListList = tdsaAllShared->MainDeviceList.flink;
        while (DeviceListList != &(tdsaAllShared->MainDeviceList))
        {
          oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
          if (oneDeviceData->tdPortContext != onePortContext)
          {
            DeviceListList = DeviceListList->flink;
          }
          else
          {
            found = agTRUE;
            break;
          }
        } /* while */
        if (found == agTRUE)
        {
          /* applied to only SATA devices */
          if (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
          {
          #ifdef FDS_SM
            tdIDStart(tiRoot, agRoot, smRoot, oneDeviceData, onePortContext);
          #else
            tdssRetrySATAID(tiRoot, oneDeviceData);
          #endif
          }
        }
        else
        {
          TI_DBG1(("ossaHwCB: no onedevicedata found!\n"));
        }
      }
#endif
      /* completed TM */
      DeviceListList = tdsaAllShared->MainDeviceList.flink;
      while (DeviceListList != &(tdsaAllShared->MainDeviceList))
      {
        oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
        if ( oneDeviceData == agNULL)
        {
          TI_DBG1(("ossaHwCB: oneDeviceData is NULL!!!\n"));
          return;
        }

        if ( (oneDeviceData->tdPortContext == onePortContext) &&
             (oneDeviceData->directlyAttached == agTRUE) &&
             (oneDeviceData->phyID == PhyID) )
        {
          TI_DBG1(("ossaHwCB: found the onePortContext and oneDeviceData!!\n"));

          currentTaskTag = (tiIORequest_t *)oneDeviceData->agDeviceResetContext.osData;
          if (currentTaskTag != agNULL )
          {
            /* applied to only SATA devices */
            if (DEVICE_IS_SATA_DEVICE(oneDeviceData))
            {
               tdIORequestBody_t  *SMTMtdIORequestBody = agNULL;
               SMTMtdIORequestBody = (tdIORequestBody_t *)currentTaskTag->tdData;
               if (SMTMtdIORequestBody != agNULL)
               {
                 /* free the SMTMtdIORequestBody memory allocated in tiINITaskManagement function */
                 ostiFreeMemory(
                       tiRoot,
                       SMTMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                       sizeof(tdIORequestBody_t)
                      );
               }
               else
               {
                 TI_DBG1(("ossaHwCB: SATA device but SMTMtdIORequestBody is NULL!!!\n"));
               }
            }
            /* set device state to DS_OPERATIONAL */
            saSetDeviceState(agRoot,
                            agNULL,
                            tdsaRotateQnumber(tiRoot, oneDeviceData),
                            oneDeviceData->agDevHandle,
                            SA_DS_OPERATIONAL
                            );
            /* notify OS layer to complete the TMF IO */
            ostiInitiatorEvent(tiRoot,
                              agNULL,
                              agNULL,
                              tiIntrEventTypeTaskManagement,
                              tiTMOK,
                              currentTaskTag
                              );

          }
          else
          {
            TI_DBG1(("ossaHwCB: currentTaskTag is NULL!!!\n"));
          }

          break;
        }
        else
        {
          DeviceListList = DeviceListList->flink;
        }
      }
#endif
    }
    break;
  }
  case OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT\n"));
    if (tIsSPC12SATA(agRoot))
    {
      TI_DBG1(("ossaHwCB: BROADCAST_ASYNCH_EVENT received for SATA Controller\n"));
      break;
    }
    if (agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: Error!!! agPortContext is NULL %d\n", PhyID));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'y', "Y2");
      return;
    }
    onePortContext = (tdsaPortContext_t *)agPortContext->osData;
    if (onePortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: Error!!! onePortContext is NULL %d\n", PhyID));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'z', "Y2");
      return;
    }

    if (onePortContext->tiPortalContext != agNULL)
    {
#if 0 
      ostiInitiatorEvent(
                         tiRoot,
                         onePortContext->tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT,
                         agNULL
                         );
#endif
    }
    else
    {
      TI_DBG1(("ossaHwCB: Error!!! onePortContext->tiPortalContext is NULL\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'A', "Y2");
      return;
    }

    break;
   }

  case OSSA_HW_EVENT_PORT_RECOVER:
  {

    PhyID = TD_GET_PHY_ID(eventParm1);
    if (agPortContext == agNULL)
    {
      TI_DBG1(("ossaHwCB: Error!!! agPortContext is NULL %d\n", PhyID));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'B', "Y2");
      return;
    }

    LinkRate = TD_GET_LINK_RATE(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agDevHandle = agNULL;
    IDframe = (agsaSASIdentify_t *)eventParm3;

    /*
      1. this is like link up
      2. handle the phyID
      3. no trigger discovery (broadcast change will do this later)
      port state must be valid
    */

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PORT_RECOVER, phyID %d\n", PhyID));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'C', "Y2");
      return;
    }
    if ( agPortContext->osData == agNULL)
    { /* if */
      /* PortContext must exit at this point */
      TI_DBG1(("ossaHwCB: NULL portalcontext. Error. Can't be NULL\n"));
    }
    else
    {
      onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
      TI_DBG2(("ossaHwCB: pid %d\n", onePortContext->id));
      onePortContext->PhyIDList[PhyID] = agTRUE;
      onePortContext->valid = agTRUE;
      tdsaAllShared->Ports[PhyID].portContext = onePortContext;
      onePortContext->tiPortalContext = tdsaAllShared->Ports[PhyID].tiPortalContext;
      onePortContext->PortRecoverPhyID = PhyID;
      if (LinkRate == 0x01)
      {
        onePortContext->LinkRate = SAS_CONNECTION_RATE_1_5G;
      }
      else if (LinkRate == 0x02)
      {
        onePortContext->LinkRate = SAS_CONNECTION_RATE_3_0G;
      }
      else if (LinkRate == 0x04)
      {
        onePortContext->LinkRate = SAS_CONNECTION_RATE_6_0G;
      }
      else /* (LinkRate == 0x08) */
      {
        onePortContext->LinkRate = SAS_CONNECTION_RATE_12_0G;
      }

      if (SA_IDFRM_GET_DEVICETTYPE(&onePortContext->sasIDframe) == SAS_END_DEVICE &&
          SA_IDFRM_IS_SSP_TARGET(&onePortContext->sasIDframe) )
      {
        TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PORT_RECOVER, sending spinup on phyID %d\n", PhyID));
        for (i=0;i<TD_MAX_NUM_NOTIFY_SPINUP;i++)
        {
          saLocalPhyControl(agRoot, agNULL, 0, PhyID, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
        }
      }

      /* transient period between link up and link down/port recovery */
      if (onePortContext->Transient == agTRUE && onePortContext->RegisteredDevNums == 0)
      {
        TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PORT_RECOVER transient period"));
        if (SA_IDFRM_GET_DEVICETTYPE(IDframe) != SAS_NO_DEVICE)
        {
#ifdef INITIATOR_DRIVER
          agSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(IDframe);
          agSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(IDframe);
          agSASSubID.initiator_ssp_stp_smp = IDframe->initiator_ssp_stp_smp;
          agSASSubID.target_ssp_stp_smp = IDframe->target_ssp_stp_smp;
          tdssAddSASToSharedcontext(
                                    onePortContext,
                                    agRoot,
                                    agDevHandle, /* agNULL */
                                    &agSASSubID,
                                    agTRUE,
                                    (bit8)PhyID,
                                    TD_OPERATION_INITIATOR
                                    );
#endif
        }
        onePortContext->Transient = agFALSE;
      }




    }
    break;
  }

  case OSSA_HW_EVENT_BROADCAST_SES:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: BROADCAST_SES  from PhyID %d; to be tested\n", PhyID));
    if (tIsSPC12SATA(agRoot))
    {
      TI_DBG1(("ossaHwCB: BROADCAST_SES received for SATA Controller\n"));
      break;
    }
    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  BROADCAST_SES\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'D', "Y2");
      return;
    }

    /*
       let os layer read payload
    */
    break;
  }
  case OSSA_HW_EVENT_BROADCAST_EXP:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: BROADCAST_EXP from PhyID %d; to be tested\n", PhyID));
    if (tIsSPC12SATA(agRoot))
    {
      TI_DBG1(("ossaHwCB: BROADCAST_EXP received for SATA Controller\n"));
      break;
    }

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  BROADCAST_EXP\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'E', "Y2");
      return;
    }
    /* to-do:
       let os layer read payload
    */
    break;
  }

  case OSSA_HW_EVENT_HARD_RESET_RECEIVED:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: HARD_RESET_RECEIVED from PhyID %d\n", PhyID));

    if (PortState == OSSA_PORT_VALID && tiIS_SPC(agRoot))
    {
      TI_DBG1(("ossaHwCB: calling saPortControl and OSSA_PORT_VALID\n"));
      saPortControl(agRoot, agNULL, 0, agPortContext, AGSA_PORT_HARD_RESET, 0,0);
    }
    else if (PortState == OSSA_PORT_3RDPARTY_RESET && (tIsSPCV12or6G(agRoot))  )
    {
      TI_DBG1(("ossaHwCB: calling saPortControl and OSSA_PORT_3RDPARTY_RESET\n"));
      saPortControl(agRoot, agNULL, 0, agPortContext, AGSA_PORT_HARD_RESET, 0,0);
    }
    else /* PortState == OSSA_PORT_INVALID */
    {
      TI_DBG1(("ossaHwCB: Error. Port state is invalid\n"));
#ifdef REMOVED
      TI_DBG1(("ossaHwCB: calling saLocalPhyControl on phyID %d\n", PhyID));
      saLocalPhyControl(agRoot, agNULL, 0, PhyID, AGSA_PHY_LINK_RESET, agNULL);
#endif
    }

    break;
  }

  case OSSA_HW_EVENT_MALFUNCTION:
  {
#ifdef TD_DEBUG_ENABLE
    agsaFatalErrorInfo_t  *FatalError = (agsaFatalErrorInfo_t *)eventParm2;
#endif
    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_MALFUNCTION \n"));
    TI_DBG1(("ossaHwCB: errorInfo0 %8X errorInfo1 %8X\n", FatalError->errorInfo0, FatalError->errorInfo1));
    TI_DBG1(("ossaHwCB: errorInfo2 %8X errorInfo3 %8X\n", FatalError->errorInfo2, FatalError->errorInfo3));
    TI_DBG1(("ossaHwCB: regDumpBusBaseNum0 %8X regDumpOffset0 %8X regDumpLen0 %8X\n", FatalError->regDumpBusBaseNum0, FatalError->regDumpOffset0, FatalError->regDumpLen0));
    TI_DBG1(("ossaHwCB: regDumpBusBaseNum1 %8X regDumpOffset1 %8X regDumpLen1 %8X\n", FatalError->regDumpBusBaseNum1, FatalError->regDumpOffset1, FatalError->regDumpLen1));


    if (eventParm1 == agTRUE)
    {
      TI_DBG1(("ossaHwCB: fatal error\n"));
      /* port panic */
      ostiPortEvent (
                     tiRoot,
                     tiPortPanic,
                     0,
                     agNULL
                     );
    }
    else
    {
      TI_DBG1(("ossaHwCB: non-fatal error \n"));
    }
    break;
  }

  case OSSA_HW_EVENT_ID_FRAME_TIMEOUT:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_ID_FRAME_TIMEOUT from PhyID %d\n", PhyID));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  OSSA_HW_EVENT_ID_FRAME_TIMEOUT\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'F', "Y2");
      return;
    }
    break;
  }

  case OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;
    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD\n"));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'G', "Y2");
      return;
    }

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: invalidDword %d\n", agPhyErrCountersPage->invalidDword));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD: Error!!!  eventParm2 is NULL\n"));
    }

    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'H', "Y2");
      return;
    }

    break;
  }

  case OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;
    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR\n"));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'I', "Y2");
      return;
    }

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: runningDisparityError %d\n", agPhyErrCountersPage->runningDisparityError));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR: Error!!!  eventParm2 is NULL\n"));
    }

    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'J', "Y2");
      return;
    }

    break;
  }

  case OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;
    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION\n"));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'K', "Y2");
      return;
    }

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: codeViolation %d\n", agPhyErrCountersPage->codeViolation));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION: Error!!!  eventParm2 is NULL\n"));
    }

    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'L', "Y2");
      return;
    }

    break;
  }

#ifdef REMOVED
  case OSSA_HW_EVENT_LINK_ERR_CODE_VIOLATION1:
  {
    PhyID = eventParm1 & 0xFF;
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_LINK_ERR_CODE_VIOLATION1 from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: invalidDword %d\n", agPhyErrCountersPage->invalidDword));
      TI_DBG1(("ossaHwCB: runningDisparityError %d\n", agPhyErrCountersPage->runningDisparityError));
      TI_DBG1(("ossaHwCB: codeViolation %d\n", agPhyErrCountersPage->codeViolation));
      TI_DBG1(("ossaHwCB: lostOfDwordSynch %d\n", agPhyErrCountersPage->lossOfDwordSynch));
      TI_DBG1(("ossaHwCB: phyResetProblem %d\n", agPhyErrCountersPage->phyResetProblem));
      TI_DBG1(("ossaHwCB: inboundCRCError %d\n", agPhyErrCountersPage->inboundCRCError));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_LINK_ERR_CODE_VIOLATION1: Error!!!  eventParm2 is NULL\n"));
    }
    break;
  }
#endif /* REMOVED */

  case OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;
    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH\n"));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'M', "Y2");
      return;
    }

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: lostOfDwordSynch %d\n", agPhyErrCountersPage->lossOfDwordSynch));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH: Error!!!  eventParm2 is NULL\n"));
    }

    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'N', "Y2");
      return;
    }

    break;
  }

  case OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);
    agPhyErrCountersPage = (agsaPhyErrCountersPage_t *)eventParm2;

    TI_DBG2(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED\n"));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: Wrong port state with  OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'O', "Y2");
      return;
    }

    if (agPhyErrCountersPage != agNULL)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED from PhyID %d\n", PhyID));
      TI_DBG1(("ossaHwCB: phyResetProblem %d\n", agPhyErrCountersPage->phyResetProblem));
    }
    else
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED: Error!!!  eventParm2 is NULL\n"));
    }

    /* saHwEventAck() */
    eventSource.agPortContext = agPortContext;
    eventSource.event = OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED;
    /* phy ID */
    eventSource.param = PhyID;
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              &eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ossaHwCB: failing in saHwEventAck; status %d\n", HwAckSatus));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'P', "Y2");
      return;
    }

    break;
  }

// #ifdef INITIATOR_DRIVER
  case OSSA_HW_EVENT_ENCRYPTION:
  {
    pEncryptCBData = (agsaHWEventEncrypt_t *) eventParm2;
    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_ENCRYPTION: encryptOperation 0x%x\n",pEncryptCBData->encryptOperation));
    TI_DBG1(("ossaHwCB: event 0x%x eventParm1 0x%x eventParm2 %p eventParm3 %p\n",event,eventParm1,eventParm2,eventParm3));

    /*
     * All events and status need to be translated from
     * SAS specific values to TISA specific values. This
     * is effectively a NOP, but the OS layer won't want to
     * look for SAS values.
     */
    if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE\n"));
      encryptEventData.encryptEvent = tiEncryptKekStore;
    }
    else if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_KEK_UPDATE)
    {
      TI_DBG1(("ossaHwCB:OSSA_HW_ENCRYPT_KEK_UPDATE \n"));
      encryptEventData.encryptEvent = tiEncryptKekAdd;
    }
    else if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_KEK_INVALIDTE)
    {
      TI_DBG1(("ossaHwCB:OSSA_HW_ENCRYPT_KEK_INVALIDTE \n"));
      /* none */
    }
    else if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_DEK_UPDATE)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_ENCRYPT_DEK_UPDATE\n"));
      encryptEventData.encryptEvent = tiEncryptDekAdd;
    }
    else if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_DEK_INVALIDTE)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_ENCRYPT_DEK_INVALIDTE\n"));
      encryptEventData.encryptEvent = tiEncryptDekInvalidate;
    }
    else if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_OPERATOR_MANAGEMENT)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_ENCRYPT_OPERATOR_MANAGEMENT\n"));
      encryptEventData.encryptEvent = tiEncryptOperatorManagement;
    }
    else if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_TEST_EXECUTE)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_ENCRYPT_TEST_EXECUTE\n"));
      encryptEventData.encryptEvent = tiEncryptSelfTest;
      encryptEventData.subEvent = pEncryptCBData->eq;
    }
    else
    {
      TI_DBG1(("ossaHwCB: unknown encryptOperation 0x%x\n",pEncryptCBData->encryptOperation));
    }

    if (pEncryptCBData->status != OSSA_SUCCESS)
    {
      encryptStatus = tiError;

      /* prints out status and error qualifier */
      TI_DBG1(("ossaHwCB: encrypt response status 0x%x error qualifier 0x%x\n", pEncryptCBData->status, pEncryptCBData->eq));
    }
    else
    {
      encryptStatus = tiSuccess;
    }

    if (pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE ||
        pEncryptCBData->encryptOperation == OSSA_HW_ENCRYPT_KEK_UPDATE )
    {
      /* returning new KEK index */
      encryptEventData.pData = pEncryptCBData->handle;
    }
    else
    {
      /* returning current KEK index or DEK index */
      encryptEventData.pData = pEncryptCBData->param;
    }

    ostiPortEvent(tiRoot,
                  tiEncryptOperation,
                  encryptStatus,
                  &encryptEventData);
    break;
  }
  case OSSA_HW_EVENT_SECURITY_MODE:
  {
    securitySetModeStatus = eventParm1;
    pEncryptInfo = (agsaEncryptInfo_t *) eventParm2;

    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_SECURITY_MODE\n"));
    if (securitySetModeStatus == OSSA_SUCCESS)
    {
      securityModeStatus = tiSuccess;
    }
    else
    {
      securityModeStatus = tiError;
    }

    encryptEventData.encryptEvent = tiEncryptSetMode;
    /* process status to fill in subevent */
    /* See PM 4.26.12.6 */
    TI_DBG1(("ossaHwCB: pEncryptInfo->status 0x%x\n", pEncryptInfo->status));
    if ( pEncryptInfo->status == OSSA_SUCCESS)
    {
      encryptEventData.subEvent = tiNVRAMSuccess;
    }
    else if (pEncryptInfo->status == 0x24)
    {
      encryptEventData.subEvent = tiNVRAMNotFound;
    }
    else if (pEncryptInfo->status == 0x05 || pEncryptInfo->status == 0x20 || pEncryptInfo->status == 0x21)
    {
      encryptEventData.subEvent = tiNVRAMAccessTimeout;
    }
    else
    {
      encryptEventData.subEvent = tiNVRAMWriteFail;
    }

    encryptEventData.pData = agNULL;
    ostiPortEvent(tiRoot,
                  tiEncryptOperation,
                  securityModeStatus,
                  &encryptEventData);

    break;
  }
  case OSSA_HW_EVENT_MODE:
  {
    pModeEvent = (agsaHWEventMode_t *) eventParm2;
    pModePage = (bit32 *) pModeEvent->modePage;

    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_MODE modePageOperation 0x%x status 0x%x modePageLen 0x%x\n",
              pModeEvent->modePageOperation, pModeEvent->status, pModeEvent->modePageLen));

    if (pModeEvent->modePageOperation == agsaModePageSet)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_MODE page code 0x%x error qualifier 0x%x\n", (eventParm1 & 0xFF), (eventParm1 >> 16)));
      ostiPortEvent(tiRoot,
                    tiModePageOperation,
                    pModeEvent->status,
                    eventParm2);
    }
    else if (pModeEvent->modePageOperation == agsaModePageGet)
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_MODE error qualifier 0x%x\n", eventParm1));
      switch ((*pModePage) & 0xFF)
      {
      case AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE:
        TI_DBG1(("ossaHwCB: AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE 0x%x %p\n", pModeEvent->status,eventParm2));
        TI_DBG1(("ossaHwCB:modePageOperation 0x%x status 0x%x modePageLen 0x%x modePage %p context %p\n",
                      pModeEvent->modePageOperation,
                      pModeEvent->status,
                      pModeEvent->modePageLen,
                      pModeEvent->modePage,
                      pModeEvent->context));
        ostiPortEvent(tiRoot,
                      tiModePageOperation,
                      pModeEvent->status,
                      eventParm2);
        break;
      case AGSA_ENCRYPTION_DEK_CONFIG_PAGE:
        TI_DBG1(("ossaHwCB: AGSA_ENCRYPTION_DEK_CONFIG_PAGE 0x%x %p\n", pModeEvent->status,eventParm2));
        ostiPortEvent(tiRoot,
                      tiModePageOperation,
                      pModeEvent->status,
                      eventParm2);
        break;
      case AGSA_ENCRYPTION_HMAC_CONFIG_PAGE:
        TI_DBG1(("ossaHwCB: AGSA_ENCRYPTION_HMAC_CONFIG_PAGE 0x%x %p\n", pModeEvent->status,eventParm2));
        ostiPortEvent(tiRoot,
                      tiModePageOperation,
                      pModeEvent->status,
                      eventParm2);
        break;
      case AGSA_ENCRYPTION_CONTROL_PARM_PAGE:
        TI_DBG1(("ossaHwCB: AGSA_ENCRYPTION_CONTROL_PARM_PAGE 0x%x %p\n", pModeEvent->status,eventParm2));
        /*
         * This page is directly related to tiCOMEncryptGetInfo() and
         * will be translated into a tiEncrytOperation for the OS layer.
         */

        /* Fill out tiEncryptInfo_t */
        securityMode = *pModePage & 0x0F00 >> 8;
        cipherMode = *pModePage & 0xF000 >> 12;

        if (securityMode == agsaEncryptSMA)
        {
          encryptInfo.securityCipherMode = TI_ENCRYPT_SEC_MODE_A;
        }
        else if (securityMode == agsaEncryptSMB)
        {
          encryptInfo.securityCipherMode = TI_ENCRYPT_SEC_MODE_B;
        }
        else
        {
          encryptInfo.securityCipherMode = TI_ENCRYPT_SEC_MODE_FACT_INIT;
        }

        if (cipherMode == agsaEncryptCipherModeECB)
        {
          encryptInfo.securityCipherMode |= TI_ENCRYPT_ATTRIB_CIPHER_ECB;
        }

        if (cipherMode == agsaEncryptCipherModeXTS)
        {
          encryptInfo.securityCipherMode |= TI_ENCRYPT_ATTRIB_CIPHER_XTS;
        }

        /* How will subEvents be tracked? */
        encryptInfo.status = 0;

        encryptInfo.sectorSize[0] = 512;  /* DIF is allowed on 512 BPS SATA drives */
        encryptInfo.sectorSize[1] = 520;
        encryptInfo.sectorSize[2] = 528;
        encryptInfo.sectorSize[3] = 4104;
        encryptInfo.sectorSize[4] = 4168;
        encryptInfo.sectorSize[5] = 4232;

        encryptEventData.encryptEvent = tiEncryptGetInfo;
        encryptEventData.subEvent = 0;
        encryptEventData.pData = &encryptInfo;

        ostiPortEvent(tiRoot,
                    tiEncryptOperation,
                    pModeEvent->status,
                    &encryptEventData);
        break;
      case AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE:
        TI_DBG1(("ossaHwCB: AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE 0x%x %p\n", pModeEvent->status,eventParm2));

#ifdef IOCTL_INTERRUPT_TIME_CONFIG
         ostiPortEvent(tiRoot,
                    tiModePageOperation,
                    pModeEvent->status,
                    eventParm2
                 );
#endif /* IOCTL_INTERRUPT_TIME_CONFIG */

        /*ostiPortEvent(tiRoot,
                    tiModePageOperation,
                    pModeEvent->status,
                    &encryptEventData);*/
        break;
      case AGSA_INTERRUPT_CONFIGURATION_PAGE:
        TI_DBG1(("ossaHwCB: AGSA_INTERRUPT_CONFIGURATION_PAGE 0x%x %p\n", pModeEvent->status,eventParm2));

#ifdef IOCTL_INTERRUPT_TIME_CONFIG
        ostiPortEvent(tiRoot,
                    tiModePageOperation,
                    pModeEvent->status,
                    eventParm2
                    );
#endif /* IOCTL_INTERRUPT_TIME_CONFIG */

        break;
      default:
        TI_DBG1(("ossaHwCB: Unknown Mode Event %x\n", *pModePage));
         break;
      }

    }
    else
    {
      TI_DBG1(("ossaHwCB: Unknown modePageOperation %x\n", pModeEvent->modePageOperation));
    }
    break;
  }

// #endif  /* INITIATOR_DRIVER */

#ifdef REMOVED
  case OSSA_HW_EVENT_PHY_UNRECOVERABLE_ERROR:
  {
    PhyID = TD_GET_PHY_ID(eventParm1);
    PortState = TD_GET_PORT_STATE(eventParm1);

    TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_PHY_UNRECOVERABLE_ERROR\n"));

    if (PortState == OSSA_PORT_INVALID)
    {
      TI_DBG1(("ossaHwCB: INVALID port state\n"));
    }
    else
    {
      TI_DBG1(("ossaHwCB: VALID port state\n"));
    }
    break;
  }
#endif /* REMOVED */
    case OSSA_HW_EVENT_OPEN_RETRY_BACKOFF_THR_ADJUSTED:
    {
      TI_DBG1(("ossaHwCB: OSSA_HW_EVENT_OPEN_RETRY_BACKOFF_THR_ADJUSTED\n"));
      break;
    }

    default:
    {
      TI_DBG1(("ossaHwCB: default error (0x%X)!!!!!\n",event));
      break;
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'R', "Y2");
  return;
}

osGLOBAL void ossaPortControlCB(
                  agsaRoot_t          *agRoot,
                  agsaContext_t       *agContext,
                  agsaPortContext_t   *agPortContext,
                  bit32               portOperation,
                  bit32               status)
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaPortContext_t   *onePortContext = agNULL;

  TI_DBG6(("ossaPortControlCB: start\n"));

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y3");
  if (portOperation == AGSA_PORT_SET_SMP_PHY_WIDTH)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_PORT_SET_SMP_PHY_WIDTH\n"));
  }
  else if (portOperation == AGSA_PORT_SET_PORT_RECOVERY_TIME)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_PORT_SET_PORT_RECOVERY_TIME\n"));
  }
  else if (portOperation == AGSA_PORT_IO_ABORT)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_PORT_IO_ABORT\n"));
    /* code is here because disocvery failed
       deregister all targets. Then, later call discovery if broacast is seen in ossaDeregisterDeviceHandleCB.
    */
    onePortContext = (tdsaPortContext_t *)agPortContext->osData;
    if (onePortContext == agNULL)
    {
      TI_DBG1(("ossaPortControlCB: onePortContext is NULL\n"));
      return;
    }
    /* qqqqq deregister all devices */
   tdsaDeregisterDevicesInPort(tiRoot, onePortContext);

  }
  else if (portOperation == AGSA_PORT_SET_PORT_RESET_TIME)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_PORT_SET_PORT_RESET_TIME\n"));
  }
  else if (portOperation == AGSA_PORT_HARD_RESET)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_PORT_HARD_RESET\n"));
  }
  else if (portOperation == AGSA_PORT_CLEAN_UP)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_PORT_CLEAN_UP\n"));
  }
  else if (portOperation == AGSA_STOP_PORT_RECOVERY_TIMER)
  {
    TI_DBG1(("ossaPortControlCB: portOperation AGSA_STOP_PORT_RECOVERY_TIMER\n"));
  }
  else
  {
    TI_DBG1(("ossaPortControlCB: undefined portOperation %d\n", portOperation));
  }

  TI_DBG1(("ossaPortControlCB: status %d\n", status));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y3");
  return;
}

/*****************************************************************************
*! \brief  ossaHwRegRead
*
*  Purpose: This routine is called to read a 32-bit value from the PCI
*           registers of the controller
*
*  \param  agRoot:       Pointer to chip/driver Instance.
*  \param  regOffset:    Byte offset to chip register from which to read a 32-bit
*                        value.
*
*  \return:             32-bit value.
*
*  \note - The scope is shared target and initiator.
*
*****************************************************************************/
FORCEINLINE
bit32
ossaHwRegRead(agsaRoot_t *agRoot,
              bit32      regOffset
              )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);
  bit32 return_value;


  return_value =  ostiChipReadBit32 (
                             osData->tiRoot,
                             regOffset
                             );
  if( agNULL != agRoot->sdkData )
  {
    smTrace(hpDBG_REGISTERS,"RR",regOffset);
    /* TP:RR regOffset */
    smTrace(hpDBG_REGISTERS,"RV",return_value);
    /* TP:RV value read */
  }

  return(return_value);

}

/*****************************************************************************
*! \brief  ossaHwRegWrite
*
*  Purpose: This routine is called to write a 32-bit value to the PCI
*           registers of the controller.
*
*  \param   agRoot:     Pointer to chip/driver Instance.
*  \param   regOffset:  Byte offset to chip register to which chipIOValue is
*                       written.
*  \param   regValue:   32-bit value to write at chipIOOffset in host byte order.
*
*  \return:             None.
*
*  \note - The scope is shared target and initiator.
*
*****************************************************************************/
FORCEINLINE
void
ossaHwRegWrite(agsaRoot_t *agRoot,
               bit32      regOffset,
               bit32      regValue
               )
{

  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);
  if( agNULL != agRoot->sdkData )
  {
    smTrace(hpDBG_REGISTERS,"RW",regOffset);
    /* TP:RW regOffset */
    smTrace(hpDBG_REGISTERS,"VW",regValue);
    /* TP:VW value written */
  }

  ostiChipWriteBit32 (
                      osData->tiRoot,
                      regOffset,
                      regValue
                      );
  return;
}

/*****************************************************************************
*! \brief  ossaHwRegReadExt
*
*  Purpose: This routine is called to read a 32-bit value from a bus-specific
*           mapped registers of the controller
*
*  \param  agRoot:       Pointer to chip/driver Instance.
*  \param  regOffset:    Byte offset to chip register from which to read a 32-bit
*                        value.
*
*  \return:             32-bit value.
*
*  \note - The scope is shared target and initiator.
*
*****************************************************************************/
FORCEINLINE
bit32
ossaHwRegReadExt(
                 agsaRoot_t  *agRoot,
                 bit32       busBaseNumber,
                 bit32       regOffset
                 )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);

  bit32 return_value;

  return_value = ostiChipReadBit32Ext(
                               osData->tiRoot,
                               busBaseNumber,
                               regOffset
                               );

  /* TI_DBG4(("#_R: 0x%x:0x%x=0x%x\n",busBaseNumber,regOffset,return_value)); */

  if( agNULL != agRoot->sdkData )
  {
    smTrace(hpDBG_REGISTERS,"EB",busBaseNumber);
    /* TP:EB EX read busBaseNumber */
    smTrace(hpDBG_REGISTERS,"EO",regOffset);
    /* TP:EO regOffset */
    smTrace(hpDBG_REGISTERS,"ER",return_value);
    /* TP:ER value read */
  }
  return(return_value);
}

void ossaPCI_TRIGGER(agsaRoot_t  *agRoot )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);
  ostiPCI_TRIGGER(osData->tiRoot);

}



/*****************************************************************************
*! \brief  ossaHwRegWriteExt
*
*  Purpose: This routine is called to write a 32-bit value to a bus specific
*           mapped registers of the controller.
*
*  \param   agRoot:     Pointer to chip/driver Instance.
*  \param   regOffset:  Byte offset to chip register to which chipIOValue is
*                       written.
*  \param   regValue:   32-bit value to write at chipIOOffset in host byte order.
*
*  \return:             None.
*
*  \note - The scope is shared target and initiator.
*
*****************************************************************************/
FORCEINLINE
void
ossaHwRegWriteExt(
                  agsaRoot_t  *agRoot,
                  bit32       busBaseNumber,
                  bit32       regOffset,
                  bit32       regValue
                  )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);
  ostiChipWriteBit32Ext(
                        osData->tiRoot,
                        busBaseNumber,
                        regOffset,
                        regValue
                        );

  /*  TI_DBG4(("#_W: 0x%x:0x%x=0x%x\n",busBaseNumber,regOffset,regValue)); */

  if( agNULL != agRoot->sdkData )
  {
    smTrace(hpDBG_REGISTERS,"Eb",busBaseNumber);
    /* TP:Eb Ex Write busBaseNumber */
    smTrace(hpDBG_REGISTERS,"Eo",regOffset);
    /* TP:Eo regOffset */
    smTrace(hpDBG_REGISTERS,"Ew",regValue);
    /* TP:Ew value written  regValue*/
  }
  return;
}


osGLOBAL bit32 ossaHwRegReadConfig32(
              agsaRoot_t  *agRoot,
              bit32       regOffset
              )
{
  tdsaRootOsData_t *osData = (tdsaRootOsData_t *) (agRoot->osData);
  bit32 to_ret;
  to_ret= ostiChipConfigReadBit32( osData->tiRoot, regOffset);
  TI_DBG4(("ossaHwRegReadConfig32: regOffset 0x%x returns 0x%x\n",regOffset,to_ret));
  return(to_ret);
}




#ifdef TD_INT_COALESCE
void
ossaIntCoalesceInitCB(
                      agsaRoot_t                *agRoot,
                      agsaIntCoalesceContext_t    *agIntCoContext,
                      bit32                   status
                      )
{
  tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)osData->tdsaAllShared;
  tiIntCoalesceContext_t    *tiIntCoalesceCxt;
  tdsaIntCoalesceContext_t  *tdsaIntCoalCxt;
  tdsaIntCoalesceContext_t  *tdsaIntCoalCxtHead
    = (tdsaIntCoalesceContext_t *)tdsaAllShared->IntCoalesce;;
  bit32                     tiStatus;

  TI_DBG2(("ossaIntCoalesceInitCB: start\n"));

  tdsaIntCoalCxt = (tdsaIntCoalesceContext_t *)agIntCoContext->osData;
  tiIntCoalesceCxt = tdsaIntCoalCxt->tiIntCoalesceCxt;
  switch (status)
  {
  case AGSA_RC_SUCCESS:
    tiStatus = tiSuccess;
    break;
  case AGSA_RC_BUSY:
    tiStatus = tiBusy;
    break;
  case AGSA_RC_FAILURE:
    tiStatus = tiError;
    break;
  default:
    TI_DBG1(("ossaIntCoalesceInitCB: unknown status %d\n", status));
    tiStatus = tiError;
    break;
  }

  TI_DBG2(("ossaIntCoalesceInitCB: status %d\n", tiStatus));

  /* enqueue tdsaIntCoalCxt to freelink */
  tdsaIntCoalCxt->tiIntCoalesceCxt = agNULL;
  TI_DBG2(("ossaIntCoalesceInitCB: id %d\n", tdsaIntCoalCxt->id));

  tdsaSingleThreadedEnter(tiRoot, TD_INTCOAL_LOCK);
  TDLIST_DEQUEUE_THIS(&(tdsaIntCoalCxt->MainLink));
  TDLIST_ENQUEUE_AT_TAIL(&(tdsaIntCoalCxt->FreeLink), &(tdsaIntCoalCxtHead->FreeLink));
  tdsaSingleThreadedLeave(tiRoot, TD_INTCOAL_LOCK);

#ifdef OS_INT_COALESCE
  ostiInitiatorIntCoalesceInitCB(tiRoot,
                                 tiIntCoalesceCxt,
                                 tiStatus);
#endif

  TI_DBG2(("ossaIntCoalesceInitCB: return end\n"));

  return;
}
#endif /* TD_INT_COALESCE */

/*****************************************************************************/
/*! \brief ossaSingleThreadedEnter
 *
 *
 * Purpose: This routine is called to ensure that only a single thread of
 *          the given port instance executes code in the region protected by
 *          this function.
 *
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   syncLockId    to be explained.
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *
 */
/*****************************************************************************/
FORCEINLINE
void ossaSingleThreadedEnter(
     agsaRoot_t *agRoot,
     bit32  syncLockId
     )
{
  tdsaRootOsData_t *pOsData = agNULL;
  tiRoot_t  *ptiRoot = agNULL;
  tdsaContext_t *tdsaAllShared = agNULL;

  TD_ASSERT(agRoot, "agRoot");
  pOsData = (tdsaRootOsData_t *) (agRoot->osData);
  TD_ASSERT(pOsData, "pOsData");
  ptiRoot = pOsData->tiRoot;
  TD_ASSERT(ptiRoot, "ptiRoot");

  tdsaAllShared = (tdsaContext_t *)pOsData->tdsaAllShared;
  TD_ASSERT(tdsaAllShared, "tdsaAllShared");

  ostiSingleThreadedEnter(ptiRoot, syncLockId + tdsaAllShared->MaxNumOSLocks);
  return;
}

/*****************************************************************************/
/*! \brief ossaSingleThreadedLeave
 *
 *
 *  Purpose: This routine is called to leave a critical region of code
 *           previously protected by a call to osSingleThreadedEnter()
 *
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   syncLockId    to be explained.
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *
 */
/*****************************************************************************/
FORCEINLINE
void ossaSingleThreadedLeave(
     agsaRoot_t *agRoot,
     bit32  syncLockId
     )
{
  tdsaRootOsData_t *pOsData = agNULL;
  tiRoot_t  *ptiRoot = agNULL;
  tdsaContext_t *tdsaAllShared = agNULL;

  TD_ASSERT(agRoot, "agRoot");
  pOsData = (tdsaRootOsData_t *) (agRoot->osData);
  TD_ASSERT(pOsData, "pOsData");
  ptiRoot = pOsData->tiRoot;
  TD_ASSERT(ptiRoot, "ptiRoot");

  tdsaAllShared = (tdsaContext_t *)pOsData->tdsaAllShared;
  TD_ASSERT(tdsaAllShared, "tdsaAllShared");

  ostiSingleThreadedLeave(ptiRoot, syncLockId + tdsaAllShared->MaxNumOSLocks);
  return;
}

#ifdef PERF_COUNT
osGLOBAL void ossaEnter(agsaRoot_t *agRoot, int io)
{
  ostiEnter(((tdsaRootOsData_t*)(agRoot->osData))->tiRoot, 0, io);
  return;
}

osGLOBAL void ossaLeave(agsaRoot_t *agRoot, int io)
{
  ostiLeave(((tdsaRootOsData_t*)(agRoot->osData))->tiRoot, 0, io);
  return;
}
#endif


osGLOBAL void
ossaSSPIoctlCompleted(
                        agsaRoot_t                        *agRoot,
                        agsaIORequest_t           *agIORequest,
                        bit32                             agIOStatus,
                        bit32                             agIOInfoLen,
                        void                              *agParam,
                        bit16                             sspTag,
                        bit32                             agOtherInfo
                   )
{
  tdsaRootOsData_t                              *osData           = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                                              *tiRoot           = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t                             *tdIORequestBody  = (tdIORequestBody_t *)agIORequest->osData;
  agsaSASRequestBody_t                  *agSASRequestBody = agNULL;
  agsaSSPInitiatorRequest_t             *agSSPFrame       = agNULL;
  bit8                          scsiOpcode        = 0;

  agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
  agSSPFrame = &(agSASRequestBody->sspInitiatorReq);
  scsiOpcode = agSSPFrame->sspCmdIU.cdb[0];

  TI_DBG2(("ossaSSPIoctlCompleted: start\n"));

  if (agIOStatus == OSSA_SUCCESS)
  {
    TI_DBG2(("ossaSSPIoctlCompleted: Success status\n"));
  }
  else
  {
    TI_DBG1(("ossaSSPIoctlCompleted: Status 0x%x\n", agIOStatus));
  }
  switch(scsiOpcode)
  {
  case REPORT_LUN_OPCODE:
    ostiNumOfLUNIOCTLRsp(tiRoot, agIOStatus);
        break;

  default:
        TI_DBG1(("ossaSSPIoctlCompleted: Unsupported SCSI command Response  0x%x\n",scsiOpcode));
        break;
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yi");
  return;

}

osGLOBAL void
ossaSMPIoctlCompleted(
                 agsaRoot_t            *agRoot,
                 agsaIORequest_t       *agIORequest,
                 bit32                 agIOStatus,
                 bit32                 agIOInfoLen,
                 agsaFrameHandle_t     agFrameHandle
                 )
{
        tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
        tiRoot_t                        *tiRoot = (tiRoot_t *)osData->tiRoot;
        TI_DBG2(("ossaSMPIoctlCompleted: start\n"));

        if (agIOStatus == OSSA_SUCCESS)
        {
          TI_DBG2(("ossaSMPIoctlCompleted: Success status\n"));
        }
        else
        {
          TI_DBG1(("ossaSMPIoctlCompleted: Status 0x%x\n", agIOStatus));
        }

        ostiSendSMPIOCTLRsp(tiRoot, agIOStatus);
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yi");
        return;

}


/*****************************************************************************/
/*! \brief ossaSMPCompleted
 *
 *
 *  Purpose: This routine is called by lower layer to indicate the completion of
 *           SMP request
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agIORequest   Pointer to SMP request handle
 *  \param   agIOStatus    Status
 *  \param   agFrameHeader:Pointer to SMP frame header.
 *  \param   agIOInfoLen   IO information length assoicated with the IO
 *  \param   agFrameHandle A Handle used to refer to the response frame
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
osGLOBAL void ossaSMPCompleted(
                 agsaRoot_t            *agRoot,
                 agsaIORequest_t       *agIORequest,
                 bit32                 agIOStatus,
                 bit32                 agIOInfoLen,
                 agsaFrameHandle_t     agFrameHandle
                 )
{
#ifdef PASSTHROUGH
  tdsaRootOsData_t         *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                 *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdPassthroughCmndBody_t  *tdPTCmndBody  = (tdPassthroughCmndBody_t *)agIORequest->osData;
  bit32                    tiStatus = tiPassthroughError;
  bit8                     SMPframe[agIOInfoLen + sizeof(agsaSMPFrameHeader_t)];
  bit8                     SMPpayload[agIOInfoLen];

  TI_DBG2(("ossaSMPCompleted: start and passthrough\n"));
#else /* not PASSTHROUGH */

  tdssSMPRequestBody_t *pSMPRequestBody = (tdssSMPRequestBody_t *) agIORequest->osData;
  TI_DBG4(("ossaSMPCompleted: start\n"));
#endif /* end not PASSTHROUGH */

  TDSA_OUT_ENTER((tiRoot_t *)((tdsaRootOsData_t *)agRoot->osData)->tiRoot);
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y4");

#ifdef PASSTHROUGH
  if (tdPTCmndBody == agNULL)
  {
    TI_DBG1(("ossaSMPCompleted: tdPTCmndBody is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y4");
    goto ext;
  }

  if (tdPTCmndBody->EventCB == agNULL)
  {
    TI_DBG1(("ossaSMPCompleted: tdPTCmndBody->EventCB is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Y4");
    goto ext;
  }

  if (agIOStatus == OSSA_IO_SUCCESS)
  {
    tiStatus = tiPassthroughSuccess;
  }
  else if (agIOStatus == OSSA_IO_ABORTED)
  {
    tiStatus = tiPassthroughAborted;
  }
  else
  {
    tiStatus = tiPassthroughError;
  }

  osti_memset(SMPpayload, 0, agIOInfoLen);
  osti_memset(SMPframe, 0, agIOInfoLen + sizeof(agsaSMPFrameHeader_t));

  /* combine the header and payload */
  saFrameReadBlock(agRoot, agFrameHandle, 0, &SMPpayload, agIOInfoLen);
  osti_memcpy(SMPframe, agFrameHeader, sizeof(agsaSMPFrameHeader_t));
  osti_memcpy(SMPframe+sizeof(agsaSMPFrameHeader_t), SMPpayload, agIOInfoLen);

  tdPTCmndBody->EventCB(tiRoot,
                        tdPTCmndBody->tiPassthroughRequest,
                        tiStatus,
                        SMPframe,
                        agIOInfoLen + sizeof(agsaSMPFrameHeader_t)
                        );


#else /* not PASSTHROUGH */

  /*
    At initiator, passing SMP to TD layer, itdssSMPCompleted(), which does nothing.
    At target, passing SMP to TD layer, ttdsaSMPCompleted()
  */
  /*
     how to use agFrameHandle, when saFrameReadBlock() is used
  */

  /* SPC can't be SMP target */

  TI_DBG4(("ossaSMPCompleted: start\n"));

  if (pSMPRequestBody == agNULL)
  {
    TI_DBG1(("ossaSMPCompleted: pSMPRequestBody is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Y4");
    goto ext;
  }

  if (pSMPRequestBody->SMPCompletionFunc == agNULL)
  {
    TI_DBG1(("ossaSMPCompleted: pSMPRequestBody->SMPCompletionFunc is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "Y4");
    goto ext;
  }
#ifdef TD_INTERNAL_DEBUG /* debugging */
  TI_DBG4(("ossaSMPCompleted: agIOrequest %p\n", agIORequest->osData));
  TI_DBG4(("ossaSMPCompleted: sizeof(tdIORequestBody_t) %d 0x%x\n", sizeof(tdIORequestBody_t),
           sizeof(tdIORequestBody_t)));
  TI_DBG4(("ossaSMPCompleted: SMPRequestbody %p\n", pSMPRequestBody));
  TI_DBG4(("ossaSMPCompleted: calling callback fn\n"));
  TI_DBG4(("ossaSMPCompleted: callback fn %p\n",pSMPRequestBody->SMPCompletionFunc));
#endif /* TD_INTERNAL_DEBUG */
  /*
    if initiator, calling itdssSMPCompleted() in itdcb.c
    if target,    calling ttdsaSMPCompleted() in ttdsmp.c
  */
  pSMPRequestBody->SMPCompletionFunc(
                                     agRoot,
                                     agIORequest,
                                     agIOStatus,
                                     agIOInfoLen,
                                     agFrameHandle
                                     );

#endif /* Not PASSTHROUGH */

  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "Y4");
ext:
  TDSA_OUT_LEAVE((tiRoot_t *)((tdsaRootOsData_t *)agRoot->osData)->tiRoot);
  return;
}

osGLOBAL void
ossaSMPReqReceived(
                   agsaRoot_t           *agRoot,
                   agsaDevHandle_t      *agDevHandle,
                   agsaFrameHandle_t    agFrameHandle,
                   bit32                agIOInfoLen,
                   bit32                phyId
                   )
{
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y5");
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y5");
  return;
}

/*****************************************************************************/
/*! \brief ossaSMPCAMCompleted
 *
 *
 *  Purpose: This routine is called by lower layer to indicate the completion of
 *           SMP request
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agIORequest   Pointer to SMP request handle
 *  \param   agIOStatus    Status
 *  \param   agIOInfoLen   IO information length assoicated with the IO
 *  \param   agFrameHandle A Handle used to refer to the response frame
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
osGLOBAL void ossaSMPCAMCompleted(
                 agsaRoot_t            *agRoot,
                 agsaIORequest_t       *agIORequest,
                 bit32                 agIOStatus,
                 bit32                 agIOInfoLen,
                 agsaFrameHandle_t     agFrameHandle
                 )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t   *tdSMPRequestBody  = agNULL;
  bit32                context = osData->IntContext;
  tiSMPStatus_t        status;
  bit32               PhysUpper32;
  bit32               PhysLower32;
  bit32               memAllocStatus;
  void                *osMemHandle;
  bit32               *SMPpayload;
  TI_DBG2(("ossaSMPCAMCompleted: start\n"));
  TI_DBG2(("ossaSMPCAMCompleted: agIOInfoLen %d\n", agIOInfoLen));
  if (!agIORequest->osData)
  {
    TD_ASSERT((0), "ossaSMPCAMCompleted agIORequest->osData");
    goto ext;
  }
  tdSMPRequestBody = (tdIORequestBody_t *)agIORequest->osData;
  if (tdSMPRequestBody->tiIORequest->osData == agNULL)
  {
    TI_DBG1(("ossaSMPCAMCompleted: tdIORequestBody->tiIORequest->osData is null, wrong\n"));
    goto ext;
  }
  /* allocating agIORequest for SMP Payload itself */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&SMPpayload,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   agIOInfoLen,
                                   agTRUE
                                   );
  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    TI_DBG1(("ossaSMPCAMCompleted: ostiAllocMemory failed...\n"));
    goto ext;
  }
  if (SMPpayload == agNULL)
  {
    TI_DBG1(("ossaSMPCAMCompleted: ostiAllocMemory returned NULL SMPpayload\n"));
    goto ext;
  }
  if (agIOStatus == OSSA_IO_SUCCESS)
  {
    TI_DBG1(("ossaSMPCAMCompleted: Success status\n"));
    osti_memset(SMPpayload, 0, agIOInfoLen);
    TI_DBG1(("ossaSMPCAMCompleted: after memset\n"));
    saFrameReadBlock(agRoot, agFrameHandle, 0, SMPpayload, agIOInfoLen);
    TI_DBG1(("ossaSMPCAMCompleted: after read \n"));
    status = tiSMPSuccess;
  }
  else if (agIOStatus == OSSA_IO_ABORTED)
  {
    TI_DBG1(("ossaSMPCAMCompleted: SMP Aborted status\n"));
    status = tiSMPAborted;
    TI_DBG1(("ossaSMPCAMCompleted: failed status=%d\n", status));
    //failed to send smp command, we need to free the memory
    ostiFreeMemory(
                  tiRoot,
                  osMemHandle,
                  agIOInfoLen
                  );
  }
  else
  {
    TI_DBG1(("ossaSMPCAMCompleted: SMP failed status\n"));
    status = tiSMPFailed;
    TI_DBG1(("ossaSMPCAMCompleted: failed status=%d\n", status));
    //failed to send smp command, we need to free the memory
    ostiFreeMemory(
                  tiRoot,
                  osMemHandle,
                  agIOInfoLen
                  );
  }
  ostiInitiatorSMPCompleted(tiRoot,
                            tdSMPRequestBody->tiIORequest,
                            status,
                            agIOInfoLen,
                            SMPpayload,
                            context
                            );
  ext:
  TDSA_OUT_LEAVE((tiRoot_t*)((tdsaRootOsData_t*)agRoot->osData)->tiRoot);
  return;
}
#ifdef REMOVED
#ifdef TARGET_DRIVER
/*****************************************************************************/
/*! \brief ossaSMPReqReceived
 *
 *
 *  Purpose: This routine is called by lower layer to indicate the reception of
 *           SMP request
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agDevHandle   Pointer to the device handle of the device
 *  \param   agFrameHandle A Handle used to refer to the response frame
 *
 *
 *  \return None.
 *
 *  \note - The scope is target only
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
osGLOBAL void ossaSMPReqReceived(
                   agsaRoot_t           *agRoot,
                   agsaDevHandle_t      *agDevHandle,
                   agsaFrameHandle_t    agFrameHandle,
                   bit32                 agFrameLength,
                   bit32                phyId
                   )
{
  bit8                   smpHeader[4];
  agsaSMPFrameHeader_t   *agFrameHeader;
#ifdef PASSTHROUGH
  /* call the registered function(parameter in tiTGTPassthroughCmndRegister() by target */
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  ttdsaTgt_t             *Target = (ttdsaTgt_t *)osData->ttdsaTgt;

  bit8                   SMPframe[agIOInfoLen + sizeof(agsaSMPFrameHeader_t)];
  bit8                   SMPpayload[agIOInfoLen];

  TI_DBG2(("ossaSMPReqReceived: start and passthrough\n"));
  osti_memset(SMPpayload, 0, agIOInfoLen);
  osti_memset(SMPframe, 0, agIOInfoLen + sizeof(agsaSMPFrameHeader_t));
  /* combine smp header and payload */
  saFrameReadBlock(agRoot, agFrameHandle, 0, &SMPpayload, agIOInfoLen);
  osti_memcpy(SMPframe, agFrameHeader, sizeof(agsaSMPFrameHeader_t));
  osti_memcpy(SMPframe+sizeof(agsaSMPFrameHeader_t), SMPpayload, agIOInfoLen);

  Target->PasthroughCB(
                       tiRoot,
                       tiSASATA,
                       tiSMP,
                       tiSMPResponse,
                       SMPframe,
                       agIOInfoLen + sizeof(agsaSMPFrameHeader_t),
                       phyId
                       );

#else

  /*
    agDevHandle_t->osData points to tdssDeviceData_t
   */
  tdsaDeviceData_t *pDeviceData = (tdsaDeviceData_t *) agDevHandle->osData;

    saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
    agFrameHeader = (agsaSMPFrameHeader_t *)smpHeader;
  TI_DBG4(("ossaSMPReqReceived: start\n"));

  /* tdtypes.h, calling  ttdsaSMPReqReceived in ttdsmp.c */
  pDeviceData->pJumpTable->pSMPReqReceived (
                                            agRoot,
                                            agDevHandle,
                                            agFrameHeader,
                                            agFrameHandle,
                                            agFrameLength,
                                            phyId
                                            );
#endif
  return;
}
#endif
#endif

/*****************************************************************************/
/*! \brief ossaSSPCompleted
 *
 *
 *  Purpose: This routine is called by lower layer to indicate the completion of
 *           SSP request
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agIORequest   Pointer to SMP request handle
 *  \param   agIOStatus    Status
 *  \param   agIOInfoLen   IO information length assoicated with the IO
 *  \param   agFrameHandle A Handle used to refer to the response frame
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
FORCEINLINE
void ossaSSPCompleted(
                 agsaRoot_t          *agRoot,
                 agsaIORequest_t     *agIORequest,
                 bit32               agIOStatus,
                 bit32               agIOInfoLen,
                 void                *agParam,
                 bit16               sspTag,
                 bit32               agOtherInfo
                )
{
  tdIORequestBody_t  *pIORequestBody;
#ifdef TD_DEBUG_ENABLE
  tiDeviceHandle_t   *tiDeviceHandle = agNULL;
  tdsaDeviceData_t   *oneDeviceData = agNULL;
#endif

  TDSA_OUT_ENTER((tiRoot_t*)((tdsaRootOsData_t*)agRoot->osData)->tiRoot);
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2L");

  if(!agIORequest->osData)
  {
    TD_ASSERT((0), "ossaSSPCompleted agIORequest->osData");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2L");
    goto ext;
  }
  pIORequestBody = (tdIORequestBody_t *)agIORequest->osData;


  TI_DBG4(("ossaSSPCompleted: start\n"));

  if (pIORequestBody == agNULL)
  {
    TI_DBG1(("ossaSSPCompleted: pIORequestBody is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2L");
    goto ext;
  }
  if (pIORequestBody->IOCompletionFunc == agNULL)
  {
#ifdef TD_DEBUG_ENABLE
    tiDeviceHandle = pIORequestBody->tiDevHandle;
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif
    TI_DBG1(("ossaSSPCompleted: IOCompletionFunc is NULL \n"));
    TI_DBG1(("ossaSSPCompleted: did %d \n", oneDeviceData->id));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2L");
    goto ext;
  }

   /*
     if initiator, calling itdssIOCompleted() in itdcb.c
     if initiator, calling itdssTaskCompleted in itdcb.c
     if target,    calling ttdsaIOCompleted() in ttdio.c
   */
  pIORequestBody->IOCompletionFunc(
                                   agRoot,
                                   agIORequest,
                                   agIOStatus,
                                   agIOInfoLen,
                                   agParam,
                                   agOtherInfo
                                   );
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2L");
ext:
  TDSA_OUT_LEAVE((tiRoot_t*)((tdsaRootOsData_t*)agRoot->osData)->tiRoot);
  return;
}

#ifdef FAST_IO_TEST
GLOBAL void ossaFastSSPCompleted(
                 agsaRoot_t          *agRoot,
                 agsaIORequest_t     *cbArg,
                 bit32               agIOStatus,
                 bit32               agIOInfoLen,
                 void                *agParam,
                 bit16               sspTag,
                 bit32               agOtherInfo
                )
{
  agsaFastCBBuf_t    *safb = (agsaFastCBBuf_t*)cbArg;
  tdsaRootOsData_t *osData = (tdsaRootOsData_t*)agRoot->osData;
  tiRoot_t         *tiRoot = (tiRoot_t*)osData->tiRoot;
  bit32            scsi_status;
  bit32            data_status;
  bit32            respLen;
  bit8             respData[128];
  bit32            senseLen;
  agsaSSPResponseInfoUnit_t agSSPRespIU;

  TDSA_OUT_ENTER((tiRoot_t*)((tdsaRootOsData_t*)agRoot->osData)->tiRoot);
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y6");

  TI_DBG4(("ossaSSPCompleted: start\n"));

  if (safb->cb == agNULL || safb->cbArg == agNULL)
  {
    TI_DBG1(("ossaFastSSPCompleted: pIORequestBody is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y6");
    TD_ASSERT((0), "");
    goto ext;
  }

  switch (agIOStatus)
  {
    case OSSA_IO_SUCCESS:

      /* ~ itdssIOSuccessHandler */
      if ((agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t)))
      {
        ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, OSSA_IO_SUCCESS, 0);
        break;
      }

      /* reads agsaSSPResponseInfoUnit_t */
      saFrameReadBlock(agRoot, agParam, 0, &agSSPRespIU,
                       sizeof(agsaSSPResponseInfoUnit_t));

      data_status = SA_SSPRESP_GET_DATAPRES(&agSSPRespIU);
      scsi_status = agSSPRespIU.status;

      TI_DBG1(("itdssIOSuccessHandler: scsi_status %d\n", scsi_status));

      /* endianess is invovled here */
      senseLen = SA_SSPRESP_GET_SENSEDATALEN(&agSSPRespIU);
      respLen = SA_SSPRESP_GET_RESPONSEDATALEN(&agSSPRespIU);
      TI_DBG2(("itdssIOSuccessHandler: scsi status=0x%x, senselen=0x%x resplen "
               "0x%x\n", scsi_status, senseLen, respLen));

      if (agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen)
      {
        ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOFailed,
                                     tiDetailOtherError);
        break;
      }

      /* reads response data */
      saFrameReadBlock(agRoot, agParam, sizeof(agsaSSPResponseInfoUnit_t),
                       respData, respLen);
      /* reads sense data */
      saFrameReadBlock(agRoot, agParam, sizeof(agsaSSPResponseInfoUnit_t)
                       + respLen, safb->pSenseData, senseLen);

      if (data_status == 0)
      {
        /* NO_DATA */
        TI_DBG2(("ossaFastSSPCompleted: no data\n"));
        ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOSuccess,
                                     scsi_status);
        break;
      }

      if (data_status == 1)
      {
        /* RESPONSE_DATA */
        TI_DBG1(("ossaFastSSPCompleted: response data \n"));
        ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOSuccess, 0);
        break;
      }

      if (data_status == 2)
      {
        tiSenseData_t senseData;

        /* SENSE_DATA */
        TI_DBG2(("itdssIOSuccessHandler: sense data \n"));

        senseData.senseData = safb->pSenseData;
        senseData.senseLen = MIN(*(safb->senseLen), senseLen);

        /* when ASC = 0x04 - Log Unit Not Ready,
           and ASCQ = 0x11 - Enable Spinup Required:
           call saLocalPhyControl to notify spinup */
        if (((char*)safb->pSenseData)[12] == 0x04 &&
            ((char*)safb->pSenseData)[13] == 0x11)
        {
          int i;

          TI_DBG2(("ossaFastSSPCompleted: sending notfify spinup\n"));

          if (((tdsaDeviceData_t*)safb->oneDeviceData)->directlyAttached ==
               agTRUE)
          {
            for (i = 0; i < TD_MAX_NUM_NOTIFY_SPINUP; i++)
            {
              saLocalPhyControl(agRoot, agNULL, 0,
                                ((tdsaDeviceData_t*)safb->oneDeviceData)->phyID,
                                AGSA_PHY_NOTIFY_ENABLE_SPINUP,
                                agNULL);
            }
          }
        }

        if (*(safb->senseLen) > senseData.senseLen)
          *(safb->senseLen) = senseData.senseLen;
//       memcpy((void *)safb->pSenseData, senseData.senseData, safb->senseLen);

        ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOSuccess,
                                     scsi_status);
        break;
      }

      if (data_status == 3)
      {
        /* RESERVED */
        TI_DBG1(("ossaFastSSPCompleted: reserved wrong!!!\n"));

        ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOFailed,
                                     scsi_status);
        break;
      }
      break;
#ifdef REMOVED
    case OSSA_IO_OVERFLOW:
      ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOOverRun,
                                   agIOInfoLen);
      break;
#endif /* REMOVED */
    case OSSA_IO_UNDERFLOW:
      ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOUnderRun,
                                   agIOInfoLen);
      break;

    case OSSA_IO_ABORTED:
      ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOFailed,
                                   tiDetailAborted);
      break;
    case OSSA_IO_ABORT_RESET:
      ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOFailed,
                                   tiDetailAbortReset);
      break;
    case OSSA_IO_NO_DEVICE:
      ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOFailed,
                                   tiDetailNoLogin);
      break;
    case OSSA_IO_DS_NON_OPERATIONAL:
    {

      tdsaDeviceData_t *oneDeviceData;

      oneDeviceData = (tdsaDeviceData_t*)safb->oneDeviceData;
      if (oneDeviceData->valid == agTRUE &&
          oneDeviceData->registered == agTRUE &&
          oneDeviceData->tdPortContext != agNULL)
      {
        saSetDeviceState(oneDeviceData->agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData),
                         oneDeviceData->agDevHandle, SA_DS_OPERATIONAL);
      }
      /* fall through */
    }

    default:
      ((ostiFastSSPCb_t)safb->cb)(tiRoot, safb->cbArg, tiIOFailed,
                                   tiDetailOtherError);
      break;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Y6");

ext:
  TDSA_OUT_LEAVE((tiRoot_t*)((tdsaRootOsData_t*)agRoot->osData)->tiRoot);
  return;
} /* ossaFastSSPCompleted */
#endif

/*****************************************************************************/
/*! \brief ossaSSPReqReceived
 *
 *
 *  Purpose: This routine is called by lower layer to indicate the reception of
 *           SMP request
 *
 *  \param   agRoot:         Pointer to chip/driver Instance.
 *  \param   agDevHandle     Pointer to the device handle of the device
 *  \param   agFrameHandle   A Handle used to refer to the response frame
 *  \param   agInitiatorTag  the initiator tag
 *  \param   agFrameType     SSP frame type
 *
 *  \return none.
 *
 *  \note - The scope is target only
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
osGLOBAL void ossaSSPReqReceived(
                   agsaRoot_t           *agRoot,
                   agsaDevHandle_t      *agDevHandle,
                   agsaFrameHandle_t    agFrameHandle,
                   bit16                agInitiatorTag,
                   bit32                parameter,
                   bit32                agFrameLen
                   )
{
  /*
    at target only
    uses jumptable, not callback
  */
  /*
    agDevHandle_t->osData points to tdssDeviceData_t
  */
  tdsaDeviceData_t *pDeviceData = (tdsaDeviceData_t *) agDevHandle->osData;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y7");

  /* tdtypes.h, calling  ttdsaSSPReqReceived() in ttdio.c */
  pDeviceData->pJumpTable->pSSPReqReceived (
                                            agRoot,
                                            agDevHandle,
                                            agFrameHandle,
                                            agInitiatorTag,
                                            parameter,
                                            agFrameLen
                                            );
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y7");
  return;
}
/*****************************************************************************/
/*! \brief ossaStallThread
 *
 *
 *  Purpose: This routine is called to stall this thread for a number of
 *           microseconds.
 *
 *
 *  \param  agRoot:       Pointer to chip/driver Instance.
 *  \param   microseconds: Micro second to stall.
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaStallThread(agsaRoot_t *agRoot,
                bit32 microseconds
                )
{
  tdsaRootOsData_t *pOsData = (tdsaRootOsData_t *) (agRoot->osData);

  ostiStallThread (
                   pOsData->tiRoot,
                   microseconds
                   );
  return;
}


/*****************************************************************************
*! \brief  ossaSSPEvent
*
*   This routine is called to notify the OS Layer of an event associated with
*   SAS port or SAS device
*
*  \param   agRoot:         Handles for this instance of SAS/SATA hardware
*  \param   agIORequest     Pointer to IO request
*  \param   event:          event type
*  \param   agIOInfoLen:    not in use
*  \param   agFrameHandle:  not in use
*
*  \return: none
*
*****************************************************************************/
/* in case of CMD ACK_NAK timeout, send query task */
osGLOBAL void ossaSSPEvent(
             agsaRoot_t           *agRoot,
             agsaIORequest_t      *agIORequest,
             agsaPortContext_t    *agPortContext,
             agsaDevHandle_t      *agDevHandle,
             bit32                event,
             bit16                sspTag,
             bit32                agIOInfoLen,
             void                 *agParam
             )
{
#ifdef INITIATOR_DRIVER
  tdsaRootOsData_t            *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                    *tiRoot = (tiRoot_t *)osData->tiRoot;
  /*  bit32                       intContext = osData->IntContext; */
  void                        *osMemHandle;
  tdIORequestBody_t           *TMtdIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  bit32                       agRequestType;
  agsaIORequest_t             *agTMIORequest = agNULL;  /* task management itself */
  agsaSASRequestBody_t        *agSASRequestBody = agNULL;
  agsaSSPScsiTaskMgntReq_t    *agSSPTaskMgntRequest;
  bit32                       saStatus;
  bit32                       agIORequestType;  /* type of IO recevied */
  tiIORequest_t               *taskTag;                 /* being task managed one */
  tdIORequestBody_t           *tdIORequestBody;
#endif

#ifdef REMOVED
  tiDeviceHandle_t            *tiDeviceHandle;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  tdIORequestBody_t           *tdAbortIORequestBody;
#endif
  agsaDifDetails_t            agDifDetails;
  bit8                        framePayload[256];
#ifdef REMOVED
  bit16                       frameOffset = 0;
#endif
  bit16                       frameLen = 0;

  TI_DBG6(("ossaSSPEvent: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Y9");



  if (event == OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT ||
      event == OSSA_IO_XFER_ERROR_BREAK ||
      event == OSSA_IO_XFER_ERROR_PHY_NOT_READY
      )
  {

    /* IO being task managed(the original IO) depending on event */
#ifdef INITIATOR_DRIVER
    tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
    taskTag         = tdIORequestBody->tiIORequest;
#endif
#ifdef REMOVED
    tiDeviceHandle  = tdIORequestBody->tiDevHandle;
    oneDeviceData   = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif

#ifdef INITIATOR_DRIVER
    agIORequestType = tdIORequestBody->agRequestType;

    /* error checking; only command is expected here */
    if (agIORequestType == AGSA_REQ_TYPE_UNKNOWN)
    {
      TI_DBG1(("ossaSSPEvent: incorrect frame 0x%x. Should be command\n", agIORequestType));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Y9");
      return;
    }

    /* Allocate memory for query task management */
    memAllocStatus = ostiAllocMemory(
                                     tiRoot,
                                     &osMemHandle,
                                     (void **)&TMtdIORequestBody,
                                     &PhysUpper32,
                                     &PhysLower32,
                                     8,
                                     sizeof(tdIORequestBody_t),
                                     agTRUE
                                     );

    if (memAllocStatus != tiSuccess)
    {
      /* let os process IO */
      TI_DBG1(("ossaSSPEvent: ostiAllocMemory failed...\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Y9");
      return;
    }

    if (TMtdIORequestBody == agNULL)
    {
      /* let os process IO */
      TI_DBG1(("ossaSSPEvent: ostiAllocMemory returned NULL TMIORequestBody\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Y9");
      return;
    }

    /* setup task management structure */
    TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
    /* TD generates Query Task not OS layer */
    TMtdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag = agNULL;
    TMtdIORequestBody->IOType.InitiatorTMIO.TaskTag = taskTag;

    /* initialize callback function */
    TMtdIORequestBody->IOCompletionFunc = itdssQueryTaskCompleted;

    /* initialize tiDevhandle */
    TMtdIORequestBody->tiDevHandle = tdIORequestBody->tiDevHandle;


    /* initialize agIORequest */
    agTMIORequest = &(TMtdIORequestBody->agIORequest);
    agTMIORequest->osData = (void *) TMtdIORequestBody;
    agTMIORequest->sdkData = agNULL; /* LL takes care of this */

    /* request type */
    agRequestType = AGSA_SSP_TASK_MGNT_REQ;
    TMtdIORequestBody->agRequestType = AGSA_SSP_TASK_MGNT_REQ;

    /*
      initialize
      tdIORequestBody_t tdIORequestBody -> agSASRequestBody
    */
    agSASRequestBody = &(TMtdIORequestBody->transport.SAS.agSASRequestBody);
    agSSPTaskMgntRequest = &(agSASRequestBody->sspTaskMgntReq);

    /* fill up LUN field */
    osti_memset(agSSPTaskMgntRequest->lun, 0, 8);

    /* sets taskMgntFunction field */
    agSSPTaskMgntRequest->taskMgntFunction = AGSA_QUERY_TASK;
    /* debugging */
    if (TMtdIORequestBody->IOCompletionFunc == agNULL)
    {
      TI_DBG1(("ossaSSPEvent: Error !!! IOCompletionFunc is NULL\n"));
    }
    /* send query task management */
    saStatus = saSSPStart(agRoot,
                          agTMIORequest,
                          0,
                          agDevHandle,
                          agRequestType,
                          agSASRequestBody,
                          agIORequest,
                          &ossaSSPCompleted);

    if (saStatus != AGSA_RC_SUCCESS)
    {
      /* free up allocated memory */
      ostiFreeMemory(
                     tiRoot,
                     TMtdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
      TI_DBG1(("ossaSSPEvent: saSSPStart failed\n"));
      return;
    }
#endif
  }
#ifdef REMOVED
  else if (event == OSSA_IO_ABORTED)
  {
    TI_DBG2(("ossaSSPEvent: OSSA_IO_ABORTED\n"));
    /* clean up TD layer's IORequestBody */
    tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
    ostiFreeMemory(
                   tiRoot,
                   tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );

  }
  else if (event == OSSA_IO_NOT_VALID)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_NOT_VALID\n"));
    tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
    ostiFreeMemory(
                   tiRoot,
                   tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );


  }
#endif
  else if (event == OSSA_IO_XFER_CMD_FRAME_ISSUED)
  {
    TI_DBG2(("ossaSSPEvent: OSSA_IO_XFER_CMD_FRAME_ISSUED\n"));
  }
  else if (event == OSSA_IO_XFER_ERROR_OFFSET_MISMATCH)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_XFER_ERROR_OFFSET_MISMATCH\n"));
  }
  else if (event == OSSA_IO_OVERFLOW)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_OVERFLOW\n"));
    /*
    ??? can't call; missing agIOInfoLen
    ostiInitiatorIOCompleted (
                              tiRoot,
                              tdIORequestBody->tiIORequest,
                              tiIOOverRun,
                              agIOInfoLen,
                              agNULL,
                              intContext
                              );

    */

  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE\n"));
  }
  else if (event == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED\n"));
  }
  else if (event == OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH\n"));
  }
  else if (event == OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN)
  {
    TI_DBG1(("ossaSSPEvent: OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN\n"));
  }
  else if (event == OSSA_IO_XFR_ERROR_DIF_MISMATCH                 ||
           event == OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH ||
           event == OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH   ||
           event == OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH                 )
  {
    TI_DBG1(("ossaSSPEvent: DIF related, event 0x%x\n", event));
    /* process DIF detail information */
    TI_DBG2(("ossaSSPEvent: agIOInfoLen %d\n", agIOInfoLen));
    if (agParam == agNULL)
    {
      TI_DBG2(("ossaSSPEvent: agParam is NULL!!!\n"));
      return;
    }
    if (agIOInfoLen < sizeof(agsaDifDetails_t))
    {
      TI_DBG2(("ossaSSPEvent: wrong agIOInfoLen!!! agIOInfoLen %d sizeof(agsaDifDetails_t) %d\n", agIOInfoLen, (int)sizeof(agsaDifDetails_t)));
      return;
    }
    /* reads agsaDifDetails_t */
    saFrameReadBlock(agRoot, agParam, 0, &agDifDetails, sizeof(agsaDifDetails_t));
#ifdef REMOVED
    frameOffset = (agDifDetails.ErrBoffsetEDataLen & 0xFFFF);
#endif
    frameLen = (bit16)((agDifDetails.ErrBoffsetEDataLen & 0xFFFF0000) >> 16);

    TI_DBG2(("ossaSSPEvent: UpperLBA 0x%08x LowerLBA 0x%08x\n", agDifDetails.UpperLBA, agDifDetails.LowerLBA));
    TI_DBG2(("ossaSSPEvent: SASAddrHI 0x%08x SASAddrLO 0x%08x\n",
             TD_GET_SAS_ADDRESSHI(agDifDetails.sasAddressHi), TD_GET_SAS_ADDRESSLO(agDifDetails.sasAddressLo)));
    TI_DBG2(("ossaSSPEvent: DIF error mask 0x%x Device ID 0x%x\n",
             (agDifDetails.DIFErrDevID) & 0xFF, (agDifDetails.DIFErrDevID & 0xFFFF0000) >> 16));
    if (frameLen != 0 && frameLen <= 256)
    {
      saFrameReadBlock(agRoot, agParam, sizeof(agsaDifDetails_t), framePayload, frameLen);
      tdhexdump("ossaSSPEvent frame", framePayload, frameLen);
    }
  }
  else
  {
    TI_DBG1(("ossaSSPEvent: other event 0x%x\n", event));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "Y9");
  return;
}

#ifdef FDS_SM
osGLOBAL void ossaSATAIDAbortCB(
               agsaRoot_t               *agRoot,
               agsaIORequest_t          *agIORequest,
               bit32                    flag,
               bit32                    status)
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t       *tdAbortIORequestBody;

  TI_DBG1(("ossaSATAIDAbortCB: start flag %d status %d\n", flag, status));

  tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  /*
   triggered by tdIDStartTimerCB
  */
  ostiFreeMemory(
                 tiRoot,
                 tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                 sizeof(tdIORequestBody_t)
                );
  return;
}
#endif

#ifdef INITIATOR_DRIVER
osGLOBAL void ossaSSPAbortCB(
               agsaRoot_t               *agRoot,
               agsaIORequest_t          *agIORequest,
               bit32                    flag,
               bit32                    status)
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t       *tdAbortIORequestBody = agNULL;
  tdsaDeviceData_t        *oneDeviceData        = agNULL;
  tiDeviceHandle_t        *tiDeviceHandle       = agNULL;
  tiIORequest_t           *taskTag              = agNULL;

  TI_DBG2(("ossaSSPAbortCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Ya");

  tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  if (tdAbortIORequestBody == agNULL)
  {
    TI_DBG1(("ossaSSPAbortCB: tdAbortIORequestBody is NULL warning!!!!\n"));
    return;
  }

  if (flag == 2)
  {
    /* abort per port */
    TI_DBG1(("ossaSSPAbortCB: abort per port\n"));
  }
  else if (flag == 1)
  {
    TI_DBG2(("ossaSSPAbortCB: abort all\n"));

    tiDeviceHandle = (tiDeviceHandle_t *)tdAbortIORequestBody->tiDevHandle;
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("ossaSSPAbortCB: tiDeviceHandle is NULL warning!!!!\n"));
      ostiFreeMemory(
               tiRoot,
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(tdIORequestBody_t)
               );
      return;
    }

    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("ossaSSPAbortCB: oneDeviceData is NULL warning!!!!\n"));
      ostiFreeMemory(
               tiRoot,
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(tdIORequestBody_t)
               );
      return;
    }

    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG2(("ossaSSPAbortCB: OSSA_IO_SUCCESS\n"));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortOK,
                            agNULL);
      }
      else
      {
        TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NOT_VALID\n"));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortFailed,
                            agNULL );
      }
      else
      {
        TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NO_DEVICE\n"));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            agNULL );
      }
      else
      {
        TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            agNULL );
      }
      else
      {
        TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_DELAYED\n"));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortDelayed,
                            agNULL );
      }
      else
      {
        TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#endif
    else
    {
      TI_DBG1(("ossaSSPAbortCB: other status %d\n", status));
      /* clean up TD layer's IORequestBody */
      if (oneDeviceData->OSAbortAll == agTRUE)
      {
        oneDeviceData->OSAbortAll = agFALSE;
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            tiDeviceHandle,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            agNULL );
      }
      else
      {
        TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else if (flag == 0)
  {
    TI_DBG2(("ossaSSPAbortCB: abort one\n"));
    taskTag = tdAbortIORequestBody->tiIOToBeAbortedRequest;

    if ( taskTag == agNULL)
    {
      TI_DBG1(("ossaSSPAbortCB: taskTag is NULL; triggered by itdssQueryTaskCompleted\n"));
    }
    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG2(("ossaSSPAbortCB: OSSA_IO_SUCCESS\n"));
      if (taskTag != agNULL)
      {
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            agNULL,
                            tiIntrEventTypeLocalAbort,
                            tiAbortOK,
                            taskTag );
      }
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NOT_VALID\n"));

      if (taskTag != agNULL)
      {
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            agNULL,
                            tiIntrEventTypeLocalAbort,
                            tiAbortFailed,
                            taskTag );
      }
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NO_DEVICE\n"));

      if (taskTag != agNULL)
      {
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            agNULL,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            taskTag );
      }
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));

      if (taskTag != agNULL)
      {
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            agNULL,
                            tiIntrEventTypeLocalAbort,
                            tiAbortInProgress,
                            taskTag );
      }
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_DELAYED\n"));

      if (taskTag != agNULL)
      {
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            agNULL,
                            tiIntrEventTypeLocalAbort,
                            tiAbortDelayed,
                            taskTag );
      }
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#endif
    else
    {
      TI_DBG1(("ossaSSPAbortCB: other status %d\n", status));

      if (taskTag != agNULL)
      {
        ostiInitiatorEvent( tiRoot,
                            agNULL,
                            agNULL,
                            tiIntrEventTypeLocalAbort,
                            tiAbortFailed,
                            taskTag );
      }
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else
  {
    TI_DBG1(("ossaSSPAbortCB: wrong flag %d\n", flag));
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Ya");
  return;

}
#endif


#ifdef TARGET_DRIVER
osGLOBAL void ossaSSPAbortCB(
               agsaRoot_t       *agRoot,
               agsaIORequest_t  *agIORequest,
               bit32            flag,
               bit32            status)
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t       *tdAbortIORequestBody;
  tdsaDeviceData_t        *oneDeviceData;
  tiDeviceHandle_t        *tiDeviceHandle;

  TI_DBG3(("ossaSSPAbortCB: start\n"));
  tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  if (flag == 2)
  {
    /* abort per port */
    TI_DBG2(("ossaSSPAbortCB: abort per port\n"));
  }
  else if (flag == 1)
  {
    TI_DBG2(("ossaSSPAbortCB: abort all\n"));
    tiDeviceHandle = (tiDeviceHandle_t *)tdAbortIORequestBody->tiDevHandle;
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG2(("ossaSSPAbortCB: OSSA_IO_SUCCESS\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG3(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NOT_VALID\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NO_DEVICE\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_DELAYED\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG2(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#endif
    else
    {
      TI_DBG1(("ossaSSPAbortCB: other status %d\n", status));
      /* clean up TD layer's IORequestBody */
      TI_DBG2(("ossaSSPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG1(("ossaSSPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else if (flag == 0)
  {
    TI_DBG2(("ossaSSPAbortCB: abort one\n"));
    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG2(("ossaSSPAbortCB: OSSA_IO_SUCCESS\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NOT_VALID\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_NO_DEVICE\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      TI_DBG1(("ossaSSPAbortCB: OSSA_IO_ABORT_DELAYED\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#endif
    else
    {
      TI_DBG1(("ossaSSPAbortCB: other status %d\n", status));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else
  {
    TI_DBG1(("ossaSSPAbortCB: wrong flag %d\n", flag));
  }

  return;

}
#endif


/*****************************************************************************/
/*! \brief ossaLocalPhyControlCB
 *
 *
 *  Purpose: This routine is called by lower layer to indicate the status of
 *           phy operations
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   phyId         Phy id
 *  \param   phyOperation  Operation to be done on the phy
 *  \param   status        Phy operation specific completion status
 *  \param   parm          Additional parameter, phy operation and status specific
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaLocalPhyControlCB(
                      agsaRoot_t  *agRoot,
                      agsaContext_t *agContext,
                      bit32       phyId,
                      bit32       phyOperation,
                      bit32       status,
                      void        *parm
                      )
{
#ifdef REMVOED
  agsaPhyErrCounters_t    *agPhyErrCounters;
#endif
#ifdef INITIATOR_DRIVER
  tdsaRootOsData_t         *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                 *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiIORequest_t            *currentTaskTag;
  tdsaDeviceData_t         *TargetDeviceData;
  satDeviceData_t          *pSatDevData;
  agsaDevHandle_t          *agDevHandle = agNULL;
  agsaContext_t            *agContextDevice;
#endif

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yb");
  TI_DBG3(("ossaLocalPhyControlCB: start phyID %d\n", phyId));
  TI_DBG3(("ossaLocalPhyControlCB: phyOperation %d status 0x%x\n", phyOperation, status));
  switch (phyOperation)
  {
  case AGSA_PHY_LINK_RESET: /* fall through */
  case AGSA_PHY_HARD_RESET:
    if (phyOperation == AGSA_PHY_LINK_RESET)
    {
      TI_DBG1(("ossaLocalPhyControlCB: AGSA_PHY_LINK_RESET, status 0x%x\n", status));
    }
    else
    {
      TI_DBG1(("ossaLocalPhyControlCB: AGSA_PHY_HARD_RESET, status 0x%x\n", status));
    }
#ifdef INITIATOR_DRIVER
    if (agContext != agNULL)
    {
      currentTaskTag = (tiIORequest_t *)agContext->osData;
      if (status == OSSA_SUCCESS)
      {
        if (currentTaskTag != agNULL)
        {
          TI_DBG2(("ossaLocalPhyControlCB: callback to OS layer with success\n"));
          TargetDeviceData = (tdsaDeviceData_t *)currentTaskTag->tdData;
          pSatDevData = (satDeviceData_t *)&(TargetDeviceData->satDevData);
          agDevHandle = TargetDeviceData->agDevHandle;
          TI_DBG2(("ossaLocalPhyControlCB: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
          TI_DBG2(("ossaLocalPhyControlCB: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
          pSatDevData->satDriveState = SAT_DEV_STATE_NORMAL;

          if (TargetDeviceData->TRflag == agTRUE)
          {
            saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, TargetDeviceData), agDevHandle, SA_DS_OPERATIONAL);
            TargetDeviceData->TRflag = agFALSE;
            ostiInitiatorEvent(tiRoot,
                               TargetDeviceData->tdPortContext->tiPortalContext,
                               &(TargetDeviceData->tiDeviceHandle),
                               tiIntrEventTypeTransportRecovery,
                               tiRecOK,
                               agNULL
                              );
          }
          else
          {
            agDevHandle = TargetDeviceData->agDevHandle;
            if (agDevHandle == agNULL)
            {
              TI_DBG1(("ossaLocalPhyControlCB: wrong, agDevHandle is NULL\n"));
            }
            /* move this to OSSA_HW_EVENT_PORT_RESET_COMPLETE in ossaHwCB() */
            agContextDevice = &(TargetDeviceData->agDeviceResetContext);
            agContextDevice->osData = currentTaskTag;

#ifdef REMOVED
            ostiInitiatorEvent( tiRoot,
                                NULL,
                                NULL,
                                tiIntrEventTypeTaskManagement,
                                tiTMOK,
                                currentTaskTag );
#endif
          }
        }
      }
      else
      {
        if (currentTaskTag != agNULL)
        {
          TI_DBG1(("ossaLocalPhyControlCB: callback to OS layer with failure\n"));
          TargetDeviceData = (tdsaDeviceData_t *)currentTaskTag->tdData;
          pSatDevData = (satDeviceData_t *)&(TargetDeviceData->satDevData);
          TI_DBG1(("ossaLocalPhyControlCB: satPendingIO %d satNCQMaxIO %d\n", pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
          TI_DBG1(("ossaLocalPhyControlCB: satPendingNCQIO %d satPendingNONNCQIO %d\n", pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
          if (TargetDeviceData->TRflag == agTRUE)
          {
            TargetDeviceData->TRflag = agFALSE;
            ostiInitiatorEvent(tiRoot,
                               TargetDeviceData->tdPortContext->tiPortalContext,
                               &(TargetDeviceData->tiDeviceHandle),
                               tiIntrEventTypeTransportRecovery,
                               tiRecFailed ,
                               agNULL
                              );
          }
          else
          {
            ostiInitiatorEvent( tiRoot,
                                NULL,
                                NULL,
                                tiIntrEventTypeTaskManagement,
                                tiTMFailed,
                                currentTaskTag );
          }
        }
      }
    }
#endif
    break;
#ifdef REMOVED
  case AGSA_PHY_GET_ERROR_COUNTS:

    TI_DBG2(("ossaLocalPhyControlCB: AGSA_PHY_GET_ERROR_COUNTS, status 0x%x\n", status));
    if(parm !=agNULL )
    {
      agPhyErrCounters = (agsaPhyErrCounters_t *)parm;
      TI_DBG2(("ossaLocalPhyControlCB: invalidDword %d\n", agPhyErrCounters->invalidDword));
      TI_DBG2(("ossaLocalPhyControlCB: runningDisparityError %d\n", agPhyErrCounters->runningDisparityError));
      TI_DBG2(("ossaLocalPhyControlCB: lostOfDwordSynch %d\n", agPhyErrCounters->lossOfDwordSynch));
      TI_DBG2(("ossaLocalPhyControlCB: phyResetProblem %d\n", agPhyErrCounters->phyResetProblem));
      TI_DBG2(("ossaLocalPhyControlCB: elasticityBufferOverflow %d\n", agPhyErrCounters->elasticityBufferOverflow));
      TI_DBG2(("ossaLocalPhyControlCB: receivedErrorPrimitive %d\n", agPhyErrCounters->receivedErrorPrimitive));
    }
    break;
  case AGSA_PHY_CLEAR_ERROR_COUNTS:
    TI_DBG2(("ossaLocalPhyControlCB: AGSA_PHY_CLEAR_ERROR_COUNTS, status 0x%x\n", status));
    break;
#endif
  case AGSA_PHY_NOTIFY_ENABLE_SPINUP:
    TI_DBG2(("ossaLocalPhyControlCB: AGSA_PHY_NOTIFY_ENABLE_SPINUP, status 0x%x\n", status));
    break;
  case AGSA_PHY_BROADCAST_ASYNCH_EVENT:
    TI_DBG2(("ossaLocalPhyControlCB: AGSA_PHY_BROADCAST_ASYNCH_EVENT, status 0x%x\n", status));
    if (tIsSPC12SATA(agRoot))
    {
      TI_DBG1(("ossaLocalPhyControlCB: BROADCAST_ASYNCH_EVENT received for SATA Controller\n"));
      break;
    }
    break;
  case AGSA_PHY_COMINIT_OOB :
    TI_DBG2(("ossaLocalPhyControlCB: AGSA_PHY_COMINIT_OOB, status 0x%x\n", status));
    break;
  default:
    TI_DBG1(("ossaLocalPhyControlCB: UNKNOWN default case. phyOperation %d status 0x%x\n", phyOperation, status));
    break;
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yb");
  return;
}

GLOBAL void   ossaGetPhyProfileCB(
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      bit32         status,
                      bit32         ppc,
                      bit32         phyID,
                      void          *parm )
{

  tdsaRootOsData_t  *osData        = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot        = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef CCFLAGS_PHYCONTROL_COUNTS
  agsaPhyAnalogSettingsPage_t *analog;
#endif /* CCFLAGS_PHYCONTROL_COUNTS   */
  tdPhyCount_t      *PhyBlob = agNULL;

  agsaPhyBWCountersPage_t       *agBWCounters;
  agsaPhyErrCountersPage_t      *agPhyErrCounters;
  TI_DBG1(("ossaGetPhyProfileCB: agContext %p parm %p\n", agContext, parm));
/*
  if(  tdsaAllShared->tdFWControlEx.inProgress )
  {
    tdsaAllShared->tdFWControlEx.inProgress = 0;
    PhyBlob = (tdPhyCount_t  *)tdsaAllShared->tdFWControlEx.usrAddr;
  }
*/
  switch(ppc)
  {
    case  AGSA_SAS_PHY_BW_COUNTERS_PAGE:
      TI_DBG1(("ossaGetPhyProfileCB: AGSA_SAS_PHY_BW_COUNTERS_PAGE, status 0x%x phyID %d\n", status, phyID));
      if(parm !=agNULL )
      {
        agBWCounters = (agsaPhyBWCountersPage_t *)parm;
        TI_DBG1(("ossaGetPhyProfileCB: RX %d TX %d\n", agBWCounters->RXBWCounter,agBWCounters->TXBWCounter));
        if(PhyBlob !=agNULL )
        {
          PhyBlob->InvalidDword = 0;
          PhyBlob->runningDisparityError = 0;
          PhyBlob->codeViolation = 0;
          PhyBlob->phyResetProblem = 0;
          PhyBlob->inboundCRCError = 0;
          PhyBlob->BW_rx = agBWCounters->RXBWCounter;
          PhyBlob->BW_tx = agBWCounters->TXBWCounter;
        }

      }
      break;
    case AGSA_SAS_PHY_ERR_COUNTERS_PAGE:
      if(  tdsaAllShared->tdFWControlEx.inProgress )
      {
  	  tdsaAllShared->tdFWControlEx.inProgress = 0;
  	  PhyBlob = (tdPhyCount_t  *)tdsaAllShared->tdFWControlEx.usrAddr;
      }
      TI_DBG1(("ossaGetPhyProfileCB: AGSA_SAS_PHY_ERR_COUNTERS_PAGE, status 0x%x phyID %d\n", status, phyID));
      if(parm !=agNULL )
      {
        agPhyErrCounters = (agsaPhyErrCountersPage_t *)parm;
        if(PhyBlob !=agNULL )
        {

          PhyBlob->InvalidDword          = agPhyErrCounters->invalidDword;
          PhyBlob->runningDisparityError = agPhyErrCounters->runningDisparityError;
          PhyBlob->LossOfSyncDW          = agPhyErrCounters->lossOfDwordSynch;
          PhyBlob->codeViolation         = agPhyErrCounters->codeViolation;
          PhyBlob->phyResetProblem       = agPhyErrCounters->phyResetProblem;
          PhyBlob->inboundCRCError       = agPhyErrCounters->inboundCRCError;
          PhyBlob->BW_rx = 0;
          PhyBlob->BW_tx = 0;

          TI_DBG2(("ossaGetPhyProfileCB: invalidDword          %d\n", agPhyErrCounters->invalidDword));
          TI_DBG2(("ossaGetPhyProfileCB: runningDisparityError %d\n", agPhyErrCounters->runningDisparityError));
          TI_DBG2(("ossaGetPhyProfileCB: lostOfDwordSynch      %d\n", agPhyErrCounters->lossOfDwordSynch));
          TI_DBG2(("ossaGetPhyProfileCB: phyResetProblem       %d\n", agPhyErrCounters->phyResetProblem));
          TI_DBG2(("ossaGetPhyProfileCB: inboundCRCError       %d\n", agPhyErrCounters->inboundCRCError));
        }
      }
      break;
    case AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE:
      TI_DBG1(("ossaGetPhyProfileCB: AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE status 0x%x phyID %d\n", status, phyID));
      break;
    case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
      TI_DBG1(("ossaGetPhyProfileCB:AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE status 0x%x phyID %d\n", status, phyID));
#ifdef CCFLAGS_PHYCONTROL_COUNTS
      if(parm !=agNULL )
      {
        analog = (agsaPhyAnalogSettingsPage_t *)parm;
        TI_DBG1(("ossaGetPhyProfileCB: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
          analog->Dword0, analog->Dword1, analog->Dword2, analog->Dword3, analog->Dword4,
          analog->Dword5, analog->Dword6, analog->Dword7, analog->Dword8, analog->Dword9));
        tdsaAllShared->analog[phyID].spaRegister0 = analog->Dword0;
        tdsaAllShared->analog[phyID].spaRegister1 = analog->Dword1;
        tdsaAllShared->analog[phyID].spaRegister2 = analog->Dword2;
        tdsaAllShared->analog[phyID].spaRegister3 = analog->Dword3;
        tdsaAllShared->analog[phyID].spaRegister4 = analog->Dword4;
        saSetPhyProfile( agRoot,agContext,tdsaRotateQnumber(tiRoot, agNULL), AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE,sizeof(agsaPhyAnalogSetupRegisters_t),&tdsaAllShared->analog[phyID],phyID);
      }
#endif /* CCFLAGS_PHYCONTROL_COUNTS   */
     break;
    case AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE:
    {
      TI_DBG1(("ossaGetPhyProfileCB:AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE status 0x%x phyID %d\n", status, phyID));
      if( parm !=agNULL )
      {
#ifdef TD_DEBUG_ENABLE
        agsaSASPhyOpenRejectRetryBackOffThresholdPage_t *Backoff =
          (agsaSASPhyOpenRejectRetryBackOffThresholdPage_t *)parm;
#endif
        TI_DBG2(("ossaGetPhyProfileCB: DW0 0x%X DW1 0x%X DW2 0x%X DW3 0x%X\n",
                 Backoff->Dword0,Backoff->Dword1,
                 Backoff->Dword2,Backoff->Dword3));
       }
      break;
    }

    case AGSA_SAS_PHY_GENERAL_STATUS_PAGE:
    {
      agsaSASPhyGeneralStatusPage_t * GenStatus = NULL;

      TI_DBG1(("ossaGetPhyProfileCB: AGSA_SAS_PHY_GENERAL_STATUS_PAGE status 0x%x phyID %d\n",
               status, phyID));
      if( parm !=agNULL )
      {
          GenStatus=
          (agsaSASPhyGeneralStatusPage_t *)parm;
        TI_DBG2(("ossaGetPhyProfileCB: "
                 "AGSA_SAS_PHY_GENERAL_STATUS_PAGE status %d DW0 0x%x DW1 0x%x\n",
                 status, GenStatus->Dword0, GenStatus->Dword1));
      }
      ostiGetPhyGeneralStatusRsp(tiRoot, GenStatus, phyID);
//      break;
      return ;
    }

    default:
      TI_DBG1(("ossaGetPhyProfileCB: UNKNOWN default case. phyOperation %d status 0x%x\n", ppc, status));
      break;

  }

  ostiGetPhyProfileIOCTLRsp(tiRoot, status);

}


GLOBAL void ossaSetPhyProfileCB(
                     agsaRoot_t    *agRoot,
                     agsaContext_t *agContext,
                     bit32         status,
                     bit32         ppc,
                     bit32         phyID,
                     void          *parm )
{
  TI_DBG1(("ossaSetPhyProfileCB:agContext %p status 0x%x ppc %d phyID %d parm %p\n",agContext, status, ppc, phyID,parm));
}


/*****************************************************************************/
/*! \brief ossaGetDeviceHandlesCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGetDeviceHandles()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the get device handle request originally passed into
 *                         saGetDeviceHandles().
 *  \param   agPortContext:Pointer to this instance of a port context
 *  \param   agDev:        Array containing pointers to the device handles

 *  \param   validDevs     Number of valid device handles
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
osGLOBAL void ossaGetDeviceHandlesCB(
                       agsaRoot_t           *agRoot,
                       agsaContext_t        *agContext,
                       agsaPortContext_t    *agPortContext,
                       agsaDevHandle_t      *agDev[],
                       bit32                validDevs
                       )
{
  TI_DBG2(("ossaGetDeviceHandlesCB: start\n"));
  TI_DBG2(("ossaGetDeviceHandlesCB: validDevs %d\n", validDevs));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yc");
#ifdef TO_DO
  for (i = 0 ; i < validDevs ; i++)
  {
    agDev[i];
  }
#endif
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yc");
  return;
}

/*****************************************************************************/
/*! \brief ossaGetDeviceInfoCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGetDeviceInfo()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agDevHandle:  Handle of the device
 *  \param   status:       status
 *  \param   agInfo:       Pointer to the structure that describes device information
 *
 *
 *  \return None.
 *
 *  \note - The scope is shared target and initiator.
 *          For details, refer to SAS/SATA Low-Level API Specification
 */
/*****************************************************************************/
osGLOBAL void ossaGetDeviceInfoCB(
                    agsaRoot_t        *agRoot,
                    agsaContext_t     *agContext,
                    agsaDevHandle_t   *agDevHandle,
                    bit32             status,
                    void              *agInfo
                    )
{
  
#ifdef TD_DEBUG_ENABLE
  agsaDeviceInfo_t       *agDeviceInfo;
  agsaSASDeviceInfo_t    *agSASDeviceInfo;
  agsaSATADeviceInfo_t   *agSATADeviceInfo;
#endif
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yd");

  TI_DBG1(("ossaGetDeviceInfoCB: start agContext %p\n",agContext));
  switch (status)
  {
  case OSSA_DEV_INFO_INVALID_HANDLE:
    TI_DBG1(("ossaGetDeviceInfoCB: OSSA_DEV_INFO_INVALID_HANDLE\n"));
    /*ostiGetDeviceInfoIOCTLRsp(tiRoot, status, agNULL);*/
    break;
  case OSSA_DEV_INFO_NO_EXTENDED_INFO:
#ifdef TD_DEBUG_ENABLE
    agDeviceInfo = (agsaDeviceInfo_t *)agInfo;
#endif
    TI_DBG1(("ossaGetDeviceInfoCB: OSSA_DEV_INFO_NO_EXTENDED_INFO\n"));
    TI_DBG1(("ossaGetDeviceInfoCB: sasAddressHi 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSHI(agDeviceInfo)));
    TI_DBG1(("ossaGetDeviceInfoCB: sasAddressLo 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSLO(agDeviceInfo)));
    TI_DBG1(("ossaGetDeviceInfoCB: devType_S_Rate 0x%08x\n", agDeviceInfo->devType_S_Rate));
    TI_DBG1(("ossaGetDeviceInfoCB: firstBurstSize 0x%08x\n", agDeviceInfo->firstBurstSize));

    /*ostiPortEvent (tiRoot, tiGetDevInfo, tiSuccess,(void *)agContext );*/
    /*ostiGetDeviceInfoIOCTLRsp(tiRoot, status, agDeviceInfo);*/
    break;
  case OSSA_DEV_INFO_SAS_EXTENDED_INFO:
#ifdef TD_DEBUG_ENABLE
    agSASDeviceInfo = (agsaSASDeviceInfo_t *)agInfo;
#endif
    TI_DBG2(("ossaGetDeviceInfoCB: OSSA_DEV_INFO_SAS_EXTENDED_INFO\n"));
    TI_DBG2(("ossaGetDeviceInfoCB: sasAddressHi 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSHI(&agSASDeviceInfo->commonDevInfo)));
    TI_DBG2(("ossaGetDeviceInfoCB: sasAddressLo 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSLO(&agSASDeviceInfo->commonDevInfo)));
    TI_DBG2(("ossaGetDeviceInfoCB: initiator_ssp_stp_smp %d\n", agSASDeviceInfo->initiator_ssp_stp_smp));
    TI_DBG2(("ossaGetDeviceInfoCB: target_ssp_stp_smp %d\n", agSASDeviceInfo->target_ssp_stp_smp));
    TI_DBG2(("ossaGetDeviceInfoCB: numOfPhys %d\n", agSASDeviceInfo->numOfPhys));
    TI_DBG2(("ossaGetDeviceInfoCB: phyIdentifier %d\n", agSASDeviceInfo->phyIdentifier));

    break;
  case OSSA_DEV_INFO_SATA_EXTENDED_INFO:
#ifdef TD_DEBUG_ENABLE
    agSATADeviceInfo = (agsaSATADeviceInfo_t *)agInfo;
#endif
    TI_DBG2(("ossaGetDeviceInfoCB: OSSA_DEV_INFO_SATA_EXTENDED_INFO\n"));
    TI_DBG2(("ossaGetDeviceInfoCB: sasAddressHi 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSHI(&agSATADeviceInfo->commonDevInfo)));
    TI_DBG2(("ossaGetDeviceInfoCB: sasAddressLo 0x%08x\n", SA_DEVINFO_GET_SAS_ADDRESSLO(&agSATADeviceInfo->commonDevInfo)));
    TI_DBG2(("ossaGetDeviceInfoCB: connection %d\n", agSATADeviceInfo->connection));
    TI_DBG2(("ossaGetDeviceInfoCB: portMultiplierField %d\n", agSATADeviceInfo->portMultiplierField));
    TI_DBG2(("ossaGetDeviceInfoCB: stpPhyIdentifier %d\n", agSATADeviceInfo->stpPhyIdentifier));
#ifdef TD_DEBUG_ENABLE
    tdhexdump("ossaGetDeviceInfoCB: signature", (bit8 *)agSATADeviceInfo->signature, 8);
#endif
     break;
  default:
    TI_DBG2(("ossaGetDeviceInfoCB: error default case, status is %d\n", status));
    break;
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yd");
  return;
}

/*****************************************************************************/
/*! \brief ossaDeviceRegistrationCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saRegisterNewDevice()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the get device handle request originally
 *                         passed into saRegisterNewDevice().
 *  \param   status:       status
 *  \param   agDevHandle:  Pointer to the assigned device handle for the
 *                         registered device.
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaDeviceRegistrationCB(
                         agsaRoot_t        *agRoot,
                         agsaContext_t     *agContext,
                         bit32             status,
                         agsaDevHandle_t   *agDevHandle,
                         bit32             deviceID
                         )
{
#ifdef INITIATOR_DRIVER
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t           *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32                Indenom = tdsaAllShared->QueueConfig.numInboundQueues;
  bit32                Outdenom = tdsaAllShared->QueueConfig.numOutboundQueues;
  tdsaDeviceData_t     *oneDeviceData = (tdsaDeviceData_t *)agContext->osData;
  tdsaPortContext_t    *onePortContext = oneDeviceData->tdPortContext;
  tiPortalContext_t    *tiPortalContext = onePortContext->tiPortalContext;
#ifdef FDS_DM
  dmRoot_t             *dmRoot = &(tdsaAllShared->dmRoot);
  dmPortContext_t      *dmPortContext = &(onePortContext->dmPortContext);
  dmDeviceInfo_t       dmDeviceInfo;
  bit32                DMstatus = DM_RC_FAILURE;
  bit16                ext = 0;
  bit32                expanderType = 1;
#endif

#if defined(FDS_DM) && !defined(FDS_SM)
  bit32                IDstatus;
#endif

#ifdef FDS_SM
  smRoot_t             *smRoot = &(tdsaAllShared->smRoot);
  bit32                SMstatus = SM_RC_FAILURE;
#endif
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Ye");
  TI_DBG3(("ossaDeviceRegistrationCB: start status 0x%x\n",status));
  TI_DBG3(("ossaDeviceRegistrationCB: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("ossaDeviceRegistrationCB: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("ossaDeviceRegistrationCB: did 0x%x\n", oneDeviceData->id));
  TI_DBG3(("ossaDeviceRegistrationCB: deviceID 0x%x\n", deviceID));
  TI_DBG3(("ossaDeviceRegistrationCB: agDevHandle %p %p %p\n",agDevHandle,agDevHandle->osData,agDevHandle->sdkData ));

  /* transient period caused by tdssReportRemovals(), device was in the middle
    of registration but port is invalidated
  */
  if (oneDeviceData->valid == agFALSE && oneDeviceData->valid2 == agFALSE
      && oneDeviceData->DeviceType == TD_DEFAULT_DEVICE)
  {
    if (status == OSSA_SUCCESS)
    {
      TI_DBG2(("ossaDeviceRegistrationCB: transient, calling saDeregisterDeviceHandle, did %d\n", oneDeviceData->id));
      oneDeviceData->agDevHandle = agDevHandle;
      agDevHandle->osData = oneDeviceData;
      if (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
      {
        if (oneDeviceData->satDevData.IDDeviceValid == agFALSE)
        {
          saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, 0);
        }
        else
        {
          saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
        }
      }
      else
      {
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      }
    }
    else if (status == OSSA_FAILURE_PORT_NOT_VALID_STATE || status == OSSA_ERR_PORT_STATE_NOT_VALID)
    {
      /* do nothing */
      TI_DBG2(("ossaDeviceRegistrationCB: transient, do nothing did %d\n", oneDeviceData->id));
    }
    return;
  }

  if (agDevHandle == agNULL)
  {
    TI_DBG3(("ossaDeviceRegistrationCB: agDevHandle is NULL\n"));
  }
  else
  {
    TI_DBG3(("ossaDeviceRegistrationCB: agDevHandle is NOT NULL\n"));
  }

  switch (status)
  {
  case OSSA_SUCCESS:
    TI_DBG3(("ossaDeviceRegistrationCB: success\n"));
    TI_DBG2(("ossaDeviceRegistrationCB: Success did %d FW did 0x%x\n", oneDeviceData->id, deviceID));
    TI_DBG2(("ossaDeviceRegistrationCB: Success pid %d\n", onePortContext->id));
    if (agDevHandle == agNULL)
    {
      TI_DBG1(("ossaDeviceRegistrationCB: agDevHandle is NULL, wrong!\n"));
      return;
    }
    oneDeviceData->agDevHandle = agDevHandle;
    agDevHandle->osData = oneDeviceData;
    oneDeviceData->registered = agTRUE;
    oneDeviceData->InQID = oneDeviceData->id % Indenom;
    oneDeviceData->OutQID = oneDeviceData->id % Outdenom;
    onePortContext->RegisteredDevNums++;

    TI_DBG3(("ossaDeviceRegistrationCB: direct %d STP target %d target_ssp_stp_smp %d\n", oneDeviceData->directlyAttached, DEVICE_IS_STP_TARGET(oneDeviceData), oneDeviceData->target_ssp_stp_smp));
    TI_DBG3(("ossaDeviceRegistrationCB: pid %d registeredNumDevice %d\n", onePortContext->id, onePortContext->RegisteredDevNums));
    TI_DBG3(("ossaDeviceRegistrationCB: pid %d Count %d\n", onePortContext->id, onePortContext->Count));

#ifdef FDS_DM
    /* if device is an expander, register it to DM */
    if (onePortContext->valid == agTRUE)
    {
      if (DEVICE_IS_SMP_TARGET(oneDeviceData))
      {
        TI_DBG1(("ossaDeviceRegistrationCB: calling dmRegisterDevice\n"));
        TI_DBG1(("ossaDeviceRegistrationCB: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG1(("ossaDeviceRegistrationCB: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        /* set up dmDeviceInfo */
        osti_memset(&dmDeviceInfo, 0, sizeof(dmDeviceInfo_t));
        DEVINFO_PUT_SAS_ADDRESSLO(&dmDeviceInfo, oneDeviceData->SASAddressID.sasAddressLo);
        DEVINFO_PUT_SAS_ADDRESSHI(&dmDeviceInfo, oneDeviceData->SASAddressID.sasAddressHi);
        dmDeviceInfo.initiator_ssp_stp_smp = oneDeviceData->initiator_ssp_stp_smp;
        dmDeviceInfo.target_ssp_stp_smp = oneDeviceData->target_ssp_stp_smp;
        dmDeviceInfo.devType_S_Rate = oneDeviceData->agDeviceInfo.devType_S_Rate;
        if (oneDeviceData->directlyAttached == agTRUE)
        {
          /* setting SMP bit */
          ext = (bit16)(ext | 0x100);
          expanderType = SA_IDFRM_GET_DEVICETTYPE(&onePortContext->sasIDframe);
          ext = (bit16)( ext | (expanderType << 9));
          /* setting MCN field to 0xF */
          ext = (bit16)(ext | (bit16)(0xF << 11));
          TI_DBG1(("ossaDeviceRegistrationCB: directlyAttached ext 0x%x\n", ext));
          dmDeviceInfo.ext = ext;
        }
        DMstatus = dmRegisterDevice(dmRoot, dmPortContext, &dmDeviceInfo, oneDeviceData->agDevHandle);
        if (DMstatus != DM_RC_SUCCESS)
        {
          TI_DBG1(("ossaDeviceRegistrationCB: dmRegisterDevice failed!!! 0x%x\n", DMstatus));
        }
      }
    }
#endif /* FDS_DM */
#ifdef FDS_SM
    /* if device is SATA, register it to SM */
    if (onePortContext->valid == agTRUE)
    {
      if (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
      {
        TI_DBG1(("ossaDeviceRegistrationCB: calling smRegisterDevice\n"));
        if (oneDeviceData->directlyAttached == agTRUE)
        {
          SMstatus = smRegisterDevice(smRoot,
                                      agDevHandle,
                                      &(oneDeviceData->smDeviceHandle),
                                      agNULL,
                                      (bit32)oneDeviceData->phyID,
                                      oneDeviceData->satDevData.satDeviceType);
        }
        else
        {
          if (oneDeviceData->ExpDevice == agNULL)
          {
            TI_DBG1(("ossaDeviceRegistrationCB: oneDeviceData->ExpDevice NULL!!!\n"));
            return;
          }
          if (oneDeviceData->ExpDevice->agDevHandle == agNULL)
          {
            TI_DBG1(("ossaDeviceRegistrationCB: oneDeviceData->ExpDevice->agDevHandle NULL!!!\n"));
          }
          SMstatus = smRegisterDevice(smRoot,
                                      agDevHandle,
                                      &(oneDeviceData->smDeviceHandle),
                                      oneDeviceData->ExpDevice->agDevHandle,
                                      (bit32)oneDeviceData->phyID,
                                      oneDeviceData->satDevData.satDeviceType);
        }
        if (SMstatus != SM_RC_SUCCESS)
        {
          TI_DBG1(("ossaDeviceRegistrationCB: smRegisterDevice failed!!! 0x%x\n", DMstatus));
        }
      }
    }
#endif /* FDS_SM */
    /* special case for directly attached targets */
    if (oneDeviceData->directlyAttached == agTRUE)
    {
      TI_DBG3(("ossaDeviceRegistrationCB: directly attached did %d\n", oneDeviceData->id));
      if (oneDeviceData->DeviceType == TD_SAS_DEVICE)
      {
        TI_DBG3(("ossaDeviceRegistrationCB: SAS target\n"));
        if (onePortContext->valid == agTRUE)
        {
          if (onePortContext->PortRecoverPhyID != 0xFF)
          {
            oneDeviceData->phyID = (bit8)onePortContext->PortRecoverPhyID;
            onePortContext->PortRecoverPhyID = 0xFF;
            TI_DBG3(("ossaDeviceRegistrationCB: PortRecoverPhyID %d\n", oneDeviceData->phyID));
          }
          /* link up and discovery ready event */
          if (onePortContext->DiscoveryRdyGiven == agFALSE)
          {
            TI_DBG2(("ossaDeviceRegistrationCB: link up and discovery ready\n"));
            TI_DBG3(("ossaDeviceRegistrationCB: phyID %d pid %d\n", oneDeviceData->phyID, onePortContext->id));
            TI_DBG3(("ossaDeviceRegistrationCB: tiPortalContext %p\n", tdsaAllShared->Ports[oneDeviceData->phyID].tiPortalContext));
            TI_DBG3(("ossaDeviceRegistrationCB: onePortContext->tiPortalContext %p\n", onePortContext->tiPortalContext));
            onePortContext->DiscoveryRdyGiven = agTRUE;
            if (onePortContext->DiscoveryState != ITD_DSTATE_NOT_STARTED)
            {
              TI_DBG1(("ossaDeviceRegistrationCB: wrong discovery state 0x%x\n", onePortContext->DiscoveryState));
            }
            /* notifying link up */
            ostiPortEvent (
                           tiRoot,
                           tiPortLinkUp,
                           tiSuccess,
                           (void *)onePortContext->tiPortalContext
                           );
#ifdef INITIATOR_DRIVER
            /* triggers discovery */
            ostiPortEvent(
                          tiRoot,
                          tiPortDiscoveryReady,
                          tiSuccess,
                          (void *)onePortContext->tiPortalContext
                          );
#endif
          }
        }
        else
        {
          TI_DBG2(("ossaDeviceRegistrationCB: abort call\n"));
          /* abort all followed by deregistration of sas target */
          tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
        }
      }
      else
      {
        TI_DBG2(("ossaDeviceRegistrationCB: SATA target\n"));
        if (onePortContext->valid == agTRUE)
        {
          if (oneDeviceData->satDevData.IDDeviceValid == agFALSE)
          {
#ifdef FDS_SM
            /* send identify device data */
            tdIDStart(tiRoot, agRoot, smRoot, oneDeviceData, onePortContext);

#else
            /* send identify device data */
            tdssSubAddSATAToSharedcontext(tiRoot, oneDeviceData);
#endif
          }
        }
        else
        {
          TI_DBG2(("ossaDeviceRegistrationCB: abort call\n"));
          /* abort all followed by deregistration of sas target */
          tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
        }
      }
    }
    else /* behind the expander */
    {
#if defined(FDS_DM) && defined(FDS_SM)
      /* send ID to SATA targets
         needs go allocate tdIORequestBody_t for smIORequest
      */

      if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
          &&
          oneDeviceData->satDevData.IDDeviceValid == agFALSE)
      {
        tdIDStart(tiRoot, agRoot, smRoot, oneDeviceData, onePortContext);
      }

#elif defined(FDS_DM) /* worked with DM */
      if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
          &&
          oneDeviceData->satDevData.IDDeviceValid == agFALSE)
      {
         IDstatus = tdsaDiscoveryStartIDDev(tiRoot,
                                         agNULL,
                                         &(oneDeviceData->tiDeviceHandle),
                                         agNULL,
                                         oneDeviceData);

        if (IDstatus != tiSuccess)
        {
          /* identify device data is not valid */
          TI_DBG1(("ossaDeviceRegistrationCB: fail or busy %d\n", IDstatus));
          oneDeviceData->satDevData.IDDeviceValid = agFALSE;
        }
      }
#endif


   }
    /* after discovery is finished */
    if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED)
    {
      TI_DBG2(("ossaDeviceRegistrationCB: calling new device arrival\n"));
      if (DEVICE_IS_SSP_TARGET(oneDeviceData))
      {
        /* in case registration is finished after discovery is finished */
#ifdef AGTIAPI_CTL
        if (tdsaAllShared->SASConnectTimeLimit)
          tdsaCTLSet(tiRoot, onePortContext, tiIntrEventTypeDeviceChange,
                     tiDeviceArrival);
        else
#endif
          ostiInitiatorEvent(
                             tiRoot,
                             tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDeviceChange,
                             tiDeviceArrival,
                             agNULL
                             );
      }
      else if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
                &&
                oneDeviceData->satDevData.IDDeviceValid == agTRUE )
      {
        /* in case registration is finished after discovery is finished */
        ostiInitiatorEvent(
                           tiRoot,
                           tiPortalContext,
                           agNULL,
                           tiIntrEventTypeDeviceChange,
                           tiDeviceArrival,
                           agNULL
                           );
      }
    }
    break;
  case OSSA_FAILURE_OUT_OF_RESOURCE: /* fall through */
  case OSSA_ERR_DEVICE_HANDLE_UNAVAILABLE:
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_OUT_OF_RESOURCE or OSSA_ERR_DEVICE_HANDLE_UNAVAILABLE\n"));
    oneDeviceData->registered = agFALSE;
    break;
  case OSSA_FAILURE_DEVICE_ALREADY_REGISTERED: /* fall through */
  case OSSA_ERR_DEVICE_ALREADY_REGISTERED:
    /* do nothing */
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_DEVICE_ALREADY_REGISTERED or OSSA_ERR_DEVICE_ALREADY_REGISTERED\n"));
    break;
  case OSSA_FAILURE_INVALID_PHY_ID: /* fall through */
  case OSSA_ERR_PHY_ID_INVALID:
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_INVALID_PHY_ID or OSSA_ERR_PHY_ID_INVALID\n"));
    oneDeviceData->registered = agFALSE;
    break;
  case OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED: /* fall through */
  case OSSA_ERR_PHY_ID_ALREADY_REGISTERED:
    /* do nothing */
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED or OSSA_ERR_PHY_ID_ALREADY_REGISTERED\n"));
    break;
  case OSSA_FAILURE_PORT_ID_OUT_OF_RANGE: /* fall through */
  case OSSA_ERR_PORT_INVALID:
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_PORT_ID_OUT_OF_RANGE or OSSA_ERR_PORT_INVALID\n"));
    oneDeviceData->registered = agFALSE;
    break;
  case OSSA_FAILURE_PORT_NOT_VALID_STATE: /* fall through */
  case OSSA_ERR_PORT_STATE_NOT_VALID:
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_PORT_NOT_VALID_STATE or OSSA_ERR_PORT_STATE_NOT_VALID\n"));
    TI_DBG2(("ossaDeviceRegistrationCB: did %d pid %d\n", oneDeviceData->id, onePortContext->id));
    oneDeviceData->registered = agFALSE;
    /* transient period between link up and link down/port recovery */
    onePortContext->Transient = agTRUE;
    if (onePortContext->valid == agTRUE && (oneDeviceData->valid == agTRUE || oneDeviceData->valid2 == agTRUE))
    {
      TI_DBG1(("ossaDeviceRegistrationCB: retries regisration\n"));
#ifdef REMOVED
      //temp; setting MCN to tdsaAllShared->MCN
      oneDeviceData->agDeviceInfo.flag = oneDeviceData->agDeviceInfo.flag | (tdsaAllShared->MCN << 16);
      //end temp
#endif
      saRegisterNewDevice( /* ossaDeviceRegistrationCB */
                          agRoot,
                          &oneDeviceData->agContext,
                          0,
                          &oneDeviceData->agDeviceInfo,
                          onePortContext->agPortContext,
                          0
                         );
    }
    else if (oneDeviceData->directlyAttached == agTRUE && DEVICE_IS_SATA_DEVICE(oneDeviceData))
    {
      TI_DBG1(("ossaDeviceRegistrationCB: directly attached SATA, put back into free list\n"));
      tdsaDeviceDataReInit(tiRoot, oneDeviceData);
      tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
      TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    }
    break;
  case OSSA_FAILURE_DEVICE_TYPE_NOT_VALID: /* fall through */
  case OSSA_ERR_DEVICE_TYPE_NOT_VALID:
    TI_DBG1(("ossaDeviceRegistrationCB: OSSA_FAILURE_DEVICE_TYPE_NOT_VALID or OSSA_ERR_DEVICE_TYPE_NOT_VALID\n"));
    oneDeviceData->registered = agFALSE;
    break;
  default:
    TI_DBG1(("ossaDeviceRegistrationCB: wrong. default status is %d\n", status));
    break;


    }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Ye");
  return;
#endif
}

/*****************************************************************************/
/*! \brief ossaDeregisterDeviceHandleCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saDeregisterDeviceHandle()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agDevHandle:  Pointer to the assigned device handle for the
 *                         registered device.
 *  \param   status:       status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaDeregisterDeviceHandleCB(
                             agsaRoot_t          *agRoot,
                             agsaContext_t       *agContext,
                             agsaDevHandle_t     *agDevHandle,
                             bit32               status
                             )
{
  tdsaRootOsData_t     *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t             *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t           *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t     *oneDeviceData = agNULL;
  tdsaPortContext_t    *onePortContext = agNULL;
  agsaEventSource_t    *eventSource;
  bit32                HwAckSatus;
  bit32                PhyID;
#ifdef FDS_DM
  dmRoot_t             *dmRoot = &(tdsaAllShared->dmRoot);
  dmPortContext_t      *dmPortContext = agNULL;
  dmPortInfo_t         dmPortInfo;
  bit32                DMstatus = DM_RC_FAILURE;
#endif
#ifdef FDS_SM
  smRoot_t             *smRoot = &(tdsaAllShared->smRoot);
#endif

  TI_DBG3(("ossaDeregisterDeviceHandleCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yf");

  if (status == OSSA_ERR_DEVICE_HANDLE_INVALID)
  {
    /* there is no device handle to process */
    TI_DBG2(("ossaDeregisterDeviceHandleCB: OSSA_ERR_DEVICE_HANDLE_INVALID\n"));
    return;
  }

  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;
  onePortContext = oneDeviceData->tdPortContext;
#ifdef FDS_DM
  dmPortContext = &(onePortContext->dmPortContext);
#endif

  if (oneDeviceData->valid == agFALSE && oneDeviceData->valid2 == agFALSE &&
      oneDeviceData->DeviceType == TD_DEFAULT_DEVICE && onePortContext->valid == agTRUE)
  {
    TI_DBG2(("ossaDeregisterDeviceHandleCB: transient did %d\n", oneDeviceData->id));
    return;
  }

  if (onePortContext != agNULL)
  {
    TI_DBG2(("ossaDeregisterDeviceHandleCB: pid %d registeredNumDevice %d\n", onePortContext->id, onePortContext->RegisteredDevNums));
  }

  switch (status)
  {
  case OSSA_SUCCESS:
    TI_DBG3(("ossaDeregisterDeviceHandleCB: Success\n"));
    if (onePortContext == agNULL)
    {
      TI_DBG1(("ossaDeregisterDeviceHandleCB: onePortContext is NULL, wrong!\n"));
      return;
    }
    /* port is going down */
    if (onePortContext->valid == agFALSE)
    {
      if (!(oneDeviceData->valid == agFALSE && oneDeviceData->valid2 == agFALSE && oneDeviceData->DeviceType == TD_DEFAULT_DEVICE))
      {
        /* remove oneDevice from MainLink */
        TI_DBG2(("ossaDeregisterDeviceHandleCB: delete from MainLink\n"));
#ifdef FDS_SM
        if (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: did %d calling smDeregisterDevice\n", oneDeviceData->id));
          smDeregisterDevice(smRoot, oneDeviceData->agDevHandle, &(oneDeviceData->smDeviceHandle));
        }
#endif
        tdsaDeviceDataReInit(tiRoot, oneDeviceData);
        osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));

        tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
        TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
        TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
        tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      }
      /* for portcontext */
      PhyID = onePortContext->eventPhyID;
      TI_DBG3(("ossaDeregisterDeviceHandleCB: PhyID %d\n", PhyID));
      onePortContext->RegisteredDevNums--;
      /*
        check if valid in tdsaAllShared and the last registered device in a portcontext;
        if so, call saHwEventAck()
       */
      if (tdsaAllShared->eventSource[PhyID].EventValid == agTRUE &&
          onePortContext->RegisteredDevNums == 0 &&
          PhyID != 0xFF
          )
      {
        TI_DBG2(("ossaDeregisterDeviceHandleCB: calling saHwEventAck\n"));
        eventSource = &(tdsaAllShared->eventSource[PhyID].Source);
        HwAckSatus = saHwEventAck(
                                  agRoot,
                                  agNULL, /* agContext */
                                  0,
                                  eventSource, /* agsaEventSource_t */
                                  0,
                                  0
                                  );
        if ( HwAckSatus != AGSA_RC_SUCCESS)
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: failing in saHwEventAck; status %d\n", HwAckSatus));
        }

        /* toggle */
        tdsaAllShared->eventSource[PhyID].EventValid = agFALSE;

#ifdef FDS_DM
        if (onePortContext->UseDM == agTRUE)
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: calling dmDestroyPort\n"));
          /* setup dmPortInfo */
          PORTINFO_PUT_SAS_REMOTE_ADDRESSLO(&dmPortInfo, onePortContext->sasRemoteAddressLo);
          PORTINFO_PUT_SAS_REMOTE_ADDRESSHI(&dmPortInfo, onePortContext->sasRemoteAddressHi);
          PORTINFO_PUT_SAS_LOCAL_ADDRESSLO(&dmPortInfo, onePortContext->sasLocalAddressLo);
          PORTINFO_PUT_SAS_LOCAL_ADDRESSHI(&dmPortInfo, onePortContext->sasLocalAddressHi);
          DMstatus = dmDestroyPort(dmRoot, dmPortContext, &dmPortInfo);
          if (DMstatus != DM_RC_SUCCESS)
          {
             TI_DBG1(("ossaDeregisterDeviceHandleCB: dmDestroyPort failed!!! 0x%x\n", DMstatus));
          }
        }
#endif
        tdsaPortContextReInit(tiRoot, onePortContext);
        /*
          put all devices belonging to the onePortContext
          back to the free link
        */

        tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
        TDLIST_DEQUEUE_THIS(&(onePortContext->MainLink));
        TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->FreeLink), &(tdsaAllShared->FreePortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
      }
      else if (tdsaAllShared->eventSource[PhyID].EventValid == NO_ACK &&
               onePortContext->RegisteredDevNums == 0
              )
      {
        TI_DBG2(("ossaDeregisterDeviceHandleCB: NO ACK case\n"));
#ifdef FDS_DM
        if (onePortContext->UseDM == agTRUE)
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: calling dmDestroyPort\n"));
          /* setup dmPortInfo */
          PORTINFO_PUT_SAS_REMOTE_ADDRESSLO(&dmPortInfo, onePortContext->sasRemoteAddressLo);
          PORTINFO_PUT_SAS_REMOTE_ADDRESSHI(&dmPortInfo, onePortContext->sasRemoteAddressHi);
          PORTINFO_PUT_SAS_LOCAL_ADDRESSLO(&dmPortInfo, onePortContext->sasLocalAddressLo);
          PORTINFO_PUT_SAS_LOCAL_ADDRESSHI(&dmPortInfo, onePortContext->sasLocalAddressHi);
          DMstatus = dmDestroyPort(dmRoot, dmPortContext, &dmPortInfo);
          if (DMstatus != DM_RC_SUCCESS)
          {
            TI_DBG1(("ossaDeregisterDeviceHandleCB: dmDestroyPort failed!!! 0x%x\n", DMstatus));
          }
        }
#endif
        tdsaPortContextReInit(tiRoot, onePortContext);
        /*
          put all devices belonging to the onePortContext
          back to the free link
        */

        tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
        TDLIST_DEQUEUE_THIS(&(onePortContext->MainLink));
        TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->FreeLink), &(tdsaAllShared->FreePortContextList));
        tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
      }
      else
      {
        if (PhyID < TD_MAX_NUM_PHYS)
        {
          TI_DBG3(("ossaDeregisterDeviceHandleCB: pid %d eventvalid %d registeredNumDevice %d\n", onePortContext->id, tdsaAllShared->eventSource[PhyID].EventValid , onePortContext->RegisteredDevNums));
        }
        else
        {
          TI_DBG3(("ossaDeregisterDeviceHandleCB: pid %d registeredNumDevice %d wrong phyid %d\n", onePortContext->id, onePortContext->RegisteredDevNums, PhyID));
        }
      }
    }
    else
    {
      PhyID = onePortContext->eventPhyID;
      TI_DBG3(("ossaDeregisterDeviceHandleCB: PhyID %d\n", PhyID));
      onePortContext->RegisteredDevNums--;
#ifdef FDS_SM
      oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      if (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
      {
        smDeregisterDevice(smRoot, oneDeviceData->agDevHandle, &(oneDeviceData->smDeviceHandle));
      }
#endif
      /*
        check if valid in tdsaAllShared and the last registered device in a portcontext;
        if so, call saHwEventAck()
      */
      if (tdsaAllShared->eventSource[PhyID].EventValid == agTRUE &&
          onePortContext->RegisteredDevNums == 0 &&
          PhyID != 0xFF
          )
      {
        TI_DBG2(("ossaDeregisterDeviceHandleCB: calling saHwEventAck\n"));
        eventSource = &(tdsaAllShared->eventSource[PhyID].Source);
        HwAckSatus = saHwEventAck(
                                  agRoot,
                                  agNULL, /* agContext */
                                  0,
                                  eventSource, /* agsaEventSource_t */
                                  0,
                                  0
                                  );
        if ( HwAckSatus != AGSA_RC_SUCCESS)
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: failing in saHwEventAck; status %d\n", HwAckSatus));
        }

        /* toggle */
        tdsaAllShared->eventSource[PhyID].EventValid = agFALSE;
      }
#ifdef INITIATOR_DRIVER
      else if (onePortContext->RegisteredDevNums == 1)
      {
        TI_DBG1(("ossaDeregisterDeviceHandleCB: all devices have been deregistered except directly attached EXP\n"));
        /* qqqqq If broadcast has been seen, call incremental discovery*/
        if (onePortContext->DiscFailNSeenBC == agTRUE)
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: calling dmDiscover, incremental, pid %d\n", onePortContext->id));
          dmDiscover(dmRoot, dmPortContext, DM_DISCOVERY_OPTION_INCREMENTAL_START);
          onePortContext->DiscFailNSeenBC = agFALSE;
        }
        else
        {
          TI_DBG1(("ossaDeregisterDeviceHandleCB: not calling dmDiscover\n"));
          /* qqqqq needs to change discovery state to onePortContext->DMDiscoveryState == dmDiscCompleted
             in dmQueryDiscovery
             change the discovery state from dmDiscFailed to dmDiscCompleted
          */
          dmResetFailedDiscovery(dmRoot, dmPortContext);

        }
      }
#endif
      else
      {
        if (PhyID < TD_MAX_NUM_PHYS)
        {
          TI_DBG3(("ossaDeregisterDeviceHandleCB: pid %d eventvalid %d registeredNumDevice %d\n", onePortContext->id, tdsaAllShared->eventSource[PhyID].EventValid , onePortContext->RegisteredDevNums));
        }
        else
        {
          TI_DBG3(("ossaDeregisterDeviceHandleCB: pid %d registeredNumDevice %d wrong phyid %d\n", onePortContext->id, onePortContext->RegisteredDevNums, PhyID));
        }
      }
    }
    break;
  case OSSA_INVALID_HANDLE:
    TI_DBG1(("ossaDeregisterDeviceHandleCB: OSSA_INVALID_HANDLE\n"));
    break;
#ifdef REMOVED
  case OSSA_FAILURE_DEVICE_DIRECT_ATTACH:
    TI_DBG1(("ossaDeregisterDeviceHandleCB: OSSA_FAILURE_DEVICE_DIRECT_ATTACH\n"));
    break;
#endif
  case OSSA_ERR_DEVICE_HANDLE_INVALID:
    TI_DBG1(("ossaDeregisterDeviceHandleCB: OSSA_ERR_DEVICE_HANDLE_INVALID\n"));
    break;
  case OSSA_ERR_DEVICE_BUSY:
    TI_DBG1(("ossaDeregisterDeviceHandleCB: OSSA_ERR_DEVICE_BUSY\n"));
    break;
  default:
    TI_DBG1(("ossaDeregisterDeviceHandleCB: unknown status 0x%x\n", status));
    break;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yf");
  return;
}

/*****************************************************************************/
/*! \brief ossaDeviceHandleRemovedEvent
 *
 *
 *  Purpose: This routine is called by lower layer to notify the device removal
 *
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agDevHandle:  Pointer to the assigned device handle for the
 *                         registered device.
 *  \param   agPortContext:Pointer to this instance of port context.
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaDeviceHandleRemovedEvent (
                                agsaRoot_t        *agRoot,
                                agsaDevHandle_t   *agDevHandle,
                                agsaPortContext_t *agPortContext
                                )
{
#ifdef NOT_YET
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
#endif
  tdsaPortContext_t *onePortContext = agNULL;
  tdsaDeviceData_t  *oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yg");
  TI_DBG2(("ossaDeviceHandleRemovedEvent: start\n"));
  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleRemovedEvent: Wrong! oneDeviceData is NULL\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yg");
    return;
  }
  TI_DBG2(("ossaDeviceHandleRemovedEvent: did %d\n", oneDeviceData->id));
  oneDeviceData->registered = agFALSE;
  onePortContext  = (tdsaPortContext_t *)agPortContext->osData;
  if (onePortContext == agNULL)
  {
    TI_DBG1(("ossaDeviceHandleRemovedEvent: Wrong! onePortContext is NULL\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Yg");
    return;
  }
  TI_DBG2(("ossaDeviceHandleRemovedEvent: pid %d\n", onePortContext->id));
  onePortContext->RegisteredDevNums--;
#ifdef NOT_YET
  ostiInitiatorEvent(
                     tiRoot,
                     onePortContext->tiPortalContext,
                     agNULL,
                     tiIntrEventTypeDeviceChange,
                     tiDeviceRemoval,
                     agNULL
                     );
#endif

  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Yg");
  return;
}

#ifdef SPC_ENABLE_PROFILE
/*****************************************************************************/
/*! \brief ossaFwProfileCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saFwProfile()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the operation originally passed
 *                         into saFwProfile()
 *  \param   status:       status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaFwProfileCB(
                    agsaRoot_t          *agRoot,
                    agsaContext_t       *agContext,
                    bit32                status,
                    bit32                len)
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG2(("ossaFwProfileCB: start\n"));

  switch (status)
  {
    case AGSA_RC_SUCCESS:
    {
      TI_DBG2(("ossaFwProfileCB: SUCCESS\n"));
      break;
    }
    case AGSA_RC_FAILURE:
    {
      TI_DBG1(("ossaFwProfileCB: FAIL\n"));
      break;
    }
    default:
    {
      TI_DBG1(("ossaFwProfileCB: !!! default, status %d\n", status));
      break;
    }
  }

  ostiFWProfileIOCTLRsp(tiRoot, status, len);
  return;
}
#endif
/*****************************************************************************/
/*! \brief ossaFwFlashUpdateCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saFwFlashUpdate()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the operation originally passed
 *                         into saFwFlashUpdate()
 *  \param   status:       status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaFwFlashUpdateCB(
                    agsaRoot_t          *agRoot,
                    agsaContext_t       *agContext,
                    bit32               status
                    )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG2(("ossaFwFlashUpdateCB: start\n"));

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yh");
  switch (status)
  {
  case OSSA_FLASH_UPDATE_COMPLETE_PENDING_REBOOT:
  {
    TI_DBG2(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_COMPLETE_PENDING_REBOOT\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_IN_PROGRESS:
  {
    TI_DBG2(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_IN_PROGRESS\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_HDR_ERR:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_HDR_ERR\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_OFFSET_ERR:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_OFFSET_ERR\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_CRC_ERR:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_CRC_ERR\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_LENGTH_ERR:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_LENGTH_ERR\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_HW_ERR:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_HW_ERR\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_DNLD_NOT_SUPPORTED:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_DNLD_NOT_SUPPORTED\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_DISABLED:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_DISABLED\n"));
    break;
  }
  case OSSA_FLASH_FWDNLD_DEVICE_UNSUPPORT:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_FWDNLD_DEVICE_UNSUPPORT\n"));
    break;
  }
  case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE\n"));
    break;
  }
  case OSSA_FLASH_UPDATE_HMAC_ERR:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: OSSA_FLASH_UPDATE_HMAC_ERR\n"));
    break;
  }

  default:
  {
    TI_DBG1(("ossaFwFlashUpdateCB: !!! default, status 0x%X\n", status));
    break;
  }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yh");
  ostiCOMMgntIOCTLRsp(tiRoot, status);
  return;

}


GLOBAL void   ossaFlashExtExecuteCB(
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                    status,
                      bit32                    command,
                      agsaFlashExtResponse_t  *agFlashExtRsp)
{
    TI_DBG1(("ossaFlashExtExecuteCB: command  0x%X status 0x%X\n",command, status));

}



/*****************************************************************************/
/*! \brief ossaGetNVMDResponseCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGetNVMDCommand()
 *
 *  \param   agRoot:           Pointer to chip/driver Instance.
 *  \param   agContext:        Context of the operation originally passed
 *                             into saGetVPDCommand()
 *  \param   status:           status
 *  \param   indirectPayload:  The value passed in agsaNVMDData_t when
 *                             calling saGetNVMDCommand()
 *  \param   agInfoLen:        the length of VPD information
 *  \param   agFrameHandle:    handler of VPD information
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaGetNVMDResponseCB(
                      agsaRoot_t                    *agRoot,
                      agsaContext_t                 *agContext,
                      bit32                         status,
                      bit8                          indirectPayload,
                      bit32                         agInfoLen,
                      agsaFrameHandle_t             agFrameHandle
)
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  TI_DBG2(("ossaGetNVMDResponseCB: start\n"));
  TI_DBG2(("ossaGetNVMDResponseCB: agInfoLen %d\n", agInfoLen));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yi");

  if (status == OSSA_SUCCESS)
  {
    TI_DBG2(("ossaGetNVMDResponseCB: Success status\n"));
    if (indirectPayload == 0 && agInfoLen != 0)
    {
      TI_DBG2(("ossaGetNVMDResponseCB: direct\n"));
      tdhexdump("ossaGetNVMDResponseCB", (bit8 *)agFrameHandle, agInfoLen);
    }
  }
  else
  {
    TI_DBG1(("ossaGetNVMDResponseCB: Status 0x%x\n", status));
  }

  if (indirectPayload == 0)
  {
    TI_DBG2(("ossaGetNVMDResponseCB: direct\n"));
  }
  else
  {
    TI_DBG2(("ossaGetNVMDResponseCB: indirect\n"));
  }

  ostiGetNVMDIOCTLRsp(tiRoot, status);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yi");
  return;
}


/*****************************************************************************/
/*! \brief ossaSetNVMDResponseCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saSetNVMDCommand()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the operation originally passed
 *                         into saSetVPDCommand()
 *  \param   status:       status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaSetNVMDResponseCB(
                      agsaRoot_t            *agRoot,
                      agsaContext_t         *agContext,
                      bit32                 status
                      )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  TI_DBG2(("ossaSetNVMDResponseCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yj");
  if (status == OSSA_SUCCESS)
  {
    TI_DBG2(("ossaSetNVMDResponseCB: success\n"));
  }
  else
  {
    TI_DBG1(("ossaSetNVMDResponseCB: fail or undefined staus %d\n", status));
  }
  ostiSetNVMDIOCTLRsp(tiRoot, status);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yj");
  return;
}


#ifdef REMOVED
/*****************************************************************************/
/*! \brief ossaGetVPDResponseCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGetVPDCommand()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the operation originally passed
 *                         into saGetVPDCommand()
 *  \param   status:       status
 *  \param   agInfoLen:    the length of VPD information
 *  \param   agFrameHandle:handler of VPD information
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaGetVPDResponseCB(
                     agsaRoot_t         *agRoot,
                     agsaContext_t      *agContext,
                     bit32              status,
                     bit8               indirectMode,
                     bit32              agInfoLen,
                     agsaFrameHandle_t  agFrameHandle
                     )
{
  bit8 VPDData[48];

  TI_DBG2(("ossaGetVPDResponseCB: start\n"));

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yk");
  if (status == OSSA_SUCCESS)
  {
    TI_DBG2(("ossaGetVPDResponseCB: agInfoLen %d\n", agInfoLen));
    osti_memset(VPDData, 0, 48);
    /* We can read only in case of Direct */
    saFrameReadBlock(agRoot, agFrameHandle, 0, VPDData, agInfoLen);
    tdhexdump("ossaGetVPDResponseCB", (bit8 *)VPDData, agInfoLen);
    /*
      callback osti....
    */
  }
  else
  {
    TI_DBG1(("ossaGetVPDResponseCB: fail or undefined staus %d\n", status));
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yk");
  return;
}


/*****************************************************************************/
/*! \brief ossaSetVPDResponseCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saSetVPDCommand()
 *
 *  \param   agRoot:       Pointer to chip/driver Instance.
 *  \param   agContext:    Context of the operation originally passed
 *                         into saSetVPDCommand()
 *  \param   status:       status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaSetVPDResponseCB(
                     agsaRoot_t         *agRoot,
                     agsaContext_t      *agContext,
                     bit32              status
                     )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG2(("ossaSetVPDResponseCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yl");

  if (status == OSSA_SUCCESS)
  {
    TI_DBG2(("ossaSetVPDResponseCB: success\n"));
    ostiCOMMgntVPDSetIOCTLRsp(tiRoot, 0);
    /*
      callback osti.....
    */

#ifdef VPD_TESTING
    /* temporary to test saSetVPDCommand() and saGetVPDCommand */
    tdsaVPDGet(tiRoot);
#endif

  }
  else
  {
    TI_DBG1(("ossaSetVPDResponseCB: fail or undefined staus %d\n", status));
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yl");
  return;
}
#endif

/*****************************************************************************/
/*! \brief ossaEchoCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saEchoCommand()
 *
 *  \param   agRoot:        Pointer to chip/driver Instance.
 *  \param   agContext:     Context of the operation originally passed
 *                          into saEchoCommand()
 *  \param   echoPayload:   Pointer to the echo payload
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaEchoCB(
            agsaRoot_t      *agRoot,
            agsaContext_t   *agContext,
            void            *echoPayload
          )
{
#ifdef ECHO_TESTING
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit8                payload[56];
#endif

  TI_DBG2(("ossaEchoCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Ym");

  /* dumping received echo payload is 56 bytes */
  tdhexdump("ossaEchoCB: echoPayload", (bit8 *)(echoPayload), 56);

#ifdef ECHO_TESTING
  /* temporary to test saEchoCommand() */

  /* new echo payload */
  osti_memset(payload,0, sizeof(payload));

  payload[0] = gEcho;
  payload[55] = gEcho;

  TI_DBG2(("ossaEchoCB: gEcho %d\n", gEcho));

  saEchoCommand(agRoot, agNULL, tdsaRotateQnumber(tiRoot, agNULL), (void *)&payload);

  if (gEcho == 0xFF)
  {
    gEcho = 0;
  }
  else
  {
    gEcho++;
  }
#endif

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Ym");
  return;
}

/*****************************************************************************/
/*! \brief ossaGpioResponseCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGpioEventSetup(), saGpioPinSetup(), saGpioRead(), or
 *           saGpioWrite()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally passed
 *                                in.
 *  \param   status:              GPIO operation completion status
 *  \param   gpioReadValue:       a bit map containing the corresponding
 *                                value for each GPIO pin.
 *  \param   gpioPinSetupInfo:    Pointer to agsaGpioPinSetupInfo_t structure
 *                                describing the GPIO pin setup
 *  \param   gpioEventSetupInfo   Pointer to agsaGpioEventSetupInfo_t structure
 *                                describing the GPIO event setups
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaGpioResponseCB(
                   agsaRoot_t               *agRoot,
                   agsaContext_t            *agContext,
                   bit32                    status,
                   bit32                    gpioReadValue,
                   agsaGpioPinSetupInfo_t   *gpioPinSetupInfo,
                   agsaGpioEventSetupInfo_t *gpioEventSetupInfo
                   )
{
  TI_DBG2(("ossaGpioResponseCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yn");
  if (status == OSSA_SUCCESS)
  {
    TI_DBG2(("ossaGpioResponseCB: Success\n"));
    /* printing gpioReadValue, agsaGpioPinSetupInfo_t and agsaGpioEventSetupInfo_t */
    TI_DBG2(("ossaGpioResponseCB: gpioReadValue 0x%x\n", gpioReadValue));
    TI_DBG2(("ossaGpioResponseCB: PinSetupInfo gpioInputEnabled 0x%x\n", gpioPinSetupInfo->gpioInputEnabled));
    TI_DBG2(("ossaGpioResponseCB: PinSetupInfo gpioTypePart1 0x%x\n", gpioPinSetupInfo->gpioTypePart1));
    TI_DBG2(("ossaGpioResponseCB: PinSetupInfo gpioTypePart2 0x%x\n", gpioPinSetupInfo->gpioTypePart2));
    TI_DBG2(("ossaGpioResponseCB: EventSetupInfo gpioEventLevel 0x%x\n", gpioEventSetupInfo->gpioEventLevel));
    TI_DBG2(("ossaGpioResponseCB: EventSetupInfo gpioEventRisingEdge 0x%x\n", gpioEventSetupInfo->gpioEventRisingEdge));
    TI_DBG2(("ossaGpioResponseCB: EventSetupInfo gpioEventFallingEdge 0x%x\n", gpioEventSetupInfo->gpioEventFallingEdge));
  }
  else
  {
    TI_DBG1(("ossaGpioResponseCB: Failure\n"));
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yn");
  return;
}

/*****************************************************************************/
/*! \brief ossaGpioEvent
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGpioEventSetup(), saGpioPinSetup(), saGpioRead(), or
 *           saGpioWrite()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   gpioEvent:           a bit map that indicates which GPIO
 *                                input pins have generated the event.
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaGpioEvent(
              agsaRoot_t    *agRoot,
              bit32         gpioEvent
              )
{
  TI_DBG2(("ossaGpioEvent: start\n"));
  TI_DBG2(("ossaGpioEvent: gpioEvent 0x%x\n", gpioEvent));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yo");
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yo");
  return;
}


/*****************************************************************************/
/*! \brief ossaSASDiagExecuteCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saSASDiagExecute()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally passed
 *                                in.
 *  \param   status:              Diagnostic operation completion status
 *  \param   command:             SAS diagnostic command field in agsaSASDiagExecute_t
 *                                structure passed in saSASDiagExecute().
 *  \param   reportData:          Report Diagnostic Data
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaSASDiagExecuteCB(
                      agsaRoot_t      *agRoot,
                      agsaContext_t   *agContext,
                      bit32           status,
                      bit32           command,
                      bit32           reportData)
{
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yq");
  TI_DBG2(("ossaSASDiagExecuteCB: start\n"));
  TI_DBG2(("ossaSASDiagExecuteCB: status %d\n", status));
  TI_DBG2(("ossaSASDiagExecuteCB: command %d\n", command));
  TI_DBG2(("ossaSASDiagExecuteCB: reportData %d\n", reportData));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yq");
  return;

}


/*****************************************************************************/
/*! \brief ossaSASDiagStartEndCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saSASDiagExecute()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally passed
 *                                in.
 *  \param   status:              Diagnostic operation completion status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void ossaSASDiagStartEndCB(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             status)
{
  TI_DBG2(("ossaSASDiagStartEndCB: start\n"));
  TI_DBG2(("ossaSASDiagStartEndCB: status %d\n", status));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yr");
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yr");
  return;
}

/*****************************************************************************/
/*! \brief ossaReconfigSASParamsCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saReconfigSASParams()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally passed
 *                                in saReconfigSASParams().
 *  \param   status:              saReconfigSASParams() completion status
 *  \param   agSASConfig:         Pointer to the data structure agsaSASReconfig_t
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void   ossaReconfigSASParamsCB(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        bit32             status,
                        agsaSASReconfig_t *agSASConfig)
{
  TI_DBG2(("ossaReconfigSASParamsCB: status %d\n", status));
  return;
}

GLOBAL void ossaPCIeDiagExecuteCB(
            agsaRoot_t             *agRoot,
            agsaContext_t         *agContext,
            bit32                  status,
            bit32                  command,
            agsaPCIeDiagResponse_t *resp )
{
  TI_DBG2(("ossaPCIeDiagExecuteCB: status %d\n", status));
  TI_DBG2(("ossaPCIeDiagExecuteCB: ERR_BLKH 0x%X\n",resp->ERR_BLKH ));
  TI_DBG2(("ossaPCIeDiagExecuteCB: ERR_BLKL 0x%X\n",resp->ERR_BLKL ));
  TI_DBG2(("ossaPCIeDiagExecuteCB: DWord8 0x%X\n",resp->DWord8 ));
  TI_DBG2(("ossaPCIeDiagExecuteCB: DWord9 0x%X\n",resp->DWord9 ));
  TI_DBG2(("ossaPCIeDiagExecuteCB: DWord10 0x%X\n",resp->DWord10 ));
  TI_DBG2(("ossaPCIeDiagExecuteCB: DWord11 0x%X\n",resp->DWord11 ));
  TI_DBG2(("ossaPCIeDiagExecuteCB: DIF_ERR 0x%X\n",resp->DIF_ERR ));

  return;
}


#ifndef BIOS
GLOBAL void ossaSGpioCB(
                    agsaRoot_t              *agRoot,
                    agsaContext_t           *agContext, 
                    agsaSGpioReqResponse_t  *pSgpioResponse
                    )
{
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
	
  TI_DBG2(("ossaSGpioCB:  smpFrameType: 0x%02x \n", pSgpioResponse->smpFrameType));
 // printf("SS:ossaSGpioCB:  smpFrameType: 0x%02x \n", pSgpioResponse->smpFrameType);
  TI_DBG2(("ossaSGpioCB:  function: 0x%02x \n", pSgpioResponse->function));
  TI_DBG2(("ossaSGpioCB:  functionResult: 0x%02x \n", pSgpioResponse->functionResult));
  //printf("SS:ossaSGpioCB:  functionResult: 0x%02x \n", pSgpioResponse->functionResult);
  
  tdhexdump("ossaSGpioCB Response", (bit8 *)pSgpioResponse, sizeof(agsaSGpioReqResponse_t));
  ostiSgpioIoctlRsp(tiRoot, pSgpioResponse);	
}

#endif /* BIOS */

/*****************************************************************************/
/*! \brief ossaLogDebugString
 *
 *
 *  Purpose: This routine is called by lower layer to log.
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   level:               Detail of information desired.
 *  \param   string:              Pointer to the character string.
 *  \param   ptr1:                First pointer value.
 *  \param   ptr2:                Second pointer value.
 *  \param   value1:              First 32-bit value related to the specific information.
 *  \param   value2:              Second 32-bit value related to the specific information.
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaLogDebugString(
                         agsaRoot_t   *agRoot,
                         bit32        level,
                         char         *string,
                         void         *ptr1,
                         void         *ptr2,
                         bit32        value1,
                         bit32        value2
                         )
{
#if defined(SALLSDK_DEBUG)
  TIDEBUG_MSG(gLLDebugLevel, level, ("%s %p %p %d %d\n", string, ptr1, ptr2, value1, value2));
#endif
  return;
}

/*****************************************************************************/
/*! \brief ossaHwEventAckCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saHwEventAck(()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally passed
 *                                in.
 *  \param   status:              Status
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaHwEventAckCB(
                             agsaRoot_t         *agRoot,
                             agsaContext_t      *agContext,
                             bit32              status
                             )
{
  TI_DBG3(("ossaHwEventAckCB: start\n"));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Ys");
  if (status == tiSuccess)
  {
    TI_DBG3(("ossaHwEventAckCB: SUCCESS status\n"));
  }
  else
  {
    TI_DBG1(("ossaHwEventAckCB: FAIL status 0x%X\n", status));
    TI_DBG1(("ossaHwEventAckCB: invalid event status bit0 %d\n", status & 0x01));
    TI_DBG1(("ossaHwEventAckCB: invalid phyid status bit1 %d\n", (status & 0x02) >> 1 ));
    TI_DBG1(("ossaHwEventAckCB: invalid portcontext status bit2 %d\n", (status & 0x04) >> 2));
    TI_DBG1(("ossaHwEventAckCB: invalid param0 status bit3 %d\n", (status & 0x08) >> 3));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Ys");
  return;
}

/*****************************************************************************/
/*! \brief ossaGetTimeStampCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saGetTimeStamp()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally passed
 *                                in.
 *  \param   timeStampLower:      The controller lower 32-bit of internal time
 *                                stamp associated with event log.
 *  \param   timeStampUpper:      The controller upper 32-bit of internal time
 *                                stamp associated with event log.
 *
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaGetTimeStampCB(
                         agsaRoot_t    *agRoot,
                         agsaContext_t *agContext,
                         bit32         timeStampLower,
                         bit32         timeStampUpper
                         )
{
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yt");
  TI_DBG4(("ossaGetTimeStampCB: start\n"));
  TI_DBG4(("ossaGetTimeStampCB: timeStampUpper 0x%x timeStampLower 0x%x\n", timeStampUpper, timeStampLower));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yt");
  return;
}


/*****************************************************************************/
/*! \brief ossaSMPAbortCB
 *
 *
 *  Purpose: This routine is called by lower layer to corresponding to
 *           saSMPAbort()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agIORequest:         This is the agIORequest parameter passed in
 *                                saSMPAbort()
 *  \param   status:              Status of abort
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaSMPAbortCB(
                           agsaRoot_t           *agRoot,
                           agsaIORequest_t      *agIORequest,
                           bit32                flag,
                           bit32                status)
{
  tdsaRootOsData_t        *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t       *tdAbortIORequestBody = agNULL;
  tdsaDeviceData_t        *oneDeviceData        = agNULL;
  tiDeviceHandle_t        *tiDeviceHandle       = agNULL;

  TI_DBG4(("ossaSMPAbortCB: start\n"));
  TI_DBG4(("ossaSMPAbortCB: flag %d\n", flag));
  TI_DBG4(("ossaSMPAbortCB: status %d\n", status));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yu");

  tdAbortIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  if (tdAbortIORequestBody == agNULL)
  {
    TI_DBG1(("ossaSMPAbortCB: tdAbortIORequestBody is NULL warning!!!!\n"));
    return;
  }

  if (flag == 2)
  {
    /* abort per port */
    TI_DBG2(("ossaSMPAbortCB: abort per port\n"));
  }
  else if (flag == 1)
  {
    TI_DBG2(("ossaSMPAbortCB: abort all\n"));

    tiDeviceHandle = (tiDeviceHandle_t *)tdAbortIORequestBody->tiDevHandle;
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("ossaSMPAbortCB: tiDeviceHandle is NULL warning!!!!\n"));
      ostiFreeMemory(
               tiRoot,
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(tdIORequestBody_t)
               );
      return;
    }

    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("ossaSMPAbortCB: oneDeviceData is NULL warning!!!!\n"));
      ostiFreeMemory(
               tiRoot,
               tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
               sizeof(tdIORequestBody_t)
               );
      return;
    }

    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG2(("ossaSMPAbortCB: OSSA_IO_SUCCESS\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG3(("ossaSMPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG2(("ossaSMPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_NOT_VALID\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG1(("ossaSMPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG1(("ossaSMPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_NO_DEVICE\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG1(("ossaSMPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG1(("ossaSMPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG1(("ossaSMPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG1(("ossaSMPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_ABORT_DELAYED\n"));
      /* clean up TD layer's IORequestBody */
      TI_DBG1(("ossaSMPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG1(("ossaSMPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#endif
    else
    {
      TI_DBG1(("ossaSMPAbortCB: other status %d\n", status));
      /* clean up TD layer's IORequestBody */
      TI_DBG1(("ossaSMPAbortCB: calling saDeregisterDeviceHandle\n"));
      saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
      TI_DBG1(("ossaSMPAbortCB: did %d\n", oneDeviceData->id));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else if (flag == 0)
  {
    TI_DBG2(("ossaSMPAbortCB: abort one\n"));
    if (status == OSSA_IO_SUCCESS)
    {
      TI_DBG2(("ossaSMPAbortCB: OSSA_IO_SUCCESS\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );

    }
    else if (status == OSSA_IO_NOT_VALID)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_NOT_VALID\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_NO_DEVICE)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_NO_DEVICE\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
    else if (status == OSSA_IO_ABORT_IN_PROGRESS)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_ABORT_IN_PROGRESS\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#ifdef REMOVED
    else if (status == OSSA_IO_ABORT_DELAYED)
    {
      TI_DBG1(("ossaSMPAbortCB: OSSA_IO_ABORT_DELAYED\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
#endif
    else
    {
      TI_DBG1(("ossaSMPAbortCB: other status %d\n", status));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }
  else
  {
    TI_DBG1(("ossaSMPAbortCB: wrong flag %d\n", flag));
  }


  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yu");
  return;
}

/*****************************************************************************/
/*! \brief ossaGeneralEvent
 *
 *
 *  Purpose: This is the event notification for debugging purposes sent to
 *           inform the OS layer of some general error related to a specific
 *           inbound operation.
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   status:              Status associated with this event
 *  \param   msg:                 Pointer to controller specific command
 *                                massage that caused the error
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaGeneralEvent(
                             agsaRoot_t    *agRoot,
                             bit32         status,
                             agsaContext_t *agContext,
                             bit32         *msg)
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG1(("ossaGeneralEvent: start\n"));
  TI_DBG1(("ossaGeneralEvent: status %d\n", status));

  if(msg)
  {
    TI_DBG1(("ossaGeneralEvent: *msg %X\n", *msg));
  }

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yv");
  ostiGenEventIOCTLRsp(tiRoot, status);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yv");
  return;
}

GLOBAL void   ossaGetForensicDataCB (
        agsaRoot_t         *agRoot,
        agsaContext_t      *agContext,
        bit32              status,
        agsaForensicData_t *forensicData)
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  ostiGetForensicDataIOCTLRsp(tiRoot, status, forensicData);
  return;
}


#ifdef INITIATOR_DRIVER

GLOBAL void ossaGetIOErrorStatsCB (
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                     status,
                      agsaIOErrorEventStats_t  *stats)

{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  ostiGetIoErrorStatsIOCTLRsp(tiRoot, status, stats);
}
#else
GLOBAL void ossaGetIOErrorStatsCB (
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                     status,
                      agsaIOErrorEventStats_t  *stats)

{

}

#endif

GLOBAL void ossaGetIOEventStatsCB (
                      agsaRoot_t               *agRoot,
                      agsaContext_t            *agContext,
                      bit32                     status,
                      agsaIOErrorEventStats_t  *stats)

{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  ostiGetIoEventStatsIOCTLRsp(tiRoot, status, stats);
}


/*****************************************************************************/
/*! \brief ossaGetRegisterDumpCB
 *
 *
 *  Purpose: ossaGetRegisterDumpCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saGetRegisterDump()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into saGetRegisterDump()
 *  \param   status:              status
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaGetRegisterDumpCB(
                                  agsaRoot_t    *agRoot,
                                  agsaContext_t *agContext,
                                  bit32         status
)
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;

  TI_DBG4(("ossaGetRegisterDumpCB: start\n"));
  TI_DBG4(("ossaGetRegisterDumpCB: status %d\n", status));
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Yw");

  ostiRegDumpIOCTLRsp(tiRoot, status);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Yw");
  return;
}

/*****************************************************************************/
/*! \brief ossaSetDeviceStateCB
 *
 *
 *  Purpose: ossaSetDeviceStateCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saSetDeviceState()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into saGetRegisterDump()
 *  \param   agDevHandle          Pointer to the device handle of the device
 *  \param   status:              status
 *  \param   newDeviceState:      newly set device status
 *  \param   previousDeviceState: old device status
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaSetDeviceStateCB(
                                 agsaRoot_t         *agRoot,
                                 agsaContext_t      *agContext,
                                 agsaDevHandle_t    *agDevHandle,
                                 bit32              status,
                                 bit32              newDeviceState,
                                 bit32              previousDeviceState
                                 )
{
  tdsaDeviceData_t            *oneDeviceData = agNULL;

  TI_DBG2(("ossaSetDeviceStateCB: start\n"));
  TI_DBG2(("ossaSetDeviceStateCB: status %d\n", status));
  TI_DBG2(("ossaSetDeviceStateCB: newDeviceState %d\n", newDeviceState));
  TI_DBG2(("ossaSetDeviceStateCB: previousDeviceState %d\n", previousDeviceState));

  if (agDevHandle == agNULL)
  {
    TI_DBG4(("ossaSetDeviceStateCB: agDevHandle is NULL\n"));
    return;
  }

  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("ossaSetDeviceStateCB: wrong; oneDeviceData is NULL\n"));
  }
  else
  {
    TI_DBG2(("ossaSetDeviceStateCB: did %d\n", oneDeviceData->id));
  }

  return;
}

/*****************************************************************************/
/*! \brief ossaGetDeviceStateCB
 *
 *
 *  Purpose: ossaGetDeviceStateCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saGetDeviceState()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into saGetRegisterDump()
 *  \param   agDevHandle          Pointer to the device handle of the device
 *  \param   status:              status
 *  \param   deviceState:         device status
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaGetDeviceStateCB(
                                 agsaRoot_t         *agRoot,
                                 agsaContext_t      *agContext,
                                 agsaDevHandle_t    *agDevHandle,
                                 bit32              status,
                                 bit32              deviceState
                                 )
{
  TI_DBG4(("ossaGetDeviceStateCB: start\n"));
  TI_DBG4(("ossaGetDeviceStateCB: status %d\n", status));
  TI_DBG4(("ossaGetDeviceStateCB: deviceState %d\n", deviceState));

  return;
}

#ifdef INITIATOR_DRIVER
/*****************************************************************************/
/*! \brief ossaIniSetDeviceInfoCB
 *
 *
 *  Purpose: ossaIniSetDeviceInfoCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saSetDeviceInfo()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into saSetDeviceInfo()
 *  \param   agDevHandle          Pointer to the device handle of the device
 *  \param   status:              status
 *  \param   option:              option parameter passed in saSetDeviceInfo()
 *  \param   param:               param parameter passed in saSetDeviceInfo()
 *
 *  \return None.
 *
 */
/*****************************************************************************/
osGLOBAL void
ossaIniSetDeviceInfoCB(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext,
                        agsaDevHandle_t   *agDevHandle,
                        bit32             status,
                        bit32             option,
                        bit32             param
                      )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t             *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t          *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t             *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody = agNULL;
  agsaIORequest_t        *agIORequest = agNULL;
  bit32                  saStatus = AGSA_RC_FAILURE;
  bit8                   devType_S_Rate;
  tdsaDeviceData_t       *oneDeviceData = agNULL;

  TI_DBG4(("ossaIniSetDeviceInfoCB: start\n"));
  TI_DBG4(("ossaIniSetDeviceInfoCB: status 0x%x\n", status));
  TI_DBG4(("ossaIniSetDeviceInfoCB: option 0x%x\n", option));
  TI_DBG4(("ossaIniSetDeviceInfoCB: param 0x%x\n", param));

  if (status != OSSA_SUCCESS)
  {
    TI_DBG1(("ossaIniSetDeviceInfoCB: status %d\n", status));
    TI_DBG1(("ossaIniSetDeviceInfoCB: option 0x%x\n", option));
    TI_DBG1(("ossaIniSetDeviceInfoCB: param 0x%x\n", param));
    if (option == 32) /* set connection rate */
    {
      TI_DBG1(("ossaIniSetDeviceInfoCB: IO failure\n"));
      agIORequest = (agsaIORequest_t *)agContext->osData;
      tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
      ostiInitiatorIOCompleted(
                               tiRoot,
                               tdIORequestBody->tiIORequest,
                               tiIOFailed,
                               tiDetailOtherError,
                               agNULL,
                               intContext
                               );
    }
  }
  if (agDevHandle == agNULL)
  {
    TI_DBG4(("ossaIniSetDeviceInfoCB: agDevHandle is NULL\n"));
    return;
  }
  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;
  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("ossaIniSetDeviceInfoCB: wrong; oneDeviceData is NULL\n"));
    return;
  }
  else
  {
    TI_DBG4(("ossaIniSetDeviceInfoCB: did %d\n", oneDeviceData->id));
  }

  /* retry IOs */
  if (option == 32) /* set connection rate */
  {
    TI_DBG1(("ossaIniSetDeviceInfoCB: set connection rate option\n"));
    agIORequest = (agsaIORequest_t *)agContext->osData;
    tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
    devType_S_Rate = oneDeviceData->agDeviceInfo.devType_S_Rate;
    devType_S_Rate = (devType_S_Rate & 0xF0) | (param >> 28);
    oneDeviceData->agDeviceInfo.devType_S_Rate =  devType_S_Rate;
    TI_DBG1(("ossaIniSetDeviceInfoCB: new rate is 0x%x\n", DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo)));
    if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
        oneDeviceData->tdPortContext != agNULL )
    {
      saStatus = saSSPStart(agRoot,
                            agIORequest,
                            tdsaRotateQnumber(tiRoot, oneDeviceData),
                            agDevHandle,
                            tdIORequestBody->agRequestType,
                            &(tdIORequestBody->transport.SAS.agSASRequestBody),
                            agNULL,
                            &ossaSSPCompleted);
      if (saStatus == AGSA_RC_SUCCESS)
      {
        TI_DBG1(("ossaIniSetDeviceInfoCB: retried\n"));
        Initiator->NumIOsActive++;
        tdIORequestBody->ioStarted = agTRUE;
        tdIORequestBody->ioCompleted = agFALSE;
        return;
      }
      else
      {
        TI_DBG1(("ossaIniSetDeviceInfoCB: retry failed\n"));
        tdIORequestBody->ioStarted = agFALSE;
        tdIORequestBody->ioCompleted = agTRUE;
        ostiInitiatorIOCompleted(
                                 tiRoot,
                                 tdIORequestBody->tiIORequest,
                                 tiIOFailed,
                                 tiDetailOtherError,
                                 agNULL,
                                 intContext
                                 );
       }
    }
  }
  return;
}
#endif
/*****************************************************************************/
/*! \brief ossaSetDeviceInfoCB
 *
 *
 *  Purpose: ossaSetDeviceInfoCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saSetDeviceInfo()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into saSetDeviceInfo()
 *  \param   agDevHandle          Pointer to the device handle of the device
 *  \param   status:              status
 *  \param   option:              option parameter passed in saSetDeviceInfo()
 *  \param   param:               param parameter passed in saSetDeviceInfo()
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaSetDeviceInfoCB(
                                 agsaRoot_t         *agRoot,
                                 agsaContext_t      *agContext,
                                 agsaDevHandle_t    *agDevHandle,
                                 bit32              status,
                                 bit32              option,
                                 bit32              param
                                )
{
  tdsaDeviceData_t       *oneDeviceData = agNULL;

  TI_DBG4(("ossaSetDeviceInfoCB: start\n"));
  TI_DBG4(("ossaSetDeviceInfoCB: status 0x%x\n", status));
  TI_DBG4(("ossaSetDeviceInfoCB: option 0x%x\n", option));
  TI_DBG4(("ossaSetDeviceInfoCB: param 0x%x\n", param));

  if (status != OSSA_SUCCESS)
  {
    TI_DBG1(("ossaSetDeviceInfoCB: status %d\n", status));
    TI_DBG1(("ossaSetDeviceInfoCB: option 0x%x\n", option));
    TI_DBG1(("ossaSetDeviceInfoCB: param 0x%x\n", param));
  }

  if (agDevHandle == agNULL)
  {
    TI_DBG4(("ossaSetDeviceInfoCB: agDevHandle is NULL\n"));
    return;
  }

  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("ossaSetDeviceInfoCB: wrong; oneDeviceData is NULL\n"));
  }
  else
  {
    TI_DBG4(("ossaSetDeviceInfoCB: did %d\n", oneDeviceData->id));
  }

  return;
}

/*****************************************************************************/
/*! \brief ossaGetDFEDataCB
 *
 *
 *  Purpose: ossaGetDFEDataCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saGetDFEData()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into saGetDFEData()
 *  \param   status:              status
 *  \param   agInfoLen:           length in bytes of DFE data captured and transferred
 *
 *  \return None.
 *
 */
/*****************************************************************************/
GLOBAL void ossaGetDFEDataCB(
                             agsaRoot_t     *agRoot,
                             agsaContext_t  *agContext,
                             bit32   status,
                             bit32   agInfoLen)
{
  TI_DBG1(("ossaGetDFEDataCB: start\n"));
  TI_DBG1(("ossaGetDFEDataCB: status 0x%x agInfoLen 0x%x\n", status, agInfoLen));
  return;
}

/*****************************************************************************/
/*! \brief ossaVhistCaptureCB
 *
 *
 *  Purpose: ossaVhistCaptureCB() is the response callback function
 *           called by the LL Layer to indicate a response to
 *           saGetDFEData()
 *
 *  \param   agRoot:              Pointer to chip/driver Instance.
 *  \param   agContext:           Context of the operation originally
 *                                passed into ()
 *  \param   status:              status
 *  \param   len:           length in bytes of Vis data captured and transferred
 *
 *  \return None.
 *
 */
/*****************************************************************************/

void ossaVhistCaptureCB(
        agsaRoot_t    *agRoot,
        agsaContext_t *agContext,
        bit32         status,
        bit32         len)
{
  TI_DBG1(("ossaVhistCaptureCB: start\n"));
  TI_DBG1(("ossaVhistCaptureCB: status 0x%x agInfoLen 0x%x\n", status,len ));
  return;
}

GLOBAL void ossaOperatorManagementCB(
                  agsaRoot_t    *agRoot,
                  agsaContext_t *agContext,
                  bit32          status,
                  bit32          eq
                  )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiEncryptPort_t      encryptEventData;

  TI_DBG1(("ossaOperatorManagementCB: status 0x%x eq 0x%x\n", status, eq));

  osti_memset(&encryptEventData, 0, sizeof(tiEncryptPort_t));
  encryptEventData.encryptEvent = tiEncryptOperatorManagement;
  encryptEventData.subEvent = eq;
  encryptEventData.pData = agNULL;

  ostiPortEvent(tiRoot,
              tiEncryptOperation,
              status,
              &encryptEventData);
}

GLOBAL void ossaEncryptSelftestExecuteCB (
                        agsaRoot_t    *agRoot,
                        agsaContext_t *agContext,
                        bit32          status,
                        bit32          type,
                        bit32          length,
                        void          *TestResult
                        )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiEncryptPort_t      encryptEventData;

  TI_DBG1(("ossaEncryptSelftestExecuteCB: status 0x%x type 0x%x length 0x%x\n", status, type, length));

  osti_memset(&encryptEventData, 0, sizeof(tiEncryptPort_t));
  encryptEventData.encryptEvent = tiEncryptSelfTest;
  encryptEventData.subEvent = type;
  encryptEventData.pData = (void*)TestResult;

  ostiPortEvent(tiRoot,
              tiEncryptOperation,
              status,
              &encryptEventData);
}

GLOBAL void ossaGetOperatorCB(
               agsaRoot_t    *agRoot,
               agsaContext_t *agContext,
               bit32          status,
               bit32          option,
               bit32          num,
               bit32          role,
               agsaID_t      *id
               )
{

  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiEncryptPort_t      encryptEventData;

  TI_DBG1(("ossaGetOperatorCB: status 0x%x option 0x%x num 0x%x role 0x%x\n",
                status, option, num, role));
  TI_DBG1(("ossaGetOperatorCB: agContext %p id %p\n",agContext,id));
  osti_memset(&encryptEventData, 0, sizeof(tiEncryptPort_t));
  encryptEventData.encryptEvent = tiEncryptGetOperator;
  encryptEventData.subEvent = option;
  encryptEventData.pData = agNULL;

  switch(status)
  {
    case OSSA_IO_SUCCESS:
      TI_DBG1(("ossaGetOperatorCB: OSSA_IO_SUCCESS option 0x%x\n", option));
      if(option == 1)
      {
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[0], id->ID[1], id->ID[2], id->ID[3]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[4], id->ID[5], id->ID[6], id->ID[7]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[8], id->ID[9], id->ID[10],id->ID[11]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[12],id->ID[13],id->ID[14],id->ID[15]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[16],id->ID[17],id->ID[18],id->ID[19]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[20],id->ID[21],id->ID[22],id->ID[23]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x 0x%02x\n",id->ID[24],id->ID[25],id->ID[26],id->ID[27]));
        TI_DBG2(("ossaGetOperatorCB: 0x%02x 0x%02x 0x%02x\n",       id->ID[28],id->ID[29],id->ID[30]));
      }else if(option == 2)
      {
        TI_DBG1(("ossaGetOperatorCB: number operators 0x%02x\n", num ));
      }

      encryptEventData.pData = id;
      break;
    case OSSA_MPI_ENC_ERR_UNSUPPORTED_OPTION:
      TI_DBG1(("ossaGetOperatorCB: OSSA_MPI_ENC_ERR_UNSUPPORTED_OPTION 0x%x\n",option));
      break;
    case OSSA_MPI_ENC_ERR_ID_TRANSFER_FAILURE:
      TI_DBG1(("ossaGetOperatorCB: OSSA_MPI_ENC_ERR_ID_TRANSFER_FAILURE 0x%x\n",option));
      break;
    default:
      TI_DBG1(("ossaGetOperatorCB: Unknown status 0x%x\n",status));
  }
  ostiPortEvent(tiRoot,
              tiEncryptOperation,
              status,
              &encryptEventData);

}

GLOBAL void ossaSetOperatorCB(
              agsaRoot_t    *agRoot,
              agsaContext_t *agContext,
              bit32          status,
              bit32          eq
              )
{
  tdsaRootOsData_t    *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t            *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiEncryptPort_t      encryptEventData;

  TI_DBG1(("ossaSetOperatorCB: agContext %p status 0x%x eq 0x%x\n",agContext, status, eq));

  osti_memset(&encryptEventData, 0, sizeof(tiEncryptPort_t));
  encryptEventData.encryptEvent = tiEncryptSetOperator;
  encryptEventData.subEvent = 0;
  switch(status)
  {
    case OSSA_IO_SUCCESS:
      TI_DBG1(("ossaSetOperatorCB: OSSA_IO_SUCCESS\n"));
      encryptEventData.pData = agNULL;
      break;
    case OSSA_MPI_ENC_ERR_CONTROLLER_NOT_IDLE:
      TI_DBG1(("ossaSetOperatorCB: OSSA_MPI_ENC_ERR_CONTROLLER_NOT_IDLE\n"));
      break;
    case OSSA_MPI_ENC_OPERATOR_AUTH_FAILURE:
      TI_DBG1(("ossaSetOperatorCB: OSSA_MPI_ENC_OPERATOR_AUTH_FAILURE error qualifier 0x%x\n",eq));
      break;
    case OSSA_MPI_ENC_OPERATOR_OPERATOR_ALREADY_LOGGED_IN:
      TI_DBG1(("ossaSetOperatorCB: OSSA_MPI_ENC_OPERATOR_OPERATOR_ALREADY_LOGGED_IN\n"));
      break;
    case OSSA_MPI_ENC_OPERATOR_ILLEGAL_PARAMETER:
      TI_DBG1(("ossaSetOperatorCB: OSSA_MPI_ENC_OPERATOR_ILLEGAL_PARAMETER\n"));
      break;
    case OSSA_MPI_ENC_ERR_UNSUPPORTED_OPTION:
      TI_DBG1(("ossaSetOperatorCB: OSSA_MPI_ENC_ERR_UNSUPPORTED_OPTION\n"));
      break;
    case OSSA_MPI_ENC_ERR_ID_TRANSFER_FAILURE:
      TI_DBG1(("ossaSetOperatorCB: OSSA_MPI_ENC_ERR_ID_TRANSFER_FAILURE\n"));
      break;
    default:
      TI_DBG1(("ossaGetOperatorCB: Unknown status 0x%x\n",status));
  }
  ostiPortEvent(tiRoot,
              tiEncryptOperation,
              status,
              &encryptEventData);
}

GLOBAL void ossaDIFEncryptionOffloadStartCB(
                             agsaRoot_t     *agRoot,
                             agsaContext_t  *agContext,
                             bit32   status,
                             agsaOffloadDifDetails_t *agsaOffloadDifDetails)
{
  TI_DBG1(("ossaDIFEncryptionOffloadStartCB: start\n"));
  TI_DBG1(("ossaDIFEncryptionOffloadStartCB: status 0x%x agsaOffloadDifDetails=%p\n", status, agsaOffloadDifDetails));
  return;
}

GLOBAL bit32 ossaTimeStamp( agsaRoot_t     *agRoot )
{
  tdsaRootOsData_t    *osData= agNULL;
  tiRoot_t            *tiRoot= agNULL;
  if(agRoot)
  {
    osData = (tdsaRootOsData_t *)agRoot->osData;
  }
  if(osData)
  {
    tiRoot = (tiRoot_t *)osData->tiRoot;
  }
  return(ostiTimeStamp(tiRoot));
} 

GLOBAL bit64 ossaTimeStamp64( agsaRoot_t     *agRoot)
{
  tdsaRootOsData_t    *osData= agNULL;
  tiRoot_t            *tiRoot= agNULL;
  if(agRoot)
  {
    osData = (tdsaRootOsData_t *)agRoot->osData;
  }
  if(osData)
  {
    tiRoot = (tiRoot_t *)osData->tiRoot;
  }
  return(ostiTimeStamp64(tiRoot));
} 

#ifdef FDS_SM
osGLOBAL void
tdIDStartTimer(tiRoot_t                 *tiRoot,
                  smIORequest_t            *smIORequest,
                  tdsaDeviceData_t         *oneDeviceData
                  )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;

  TI_DBG1(("tdIDStartTimer: start\n"));

  tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
  if (oneDeviceData->tdIDTimer.timerRunning == agTRUE)
  {
    tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
    tdsaKillTimer(
                  tiRoot,
                  &oneDeviceData->tdIDTimer
                  );
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
  }

  tdsaSetTimerRequest(
                      tiRoot,
                      &oneDeviceData->tdIDTimer,
                      SATA_ID_DEVICE_DATA_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                      tdIDStartTimerCB,
                      smIORequest,
                      oneDeviceData,
                      agNULL
                     );

  tdsaAddTimer(
               tiRoot,
               &Initiator->timerlist,
               &oneDeviceData->tdIDTimer
              );
  TI_DBG1(("tdIDStartTimer: end\n"));
  return;
}

osGLOBAL void
tdIDStartTimerCB(
                  tiRoot_t    * tiRoot,
                  void        * timerData1,
                  void        * timerData2,
                  void        * timerData3
                )
{
  tdsaRoot_t         *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t      *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  smIORequest_t      *smIORequest;
  tdsaDeviceData_t   *oneDeviceData;
  smRoot_t           *smRoot;
  tdIORequestBody_t  *tdIORequestBody;
  smDeviceHandle_t   *smDeviceHandle;
  tdsaPortContext_t  *onePortContext;
#ifdef REMOVED
  agsaRoot_t         *agRoot;
  bit32               IDstatus;
//#endif
//#ifdef REMOVED
  agsaIORequest_t    *agAbortIORequest = agNULL;
  tdIORequestBody_t  *tdAbortIORequestBody = agNULL;
  bit32               PhysUpper32;
  bit32               PhysLower32;
  bit32               memAllocStatus;
  void               *osMemHandle;
#endif // REMOVED
#ifdef  TD_DEBUG_ENABLE
  bit32               status = AGSA_RC_FAILURE;
#endif

  TI_DBG1(("tdIDStartTimerCB start\n"));
  smIORequest = (smIORequest_t *)timerData1;
  oneDeviceData = (tdsaDeviceData_t *)timerData2;
  smRoot = &(tdsaAllShared->smRoot);
#ifdef REMOVED
  agRoot = oneDeviceData->agRoot;
#endif // REMOVED

  if (smIORequest == agNULL)
  {
    TI_DBG1(("tdIDStartTimerCB: smIORequest == agNULL !!!!!!\n"));
    return;
  }

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tdIDStartTimerCB: oneDeviceData == agNULL !!!!!!\n"));
    return;
  }

  if (oneDeviceData->satDevData.IDPending == agFALSE || oneDeviceData->satDevData.IDDeviceValid == agTRUE)
  {
     /*the Identify Device command already normally completed, just return*/
     return;
  }

  tdIORequestBody = (tdIORequestBody_t *)smIORequest->tdData;
  smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
  onePortContext = oneDeviceData->tdPortContext;
  if (tdIORequestBody == agNULL)
  {
    TI_DBG1(("tdIDStartTimerCB: tdIORequestBody == agNULL !!!!!!\n"));
    return;
  }

  if (smDeviceHandle == agNULL)
  {
    TI_DBG1(("tdIDStartTimerCB: smDeviceHandle == agNULL !!!!!!\n"));
    return;
  }

  if (onePortContext == agNULL)
  {
    TI_DBG1(("tdIDStartTimerCB: onePortContext == agNULL !!!!!!\n"));
    return;
  }

  TI_DBG1(("tdIDStartTimerCB: did %d\n", oneDeviceData->id));
  /*
   1. smIOabort()
   2. in tdsmIDCompletedCB(), retry
  */
  if (oneDeviceData->valid == agFALSE)
  {
    TI_DBG1(("tdIDStartTimerCB: invalid device\n"));
    return;
  }
#ifdef  TD_DEBUG_ENABLE
  status = smIOAbort( smRoot, smIORequest );
#else
  smIOAbort( smRoot, smIORequest );
#endif

#ifdef REMOVED
  /* allocating agIORequest for abort itself */
  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdAbortIORequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdIORequestBody_t),
                                   agTRUE
                                   );
  if (memAllocStatus != tiSuccess)
  {
    /* let os process IO */
    TI_DBG1(("tdIDStartTimerCB: ostiAllocMemory failed...; can't retry ID data \n"));
    return;
  }
  if (tdAbortIORequestBody == agNULL)
  {
    /* let os process IO */
    TI_DBG1(("tdIDStartTimerCB: ostiAllocMemory returned NULL tdAbortIORequestBody; can't retry ID data\n"));
    return;
  }
  /* setup task management structure */
  tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
  /* setting callback but not used later */
  tdAbortIORequestBody->IOCompletionFunc = agNULL;
  //tdAbortIORequestBody->IOCompletionFunc = itdssIOAbortedHandler;
  tdAbortIORequestBody->tiDevHandle = (tiDeviceHandle_t *)&(oneDeviceData->tiDeviceHandle);
  /* initialize agIORequest */
  agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
  agAbortIORequest->osData = (void *) tdAbortIORequestBody;
  agAbortIORequest->sdkData = agNULL; /* LL takes care of this */
//#endif
//#ifdef REMOVED
  status = saSATAAbort(agRoot,
                       agAbortIORequest,
                       0,
                       oneDeviceData->agDevHandle,
                       1, /* abort all */
                       agNULL,
                       ossaSATAIDAbortCB
                       );
  status = saSATAAbort(agRoot,
                       agAbortIORequest,
                       0,
                       oneDeviceData->agDevHandle,
                       0, /* abort one */
                       agIORequest,
                       ossaSATAIDAbortCB
                       );
//#endif
//#ifdef REMOVED
  if (status != AGSA_RC_SUCCESS)
  {
    TI_DBG1(("tdIDStartTimerCB: saSATAAbort failed; can't retry ID data\n"));
  }
  if (oneDeviceData->satDevData.IDDeviceValid == agTRUE)
  {
    TI_DBG1(("tdIDStartTimerCB: IDDeviceValid is valid, no need to retry\n"));
    return;
  }
  if (tdIORequestBody->reTries <= SM_RETRIES)
  {
    tdIORequestBody->tiIORequest = agNULL; /* not in use */
    tdIORequestBody->pid = onePortContext->id;
    smIORequest->tdData = tdIORequestBody;
    smIORequest->smData = &tdIORequestBody->smIORequestBody;
    smDeviceHandle->tdData = oneDeviceData;
    IDstatus = smIDStart(smRoot, smIORequest, smDeviceHandle );
    if (IDstatus == SM_RC_SUCCESS)
    {
      TI_DBG1(("tdIDStartTimerCB: being retried!!!\n"));
      tdIORequestBody->reTries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      tdIDStartTimer(tiRoot, smIORequest, oneDeviceData);
    }
    else
    {
      /* identify device data is not valid */
      TI_DBG1(("tdIDStartTimerCB: smIDStart fail or busy %d!!!\n", IDstatus));
      tdIORequestBody->reTries = 0;
      tdIORequestBody->ioCompleted = agTRUE;
      tdIORequestBody->ioStarted = agFALSE;
      ostiFreeMemory( tiRoot,
                      tdIORequestBody->osMemHandle,
                      sizeof(tdIORequestBody_t)
                    );
      oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
      return;
    }
  }
  else
  {
    /* give up */
    TI_DBG1(("tdIDStartTimerCB: retries are over!!!\n"));
    if (oneDeviceData->tdIDTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer( tiRoot, &oneDeviceData->tdIDTimer );
    }
    tdIORequestBody->reTries = 0;
    tdIORequestBody->ioCompleted = agTRUE;
    tdIORequestBody->ioStarted = agFALSE;
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->osMemHandle,
                   sizeof(tdIORequestBody_t)
            );
    oneDeviceData->satDevData.IDDeviceValid = agFALSE;

    if (oneDeviceData->SMNumOfID <= 0) /* does SMP HARD RESET only upto one time */
    {
      TI_DBG1(("tdIDStartTimerCB: fail; sending HARD_RESET\n"));
      oneDeviceData->SMNumOfID++;
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        saLocalPhyControl(agRoot, agNULL, 0, oneDeviceData->phyID, AGSA_PHY_HARD_RESET, agNULL);
      }
      else
      {
        tdsaPhyControlSend(tiRoot,
                           oneDeviceData,
                           SMP_PHY_CONTROL_HARD_RESET,
                           agNULL);
      }
    }
    else
    {
      /* given up after one time of SMP HARD RESET; */
      TI_DBG1(("tdIDStartTimerCB: fail; but giving up sending HARD_RESET!!!\n"));
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        smReportRemovalDirect(tiRoot, agRoot, oneDeviceData);
      }
      else
      {
        smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
      }
    }
  }
#endif // REMOVED

  TI_DBG1(("tdIDStartTimerCB: end, smIOAbort status %d\n", status));
  return;
}
#endif // FDS_SM


#if defined(FDS_DM) && defined(FDS_SM)
//start here
GLOBAL void
tdIDStart(
           tiRoot_t             *tiRoot,
           agsaRoot_t           *agRoot,
           smRoot_t             *smRoot,
           tdsaDeviceData_t     *oneDeviceData,
           tdsaPortContext_t    *onePortContext
          )
{
  tdsaRoot_t           *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32                SMstatus = SM_RC_FAILURE;
  tdIORequestBody_t    *tdIORequestBody;
  smIORequest_t        *smIORequest;
  smDeviceHandle_t     *smDeviceHandle;
  bit32                PhysUpper32;
  bit32                PhysLower32;
  bit32                memAllocStatus;
  void                 *osMemHandle;


  TI_DBG1(("tdIDStart: start, did %d\n",oneDeviceData->id));
 
  if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData)|| DEVICE_IS_STP_TARGET(oneDeviceData))
      &&
      oneDeviceData->satDevData.IDDeviceValid == agFALSE
      &&
      oneDeviceData->satDevData.IDPending == agFALSE
      )
  {
    TI_DBG2(("tdIDStart: in loop, did %d\n", oneDeviceData->id));
    /* allocating tdIORequestBody */
    memAllocStatus = ostiAllocMemory(
                                      tiRoot,
                                      &osMemHandle,
                                      (void **)&tdIORequestBody,
                                      &PhysUpper32,
                                      &PhysLower32,
                                      8,
                                      sizeof(tdIORequestBody_t),
                                      agTRUE
                                    );
    if (memAllocStatus != tiSuccess || tdIORequestBody == agNULL)
    {
      /* let os process IO */
      TI_DBG1(("tdIDStart: ostiAllocMemory failed... or ostiAllocMemory returned NULL tdIORequestBody!!!\n"));
      oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        /* notifying link up */
        ostiPortEvent(
                       tiRoot,
                       tiPortLinkUp,
                       tiSuccess,
                       (void *)onePortContext->tiPortalContext
                     );
#ifdef INITIATOR_DRIVER
        /* triggers discovery */
        ostiPortEvent(
                       tiRoot,
                       tiPortDiscoveryReady,
                       tiSuccess,
                       (void *) onePortContext->tiPortalContext
                     );
#endif
      }
    }
    else
    {
      /* initialize */
      osti_memset(tdIORequestBody, 0, sizeof(tdIORequestBody_t));

      tdIORequestBody->osMemHandle = osMemHandle;
      TI_DBG2(("tdIDStart: tdIORequestBody %p  tdIORequestBody->osMemHandle %p\n", tdIORequestBody, tdIORequestBody->osMemHandle));

      /* not in use */
      tdIORequestBody->IOCompletionFunc = agNULL;
      tdIORequestBody->tiDevHandle = agNULL;

      tdIORequestBody->tiIORequest = agNULL; /* not in use */
      tdIORequestBody->pid = onePortContext->id;
      tdIORequestBody->reTries = 0;
      smIORequest = (smIORequest_t *)&(tdIORequestBody->smIORequest);
      smIORequest->tdData = tdIORequestBody;
      smIORequest->smData = &tdIORequestBody->smIORequestBody;

      smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
      smDeviceHandle->tdData = oneDeviceData;

      TI_DBG2(("tdIDStart: smIORequest %p\n", smIORequest));

      SMstatus = smIDStart(smRoot,
                           smIORequest,
                           &(oneDeviceData->smDeviceHandle)
                           );

      if (SMstatus == SM_RC_SUCCESS)
      {
        if (oneDeviceData->directlyAttached == agTRUE)
        {
          TI_DBG2(("tdIDStart: successfully sent identify device data\n"));

          /* Add the devicedata to the mainlink */
          tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
          TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(tdsaAllShared->MainDeviceList));
          tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
          TI_DBG6(("tdIDStart: one case did %d \n", oneDeviceData->id));
        }
        oneDeviceData->satDevData.IDPending = agTRUE;
        /* start a timer */
        tdIDStartTimer(tiRoot, smIORequest, oneDeviceData);
      }
      else
      {
        /* failed to send  */
        TI_DBG1(("tdIDStart: smIDStart fail or busy %d\n", SMstatus));

        /* free up allocated memory */
        ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );

        oneDeviceData->satDevData.IDDeviceValid = agFALSE;
        if (oneDeviceData->directlyAttached == agTRUE)
        {
          TI_DBG1(("tdIDStart: failed in sending identify device data\n"));
          /* put onedevicedata back to free list */
          tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
          TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
          tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
          /* notifying link up */
          ostiPortEvent(
                         tiRoot,
                         tiPortLinkUp,
                         tiSuccess,
                         (void *)onePortContext->tiPortalContext
                       );
#ifdef INITIATOR_DRIVER
          /* triggers discovery */
          ostiPortEvent(
                         tiRoot,
                         tiPortDiscoveryReady,
                         tiSuccess,
                         (void *) onePortContext->tiPortalContext
                       );
#endif
        }
        else
        {
          smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
        }
      }
    }
  }
  TI_DBG1(("tdIDStart: exit\n"));
  return;
}

#endif

#ifdef SALLSDK_OS_IOMB_LOG_ENABLE
GLOBAL void ossaLogIomb(agsaRoot_t  *agRoot,
                        bit32        queueNum,
                        agBOOLEAN      isInbound,
                        void        *pMsg,
                        bit32        msgLength)
{
  return;
}
#endif /* SALLSDK_OS_IOMB_LOG_ENABLE */

#ifndef SATA_ENABLE
/*
 * These callback routines are defined in ossasat.c which are included in the
 * compilation if SATA_ENABLED is defined.
 */

/*****************************************************************************
*! \brief ossaDiscoverSataCB
*
*  Purpose:  This function is called by lower layer to inform TD layer of
*            STP/SATA discovery results
*
*
*  \param   agRoot         Pointer to chip/driver Instance.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   event          event type
*  \param   pParm1         Pointer to data associated with event
*  \param   pParm2         Pointer to data associated with event
*
*  \return: none
*
*  \note -  For details, refer to SAS/SATA Low-Level API Specification
*
*****************************************************************************/

osGLOBAL void ossaDiscoverSataCB( agsaRoot_t        *agRoot,
                    agsaPortContext_t *agPortContext,
                    bit32             event,
                    void              *pParm1,
                    void              *pParm2
                    )
{
  return;
}


/*****************************************************************************
*! \brief  ossaSATACompleted
*
*   This routine is called to complete a SATA request previously issued to the
*    LL Layer in saSATAStart()
*
*  \param   agRoot:      Handles for this instance of SAS/SATA hardware
*  \param   agIORequest: Pointer to the LL I/O request context for this I/O.
*  \param   agIOStatus:  Status of completed I/O.
*  \param   agFirstDword:Pointer to the four bytes of FIS.
*  \param   agIOInfoLen: Length in bytes of overrun/underrun residual or FIS
*                        length.
*  \param   agParam:     Additional info based on status.
*
*  \return: none
*
*****************************************************************************/
GLOBAL void ossaSATACompleted(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  void              *agFirstDword,
                  bit32             agIOInfoLen,
                  void              *agParam
                  )
{
  return;
}


/*****************************************************************************
*! \brief  ossaSATAEvent
*
*   This routine is called to notify the OS Layer of an event associated with
*   SATA port or SATA device
*
*  \param   agRoot:        Handles for this instance of SAS/SATA hardware
*  \param   agIORequest:   Pointer to the LL I/O request context for this I/O.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   agDevHandle:   Pointer to a device handle
*  \param   event:         event type
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void ossaSATAEvent(
                        agsaRoot_t        *agRoot,
                        agsaIORequest_t   *agIORequest,
                        agsaPortContext_t *agPortContext,
                        agsaDevHandle_t   *agDevHandle,
                        bit32             event,
                        bit32             agIOInfoLen,
                        void              *agParam
                        )
{
  return;
}


/*****************************************************************************
*! \brief  ossaSATADeviceResetCB
*
*   This routine is called to complete a SATA device reset request previously
*   issued to the LL Layer in saSATADeviceReset().
*
*  \param agRoot:      Handles for this instance of SAS/SATA hardware
*  \param agDevHandle: Pointer to a device handle
*  \param resetStatus: Reset status:
*                      OSSA_SUCCESS: The reset operation completed successfully.
*                      OSSA_FAILURE: The reset operation failed.
*  \param resetparm:  Pointer to the Device-To-Host FIS received from the device.
*
*  \return: none
*
*****************************************************************************/
osGLOBAL void ossaSATADeviceResetCB(
                      agsaRoot_t        *agRoot,
                      agsaDevHandle_t   *agDevHandle,
                      bit32             resetStatus,
                      void              *resetparm)
{

  return;

}

/*****************************************************************************
*! \brief ossaDiscoverSasCB
*
*  Purpose:  This function is called by lower layer to inform TD layer of
*            SAS discovery results
*
*
*  \param   agRoot         Pointer to chip/driver Instance.
*  \param   agPortContext  Pointer to the port context of TD and Lower layer
*  \param   event          event type
*  \param   pParm1         Pointer to data associated with event
*  \param   pParm2         Pointer to data associated with event
*
*  \return: none
*
*  \note -  For details, refer to SAS/SATA Low-Level API Specification
*
*****************************************************************************/
osGLOBAL void ossaDiscoverSasCB(agsaRoot_t        *agRoot,
                  agsaPortContext_t *agPortContext,
                  bit32             event,
                  void              *pParm1,
                  void              *pParm2
                  )
{
  return;
}
#endif

