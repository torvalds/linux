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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
/* for TIDEBUG_MSG */
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>

#ifdef FDS_DM

#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#endif

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>

#if defined(DM_DEBUG)
extern bit32 gDMDebugLevel;
#endif

osGLOBAL bit32
tddmRotateQnumber(
                  dmRoot_t          *dmRoot,
                  agsaDevHandle_t   *agDevHandle
                 )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  tdsaDeviceData_t     *oneDeviceData = agNULL;
  TI_DBG1(("tddmRotateQnumber: start\n"));
  if (agDevHandle == agNULL)
  {
    TI_DBG1(("tddmRotateQnumber: agDevHandle is NULL!!!\n"));
    return 0;
  }
  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;
  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tddmRotateQnumber: oneDeviceData is NULL!!!\n"));
    return 0;
  }
  tdsaRoot = (tdsaRoot_t *)dmRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tddmRotateQnumber: tdsaRoot is NULL\n"));
    return 0;
  }
  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tddmRotateQnumber: tdsaAllShared is NULL\n"));
    return 0;
  }
  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tddmRotateQnumber: tiRoot is NULL\n"));
    return 0;
  }
  return tdsaRotateQnumber(tiRoot, oneDeviceData);
}
osGLOBAL bit32
tdsaFindLocalMCN(
                 tiRoot_t                   *tiRoot,
                 tdsaPortContext_t          *onePortContext
                )
{
  bit32              i, localMCN = 0;

  TI_DBG2(("tdsaFindLocalMCN: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaFindLocalMCN: invalid portcontext id %d\n", onePortContext->id));
    return 0;
  }

  for(i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    if (onePortContext->PhyIDList[i] == agTRUE)
    {
      localMCN++;
    }
  }

  return localMCN;
}


/*
 on success,
           ostiInitiatorEvent(
                             tiRoot,
                             onePortContext->tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDiscovery,
                             tiDiscOK,
                             agNULL
                             );
else
        remove(de-register) all devices
        ostiInitiatorEvent(
                           tiRoot,
                           onePortContext->tiPortalContext,
                           agNULL,
                           tiIntrEventTypeDiscovery,
                           tiDiscFailed,
                           agNULL
                           );


  dmRoot->tdData is tdsaRoot_t (just like current TD layer)
  dmPortContext->tdData is tdsaPortContext_t

*/
osGLOBAL void
tddmDiscoverCB(
               dmRoot_t        *dmRoot,
               dmPortContext_t *dmPortContext,
               bit32           eventStatus
              )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  tdsaPortContext_t  *onePortContext;
  agsaRoot_t         *agRoot;
  agsaPortContext_t  *agPortContext;

  TI_DBG1(("tddmDiscoverCB: start\n"));
  tdsaRoot = (tdsaRoot_t *)dmRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tddmDiscoverCB: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tddmDiscoverCB: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tddmDiscoverCB: tiRoot is NULL\n"));
    return;
  }

  onePortContext = (tdsaPortContext_t *)dmPortContext->tdData;
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tddmDiscoverCB: onePortContext is NULL\n"));
    return;
  }

  TI_DBG2(("tddmDiscoverCB: localMCN 0x%x\n", tdsaFindLocalMCN(tiRoot, onePortContext)));

  if (eventStatus == dmDiscCompleted)
  {
    TI_DBG1(("tddmDiscoverCB: dmDiscCompleted\n"));
    onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
    onePortContext->DMDiscoveryState = dmDiscCompleted;
    TI_DBG1(("tddmDiscoverCB: pid %d tiPortalContext %p\n", onePortContext->id, onePortContext->tiPortalContext));

    /* update onePortContext->UpdateMCN = agFALSE */
    if ( onePortContext->UpdateMCN == agTRUE)
    {
      TI_DBG2(("tddmDiscoverCB: calling tdsaUpdateMCN\n"));
      onePortContext->UpdateMCN = agFALSE;
      tdsaUpdateMCN(dmRoot, onePortContext);
    }

    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscOK,
                       agNULL
                      );
  }
  else if (eventStatus == dmDiscFailed )
  {
    TI_DBG1(("tddmDiscoverCB: dmDiscFailed \n"));
    onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
    onePortContext->DMDiscoveryState = dmDiscFailed;
    TI_DBG1(("tddmDiscoverCB: pid %d tiPortalContext %p\n", onePortContext->id, onePortContext->tiPortalContext));
    agRoot = &(tdsaAllShared->agRootNonInt);
    if (agRoot == agNULL)
    {
      TI_DBG1(("tddmDiscoverCB: agRoot is NULL\n"));
      return;
    }
    agPortContext = onePortContext->agPortContext;
    if (agPortContext == agNULL)
    {
      TI_DBG1(("tddmDiscoverCB: agPortContext is NULL\n"));
      return;
    }
    /*
      invalidate all devices in this port
    */
    tddmInvalidateDevicesInPort(tiRoot, onePortContext);

    saPortControl(agRoot,
                  agNULL,
                  0,
                  agPortContext,
                  AGSA_PORT_IO_ABORT,
                  0 /*quarantine */,
                  0 /* unused */);


    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                      );
  }
  else if (eventStatus == dmDiscAborted )
  {
    TI_DBG1(("tddmDiscoverCB: dmDiscAborted \n"));
    onePortContext->DMDiscoveryState = dmDiscAborted;
  }
  else if (eventStatus == dmDiscAbortFailed  )
  {
     TI_DBG1(("tddmDiscoverCB: dmDiscAbortFailed  \n"));
     onePortContext->DMDiscoveryState = dmDiscAbortFailed;
  }
  else if (eventStatus == dmDiscAbortInvalid  )
  {
     TI_DBG1(("tddmDiscoverCB: dmDiscAbortInvalid  \n"));
     onePortContext->DMDiscoveryState = dmDiscAbortInvalid;
  }
  else if (eventStatus == dmDiscAbortInProgress  )
  {
     TI_DBG1(("tddmDiscoverCB: dmDiscAbortInProgress  \n"));
     onePortContext->DMDiscoveryState = dmDiscAbortInProgress;
  }
  else
  {
    TI_DBG1(("tddmDiscoverCB: undefined eventStatus 0x%x\n", eventStatus));
    onePortContext->DMDiscoveryState = dmDiscFailed;
  }

  return;
}


