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

#ifdef FDS_SM

#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#endif

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>

#if defined(SM_DEBUG)
extern bit32 gSMDebugLevel;
#endif

osGLOBAL void
smReportRemovalDirect(
                       tiRoot_t             *tiRoot,
                       agsaRoot_t           *agRoot,
                       tdsaDeviceData_t     *oneDeviceData
         )
{
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit8                    PhyID;

  TI_DBG2(("smReportRemovalDirect: start\n"));

  PhyID                  = oneDeviceData->phyID;

  tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
  oneDeviceData->valid = agFALSE;
  oneDeviceData->valid2 = agFALSE;
  /* put onedevicedata back to free list */
  osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
  TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
  TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));

  /* notifying link up */
  ostiPortEvent (
                 tiRoot,
                 tiPortLinkUp,
                 tiSuccess,
                 (void *)tdsaAllShared->Ports[PhyID].tiPortalContext
                );
#ifdef INITIATOR_DRIVER
  /* triggers discovery */
  ostiPortEvent(
                tiRoot,
                tiPortDiscoveryReady,
                tiSuccess,
                (void *) tdsaAllShared->Ports[PhyID].tiPortalContext
                );
#endif
  return;
}

osGLOBAL void
smReportRemoval(
                 tiRoot_t             *tiRoot,
                 agsaRoot_t           *agRoot,
                 tdsaDeviceData_t     *oneDeviceData,
                 tdsaPortContext_t    *onePortContext
         )
{
  TI_DBG2(("smReportRemoval: start\n"));

  if (oneDeviceData->registered == agTRUE)
  {
    /*
      1. remove this device
      2. device removal event
    */
    tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
    oneDeviceData->valid = agFALSE;
    oneDeviceData->valid2 = agFALSE;
    oneDeviceData->registered = agFALSE;
    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDeviceChange,
                       tiDeviceRemoval,
                       agNULL
                     );
  }

  return;
}
osGLOBAL void
smHandleDirect(
                tiRoot_t             *tiRoot,
                agsaRoot_t           *agRoot,
                tdsaDeviceData_t     *oneDeviceData,
                void                 *IDdata
        )
{
  tdsaRoot_t              *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t           *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaSATAIdentifyData_t  *pSATAIdData;
  tdList_t                *DeviceListList;
  tdsaDeviceData_t        *tmpOneDeviceData = agNULL;
  int                     new_device = agTRUE;
  bit8                    PhyID;

  TI_DBG2(("smHandleDirect: start\n"));
  PhyID = oneDeviceData->phyID;

  pSATAIdData = (agsaSATAIdentifyData_t *)IDdata;
  //tdhexdump("satAddSATAIDDevCB after", (bit8 *)pSATAIdData, sizeof(agsaSATAIdentifyData_t));

  /* compare idenitfy device data to the exiting list */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    tmpOneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (tmpOneDeviceData == agNULL)
    {
      TI_DBG1(("smHandleDirect: tmpOneDeviceData is NULL!!!\n"));
      return;
    }
    TI_DBG1(("smHandleDirect: LOOP tmpOneDeviceData %p did %d\n", tmpOneDeviceData, tmpOneDeviceData->id));
    //tdhexdump("smHandleDirect LOOP", (bit8 *)&tmpOneDeviceData->satDevData.satIdentifyData, sizeof(agsaSATAIdentifyData_t));

    /* what is unique ID for sata device -> response of identify devicedata; not really
       Let's compare serial number, firmware version, model number
    */
    if ( tmpOneDeviceData->DeviceType == TD_SATA_DEVICE &&
         (osti_memcmp (tmpOneDeviceData->satDevData.satIdentifyData.serialNumber,
                       pSATAIdData->serialNumber,
                       20) == 0) &&
         (osti_memcmp (tmpOneDeviceData->satDevData.satIdentifyData.firmwareVersion,
                       pSATAIdData->firmwareVersion,
                       8) == 0) &&
         (osti_memcmp (tmpOneDeviceData->satDevData.satIdentifyData.modelNumber,
                       pSATAIdData->modelNumber,
                       40) == 0)
       )
    {
      TI_DBG2(("smHandleDirect: did %d\n", tmpOneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }


  if (new_device == agFALSE)
  {
    TI_DBG2(("smHandleDirect: old device data\n"));
    tmpOneDeviceData->valid = agTRUE;
    tmpOneDeviceData->valid2 = agTRUE;
    /* save data field from new device data */
    tmpOneDeviceData->agRoot = agRoot;
    tmpOneDeviceData->agDevHandle = oneDeviceData->agDevHandle;
    tmpOneDeviceData->agDevHandle->osData = tmpOneDeviceData; /* TD layer */
    tmpOneDeviceData->tdPortContext = oneDeviceData->tdPortContext;
    tmpOneDeviceData->phyID = oneDeviceData->phyID;

    /*
      one SATA directly attached device per phy;
      Therefore, deregister then register
    */
    saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, tdsaRotateQnumber(tiRoot, oneDeviceData));

    if (tmpOneDeviceData->registered == agFALSE)
    {
      TI_DBG2(("smHandleDirect: re-registering old device data\n"));
      /* already has old information; just register it again */
      saRegisterNewDevice( /* smHandleDirect */
                          agRoot,
                          &tmpOneDeviceData->agContext,
                          0,/*tdsaRotateQnumber(tiRoot, tmpOneDeviceData),*/
                          &tmpOneDeviceData->agDeviceInfo,
                          tmpOneDeviceData->tdPortContext->agPortContext,
                          0
                          );
    }

//    tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
    /* put tmpOneDeviceData back to free list */
    osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
    TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));

    TI_DBG2(("smHandleDirect: pid %d\n", tdsaAllShared->Ports[PhyID].portContext->id));
    /* notifying link up */
    ostiPortEvent (
                   tiRoot,
                   tiPortLinkUp,
                   tiSuccess,
                   (void *)tdsaAllShared->Ports[PhyID].tiPortalContext
                   );


#ifdef INITIATOR_DRIVER
    /* triggers discovery */
    ostiPortEvent(
                  tiRoot,
                  tiPortDiscoveryReady,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[PhyID].tiPortalContext
                  );
#endif
     return;
  }

  TI_DBG2(("smHandleDirect: new device data\n"));
  oneDeviceData->satDevData.satIdentifyData = *pSATAIdData;
  /* notifying link up */
  ostiPortEvent (
                 tiRoot,
                 tiPortLinkUp,
                 tiSuccess,
                 (void *)tdsaAllShared->Ports[PhyID].tiPortalContext
                 );