osGLOBAL void
tddmQueryDiscoveryCB(
                     dmRoot_t        *dmRoot,
                     dmPortContext_t *dmPortContext,
                     bit32           discType,
                     bit32           discState
                    )
{
  tdsaPortContext_t  *onePortContext = agNULL;

  TI_DBG2(("tddmQueryDiscoveryCB: start\n"));
  onePortContext = (tdsaPortContext_t *)dmPortContext->tdData;
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tddmQueryDiscoveryCB: onePortContext is NULL\n"));
    return;
  }
  TI_DBG2(("tddmQueryDiscoveryCB: discType %d discState %d\n", discType, discState));

  onePortContext->DMDiscoveryState = discState;
  return;
}

osGLOBAL void
tddmInvalidateDevicesInPort(
                tiRoot_t             *tiRoot,
                tdsaPortContext_t    *onePortContext
               )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;

  TI_DBG1(("tddmInvalidateDevicesInPort: start\n"));

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmInvalidateDevicesInPort: oneDeviceData is NULL!!!\n"));
      return;
    }
    if ((oneDeviceData->registered == agTRUE) &&
        (oneDeviceData->tdPortContext == onePortContext)
        )
    {

      TI_DBG3(("tddmInvalidateDevicesInPort: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      if (oneDeviceData->SASAddressID.sasAddressHi == onePortContext->sasRemoteAddressHi &&
          oneDeviceData->SASAddressID.sasAddressLo == onePortContext->sasRemoteAddressLo
         )
      {
        TI_DBG1(("tddmInvalidateDevicesInPort: keeping\n"));
        oneDeviceData->valid = agTRUE;
        oneDeviceData->valid2 = agFALSE;
      }
      else if (oneDeviceData->valid == agTRUE)
      {
        oneDeviceData->valid = agFALSE;
        oneDeviceData->valid2 = agFALSE;
        oneDeviceData->registered = agFALSE;
      }
     }
    DeviceListList = DeviceListList->flink;
  }

  TI_DBG3(("tddmInvalidateDevicesInPort: end\n"));

  return;
}