#ifdef INITIATOR_DRIVER
  /* triggers discovery */
  ostiPortEvent(
                tiRoot,
                tiPortDiscoveryReady,
                tiSuccess,
                (void *) tdsaAllShared->Ports[PhyID].tiPortalContext
                );
#endif

  return;
}

/*
  combine satAddSATAIDDevCB(expander) and satAddSATAIDDevCB(directly attached)
*/
osGLOBAL void
tdsmIDCompletedCB(
                  smRoot_t      *smRoot,
                  smIORequest_t     *smIORequest,
                  smDeviceHandle_t    *smDeviceHandle,
                  bit32       status,
                  void        *IDdata
                 )
{
  tdsaRoot_t                *tdsaRoot;
  tdsaContext_t             *tdsaAllShared;
  tiRoot_t                  *tiRoot;
  agsaRoot_t                *agRoot;
  tdIORequestBody_t         *tdIORequestBody;
  tdsaDeviceData_t          *oneDeviceData;
  tdsaPortContext_t         *onePortContext;
  tiPortalContext_t         *tiPortalContext;
  bit32                     pid = 0xff;
  bit32                     IDstatus;
  agsaSATAIdentifyData_t    *pSATAIdData;

  TI_DBG2(("tdsmIDCompletedCB: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  tdIORequestBody = (tdIORequestBody_t *)smIORequest->tdData;

  if (smDeviceHandle == agNULL)
  {
     TI_DBG1(("tdsmIDCompletedCB: smDeviceHandle is NULL !!!!\n"));
     ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
     return;
  }

  oneDeviceData = (tdsaDeviceData_t *)smDeviceHandle->tdData;
  onePortContext = oneDeviceData->tdPortContext;
  agRoot = oneDeviceData->agRoot;
  pid = tdIORequestBody->pid;


//  oneDeviceData->satDevData.IDDeviceValid = agFALSE;
  oneDeviceData->satDevData.IDPending = agFALSE;

  TI_DBG2(("tdsmIDCompletedCB: tdIORequestBody %p  tdIORequestBody->osMemHandle %p\n", tdIORequestBody, tdIORequestBody->osMemHandle));

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

  if (onePortContext == agNULL)
  {
    TI_DBG1(("tdsmIDCompletedCB: onePortContext is NULL!!!\n"));
    ostiFreeMemory(
                    tiRoot,
                    tdIORequestBody->osMemHandle,
                    sizeof(tdIORequestBody_t)
                  );
    return;
  }

  /* check port id */
  if (pid != onePortContext->id)
  {
    TI_DBG1(("tdsmIDCompletedCB: not matching pid; pid %d onePortContext->id %d!!!\n", pid, onePortContext->id));
    if (oneDeviceData->directlyAttached == agTRUE)
    {
      smReportRemovalDirect(tiRoot, agRoot, oneDeviceData);
    }
    else
    {
      smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
    }
    ostiFreeMemory(
                    tiRoot,
                    tdIORequestBody->osMemHandle,
                    sizeof(tdIORequestBody_t)
                  );
    return;
  }

  tiPortalContext= onePortContext->tiPortalContext;

  if (tiPortalContext == agNULL)
  {
    TI_DBG1(("tdsmIDCompletedCB: tiPortalContext is NULL!!!\n"));
    if (oneDeviceData->directlyAttached == agTRUE)
    {
      smReportRemovalDirect(tiRoot, agRoot, oneDeviceData);
    }
    else
    {
      smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
    }
    ostiFreeMemory(
                    tiRoot,
                    tdIORequestBody->osMemHandle,
                    sizeof(tdIORequestBody_t)
                  );
    return;
  }

  if (agRoot == agNULL)
  {
    TI_DBG1(("tdsmIDCompletedCB: agRoot is NULL!!!\n"));
    ostiFreeMemory(
                    tiRoot,
                    tdIORequestBody->osMemHandle,
                    sizeof(tdIORequestBody_t)
                  );
    return;
  }

  if (status == smIOSuccess)
  {
    TI_DBG2(("tdsmIDCompletedCB: smIOSuccess\n"));

    oneDeviceData->satDevData.IDDeviceValid = agTRUE;
    if (oneDeviceData->directlyAttached == agTRUE)
    {
      TI_DBG2(("tdsmIDCompletedCB: directlyAttached\n"));
      pSATAIdData = (agsaSATAIdentifyData_t *)IDdata;
      smHandleDirect(tiRoot, agRoot, oneDeviceData, IDdata);
      /* filling in */
      osti_memcpy(onePortContext->remoteName, pSATAIdData->serialNumber, 20);
      osti_memcpy(&(onePortContext->remoteName[20]), pSATAIdData->firmwareVersion, 8);
      osti_memcpy(&(onePortContext->remoteName[28]), pSATAIdData->modelNumber, 40);
    }
    else /* expander attached */
    {
    
      TI_DBG2(("tdsmIDCompletedCB: expander attached\n"));

      if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED)
      {
        TI_DBG1(("tdsmIDCompletedCB: ID completed after discovery is done; tiDeviceArrival\n"));
        /* ID data completed after discovery is completed */
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
    TI_DBG2(("tdsmIDCompletedCB: tdIORequestBody %p  tdIORequestBody->osMemHandle %p\n", tdIORequestBody, tdIORequestBody->osMemHandle));
    ostiFreeMemory(
                    tiRoot,
                    tdIORequestBody->osMemHandle,
                    sizeof(tdIORequestBody_t)
                  );

  }
  else if ( status == smIORetry)
  {
    TI_DBG1(("tdsmIDCompletedCB: smIORetry!!!\n"));
    if ( !(oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
           oneDeviceData->tdPortContext != agNULL)
       )
    {
      TI_DBG1(("tdsmIDCompletedCB: smIORetry but device is not valid!!!\n"));
      tdIORequestBody->reTries = 0;
      tdIORequestBody->ioCompleted = agTRUE;
      tdIORequestBody->ioStarted = agFALSE;
      ostiFreeMemory(
                     tiRoot,
                     tdIORequestBody->osMemHandle,
                     sizeof(tdIORequestBody_t)
             );
      oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      return;
    }

    if (tdIORequestBody->reTries <= SM_RETRIES)
    {
      tdIORequestBody->tiIORequest = agNULL; /* not in use */
      tdIORequestBody->pid = onePortContext->id;
      smIORequest->tdData = tdIORequestBody;
      smIORequest->smData = &tdIORequestBody->smIORequestBody;

      smDeviceHandle->tdData = oneDeviceData;

      oneDeviceData->satDevData.IDDeviceValid = agFALSE;

      IDstatus = smIDStart(smRoot,
                           smIORequest,
                           smDeviceHandle
                           );
      if (IDstatus != SM_RC_SUCCESS)
      {
        /* identify device data is not valid */
        TI_DBG1(("tdsmIDCompletedCB: smIDStart fail or busy %d!!!\n", IDstatus));
        tdIORequestBody->reTries = 0;
        tdIORequestBody->ioCompleted = agTRUE;
        tdIORequestBody->ioStarted = agFALSE;
        ostiFreeMemory(
                       tiRoot,
                       tdIORequestBody->osMemHandle,
                       sizeof(tdIORequestBody_t)
                        );
        smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
        return;
      }
      tdIORequestBody->reTries++;
      tdIORequestBody->ioCompleted = agFALSE;
      tdIORequestBody->ioStarted = agTRUE;
      oneDeviceData->satDevData.IDPending = agTRUE;
      /* start a timer */
      tdIDStartTimer(tiRoot, smIORequest, oneDeviceData);
      TI_DBG1(("tdsmIDCompletedCB: being retried!!!\n"));
    }
    else
    {
      /* give up */
      TI_DBG1(("tdsmIDCompletedCB: retries are over!!!\n"));
      tdIORequestBody->reTries = 0;
      tdIORequestBody->ioCompleted = agTRUE;
      tdIORequestBody->ioStarted = agFALSE;
      ostiFreeMemory(
                     tiRoot,
                     tdIORequestBody->osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
      oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      /* SATA device is not usable; remove it */
      smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
    }
  }
  else if ( status == smIOSTPResourceBusy)
  {
    /* decides to send smp hard reset or not */
    TI_DBG1(("tdsmIDCompletedCB: smIOSTPResourceBusy\n"));
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->osMemHandle,
                   sizeof(tdIORequestBody_t)
                  );
    oneDeviceData->satDevData.IDDeviceValid = agFALSE;
    if (tdsaAllShared->FCA)
    {
      if (oneDeviceData->SMNumOfFCA <= 0) /* does SMP HARD RESET only upto one time */
      {
        TI_DBG1(("tdsmIDCompletedCB: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; sending HARD_RESET\n"));
        oneDeviceData->SMNumOfFCA++;
        tdsaPhyControlSend(tiRoot,
                           oneDeviceData,
                           SMP_PHY_CONTROL_HARD_RESET,
                           agNULL,
                           tdsaRotateQnumber(tiRoot, oneDeviceData)
                          );
      }
      else
      {
        /* given up after one time of SMP HARD RESET; */
        TI_DBG1(("tdsmIDCompletedCB: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; but giving up sending HARD_RESET!!!\n"));
        smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
      }
    }
    else
    {
      /* do nothing */
    }
  }
  else
  {
    TI_DBG1(("tdsmIDCompletedCB: smIDStart fail, status 0x%x!!!\n", status));
    TI_DBG1(("tdsmIDCompletedCB: did %d!!!\n", oneDeviceData->id));
    if ( !(oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
           oneDeviceData->tdPortContext != agNULL)
       )
    {
      TI_DBG1(("tdsmIDCompletedCB: fail but device is not valid!!!\n"));
      tdIORequestBody->reTries = 0;
      tdIORequestBody->ioCompleted = agTRUE;
      tdIORequestBody->ioStarted = agFALSE;
      ostiFreeMemory(
                     tiRoot,
                     tdIORequestBody->osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
      oneDeviceData->satDevData.IDDeviceValid = agFALSE;
      return;
    }
    tdsaAllShared->IDRetry = agTRUE;
    if (tdsaAllShared->IDRetry)
    {
      if (tdIORequestBody->reTries <= SM_RETRIES)
      {
        tdIORequestBody->tiIORequest = agNULL; /* not in use */
        tdIORequestBody->pid = onePortContext->id;
        smIORequest->tdData = tdIORequestBody;
        smIORequest->smData = &tdIORequestBody->smIORequestBody;

        smDeviceHandle->tdData = oneDeviceData;
        IDstatus = smIDStart(smRoot,
                             smIORequest,
                             smDeviceHandle
                             );
        if (IDstatus != SM_RC_SUCCESS)
        {
          /* identify device data is not valid */
          TI_DBG1(("tdsmIDCompletedCB: smIDStart fail or busy %d!!!\n", IDstatus));
          tdIORequestBody->reTries = 0;
          tdIORequestBody->ioCompleted = agTRUE;
          tdIORequestBody->ioStarted = agFALSE;
          ostiFreeMemory(
                         tiRoot,
                         tdIORequestBody->osMemHandle,
                         sizeof(tdIORequestBody_t)
                         );
          oneDeviceData->satDevData.IDDeviceValid = agFALSE;
          if (oneDeviceData->directlyAttached == agTRUE)
          {
            smReportRemovalDirect(tiRoot, agRoot, oneDeviceData);
          }
          else
          {
            smReportRemoval(tiRoot, agRoot, oneDeviceData, onePortContext);
          }
          return;
        }
        tdIORequestBody->reTries++;
        tdIORequestBody->ioCompleted = agFALSE;
        tdIORequestBody->ioStarted = agTRUE;
        oneDeviceData->satDevData.IDPending = agTRUE;
        /* start a timer */
        tdIDStartTimer(tiRoot, smIORequest, oneDeviceData);
        TI_DBG1(("tdsmIDCompletedCB: being retried!!!\n"));
      }
      else
      {
        /* give up */
        TI_DBG1(("tdsmIDCompletedCB: retries are over; sending hard reset!!!\n"));
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
          TI_DBG1(("tdsmIDCompletedCB: fail; sending HARD_RESET\n"));
          oneDeviceData->SMNumOfID++;
          if (oneDeviceData->directlyAttached == agTRUE)
          {
            saLocalPhyControl(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), oneDeviceData->phyID, AGSA_PHY_HARD_RESET, agNULL);
          }
          else
          {
            tdsaPhyControlSend(tiRoot,
                               oneDeviceData,
                               SMP_PHY_CONTROL_HARD_RESET,
                               agNULL,
                               tdsaRotateQnumber(tiRoot, oneDeviceData)
                              );
          }
        }
        else
        {
          /* given up after one time of SMP HARD RESET; */
          TI_DBG1(("tdsmIDCompletedCB: fail; but giving up sending HARD_RESET!!!\n"));
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
    }
    else
    {
      /* do nothing */
    }


  }


  return;
}

FORCEINLINE void
tdsmIOCompletedCB(
                  smRoot_t      *smRoot,
                  smIORequest_t     *smIORequest,
                  bit32       status,
                  bit32       statusDetail,
                  smSenseData_t     *senseData,
                  bit32       interruptContext
                  )
{
  tdsaRoot_t                *tdsaRoot         = (tdsaRoot_t *)smRoot->tdData;
  tdsaContext_t             *tdsaAllShared    = &(tdsaRoot->tdsaAllShared);
  tiRoot_t                  *tiRoot           = tdsaAllShared->agRootOsDataForInt.tiRoot;
  tdIORequestBody_t         *tdIORequestBody  = (tdIORequestBody_t *)smIORequest->tdData;
  tiIORequest_t             *tiIORequest      = tdIORequestBody->tiIORequest;

  tdsaDeviceData_t          *oneDeviceData;
  tiDeviceHandle_t          *tiDeviceHandle;
  smDeviceHandle_t          *smDeviceHandle;
  smScsiInitiatorRequest_t  *smSCSIRequest;
  smSuperScsiInitiatorRequest_t  *smSuperSCSIRequest;

  bit32                     SMStatus = SM_RC_FAILURE;


  TI_DBG5(("tdsmIOCompletedCB: start\n"));

  if (status == smIOSuccess)
  {
    ostiInitiatorIOCompleted( tiRoot,
                         tiIORequest,
                         status,
                         statusDetail,
                         (tiSenseData_t *)senseData,
                         interruptContext);
  }
  else if (status == smIORetry)
  {
    TI_DBG1(("tdsmIOCompletedCB: smIORetry!!!\n"));
    smIORequest = (smIORequest_t *)&(tdIORequestBody->smIORequest);
    tiDeviceHandle = tdIORequestBody->tiDevHandle;
    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

    if (! (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
           oneDeviceData->tdPortContext != agNULL)
       )
    {
      TI_DBG1(("tdsmIOCompletedCB: smIORetry but device is not valid!!!\n"));
      tdIORequestBody->reTries = 0;
      tdIORequestBody->ioCompleted = agTRUE;
      tdIORequestBody->ioStarted = agFALSE;
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                status,
                                statusDetail,
                                (tiSenseData_t *)senseData,
                                interruptContext);
      return;
    }
    if (tdIORequestBody->reTries <= SM_RETRIES)
    {
      smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
      if (tdIORequestBody->superIOFlag == agTRUE)
      {
        smSuperSCSIRequest = (smSuperScsiInitiatorRequest_t *)&(tdIORequestBody->SM.smSuperSCSIRequest);
        SMStatus = smSuperIOStart(smRoot,
                                  smIORequest,
                                  smDeviceHandle,
                                  smSuperSCSIRequest,
                                  oneDeviceData->SASAddressID.sasAddressHi,			      
                                  oneDeviceData->SASAddressID.sasAddressLo,
                                  interruptContext);
      }
      else
      {
        smSCSIRequest = (smScsiInitiatorRequest_t *)&(tdIORequestBody->SM.smSCSIRequest);
        SMStatus = smIOStart(smRoot,
                             smIORequest,
                             smDeviceHandle,
                             smSCSIRequest,
                             interruptContext);
      }


      if (SMStatus != SM_RC_SUCCESS)
      {
        TI_DBG1(("tdsmIOCompletedCB: smIDStart fail or busy %d!!!\n", SMStatus));
        tdIORequestBody->reTries = 0;
        tdIORequestBody->ioCompleted = agTRUE;
        tdIORequestBody->ioStarted = agFALSE;
        ostiInitiatorIOCompleted( tiRoot,
                                  tiIORequest,
                                  status,
                                  statusDetail,
                                  (tiSenseData_t *)senseData,
                                  interruptContext);
        return;
      }
      else
      {
        TI_DBG1(("tdsmIOCompletedCB: being retried!!!\n"));
        tdIORequestBody->reTries++;
        tdIORequestBody->ioCompleted = agFALSE;
        tdIORequestBody->ioStarted = agTRUE;
      }
    }
    else
    {
      /* give up; complete IO */
      TI_DBG1(("tdsmIOCompletedCB: retries are over!!!\n"));
      tdIORequestBody->reTries = 0;
      tdIORequestBody->ioCompleted = agTRUE;
      tdIORequestBody->ioStarted = agFALSE;
      ostiInitiatorIOCompleted( tiRoot,
                                tiIORequest,
                                status,
                                statusDetail,
                                (tiSenseData_t *)senseData,
                                interruptContext);
      return;
    }

  }
  else if ( status == smIOSTPResourceBusy)
  {
    /* decides to send smp hard reset or not */
    TI_DBG1(("tdsmIOCompletedCB: smIOSTPResourceBusy\n"));
    if (tdsaAllShared->FCA)
    {
      smIORequest = (smIORequest_t *)&(tdIORequestBody->smIORequest);
      tiDeviceHandle = tdIORequestBody->tiDevHandle;
      oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      if (oneDeviceData->SMNumOfFCA <= 0) /* does SMP HARD RESET only upto one time */
      {
        TI_DBG1(("tdsmIOCompletedCB: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; sending HARD_RESET\n"));
        oneDeviceData->SMNumOfFCA++;
        tdsaPhyControlSend(tiRoot,
                           oneDeviceData,
                           SMP_PHY_CONTROL_HARD_RESET,
                           agNULL,
                           tdsaRotateQnumber(tiRoot, oneDeviceData)
                          );
      }
      else
      {
        /* given up after one time of SMP HARD RESET; */
        TI_DBG1(("tdsmIOCompletedCB: OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY; but giving up sending HARD_RESET!!!\n"));
      }
    }
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              status,
                              statusDetail,
                              (tiSenseData_t *)senseData,
                              interruptContext);
    return;
  }
  else
  {
    if (statusDetail == smDetailAborted)
    {
      tiDeviceHandle = tdIORequestBody->tiDevHandle;
      oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      TI_DBG1(("tdsmIOCompletedCB: agIOStatus = OSSA_IO_ABORTED did %d\n", oneDeviceData->id));
    }
    ostiInitiatorIOCompleted( tiRoot,
                              tiIORequest,
                              status,
                              statusDetail,
                              (tiSenseData_t *)senseData,
                              interruptContext);
  }

  return;
}

/* completion of taskmanagement
osGLOBAL void ostiInitiatorEvent (
                        tiRoot_t            *tiRoot,
                        tiPortalContext_t   *portalContext,
                        tiDeviceHandle_t    *tiDeviceHandle,
                        tiIntrEventType_t   eventType,
                        bit32               eventStatus,
                        void                *parm
                        );

*/
//qqq1
osGLOBAL void
tdsmEventCB(
            smRoot_t          *smRoot,
            smDeviceHandle_t  *smDeviceHandle,
            smIntrEventType_t  eventType,
            bit32              eventStatus,
            void              *parm
           )
{
  tdsaRoot_t                  *tdsaRoot;
  tdsaContext_t               *tdsaAllShared;
  tiRoot_t                    *tiRoot;
  tdIORequestBody_t           *tdIORequestBody;
  smIORequest_t               *SMcurrentTaskTag;
  tiIORequest_t               *currentTaskTag;
  tdsaDeviceData_t            *oneDeviceData;
  void                        *osMemHandle;
  tdsaPortContext_t           *onePortContext;
  tiPortalContext_t           *tiportalContext;
  tiDeviceHandle_t            *tiDeviceHandle;

  /* be sure to free using tdIORequestBody->->IOType.InitiatorTMIO.osMemHandle but how???
     parm = pSatDevData->satTmTaskTag (currentTaskTag in tiINITaskManagement)
     In this case, parm is smIORequest_t
  */

  TI_DBG2(("tdsmEventCB: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;


  if (eventType == smIntrEventTypeLocalAbort)
  {
    oneDeviceData = (tdsaDeviceData_t *)smDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdsmEventCB: oneDeviceData is NULL\n"));
      return;
    }
    else
    {
      tiDeviceHandle = &(oneDeviceData->tiDeviceHandle);
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
    }
  }
  else
  {

    SMcurrentTaskTag = (smIORequest_t *)parm;
    if (SMcurrentTaskTag == agNULL)
    {
      TI_DBG1(("tdsmEventCB: SMcurrentTaskTag is NULL!!!\n"));
      return;
    }

    tdIORequestBody = (tdIORequestBody_t *)SMcurrentTaskTag->tdData;
    if (tdIORequestBody == agNULL)
    {
      TI_DBG1(("tdsmEventCB: tdIORequestBody is NULL!!!\n"));
      return;
    }

    osMemHandle =  tdIORequestBody->IOType.InitiatorTMIO.osMemHandle;
    currentTaskTag = tdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag;


    oneDeviceData = (tdsaDeviceData_t *)smDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdsmEventCB: oneDeviceData is NULL!!!\n"));
      return;
    }

    tiDeviceHandle = &(oneDeviceData->tiDeviceHandle);
    onePortContext = oneDeviceData->tdPortContext;
    if (onePortContext == agNULL)
    {
      TI_DBG1(("tdsmEventCB: onePortContext is NULL!!!\n"));
      return;
    }
    tiportalContext = onePortContext->tiPortalContext;

    /* free tdIORequestBody */
    ostiFreeMemory(
                    tiRoot,
                    osMemHandle,
                    sizeof(tdIORequestBody_t)
                   );


    TI_DBG2(("tdsmEventCB: calling ostiInitiatorEvent\n"));
    ostiInitiatorEvent(
                        tiRoot,
                        tiportalContext,
                        tiDeviceHandle,
                        eventType,
                        eventStatus,
                        (void *)currentTaskTag
                       );


      /* completion of taskmanagement
      osGLOBAL void ostiInitiatorEvent (
                              tiRoot_t            *tiRoot,
                              tiPortalContext_t   *portalContext,
                              tiDeviceHandle_t    *tiDeviceHandle,
                              tiIntrEventType_t   eventType,
                              bit32               eventStatus,
                              void                *parm
                              );


      ostiFreeAlloc()
    */

  }

  return;
}


FORCEINLINE void
tdsmSingleThreadedEnter(
                        smRoot_t    *smRoot,
                        bit32        syncLockId
                        )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  bit32              offset = 0;

  TI_DBG7(("tdsmSingleThreadedEnter: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tdsmSingleThreadedEnter: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tdsmSingleThreadedEnter: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsmSingleThreadedEnter: tiRoot is NULL\n"));
    return;
  }

  offset = tdsaAllShared->MaxNumLLLocks + tdsaAllShared->MaxNumOSLocks + TD_MAX_LOCKS + tdsaAllShared->MaxNumDMLocks;

  ostiSingleThreadedEnter(tiRoot, syncLockId + offset);

  return;
}