osGLOBAL bit32
tddmNewSASorNot(
                tiRoot_t             *tiRoot,
                tdsaPortContext_t    *onePortContext,
                tdsaSASSubID_t       *agSASSubID
               )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  bit32             ret = agTRUE;

  TI_DBG3(("tddmNewSASorNot: start\n"));

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmNewSASorNot: oneDeviceData is NULL!!!\n"));
      return agFALSE;
    }
    if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
        (oneDeviceData->registered == agTRUE) &&
        (oneDeviceData->tdPortContext == onePortContext)
        )
    {
      TI_DBG3(("tddmNewSASorNot: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      ret = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }



  TI_DBG3(("tddmNewSASorNot: end\n"));

  return ret;
}

osGLOBAL tdsaDeviceData_t *
tddmPortSASDeviceFind(
                      tiRoot_t           *tiRoot,
                      tdsaPortContext_t  *onePortContext,
                      bit32              sasAddrLo,
                      bit32              sasAddrHi
                      )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData, *RetDeviceData=agNULL;
  tdList_t          *DeviceListList;

  TI_DBG2(("tddmPortSASDeviceFind: start\n"));

  TD_ASSERT((agNULL != tiRoot), "");
  TD_ASSERT((agNULL != onePortContext), "");

  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmPortSASDeviceFind: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }
    if ((oneDeviceData->SASAddressID.sasAddressHi == sasAddrHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == sasAddrLo) &&
        (oneDeviceData->valid == agTRUE) &&
        (oneDeviceData->tdPortContext == onePortContext)
      )
    {
      TI_DBG2(("tddmPortSASDeviceFind: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      TI_DBG2(("tddmPortSASDeviceFind: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG2(("tddmPortSASDeviceFind: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
      RetDeviceData = oneDeviceData;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);

  return RetDeviceData;
}

/* not in use yet */
osGLOBAL tdsaDeviceData_t *
tddmAddToSharedcontext(
                       agsaRoot_t           *agRoot,
                       tdsaPortContext_t    *onePortContext,
                       tdsaSASSubID_t       *agSASSubID,
                       tdsaDeviceData_t     *oneExpDeviceData,
                       bit8                 phyID
                      )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             new_device = agTRUE;

  TI_DBG1(("tddmAddToSharedcontext: start\n"));

  TI_DBG1(("tddmAddToSharedcontext: oneportContext ID %d\n", onePortContext->id));
  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmAddToSharedcontext: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }
    if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
        (oneDeviceData->tdPortContext == onePortContext)
        )
    {
      TI_DBG1(("tddmAddToSharedcontext: pid %dtddmAddToSharedcontext did %d\n", onePortContext->id, oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  /* new device */
  if (new_device == agTRUE)
  {
    TI_DBG1(("tddmAddToSharedcontext: new device\n"));
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    if (!TDLIST_NOT_EMPTY(&(tdsaAllShared->FreeDeviceList)))
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      TI_DBG1(("tddmAddToSharedcontext: empty DeviceData FreeLink\n"));
      return agNULL;
    }

    TDLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, FreeLink, DeviceListList);

    TI_DBG1(("tddmAddToSharedcontext: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));

    onePortContext->Count++;
    oneDeviceData->agRoot = agRoot;
    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = agSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = agSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = agSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = agSASSubID->target_ssp_stp_smp;
    oneDeviceData->tdPortContext = onePortContext;
    /* handles both SAS target and STP-target, SATA-device */
    if (!DEVICE_IS_SATA_DEVICE(oneDeviceData) && !DEVICE_IS_STP_TARGET(oneDeviceData))
    {
      oneDeviceData->DeviceType = TD_SAS_DEVICE;
    }
    else
    {
      oneDeviceData->DeviceType = TD_SATA_DEVICE;
    }

    oneDeviceData->ExpDevice = oneExpDeviceData;
    /* set phyID only when it has initial value of 0xFF */
    if (oneDeviceData->phyID == 0xFF)
    {
      oneDeviceData->phyID = phyID;
    }

    oneDeviceData->valid = agTRUE;

    /* add the devicedata to the portcontext */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(tdsaAllShared->MainDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG1(("tddmAddToSharedcontext: one case pid %d did %d \n", onePortContext->id, oneDeviceData->id));
    TI_DBG1(("tddmAddToSharedcontext: new case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));
  }
  else /* old device */
  {
    TI_DBG1(("tddmAddToSharedcontext: old device\n"));
    TI_DBG1(("tddmAddToSharedcontext: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));

    oneDeviceData->agRoot = agRoot;
    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = agSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = agSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = agSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = agSASSubID->target_ssp_stp_smp;
    oneDeviceData->tdPortContext = onePortContext;
    /* handles both SAS target and STP-target, SATA-device */
    if (!DEVICE_IS_SATA_DEVICE(oneDeviceData) && !DEVICE_IS_STP_TARGET(oneDeviceData))
    {
      oneDeviceData->DeviceType = TD_SAS_DEVICE;
    }
    else
    {
      oneDeviceData->DeviceType = TD_SATA_DEVICE;
    }

    oneDeviceData->ExpDevice = oneExpDeviceData;
    /* set phyID only when it has initial value of 0xFF */
    if (oneDeviceData->phyID == 0xFF)
    {
      oneDeviceData->phyID = phyID;
    }

    oneDeviceData->valid = agTRUE;
    TI_DBG1(("tddmAddToSharedcontext: old case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));

  }
  return oneDeviceData;

}


/*
  calls saRegisterNewDevice()
  in ossaDeviceRegistrationCB(), if an expander, register to DM
#define DEVICE_IS_SMP_TARGET(DeviceData) \
  (((DeviceData)->target_ssp_stp_smp & DEVICE_SMP_BIT) == DEVICE_SMP_BIT)
*/
osGLOBAL tdsaDeviceData_t *
tddmPortDeviceAdd(
                     tiRoot_t            *tiRoot,
                     tdsaPortContext_t   *onePortContext,
                     dmDeviceInfo_t      *dmDeviceInfo,
                     tdsaDeviceData_t    *oneExpDeviceData
      )
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdsaSASSubID_t    agSASSubID;
  bit8              phyID;

  TI_DBG2(("tddmPortDeviceAdd: start\n"));


  agSASSubID.sasAddressHi = TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
  agSASSubID.sasAddressLo = TD_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
  agSASSubID.initiator_ssp_stp_smp = dmDeviceInfo->initiator_ssp_stp_smp;
  agSASSubID.target_ssp_stp_smp = dmDeviceInfo->target_ssp_stp_smp;
  phyID = (dmDeviceInfo->ext) & 0xFF;

  /* old device and already registered to LL; added by link-up event */
  if ( agFALSE == tdssNewSASorNot(
                                   onePortContext->agRoot,
                                   onePortContext,
                                   &agSASSubID
                                   )
       )
  {
    /* old device and already registered to LL; added by link-up event */
    TI_DBG2(("tddmPortDeviceAdd: OLD qqqq initiator_ssp_stp_smp %d target_ssp_stp_smp %d\n", agSASSubID.initiator_ssp_stp_smp, agSASSubID.target_ssp_stp_smp));
    /* find the old device */
    oneDeviceData = tdssNewAddSASToSharedcontext(
                                                 onePortContext->agRoot,
                                                 onePortContext,
                                                 &agSASSubID,
                                                 oneExpDeviceData,
                                                 phyID
                                                 );

    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmPortDeviceAdd: no more device!!! oneDeviceData is null\n"));
    }

    /* If a device is allocated */
    if ( oneDeviceData != agNULL )
    {

      TI_DBG2(("tddmPortDeviceAdd: sasAddressHi 0x%08x\n", agSASSubID.sasAddressHi));
      TI_DBG2(("tddmPortDeviceAdd: sasAddressLo 0x%08x\n", agSASSubID.sasAddressLo));
      TI_DBG2(("tddmPortDeviceAdd: phyID 0x%x\n", phyID));

      /* copy dmDeviceInfo to oneDeviceData->agDeviceInfo except ext field */
      oneDeviceData->agDeviceInfo.smpTimeout = dmDeviceInfo->smpTimeout;
      oneDeviceData->agDeviceInfo.it_NexusTimeout = dmDeviceInfo->it_NexusTimeout;
      oneDeviceData->agDeviceInfo.firstBurstSize = dmDeviceInfo->firstBurstSize;
      oneDeviceData->agDeviceInfo.devType_S_Rate = dmDeviceInfo->devType_S_Rate;
      osti_memcpy(&(oneDeviceData->agDeviceInfo.sasAddressHi), &(dmDeviceInfo->sasAddressHi), 4);
      osti_memcpy(&(oneDeviceData->agDeviceInfo.sasAddressLo), &(dmDeviceInfo->sasAddressLo), 4);
      if (dmDeviceInfo->sataDeviceType == SATA_ATAPI_DEVICE)
      {
          oneDeviceData->agDeviceInfo.flag |= ATAPI_DEVICE_FLAG;
      }

      oneDeviceData->satDevData.satDeviceType = dmDeviceInfo->sataDeviceType;



      oneDeviceData->agContext.osData = oneDeviceData;
      oneDeviceData->agContext.sdkData = agNULL;

    }
    return oneDeviceData;
  } /* old device */

  /* new device */

  TI_DBG2(("tddmPortDeviceAdd: NEW qqqq initiator_ssp_stp_smp %d target_ssp_stp_smp %d\n", agSASSubID.initiator_ssp_stp_smp, agSASSubID.target_ssp_stp_smp));

  /* allocate a new device and set the valid bit */
  oneDeviceData = tdssNewAddSASToSharedcontext(
                                               onePortContext->agRoot,
                                               onePortContext,
                                               &agSASSubID,
                                               oneExpDeviceData,
                                               phyID
                                               );

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tddmPortDeviceAdd: no more device!!! oneDeviceData is null\n"));
  }

   /* If a device is allocated */
  if ( oneDeviceData != agNULL )
  {

    TI_DBG2(("tddmPortDeviceAdd: sasAddressHi 0x%08x\n", agSASSubID.sasAddressHi));
    TI_DBG2(("tddmPortDeviceAdd: sasAddressLo 0x%08x\n", agSASSubID.sasAddressLo));
    TI_DBG2(("tddmPortDeviceAdd: phyID 0x%x\n", phyID));

    /* copy dmDeviceInfo to oneDeviceData->agDeviceInfo except ext field */
    oneDeviceData->agDeviceInfo.smpTimeout = dmDeviceInfo->smpTimeout;
    oneDeviceData->agDeviceInfo.it_NexusTimeout = dmDeviceInfo->it_NexusTimeout;
    oneDeviceData->agDeviceInfo.firstBurstSize = dmDeviceInfo->firstBurstSize;
    oneDeviceData->agDeviceInfo.devType_S_Rate = dmDeviceInfo->devType_S_Rate;
    osti_memcpy(&(oneDeviceData->agDeviceInfo.sasAddressHi), &(dmDeviceInfo->sasAddressHi), 4);
    osti_memcpy(&(oneDeviceData->agDeviceInfo.sasAddressLo), &(dmDeviceInfo->sasAddressLo), 4);

    oneDeviceData->satDevData.satDeviceType = dmDeviceInfo->sataDeviceType;
    if (dmDeviceInfo->sataDeviceType == SATA_ATAPI_DEVICE)
    {
        oneDeviceData->agDeviceInfo.flag |= ATAPI_DEVICE_FLAG;
    }

    oneDeviceData->agContext.osData = oneDeviceData;
    oneDeviceData->agContext.sdkData = agNULL;

    TI_DBG2(("tddmPortDeviceAdd: did %d\n", oneDeviceData->id));

    /* don't add and register initiator for T2D */
    if ( (((oneDeviceData->initiator_ssp_stp_smp & DEVICE_SSP_BIT) == DEVICE_SSP_BIT) &&
         ((oneDeviceData->target_ssp_stp_smp & DEVICE_SSP_BIT) != DEVICE_SSP_BIT))
        ||
         (((oneDeviceData->initiator_ssp_stp_smp & DEVICE_STP_BIT) == DEVICE_STP_BIT) &&
         ((oneDeviceData->target_ssp_stp_smp & DEVICE_SSP_BIT) != DEVICE_SSP_BIT))
       )
    {
      TI_DBG1(("tddmPortDeviceAdd: initiator. no add and registration\n"));
      TI_DBG1(("tddmPortDeviceAdd: sasAddressHi 0x%08x\n", agSASSubID.sasAddressHi));
      TI_DBG1(("tddmPortDeviceAdd: sasAddressLo 0x%08x\n", agSASSubID.sasAddressLo));

    }
    else
    {
      if (oneDeviceData->registered == agFALSE)
      {
#ifdef REMOVED
        //temp; setting MCN to tdsaAllShared->MCN
        oneDeviceData->agDeviceInfo.flag = oneDeviceData->agDeviceInfo.flag | (tdsaAllShared->MCN << 16);
        //end temp
#endif
        if( tdsaAllShared->sflag )
        {
          if( ! DEVICE_IS_SMP_TARGET(oneDeviceData))
          {
            TI_DBG1(("tddmPortDeviceAdd: saRegisterNewDevice sflag %d\n", tdsaAllShared->sflag));
            oneDeviceData->agDeviceInfo.flag = oneDeviceData->agDeviceInfo.flag | TD_XFER_RDY_PRIORTY_DEVICE_FLAG;
          }
        }
        saRegisterNewDevice( /* tddmPortDeviceAdd */
                            onePortContext->agRoot,
                            &oneDeviceData->agContext,
                            0,
                            &oneDeviceData->agDeviceInfo,
                            onePortContext->agPortContext,
                            0
                            );
      }
    }
  }

  return oneDeviceData;
}


/*
  each call, add the device to the device list
  typedef struct{
  bit16 smpTimeout;
  bit16 it_NexusTimeout;
  bit16 firstBurstSize;
  bit8  flag;
  bit8  devType_S_Rate;
  bit8  sasAddressHi[4];
  bit8  sasAddressLo[4];
} dmDeviceInfo_t;

 find oneExpDeviceData (expander device data) from dmExpDeviceInfo and
 pass it to tddmPortDeviceAdd()
 start here - change spec from bit32 to void

 phyID = ((dmDeviceInfo->flag) & 0xFC) >> 2;
 Initiators are not registered
*/
//start here
osGLOBAL void
tddmReportDevice(
                 dmRoot_t        *dmRoot,
                 dmPortContext_t *dmPortContext,
                 dmDeviceInfo_t  *dmDeviceInfo, /* device */
                 dmDeviceInfo_t  *dmExpDeviceInfo, /* expander the device is attached to */
     bit32                   flag

                 )
{
  agsaRoot_t         *agRoot;
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  tdsaDeviceData_t   *oneExpDeviceData = agNULL;
  bit32              sasAddressHi, sasAddressLo;
  tdsaPortContext_t  *onePortContext;
  tdsaDeviceData_t   *oneDeviceData = agNULL;
  bit32              localMCN = 0, finalMCN = 0;
  bit32              devMCN = 1;
  bit32              DLR = 0xA;
  bit32              option;
  bit32              param;

#ifdef FDS_SM
  smRoot_t           *smRoot;
#endif

  TI_DBG2(("tddmReportDevice: start\n"));
  tdsaRoot = (tdsaRoot_t *)dmRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tddmReportDevice: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tddmReportDevice: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tddmReportDevice: tiRoot is NULL\n"));
    return;
  }

  onePortContext = (tdsaPortContext_t *)dmPortContext->tdData;
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tddmReportDevice: onePortContext is NULL\n"));
    return;
  }

#ifdef FDS_SM
  smRoot = &(tdsaAllShared->smRoot);
#endif

  TI_DBG2(("tddmReportDevice: device addrHi 0x%08x addrLo 0x%08x\n",
            TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi), TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressLo)));

  if (dmExpDeviceInfo != agNULL)
  {
    TI_DBG2(("tddmReportDevice: attached expander addrHi 0x%08x addrLo 0x%08x\n",
              TD_GET_SAS_ADDRESSHI(dmExpDeviceInfo->sasAddressHi), TD_GET_SAS_ADDRESSLO(dmExpDeviceInfo->sasAddressLo)));
  }
  else
  {
    TI_DBG2(("tddmReportDevice: No attached expander\n"));
  }

  /* initiators only (e.g. SPC or SPCv) are discarded */
  if ( (dmDeviceInfo->target_ssp_stp_smp == 0) &&
       ( DEVICE_IS_SSP_INITIATOR(dmDeviceInfo) || DEVICE_IS_STP_INITIATOR(dmDeviceInfo) || DEVICE_IS_SMP_INITIATOR(dmDeviceInfo))
     )
  {
    TI_DBG3(("tddmReportDevice: Initiators are not added\n"));
    TI_DBG3(("tddmReportDevice: device addrHi 0x%08x addrLo 0x%08x\n",
            TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi), TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressLo)));
    return;
  }

  if (flag == dmDeviceArrival)
  {
    TI_DBG2(("tddmReportDevice: arrival\n"));
    if (dmExpDeviceInfo != agNULL)
    {
      sasAddressHi = TD_GET_SAS_ADDRESSHI(dmExpDeviceInfo->sasAddressHi);
      sasAddressLo = TD_GET_SAS_ADDRESSLO(dmExpDeviceInfo->sasAddressLo);

      oneExpDeviceData = tddmPortSASDeviceFind(tiRoot, onePortContext, sasAddressLo, sasAddressHi);
    }

    tddmPortDeviceAdd(tiRoot, onePortContext, dmDeviceInfo, oneExpDeviceData);

  }
  else if (flag == dmDeviceRemoval)
  {
    TI_DBG2(("tddmReportDevice: removal\n"));
    sasAddressHi = TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
    sasAddressLo = TD_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
    oneDeviceData = tddmPortSASDeviceFind(tiRoot, onePortContext, sasAddressLo, sasAddressHi);
    if (oneDeviceData == agNULL)
    {
      TI_DBG2(("tddmReportDevice: oneDeviceData is NULL!!!\n"));
    }
    else
    {
      /* invalidate device */
      TI_DBG2(("tddmReportDevice: invalidating\n"));
      TI_DBG2(("tddmReportDevice: agDevHandle %p\n", oneDeviceData->agDevHandle));
      if ( oneDeviceData->agDevHandle != agNULL)
      {
        TI_DBG2(("tddmReportDevice: agDevHandle->sdkData %p\n", oneDeviceData->agDevHandle->sdkData));
      }
      else
      {
        TI_DBG2(("tddmReportDevice: agDevHandle->sdkData is NULL\n"));
      }
      oneDeviceData->valid = agFALSE;
//to do; to be tested
      agRoot = oneDeviceData->agRoot;
      if ( (oneDeviceData->registered == agTRUE) &&
           ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData)
           || DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_SMP_TARGET(oneDeviceData) )
         )
      {
        if ( !( DEVICE_IS_SMP_TARGET(oneDeviceData) && oneDeviceData->directlyAttached == agTRUE))
        {
          tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
          oneDeviceData->registered = agFALSE;
        }
        else
        {
          TI_DBG2(("tddmReportDevice: keeping\n"));
          oneDeviceData->registered = agTRUE;
        }
      }
      else if (oneDeviceData->registered == agTRUE)
      {
        if ( oneDeviceData->agDevHandle == agNULL)
        {
          TI_DBG1(("tddmReportDevice: agDevHandle->sdkData is NULL. Error!!! \n"));
        }
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));
        oneDeviceData->registered = agFALSE;
      }
//to do remove
#ifdef FDS_SM_WRONG
      if (DEVICE_IS_SATA_DEVICE(oneDeviceData))
      {
        TI_DBG2(("tddmReportDevice: smDeregisterDevice\n"));
        smDeregisterDevice(smRoot, agNULL, oneDeviceData->agDevHandle, &(oneDeviceData->smDeviceHandle));
        oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      }
#endif
    }
  }
  else if (flag == dmDeviceNoChange)
  {
    TI_DBG2(("tddmReportDevice: no change; do nothing \n"));
#ifdef FDS_SM
    sasAddressHi = TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
    sasAddressLo = TD_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
    oneDeviceData = tddmPortSASDeviceFind(tiRoot, onePortContext, sasAddressLo, sasAddressHi);
    if (oneDeviceData == agNULL)
    {
      TI_DBG2(("tddmReportDevice: oneDeviceData is NULL!!!\n"));
    }
    else
    {
      agRoot = oneDeviceData->agRoot;
      if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
          &&
          oneDeviceData->satDevData.IDDeviceValid == agFALSE)
      {
        tdIDStart(tiRoot, agRoot, smRoot, oneDeviceData, onePortContext);
      }
    }