FORCEINLINE void
tdsmSingleThreadedLeave(
                        smRoot_t    *smRoot,
                        bit32       syncLockId
                        )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  bit32              offset = 0;

  TI_DBG7(("tdsmSingleThreadedLeave: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tdsmSingleThreadedLeave: tdsaRoot is NULL\n"));
    return;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tdsmSingleThreadedLeave: tdsaAllShared is NULL\n"));
    return;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsmSingleThreadedLeave: tiRoot is NULL\n"));
    return;
  }
  offset = tdsaAllShared->MaxNumLLLocks + tdsaAllShared->MaxNumOSLocks + TD_MAX_LOCKS + tdsaAllShared->MaxNumDMLocks;

  ostiSingleThreadedLeave(tiRoot, syncLockId + offset);

  return;
}

osGLOBAL FORCEINLINE bit8
tdsmBitScanForward(
                  smRoot_t    *smRoot,
                  bit32      *Index,
                  bit32       Mask
                  )
{
    return ostiBitScanForward(agNULL, Index, Mask);
}

#ifdef LINUX_VERSION_CODE

osGLOBAL FORCEINLINE sbit32
tdsmInterlockedIncrement(
                   smRoot_t         *smRoot,
                   sbit32 volatile  *Addend
                   )
{
   return ostiAtomicIncrement(agNULL, Addend);
}

osGLOBAL FORCEINLINE sbit32
tdsmInterlockedDecrement(
                   smRoot_t         *smRoot,
                   sbit32 volatile  *Addend
                   )
{
   return ostiAtomicDecrement(agNULL, Addend);
}



osGLOBAL FORCEINLINE sbit32
tdsmAtomicBitClear(
               smRoot_t         *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               )
{
   return ostiAtomicBitClear(agNULL, Destination, Value);
}

osGLOBAL FORCEINLINE sbit32
tdsmAtomicBitSet(
               smRoot_t         *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               )
{
   return ostiAtomicBitSet(agNULL, Destination, Value);
}

osGLOBAL FORCEINLINE sbit32
tdsmAtomicExchange(
               smRoot_t         *smRoot,
               sbit32 volatile  *Target,
               sbit32            Value
               )
{
    return ostiAtomicExchange(agNULL, Target, Value);
}

#else

osGLOBAL FORCEINLINE sbit32
tdsmInterlockedIncrement(
                   smRoot_t         *smRoot,
                   sbit32 volatile  *Addend
                   )
{
   return ostiInterlockedIncrement(agNULL, Addend);
}

osGLOBAL FORCEINLINE sbit32
tdsmInterlockedDecrement(
                   smRoot_t        *smRoot,
                   sbit32 volatile *Addend
                   )
{
   return ostiInterlockedDecrement(agNULL, Addend);
}