#endif
  }
  else if (flag == dmDeviceMCNChange)
  {
    TI_DBG2(("tddmReportDevice: dmDeviceMCNChange \n"));
    localMCN = tdsaFindLocalMCN(tiRoot, onePortContext);
    devMCN = DEVINFO_GET_EXT_MCN(dmDeviceInfo);
    TI_DBG2(("tddmReportDevice: devMCN 0x%08x localMCN 0x%08x\n", devMCN, localMCN));

    sasAddressHi = TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
    sasAddressLo = TD_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
    oneDeviceData = tddmPortSASDeviceFind(tiRoot, onePortContext, sasAddressLo, sasAddressHi);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmReportDevice: oneDeviceData is NULL!!!\n"));
    }
    else
    {
      agRoot = oneDeviceData->agRoot;
      oneDeviceData->devMCN = devMCN;
      TI_DBG2(("tddmReportDevice: sasAddrHi 0x%08x sasAddrLo 0x%08x\n", sasAddressHi, sasAddressLo));
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        finalMCN = localMCN;
        TI_DBG2(("tddmReportDevice: directlyAttached, Final MCN 0x%08x\n", finalMCN));
      }
      else
      {
        finalMCN = MIN(devMCN, localMCN);
        TI_DBG2(("tddmReportDevice: Not directlyAttached, Final MCN 0x%08x\n", finalMCN));
      }
      if ( oneDeviceData->registered == agTRUE)
      {
        /* saSetDeviceInfo to change MCN, using finalMCN */
        option = 8; /* setting only MCN 1000b */
        param = finalMCN << 24;
        TI_DBG2(("tddmReportDevice: option 0x%x param 0x%x MCN 0x%x\n", option, param, finalMCN));
        saSetDeviceInfo(agRoot, agNULL, 0, oneDeviceData->agDevHandle, option, param, ossaSetDeviceInfoCB);
      }
      else
      {
        TI_DBG1(("tddmReportDevice: oneDeviceData is not yet registered !!!\n"));
      }
      oneDeviceData->finalMCN = finalMCN;
    }
  }
  else if (flag == dmDeviceRateChange)
  {
    TI_DBG1(("tddmReportDevice: dmDeviceRateChange \n"));
    sasAddressHi = TD_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
    sasAddressLo = TD_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
    oneDeviceData = tddmPortSASDeviceFind(tiRoot, onePortContext, sasAddressLo, sasAddressHi);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tddmReportDevice: oneDeviceData is NULL!!!\n"));
    }
    else
    {
      agRoot = oneDeviceData->agRoot;
      if ( oneDeviceData->registered == agTRUE)
      {
        option = 0x20; /* bit 5 */
        DLR = DEVINFO_GET_LINKRATE(dmDeviceInfo);
        param = DLR << 28;
        TI_DBG1(("tddmReportDevice: option 0x%x param 0x%x DLR 0x%x\n", option, param, DLR));
        saSetDeviceInfo(agRoot, agNULL, 0, oneDeviceData->agDevHandle, option, param, ossaSetDeviceInfoCB);

      }
      else
      {
        TI_DBG1(("tddmReportDevice: oneDeviceData is not yet registered !!!\n"));
      }

    }
  }
  else
  {
    TI_DBG1(("tddmReportDevice: unknown flag 0x%x, wrong\n", flag));
  }

  return;
}