osGLOBAL FORCEINLINE sbit32
tdsmInterlockedAnd(
               smRoot_t        *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               )
{

   return ostiInterlockedAnd(agNULL, Destination, Value);
}

osGLOBAL FORCEINLINE sbit32
tdsmInterlockedOr(
               smRoot_t        *smRoot,
               sbit32 volatile  *Destination,
               sbit32            Value
               )
{
   return ostiInterlockedOr(agNULL, Destination, Value);
}

osGLOBAL FORCEINLINE sbit32
tdsmInterlockedExchange(
               smRoot_t          *smRoot,
               sbit32  volatile  *Target,
               sbit32             Value
               )
{
    return ostiInterlockedExchange(agNULL, Target, Value);
}

#endif /*LINUX_VERSION_CODE*/

osGLOBAL bit32
tdsmAllocMemory(
                smRoot_t    *smRoot,
                void        **osMemHandle,
                void        ** virtPtr,
                bit32       * physAddrUpper,
                bit32       * physAddrLower,
                bit32       alignment,
                bit32       allocLength,
                smBOOLEAN   isCacheable
               )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  bit32               status;

  TI_DBG5(("tdsmAllocMemory: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tdsmAllocMemory: tdsaRoot is NULL\n"));
    return SM_RC_FAILURE;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tdsmAllocMemory: tdsaAllShared is NULL\n"));
    return SM_RC_FAILURE;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsmAllocMemory: tiRoot is NULL\n"));
    return SM_RC_FAILURE;
  }

  status = ostiAllocMemory(tiRoot,
                           osMemHandle,
                           virtPtr,
                           physAddrUpper,
                           physAddrLower,
                           alignment,
                           allocLength,
                           isCacheable);

  if (status == tiSuccess)
  {
    return SM_RC_SUCCESS;
  }
  else
  {
    return SM_RC_FAILURE;
  }

}

osGLOBAL bit32
tdsmFreeMemory(
               smRoot_t    *smRoot,
               void        *osDMAHandle,
               bit32       allocLength
              )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  bit32               status;

  TI_DBG5(("tdsmFreeMemory: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tdsmFreeMemory: tdsaRoot is NULL\n"));
    return SM_RC_FAILURE;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tdsmFreeMemory: tdsaAllShared is NULL\n"));
    return SM_RC_FAILURE;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsmFreeMemory: tiRoot is NULL\n"));
    return SM_RC_FAILURE;
  }

  status = ostiFreeMemory(tiRoot,
                          osDMAHandle,
                          allocLength);

  if (status == tiSuccess)
  {
    return SM_RC_SUCCESS;
  }
  else
  {
    return SM_RC_FAILURE;
  }
}

FORCEINLINE bit32
tdsmRotateQnumber(smRoot_t        *smRoot,
                         smDeviceHandle_t *smDeviceHandle
                         )
{
  tdsaRoot_t         *tdsaRoot;
  tdsaContext_t      *tdsaAllShared;
  tiRoot_t           *tiRoot;
  tdsaDeviceData_t   *oneDeviceData;
  bit32              ret = 0;

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;


  TI_DBG6(("tdsmRotateQnumber: start\n"));

  if (smDeviceHandle == agNULL)
  {
     TI_DBG1(("tdsmRotateQnumber: smDeviceHandle is NULL !!!!\n"));
     return ret;
  }
  oneDeviceData = (tdsaDeviceData_t *)smDeviceHandle->tdData;
  if (oneDeviceData == agNULL)
  {
     TI_DBG1(("tdsmRotateQnumber: oneDeviceData is NULL !!!!\n"));
     return ret;
  }
  return tdsaRotateQnumber(tiRoot, oneDeviceData);
}