osGLOBAL void
tdsaUpdateMCN(
              dmRoot_t             *dmRoot,
              tdsaPortContext_t    *onePortContext
             )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  tdsaDeviceData_t   *oneDeviceData = agNULL;
  tdList_t           *DeviceListList;
  bit32              localMCN = 0, finalMCN = 0;
  bit32              devMCN = 1;
  bit32              option;
  bit32              param;

  TI_DBG3(("tdsaUpdateMCN: start\n"));
  tdsaRoot = (tdsaRoot_t *)dmRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tdsaUpdateMCN: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tdsaUpdateMCN: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsaUpdateMCN: tiRoot is NULL\n"));
    return;
  }

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaUpdateMCN: onePortContext is invalid\n"));
    return;
  }

  TI_DBG3(("tdsaUpdateMCN: pid %d\n", onePortContext->id));

  localMCN = tdsaFindLocalMCN(tiRoot, onePortContext);

  if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
  {
    TI_DBG1(("tdsaUpdateMCN: empty device list\n"));
    return;
  }

  /* update directly and behind expander device */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdsaUpdateMCN: oneDeviceData is NULL!!!\n"));
      return;
    }
    TI_DBG3(("tdsaUpdateMCN: loop did %d\n", oneDeviceData->id));
    TI_DBG3(("tdsaUpdateMCN: sasAddrHi 0x%08x sasAddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
    devMCN = oneDeviceData->devMCN;
    if ( oneDeviceData->tdPortContext == onePortContext)
    {
      if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE && oneDeviceData->directlyAttached == agTRUE)
      {
        TI_DBG3(("tdsaUpdateMCN: found directly attached\n"));
        finalMCN = localMCN;
        TI_DBG3(("tdsaUpdateMCN: devMCN 0x%08x localMCN 0x%08x\n", devMCN, localMCN));
        TI_DBG3(("tdsaUpdateMCN: finalMCN 0x%08x\n", finalMCN));
        if (oneDeviceData->finalMCN != finalMCN)
        {
          /* saSetDeviceInfo using finalMCN */
          option = 8; /* setting only MCN 1000b */
          param = finalMCN << 24;
          TI_DBG3(("tdsaUpdateMCN: option 0x%x param 0x%x MCN 0x%x\n", option, param, finalMCN));
          saSetDeviceInfo(oneDeviceData->agRoot, agNULL, 0, oneDeviceData->agDevHandle, option, param, ossaSetDeviceInfoCB);
          oneDeviceData->finalMCN = finalMCN;
        }

      }
      else if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE && oneDeviceData->directlyAttached == agFALSE)
      {
        TI_DBG3(("tdsaUpdateMCN: found behind expander device\n"));
        finalMCN = MIN(localMCN, devMCN);
        TI_DBG3(("tdsaUpdateMCN: devMCN 0x%08x localMCN 0x%08x\n", devMCN, localMCN));
        TI_DBG3(("tdsaUpdateMCN: finalMCN 0x%08x\n", finalMCN));
        if (oneDeviceData->finalMCN != finalMCN)
        {
          /* saSetDeviceInfo using finalMCN */
          option = 8; /* setting only MCN 1000b */
          param = finalMCN << 24;
          TI_DBG3(("tdsaUpdateMCN: option 0x%x param 0x%x MCN 0x%x\n", option, param, finalMCN));
          saSetDeviceInfo(oneDeviceData->agRoot, agNULL, 0, oneDeviceData->agDevHandle, option, param, ossaSetDeviceInfoCB);
          oneDeviceData->finalMCN = finalMCN;
        }

      }
      DeviceListList = DeviceListList->flink;
    }
    else
    {
      if (oneDeviceData->tdPortContext != agNULL)
      {
        TI_DBG3(("tdsaUpdateMCN: different portcontext; oneDeviceData->tdPortContext pid %d oneportcontext pid %d\n", oneDeviceData->tdPortContext->id, onePortContext->id));
      }
      else
      {
        TI_DBG3(("tdsaUpdateMCN: different portcontext; oneDeviceData->tdPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }
  }  /* while */

  return;
}

osGLOBAL bit8
tddmSATADeviceTypeDecode(bit8 * pSignature)
{
    return (bit8)tdssSATADeviceTypeDecode(pSignature);
}


osGLOBAL void
tddmSingleThreadedEnter(
                        dmRoot_t    *dmRoot,
                        bit32       syncLockId
                       )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  bit32              offset = 0;

  TI_DBG7(("tddmSingleThreadedEnter: start\n"));

  tdsaRoot = (tdsaRoot_t *)dmRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tddmSingleThreadedEnter: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tddmSingleThreadedEnter: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tddmSingleThreadedEnter: tiRoot is NULL\n"));
    return;
  }
  offset = tdsaAllShared->MaxNumLLLocks + tdsaAllShared->MaxNumOSLocks + TD_MAX_LOCKS;

  ostiSingleThreadedEnter(tiRoot, syncLockId + offset);
  return;
}

osGLOBAL void
tddmSingleThreadedLeave(
                        dmRoot_t    *dmRoot,
                        bit32       syncLockId
                       )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  bit32              offset = 0;

  TI_DBG7(("tddmSingleThreadedLeave: start\n"));

  tdsaRoot = (tdsaRoot_t *)dmRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tddmSingleThreadedLeave: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tddmSingleThreadedLeave: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tddmSingleThreadedLeave: tiRoot is NULL\n"));
    return;
  }
  offset = tdsaAllShared->MaxNumLLLocks + tdsaAllShared->MaxNumOSLocks + TD_MAX_LOCKS;

  ostiSingleThreadedLeave(tiRoot, syncLockId + offset);

  return;
}

osGLOBAL bit32 tddmGetTransportParam(
                        dmRoot_t    *dmRoot,
                        char        *key,
                        char        *subkey1,
                        char        *subkey2,
                        char        *subkey3,
                        char        *subkey4,
                        char        *subkey5,
                        char        *valueName,
                        char        *buffer,
                        bit32       bufferLen,
                        bit32       *lenReceived
                        )
{
  bit32              ret = tiError;

  TI_DBG7(("tddmGetTransportParam: start\n"));
  ret = ostiGetTransportParam(agNULL,
                              key,
                              subkey1,
                              subkey2,
                              subkey3,
                              subkey4,
                              subkey5,
                              valueName,
                              buffer,
                              bufferLen,
                              lenReceived
                              );

  return ret;
}

#endif /* FDS_DM */