osGLOBAL bit32
tdsmSetDeviceQueueDepth(smRoot_t      *smRoot,
                                 smIORequest_t  *smIORequest,
                                 bit32          QueueDepth
                                 )
{
  tdsaRoot_t         *tdsaRoot      = agNULL;
  tdsaContext_t      *tdsaAllShared = agNULL;
  tiRoot_t           *tiRoot        = agNULL;
  tdIORequestBody_t  *tdIORequestBody  = (tdIORequestBody_t *)smIORequest->tdData;
  tiIORequest_t      *tiIORequest      = tdIORequestBody->tiIORequest;


  TI_DBG5(("tdsmSetDeviceQueueDepth: start\n"));

  tdsaRoot = (tdsaRoot_t *)smRoot->tdData;
  if (tdsaRoot == agNULL)
  {
    TI_DBG1(("tdsmSetDeviceQueueDepth: tdsaRoot is NULL\n"));
    return SM_RC_FAILURE;
  }

  tdsaAllShared = &(tdsaRoot->tdsaAllShared);
  if (tdsaAllShared == agNULL)
  {
    TI_DBG1(("tdsmSetDeviceQueueDepth: tdsaAllShared is NULL\n"));
    return SM_RC_FAILURE;
  }

  tiRoot = tdsaAllShared->agRootOsDataForInt.tiRoot;
  if (tiRoot == agNULL)
  {
    TI_DBG1(("tdsmFreeMemory: tiRoot is NULL\n"));
    return SM_RC_FAILURE;
  }

  return ostiSetDeviceQueueDepth(tiRoot, tiIORequest, QueueDepth);
}

osGLOBAL bit32 tdsmGetTransportParam(
                        smRoot_t    *smRoot,
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

  TI_DBG7(("tdsmGetTransportParam: start\n"));
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
#endif /* FDS_SM */

