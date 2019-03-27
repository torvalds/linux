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
 * This file contains initiator discover related functions
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

/*****************************************************************************
*! \brief  tiINIDiscoverTargets
*
*  Purpose:  This function is called to send a transport dependent discovery
*            request. An implicit login will be started following the
*            completion of discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   portalContext: Pointer to the portal context instance.
*  \param   option: This is a bit field option on how the session is to be
*                   created
*  \return:
*           tiSuccess    Discovery initiated.
*           tiBusy       Discovery could not be initiated at this time.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tiINIDiscoverTargets(
                     tiRoot_t            *tiRoot,
                     tiPortalContext_t   *portalContext,
                     bit32               option
                     )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  bit32             found = agFALSE;

#ifdef FDS_DM
  dmRoot_t          *dmRoot = &(tdsaAllShared->dmRoot);
  dmPortContext_t   *dmPortContext = agNULL;
#endif
  /*
   this function is called after LINK_UP by ossaHWCB()
   Therefore, tdsaportcontext is ready at this point
  */

  TI_DBG3(("tiINIDiscoverTargets: start\n"));

  /* find a right tdsaPortContext using tiPortalContext
     then, check the status of tdsaPortContext
     then, if status is right, start the discovery
  */

  TI_DBG6(("tiINIDiscoverTargets: portalContext %p\n", portalContext));
  tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    TI_DBG1(("tiINIDiscoverTargets: No tdsaPortContext\n"));
    return tiError;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }

  /* find a right portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  if (PortContextList == agNULL)
  {
    TI_DBG1(("tiINIDiscoverTargets: PortContextList is NULL\n"));
    return tiError;
  }
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if (onePortContext == agNULL)
    {
      TI_DBG1(("tiINIDiscoverTargets: onePortContext is NULL, PortContextList = %p\n", PortContextList));
      return tiError;
    }
    if (onePortContext->tiPortalContext == portalContext && onePortContext->valid == agTRUE)
    {
      TI_DBG6(("tiINIDiscoverTargets: found; oneportContext ID %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tiINIDiscoverTargets: No corresponding tdsaPortContext\n"));
    return tiError;
  }

  TI_DBG2(("tiINIDiscoverTargets: pid %d\n", onePortContext->id));
  if (onePortContext->DiscoveryState == ITD_DSTATE_NOT_STARTED)
  {
    TI_DBG6(("tiINIDiscoverTargets: calling Discovery\n"));
    /* start SAS discovery */
#ifdef FDS_DM
    if (onePortContext->UseDM == agTRUE)
    {
      TI_DBG1(("tiINIDiscoverTargets: calling dmDiscover, pid %d\n", onePortContext->id));
      onePortContext->DiscoveryState = ITD_DSTATE_STARTED;
      dmPortContext = &(onePortContext->dmPortContext);
      dmDiscover(dmRoot, dmPortContext, DM_DISCOVERY_OPTION_FULL_START);
    }
    else
    {
      /* complete discovery */
      onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
      ostiInitiatorEvent(
                         tiRoot,
                         portalContext,
                         agNULL,
                         tiIntrEventTypeDiscovery,
                         tiDiscOK,
                         agNULL
                         );

      return tiSuccess;
    }

#else

#ifdef TD_DISCOVER
    tdsaDiscover(
                 tiRoot,
                 onePortContext,
                 AG_SA_DISCOVERY_TYPE_SAS,
                 TDSA_DISCOVERY_OPTION_FULL_START
                 );
#else
    saDiscover(onePortContext->agRoot, onePortContext->agPortContext, AG_SA_DISCOVERY_TYPE_SAS, onePortContext->discoveryOptions);



#endif
#endif /* FDS_DM */
  }
  else
  {
    TI_DBG1(("tiINIDiscoverTargets: Discovery has started or incorrect initialization; state %d pid 0x%x\n", 
                      onePortContext->DiscoveryState, 
                      onePortContext->id));
    return tiError;
  }

  return tiSuccess;
}

/*****************************************************************************
*! \brief  tiINIGetDeviceHandles
*
*  Purpose: This routine is called to to return the device handles for each
*           device currently available.
*
*  \param  tiRoot:   Pointer to driver Instance.
*  \param  tiPortalContext: Pointer to the portal context instance.
*  \param  agDev[]:  Array to receive pointers to the device handles.
*  \param  maxDevs:  Number of device handles which will fit in array pointed
*                    by agDev.
*  \return:
*    Number of device handle slots present (however, only maxDevs
*    are copied into tiDev[]) which may be greater than the number of
*    handles actually present. In short, returns the number of devices that
*    were found.
*
*  \note:
*
*****************************************************************************/
osGLOBAL bit32
tiINIGetDeviceHandles(
                      tiRoot_t          * tiRoot,
                      tiPortalContext_t * tiPortalContext,
                      tiDeviceHandle_t  * tiDev[],
                      bit32               maxDevs
                      )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  bit32             i;
  bit32             FoundDevices = 0;
  bit32             DeviceIndex = 0;
  bit32             found = agFALSE;
#ifdef  TD_DEBUG_ENABLE
  satDeviceData_t   *pSatDevData;
#endif
#ifdef FDS_DM
  dmRoot_t          *dmRoot = &(tdsaAllShared->dmRoot);
#endif

  TI_DBG2(("tiINIGetDeviceHandles: start\n"));
  TI_DBG2(("tiINIGetDeviceHandles: tiPortalContext %p\n", tiPortalContext));


  if (maxDevs == 0)
  {
    TI_DBG1(("tiINIGetDeviceHandles: maxDevs is 0\n"));
    TI_DBG1(("tiINIGetDeviceHandles: first, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    TI_DBG1(("tiINIGetDeviceHandles: No available tdsaPortContext\n"));
    TI_DBG1(("tiINIGetDeviceHandles: second, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }
  /* find a corresponding portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if(onePortContext == agNULL) continue;

    TI_DBG3(("tiINIGetDeviceHandles: oneportContext pid %d\n", onePortContext->id));
    if (onePortContext->tiPortalContext == tiPortalContext && onePortContext->valid == agTRUE)
    {
      TI_DBG3(("tiINIGetDeviceHandles: found; oneportContext pid %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tiINIGetDeviceHandles: First, No corresponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetDeviceHandles: third, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext == agNULL)
  {
    TI_DBG1(("tiINIGetDeviceHandles: Second, No corressponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetDeviceHandles: fourth, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tiINIGetDeviceHandles: Third, tdsaPortContext is invalid, pid %d\n", onePortContext->id));
    TI_DBG1(("tiINIGetDeviceHandles: fifth, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED &&  onePortContext->DMDiscoveryState == dmDiscFailed)
  {
    TI_DBG1(("tiINIGetDeviceHandles: forth, discovery failed, pid %d\n", onePortContext->id));
    TI_DBG1(("tiINIGetDeviceHandles: sixth, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext->DiscoveryState != ITD_DSTATE_COMPLETED)
  {
    TI_DBG1(("tiINIGetDeviceHandles: discovery not completed\n"));
    TI_DBG1(("tiINIGetDeviceHandles: sixth, returning DISCOVERY_IN_PROGRESS, pid %d\n", onePortContext->id));
    onePortContext->discovery.forcedOK = agTRUE;
    return DISCOVERY_IN_PROGRESS;
  }

  TI_DBG2(("tiINIGetDeviceHandles: pid %d\n", onePortContext->id));

#ifdef FDS_DM
  tdsaUpdateMCN(dmRoot, onePortContext);
#endif

  /* nullify all device handles */
  for (i = 0 ; i < maxDevs ; i++)
  {
    tiDev[i] = agNULL;
  }

  /*
     From the device list, returns only valid devices
  */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;

  TD_ASSERT(DeviceListList, "DeviceListList NULL");
  if (DeviceListList == agNULL  )
  {
    TI_DBG1(("tiINIGetDeviceHandles: DeviceListList == agNULL\n"));
    TI_DBG1(("tiINIGetDeviceHandles: seventh, returning not found, pid %d\n", onePortContext->id));
    return 0;
  }

  while ((DeviceIndex < maxDevs) &&
          DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
#ifdef  TD_DEBUG_ENABLE
    pSatDevData = (satDeviceData_t *)&(oneDeviceData->satDevData);
    if (pSatDevData != agNULL)
    {
      TI_DBG3(("tiINIGetDeviceHandles: device %p satPendingIO %d satNCQMaxIO %d\n",pSatDevData, pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
      TI_DBG3(("tiINIGetDeviceHandles: device %p satPendingNCQIO %d satPendingNONNCQIO %d\n",pSatDevData, pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
    }	  
#endif
    TI_DBG3(("tiINIGetDeviceHandles: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
    TI_DBG3(("tiINIGetDeviceHandles: device AddrHi 0x%08x AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));

    TI_DBG6(("tiINIGetDeviceHandles: handle %p\n",  &(oneDeviceData->tiDeviceHandle)));
    if (oneDeviceData->tdPortContext != onePortContext)
    {
      TI_DBG3(("tiINIGetDeviceHandles: different port\n"));
      DeviceListList = DeviceListList->flink;
    }
    else
    {
#ifdef SATA_ENABLE
      if ((oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext) &&
          ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData)
            || DEVICE_IS_SATA_DEVICE(oneDeviceData) )
          )
#else
      if ((oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext) &&
          ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData) )
          )
#endif
      {
        if (DEVICE_IS_SSP_TARGET(oneDeviceData))
        {
          TI_DBG2(("tiINIGetDeviceHandles: SSP DeviceIndex %d tiDeviceHandle %p\n",  DeviceIndex, &(oneDeviceData->tiDeviceHandle)));
          tiDev[DeviceIndex] = &(oneDeviceData->tiDeviceHandle);
          FoundDevices++;
        }
        else if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
                  &&
                  oneDeviceData->satDevData.IDDeviceValid == agTRUE )
        {
          TI_DBG2(("tiINIGetDeviceHandles: SATA DeviceIndex %d tiDeviceHandle %p\n",  DeviceIndex, &(oneDeviceData->tiDeviceHandle)));
          tiDev[DeviceIndex] = &(oneDeviceData->tiDeviceHandle);
          FoundDevices++;
        }
        else
        {
          TI_DBG3(("tiINIGetDeviceHandles: skip case !!!\n"));
          TI_DBG3(("tiINIGetDeviceHandles: valid %d SSP target %d STP target %d SATA device %d\n", oneDeviceData->valid, DEVICE_IS_SSP_TARGET(oneDeviceData), DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
          TI_DBG3(("tiINIGetDeviceHandles: oneDeviceData->satDevData.IDDeviceValid %d\n", oneDeviceData->satDevData.IDDeviceValid));
          TI_DBG3(("tiINIGetDeviceHandles: registered %d right port %d \n", oneDeviceData->registered, (oneDeviceData->tdPortContext == onePortContext)));
          TI_DBG3(("tiINIGetDeviceHandles: oneDeviceData->tdPortContext %p onePortContext %p\n", oneDeviceData->tdPortContext, onePortContext));
        }
        TI_DBG3(("tiINIGetDeviceHandles: valid FoundDevices %d\n", FoundDevices));
        TI_DBG3(("tiINIGetDeviceHandles: agDevHandle %p\n", oneDeviceData->agDevHandle));
      }
      else
      {
        TI_DBG3(("tiINIGetDeviceHandles: valid %d SSP target %d STP target %d SATA device %d\n", oneDeviceData->valid, DEVICE_IS_SSP_TARGET(oneDeviceData), DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
        TI_DBG3(("tiINIGetDeviceHandles: registered %d right port %d \n", oneDeviceData->registered, (oneDeviceData->tdPortContext == onePortContext)));
        TI_DBG3(("tiINIGetDeviceHandles: oneDeviceData->tdPortContext %p onePortContext %p\n", oneDeviceData->tdPortContext, onePortContext));
      }
      DeviceIndex++;
      DeviceListList = DeviceListList->flink;
    } /* else */
  }

  if (DeviceIndex > maxDevs)
  {
    TI_DBG1(("tiINIGetDeviceHandles: DeviceIndex(%d) >= maxDevs(%d)\n", DeviceIndex, maxDevs));
    FoundDevices = maxDevs;
  }

  TI_DBG1(("tiINIGetDeviceHandles: returning %d found devices, pid %d\n", FoundDevices, onePortContext->id));

  return FoundDevices;
}

/*****************************************************************************
*! \brief  tiINIGetDeviceHandlesForWinIOCTL
*
*  Purpose: This routine is called to to return the device handles for each
*           device currently available, this routine is only for Win IOCTL to display SAS topology.
*
*  \param  tiRoot:   Pointer to driver Instance.
*  \param  tiPortalContext: Pointer to the portal context instance.
*  \param  agDev[]:  Array to receive pointers to the device handles.
*  \param  maxDevs:  Number of device handles which will fit in array pointed
*                    by agDev.
*  \return:
*    Number of device handle slots present (however, only maxDevs
*    are copied into tiDev[]) which may be greater than the number of
*    handles actually present. In short, returns the number of devices that
*    were found.
*
*  \note:
*
*****************************************************************************/
osGLOBAL bit32
tiINIGetDeviceHandlesForWinIOCTL(
                      tiRoot_t          * tiRoot,
                      tiPortalContext_t * tiPortalContext,
                      tiDeviceHandle_t  * tiDev[],
                      bit32               maxDevs
                      )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  bit32             i;
  bit32             FoundDevices = 0;
  bit32             DeviceIndex = 0;
  bit32             found = agFALSE;
#ifdef  TD_DEBUG_ENABLE
  satDeviceData_t   *pSatDevData;
#endif
#ifdef FDS_DM
  dmRoot_t          *dmRoot = &(tdsaAllShared->dmRoot);
#endif

  TI_DBG2(("tiINIGetDeviceHandlesForWinIOCTL: start\n"));
  TI_DBG2(("tiINIGetDeviceHandlesForWinIOCTL: tiPortalContext %p\n", tiPortalContext));


  if (maxDevs == 0)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: maxDevs is 0\n"));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: first, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: No available tdsaPortContext\n"));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: second, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }
  /* find a corresponding portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if(onePortContext == agNULL) continue;

    TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: oneportContext pid %d\n", onePortContext->id));
    if (onePortContext->tiPortalContext == tiPortalContext && onePortContext->valid == agTRUE)
    {
      TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: found; oneportContext pid %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: First, No corresponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: third, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext == agNULL)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: Second, No corressponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: fourth, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: Third, tdsaPortContext is invalid, pid %d\n", onePortContext->id));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: fifth, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext->DiscoveryState == ITD_DSTATE_COMPLETED &&  onePortContext->DMDiscoveryState == dmDiscFailed)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: forth, discovery failed, pid %d\n", onePortContext->id));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: sixth, returning 0\n"));
    /* nullify all device handles */
    for (i = 0 ; i < maxDevs ; i++)
    {
      tiDev[i] = agNULL;
    }
    return 0;
  }

  if (onePortContext->DiscoveryState != ITD_DSTATE_COMPLETED)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: discovery not completed\n"));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: sixth, returning DISCOVERY_IN_PROGRESS, pid %d\n", onePortContext->id));
    onePortContext->discovery.forcedOK = agTRUE;
    return DISCOVERY_IN_PROGRESS;
  }

  TI_DBG2(("tiINIGetDeviceHandlesForWinIOCTL: pid %d\n", onePortContext->id));

#ifdef FDS_DM
  tdsaUpdateMCN(dmRoot, onePortContext);
#endif

  /* nullify all device handles */
  for (i = 0 ; i < maxDevs ; i++)
  {
    tiDev[i] = agNULL;
  }

  /*
     From the device list, returns only valid devices
  */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;

  TD_ASSERT(DeviceListList, "DeviceListList NULL");
  if (DeviceListList == agNULL  )
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: DeviceListList == agNULL\n"));
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: seventh, returning not found, pid %d\n", onePortContext->id));
    return 0;
  }

  while ((DeviceIndex < maxDevs) &&
          DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if(oneDeviceData == agNULL)
    {
	TI_DBG3(("tiINIGetDeviceHandles: OneDeviceData is NULL\n"));
	return 0;
    }
#ifdef  TD_DEBUG_ENABLE
    pSatDevData = (satDeviceData_t *)&(oneDeviceData->satDevData);
    if (pSatDevData != agNULL)
    {
      TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: device %p satPendingIO %d satNCQMaxIO %d\n",pSatDevData, pSatDevData->satPendingIO, pSatDevData->satNCQMaxIO ));
      TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: device %p satPendingNCQIO %d satPendingNONNCQIO %d\n",pSatDevData, pSatDevData->satPendingNCQIO, pSatDevData->satPendingNONNCQIO));
    }
#endif
    TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
    TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: device AddrHi 0x%08x AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));

    TI_DBG6(("tiINIGetDeviceHandlesForWinIOCTL: handle %p\n",  &(oneDeviceData->tiDeviceHandle)));
    if (oneDeviceData->tdPortContext != onePortContext)
    {
      TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: different port\n"));
      DeviceListList = DeviceListList->flink;
    }
    else
    {
#ifdef SATA_ENABLE
      if ((oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext) &&
          ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData)
            || DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_SMP_TARGET(oneDeviceData))
          )
#else
      if ((oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext) &&
          ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
          )
#endif
      {
        if (DEVICE_IS_SSP_TARGET(oneDeviceData))
        {
          TI_DBG2(("tiINIGetDeviceHandlesForWinIOCTL: SSP DeviceIndex %d tiDeviceHandle %p\n",  DeviceIndex, &(oneDeviceData->tiDeviceHandle)));
          tiDev[DeviceIndex] = &(oneDeviceData->tiDeviceHandle);
          DeviceIndex++;
	  FoundDevices++;
        }
        else if ( (DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData))
                  &&
                  oneDeviceData->satDevData.IDDeviceValid == agTRUE )
        {
          TI_DBG2(("tiINIGetDeviceHandlesForWinIOCTL: SATA DeviceIndex %d tiDeviceHandle %p\n",  DeviceIndex, &(oneDeviceData->tiDeviceHandle)));
          tiDev[DeviceIndex] = &(oneDeviceData->tiDeviceHandle);
          DeviceIndex++;
	  FoundDevices++;
        }
        else if (DEVICE_IS_SMP_TARGET(oneDeviceData))
        {
          TI_DBG2(("tiINIGetDeviceHandlesForWinIOCTL: SMP DeviceIndex %d tiDeviceHandle %p\n",  DeviceIndex, &(oneDeviceData->tiDeviceHandle)));
          tiDev[DeviceIndex] = &(oneDeviceData->tiDeviceHandle);
          DeviceIndex++;
	  FoundDevices++;
        }
        else
        {
          TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: skip case !!!\n"));
          TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: valid %d SSP target %d STP target %d SATA device %d\n", oneDeviceData->valid, DEVICE_IS_SSP_TARGET(oneDeviceData), DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
          TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: oneDeviceData->satDevData.IDDeviceValid %d\n", oneDeviceData->satDevData.IDDeviceValid));
          TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: registered %d right port %d \n", oneDeviceData->registered, (oneDeviceData->tdPortContext == onePortContext)));
          TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: oneDeviceData->tdPortContext %p onePortContext %p\n", oneDeviceData->tdPortContext, onePortContext));
        }
        TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: valid FoundDevices %d\n", FoundDevices));
        TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: agDevHandle %p\n", oneDeviceData->agDevHandle));
      }
      else
      {
        TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: valid %d SSP target %d STP target %d SATA device %d\n", oneDeviceData->valid, DEVICE_IS_SSP_TARGET(oneDeviceData), DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
        TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: registered %d right port %d \n", oneDeviceData->registered, (oneDeviceData->tdPortContext == onePortContext)));
        TI_DBG3(("tiINIGetDeviceHandlesForWinIOCTL: oneDeviceData->tdPortContext %p onePortContext %p\n", oneDeviceData->tdPortContext, onePortContext));
      }
      //DeviceIndex++;
      DeviceListList = DeviceListList->flink;
    } /* else */
  }

  if (DeviceIndex > maxDevs)
  {
    TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: DeviceIndex(%d) >= maxDevs(%d)\n", DeviceIndex, maxDevs));
    FoundDevices = maxDevs;
  }

  TI_DBG1(("tiINIGetDeviceHandlesForWinIOCTL: returning %d found devices, pid %d\n", FoundDevices, onePortContext->id));

  return FoundDevices;
}


/*****************************************************************************
*! \brief  tiINIGetDeviceInfo
*
*  Purpose: This routine is called by the OS Layer find out
*           the name associated with the device and where
*           it is mapped (address1 and address2).
*
*  \param  tiRoot:          Pointer to driver Instance.
*  \param  tiDeviceHandle:  device handle associated with the device
*  \param  tiDeviceInfo:    pointer to structure where the information
*                           needs to be copied.
*  \return:
*          tiSuccess - successful
*          tiInvalidHandle - device handle passed is not a valid handle.
*
*  \note:
*
*****************************************************************************/
osGLOBAL bit32
tiINIGetDeviceInfo(
                   tiRoot_t            *tiRoot,
                   tiDeviceHandle_t    *tiDeviceHandle,
                   tiDeviceInfo_t      *tiDeviceInfo)
{
  tdsaDeviceData_t     *oneDeviceData = agNULL;
  satDeviceData_t      *pSatDevData = agNULL;
  bit8                 id_limit[5];
  bit8                 SN_id_limit[25];
  agsaRoot_t           *agRoot = agNULL;

  TI_DBG6(("tiINIGetDeviceInfo: start \n"));

  if (tiDeviceHandle == agNULL)
  {
    TI_DBG6(("tiINIGetDeviceInfo: tiDeviceHandle NULL\n"));
    return tiInvalidHandle;
  }

  if (tiDeviceHandle->tdData == agNULL)
  {
    TI_DBG6(("tiINIGetDeviceInfo: ^^^^^^^^^ tiDeviceHandle->tdData NULL\n"));
    return tiInvalidHandle;
  }
  else
  {

    oneDeviceData = (tdsaDeviceData_t *)(tiDeviceHandle->tdData);
    agRoot = oneDeviceData->agRoot;
    TI_DBG6(("tiINIGetDeviceInfo: ^^^^^^^^^ tiDeviceHandle->tdData NOT NULL\n"));
  }
  if (oneDeviceData == agNULL)
  {
    TI_DBG6(("tiINIGetDeviceInfo: ^^^^^^^^^ oneDeviceData NULL\n"));
    return tiInvalidHandle;
  }


  /* filling in the link rate */
  if (oneDeviceData->registered == agTRUE)
  {
    tiDeviceInfo->info.devType_S_Rate = oneDeviceData->agDeviceInfo.devType_S_Rate;
  }
  else
  {
    tiDeviceInfo->info.devType_S_Rate = (bit8)(oneDeviceData->agDeviceInfo.devType_S_Rate & 0x0f);
  }

  /* just returning local and remote SAS address; doesn't have a name for SATA device, returns identify device data */
  if (DEVICE_IS_SATA_DEVICE(oneDeviceData) && (oneDeviceData->directlyAttached == agTRUE))
  {
    osti_memset(&id_limit, 0, sizeof(id_limit));
    osti_memset(&SN_id_limit, 0, sizeof(SN_id_limit));

    /* SATA signature 0xABCD */
    id_limit[0] = 0xA;
    id_limit[1] = 0xB;
    id_limit[2] = 0xC;
    id_limit[3] = 0xD;

    pSatDevData = &(oneDeviceData->satDevData);
    if (pSatDevData->satNCQ == agTRUE)
    {
      id_limit[4] = (bit8)pSatDevData->satNCQMaxIO;
    }
    else
    {
      /* no NCQ */
      id_limit[4] = 1;
    }

    osti_memcpy(&SN_id_limit, &(oneDeviceData->satDevData.satIdentifyData.serialNumber), 20);
    osti_memcpy(&(SN_id_limit[20]), &id_limit, 5);
    osti_memcpy(oneDeviceData->satDevData.SN_id_limit, SN_id_limit, 25);
    /* serialNumber, 20 bytes + ABCD + NCQ LENGTH ; modelNumber, 40 bytes */
//  tiDeviceInfo->remoteName    = (char *)&(oneDeviceData->satDevData.satIdentifyData.serialNumber);
    tiDeviceInfo->remoteName    = (char *)oneDeviceData->satDevData.SN_id_limit;
    tiDeviceInfo->remoteAddress = (char *)&(oneDeviceData->satDevData.satIdentifyData.modelNumber);
//    TI_DBG1(("tiINIGetDeviceInfo: SATA device remote hi 0x%08x lo 0x%08x\n", oneDeviceData->tdPortContext->sasRemoteAddressHi, oneDeviceData->tdPortContext->sasRemoteAddressLo));
//    tdhexdump("tiINIGetDeviceInfo remotename", (bit8 *)&(oneDeviceData->satDevData.satIdentifyData.serialNumber), 20);
//    tdhexdump("tiINIGetDeviceInfo new name", (bit8 *)&(SN_id_limit), sizeof(SN_id_limit));
//    tdhexdump("tiINIGetDeviceInfo remoteaddress", (bit8 *)&(oneDeviceData->satDevData.satIdentifyData.modelNumber),40);
    tiDeviceInfo->osAddress1 = 25;
    tiDeviceInfo->osAddress2 = 40;

  }
  else if (DEVICE_IS_STP_TARGET(oneDeviceData))
  {
    /* serialNumber, 20 bytes; modelNumber, 40 bytes */
    tiDeviceInfo->remoteName    = (char *)&(oneDeviceData->satDevData.satIdentifyData.serialNumber);
    tiDeviceInfo->remoteAddress = (char *)&(oneDeviceData->satDevData.satIdentifyData.modelNumber);
//    TI_DBG1(("tiINIGetDeviceInfo: SATA device remote hi 0x%08x lo 0x%08x\n", oneDeviceData->tdPortContext->sasRemoteAddressHi, oneDeviceData->tdPortContext->sasRemoteAddressLo));
//    tdhexdump("tiINIGetDeviceInfo remotename", (bit8 *)&(oneDeviceData->satDevData.satIdentifyData.serialNumber), 20);
//    tdhexdump("tiINIGetDeviceInfo remoteaddress", (bit8 *)&(oneDeviceData->satDevData.satIdentifyData.modelNumber),40);
    tiDeviceInfo->osAddress1 = 20;
    tiDeviceInfo->osAddress2 = 40;
  }
  else
  {
    tiDeviceInfo->remoteName    = (char *)&(oneDeviceData->SASAddressID.sasAddressHi);
    tiDeviceInfo->remoteAddress = (char *)&(oneDeviceData->SASAddressID.sasAddressLo);
    TI_DBG1(("tiINIGetDeviceInfo: SAS device remote hi 0x%08x lo 0x%08x\n", oneDeviceData->tdPortContext->sasRemoteAddressHi, oneDeviceData->tdPortContext->sasRemoteAddressLo));
    tiDeviceInfo->osAddress1 = 4;
    tiDeviceInfo->osAddress2 = 4;
  }

  tiDeviceInfo->localName     = (char *)&(oneDeviceData->tdPortContext->sasLocalAddressHi);
  tiDeviceInfo->localAddress  = (char *)&(oneDeviceData->tdPortContext->sasLocalAddressLo);

  TI_DBG6(("tiINIGetDeviceInfo: local hi 0x%08x lo 0x%08x\n", oneDeviceData->tdPortContext->sasLocalAddressHi, oneDeviceData->tdPortContext->sasLocalAddressLo));

  if (oneDeviceData->agDevHandle == agNULL)
  {
    TI_DBG1(("tiINIGetDeviceInfo: Error! oneDeviceData->agDevHandle is NULL"));
    return tiError;
  }
  else
  {
    saGetDeviceInfo(agRoot, agNULL, 0, 0,oneDeviceData->agDevHandle);
  }    
    

  return tiSuccess;
}

/*****************************************************************************
*! \brief  tiINILogin
*
*  Purpose: This function is called to request that the Transport Dependent
*           Layer initiates login for a specific target.
*
*  \param tiRoot:          Pointer to driver Instance.
*  \param tiDeviceHandle:  Pointer to a target device handle discovered
*                          following the discovery.
*
*  \return:
*        tiSuccess       Login initiated.
*        tiError         Login failed.
*        tiBusy          Login can not be initiated at this time.
*        tiNotSupported  This API is currently not supported by this
*                        Transport Layer
*
*
*****************************************************************************/
osGLOBAL bit32
tiINILogin(
           tiRoot_t            *tiRoot,
           tiDeviceHandle_t    *tiDeviceHandle
           )
{
  TI_DBG6(("tiINILogin: start\n"));
  return tiNotSupported;
}

/*****************************************************************************
*! \brief  tiINILogout
*
*  Purpose: This function is called to request that the Transport Dependent
*           Layer initiates logout for a specific target from the previously
*           successful login through tiINILogin() call.
*
*  \param   tiRoot      :  Pointer to the OS Specific module allocated tiRoot_t
*                          instance.
*  \param tiDeviceHandle:  Pointer to a target device handle.
*
*  \return:
*         tiSuccess       Logout initiated.
*         tiError         Logout failed.
*         tiBusy          Logout can not be initiated at this time.
*         tiNotSupported  This API is currently not supported by this
*                         Transport Layer
*
*
*****************************************************************************/
osGLOBAL bit32
tiINILogout(
            tiRoot_t            *tiRoot,
            tiDeviceHandle_t    *tiDeviceHandle
            )
{
  TI_DBG6(("tiINILogout: start\n"));
  return tiNotSupported;
}
/*****************************************************************************
*! \brief  tiINIGetExpander
*
*
*  \note:
*
*****************************************************************************/
osGLOBAL bit32
tiINIGetExpander(
                  tiRoot_t          * tiRoot,
                  tiPortalContext_t * tiPortalContext,
                  tiDeviceHandle_t  * tiDev, 
                  tiDeviceHandle_t  ** tiExp
                 )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaDeviceData_t  *oneTargetDeviceData = agNULL;  
  tdsaDeviceData_t  *oneExpanderDeviceData = agNULL;  
  bit32             found = agFALSE;
  oneTargetDeviceData = (tdsaDeviceData_t *)tiDev->tdData;
  if (oneTargetDeviceData == agNULL)
  {
    TI_DBG1(("tiINIGetExpander: oneTargetDeviceData is NULL\n"));
    return tiError;  
  }
  tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    TI_DBG1(("tiINIGetExpander: No available tdsaPortContext\n"));
    TI_DBG1(("tiINIGetExpander: second, returning 0\n"));
    return tiError;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }
  /* find a corresponding portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    TI_DBG3(("tiINIGetExpander: oneportContext pid %d\n", onePortContext->id));
    if (onePortContext->tiPortalContext == tiPortalContext && onePortContext->valid == agTRUE)
    {
      TI_DBG3(("tiINIGetExpander: found; oneportContext pid %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }
  if (found == agFALSE)
  {
    TI_DBG1(("tiINIGetExpander: First, No corresponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetExpander: third, returning 0\n"));
    return tiError;
  }
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tiINIGetExpander: Second, No corressponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetExpander: fourth, returning 0\n"));
    return tiError;
  }
  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tiINIGetExpander: Third, tdsaPortContext is invalid, pid %d\n", onePortContext->id));
    TI_DBG1(("tiINIGetExpander: fifth, returning 0\n"));
    return tiError;
  }
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while ( DeviceListList != &(tdsaAllShared->MainDeviceList) )
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData->tdPortContext != onePortContext)
    { 
      TI_DBG3(("tiINIGetExpander: different port\n"));
      DeviceListList = DeviceListList->flink;
    }
    else
    {
      if (oneDeviceData == oneTargetDeviceData)
      {
        oneExpanderDeviceData = oneDeviceData->ExpDevice;
        if (oneExpanderDeviceData == agNULL)
        {
          TI_DBG1(("tiINIGetExpander: oneExpanderDeviceData is NULL\n"));
          return tiError;  
        } 
        *tiExp = &(oneExpanderDeviceData->tiDeviceHandle);
        return tiSuccess;      
      }                  
      DeviceListList = DeviceListList->flink;    
    }
  }      
  return tiError;
}


osGLOBAL void tiIniGetDirectSataSasAddr(tiRoot_t * tiRoot, bit32 phyId, bit8 **sasAddressHi, bit8 **sasAddressLo)
{
	tdsaRoot_t		  *tdsaRoot 	   = (tdsaRoot_t *) tiRoot->tdData;
	tdsaContext_t	  *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
	agsaRoot_t	  *agRoot = &tdsaAllShared->agRootInt;
        tiIOCTLPayload_wwn_t   agIoctlPayload;
   	bit8 nvmDev;
	bit32 status;
	int i;
	agIoctlPayload.Length = 4096;
	agIoctlPayload.Reserved = 0;
	agIoctlPayload.MinorFunction = IOCTL_MN_NVMD_GET_CONFIG;
	agIoctlPayload.MajorFunction = IOCTL_MJ_NVMD_GET;
	  tiCOMDelayedInterruptHandler(tiRoot, 0,1, tiNonInterruptContext);
	if(tiIS_SPC(agRoot))
 	{
	  nvmDev = 4;
	  status = tdsaNVMDGetIoctl(tiRoot, (tiIOCTLPayload_t *)&agIoctlPayload, agNULL, agNULL, &nvmDev);
	}
	else
	{
          nvmDev = 1;
	  status = tdsaNVMDGetIoctl(tiRoot, (tiIOCTLPayload_t *)&agIoctlPayload, agNULL, agNULL, &nvmDev);
 	}
	if(status == IOCTL_CALL_FAIL)
	{
#if !(defined(__FreeBSD__))
	   printk("Error getting Adapter WWN\n");
#else
	   printf("Error getting Adapter WWN\n");
#endif
	   return;
	}
 	for(i=0; i< TD_MAX_NUM_PHYS; i++)
	{
	   *(bit32 *)(tdsaAllShared->Ports[i].SASID.sasAddressHi) = *(bit32 *)&agIoctlPayload.FunctionSpecificArea[0];
	   *(bit32 *)(tdsaAllShared->Ports[i].SASID.sasAddressLo) = *(bit32 *)&agIoctlPayload.FunctionSpecificArea[4];
	TI_DBG3(("SAS AddressHi is 0x%x\n",  *(bit32 *)(tdsaAllShared->Ports[i].SASID.sasAddressHi)));
	TI_DBG3(("SAS AddressLo is 0x%x\n",  *(bit32 *)(tdsaAllShared->Ports[i].SASID.sasAddressLo)));
	}
	*sasAddressHi = tdsaAllShared->Ports[phyId].SASID.sasAddressHi;
	*sasAddressLo = tdsaAllShared->Ports[phyId].SASID.sasAddressLo;
}
osGLOBAL tiDeviceHandle_t *
tiINIGetExpDeviceHandleBySasAddress(
                      tiRoot_t          * tiRoot,
                      tiPortalContext_t * tiPortalContext,
					  bit32 sas_addr_hi,
					  bit32 sas_addr_lo,
					  bit32               maxDevs
                      )

{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  //bit32             i;
  //bit32             FoundDevices = 0;
  bit32             DeviceIndex = 0;
  bit32             found = agFALSE;

  
  TI_DBG2(("tiINIGetExpDeviceHandleBySasAddress: start\n"));
  TI_DBG2(("tiINIGetExpDeviceHandleBySasAddress: tiPortalContext %p\n", tiPortalContext));
  
  
  if (maxDevs == 0)
  {
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: maxDevs is 0\n"));
   
    return agNULL;
  }

  tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: No available tdsaPortContext\n"));
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: second, returning 0\n")); 
    return agNULL;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }
  /* find a corresponding portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
   
  if(PortContextList == agNULL)
  {
	TI_DBG6(("tiINIGetExpDeviceHandleBySasAddress: PortContextList is NULL!!\n"));
	return agNULL;
  }
  
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
	 
	if(onePortContext == agNULL)
	{
	  TI_DBG6(("tiINIGetExpDeviceHandleBySasAddress: onePortContext is NULL!!\n"));
	  return agNULL;
    }
	
    TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: oneportContext pid %d\n", onePortContext->id));
    if (onePortContext->tiPortalContext == tiPortalContext && onePortContext->valid == agTRUE)
    {
      TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: found; oneportContext pid %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
	 
	if(PortContextList != agNULL)
	{
      PortContextList = PortContextList->flink;
	}
	
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: First, No corresponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: third, returning 0\n"));
    /* nullify all device handles */    
    return agNULL;
  }
  
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: Second, No corressponding tdsaPortContext\n"));
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: fourth, returning 0\n"));
    /* nullify all device handles */    
    return agNULL;
  }

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: Third, tdsaPortContext is invalid, pid %d\n", onePortContext->id));
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: fifth, returning 0\n"));
    return agNULL;
  }
  
   
  TI_DBG2(("tiINIGetExpDeviceHandleBySasAddress: pid %d\n", onePortContext->id));
  

  /* to do: check maxdev and length of Mainlink */ 
  /* 
     From the device list, returns only valid devices
  */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
   
  if(DeviceListList == agNULL)
  {
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: DeviceListList == agNULL\n"));
    TI_DBG1(("tiINIGetExpDeviceHandleBySasAddress: seventh, returning not found, pid %d\n", onePortContext->id));
    return agNULL;
  }
  
  while ((DeviceIndex < maxDevs) &&
          DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
	 
	if(oneDeviceData == agNULL)
	{
	  TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: oneDeviceData is NULL!!\n"));
	  return agNULL;
	}


    TI_DBG6(("tiINIGetExpDeviceHandleBySasAddress: handle %p\n",  &(oneDeviceData->tiDeviceHandle)));
    if (oneDeviceData->tdPortContext != onePortContext)
    { 
      TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: different port\n"));
	   
	  if(DeviceListList != agNULL)
	  {
        DeviceListList = DeviceListList->flink;
	  }
	  
    }
    else
    {

      if ((oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext) &&
          (  
          (oneDeviceData->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE) ||
          (oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE) ||
           DEVICE_IS_SMP_TARGET(oneDeviceData)
          )
         )

      {
	   
		if(oneDeviceData->SASAddressID.sasAddressLo == sas_addr_lo && oneDeviceData->SASAddressID.sasAddressHi == sas_addr_hi)
		{
		  //TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: valid FoundDevices %d\n", FoundDevices));
	      TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: agDevHandle %p\n", oneDeviceData->agDevHandle));
          TI_DBG3(("tiINIGetExpDeviceHandleBySasAddress: Matched sas address:  low %x and high %x\n", oneDeviceData->SASAddressID.sasAddressLo,  oneDeviceData->SASAddressID.sasAddressHi));
 		  return &(oneDeviceData->tiDeviceHandle);
		}
      }
      DeviceIndex++;
      DeviceListList = DeviceListList->flink;
    } /* else */
  }

  return agNULL;
}




#ifdef TD_DISCOVER
/*****************************************************************************
*! \brief  tdsaDiscover
*
*  Purpose:  This function is called to trigger topology discovery within a
*            portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   type: Type of discovery. It can be SAS or SATA.
*  \param   option: discovery option. It can be Full or Incremental discovery.
*
*  \return:
*           tiSuccess    Discovery initiated.
*           tiError      Discovery could not be initiated at this time.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaDiscover(
             tiRoot_t          *tiRoot,
             tdsaPortContext_t *onePortContext,
             bit32             type,
             bit32             option
             )

{
  bit32             ret = tiError;
  TI_DBG3(("tdsaDiscover: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaDiscover: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return ret;
  }

  switch ( option )
  {
  case TDSA_DISCOVERY_OPTION_FULL_START:
    TI_DBG3(("tdsaDiscover: full\n"));
    onePortContext->discovery.type = TDSA_DISCOVERY_OPTION_FULL_START;
    if ( type == TDSA_DISCOVERY_TYPE_SAS )
    {
      ret = tdsaSASFullDiscover(tiRoot, onePortContext);
    }
#ifdef SATA_ENABLE
    else if ( type == TDSA_DISCOVERY_TYPE_SATA )
    {
      if (onePortContext->discovery.status == DISCOVERY_SAS_DONE)
      {
        ret = tdsaSATAFullDiscover(tiRoot, onePortContext);
      }
    }
#endif
    break;
  case TDSA_DISCOVERY_OPTION_INCREMENTAL_START:
    TI_DBG3(("tdsaDiscover: incremental\n"));
    onePortContext->discovery.type = TDSA_DISCOVERY_OPTION_INCREMENTAL_START;
    if ( type == TDSA_DISCOVERY_TYPE_SAS )
    {
      TI_DBG3(("tdsaDiscover: incremental SAS\n"));
      ret = tdsaSASIncrementalDiscover(tiRoot, onePortContext);
    }
#ifdef SATA_ENABLE
    else if ( type == TDSA_DISCOVERY_TYPE_SATA )
    {
      if (onePortContext->discovery.status == DISCOVERY_SAS_DONE)
      {
        TI_DBG3(("tdsaDiscover: incremental SATA\n"));
        ret = tdsaSATAIncrementalDiscover(tiRoot, onePortContext);
      }
    }
#endif
    break;
  case TDSA_DISCOVERY_OPTION_ABORT:
    TI_DBG1(("tdsaDiscover: abort\n"));
    break;
  default:
    break;

  }
  if (ret != tiSuccess)
  {
    TI_DBG1(("tdsaDiscover: fail, error 0x%x\n", ret));
  }
  return ret;
}

/*****************************************************************************
*! \brief  tdsaSASFullDiscover
*
*  Purpose:  This function is called to trigger full SAS topology discovery
*            within a portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           tiSuccess    Discovery initiated.
*           tiError      Discovery could not be initiated at this time.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaSASFullDiscover(
                    tiRoot_t          *tiRoot,
                    tdsaPortContext_t *onePortContext
                    )
{
  tdsaRoot_t           *tdsaRoot       = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared  = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t     *oneDeviceData  = agNULL;
  tdList_t             *DeviceListList;
  int                  i, j;
  bit8                 portMaxRate;
  TI_DBG3(("tdsaSASFullDiscover: start\n"));
  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASFullDiscover: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return tiError;
  }
  /*
    1. abort all IO; may need a new LL API since TD does not queue IO's
    2. initializes(or invalidate) devices belonging to the port
    3. onePortContext->DiscoveryState == ITD_DSTATE_STARTED
    4. add directly connected one; if directed-SAS, spin-up
    5. tdsaSASUpStreamDiscoverStart(agRoot, pPort, pDevice)
  */
  /*
    invalidate all devices belonging to the portcontext except direct attached SAS/SATA
  */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG3(("tdsaSASFullDiscover: STARTED loop id %d\n", oneDeviceData->id));
    TI_DBG3(("tdsaSASFullDiscover: STARTED loop sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG3(("tdsaSASFullDiscover: STARTED loop sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
    if (oneDeviceData->tdPortContext == onePortContext &&
        (onePortContext->nativeSATAMode == agFALSE && onePortContext->directAttatchedSAS == agFALSE)             )

    {
      TI_DBG3(("tdsaSASFullDiscover: invalidate\n"));
      oneDeviceData->valid = agFALSE;
      oneDeviceData->processed = agFALSE;
    }
    else
    {
      TI_DBG3(("tdsaSASFullDiscover: not invalidate\n"));
      /* no changes */
    }
    DeviceListList = DeviceListList->flink;
  }

  onePortContext->DiscoveryState = ITD_DSTATE_STARTED;
  /* nativeSATAMode is set in ossaHwCB() in link up */
  if (onePortContext->nativeSATAMode == agFALSE) /* default: SAS and SAS/SATA mode */
  {
    if (SA_IDFRM_GET_DEVICETTYPE(&onePortContext->sasIDframe) == SAS_END_DEVICE &&
        SA_IDFRM_IS_SSP_TARGET(&onePortContext->sasIDframe) )
    {
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        if (onePortContext->PhyIDList[i] == agTRUE)
        {
         
           for (j=0;j<TD_MAX_NUM_NOTIFY_SPINUP;j++)
           {
             saLocalPhyControl(onePortContext->agRoot, agNULL, tdsaRotateQnumber(tiRoot, agNULL), i, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
           }
           break;
        }
      }
    }
    /*
      add the device
      1. add device in TD layer
      2. call saRegisterNewDevice
      3. update agDevHandle in ossaDeviceRegistrationCB()
    */
    portMaxRate = onePortContext->LinkRate;
    oneDeviceData = tdsaPortSASDeviceAdd(
                                         tiRoot,
                                         onePortContext,
                                         onePortContext->sasIDframe,
                                         agFALSE,
                                         portMaxRate,
                                         IT_NEXUS_TIMEOUT,
                                         0,
                                         SAS_DEVICE_TYPE,
                                         agNULL,
                                         0xFF
                                         );
    if (oneDeviceData)
    {
      if (oneDeviceData->registered == agFALSE)
      {
        /*
          set the timer and wait till the device(directly attached. eg Expander) to be registered.
         Then, in tdsaDeviceRegistrationTimerCB(), tdsaSASUpStreamDiscoverStart() is called
        */
        tdsaDeviceRegistrationTimer(tiRoot, onePortContext, oneDeviceData);
      }
      else
      {
        tdsaSASUpStreamDiscoverStart(tiRoot, onePortContext, oneDeviceData);
      }
    }
#ifdef REMOVED
    // temp testing code
    tdsaReportManInfoSend(tiRoot, oneDeviceData);
    //end temp testing code
#endif
  }
  else /* SATAOnlyMode*/
  {
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiSuccess);
  }

  return tiSuccess;
}

/*****************************************************************************
*! \brief  tdsaSASUpStreamDiscoverStart
*
*  Purpose:  This function is called to trigger upstream traverse in topology
*            within a portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASUpStreamDiscoverStart(
                             tiRoot_t             *tiRoot,
                             tdsaPortContext_t    *onePortContext,
                             tdsaDeviceData_t     *oneDeviceData
                             )
{
  tdsaExpander_t        *oneExpander;

  TI_DBG3(("tdsaSASUpStreamDiscoverStart: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASUpStreamDiscoverStart: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }

  /*
    1. update discovery state to UP_STREAM
    2. if (expander) add it
    3. tdsaSASUpStreamDiscovering

  */
  onePortContext->discovery.status = DISCOVERY_UP_STREAM;
  if (
      (oneDeviceData->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
       ||
      (oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE)
      )
  {
    oneExpander = tdssSASDiscoveringExpanderAlloc(tiRoot, onePortContext, oneDeviceData);
    if ( oneExpander != agNULL)
    {
      /* (2.2.1) Add to discovering list */
      tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, oneExpander);
    }
    else
    {
      TI_DBG1(("tdsaSASUpStreamDiscoverStart: failed to allocate expander or discovey aborted\n"));
      return;
    }
  }

  tdsaSASUpStreamDiscovering(tiRoot, onePortContext, oneDeviceData);

  return;
}

/*****************************************************************************
*! \brief  tdsaSASUpStreamDiscovering
*
*  Purpose:  For each expander in the expander list, this function sends SMP to
*            find information for discovery and calls
*            tdsaSASDownStreamDiscoverStart() function.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASUpStreamDiscovering(
                           tiRoot_t             *tiRoot,
                           tdsaPortContext_t    *onePortContext,
                           tdsaDeviceData_t     *oneDeviceData
                           )
{
  tdList_t          *ExpanderList;
  tdsaExpander_t    *oneNextExpander = agNULL;

  TI_DBG3(("tdsaSASUpStreamDiscovering: start\n"));
  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASUpStreamDiscovering: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
  /*
    1. find the next expander
    2. if (there is next expander) send report general with saSMPStart
       else tdsaSASDownStreamDiscoverStart

  */
  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  if (TDLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdsaSASUpStreamDiscovering: should be the end\n"));
    oneNextExpander = agNULL;
  }
  else
  {
    TDLIST_DEQUEUE_FROM_HEAD(&ExpanderList, &(onePortContext->discovery.discoveringExpanderList));
    oneNextExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    TDLIST_ENQUEUE_AT_HEAD(&(oneNextExpander->linkNode), &(onePortContext->discovery.discoveringExpanderList));
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);

    TI_DBG3(("tdssSASDiscoveringExpander tdsaSASUpStreamDiscovering: dequeue head\n"));
    TI_DBG3(("tdsaSASUpStreamDiscovering: expander id %d\n", oneNextExpander->id));
  }

  if (oneNextExpander != agNULL)
  {
    tdsaReportGeneralSend(tiRoot, oneNextExpander->tdDevice);
  }
  else
  {
    TI_DBG3(("tdsaSASUpStreamDiscovering: No more expander list\n"));
    tdsaSASDownStreamDiscoverStart(tiRoot, onePortContext, oneDeviceData);
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaSASDownStreamDiscoverStart
*
*  Purpose:  This function is called to trigger downstream traverse in topology
*            within a portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASDownStreamDiscoverStart(
                               tiRoot_t             *tiRoot,
                               tdsaPortContext_t    *onePortContext,
                               tdsaDeviceData_t     *oneDeviceData
                               )
{
  tdsaExpander_t        *oneExpander;
  tdsaExpander_t        *UpStreamExpander;
  TI_DBG3(("tdsaSASDownStreamDiscoverStart: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASDownStreamDiscoverStart: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
  /*
    1. update discover state
    2. if (expander is root) add it
       else just add it
    3. tdsaSASDownStreamDiscovering

  */
  /* set discovery status */
  onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;

  TI_DBG3(("tdsaSASDownStreamDiscoverStart: pPort=%p pDevice=%p\n", onePortContext, oneDeviceData));

  /* If it's an expander */
  if ( (oneDeviceData->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
       || (oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE))
  {
    oneExpander = oneDeviceData->tdExpander;
    UpStreamExpander = oneExpander->tdUpStreamExpander;

    /* If the two expanders are the root of two edge sets; sub-to-sub */
    if ( (UpStreamExpander != agNULL) && ( UpStreamExpander->tdUpStreamExpander == oneExpander ) )
    {
      TI_DBG3(("tdsaSASDownStreamDiscoverStart: Root found pExpander=%p pUpStreamExpander=%p\n",
               oneExpander, UpStreamExpander));
      //Saves the root expander
      onePortContext->discovery.RootExp = oneExpander;
      TI_DBG3(("tdsaSASDownStreamDiscoverStart: Root exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdsaSASDownStreamDiscoverStart: Root exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

      /* reset up stream inform for pExpander */
      oneExpander->tdUpStreamExpander = agNULL;
      /* Add the pExpander to discovering list */
      tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, oneExpander);

      /* reset up stream inform for oneExpander */
      UpStreamExpander->tdUpStreamExpander = agNULL;
      /* Add the UpStreamExpander to discovering list */
      tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, UpStreamExpander);
    }
    /* If the two expanders are not the root of two edge sets. eg) one root */
    else
    {
      //Saves the root expander
      onePortContext->discovery.RootExp = oneExpander;

      TI_DBG3(("tdsaSASDownStreamDiscoverStart: NO Root pExpander=%p\n", oneExpander));
      TI_DBG3(("tdsaSASDownStreamDiscoverStart: Root exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdsaSASDownStreamDiscoverStart: Root exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

      /* (2.2.2.1) Add the pExpander to discovering list */
      tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, oneExpander);
    }
  }

  /* Continue down stream discovering */
  tdsaSASDownStreamDiscovering(tiRoot, onePortContext, oneDeviceData);

  return;
}

/*****************************************************************************
*! \brief  tdsaSASDownStreamDiscovering
*
*  Purpose:  For each expander in the expander list, this function sends SMP to
*            find information for discovery and calls
*            tdsaSASDownStreamDiscoverStart() function.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASDownStreamDiscovering(
                               tiRoot_t             *tiRoot,
                               tdsaPortContext_t    *onePortContext,
                               tdsaDeviceData_t     *oneDeviceData
                               )
{
  tdsaExpander_t    *NextExpander = agNULL;
  tdList_t          *ExpanderList;

  TI_DBG3(("tdsaSASDownStreamDiscovering: start\n"));

  TI_DBG3(("tdsaSASDownStreamDiscovering: pPort=%p  pDevice=%p\n", onePortContext, oneDeviceData));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASDownStreamDiscovering: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }

  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  if (TDLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdsaSASDownStreamDiscovering: should be the end\n"));
    NextExpander = agNULL;
  }
  else
  {
    TDLIST_DEQUEUE_FROM_HEAD(&ExpanderList, &(onePortContext->discovery.discoveringExpanderList));;
    NextExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    TDLIST_ENQUEUE_AT_HEAD(&(NextExpander->linkNode), &(onePortContext->discovery.discoveringExpanderList));;
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdssSASDiscoveringExpander tdsaSASDownStreamDiscovering: dequeue head\n"));
    TI_DBG3(("tdsaSASDownStreamDiscovering: expander id %d\n", NextExpander->id));

  }

  /* If there is an expander for continue discoving */
  if ( NextExpander != agNULL)
  {
    TI_DBG3(("tdsaSASDownStreamDiscovering: Found pNextExpander=%p\n, discoveryStatus=0x%x",
             NextExpander, onePortContext->discovery.status));

    switch (onePortContext->discovery.status)
    {
      /* If the discovery status is DISCOVERY_DOWN_STREAM */
    case DISCOVERY_DOWN_STREAM:
      /* Send report general for the next expander */
      TI_DBG3(("tdsaSASDownStreamDiscovering: DownStream pNextExpander->pDevice=%p\n", NextExpander->tdDevice));
      tdsaReportGeneralSend(tiRoot, NextExpander->tdDevice);
      break;
      /* If the discovery status is DISCOVERY_CONFIG_ROUTING */
    case DISCOVERY_CONFIG_ROUTING:
    case DISCOVERY_REPORT_PHY_SATA:

      /* set discovery status */
      onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;

      TI_DBG3(("tdsaSASDownStreamDiscovering: pPort->discovery.status=DISCOVERY_CONFIG_ROUTING, nake it DOWN_STREAM\n"));
      /* If not the last phy */
      if ( NextExpander->discoveringPhyId < NextExpander->tdDevice->numOfPhys )
      {
        TI_DBG3(("tdsaSASDownStreamDiscovering: pNextExpander->discoveringPhyId=0x%x pNextExpander->pDevice->numOfPhys=0x%x.  Send More Discover\n",
                 NextExpander->discoveringPhyId, NextExpander->tdDevice->numOfPhys));
        /* Send discover for the next expander */
        tdsaDiscoverSend(tiRoot, NextExpander->tdDevice);
        }
      /* If it's the last phy */
      else
      {
        TI_DBG3(("tdsaSASDownStreamDiscovering: Last Phy, remove expander%p  start DownStream=%p\n",
                 NextExpander, NextExpander->tdDevice));
        tdssSASDiscoveringExpanderRemove(tiRoot, onePortContext, NextExpander);
        tdsaSASDownStreamDiscovering(tiRoot, onePortContext, NextExpander->tdDevice);
      }
      break;

    default:
      TI_DBG3(("tdsaSASDownStreamDiscovering: *** Unknown pPort->discovery.status=0x%x\n", onePortContext->discovery.status));
    }
  }
  /* If no expander for continue discoving */
  else
  {
    TI_DBG3(("tdsaSASDownStreamDiscovering: No more expander DONE\n"));
    /* discover done */
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiSuccess);
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaCleanAllExp
*
*  Purpose:  This function cleans up expander data structures after discovery
*            is complete.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaCleanAllExp(
                tiRoot_t                 *tiRoot,
                tdsaPortContext_t        *onePortContext
                )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *ExpanderList;
  tdsaExpander_t    *tempExpander;
  tdsaPortContext_t *tmpOnePortContext = onePortContext;

  TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: start\n"));

  TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: before all clean up\n"));
  tdsaDumpAllFreeExp(tiRoot);

  /* clean up UpdiscoveringExpanderList*/
  TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: clean discoveringExpanderList\n"));
  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  if (!TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
    while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
    {
      tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
      TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: exp addrHi 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: exp addrLo 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressLo));
      /* putting back to the free pool */
      tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
      TDLIST_DEQUEUE_THIS(&(tempExpander->linkNode));
      TDLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(tdsaAllShared->freeExpanderList));

      if (TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
      {
        tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
        break;
      }
      else
      {
        tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
      }
      ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;

//      ExpanderList = ExpanderList->flink;
    }
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: empty discoveringExpanderList\n"));
  }

  /* reset UpdiscoveringExpanderList */
  TDLIST_INIT_HDR(&(tmpOnePortContext->discovery.UpdiscoveringExpanderList));

  TI_DBG3(("tdssSASDiscoveringExpander tdsaCleanAllExp: after all clean up\n"));
  tdsaDumpAllFreeExp(tiRoot);

  return;
}

/*****************************************************************************
*! \brief  tdsaFreeAllExp
*
*  Purpose:  This function frees up expander data structures as a part of
*            soft reset.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaFreeAllExp(
                tiRoot_t                 *tiRoot,
                tdsaPortContext_t        *onePortContext
                )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *ExpanderList;
  tdsaExpander_t    *tempExpander;
  tdsaPortContext_t *tmpOnePortContext = onePortContext;

  TI_DBG3(("tdssSASDiscoveringExpander tdsaFreeAllExp: start\n"));

  TI_DBG3(("tdssSASDiscoveringExpander tdsaFreeAllExp: before all clean up\n"));
  tdsaDumpAllFreeExp(tiRoot);

  /* clean up UpdiscoveringExpanderList*/
  TI_DBG3(("tdssSASDiscoveringExpander tdsaFreeAllExp: clean discoveringExpanderList\n"));
  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  if (!TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
    while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
    {
      tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
      TI_DBG3(("tdssSASDiscoveringExpander tdsaFreeAllExp: exp addrHi 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssSASDiscoveringExpander tdsaFreeAllExp: exp addrLo 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressLo));
      /* putting back to the free pool */
      tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
      TDLIST_DEQUEUE_THIS(&(tempExpander->linkNode));
      TDLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(tdsaAllShared->freeExpanderList));

      if (TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
      {
        tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
        break;
      }
      else
      {
        tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
      }
      ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;

//      ExpanderList = ExpanderList->flink;
    }
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdssSASDiscoveringExpander tdsaFreeAllExp: empty discoveringExpanderList\n"));
  }

  /* reset UpdiscoveringExpanderList */
  TDLIST_INIT_HDR(&(tmpOnePortContext->discovery.UpdiscoveringExpanderList));

  return;
}
/*****************************************************************************
*! \brief  tdsaResetValidDeviceData
*
*  Purpose:  This function resets valid and valid2 field for discovered devices
*            in the device list. This is used only in incremental discovery.
*
*  \param   agRoot        :  Pointer to chip/driver Instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaResetValidDeviceData(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext
                                 )
{
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *DeviceListList;
  tdsaDeviceData_t  *oneDeviceData;

  TI_DBG3(("tdsaResetValidDeviceData: start\n"));

  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG1(("tdsaResetValidDeviceData: empty device list\n"));
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    DeviceListList = tdsaAllShared->MainDeviceList.flink;
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      oneDeviceData->valid = oneDeviceData->valid2;
      oneDeviceData->valid2 = agFALSE;
      DeviceListList = DeviceListList->flink;
      TI_DBG3(("tdsaResetValidDeviceData: valid %d valid2 %d\n", oneDeviceData->valid, oneDeviceData->valid2));
    }
  }

  return;
}

/*****************************************************************************
*! \brief  tdssReportChanges
*
*  Purpose:  This function goes throuhg device list and finds out whether
*            a device is removed and newly added. Based on the findings,
*            this function notifies OS layer of the change.
*
*  \param   agRoot        :  Pointer to chip/driver Instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdssReportChanges(
                  agsaRoot_t           *agRoot,
                  tdsaPortContext_t    *onePortContext
                  )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             added = agFALSE, removed = agFALSE;

  TI_DBG1(("tdssReportChanges: start\n"));

  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG1(("tdssReportChanges: empty device list\n"));
    return;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
  }
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG3(("tdssReportChanges: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG3(("tdssReportChanges: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    if ( oneDeviceData->tdPortContext == onePortContext)
    {
      TI_DBG3(("tdssReportChanges: right portcontext\n"));
      if ( (oneDeviceData->valid == agTRUE) && (oneDeviceData->valid2 == agTRUE) )
      {
        TI_DBG3(("tdssReportChanges: same\n"));
        /* reset valid bit */
        oneDeviceData->valid = oneDeviceData->valid2;
        oneDeviceData->valid2 = agFALSE;
      }
      else if ( (oneDeviceData->valid == agTRUE) && (oneDeviceData->valid2 == agFALSE) )
      {
        TI_DBG3(("tdssReportChanges: removed\n"));
        removed = agTRUE;
        /* reset valid bit */
        oneDeviceData->valid = oneDeviceData->valid2;
        oneDeviceData->valid2 = agFALSE;
        /* reset NumOfFCA */
        oneDeviceData->satDevData.NumOfFCA = 0;

        if ( (oneDeviceData->registered == agTRUE) &&
             ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData)
             || DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_SMP_TARGET(oneDeviceData) )
           )
        {
          tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
        }
        else if (oneDeviceData->registered == agTRUE)
        {
          TI_DBG1(("tdssReportChanges: calling saDeregisterDeviceHandle, did %d\n", oneDeviceData->id));
          saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, 0);
        }

        oneDeviceData->registered = agFALSE;

#ifdef REMOVED  /* don't remove device from the device list. May screw up ordering of report */
        TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
        TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
#endif
      }
      else if ( (oneDeviceData->valid == agFALSE) && (oneDeviceData->valid2 == agTRUE) )
      {
        TI_DBG3(("tdssReportChanges: added\n"));
        added = agTRUE;
        /* reset valid bit */
        oneDeviceData->valid = oneDeviceData->valid2;
        oneDeviceData->valid2 = agFALSE;
      }
      else
      {
        TI_DBG6(("tdssReportChanges: else\n"));
      }
    }
    else
    {
      TI_DBG1(("tdssReportChanges: different portcontext\n"));
    }
    DeviceListList = DeviceListList->flink;
  }
  /* arrival or removal at once */
  if (added == agTRUE)
  {
    TI_DBG3(("tdssReportChanges: added at the end\n"));
#ifdef AGTIAPI_CTL
    if (tdsaAllShared->SASConnectTimeLimit)
      tdsaCTLSet(tiRoot, onePortContext, tiIntrEventTypeDeviceChange,
                 tiDeviceArrival);
    else
#endif
      ostiInitiatorEvent(
                         tiRoot,
                         onePortContext->tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceArrival,
                         agNULL
                         );

  }
  if (removed == agTRUE)
  {
    TI_DBG3(("tdssReportChanges: removed at the end\n"));
    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDeviceChange,
                       tiDeviceRemoval,
                       agNULL
                       );
  }

  if (onePortContext->discovery.forcedOK == agTRUE && added == agFALSE && removed == agFALSE)
  {
    TI_DBG1(("tdssReportChanges: missed chance to report. forced to report OK\n"));
    onePortContext->discovery.forcedOK = agFALSE;
    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscOK,
                       agNULL
                       );
  }

  if (added == agFALSE && removed == agFALSE)
  {
    TI_DBG3(("tdssReportChanges: the same\n"));
  }
  return;
}
/*****************************************************************************
*! \brief  tdssReportRemovals
*
*  Purpose:  This function goes through device list and removes all devices
*            belong to the portcontext. This function also deregiters those
*            devices. This function is called in case of incremental discovery
*            failure.
*
*  \param   agRoot        :  Pointer to chip/driver Instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdssReportRemovals(
                  agsaRoot_t           *agRoot,
                  tdsaPortContext_t    *onePortContext,
                  bit32                flag
                  )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             removed = agFALSE;
  agsaEventSource_t *eventSource;
  bit32             PhyID;
  bit32             HwAckSatus;
  agsaDevHandle_t   *agDevHandle = agNULL;

  TI_DBG2(("tdssReportRemovals: start\n"));
  /* in case nothing was registered */
  PhyID = onePortContext->eventPhyID;
  if (tdsaAllShared->eventSource[PhyID].EventValid == agTRUE &&
      onePortContext->RegisteredDevNums == 0 &&
      PhyID != 0xFF
      )
  {
    TI_DBG2(("tdssReportRemovals: calling saHwEventAck\n"));
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
      TI_DBG1(("tdssReportRemovals: failing in saHwEventAck; status %d\n", HwAckSatus));
    }

    /* toggle */
    tdsaAllShared->eventSource[PhyID].EventValid = agFALSE;
    if (onePortContext->valid == agFALSE)
    {
      /* put device belonging to the port to freedevice list */
      DeviceListList = tdsaAllShared->MainDeviceList.flink;
      while (DeviceListList != &(tdsaAllShared->MainDeviceList))
      {
        oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
        if (oneDeviceData->tdPortContext == onePortContext)
        {
          osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
          tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
          TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
          TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
          if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
          {
            tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
            break;
          }
          tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
          DeviceListList = tdsaAllShared->MainDeviceList.flink;
        }
        else
        {
          DeviceListList = DeviceListList->flink;
        }
      } /* while */

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
  }

  else
  {
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      TI_DBG1(("tdssReportRemovals: 1st empty device list\n"));
      return;
    }
    else
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    }
    DeviceListList = tdsaAllShared->MainDeviceList.flink;
    /* needs to clean up devices which were not removed in ossaDeregisterDeviceHandleCB() since port was in valid (discovery error) */
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        TI_DBG1(("tdssReportRemovals: oneDeviceData is NULL!!!\n"));
        return;
      }
      TI_DBG2(("tdssReportRemovals: 1st loop did %d\n", oneDeviceData->id));
      TI_DBG2(("tdssReportRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG2(("tdssReportRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      TI_DBG2(("tdssReportRemovals: valid %d\n", oneDeviceData->valid));
      TI_DBG2(("tdssReportRemovals: valid2 %d\n", oneDeviceData->valid2));
      TI_DBG2(("tdssReportRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));
      TI_DBG2(("tdssReportRemovals: registered %d\n", oneDeviceData->registered));
      if ( oneDeviceData->tdPortContext == onePortContext && oneDeviceData->valid == agFALSE &&
           oneDeviceData->valid2 == agFALSE && oneDeviceData->registered == agFALSE
         )
      {
        /* remove oneDevice from MainLink */
        TI_DBG2(("tdssReportRemovals: delete from MainLink\n"));
        agDevHandle = oneDeviceData->agDevHandle;
        tdsaDeviceDataReInit(tiRoot, oneDeviceData);
        //save agDevHandle and tdPortContext
        oneDeviceData->agDevHandle = agDevHandle;
        oneDeviceData->tdPortContext = onePortContext;
        osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
        tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
        TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
        TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
        tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
        DeviceListList = tdsaAllShared->MainDeviceList.flink;
        tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
        if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
        {
          tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
          break;
        }
        else
        {
          tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
        }
      }
      else
      {
        DeviceListList = DeviceListList->flink;
      }
    } /* while */


    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      TI_DBG1(("tdssReportRemovals: 2nd empty device list\n"));
      return;
    }
    else
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    }
    DeviceListList = tdsaAllShared->MainDeviceList.flink;
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        TI_DBG1(("tdssReportRemovals: oneDeviceData is NULL!!!\n"));
        return;
      }
      TI_DBG2(("tdssReportRemovals: loop did %d\n", oneDeviceData->id));
      TI_DBG2(("tdssReportRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG2(("tdssReportRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      TI_DBG2(("tdssReportRemovals: valid %d\n", oneDeviceData->valid));
      TI_DBG2(("tdssReportRemovals: valid2 %d\n", oneDeviceData->valid2));
      TI_DBG2(("tdssReportRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));
      TI_DBG2(("tdssReportRemovals: registered %d\n", oneDeviceData->registered));
      if ( oneDeviceData->tdPortContext == onePortContext)
      {
        TI_DBG2(("tdssReportRemovals: right portcontext pid %d\n", onePortContext->id));
        if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE)
        {
          TI_DBG2(("tdssReportRemovals: removing\n"));

          /* notify only reported devices to OS layer*/
          if ( DEVICE_IS_SSP_TARGET(oneDeviceData) ||
               DEVICE_IS_STP_TARGET(oneDeviceData) ||
               DEVICE_IS_SATA_DEVICE(oneDeviceData)
              )
          {
            removed = agTRUE;
          }

          if ( (oneDeviceData->registered == agTRUE) &&
               ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData)
               || DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_SMP_TARGET(oneDeviceData) )
             )
          {
            /* all targets except expanders */
            TI_DBG2(("tdssReportRemovals: calling tdsaAbortAll\n"));
            TI_DBG2(("tdssReportRemovals: did %d\n", oneDeviceData->id));
            TI_DBG2(("tdssReportRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
            TI_DBG2(("tdssReportRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
            tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
          }
          else if (oneDeviceData->registered == agTRUE)
          {
            /* expanders */
            TI_DBG1(("tdssReportRemovals: calling saDeregisterDeviceHandle, did %d\n", oneDeviceData->id));
            TI_DBG2(("tdssReportRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
            TI_DBG2(("tdssReportRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
            saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, 0);
          }

          /* reset valid bit */
          oneDeviceData->valid = agFALSE;
          oneDeviceData->valid2 = agFALSE;
          oneDeviceData->registered = agFALSE;
          /* reset NumOfFCA */
          oneDeviceData->satDevData.NumOfFCA = 0;

        }
        /* called by port invalid case */
        if (flag == agTRUE)
        {
          oneDeviceData->tdPortContext = agNULL;
          TI_DBG1(("tdssReportRemovals: nulling-out tdPortContext; oneDeviceData did %d\n", oneDeviceData->id));
        }
#ifdef REMOVED /* removed */
        /* directly attached SATA -> always remove it */
        if (oneDeviceData->DeviceType == TD_SATA_DEVICE &&
            oneDeviceData->directlyAttached == agTRUE)
        {
          TI_DBG1(("tdssReportRemovals: device did %d\n", oneDeviceData->id));
          TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
          TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceLis));
          DeviceListList = tdsaAllShared->MainDeviceList.flink;
          if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
          {
            break;
          }
        }
        else
        {
          DeviceListList = DeviceListList->flink;
        }
#endif /* REMOVED */
        DeviceListList = DeviceListList->flink;
      }
      else
      {
        if (oneDeviceData->tdPortContext != agNULL)
        {
          TI_DBG2(("tdssReportRemovals: different portcontext; oneDeviceData->tdPortContext pid %d oneportcontext pid %d oneDeviceData did %d\n",
          oneDeviceData->tdPortContext->id, onePortContext->id, oneDeviceData->id));
        }
        else
        {
          TI_DBG1(("tdssReportRemovals: different portcontext; oneDeviceData->tdPortContext pid NULL oneportcontext pid %d oneDeviceData did %d\n",
          onePortContext->id, oneDeviceData->id));
        }
        DeviceListList = DeviceListList->flink;
      }
    }

    if (removed == agTRUE)
    {
      TI_DBG2(("tdssReportRemovals: removed at the end\n"));
      ostiInitiatorEvent(
                         tiRoot,
                         onePortContext->tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceRemoval,
                         agNULL
                         );
    }
  } /* big else */
  return;
}

/*
  changes valid and valid2 based on discovery type
*/
osGLOBAL void
tdssInternalRemovals(
                     agsaRoot_t           *agRoot,
                     tdsaPortContext_t    *onePortContext
                     )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  TI_DBG2(("tdssInternalRemovals: start\n"));

  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG1(("tdssInternalRemovals: empty device list\n"));
    return;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
  }
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG3(("tdssInternalRemovals: loop did %d\n", oneDeviceData->id));
    TI_DBG3(("tdssInternalRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG3(("tdssInternalRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    TI_DBG3(("tdssInternalRemovals: valid %d\n", oneDeviceData->valid));
    TI_DBG3(("tdssInternalRemovals: valid2 %d\n", oneDeviceData->valid2));
    TI_DBG3(("tdssInternalRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));
    TI_DBG3(("tdssInternalRemovals: registered %d\n", oneDeviceData->registered));
    if ( oneDeviceData->tdPortContext == onePortContext)
    {
      TI_DBG3(("tdssInternalRemovals: right portcontext pid %d\n", onePortContext->id));
      if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_INCREMENTAL_START)
      {
        TI_DBG3(("tdssInternalRemovals: incremental discovery\n"));
        oneDeviceData->valid2 = agFALSE;
      }
      else
      {
        TI_DBG3(("tdssInternalRemovals: full discovery\n"));
        oneDeviceData->valid = agFALSE;
      }
      DeviceListList = DeviceListList->flink;
    }
    else
    {
      if (oneDeviceData->tdPortContext != agNULL)
      {
        TI_DBG3(("tdssInternalRemovals: different portcontext; oneDeviceData->tdPortContext pid %d oneportcontext pid %d\n", oneDeviceData->tdPortContext->id, onePortContext->id));
      }
      else
      {
        TI_DBG3(("tdssInternalRemovals: different portcontext; oneDeviceData->tdPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }
  }


  return;
}

/* resets all valid and valid2 */
osGLOBAL void
tdssDiscoveryErrorRemovals(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext
                                 )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  TI_DBG1(("tdssDiscoveryErrorRemovals: start\n"));

  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG1(("tdssDiscoveryErrorRemovals: empty device list\n"));
    return;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
  }
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG2(("tdssDiscoveryErrorRemovals: loop did %d\n", oneDeviceData->id));
    TI_DBG2(("tdssDiscoveryErrorRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG2(("tdssDiscoveryErrorRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    TI_DBG2(("tdssDiscoveryErrorRemovals: valid %d\n", oneDeviceData->valid));
    TI_DBG2(("tdssDiscoveryErrorRemovals: valid2 %d\n", oneDeviceData->valid2));
    TI_DBG2(("tdssDiscoveryErrorRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));
    TI_DBG2(("tdssDiscoveryErrorRemovals: registered %d\n", oneDeviceData->registered));
    if ( oneDeviceData->tdPortContext == onePortContext)
    {
      TI_DBG2(("tdssDiscoveryErrorRemovals: right portcontext pid %d\n", onePortContext->id));
      oneDeviceData->valid = agFALSE;
      oneDeviceData->valid2 = agFALSE;
      /* reset NumOfFCA */
      oneDeviceData->satDevData.NumOfFCA = 0;

      if ( (oneDeviceData->registered == agTRUE) &&
           ( DEVICE_IS_SSP_TARGET(oneDeviceData) || DEVICE_IS_STP_TARGET(oneDeviceData)
           || DEVICE_IS_SATA_DEVICE(oneDeviceData) || DEVICE_IS_SMP_TARGET(oneDeviceData) )
         )
      {
        /* all targets other than expanders */
        TI_DBG2(("tdssDiscoveryErrorRemovals: calling tdsaAbortAll\n"));
        TI_DBG2(("tdssDiscoveryErrorRemovals: did %d\n", oneDeviceData->id));
        TI_DBG2(("tdssDiscoveryErrorRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG2(("tdssDiscoveryErrorRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        tdsaAbortAll(tiRoot, agRoot, oneDeviceData);
      }
      else if (oneDeviceData->registered == agTRUE)
      {
        /* expanders */
        TI_DBG2(("tdssDiscoveryErrorRemovals: calling saDeregisterDeviceHandle\n"));
        TI_DBG2(("tdssDiscoveryErrorRemovals: did %d\n", oneDeviceData->id));
        TI_DBG2(("tdssDiscoveryErrorRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG2(("tdssDiscoveryErrorRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        saDeregisterDeviceHandle(agRoot, agNULL, oneDeviceData->agDevHandle, 0);
      }

      oneDeviceData->registered = agFALSE;
      DeviceListList = DeviceListList->flink;
    }
    else
    {
      if (oneDeviceData->tdPortContext != agNULL)
      {
        TI_DBG2(("tdssDiscoveryErrorRemovals: different portcontext; oneDeviceData->tdPortContext pid %d oneportcontext pid %d\n", oneDeviceData->tdPortContext->id, onePortContext->id));
      }
      else
      {
        TI_DBG2(("tdssDiscoveryErrorRemovals: different portcontext; oneDeviceData->tdPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }
  }


  return;
}

/*****************************************************************************
*! \brief  tdsaSASDiscoverAbort
*
*  Purpose:  This function aborts on-going discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
/* this called when discovery is aborted
   aborted by whom
*/
osGLOBAL void
tdsaSASDiscoverAbort(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext
                    )
{

  TI_DBG2(("tdsaSASDiscoverAbort: start\n"));
  TI_DBG2(("tdsaSASDiscoverAbort: pPort=%p  DONE\n", onePortContext));
  TI_DBG2(("tdsaSASDiscoverAbort: DiscoveryState %d\n", onePortContext->DiscoveryState));

  onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
  /* clean up expanders data strucures; move to free exp when device is cleaned */
  tdsaCleanAllExp(tiRoot, onePortContext);

  /* unregister devices */
  tdssReportRemovals(onePortContext->agRoot,
                     onePortContext,
                     agFALSE
                    );
}

#ifdef AGTIAPI_CTL

STATIC void
tdsaCTLNextDevice(
  tiRoot_t          *tiRoot,
  tdsaPortContext_t *onePortContext,
  tdIORequest_t     *tdIORequest,
  tdList_t          *DeviceList);

STATIC void
tdsaCTLIOCompleted(
  agsaRoot_t      *agRoot,
  agsaIORequest_t *agIORequest,
  bit32           agIOStatus,
  bit32           agIOInfoLen,
  void            *agParam,
  bit16           sspTag,
  bit32           agOtherInfo)
{
  tiRoot_t          *tiRoot = (tiRoot_t*)
                      ((tdsaRootOsData_t*)agRoot->osData)->tiRoot;
  tdIORequestBody_t *tdIORequestBody;
  tdIORequest_t     *tdIORequest;
  tdsaDeviceData_t  *oneDeviceData;

  tdIORequest = (tdIORequest_t *)agIORequest->osData;
  tdIORequestBody = &tdIORequest->tdIORequestBody;
  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;
  oneDeviceData = (tdsaDeviceData_t *)tdIORequestBody->tiDevHandle->tdData;

  TI_DBG6(("tdsaCTLIOCompleted: stat x%x len %d id %d\n", agIOStatus,
           agIOInfoLen, oneDeviceData->id));

  //if ((agIOStatus == OSSA_IO_SUCCESS) && (agIOInfoLen == 0))
  /* SCSI command was completed OK, this is the normal path. */
  if (agIOInfoLen)
  {
    TI_DBG6(("tdsaCTLIOCompleted: SASDevAddr 0x%x / 0x%x PhyId 0x%x WARN "
             "setting CTL\n",
             oneDeviceData->SASAddressID.sasAddressHi,
             oneDeviceData->SASAddressID.sasAddressLo,
             oneDeviceData->SASAddressID.phyIdentifier));
    tdhexdump("tdsaCTLIOCompleted: response", (bit8 *)agParam, agIOInfoLen);
  }

  tdsaCTLNextDevice(tiRoot, oneDeviceData->tdPortContext, tdIORequest,
                    oneDeviceData->MainLink.flink);
} /* tdsaCTLIOCompleted */

STATIC int
tdsaCTLModeSelect(
  tiRoot_t                  *tiRoot,
  tiDeviceHandle_t          *tiDeviceHandle,
  tdIORequest_t             *tdIORequest)
{
  tiIORequest_t             *tiIORequest;
  tdsaDeviceData_t          *oneDeviceData;
  agsaRoot_t                *agRoot = agNULL;
  tdsaRoot_t                *tdsaRoot = (tdsaRoot_t*)tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t*)
                              &tdsaRoot->tdsaAllShared;
  agsaIORequest_t           *agIORequest = agNULL;
  agsaDevHandle_t           *agDevHandle = agNULL;
  agsaSASRequestBody_t      *agSASRequestBody = agNULL;
  bit32                     tiStatus;
  bit32                     saStatus;
  tdIORequestBody_t         *tdIORequestBody;
  agsaSSPInitiatorRequest_t *agSSPInitiatorRequest;
  unsigned char             *virtAddr;
  tiSgl_t                   agSgl;
  static unsigned char      cdb[6] =
  {
    MODE_SELECT,
    PAGE_FORMAT,
    0,
    0,
    DR_MODE_PG_SZ
  };

  virtAddr = (unsigned char*)tdIORequest->virtAddr;
  virtAddr[0] = DR_MODE_PG_CODE; /* Disconnect-Reconnect mode page code */
  virtAddr[1] = DR_MODE_PG_LENGTH; /* DR Mode pg length */
  virtAddr[8] = tdsaAllShared->SASConnectTimeLimit >> 8;
  virtAddr[9] = tdsaAllShared->SASConnectTimeLimit & 0xff;

  oneDeviceData = (tdsaDeviceData_t*)tiDeviceHandle->tdData;
  TI_DBG4(("tdsaCTLModeSelect: id %d\n", oneDeviceData->id));

  agRoot = oneDeviceData->agRoot;
  agDevHandle = oneDeviceData->agDevHandle;
  tiIORequest = &tdIORequest->tiIORequest;

  tdIORequestBody = &tdIORequest->tdIORequestBody;

  //tdIORequestBody->IOCompletionFunc = tdsaCTLIOCompleted;//itdssIOCompleted;
  tdIORequestBody->tiDevHandle = tiDeviceHandle;
  tdIORequestBody->IOType.InitiatorRegIO.expDataLength = DR_MODE_PG_SZ;

  agIORequest = &tdIORequestBody->agIORequest;
  agIORequest->sdkData = agNULL; /* LL takes care of this */

  agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
  agSSPInitiatorRequest = &(agSASRequestBody->sspInitiatorReq);

  osti_memcpy(agSSPInitiatorRequest->sspCmdIU.cdb, cdb, 6);
  agSSPInitiatorRequest->dataLength = DR_MODE_PG_SZ;

  agSSPInitiatorRequest->firstBurstSize = 0;

  tdIORequestBody->agRequestType = AGSA_SSP_INIT_WRITE;
  tdIORequestBody->ioStarted = agTRUE;
  tdIORequestBody->ioCompleted = agFALSE;

  agSgl.lower = BIT32_TO_LEBIT32(tdIORequest->physLower32);
#if (BITS_PER_LONG > 32)
  agSgl.upper = BIT32_TO_LEBIT32(tdIORequest->physUpper32);
#else
  agSgl1.upper = 0;
#endif
  agSgl.type = BIT32_TO_LEBIT32(tiSgl);
  agSgl.len = BIT32_TO_LEBIT32(DR_MODE_PG_SZ);

  /* initializes "agsaSgl_t   agSgl" of "agsaDifSSPInitiatorRequest_t" */
  tiStatus = itdssIOPrepareSGL(tiRoot, tdIORequestBody, &agSgl,
                               tdIORequest->virtAddr);
  if (tiStatus != tiSuccess)
  {
    TI_DBG1(("tdsaCTLModeSelect: can't get SGL\n"));
    ostiFreeMemory(tiRoot, tdIORequest->osMemHandle2, DR_MODE_PG_SZ);
    ostiFreeMemory(tiRoot, tdIORequest->osMemHandle, sizeof(*tdIORequest));
    return tiError;
  }

  saStatus = saSSPStart(agRoot, agIORequest,
                        tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle,
                        AGSA_SSP_INIT_WRITE, agSASRequestBody, agNULL,
                        &tdsaCTLIOCompleted);
  if (saStatus == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
    TI_DBG4(("tdsaCTLModeSelect: saSSPStart OK\n"));
  }
  else
  {
    tdIORequestBody->ioStarted = agFALSE;
    tdIORequestBody->ioCompleted = agTRUE;
    if (saStatus == AGSA_RC_BUSY)
    {
      tiStatus = tiBusy;
      TI_DBG4(("tdsaCTLModeSelect: saSSPStart busy\n"));
    }
    else
    {
      tiStatus = tiError;
      TI_DBG4(("tdsaCTLModeSelect: saSSPStart Error\n"));
    }
    tdsaCTLNextDevice(tiRoot, oneDeviceData->tdPortContext, tdIORequest,
                      oneDeviceData->MainLink.flink);
  }
  return tiStatus;
} /* tdsaCTLModeSelect */

STATIC void
tdsaCTLNextDevice(
  tiRoot_t          *tiRoot,
  tdsaPortContext_t *onePortContext,
  tdIORequest_t     *tdIORequest,
  tdList_t          *DeviceList)
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData;
  tiIntrEventType_t eventType;
  bit32             eventStatus;
  int               rc;

  /*
   * From the device list, returns only valid devices
   */
  for (; DeviceList && DeviceList != &(tdsaAllShared->MainDeviceList);
       DeviceList = DeviceList->flink)
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceList);
    TI_DBG6(("tdsaCTLNextDevice: devHandle %p\n",
             &(oneDeviceData->tiDeviceHandle)));
    if (oneDeviceData->tdPortContext != onePortContext)
      continue;
    if ((oneDeviceData->discovered == agFALSE) &&
        (oneDeviceData->registered == agTRUE) &&
        DEVICE_IS_SSP_TARGET(oneDeviceData) &&
        !DEVICE_IS_SSP_INITIATOR(oneDeviceData))
    {
      oneDeviceData->discovered = agTRUE;
      rc = tdsaCTLModeSelect(tiRoot, &oneDeviceData->tiDeviceHandle,
                              tdIORequest);
      TI_DBG1(("tdsaCTLNextDevice: ModeSelect ret %d\n", rc));
      return;
    }
  }
  TI_DBG2(("tdsaCTLNextDevice: no more devices found\n"));

  eventType = tdIORequest->eventType;
  eventStatus = tdIORequest->eventStatus;

  /* no more devices, free the memory */
  ostiFreeMemory(tiRoot, tdIORequest->osMemHandle2, DR_MODE_PG_SZ);
  ostiFreeMemory(tiRoot, tdIORequest->osMemHandle, sizeof(*tdIORequest));

  /* send Discovery Done event */
  ostiInitiatorEvent(tiRoot, onePortContext->tiPortalContext, agNULL,
                     eventType, eventStatus, agNULL);
} /* tdsaCTLNextDevice */

osGLOBAL void
tdsaCTLSet(
  tiRoot_t          *tiRoot,
  tdsaPortContext_t *onePortContext,
  tiIntrEventType_t eventType,
  bit32             eventStatus)
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdIORequest_t     *tdIORequest;
  tdIORequestBody_t *tdIORequestBody;
  tiIORequest_t     *tiIORequest;
  bit32             memAllocStatus;
  void              *osMemHandle;
  bit32             physUpper32;
  bit32             physLower32;

  TI_DBG2(("tdsaCTLSet: tiPortalContext pid %d etyp %x stat %x\n",
           onePortContext->id, eventType, eventStatus));

  if (onePortContext->DiscoveryState != ITD_DSTATE_COMPLETED)
  {
    TI_DBG1(("tdsaCTLSet: discovery not completed\n"));
    return;
  }

  /* use the same memory for all valid devices */
  memAllocStatus = ostiAllocMemory(tiRoot, &osMemHandle, (void **)&tdIORequest,
                                   &physUpper32, &physLower32, 8,
                                   sizeof(*tdIORequest), agTRUE);
  if (memAllocStatus != tiSuccess || tdIORequest == agNULL)
  {
    TI_DBG1(("tdsaCTLSet: ostiAllocMemory failed\n"));
    return;// tiError;
  }
  osti_memset(tdIORequest, 0, sizeof(*tdIORequest));

  tdIORequest->osMemHandle = osMemHandle;
  tdIORequest->eventType = eventType;
  tdIORequest->eventStatus = eventStatus;

  tiIORequest = &tdIORequest->tiIORequest;
  tdIORequestBody = &tdIORequest->tdIORequestBody;
  /* save context if we need to abort later */
  tiIORequest->tdData = tdIORequestBody;

  tdIORequestBody->IOCompletionFunc = NULL;//itdssIOCompleted;
  tdIORequestBody->tiIORequest = tiIORequest;
  tdIORequestBody->IOType.InitiatorRegIO.expDataLength = 16;

  tdIORequestBody->agIORequest.osData = (void *)tdIORequest; //tdIORequestBody;

  memAllocStatus = ostiAllocMemory(tiRoot, &tdIORequest->osMemHandle2,
                                   (void **)&tdIORequest->virtAddr,
                                   &tdIORequest->physUpper32,
                                   &tdIORequest->physLower32,
                                   8, DR_MODE_PG_SZ, agFALSE);
  if (memAllocStatus != tiSuccess || tdIORequest == agNULL)
  {
    TI_DBG1(("tdsaCTLSet: ostiAllocMemory noncached failed\n"));
    ostiFreeMemory(tiRoot, tdIORequest->osMemHandle, sizeof(*tdIORequest));
    return;// tiError;
  }

  osti_memset(tdIORequest->virtAddr, 0, DR_MODE_PG_SZ);
  tdsaCTLNextDevice(tiRoot, onePortContext, tdIORequest,
                    tdsaAllShared->MainDeviceList.flink);
} /* tdsaCTLSet*/
#endif

/*****************************************************************************
*! \brief  tdsaSASDiscoverDone
*
*  Purpose:  This function called to finish up SAS discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASDiscoverDone(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext,
                    bit32                flag
                    )
{
#ifndef SATA_ENABLE
  tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *)tiRoot->tdData;
  tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#endif

  TI_DBG3(("tdsaSASDiscoverDone: start\n"));
  TI_DBG3(("tdsaSASDiscoverDone: pPort=%p  DONE\n", onePortContext));
  TI_DBG3(("tdsaSASDiscoverDone: pid %d\n", onePortContext->id));

  /* Set discovery status */
  onePortContext->discovery.status = DISCOVERY_SAS_DONE;

#ifdef TD_INTERNAL_DEBUG /* debugging only */
  TI_DBG3(("tdsaSASDiscoverDone: BEFORE\n"));
  tdsaDumpAllExp(tiRoot, onePortContext, agNULL);
  tdsaDumpAllUpExp(tiRoot, onePortContext, agNULL);
#endif

  /* clean up expanders data strucures; move to free exp when device is cleaned */
  tdsaCleanAllExp(tiRoot, onePortContext);

#ifdef TD_INTERNAL_DEBUG /* debugging only */
  TI_DBG3(("tdsaSASDiscoverDone: AFTER\n"));
  tdsaDumpAllExp(tiRoot, onePortContext, agNULL);
  tdsaDumpAllUpExp(tiRoot, onePortContext, agNULL);
#endif


  /* call back to notify discovery is done */
  /* SATA is NOT enbled */
#ifndef SATA_ENABLE
  if (onePortContext->discovery.SeenBC == agTRUE)
  {
    TI_DBG3(("tdsaSASDiscoverDone: broadcast change; discover again\n"));
    tdssInternalRemovals(onePortContext->agRoot,
                         onePortContext
                         );

    /* processed broadcast change */
    onePortContext->discovery.SeenBC = agFALSE;
    if (tdsaAllShared->ResetInDiscovery != 0 &&
        onePortContext->discovery.ResetTriggerred == agTRUE)
    {
      TI_DBG2(("tdsaSASDiscoverDone: tdsaBCTimer\n"));
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
  }
  else
  {
    onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;

    if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
    {
      if (flag == tiSuccess)
      {
#ifdef AGTIAPI_CTL
        if (tdsaAllShared->SASConnectTimeLimit)
          tdsaCTLSet(tiRoot, onePortContext, tiIntrEventTypeDiscovery,
                     tiDiscOK);
        else
#endif
          ostiInitiatorEvent(
                             tiRoot,
                             onePortContext->tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDiscovery,
                             tiDiscOK,
                             agNULL
                             );
      }
      else
      {
        TI_DBG1(("tdsaSASDiscoverDone: discovery failed\n"));
        tdssDiscoveryErrorRemovals(onePortContext->agRoot,
                                   onePortContext
                                   );

        ostiInitiatorEvent(
                           tiRoot,
                           onePortContext->tiPortalContext,
                           agNULL,
                           tiIntrEventTypeDiscovery,
                           tiDiscFailed,
                           agNULL
                           );
      }
    }
    else
    {
      if (flag == tiSuccess)
      {
        tdssReportChanges(onePortContext->agRoot,
                          onePortContext
                          );
      }
      else
      {
        tdssReportRemovals(onePortContext->agRoot,
                           onePortContext,
                           agFALSE
                           );
      }
    }
  }
#ifdef TBD
  /* ACKing BC */
  tdsaAckBC(tiRoot, onePortContext);
#endif

#endif

#ifdef SATA_ENABLE

  if (flag == tiSuccess)
  {
    TI_DBG3(("tdsaSASDiscoverDone: calling SATA discovery\n"));
    /*
       tdsaSATAFullDiscover() or tdsaincrementalDiscover()
       call sata discover
       when sata discover is done, call ostiInitiatorEvent
    */
    if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
    {
      TI_DBG3(("tdsaSASDiscoverDone: calling FULL SATA discovery\n"));
      tdsaDiscover(
                   tiRoot,
                   onePortContext,
                   AG_SA_DISCOVERY_TYPE_SATA,
                   TDSA_DISCOVERY_OPTION_FULL_START
                   );
    }
    else
    {
      TI_DBG3(("tdsaSASDiscoverDone: calling INCREMENTAL SATA discovery\n"));
      tdsaDiscover(
                   tiRoot,
                   onePortContext,
                   AG_SA_DISCOVERY_TYPE_SATA,
                   TDSA_DISCOVERY_OPTION_INCREMENTAL_START
                   );
    }
  }
  else
  {
    /* error case */
    TI_DBG1(("tdsaSASDiscoverDone: Error; clean up\n"));
    tdssDiscoveryErrorRemovals(onePortContext->agRoot,
                               onePortContext
                               );

    onePortContext->discovery.SeenBC = agFALSE;
    onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;
    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery,
                       tiDiscFailed,
                       agNULL
                       );
  }
#endif
  return;
}

//temp only for testing
osGLOBAL void
tdsaReportManInfoSend(
                      tiRoot_t             *tiRoot,
                      tdsaDeviceData_t     *oneDeviceData
                      )
{
  agsaRoot_t            *agRoot;

  agRoot = oneDeviceData->agRoot;

  TI_DBG2(("tdsaReportManInfoSend: start\n"));

  tdSMPStart(
             tiRoot,
             agRoot,
             oneDeviceData,
             SMP_REPORT_MANUFACTURE_INFORMATION,
             agNULL,
             0,
             AGSA_SMP_INIT_REQ,
             agNULL,
             0
             );

  return;
}


osGLOBAL void
tdsaReportManInfoRespRcvd(
                          tiRoot_t              *tiRoot,
                          agsaRoot_t            *agRoot,
                          tdsaDeviceData_t      *oneDeviceData,
                          tdssSMPFrameHeader_t  *frameHeader,
                          agsaFrameHandle_t     frameHandle
                          )
{
  tdsaPortContext_t            *onePortContext;
  tdsaDiscovery_t              *discovery;

  TI_DBG2(("tdsaReportManInfoRespRcvd: start\n"));
  TI_DBG2(("tdsaReportManInfoRespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG2(("tdsaReportManInfoRespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->tdPortContext;
  discovery = &(onePortContext->discovery);

  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    TI_DBG2(("tdsaReportManInfoRespRcvd: SMP accepted\n"));
  }
  else
  {
    TI_DBG1(("tdsaReportManInfoRespRcvd: SMP NOT accepted; fn result 0x%x\n", frameHeader->smpFunctionResult));
  }

  TI_DBG2(("tdsaReportManInfoRespRcvd: discovery retries %d\n", discovery->retries));
  discovery->retries++;

  if (discovery->retries >= DISCOVERY_RETRIES)
  {
    TI_DBG1(("tdsaReportManInfoRespRcvd: retries are over\n"));
    discovery->retries = 0;
    /* failed the discovery */
  }
  else
  {
    TI_DBG1(("tdsaReportManInfoRespRcvd: keep retrying\n"));
    // start timer
    tdsaDiscoveryTimer(tiRoot, onePortContext, oneDeviceData);
  }

  return;
}

//end temp only for testing

/*****************************************************************************
*! \brief  tdsaReportGeneralSend
*
*  Purpose:  This function sends Report General SMP to a device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaReportGeneralSend(
                      tiRoot_t             *tiRoot,
                      tdsaDeviceData_t     *oneDeviceData
                      )
{
  agsaRoot_t            *agRoot;

  agRoot = oneDeviceData->agRoot;

  TI_DBG3(("tdsaReportGeneralSend: start\n"));

  tdSMPStart(
             tiRoot,
             agRoot,
             oneDeviceData,
             SMP_REPORT_GENERAL,
             agNULL,
             0,
             AGSA_SMP_INIT_REQ,
             agNULL,
             0
             );

  return;
}

/*****************************************************************************
*! \brief  tdsaReportGeneralRespRcvd
*
*  Purpose:  This function processes Report General response.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaReportGeneralRespRcvd(
                          tiRoot_t              *tiRoot,
                          agsaRoot_t            *agRoot,
                          agsaIORequest_t       *agIORequest,
                          tdsaDeviceData_t      *oneDeviceData,
                          tdssSMPFrameHeader_t  *frameHeader,
                          agsaFrameHandle_t     frameHandle
                          )
{
  smpRespReportGeneral_t     tdSMPReportGeneralResp;
  smpRespReportGeneral_t    *ptdSMPReportGeneralResp;
  tdsaExpander_t            *oneExpander;
  tdsaPortContext_t         *onePortContext;
  tdsaDiscovery_t           *discovery;
#ifdef REMOVED
  bit32                      i;
#endif
#ifndef DIRECT_SMP
  tdssSMPRequestBody_t      *tdSMPRequestBody;
  tdSMPRequestBody = (tdssSMPRequestBody_t *)agIORequest->osData;
#endif

  TI_DBG3(("tdsaReportGeneralRespRcvd: start\n"));
  TI_DBG3(("tdsaReportGeneralRespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaReportGeneralRespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  ptdSMPReportGeneralResp = &tdSMPReportGeneralResp;
  osti_memset(&tdSMPReportGeneralResp, 0, sizeof(smpRespReportGeneral_t));
#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, ptdSMPReportGeneralResp, sizeof(smpRespReportGeneral_t));
#else
  saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 4, ptdSMPReportGeneralResp, sizeof(smpRespReportGeneral_t));
#endif

  //tdhexdump("tdsaReportGeneralRespRcvd", (bit8 *)ptdSMPReportGeneralResp, sizeof(smpRespReportGeneral_t));
#ifndef DIRECT_SMP
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                );
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                );
#endif

  onePortContext = oneDeviceData->tdPortContext;
  discovery = &(onePortContext->discovery);

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaReportGeneralRespRcvd: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    oneDeviceData->numOfPhys = (bit8) ptdSMPReportGeneralResp->numOfPhys;
    oneExpander = oneDeviceData->tdExpander;
    oneExpander->routingIndex = (bit16) REPORT_GENERAL_GET_ROUTEINDEXES(ptdSMPReportGeneralResp);
#ifdef REMOVED
    for ( i = 0; i < oneDeviceData->numOfPhys; i++ )
    {
      oneExpander->currentIndex[i] = 0;
    }
#endif
    oneExpander->configReserved = 0;
    oneExpander->configRouteTable = REPORT_GENERAL_IS_CONFIGURABLE(ptdSMPReportGeneralResp) ? 1 : 0;
    oneExpander->configuring = REPORT_GENERAL_IS_CONFIGURING(ptdSMPReportGeneralResp) ? 1 : 0;
    TI_DBG3(("tdsaReportGeneralRespRcvd: oneExpander=%p numberofPhys=0x%x RoutingIndex=0x%x\n",
      oneExpander, oneDeviceData->numOfPhys, oneExpander->routingIndex));
    TI_DBG3(("tdsaReportGeneralRespRcvd: configRouteTable=%d configuring=%d\n",
      oneExpander->configRouteTable, oneExpander->configuring));
    if (oneExpander->configuring == 1)
    {
      discovery->retries++;
      if (discovery->retries >= DISCOVERY_RETRIES)
      {
        TI_DBG1(("tdsaReportGeneralRespRcvd: retries are over\n"));
        discovery->retries = 0;
        /* failed the discovery */
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
      }
      else
      {
        TI_DBG1(("tdsaReportGeneralRespRcvd: keep retrying\n"));
        // start timer for sending ReportGeneral
        tdsaDiscoveryTimer(tiRoot, onePortContext, oneDeviceData);
      }
    }
    else
    {
      discovery->retries = 0;
      tdsaDiscoverSend(tiRoot, oneDeviceData);
    }
  }
  else
  {
     TI_DBG1(("tdsaReportGeneralRespRcvd: SMP failed; fn result 0x%x; stopping discovery\n", frameHeader->smpFunctionResult));
     tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
  }
  return;
}


/*****************************************************************************
*! \brief  tdsaDiscoverSend
*
*  Purpose:  This function sends Discovery SMP to a device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoverSend(
                 tiRoot_t             *tiRoot,
                 tdsaDeviceData_t     *oneDeviceData
                 )
{
  agsaRoot_t            *agRoot;
  tdsaExpander_t        *oneExpander;
  smpReqDiscover_t      smpDiscoverReq;

  TI_DBG3(("tdsaDiscoverSend: start\n"));
  TI_DBG3(("tdsaDiscoverSend: device %p did %d\n", oneDeviceData, oneDeviceData->id));
  agRoot = oneDeviceData->agRoot;
  oneExpander = oneDeviceData->tdExpander;
  TI_DBG3(("tdsaDiscoverSend: phyID 0x%x\n", oneExpander->discoveringPhyId));


  osti_memset(&smpDiscoverReq, 0, sizeof(smpReqDiscover_t));

  smpDiscoverReq.reserved1 = 0;
  smpDiscoverReq.reserved2 = 0;
  smpDiscoverReq.phyIdentifier = oneExpander->discoveringPhyId;
  smpDiscoverReq.reserved3 = 0;


  tdSMPStart(
             tiRoot,
             agRoot,
             oneDeviceData,
             SMP_DISCOVER,
             (bit8 *)&smpDiscoverReq,
             sizeof(smpReqDiscover_t),
             AGSA_SMP_INIT_REQ,
             agNULL,
             0
             );
  return;
}


/*****************************************************************************
*! \brief  tdsaDiscoverRespRcvd
*
*  Purpose:  This function processes Discovery response.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoverRespRcvd(
                     tiRoot_t              *tiRoot,
                     agsaRoot_t            *agRoot,
                     agsaIORequest_t       *agIORequest,
                     tdsaDeviceData_t      *oneDeviceData,
                     tdssSMPFrameHeader_t  *frameHeader,
                     agsaFrameHandle_t     frameHandle
                     )
{
  smpRespDiscover_t   *ptdSMPDiscoverResp;
  tdsaPortContext_t   *onePortContext;
  tdsaExpander_t      *oneExpander;
  tdsaDiscovery_t     *discovery;
#ifndef DIRECT_SMP
  tdssSMPRequestBody_t *tdSMPRequestBody;
#endif

  TI_DBG3(("tdsaDiscoverRespRcvd: start\n"));
  TI_DBG3(("tdsaDiscoverRespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaDiscoverRespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));


  onePortContext = oneDeviceData->tdPortContext;
  oneExpander = oneDeviceData->tdExpander;
  discovery = &(onePortContext->discovery);
#ifndef DIRECT_SMP
  tdSMPRequestBody = (tdssSMPRequestBody_t *)agIORequest->osData;
#endif

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaDiscoverRespRcvd: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
  ptdSMPDiscoverResp = &(discovery->SMPDiscoverResp);
#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, ptdSMPDiscoverResp, sizeof(smpRespDiscover_t));
#else
  saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 4, ptdSMPDiscoverResp, sizeof(smpRespDiscover_t));
#endif
  //tdhexdump("tdsaDiscoverRespRcvd", (bit8 *)ptdSMPDiscoverResp, sizeof(smpRespDiscover_t));

#ifndef DIRECT_SMP
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                );
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                );
#endif

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      tdsaSASUpStreamDiscoverExpanderPhy(tiRoot, onePortContext, oneExpander, ptdSMPDiscoverResp);
    }
    else if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      tdsaSASDownStreamDiscoverExpanderPhy(tiRoot, onePortContext, oneExpander, ptdSMPDiscoverResp);
    }
    else if (onePortContext->discovery.status == DISCOVERY_CONFIG_ROUTING)
    {
      /* not done with configuring routing
         1. set the timer
         2. on timer expiration, call tdsaSASDownStreamDiscoverExpanderPhy()
      */
      TI_DBG2(("tdsaDiscoverRespRcvd: still configuring routing; setting timer\n"));
      TI_DBG2(("tdsaDiscoverRespRcvd: onePortContext %p oneDeviceData %p ptdSMPDiscoverResp %p\n", onePortContext, oneDeviceData, ptdSMPDiscoverResp));
      tdhexdump("tdsaDiscoverRespRcvd", (bit8*)ptdSMPDiscoverResp, sizeof(smpRespDiscover_t));

      tdsaConfigureRouteTimer(tiRoot, onePortContext, oneExpander, ptdSMPDiscoverResp);
    }
    else
    {
      /* nothing */
    }
  }
  else if (frameHeader->smpFunctionResult == PHY_VACANT)
  {
    TI_DBG3(("tdsaDiscoverRespRcvd: smpFunctionResult is PHY_VACANT, phyid %d\n",
    oneExpander->discoveringPhyId));
    if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      tdsaSASUpStreamDiscoverExpanderPhySkip(tiRoot, onePortContext, oneExpander);
    }
    else if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      tdsaSASDownStreamDiscoverExpanderPhySkip(tiRoot, onePortContext, oneExpander);
    }
    else if (onePortContext->discovery.status == DISCOVERY_CONFIG_ROUTING)
    {
      /* not done with configuring routing
         1. set the timer
         2. on timer expiration, call tdsaSASDownStreamDiscoverExpanderPhy()
      */
      TI_DBG1(("tdsaDiscoverRespRcvd: still configuring routing; setting timer\n"));
      TI_DBG1(("tdsaDiscoverRespRcvd: onePortContext %p oneDeviceData %p ptdSMPDiscoverResp %p\n", onePortContext, oneDeviceData, ptdSMPDiscoverResp));
      tdhexdump("tdsaDiscoverRespRcvd", (bit8*)ptdSMPDiscoverResp, sizeof(smpRespDiscover_t));

      tdsaConfigureRouteTimer(tiRoot, onePortContext, oneExpander, ptdSMPDiscoverResp);
    }
  }
  else
  {
    TI_DBG1(("tdsaDiscoverRespRcvd: Discovery Error SMP function return result error=%x\n",
             frameHeader->smpFunctionResult));
     tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
  }
  return;
}

/*****************************************************************************
*! \brief  tdsaSASUpStreamDiscoverExpanderPhy
*
*  Purpose:  This function actully does upstream traverse and finds out detailed
*            information about topology.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*  \param   pDiscoverResp: Pointer to the Discovery SMP respsonse.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASUpStreamDiscoverExpanderPhy(
                                   tiRoot_t              *tiRoot,
                                   tdsaPortContext_t     *onePortContext,
                                   tdsaExpander_t        *oneExpander,
                                   smpRespDiscover_t     *pDiscoverResp
                                   )
{
  tdsaDeviceData_t        *oneDeviceData;
  tdsaDeviceData_t        *AttachedDevice = agNULL;
  tdsaExpander_t          *AttachedExpander;
  agsaSASIdentify_t       sasIdentify;
  bit8                    connectionRate;
  bit32                   attachedSasHi, attachedSasLo;
  tdsaSASSubID_t          agSASSubID;

  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: start\n"));
  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }

  oneDeviceData = oneExpander->tdDevice;
  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: Phy #%d of SAS %08x-%08x\n",
           oneExpander->discoveringPhyId,
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("   Attached device: %s\n",
           ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 0 ? "No Device" :
             (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 1 ? "End Device" :
              (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 2 ? "Edge Expander" : "Fanout Expander")))));

  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
  {
    TI_DBG3(("   SAS address    : %08x-%08x\n",
      DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp),
              DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp)));
    TI_DBG3(("   SSP Target     : %d\n", DISCRSP_IS_SSP_TARGET(pDiscoverResp)?1:0));
    TI_DBG3(("   STP Target     : %d\n", DISCRSP_IS_STP_TARGET(pDiscoverResp)?1:0));
    TI_DBG3(("   SMP Target     : %d\n", DISCRSP_IS_SMP_TARGET(pDiscoverResp)?1:0));
    TI_DBG3(("   SATA DEVICE    : %d\n", DISCRSP_IS_SATA_DEVICE(pDiscoverResp)?1:0));
    TI_DBG3(("   SSP Initiator  : %d\n", DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)?1:0));
    TI_DBG3(("   STP Initiator  : %d\n", DISCRSP_IS_STP_INITIATOR(pDiscoverResp)?1:0));
    TI_DBG3(("   SMP Initiator  : %d\n", DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)?1:0));
    TI_DBG3(("   Phy ID         : %d\n", pDiscoverResp->phyIdentifier));
    TI_DBG3(("   Attached Phy ID: %d\n", pDiscoverResp->attachedPhyIdentifier));
  }
  /* end for debugging */

  /* for debugging */
  if (oneExpander->discoveringPhyId != pDiscoverResp->phyIdentifier)
  {
    TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy: !!! Incorrect SMP response !!!\n"));
    TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy: Request PhyID #%d Response PhyID #%d\n", oneExpander->discoveringPhyId, pDiscoverResp->phyIdentifier));
    tdhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover_t));
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
    return;
  }

  /* saving routing attribute for non self-configuring expanders */
  oneExpander->routingAttribute[pDiscoverResp->phyIdentifier] = DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp);

  /* for debugging */
//  dumpRoutingAttributes(tiRoot, oneExpander, pDiscoverResp->phyIdentifier);

  if ( oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE )
  {
    TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: SA_SAS_DEV_TYPE_FANOUT_EXPANDER\n"));
    if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
    {
      TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy: **** Topology Error subtractive routing on fanout expander device\n"));

      /* discovery error */
      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
        = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
        = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;

      /* (2.1.3) discovery done */
      tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
      return;
    }
  }
  else
  {
    TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: SA_SAS_DEV_TYPE_EDGE_EXPANDER\n"));

    if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
    {
      /* Setup sasIdentify for the attached device */
      sasIdentify.phyIdentifier = pDiscoverResp->phyIdentifier;
      sasIdentify.deviceType_addressFrameType = (bit8)(pDiscoverResp->attachedDeviceType & 0x70);
      sasIdentify.initiator_ssp_stp_smp = pDiscoverResp->attached_Ssp_Stp_Smp_Sata_Initiator;
      sasIdentify.target_ssp_stp_smp = pDiscoverResp->attached_SataPS_Ssp_Stp_Smp_Sata_Target;
      *(bit32*)sasIdentify.sasAddressHi = *(bit32*)pDiscoverResp->attachedSasAddressHi;
      *(bit32*)sasIdentify.sasAddressLo = *(bit32*)pDiscoverResp->attachedSasAddressLo;

      /* incremental discovery */
      agSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
      agSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
      agSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
      agSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;

      attachedSasHi = DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp);
      attachedSasLo = DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp);

      /* If the phy has subtractive routing attribute */
      if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
      {
        TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: SA_SAS_ROUTING_SUBTRACTIVE\n"));
        /* Setup upstream phys */
        tdsaSASExpanderUpStreamPhyAdd(tiRoot, oneExpander, (bit8) pDiscoverResp->attachedPhyIdentifier);
        /* If the expander already has an upsteam device set up */
        if (oneExpander->hasUpStreamDevice == agTRUE)
        {
          /* If the sas address doesn't match */
          if ( ((oneExpander->upStreamSASAddressHi != attachedSasHi) ||
                (oneExpander->upStreamSASAddressLo != attachedSasLo)) &&
               (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE ||
                DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
              )
          {
            /* TODO: discovery error, callback */
            TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy: **** Topology Error subtractive routing error - inconsistent SAS address\n"));
            /* call back to notify discovery error */
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            /* discovery done */
            tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
          }
        }
        else
        {
          /* Setup SAS address for up stream device */
          oneExpander->hasUpStreamDevice = agTRUE;
          oneExpander->upStreamSASAddressHi = attachedSasHi;
          oneExpander->upStreamSASAddressLo = attachedSasLo;

          if ( (onePortContext->sasLocalAddressHi != attachedSasHi)
              || (onePortContext->sasLocalAddressLo != attachedSasLo) )
          {
            /* Find the device from the discovered list */
            AttachedDevice = tdsaPortSASDeviceFind(tiRoot, onePortContext, attachedSasLo, attachedSasHi);
            /* If the device has been discovered before */
            if ( AttachedDevice != agNULL)
            {
              TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: Seen This Device Before\n"));
              /* If attached device is an edge expander */
              if ( AttachedDevice->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
              {
                /* The attached device is an expander */
                AttachedExpander = AttachedDevice->tdExpander;
                /* If the two expanders are the root of the two edge expander sets */
                if ( (AttachedExpander->upStreamSASAddressHi ==
                      DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo))
                     && (AttachedExpander->upStreamSASAddressLo ==
                        DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo)) )
                {
                  /* Setup upstream expander for the pExpander */
                  oneExpander->tdUpStreamExpander = AttachedExpander;
                }
                /* If the two expanders are not the root of the two edge expander sets */
                else
                {
                  /* TODO: loop found, discovery error, callback */
                  TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy: **** Topology Error loop detection\n"));
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                  /* discovery done */
                  tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
                }
              }
              /* If attached device is not an edge expander */
              else
              {
                /*TODO: should not happen, ASSERT */
                TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy, *** Attached Device is not Edge. Confused!!\n"));
              }
            }
            /* If the device has not been discovered before */
            else
            {
              /* Add the device */
              TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: New device\n"));
              /* read minimum rate from the configuration
                 onePortContext->LinkRate is SPC's local link rate
              */
              connectionRate = (bit8)(MIN(onePortContext->LinkRate, DISCRSP_GET_LINKRATE(pDiscoverResp)));
              TI_DBG3(("siSASUpStreamDiscoverExpanderPhy: link rate 0x%x\n", onePortContext->LinkRate));
              TI_DBG3(("siSASUpStreamDiscoverExpanderPhy: negotiatedPhyLinkRate 0x%x\n", DISCRSP_GET_LINKRATE(pDiscoverResp)));
              TI_DBG3(("siSASUpStreamDiscoverExpanderPhy: connectionRate 0x%x\n", connectionRate));
              //hhhhhhhh
              if (DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp))
              {
                /* incremental discovery */
                if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
                {
                  AttachedDevice = tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                }
                else
                {
                  /* incremental discovery */
                  AttachedDevice = tdsaFindRegNValid(
                                                     onePortContext->agRoot,
                                                     onePortContext,
                                                     &agSASSubID
                                                     );
                  /* not registered and not valid; add this*/
                  if (AttachedDevice == agNULL)
                  {
                    AttachedDevice = tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                  }
                }
              }
              else
              {
                /* incremental discovery */
                if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
                {
                  AttachedDevice = tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                }
                else
                {
                  /* incremental discovery */
                  AttachedDevice = tdsaFindRegNValid(
                                                     onePortContext->agRoot,
                                                     onePortContext,
                                                     &agSASSubID
                                                     );
                  /* not registered and not valid; add this*/
                  if (AttachedDevice == agNULL)
                  {
                    AttachedDevice = tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                  }
                }
              }
              /* If the device is added successfully */
              if ( AttachedDevice != agNULL)
              {

                 /* (3.1.2.3.2.3.2.1) callback about new device */
                if ( DISCRSP_IS_SSP_TARGET(pDiscoverResp)
                    || DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)
                    || DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)
                    || DISCRSP_IS_SMP_INITIATOR(pDiscoverResp) )
                {
                  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: Found SSP/SMP SAS %08x-%08x\n",
                      attachedSasHi, attachedSasLo));
                }
                else
                {
                  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: Found a SAS STP device.\n"));
                }
                 /* If the attached device is an expander */
                if ( (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE)
                    || (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE) )
                {
                  /* Allocate an expander data structure */
                  AttachedExpander = tdssSASDiscoveringExpanderAlloc(
                                                                     tiRoot,
                                                                     onePortContext,
                                                                     AttachedDevice
                                                                     );

                  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: Found expander=%p\n", AttachedExpander));
                  /* If allocate successfully */
                  if ( AttachedExpander != agNULL)
                  {
                    /* Add the pAttachedExpander to discovering list */
                    tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, AttachedExpander);
                    /* Setup upstream expander for the pExpander */
                    oneExpander->tdUpStreamExpander = AttachedExpander;
                  }
                  /* If failed to allocate */
                  else
                  {
                    TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy, Failed to allocate expander data structure\n"));
                    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
                  }
                }
                /* If the attached device is an end device */
                else
                {
                  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: Found end device\n"));
                  /* LP2006-05-26 added upstream device to the newly found device */
                  AttachedDevice->tdExpander = oneExpander;
                  oneExpander->tdUpStreamExpander = agNULL;
                }
              }
              else
              {
                TI_DBG1(("tdsaSASUpStreamDiscoverExpanderPhy, Failed to add a device\n"));
                tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
              }
            }
          }
        }
      } /* substractive routing */
    }
  }


   oneExpander->discoveringPhyId ++;
   if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
     {
       if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
       {
         TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: DISCOVERY_UP_STREAM find more ...\n"));
         /* continue discovery for the next phy */
         tdsaDiscoverSend(tiRoot, oneDeviceData);
       }
       else
       {
         TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: DISCOVERY_UP_STREAM last phy continue upstream..\n"));

         /* remove the expander from the discovering list */
         tdssSASDiscoveringExpanderRemove(tiRoot, onePortContext, oneExpander);
         /* continue upstream discovering */
         tdsaSASUpStreamDiscovering(tiRoot, onePortContext, oneDeviceData);
       }
   }
   else
   {
      TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: onePortContext->discovery.status not in DISCOVERY_UP_STREAM; status %d\n", onePortContext->discovery.status));

   }

  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhy: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));

  return;
}

// for debugging only
osGLOBAL tdsaExpander_t *
tdsaFindUpStreamConfigurableExp(tiRoot_t              *tiRoot,
                                                                tdsaExpander_t        *oneExpander)
{
  tdsaExpander_t    *ret=agNULL;
  tdsaExpander_t    *UpsreamExpander = oneExpander->tdUpStreamExpander;

  TI_DBG3(("tdsaFindUpStreamConfigurableExp: start\n"));
  TI_DBG3(("tdsaFindUpStreamConfigurableExp: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaFindUpStreamConfigurableExp: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));


  if (UpsreamExpander)
  {
    TI_DBG3(("tdsaFindUpStreamConfigurableExp: NO upsream expander\n"));
  }
  else
  {
    while (UpsreamExpander)
    {
      TI_DBG3(("tdsaFindUpStreamConfigurableExp: exp addrHi 0x%08x\n", UpsreamExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdsaFindUpStreamConfigurableExp: exp addrLo 0x%08x\n", UpsreamExpander->tdDevice->SASAddressID.sasAddressLo));

      UpsreamExpander = UpsreamExpander->tdUpStreamExpander;
    }
  }
  return ret;
}

/*****************************************************************************
*! \brief  tdsaSASUpStreamDiscoverExpanderPhySkip
*
*  Purpose:  This function skips a phy which returned PHY_VACANT in SMP
*            response in upstream
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASUpStreamDiscoverExpanderPhySkip(
                                   tiRoot_t              *tiRoot,
                                   tdsaPortContext_t     *onePortContext,
                                   tdsaExpander_t        *oneExpander
                                   )
{
  tdsaDeviceData_t        *oneDeviceData;
  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: start\n"));
  oneDeviceData = oneExpander->tdDevice;

  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  oneExpander->discoveringPhyId ++;
  if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: More Phys to discover\n"));
      /* continue discovery for the next phy */
      tdsaDiscoverSend(tiRoot, oneDeviceData);
    }
    else
    {
       TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: No More Phys\n"));

       /* remove the expander from the discovering list */
       tdssSASDiscoveringExpanderRemove(tiRoot, onePortContext, oneExpander);
       /* continue upstream discovering */
       tdsaSASUpStreamDiscovering(tiRoot, onePortContext, oneDeviceData);
     }
  }
  else
  {
    TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: onePortContext->discovery.status not in DISCOVERY_UP_STREAM; status %d\n", onePortContext->discovery.status));

  }

  TI_DBG3(("tdsaSASUpStreamDiscoverExpanderPhySkip: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));


  return;
}


// for debugging only
osGLOBAL tdsaExpander_t *
tdsaFindDownStreamConfigurableExp(tiRoot_t              *tiRoot,
                                                                  tdsaExpander_t        *oneExpander)
{
  tdsaExpander_t  *ret=agNULL;
  tdsaExpander_t  *DownsreamExpander = oneExpander->tdCurrentDownStreamExpander;

  TI_DBG3(("tdsaFindDownStreamConfigurableExp: start\n"));
  TI_DBG3(("tdsaFindDownStreamConfigurableExp: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaFindDownStreamConfigurableExp: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));


  if (DownsreamExpander)
  {
    TI_DBG3(("tdsaFindDownStreamConfigurableExp: NO downsream expander\n"));
  }
  else
  {
    while (DownsreamExpander)
    {
      TI_DBG3(("tdsaFindDownStreamConfigurableExp: exp addrHi 0x%08x\n", DownsreamExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdsaFindDownStreamConfigurableExp: exp addrLo 0x%08x\n", DownsreamExpander->tdDevice->SASAddressID.sasAddressLo));

      DownsreamExpander = DownsreamExpander->tdCurrentDownStreamExpander;
    }
  }
  return ret;
}

// for debugging only
osGLOBAL void
dumpRoutingAttributes(
                      tiRoot_t                 *tiRoot,
                      tdsaExpander_t           *oneExpander,
                      bit8                     phyID
                      )
{
  bit32 i;

  TI_DBG3(("dumpRoutingAttributes: start\n"));
  TI_DBG3(("dumpRoutingAttributes: phyID %d\n", phyID));
  TI_DBG3(("dumpRoutingAttributes: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("dumpRoutingAttributes: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

  for(i=0;i <= ((bit32)phyID + 1); i++)
  {
    TI_DBG3(("dumpRoutingAttributes: index %d routing attribute %d\n", i, oneExpander->routingAttribute[i]));
  }
  return;
}

/*****************************************************************************
*! \brief  tdsaDumpAllExp
*
*  Purpose:  This function prints out all expanders seen by discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           None
*
*   \note: For debugging only
*
*****************************************************************************/
osGLOBAL void
tdsaDumpAllExp(
               tiRoot_t                 *tiRoot,
               tdsaPortContext_t        *onePortContext,
               tdsaExpander_t           *oneExpander
              )
{
#if 0 /* for debugging only */
  tdList_t          *ExpanderList;
  tdsaExpander_t    *tempExpander;
  tdsaExpander_t    *UpsreamExpander;
  tdsaExpander_t    *DownsreamExpander;
  tdsaPortContext_t *tmpOnePortContext = onePortContext;

  TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: start\n"));
  TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: onePortcontext %p oneExpander %p\n", onePortContext, oneExpander));

  /* debugging */
  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  if (TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: empty discoveringExpanderList\n"));
    return;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
  }
  ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    UpsreamExpander = tempExpander->tdUpStreamExpander;
    DownsreamExpander = tempExpander->tdCurrentDownStreamExpander;
    TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: expander id %d\n", tempExpander->id));
    TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: exp addrHi 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressHi));
    TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: exp addrLo 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressLo));
    if (UpsreamExpander)
    {
      TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: Up exp addrHi 0x%08x\n", UpsreamExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: Up exp addrLo 0x%08x\n", UpsreamExpander->tdDevice->SASAddressID.sasAddressLo));
    }
    else
    {
      TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: No Upstream expander\n"));
    }
    if (DownsreamExpander)
    {
      TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: Down exp addrHi 0x%08x\n", DownsreamExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: Down exp addrLo 0x%08x\n", DownsreamExpander->tdDevice->SASAddressID.sasAddressLo));
    }
    else
    {
      TI_DBG3(("tdssSASDiscoveringExpander tdsaDumpAllExp: No Downstream expander\n"));
    }

    ExpanderList = ExpanderList->flink;
  }
#endif
  return;

}

/*****************************************************************************
*! \brief  tdsaDumpAllUpExp
*
*  Purpose:  This function prints out all upstream expanders seen by discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           None
*
*   \note: For debugging only
*
*****************************************************************************/
osGLOBAL void
tdsaDumpAllUpExp(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext,
                              tdsaExpander_t           *oneExpander
                              )
{
  return;

}

/*****************************************************************************
*! \brief  tdsaDumpAllFreeExp
*
*  Purpose:  This function prints out all free expanders.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \return:
*           None
*
*   \note: For debugging only
*
*****************************************************************************/
osGLOBAL void
tdsaDumpAllFreeExp(
                   tiRoot_t                 *tiRoot
                  )
{

  return;
}

/*****************************************************************************
*! \brief  tdsaDuplicateConfigSASAddr
*
*  Purpose:  This function finds whether SAS address has added to the routing
*            table of expander or not.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneExpander: Pointer to the expander data.
*  \param   configSASAddressHi: Upper 4 byte of SAS address.
*  \param   configSASAddressLo: Lower 4 byte of SAS address.
*
*  \return:
*           agTRUE  No need to add configSASAddress.
*           agFALSE Need to add configSASAddress.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaDuplicateConfigSASAddr(
                      tiRoot_t                 *tiRoot,
                      tdsaExpander_t           *oneExpander,
                      bit32                    configSASAddressHi,
                      bit32                    configSASAddressLo
                      )
{
  bit32 i;
  bit32 ret = agFALSE;
  TI_DBG3(("tdsaDuplicateConfigSASAddr: start\n"));

  if (oneExpander == agNULL)
  {
    TI_DBG3(("tdsaDuplicateConfigSASAddr: NULL expander\n"));
    return agTRUE;
  }

  if (oneExpander->tdDevice->SASAddressID.sasAddressHi == configSASAddressHi &&
      oneExpander->tdDevice->SASAddressID.sasAddressLo == configSASAddressLo
     )
  {
    TI_DBG3(("tdsaDuplicateConfigSASAddr: unnecessary\n"));
    return agTRUE;
  }

  TI_DBG3(("tdsaDuplicateConfigSASAddr: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaDuplicateConfigSASAddr: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaDuplicateConfigSASAddr: configsasAddressHi 0x%08x\n", configSASAddressHi));
  TI_DBG3(("tdsaDuplicateConfigSASAddr: configsasAddressLo 0x%08x\n", configSASAddressLo));
  TI_DBG3(("tdsaDuplicateConfigSASAddr: configSASAddrTableIndex %d\n", oneExpander->configSASAddrTableIndex));
  for(i=0;i<oneExpander->configSASAddrTableIndex;i++)
  {
    if (oneExpander->configSASAddressHiTable[i] == configSASAddressHi &&
        oneExpander->configSASAddressLoTable[i] == configSASAddressLo
        )
    {
      TI_DBG3(("tdsaDuplicateConfigSASAddr: FOUND!!!\n"));
      ret = agTRUE;
      break;
    }
  }
  /* new one; let's add it */
  if (ret == agFALSE)
  {
    TI_DBG3(("tdsaDuplicateConfigSASAddr: adding configSAS Addr!!!\n"));
    TI_DBG3(("tdsaDuplicateConfigSASAddr: configSASAddrTableIndex %d\n", oneExpander->configSASAddrTableIndex));
    oneExpander->configSASAddressHiTable[oneExpander->configSASAddrTableIndex] = configSASAddressHi;
    oneExpander->configSASAddressLoTable[oneExpander->configSASAddrTableIndex] = configSASAddressLo;
    oneExpander->configSASAddrTableIndex++;
  }

  return ret;
}
/*****************************************************************************
*! \brief  tdsaFindConfigurableExp
*
*  Purpose:  This function finds whether there is a configurable expander in
*            the upstream expander list.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           agTRUE  There is configurable expander.
*           agFALSE There is not configurable expander.
*
*   \note:
*
*****************************************************************************/
osGLOBAL tdsaExpander_t *
tdsaFindConfigurableExp(
                         tiRoot_t                 *tiRoot,
                         tdsaPortContext_t        *onePortContext,
                         tdsaExpander_t           *oneExpander
                        )
{
  tdsaExpander_t    *tempExpander;
  tdsaPortContext_t *tmpOnePortContext = onePortContext;
  tdsaExpander_t    *ret = agNULL;

  TI_DBG3(("tdsaFindConfigurableExp: start\n"));

  if (oneExpander == agNULL)
  {
    TI_DBG3(("tdsaFindConfigurableExp: NULL expander\n"));
    return agNULL;
  }

  TI_DBG3(("tdsaFindConfigurableExp: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaFindConfigurableExp: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  if (TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
    TI_DBG3(("tdsaFindConfigurableExp: empty UpdiscoveringExpanderList\n"));
    return agNULL;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
  }
  tempExpander = oneExpander->tdUpStreamExpander;
  while (tempExpander)
  {
    TI_DBG3(("tdsaFindConfigurableExp: loop exp addrHi 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressHi));
    TI_DBG3(("tdsaFindConfigurableExp: loop exp addrLo 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressLo));
    if (tempExpander->configRouteTable)
    {
      TI_DBG3(("tdsaFindConfigurableExp: found configurable expander\n"));
      ret = tempExpander;
      break;
    }
   tempExpander = tempExpander->tdUpStreamExpander;
  }

  return ret;
}

/*****************************************************************************
*! \brief  tdsaSASDownStreamDiscoverExpanderPhy
*
*  Purpose:  This function actully does downstream traverse and finds out detailed
*            information about topology.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*  \param   pDiscoverResp: Pointer to the Discovery SMP respsonse.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASDownStreamDiscoverExpanderPhy(
                                     tiRoot_t              *tiRoot,
                                     tdsaPortContext_t     *onePortContext,
                                     tdsaExpander_t        *oneExpander,
                                     smpRespDiscover_t     *pDiscoverResp
                                     )
{
  tdsaDeviceData_t        *oneDeviceData;
  tdsaExpander_t          *UpStreamExpander;
  tdsaDeviceData_t        *AttachedDevice = agNULL;
  tdsaExpander_t          *AttachedExpander;
  agsaSASIdentify_t       sasIdentify;
  bit8                    connectionRate;
  bit32                   attachedSasHi, attachedSasLo;
  tdsaSASSubID_t          agSASSubID;
  tdsaExpander_t          *ConfigurableExpander = agNULL;
  bit32                   dupConfigSASAddr = agFALSE;
  bit32                   configSASAddressHi;
  bit32                   configSASAddressLo;

  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: start\n"));
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

  TD_ASSERT(tiRoot, "(tdsaSASDownStreamDiscoverExpanderPhy) agRoot NULL");
  TD_ASSERT(onePortContext, "(tdsaSASDownStreamDiscoverExpanderPhy) pPort NULL");
  TD_ASSERT(oneExpander, "(tdsaSASDownStreamDiscoverExpanderPhy) pExpander NULL");
  TD_ASSERT(pDiscoverResp, "(tdsaSASDownStreamDiscoverExpanderPhy) pDiscoverResp NULL");

  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: onePortContxt=%p  oneExpander=%p  oneDeviceData=%p\n", onePortContext, oneExpander, oneExpander->tdDevice));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
#ifdef TD_INTERNAL_DEBUG
    tdsaDumpAllExp(tiRoot, onePortContext, oneExpander);
    tdsaFindUpStreamConfigurableExp(tiRoot, oneExpander);
    tdsaFindDownStreamConfigurableExp(tiRoot, oneExpander);
#endif
  /* (1) Find the device structure of the expander */
  oneDeviceData = oneExpander->tdDevice;
  TD_ASSERT(oneDeviceData, "(tdsaSASDownStreamDiscoverExpanderPhy) pDevice NULL");

  /* for debugging */
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Phy #%d of SAS %08x-%08x\n",
           oneExpander->discoveringPhyId,
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("   Attached device: %s\n",
           ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 0 ? "No Device" :
             (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 1 ? "End Device" :
              (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 2 ? "Edge Expander" : "Fanout Expander")))));
  /* for debugging */
  if (oneExpander->discoveringPhyId != pDiscoverResp->phyIdentifier)
  {
    TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: !!! Incorrect SMP response !!!\n"));
    TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: Request PhyID #%d Response PhyID #%d\n", oneExpander->discoveringPhyId, pDiscoverResp->phyIdentifier));
    tdhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover_t));
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
    return;
  }

#ifdef TD_INTERNAL_DEBUG  /* debugging only */
  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_NO_DEVICE)
  {
    tdhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover_t));
  }
#endif
  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
  {
    TI_DBG3(("   SAS address    : %08x-%08x\n",
      DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp),
              DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp)));
    TI_DBG3(("   SSP Target     : %d\n", DISCRSP_IS_SSP_TARGET(pDiscoverResp)?1:0));
    TI_DBG3(("   STP Target     : %d\n", DISCRSP_IS_STP_TARGET(pDiscoverResp)?1:0));
    TI_DBG3(("   SMP Target     : %d\n", DISCRSP_IS_SMP_TARGET(pDiscoverResp)?1:0));
    TI_DBG3(("   SATA DEVICE    : %d\n", DISCRSP_IS_SATA_DEVICE(pDiscoverResp)?1:0));
    TI_DBG3(("   SSP Initiator  : %d\n", DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)?1:0));
    TI_DBG3(("   STP Initiator  : %d\n", DISCRSP_IS_STP_INITIATOR(pDiscoverResp)?1:0));
    TI_DBG3(("   SMP Initiator  : %d\n", DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)?1:0));
    TI_DBG3(("   Phy ID         : %d\n", pDiscoverResp->phyIdentifier));
    TI_DBG3(("   Attached Phy ID: %d\n", pDiscoverResp->attachedPhyIdentifier));

  }
  /* end for debugging */

  /* saving routing attribute for non self-configuring expanders */
  oneExpander->routingAttribute[pDiscoverResp->phyIdentifier] = DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp);

  /* for debugging */
//  dumpRoutingAttributes(tiRoot, oneExpander, pDiscoverResp->phyIdentifier);

  oneExpander->discoverSMPAllowed = agTRUE;

  /* If a device is attached */
  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) !=  SAS_NO_DEVICE)
  {
    /* Setup sasIdentify for the attached device */
    sasIdentify.phyIdentifier = pDiscoverResp->phyIdentifier;
    sasIdentify.deviceType_addressFrameType = (bit8)(pDiscoverResp->attachedDeviceType & 0x70);
    sasIdentify.initiator_ssp_stp_smp = pDiscoverResp->attached_Ssp_Stp_Smp_Sata_Initiator;
    sasIdentify.target_ssp_stp_smp = pDiscoverResp->attached_SataPS_Ssp_Stp_Smp_Sata_Target;
    *(bit32*)sasIdentify.sasAddressHi = *(bit32*)pDiscoverResp->attachedSasAddressHi;
    *(bit32*)sasIdentify.sasAddressLo = *(bit32*)pDiscoverResp->attachedSasAddressLo;

    /* incremental discovery */
    agSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
    agSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
    agSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
    agSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;

    attachedSasHi = DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp);
    attachedSasLo = DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp);

    /* If it's a direct routing */
    if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_DIRECT)
    {
      /* If the attached device is an expander */
      if ( (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
          || (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) )

      {
        TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error direct routing can't connect to expander\n"));
        onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
           = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
        onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
          = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
        onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;

        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);

        return;
      }
    }

    /* If the expander's attached device is not myself */
    if ( (attachedSasHi != onePortContext->sasLocalAddressHi)
         || (attachedSasLo != onePortContext->sasLocalAddressLo) )
    {
      /* Find the attached device from discovered list */
      AttachedDevice = tdsaPortSASDeviceFind(tiRoot, onePortContext, attachedSasLo, attachedSasHi);
      /* If the device has not been discovered before */
      if ( AttachedDevice == agNULL) //11
      {
        /* If the phy has subtractive routing attribute */
        if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE &&
             (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE ||
              DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
           )
        {
          /* TODO: discovery error, callback */
          TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error subtractive routing error - inconsistent SAS address\n"));
          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
            = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
            = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
          onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
          /* discovery done */
          tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        }
        else
        {
          /* Add the device */
          /* read minimum rate from the configuration
             onePortContext->LinkRate is SPC's local link rate
          */
          connectionRate = (bit8)(MIN(onePortContext->LinkRate, DISCRSP_GET_LINKRATE(pDiscoverResp)));
          TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: link rate 0x%x\n", DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo)));
          TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: negotiatedPhyLinkRate 0x%x\n", DISCRSP_GET_LINKRATE(pDiscoverResp)));
          TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: connectionRate 0x%x\n", connectionRate));

          if (DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp))
          {
            if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
            {
              AttachedDevice = tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
            }
            else
            {
              /* incremental discovery */
              AttachedDevice = tdsaFindRegNValid(
                                                 onePortContext->agRoot,
                                                 onePortContext,
                                                 &agSASSubID
                                                 );
              /* not registered and not valid; add this*/
              if (AttachedDevice == agNULL)
              {
                AttachedDevice = tdsaPortSASDeviceAdd(
                                                      tiRoot,
                                                      onePortContext,
                                                      sasIdentify,
                                                      agFALSE,
                                                      connectionRate,
                                                      IT_NEXUS_TIMEOUT,
                                                      0,
                                                      STP_DEVICE_TYPE,
                                                      oneDeviceData,
                                                      pDiscoverResp->phyIdentifier
                                                      );
              }
            }
          }
          else
          {
            if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
            {
              AttachedDevice = tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
            }
            else
            {
              /* incremental discovery */
              AttachedDevice = tdsaFindRegNValid(
                                                 onePortContext->agRoot,
                                                 onePortContext,
                                                 &agSASSubID
                                                 );
              /* not registered and not valid; add this*/
              if (AttachedDevice == agNULL)
              {
                AttachedDevice = tdsaPortSASDeviceAdd(
                                                      tiRoot,
                                                      onePortContext,
                                                      sasIdentify,
                                                      agFALSE,
                                                      connectionRate,
                                                      IT_NEXUS_TIMEOUT,
                                                      0,
                                                      SAS_DEVICE_TYPE,
                                                      oneDeviceData,
                                                      pDiscoverResp->phyIdentifier
                                                      );
              }
            }
          }
          TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: newDevice  pDevice=%p\n", AttachedDevice));
          /* If the device is added successfully */
          if ( AttachedDevice != agNULL)
          {
            if ( SA_IDFRM_IS_SSP_TARGET(&sasIdentify)
                 || SA_IDFRM_IS_SMP_TARGET(&sasIdentify)
                 || SA_IDFRM_IS_SSP_INITIATOR(&sasIdentify)
                 || SA_IDFRM_IS_SMP_INITIATOR(&sasIdentify) )
            {
              TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Report a new SAS device !!\n"));

            }
            else
            {
              if ( SA_IDFRM_IS_STP_TARGET(&sasIdentify) ||
                   SA_IDFRM_IS_SATA_DEVICE(&sasIdentify) )
              {

                TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Found an STP or SATA device.\n"));
              }
              else
              {
                TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Found Other type of device.\n"));
              }
            }

            /* LP2006-05-26 added upstream device to the newly found device */
            AttachedDevice->tdExpander = oneExpander;

            /* If the phy has table routing attribute */
            if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE)
            {
              /* If the attached device is a fan out expander */
              if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
              {
                /* TODO: discovery error, callback */
                TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error two table routing phys are connected\n"));
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                /* discovery done */
                tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
              }
              else if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE)
              {
                /* Allocate an expander data structure */
                AttachedExpander = tdssSASDiscoveringExpanderAlloc(tiRoot, onePortContext, AttachedDevice);

                TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Found a EDGE exp device.%p\n", AttachedExpander));
                /* If allocate successfully */
                if ( AttachedExpander != agNULL)
                {
                  /* set up downstream information on configurable expander */
                  if (oneExpander->configRouteTable)
                  {
                    tdsaSASExpanderDownStreamPhyAdd(tiRoot, oneExpander, (bit8) oneExpander->discoveringPhyId);
                  }
                  /* Setup upstream information */
                  tdsaSASExpanderUpStreamPhyAdd(tiRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
                  AttachedExpander->hasUpStreamDevice = agTRUE;
                  AttachedExpander->upStreamSASAddressHi
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  AttachedExpander->upStreamSASAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  AttachedExpander->tdUpStreamExpander = oneExpander;
                  /* (2.3.2.2.2.2.2.2.2) Add the pAttachedExpander to discovering list */
                  tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, AttachedExpander);
                }
                /* If failed to allocate */
                else
                {
                  TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy, Failed to allocate expander data structure\n"));
                  /*  discovery done */
                  tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
                }
              }
            }
            /* If status is still DISCOVERY_DOWN_STREAM */
            if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
            {
              TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 1st before\n"));
              tdsaDumpAllUpExp(tiRoot, onePortContext, oneExpander);
              UpStreamExpander = oneExpander->tdUpStreamExpander;
              ConfigurableExpander = tdsaFindConfigurableExp(tiRoot, onePortContext, oneExpander);
              configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
              configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo);
              if (ConfigurableExpander)
              {
                if ( (ConfigurableExpander->tdDevice->SASAddressID.sasAddressHi
                      == DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo)) &&
                     (ConfigurableExpander->tdDevice->SASAddressID.sasAddressLo
                      == DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo))
                   )
                { /* directly attached between oneExpander and ConfigurableExpander */
                  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 1st before loc 1\n"));
                  configSASAddressHi = oneExpander->tdDevice->SASAddressID.sasAddressHi;
                  configSASAddressLo = oneExpander->tdDevice->SASAddressID.sasAddressLo;
                }
                else
                {
                  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 1st before loc 2\n"));
                  configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
                  configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo);
                }
              } /* if !ConfigurableExpander */
              dupConfigSASAddr = tdsaDuplicateConfigSASAddr(tiRoot,
                                                          ConfigurableExpander,
                                                          configSASAddressHi,
                                                          configSASAddressLo
                                                          );


              if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
              {
                TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 1st q123\n"));
                UpStreamExpander->tdCurrentDownStreamExpander = oneExpander;
                ConfigurableExpander->currentDownStreamPhyIndex =
                        tdsaFindCurrentDownStreamPhyIndex(tiRoot, ConfigurableExpander);
                ConfigurableExpander->tdReturnginExpander = oneExpander;
                tdsaSASRoutingEntryAdd(tiRoot,
                                       ConfigurableExpander,
                                       ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                                       configSASAddressHi,
                                       configSASAddressLo
                                       );
              }
            }
          }
          /*  If fail to add the device */
          else
          {
            TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy, Failed to add a device\n"));
            /*  discovery done */
            tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
          }
        }
      }
      /* If the device has been discovered before */
      else /* haha discovered before */
      {
        /* If the phy has subtractive routing attribute */
        if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
        {
          /* If the expander doesn't have up stream device */
          if ( oneExpander->hasUpStreamDevice == agFALSE)
          {
            /* TODO: discovery error, callback */
            TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error loop, or end device connects to two expanders\n"));
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            /* discovery done */
            tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
          }
          /* If the expander has up stream device */
          else
          {
            /* If sas address doesn't match */
            if ( (oneExpander->upStreamSASAddressHi != attachedSasHi)
                 || (oneExpander->upStreamSASAddressLo != attachedSasLo) )
            {
              /* TODO: discovery error, callback */
              TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error two subtractive phys\n"));
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
              /* discovery done */
              tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
            }
          }
        }
        /* If the phy has table routing attribute */
        else if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE)
        {
          /* If the attached device is a fan out expander */
          if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
          {
            /* (2.3.3.2.1.1) TODO: discovery error, callback */
            TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error fan out expander to routing table phy\n"));
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            /* discovery done */
            tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
          }
          /* If the attached device is an edge expander */
          else if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE)
          {
            /* Setup up stream inform */
            AttachedExpander = AttachedDevice->tdExpander;
            TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Found edge expander=%p\n", AttachedExpander));
            //hhhhhh
            /* If the attached expander has up stream device */
            if ( AttachedExpander->hasUpStreamDevice == agTRUE)
            {
              /* compare the sas address */
              if ( (AttachedExpander->upStreamSASAddressHi
                    != DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo))
                   || (AttachedExpander->upStreamSASAddressLo
                       != DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo)))
              {
                /* TODO: discovery error, callback */
                TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error two table routing phys connected (1)\n"));
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                /* discovery done */
                tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
              }
              else
              {
                TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Add edge expander=%p\n", AttachedExpander));
                /* set up downstream information on configurable expander */
                if (oneExpander->configRouteTable)
                {
                  tdsaSASExpanderDownStreamPhyAdd(tiRoot, oneExpander, (bit8) oneExpander->discoveringPhyId);
                }
                /* haha */
                tdsaSASExpanderUpStreamPhyAdd(tiRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
                /* Add the pAttachedExpander to discovering list */
                tdssSASDiscoveringExpanderAdd(tiRoot, onePortContext, AttachedExpander);
              }
            }
            /* If the attached expander doesn't have up stream device */
            else
            {
              /* TODO: discovery error, callback */
              TI_DBG1(("tdsaSASDownStreamDiscoverExpanderPhy: **** Topology Error two table routing phys connected (2)\n"));
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
              /* discovery done */
              tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
            }
          }
        } /* for else if (DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE) */

        /* do this regradless of sub or table */
        /* If status is still DISCOVERY_DOWN_STREAM */
        if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
        {
          TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 2nd before\n"));
          tdsaDumpAllUpExp(tiRoot, onePortContext, oneExpander);

          UpStreamExpander = oneExpander->tdUpStreamExpander;
          ConfigurableExpander = tdsaFindConfigurableExp(tiRoot, onePortContext, oneExpander);
          configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
          configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo);
          if (ConfigurableExpander)
          {
            if ( (ConfigurableExpander->tdDevice->SASAddressID.sasAddressHi
                 == DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo)) &&
                 (ConfigurableExpander->tdDevice->SASAddressID.sasAddressLo
                   == DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo))
               )
            { /* directly attached between oneExpander and ConfigurableExpander */
              TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 2nd before loc 1\n"));
              configSASAddressHi = oneExpander->tdDevice->SASAddressID.sasAddressHi;
              configSASAddressLo = oneExpander->tdDevice->SASAddressID.sasAddressLo;
            }
            else
            {
              TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 2nd before loc 2\n"));
              configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
              configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo);
            }
          } /* if !ConfigurableExpander */
          dupConfigSASAddr = tdsaDuplicateConfigSASAddr(tiRoot,
                                                        ConfigurableExpander,
                                                        configSASAddressHi,
                                                        configSASAddressLo
                                                        );

          if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
          {
            TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 2nd q123 \n"));
            UpStreamExpander->tdCurrentDownStreamExpander = oneExpander;
            ConfigurableExpander->currentDownStreamPhyIndex =
                        tdsaFindCurrentDownStreamPhyIndex(tiRoot, ConfigurableExpander);
            ConfigurableExpander->tdReturnginExpander = oneExpander;
            tdsaSASRoutingEntryAdd(tiRoot,
                                   ConfigurableExpander,
                                   ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                                   configSASAddressHi,
                                   configSASAddressLo
                                   );
          }
        } /* if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM) */
        /* incremental discovery */
        if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_INCREMENTAL_START)
        {
          connectionRate = (bit8)(MIN(onePortContext->LinkRate, DISCRSP_GET_LINKRATE(pDiscoverResp)));

          if (DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp))
          {
            TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: incremental SATA_STP\n"));

            tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );
          }
          else
          {
            TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: incremental SAS\n"));

             tdsaPortSASDeviceAdd(
                                                    tiRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    IT_NEXUS_TIMEOUT,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    pDiscoverResp->phyIdentifier
                                                    );

          }
        }


      }/* else; existing devce */
    } /* not attached to myself */
    /* If the attached device is myself */
    else
    {
      TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: Found Self\n"));
      TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 3rd before\n"));
      tdsaDumpAllUpExp(tiRoot, onePortContext, oneExpander);

      UpStreamExpander = oneExpander->tdUpStreamExpander;
      ConfigurableExpander = tdsaFindConfigurableExp(tiRoot, onePortContext, oneExpander);
      dupConfigSASAddr = tdsaDuplicateConfigSASAddr(tiRoot,
                                                    ConfigurableExpander,
                                                    onePortContext->sasLocalAddressHi,
                                                    onePortContext->sasLocalAddressLo
                                                    );

      if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
      {
        TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: 3rd q123 Setup routing table\n"));
        UpStreamExpander->tdCurrentDownStreamExpander = oneExpander;
        ConfigurableExpander->currentDownStreamPhyIndex =
                        tdsaFindCurrentDownStreamPhyIndex(tiRoot, ConfigurableExpander);
        ConfigurableExpander->tdReturnginExpander = oneExpander;
        tdsaSASRoutingEntryAdd(tiRoot,
                               ConfigurableExpander,
                               ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                               onePortContext->sasLocalAddressHi,
                               onePortContext->sasLocalAddressLo
                               );
      }
    }
  }
  /* If no device is attached */
  else
  {
  }


  /* Increment the discovering phy id */
  oneExpander->discoveringPhyId ++;

  /* If the discovery status is DISCOVERY_DOWN_STREAM */
  if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM )
  {
    /* If not the last phy */
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: More Phys to discover\n"));
      /* continue discovery for the next phy */
      tdsaDiscoverSend(tiRoot, oneDeviceData);
    }
    /* If the last phy */
    else
    {
      TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: No More Phys\n"));

      /* remove the expander from the discovering list */
      tdssSASDiscoveringExpanderRemove(tiRoot, onePortContext, oneExpander);
      /* continue downstream discovering */
      tdsaSASDownStreamDiscovering(tiRoot, onePortContext, oneDeviceData);
    }
  }
  else
  {
    TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: onePortContext->discovery.status not in DISCOVERY_DOWN_STREAM; status %d\n", onePortContext->discovery.status));
  }
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhy: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));

  return;
}

/*****************************************************************************
*! \brief  tdsaSASDownStreamDiscoverExpanderPhySkip
*
*  Purpose:  This function skips a phy which returned PHY_VACANT in SMP
*            response in downstream
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASDownStreamDiscoverExpanderPhySkip(
                                     tiRoot_t              *tiRoot,
                                     tdsaPortContext_t     *onePortContext,
                                     tdsaExpander_t        *oneExpander
                                     )
{
  tdsaDeviceData_t        *oneDeviceData;
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: start\n"));
  oneDeviceData = oneExpander->tdDevice;

  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  /* Increment the discovering phy id */
  oneExpander->discoveringPhyId ++;

  /* If the discovery status is DISCOVERY_DOWN_STREAM */
  if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM )
  {
    /* If not the last phy */
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: More Phys to discover\n"));
      /* continue discovery for the next phy */
      tdsaDiscoverSend(tiRoot, oneDeviceData);
    }
    /* If the last phy */
    else
    {
      TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: No More Phys\n"));

      /* remove the expander from the discovering list */
      tdssSASDiscoveringExpanderRemove(tiRoot, onePortContext, oneExpander);
      /* continue downstream discovering */
      tdsaSASDownStreamDiscovering(tiRoot, onePortContext, oneDeviceData);
    }
  }
  else
  {
    TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: onePortContext->discovery.status not in DISCOVERY_DOWN_STREAM; status %d\n", onePortContext->discovery.status));
  }
  TI_DBG3(("tdsaSASDownStreamDiscoverExpanderPhySkip: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));

  return;
}

/*****************************************************************************
*! \brief  tdsaSASRoutingEntryAdd
*
*  Purpose:  This function adds a routing entry in the configurable expander.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneExpander: Pointer to the expander data.
*  \param   phyId: Phy identifier.
*  \param   configSASAddressHi: Upper 4 byte of SAS address.
*  \param   configSASAddressLo: Lower 4 byte of SAS address.
*
*  \return:
*           agTRUE   Routing entry is added successfully
*           agFALSE  Routing entry is not added successfully
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaSASRoutingEntryAdd(
                       tiRoot_t          *tiRoot,
                       tdsaExpander_t    *oneExpander,
                       bit32             phyId,
                       bit32             configSASAddressHi,
                       bit32             configSASAddressLo
                       )
{
  bit32                                   ret = agTRUE;
  smpReqConfigureRouteInformation_t       confRoutingInfo;
  tdsaPortContext_t                       *onePortContext;
  bit32                                   i;
  agsaRoot_t                              *agRoot;

  TI_DBG3(("tdsaSASRoutingEntryAdd: start\n"));
  TI_DBG3(("tdsaSASRoutingEntryAdd: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaSASRoutingEntryAdd: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaSASRoutingEntryAdd: phyid %d\n", phyId));

  /* needs to compare the location of oneExpander and configSASAddress
     add only if
     oneExpander
          |
     configSASaddress

  */
  if (oneExpander->tdDevice->SASAddressID.sasAddressHi == configSASAddressHi &&
      oneExpander->tdDevice->SASAddressID.sasAddressLo == configSASAddressLo
     )
  {
    TI_DBG3(("tdsaSASRoutingEntryAdd: unnecessary\n"));
    return ret;
  }
  if (oneExpander->routingAttribute[phyId] != SAS_ROUTING_TABLE)
  {
    TI_DBG3(("tdsaSASRoutingEntryAdd: not table routing, routing is %d\n", oneExpander->routingAttribute[phyId]));
    return ret;
  }

  agRoot = oneExpander->tdDevice->agRoot;
  onePortContext = oneExpander->tdDevice->tdPortContext;

  onePortContext->discovery.status = DISCOVERY_CONFIG_ROUTING;

  /* reset smpReqConfigureRouteInformation_t */
  osti_memset(&confRoutingInfo, 0, sizeof(smpReqConfigureRouteInformation_t));
  if ( oneExpander->currentIndex[phyId] < oneExpander->routingIndex )
  {
    TI_DBG3(("tdsaSASRoutingEntryAdd: adding sasAddressHi 0x%08x\n", configSASAddressHi));
    TI_DBG3(("tdsaSASRoutingEntryAdd: adding sasAddressLo 0x%08x\n", configSASAddressLo));
    TI_DBG3(("tdsaSASRoutingEntryAdd: phyid %d currentIndex[phyid] %d\n", phyId, oneExpander->currentIndex[phyId]));

    oneExpander->configSASAddressHi = configSASAddressHi;
    oneExpander->configSASAddressLo = configSASAddressLo;
    confRoutingInfo.reserved1[0] = 0;
    confRoutingInfo.reserved1[1] = 0;
    OSSA_WRITE_BE_16(agRoot, confRoutingInfo.expanderRouteIndex, 0, (oneExpander->currentIndex[phyId]));
    confRoutingInfo.reserved2 = 0;
    confRoutingInfo.phyIdentifier = (bit8)phyId;
    confRoutingInfo.reserved3[0] = 0;
    confRoutingInfo.reserved3[1] = 0;
    confRoutingInfo.disabledBit_reserved4 = 0;
    confRoutingInfo.reserved5[0] = 0;
    confRoutingInfo.reserved5[1] = 0;
    confRoutingInfo.reserved5[2] = 0;
    OSSA_WRITE_BE_32(agRoot, confRoutingInfo.routedSasAddressHi, 0, configSASAddressHi);
    OSSA_WRITE_BE_32(agRoot, confRoutingInfo.routedSasAddressLo, 0, configSASAddressLo);
    for ( i = 0; i < 16; i ++ )
    {
      confRoutingInfo.reserved6[i] = 0;
    }
    tdSMPStart(tiRoot, agRoot, oneExpander->tdDevice, SMP_CONFIGURE_ROUTING_INFORMATION, (bit8 *)&confRoutingInfo, sizeof(smpReqConfigureRouteInformation_t), AGSA_SMP_INIT_REQ, agNULL, 0);

    oneExpander->currentIndex[phyId] ++;
  }
  else
  {
    TI_DBG1(("tdsaSASRoutingEntryAdd: Discovery Error routing index overflow for currentIndex=%d, routingIndex=%d\n", oneExpander->currentIndex[phyId], oneExpander->routingIndex));
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);

    ret = agFALSE;
  }


  return ret;
}
/*****************************************************************************
*! \brief  tdsaConfigRoutingInfoRespRcvd
*
*  Purpose:  This function processes Configure Routing Information response.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
/* needs to traverse only upstream not downstream */
osGLOBAL void
tdsaConfigRoutingInfoRespRcvd(
                              tiRoot_t              *tiRoot,
                              agsaRoot_t            *agRoot,
                              agsaIORequest_t       *agIORequest,
                              tdsaDeviceData_t      *oneDeviceData,
                              tdssSMPFrameHeader_t  *frameHeader,
                              agsaFrameHandle_t     frameHandle
                              )
{
  tdsaExpander_t                          *oneExpander = oneDeviceData->tdExpander;
  tdsaExpander_t                          *UpStreamExpander;
  tdsaExpander_t                          *DownStreamExpander;
  tdsaExpander_t                          *ReturningExpander;
  tdsaExpander_t                          *ConfigurableExpander;

  tdsaPortContext_t                       *onePortContext;
  tdsaDeviceData_t                        *ReturningExpanderDeviceData;
  bit32                                   dupConfigSASAddr = agFALSE;

  TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: start\n"));
  TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->tdPortContext;

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaConfigRoutingInfoRespRcvd: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED ||
       frameHeader->smpFunctionResult == PHY_VACANT
     )
  {
    DownStreamExpander = oneExpander->tdCurrentDownStreamExpander;
    if (DownStreamExpander != agNULL)
    {
      DownStreamExpander->currentUpStreamPhyIndex ++;
      TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: DownStreamExpander->currentUpStreamPhyIndex %d\n", DownStreamExpander->currentUpStreamPhyIndex));
      TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: DownStreamExpander->numOfUpStreamPhys %d\n", DownStreamExpander->numOfUpStreamPhys));
      TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: DownStreamExpander addrHi 0x%08x\n", DownStreamExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: DownStreamExpander addrLo 0x%08x\n", DownStreamExpander->tdDevice->SASAddressID.sasAddressLo));

    }

    oneExpander->currentDownStreamPhyIndex++;
    TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: oneExpander->currentDownStreamPhyIndex %d oneExpander->numOfDownStreamPhys %d\n", oneExpander->currentDownStreamPhyIndex, oneExpander->numOfDownStreamPhys));

    if ( DownStreamExpander != agNULL)
    {
      if (DownStreamExpander->currentUpStreamPhyIndex < DownStreamExpander->numOfUpStreamPhys)
      {
        TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: first if\n"));
        TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: DownStreamExpander->currentUpStreamPhyIndex %d\n", DownStreamExpander->currentUpStreamPhyIndex));

        TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: DownStreamExpander->upStreamPhys[] %d\n", DownStreamExpander->upStreamPhys[DownStreamExpander->currentUpStreamPhyIndex]));

        tdsaSASRoutingEntryAdd(tiRoot,
                               oneExpander,
                               DownStreamExpander->upStreamPhys[DownStreamExpander->currentUpStreamPhyIndex],
                               oneExpander->configSASAddressHi,
                               oneExpander->configSASAddressLo
                               );
      }
      else
      {
        /* traversing up till discovery Root onePortContext->discovery.RootExp */
        TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: else\n"));

        UpStreamExpander = oneExpander->tdUpStreamExpander;
        ConfigurableExpander = tdsaFindConfigurableExp(tiRoot, onePortContext, oneExpander);
        if (UpStreamExpander != agNULL)
        {
          TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: UpStreamExpander addrHi 0x%08x\n", UpStreamExpander->tdDevice->SASAddressID.sasAddressHi));
          TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: UpStreamExpander addrLo 0x%08x\n", UpStreamExpander->tdDevice->SASAddressID.sasAddressLo));
          dupConfigSASAddr = tdsaDuplicateConfigSASAddr(tiRoot,
                                                      ConfigurableExpander,
                                                      oneExpander->configSASAddressHi,
                                                      oneExpander->configSASAddressLo
                                                      );

          if ( ConfigurableExpander != agNULL && dupConfigSASAddr == agFALSE)
          {
            TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: else if\n"));

            TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ConfigurableExpander addrHi 0x%08x\n", ConfigurableExpander->tdDevice->SASAddressID.sasAddressHi));
            TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ConfigurableExpander addrLo 0x%08x\n", ConfigurableExpander->tdDevice->SASAddressID.sasAddressLo));

            UpStreamExpander->tdCurrentDownStreamExpander = oneExpander;
            ConfigurableExpander->currentDownStreamPhyIndex =
                    tdsaFindCurrentDownStreamPhyIndex(tiRoot, ConfigurableExpander);
            ConfigurableExpander->tdReturnginExpander = oneExpander->tdReturnginExpander;
            DownStreamExpander->currentUpStreamPhyIndex = 0;
            TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ConfigurableExpander->currentDownStreamPhyIndex %d\n", ConfigurableExpander->currentDownStreamPhyIndex));

            TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ConfigurableExpander->downStreamPhys[] %d\n", ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex]));
            tdsaSASRoutingEntryAdd(tiRoot,
                                   ConfigurableExpander,
                                   ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                                   oneExpander->configSASAddressHi,
                                   oneExpander->configSASAddressLo
                                   );
          }
          else
          {
            /* going back to where it was */
            /* ConfigRoutingInfo is done for a target */
            TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: $$$$$$ my change $$$$$ \n"));
            ReturningExpander = oneExpander->tdReturnginExpander;
            DownStreamExpander->currentUpStreamPhyIndex = 0;
            /* debugging */
            if (ReturningExpander != agNULL)
            {
              TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ReturningExpander addrHi 0x%08x\n", ReturningExpander->tdDevice->SASAddressID.sasAddressHi));
              TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ReturningExpander addrLo 0x%08x\n", ReturningExpander->tdDevice->SASAddressID.sasAddressLo));

              ReturningExpanderDeviceData = ReturningExpander->tdDevice;

              /* No longer in DISCOVERY_CONFIG_ROUTING */
              onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;

              /* If not the last phy */
              if ( ReturningExpander->discoveringPhyId < ReturningExpanderDeviceData->numOfPhys )
              {
                TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: More Phys to discover\n"));
                /* continue discovery for the next phy */
                /* needs to send only one Discovery not multiple times */
                if (ReturningExpander->discoverSMPAllowed == agTRUE)
                {
                  tdsaDiscoverSend(tiRoot, ReturningExpanderDeviceData);
                }
                ReturningExpander->discoverSMPAllowed = agFALSE;
              }
              /* If the last phy */
              else
              {
                TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: No More Phys\n"));
                ReturningExpander->discoverSMPAllowed = agTRUE;

                /* remove the expander from the discovering list */
                tdssSASDiscoveringExpanderRemove(tiRoot, onePortContext, ReturningExpander);
                /* continue downstream discovering */
                tdsaSASDownStreamDiscovering(tiRoot, onePortContext, ReturningExpanderDeviceData);

                //DownStreamExpander
              }
            }
            else
            {
              TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: ReturningExpander is NULL\n"));
            }
          }
        }
        else
        {
          TI_DBG3(("tdsaConfigRoutingInfoRespRcvd: UpStreamExpander is NULL\n"));
        }
      }
    }
  }
  else
  {
    TI_DBG1(("tdsaConfigRoutingInfoRespRcvd: Discovery Error SMP function return result error=%x\n", frameHeader->smpFunctionResult));
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
  }
  return;
}

/*****************************************************************************
*! \brief  tdsaReportPhySataSend
*
*  Purpose:  This function sends Report Phy SATA to a device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   phyId: Phy Identifier.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaReportPhySataSend(
                      tiRoot_t             *tiRoot,
                      tdsaDeviceData_t     *oneDeviceData,
                      bit8                 phyId
                      )
{
  agsaRoot_t            *agRoot;
  tdsaExpander_t        *oneExpander;
  tdsaPortContext_t     *onePortContext;
  smpReqReportPhySata_t smpReportPhySataReq;

  TI_DBG3(("tdsaReportPhySataSend: start\n"));

  agRoot = oneDeviceData->agRoot;
  onePortContext = oneDeviceData->tdPortContext;
  oneExpander = oneDeviceData->tdExpander;

  if (onePortContext == agNULL)
  {
    TI_DBG1(("tdsaReportPhySataSend: Error!!! portcontext is NULL\n"));
  }

  if (oneExpander == agNULL)
  {
    TI_DBG1(("tdsaReportPhySataSend: Error!!! expander is NULL\n"));
    return;
  }
  TI_DBG3(("tdsaReportPhySataSend: device %p did %d\n", oneDeviceData, oneDeviceData->id));
  TI_DBG3(("tdsaReportPhySataSend: phyid %d\n", phyId));

  oneExpander->tdDeviceToProcess = oneDeviceData;

  osti_memset(&smpReportPhySataReq, 0, sizeof(smpReqReportPhySata_t));

  smpReportPhySataReq.phyIdentifier = phyId;


  tdSMPStart(
             tiRoot,
             agRoot,
             oneExpander->tdDevice,
             SMP_REPORT_PHY_SATA,
             (bit8 *)&smpReportPhySataReq,
             sizeof(smpReqReportPhySata_t),
             AGSA_SMP_INIT_REQ,
             agNULL,
             0
             );

  return;
}

/*****************************************************************************
*! \brief  tdsaReportPhySataRcvd
*
*  Purpose:  This function processes Report Phy SATA response.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaReportPhySataRcvd(
                      tiRoot_t              *tiRoot,
                      agsaRoot_t            *agRoot,
                      agsaIORequest_t       *agIORequest,
                      tdsaDeviceData_t      *oneDeviceData,
                      tdssSMPFrameHeader_t  *frameHeader,
                      agsaFrameHandle_t     frameHandle
                      )
{
  smpRespReportPhySata_t      SMPreportPhySataResp;
  smpRespReportPhySata_t      *pSMPReportPhySataResp;
  tdsaExpander_t              *oneExpander = oneDeviceData->tdExpander;
  tdsaPortContext_t           *onePortContext;
  agsaFisRegDeviceToHost_t    *fis;
  tdsaDeviceData_t            *SataDevice;
#ifndef DIRECT_SMP
  tdssSMPRequestBody_t        *tdSMPRequestBody;
#endif

  TI_DBG3(("tdsaReportPhySataRcvd: start\n"));
  TI_DBG3(("tdsaReportPhySataRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaReportPhySataRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
#ifndef DIRECT_SMP
  tdSMPRequestBody = (tdssSMPRequestBody_t *)agIORequest->osData;
#endif
  /* get the current sata device hanlde stored in the expander structure */
  SataDevice = oneExpander->tdDeviceToProcess;
  pSMPReportPhySataResp = &SMPreportPhySataResp;
#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));
#else
  saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 4, pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));
#endif

  //tdhexdump("tdsaReportPhySataRcvd", (bit8 *)pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));

#ifndef DIRECT_SMP
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                );
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                );
#endif

  onePortContext = oneDeviceData->tdPortContext;

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaReportPhySataRcvd: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
  if (SataDevice == agNULL)
  {
    TI_DBG1(("tdsaReportPhySataRcvd: SataDevice is NULL, wrong\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }
  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED ||
       frameHeader->smpFunctionResult == PHY_VACANT
     )
  {
    fis = (agsaFisRegDeviceToHost_t*) &SMPreportPhySataResp.regDevToHostFis;
    if (fis->h.fisType == REG_DEV_TO_HOST_FIS)
    {
      /* save signature */
      TI_DBG3(("tdsaReportPhySataRcvd: saves the signature\n"));
      /* saves signature */
      SataDevice->satDevData.satSignature[0] = fis->d.sectorCount;
      SataDevice->satDevData.satSignature[1] = fis->d.lbaLow;
      SataDevice->satDevData.satSignature[2] = fis->d.lbaMid;
      SataDevice->satDevData.satSignature[3] = fis->d.lbaHigh;
      SataDevice->satDevData.satSignature[4] = fis->d.device;
      SataDevice->satDevData.satSignature[5] = 0;
      SataDevice->satDevData.satSignature[6] = 0;
      SataDevice->satDevData.satSignature[7] = 0;

      TI_DBG3(("tdsaReportPhySataRcvd: SATA Signature = %02x %02x %02x %02x %02x\n",
        SataDevice->satDevData.satSignature[0],
        SataDevice->satDevData.satSignature[1],
        SataDevice->satDevData.satSignature[2],
        SataDevice->satDevData.satSignature[3],
        SataDevice->satDevData.satSignature[4]));
      /*
        no longer, discovery sends sata identify device command
        tdsaSATAIdentifyDeviceCmdSend(tiRoot, SataDevice);
      */
      SataDevice = tdsaFindRightDevice(tiRoot, onePortContext, SataDevice);
      tdsaDiscoveringStpSATADevice(tiRoot, onePortContext, SataDevice);
    }
    else
    {
      TI_DBG3(("tdsaReportPhySataRcvd: getting next stp bride\n"));
      SataDevice = tdsaFindRightDevice(tiRoot, onePortContext, SataDevice);
      tdsaDiscoveringStpSATADevice(tiRoot, onePortContext, SataDevice);
    }
  }
  else
  {
    TI_DBG3(("tdsaReportPhySataRcvd: siReportPhySataRcvd SMP function return result %x\n",
             frameHeader->smpFunctionResult));
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
  }
  return;
}

/*****************************************************************************
*! \brief  tdsaSASExpanderUpStreamPhyAdd
*
*  Purpose:  This function adds upstream expander to a specfic phy.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneExpander: Pointer to the expander data.
*  \param   phyId: Phy Identifier.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASExpanderUpStreamPhyAdd(
                              tiRoot_t          *tiRoot,
                              tdsaExpander_t    *oneExpander,
                              bit8              phyId
                              )
{
  bit32   i;
  bit32   hasSet = agFALSE;

  TI_DBG3(("tdsaSASExpanderUpStreamPhyAdd: start, phyid %d\n", phyId));
  TI_DBG3(("tdsaSASExpanderUpStreamPhyAdd: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaSASExpanderUpStreamPhyAdd: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaSASExpanderUpStreamPhyAdd: phyid %d  numOfUpStreamPhys %d\n", phyId, oneExpander->numOfUpStreamPhys));

  for ( i = 0; i < oneExpander->numOfUpStreamPhys; i ++ )
  {
    if ( oneExpander->upStreamPhys[i] == phyId )
    {
      hasSet = agTRUE;
      break;
    }
  }

  if ( hasSet == agFALSE )
  {
    oneExpander->upStreamPhys[oneExpander->numOfUpStreamPhys ++] = phyId;
  }

  TI_DBG3(("tdsaSASExpanderUpStreamPhyAdd: AFTER phyid %d  numOfUpStreamPhys %d\n", phyId, oneExpander->numOfUpStreamPhys));

  /* for debugging */
  for ( i = 0; i < oneExpander->numOfUpStreamPhys; i ++ )
  {
    TI_DBG3(("tdsaSASExpanderUpStreamPhyAdd: index %d upstream[index] %d\n", i, oneExpander->upStreamPhys[i]));
  }
  return;
}

/*
  just add phys in downstream in configurable expnader
*/
/*****************************************************************************
*! \brief  tdsaSASExpanderDownStreamPhyAdd
*
*  Purpose:  This function adds downstream expander to a specfic phy.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneExpander: Pointer to the expander data.
*  \param   phyId: Phy Identifier.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSASExpanderDownStreamPhyAdd(
                              tiRoot_t          *tiRoot,
                              tdsaExpander_t    *oneExpander,
                              bit8              phyId
                              )
{
  bit32   i;
  bit32   hasSet = agFALSE;

  TI_DBG3(("tdsaSASExpanderDownStreamPhyAdd: start, phyid %d\n", phyId));
  TI_DBG3(("tdsaSASExpanderDownStreamPhyAdd: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaSASExpanderDownStreamPhyAdd: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaSASExpanderDownStreamPhyAdd: phyid %d  numOfDownStreamPhys %d\n", phyId, oneExpander->numOfDownStreamPhys));

  for ( i = 0; i < oneExpander->numOfDownStreamPhys; i ++ )
  {
    if ( oneExpander->downStreamPhys[i] == phyId )
    {
      hasSet = agTRUE;
      break;
    }
  }

  if ( hasSet == agFALSE )
  {
    oneExpander->downStreamPhys[oneExpander->numOfDownStreamPhys ++] = phyId;
  }

  TI_DBG3(("tdsaSASExpanderDownStreamPhyAdd: AFTER phyid %d  numOfDownStreamPhys %d\n", phyId, oneExpander->numOfDownStreamPhys));

  /* for debugging */
  for ( i = 0; i < oneExpander->numOfDownStreamPhys; i ++ )
  {
     TI_DBG3(("tdsaSASExpanderDownStreamPhyAdd: index %d downstream[index] %d\n", i, oneExpander->downStreamPhys[i]));
  }
  return;
}

/* oneExpander is the configurable expander of interest
   phyId is the first phyID in upStreamPhys[0] of downExpander
*/
/*****************************************************************************
*! \brief  tdsaFindCurrentDownStreamPhyIndex
*
*  Purpose:  This function finds CurrentDownStreamPhyIndex from a configurable
*            expander.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   oneExpander: Pointer to the configuralbe expander data.
*
*  \return:
*           CurrentDownStreamPhyIndex
*
*
*****************************************************************************/
osGLOBAL bit16
tdsaFindCurrentDownStreamPhyIndex(
                              tiRoot_t          *tiRoot,
                              tdsaExpander_t    *oneExpander
                              )
{
  tdsaExpander_t    *DownStreamExpander;
  bit16              index = 0;
  bit16              i;
  bit8               phyId = 0;

  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: start\n"));

  if (oneExpander == agNULL)
  {
    TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: wrong!!! oneExpander is NULL\n"));
    return 0;
  }

  DownStreamExpander = oneExpander->tdCurrentDownStreamExpander;

  if (DownStreamExpander == agNULL)
  {
    TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: wrong!!! DownStreamExpander is NULL\n"));
    return 0;
  }

  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: downstream exp addrHi 0x%08x\n", DownStreamExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: downstream exp addrLo 0x%08x\n", DownStreamExpander->tdDevice->SASAddressID.sasAddressLo));
  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: numOfDownStreamPhys %d\n", oneExpander->numOfDownStreamPhys));

  phyId = DownStreamExpander->upStreamPhys[0];

  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: phyId %d\n", phyId));

  for (i=0; i<oneExpander->numOfDownStreamPhys;i++)
  {
    if (oneExpander->downStreamPhys[i] == phyId)
    {
      index = i;
      break;
    }
  }
  TI_DBG3(("tdsaFindCurrentDownStreamPhyIndex: index %d\n", index));
  return index;
}
/*****************************************************************************
*! \brief  tdsaPortSASDeviceFind
*
*  Purpose:  Given SAS address, this function finds a device with that SAS address
*            in the device list.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   sasAddrLo: Lower 4 byte of SAS address.
*  \param   sasAddrHi: Upper 4 byte of SAS address.
*
*  \return:
*           agNULL  When no device found
*           Pointer to device   When device is found
*
*   \note:
*
*****************************************************************************/
osGLOBAL tdsaDeviceData_t *
tdsaPortSASDeviceFind(
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

  TI_DBG3(("tdsaPortSASDeviceFind: start\n"));

  TD_ASSERT((agNULL != tiRoot), "");
  TD_ASSERT((agNULL != onePortContext), "");

  tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
  {
    TI_DBG3(("tdsaPortSASDeviceFind: Full discovery\n"));
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if ((oneDeviceData->SASAddressID.sasAddressHi == sasAddrHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == sasAddrLo) &&
          (oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext)
        )
      {
        TI_DBG3(("tdsaPortSASDeviceFind: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        TI_DBG3(("tdsaPortSASDeviceFind: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG3(("tdsaPortSASDeviceFind: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        RetDeviceData = oneDeviceData;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }
  else
  {
    /* incremental discovery */
    TI_DBG3(("tdsaPortSASDeviceFind: Incremental discovery\n"));
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if ((oneDeviceData->SASAddressID.sasAddressHi == sasAddrHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == sasAddrLo) &&
          (oneDeviceData->valid2 == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext)
          )
      {
        TI_DBG3(("tdsaPortSASDeviceFind: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        TI_DBG3(("tdsaPortSASDeviceFind: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG3(("tdsaPortSASDeviceFind: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

        RetDeviceData = oneDeviceData;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }

  tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);

  return RetDeviceData;
}

/* include both sas and stp-sata targets*/
/*****************************************************************************
*! \brief  tdsaPortSASDeviceAdd
*
*  Purpose:  This function adds the SAS device to the device list.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   sasIdentify: SAS identify address frame.
*  \param   sasInitiator: SAS initiator.
*  \param   connectionRate: Connection Rate.
*  \param   itNexusTimeout: IT NEXUS timeout value.
*  \param   firstBurstSize: First Burst Size.
*  \param   deviceType: Device Type.
*
*  \return:
*           Pointer to device data.
*
*   \note:
*
*****************************************************************************/
GLOBAL tdsaDeviceData_t *
tdsaPortSASDeviceAdd(
                     tiRoot_t            *tiRoot,
                     tdsaPortContext_t   *onePortContext,
                     agsaSASIdentify_t   sasIdentify,
                     bit32               sasInitiator,
                     bit8                connectionRate,
                     bit32               itNexusTimeout,
                     bit32               firstBurstSize,
                     bit32               deviceType,
                     tdsaDeviceData_t    *oneExpDeviceData,
                     bit8                phyID
                     )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  bit8              dev_s_rate = 0;
  bit8              sasorsata = 1;
//  bit8              devicetype;
  tdsaSASSubID_t    agSASSubID;
  tdsaDeviceData_t  *oneAttachedExpDeviceData = agNULL;

  TI_DBG3(("tdsaPortSASDeviceAdd: start\n"));
  TI_DBG3(("tdsaPortSASDeviceAdd: connectionRate %d\n", connectionRate));

  agSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
  agSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
  agSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
  agSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;

  /* old device and already registered to LL; added by link-up event */
  if ( agFALSE == tdssNewSASorNot(
                                   onePortContext->agRoot,
                                   onePortContext,
                                   &agSASSubID
                                   )
       )
  {
    /* old device and already registered to LL; added by link-up event */
    TI_DBG3(("tdsaPortSASDeviceAdd: OLD qqqq initiator_ssp_stp_smp %d target_ssp_stp_smp %d\n", agSASSubID.initiator_ssp_stp_smp, agSASSubID.target_ssp_stp_smp));
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
      TI_DBG1(("tdsaPortSASDeviceAdd: no more device!!! oneDeviceData is null\n"));
    }

    /* If a device is allocated */
    if ( oneDeviceData != agNULL )
    {

      TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify)));
      TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify)));

      oneDeviceData->sasIdentify = sasIdentify;

      TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)));
      TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)));

      /* parse sasIDframe to fill in agDeviceInfo */
      DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
      DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)itNexusTimeout);
      DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, (bit16)firstBurstSize);
      DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 1);

      oneDeviceData->SASSpecDeviceType = (bit8)(SA_IDFRM_GET_DEVICETTYPE(&sasIdentify));

      /* adjusting connectionRate */
      oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
      if (oneAttachedExpDeviceData != agNULL)
      {
        connectionRate = (bit8)(MIN(connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
        TI_DBG3(("tdsaPortSASDeviceAdd: 1st connectionRate 0x%x  DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo) 0x%x\n",
                 connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
      }
      else
      {
       TI_DBG3(("tdsaPortSASDeviceAdd: 1st oneAttachedExpDeviceData is NULL\n"));
      }

      /* Device Type, SAS or SATA, connection rate; bit7 --- bit0 */
      sasorsata = (bit8)deviceType;
      /* sTSDK spec device typ */
      dev_s_rate = (bit8)(dev_s_rate | (sasorsata << 4));
      dev_s_rate = (bit8)(dev_s_rate | connectionRate);
      DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);


      DEVINFO_PUT_SAS_ADDRESSLO(
                                &oneDeviceData->agDeviceInfo,
                                SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)
                                );
      DEVINFO_PUT_SAS_ADDRESSHI(
                                &oneDeviceData->agDeviceInfo,
                                SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)
                                );
      oneDeviceData->agContext.osData = oneDeviceData;
      oneDeviceData->agContext.sdkData = agNULL;

    }
    return oneDeviceData;
  } /* old device */

  /* new device */

  TI_DBG3(("tdsaPortSASDeviceAdd: NEW qqqq initiator_ssp_stp_smp %d target_ssp_stp_smp %d\n", agSASSubID.initiator_ssp_stp_smp, agSASSubID.target_ssp_stp_smp));

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
    TI_DBG1(("tdsaPortSASDeviceAdd: no more device!!! oneDeviceData is null\n"));
  }

   /* If a device is allocated */
  if ( oneDeviceData != agNULL )
  {

    TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify)));
    TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify)));

    oneDeviceData->sasIdentify = sasIdentify;

    TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)));
    TI_DBG3(("tdsaPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)));


    /* parse sasIDframe to fill in agDeviceInfo */
    DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
    DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)itNexusTimeout);
    DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, (bit16)firstBurstSize);
    DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 1);

    oneDeviceData->SASSpecDeviceType = (bit8)(SA_IDFRM_GET_DEVICETTYPE(&sasIdentify));

    /* adjusting connectionRate */
    oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
    if (oneAttachedExpDeviceData != agNULL)
    {
      connectionRate = (bit8)(MIN(connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
      TI_DBG3(("tdsaPortSASDeviceAdd: 2nd connectionRate 0x%x  DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo) 0x%x\n",
                connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
    }
    else
    {
     TI_DBG3(("tdsaPortSASDeviceAdd: 2nd oneAttachedExpDeviceData is NULL\n"));
    }

    /* Device Type, SAS or SATA, connection rate; bit7 --- bit0 */
    sasorsata = (bit8)deviceType;
    dev_s_rate = (bit8)(dev_s_rate | (sasorsata << 4));
    dev_s_rate = (bit8)(dev_s_rate | connectionRate);
    DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);


    DEVINFO_PUT_SAS_ADDRESSLO(
                              &oneDeviceData->agDeviceInfo,
                              SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)
                              );
    DEVINFO_PUT_SAS_ADDRESSHI(
                              &oneDeviceData->agDeviceInfo,
                              SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)
                              );
    oneDeviceData->agContext.osData = oneDeviceData;
    oneDeviceData->agContext.sdkData = agNULL;

    TI_DBG3(("tdsaPortSASDeviceAdd: did %d\n", oneDeviceData->id));

    /* don't add and register initiator for T2D */
    if ( (((sasIdentify.initiator_ssp_stp_smp & DEVICE_SSP_BIT) == DEVICE_SSP_BIT) &&
         ((sasIdentify.target_ssp_stp_smp & DEVICE_SSP_BIT) != DEVICE_SSP_BIT))
        ||
         (((sasIdentify.initiator_ssp_stp_smp & DEVICE_STP_BIT) == DEVICE_STP_BIT) &&
         ((sasIdentify.target_ssp_stp_smp & DEVICE_SSP_BIT) != DEVICE_SSP_BIT))
       )
    {
      TI_DBG1(("tdsaPortSASDeviceAdd: initiator. no add and registration\n"));
      TI_DBG1(("tdsaPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)));
      TI_DBG1(("tdsaPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)));

    }
    else
    {
      if (oneDeviceData->registered == agFALSE)
      {
        TI_DBG2(("tdsaPortSASDeviceAdd: did %d\n", oneDeviceData->id));
        saRegisterNewDevice( /* tdsaPortSASDeviceAdd  */
                            onePortContext->agRoot,
                            &oneDeviceData->agContext,
                            tdsaRotateQnumber(tiRoot, oneDeviceData),
                            &oneDeviceData->agDeviceInfo,
                            onePortContext->agPortContext,
                            0
                            );
      }
    }
  }

  return oneDeviceData;
}

/*****************************************************************************
*! \brief  tdsaDiscoveryResetProcessed
*
*  Purpose:  This function called to reset "processed flag" of device belong to
*            a specified port.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/

osGLOBAL void
tdsaDiscoveryResetProcessed(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext
                    )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  TI_DBG6(("tdsaDiscoveryResetProcessed: start\n"));

  /* reinitialize the device data belonging to this portcontext */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG6(("tdsaDiscoveryResetProcessed: loop did %d\n", oneDeviceData->id));
    if (oneDeviceData->tdPortContext == onePortContext)
    {
      TI_DBG6(("tdsaDiscoveryResetProcessed: resetting procssed flag\n"));
      oneDeviceData->processed = agFALSE;
    }
    DeviceListList = DeviceListList->flink;
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaSATADiscoverDone
*
*  Purpose:  This function called to finish up SATA discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   flag: status of discovery (success or failure).
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSATADiscoverDone(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext,
                    bit32                flag
                    )
{
  tdsaRoot_t           *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t        *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TI_DBG3(("tdsaSATADiscoverDone: start\n"));
  tdsaDiscoveryResetProcessed(tiRoot, onePortContext);

  if (onePortContext->discovery.SeenBC == agTRUE)
  {
    TI_DBG3(("tdsaSATADiscoverDone: broadcast change; discover again\n"));
    tdssInternalRemovals(onePortContext->agRoot,
                         onePortContext
                         );

    /* processed broadcast change */
    onePortContext->discovery.SeenBC = agFALSE;
    if (tdsaAllShared->ResetInDiscovery != 0 &&
        onePortContext->discovery.ResetTriggerred == agTRUE)
    {
      TI_DBG1(("tdsaSATADiscoverDone: tdsaBCTimer\n"));
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
  }
  else
  {
    onePortContext->DiscoveryState = ITD_DSTATE_COMPLETED;

    if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
    {
      if (flag == tiSuccess)
      {
#ifdef AGTIAPI_CTL
        tdsaContext_t *tdsaAllShared =
                        &((tdsaRoot_t*)tiRoot->tdData)->tdsaAllShared;

        if (tdsaAllShared->SASConnectTimeLimit)
          tdsaCTLSet(tiRoot, onePortContext, tiIntrEventTypeDiscovery,
                     tiDiscOK);
        else
#endif
          ostiInitiatorEvent(
                             tiRoot,
                             onePortContext->tiPortalContext,
                             agNULL,
                             tiIntrEventTypeDiscovery,
                             tiDiscOK,
                             agNULL
                             );
      }
      else
      {
        TI_DBG1(("tdsaSATADiscoverDone: Error; clean up\n"));
        tdssDiscoveryErrorRemovals(onePortContext->agRoot,
                                   onePortContext
                                   );

        ostiInitiatorEvent(
                           tiRoot,
                           onePortContext->tiPortalContext,
                           agNULL,
                           tiIntrEventTypeDiscovery,
                           tiDiscFailed,
                           agNULL
                           );
      }
    }
    else
    {
      if (flag == tiSuccess)
      {
        tdssReportChanges(onePortContext->agRoot,
                          onePortContext
                          );
      }
      else
      {
        tdssReportRemovals(onePortContext->agRoot,
                           onePortContext,
                           agFALSE
                           );
      }
    }
  }
#ifdef TBD
  /* ACKing BC */
  tdsaAckBC(tiRoot, onePortContext);
#endif
  return;
}

osGLOBAL void
tdsaAckBC(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext
                    )
{
#ifdef TBD /* not yet */
  agsaEventSource_t        eventSource[TD_MAX_NUM_PHYS];
  bit32                    HwAckSatus = AGSA_RC_SUCCESS;
  int                      i;
  TI_DBG3(("tdsaAckBC: start\n"));

  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    if (onePortContext->BCPhyID[i] == agTRUE)
    {
      /* saHwEventAck() */
      eventSource[i].agPortContext = onePortContext->agPortContext;
      eventSource[i].event = OSSA_HW_EVENT_BROADCAST_CHANGE;
      /* phy ID */
      eventSource[i].param = i;
      HwAckSatus = saHwEventAck(
                                onePortContext->agRoot,
                                agNULL, /* agContext */
                                0,
                                &eventSource[i], /* agsaEventSource_t */
                                0,
                                0
                                );
      TI_DBG3(("tdsaAckBC: calling saHwEventAck\n"));

      if ( HwAckSatus != AGSA_RC_SUCCESS)
      {
        TI_DBG1(("tdsaAckBC: failing in saHwEventAck; status %d\n", HwAckSatus));
        return;
      }
    }
    onePortContext->BCPhyID[i] = agFALSE;
  }
#endif
}

#ifdef SATA_ENABLE

/*****************************************************************************
*! \brief  tdsaSATAFullDiscover
*
*  Purpose:  This function is called to trigger full SATA topology discovery
*            within a portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           tiSuccess    Discovery initiated.
*           tiError      Discovery could not be initiated at this time.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaSATAFullDiscover(
                     tiRoot_t          *tiRoot,
                     tdsaPortContext_t *onePortContext
                     )
{
  bit32                 ret = tiSuccess;
  tdsaDeviceData_t      *oneDeviceData = agNULL;
  bit32                 deviceType;
  bit8                  phyRate = SAS_CONNECTION_RATE_3_0G;
  bit32                 i;
  tdsaRoot_t            *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t         *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
//  tdsaDeviceData_t      *tdsaDeviceData  = (tdsaDeviceData_t *)tdsaAllShared->DeviceMem;
  tdsaDeviceData_t      *tdsaDeviceData;
  tdList_t              *DeviceListList;

  TI_DBG3(("tdsaSATAFullDiscover: start\n"));
  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSATAFullDiscover: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return tiError;
  }
  phyRate = onePortContext->LinkRate;
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  tdsaDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
  /*  If port is SATA mode */
  /*
    Native SATA mode is decided in ossaHWCB() SAS_LINK_UP or SATA_LINK_UP
   */
  if (onePortContext->nativeSATAMode == agTRUE)
  {
    /* Decode device type */
    deviceType = tdssSATADeviceTypeDecode(onePortContext->remoteSignature);
    /* Create a device descriptor for the SATA device attached to the port */
    if ( deviceType == SATA_PM_DEVICE)
    {
      TI_DBG3(("tdsaSATAFullDiscover: Found a PM device\n"));
      oneDeviceData = tdsaPortSATADeviceAdd(
                                            tiRoot,
                                            onePortContext,
                                            agNULL,
                                            onePortContext->remoteSignature,
                                            agTRUE,
                                            0xF,
                                            phyRate,
                                            agNULL,
                                            0xFF
                                            );
    }
    else
    {
      /* already added in ossahwcb() in SATA link up */
      TI_DBG3(("tdsaSATAFullDiscover: Found a DIRECT SATA device\n"));
    }

    /* Process for different device type */
    switch ( deviceType )
    {
      /* if it's PM */
      case SATA_PM_DEVICE:
      {

        TI_DBG3(("tdsaSATAFullDiscover: Process a PM device\n"));
        /* For each port of the PM */
        for ( i = 0; i < SATA_MAX_PM_PORTS; i ++ )
        {
          /* Read the signature */
          /* Decode the device type */
          /* Create device descriptor */
          /* Callback with the discovered devices */
        }
        break;
      }
      /* if it's ATA device */
      case SATA_ATA_DEVICE:
      case SATA_ATAPI_DEVICE:
      {
        TI_DBG3(("tdsaSATAFullDiscover: Process an ATA device. Sending Identify Device cmd\n"));

        /* to-check: for this direct attached one, already added and do nothing */
        /* no longer, discovery sends sata identify device command */
        //tdsaSATAIdentifyDeviceCmdSend(tiRoot, oneDeviceData);
        tdsaSATADiscoverDone(tiRoot, onePortContext, tiSuccess);
        break;
      }
      /* Other devices */
      default:
      {
        /* callback */
        TI_DBG3(("siSATAFullDiscover: Process OTHER SATA device. Just report the device\n"));
        break;
      }
    }
  }
  /* If port is SAS mode */
  else
  {
    TI_DBG3(("tdsaSATAFullDiscover: Discovering attached STP devices  starts....\n"));
    oneDeviceData = tdsaFindRightDevice(tiRoot, onePortContext, tdsaDeviceData);
    tdsaDiscoveringStpSATADevice(tiRoot, onePortContext, oneDeviceData);
  }
  return ret;
}

/* adding only direct attached SATA such as PM
  Other directly attached SATA device such as disk is reported by ossahwcb() in link up
  used in sata native mode
  */
/*****************************************************************************
*! \brief  tdsaPortSATADeviceAdd
*
*  Purpose:  This function adds the SATA device to the device list.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneSTPBridge: STP bridge.
*  \param   Signature: SATA signature.
*  \param   pm: Port Multiplier.
*  \param   pmField: Port Multiplier field.
*  \param   connectionRate: Connection Rate.
*
*  \return:
*           Pointer to device data.
*
*   \note:
*
*****************************************************************************/
GLOBAL tdsaDeviceData_t *
tdsaPortSATADeviceAdd(
                      tiRoot_t                *tiRoot,
                      tdsaPortContext_t       *onePortContext,
                      tdsaDeviceData_t        *oneSTPBridge,
                      bit8                    *Signature,
                      bit8                    pm,
                      bit8                    pmField,
                      bit8                    connectionRate,
                      tdsaDeviceData_t        *oneExpDeviceData,
                      bit8                    phyID
                      )
{
  tdsaDeviceData_t      *oneDeviceData = agNULL;
  agsaRoot_t            *agRoot = onePortContext->agRoot;
  bit8                  dev_s_rate = 0;
  bit8                  sasorsata = SATA_DEVICE_TYPE;
//  bit8                  devicetype = 0;
  bit8                  flag = 0;
  bit8                  TLR = 0;
  tdsaDeviceData_t      *oneAttachedExpDeviceData = agNULL;

  TI_DBG3(("tdsaPortSATADeviceAdd: start\n"));

  /* sanity check */
  TD_ASSERT((agNULL != tiRoot), "");
  TD_ASSERT((agNULL != agRoot), "");
  TD_ASSERT((agNULL != onePortContext), "");
  TD_ASSERT((agNULL != Signature), "");

  oneDeviceData = tdssNewAddSATAToSharedcontext(
                                                tiRoot,
                                                agRoot,
                                                onePortContext,
                                                agNULL,
                                                Signature,
                                                pm,
                                                pmField,
                                                connectionRate,
                                                oneExpDeviceData,
                                                phyID
                                                );
  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("tdsaPortSATADeviceAdd: no more device!!! oneDeviceData is null\n"));
    return agNULL;
  }

  flag = (bit8)((phyID << 4) | TLR);
  DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
  DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, 0xFFF);
  DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, 0);
  DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, flag);

  /* adjusting connectionRate */
  oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
  if (oneAttachedExpDeviceData != agNULL)
  {
    connectionRate = (bit8)(MIN(connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
    TI_DBG3(("tdsaPortSATADeviceAdd: 1st connectionRate 0x%x  DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo) 0x%x\n",
              connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
  }
  else
  {
    TI_DBG3(("tdsaPortSATADeviceAdd: 1st oneAttachedExpDeviceData is NULL\n"));
  }

   /* Device Type, SAS or SATA, connection rate; bit7 --- bit0*/
//   dev_s_rate = dev_s_rate | (devicetype << 6);
   dev_s_rate = (bit8)(dev_s_rate | (sasorsata << 4));
   dev_s_rate = (bit8)(dev_s_rate | connectionRate);
   DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);

   osti_memset(&oneDeviceData->agDeviceInfo.sasAddressHi, 0, 4);
   osti_memset(&oneDeviceData->agDeviceInfo.sasAddressLo, 0, 4);

   oneDeviceData->agContext.osData = oneDeviceData;
   oneDeviceData->agContext.sdkData = agNULL;

   TI_DBG1(("tdsaPortSATADeviceAdd: did %d\n", oneDeviceData->id));
   if (oneDeviceData->registered == agFALSE)
   {
     TI_DBG2(("tdsaPortSATADeviceAdd: did %d\n", oneDeviceData->id));
     saRegisterNewDevice( /* tdsaPortSATADeviceAdd */
                         onePortContext->agRoot,
                         &oneDeviceData->agContext,
                         tdsaRotateQnumber(tiRoot, oneDeviceData),
                         &oneDeviceData->agDeviceInfo,
                         onePortContext->agPortContext,
                         0
                         );
   }

   return oneDeviceData;
}
#endif

/*****************************************************************************
*! \brief  tdsaFindRightDevice
*
*  Purpose:  This function returns device-to-be processed.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   tdsaDeviceData: Pointer to the starting device data.
*
*  \return:
*           Pointer to device data.
*
*   \note:
*
*****************************************************************************/
osGLOBAL tdsaDeviceData_t  *
tdsaFindRightDevice(
                   tiRoot_t               *tiRoot,
                   tdsaPortContext_t      *onePortContext,
                   tdsaDeviceData_t       *tdsaDeviceData
                   )
{
  tdList_t          *DeviceListList;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  bit32             found = agFALSE;

  TI_DBG3(("tdsaFindHeadDevice: start\n"));

  DeviceListList = tdsaDeviceData->MainLink.flink;

  while (DeviceListList != &(tdsaDeviceData->MainLink))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    TI_DBG3(("tdsaFindRightDevice: did %d STP %d SATA %d \n", onePortContext->id, DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
    DeviceListList = DeviceListList->flink;
  }

  DeviceListList = tdsaDeviceData->MainLink.flink;

  while (DeviceListList != &(tdsaDeviceData->MainLink))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if ((oneDeviceData->registered == agTRUE) &&
        (oneDeviceData->tdPortContext == onePortContext) &&
        (oneDeviceData->processed == agFALSE) &&
        (SA_IDFRM_IS_STP_TARGET(&oneDeviceData->sasIdentify) ||
         SA_IDFRM_IS_SATA_DEVICE(&oneDeviceData->sasIdentify))
        )
    {
      TI_DBG3(("tdsaFindRightDevice: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      oneDeviceData->processed = agTRUE;
      found = agTRUE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  if (found == agTRUE)
  {
    return oneDeviceData;
  }
  else
  {
    return agNULL;
  }
}



// tdsaDeviceData is head of list
/*****************************************************************************
*! \brief  tdsaDiscoveringStpSATADevice
*
*  Purpose:  For each device in the device list, this function peforms
*            SATA discovery.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the heade of device list.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoveringStpSATADevice(
                             tiRoot_t               *tiRoot,
                             tdsaPortContext_t      *onePortContext,
                             tdsaDeviceData_t       *oneDeviceData
                             )
{
  bit32                 status;
  tdsaRoot_t            *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t         *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
//  tdsaDeviceData_t      *tdsaDeviceData  = (tdsaDeviceData_t *)tdsaAllShared->DeviceMem;
  tdsaDeviceData_t      *tdsaDeviceData;
  tdList_t              *DeviceListList;

  TI_DBG3(("tdsaDiscoveringStpSATADevice: start\n"));

  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  tdsaDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);

  if (oneDeviceData)
  {
    TI_DBG3(("tdsaDiscoveringStpSATADevice: Found STP-SATA Device=%p\n", oneDeviceData));
    if ((SA_IDFRM_IS_SATA_DEVICE(&oneDeviceData->sasIdentify) || SA_IDFRM_IS_STP_TARGET(&oneDeviceData->sasIdentify))
         &&
        ((onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START &&
          oneDeviceData->valid == agTRUE) ||
        (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_INCREMENTAL_START &&
         oneDeviceData->valid2 == agTRUE)) &&
        (oneDeviceData->tdPortContext == onePortContext)
       )
    {
      /* if found an STP bridges */
      /* in order to get sata signature and etc */
      TI_DBG3(("tdsaDiscoveringStpSATADevice: sending report phy sata\n"));
      tdsaReportPhySataSend(tiRoot, oneDeviceData, oneDeviceData->sasIdentify.phyIdentifier);
      //send ID in every discovery? No
      if (oneDeviceData->satDevData.IDDeviceValid == agFALSE)
      {
        TI_DBG3(("tdsaDiscoveringStpSATADevice: sending identify device data\n"));
        /* all internal */
        status = tdsaDiscoveryStartIDDev(tiRoot,
                                         agNULL,
                                         &(oneDeviceData->tiDeviceHandle),
                                         agNULL,
                                         oneDeviceData);

        if (status != tiSuccess)
        {
          /* identify device data is not valid */
          TI_DBG1(("tdsaDiscoveringStpSATADevice: fail or busy %d\n", status));
          oneDeviceData->satDevData.IDDeviceValid = agFALSE;
        }
      }
    }
    else
    {
      TI_DBG2(("tdsaDiscoveringStpSATADevice: moving to the next\n"));
      oneDeviceData = tdsaFindRightDevice(tiRoot, onePortContext, tdsaDeviceData);
      tdsaDiscoveringStpSATADevice(tiRoot, onePortContext, oneDeviceData);
    }
  }
  else
  {
    /* otherwise, there is no more SATA device found */
    TI_DBG3(("tdsaDiscoveringStpSATADevice: No More Device; SATA discovery finished\n"));

    tdsaSATADiscoverDone(tiRoot, onePortContext, tiSuccess);
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaSASIncrementalDiscover
*
*  Purpose:  This function is called to trigger incremental SAS topology discovery
*            within a portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           tiSuccess    Discovery initiated.
*           tiError      Discovery could not be initiated at this time.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaSASIncrementalDiscover(
                    tiRoot_t          *tiRoot,
                    tdsaPortContext_t *onePortContext
                    )
{
  tdsaDeviceData_t     *oneDeviceData  = agNULL;
  int                  i,j;
  bit8                 portMaxRate;

  TI_DBG3(("tdsaSASIncrementalDiscover: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSASIncrementalDiscover: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return tiError;
  }

  onePortContext->DiscoveryState = ITD_DSTATE_STARTED;

  /* nativeSATAMode is set in ossaHwCB() in link up */
  if (onePortContext->nativeSATAMode == agFALSE) /* default: SAS and SAS/SATA mode */
  {
    if (SA_IDFRM_GET_DEVICETTYPE(&onePortContext->sasIDframe) == SAS_END_DEVICE &&
        SA_IDFRM_IS_SSP_TARGET(&onePortContext->sasIDframe) )
    {
      for(i=0;i<TD_MAX_NUM_PHYS;i++)
      {
        if (onePortContext->PhyIDList[i] == agTRUE)
        {
       
          for (j=0;j<TD_MAX_NUM_NOTIFY_SPINUP;j++)
          {
            saLocalPhyControl(onePortContext->agRoot, agNULL, tdsaRotateQnumber(tiRoot, agNULL), i, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
          }
          break;
        }
      }
    }
    /*
      add the device
      1. add device in TD layer
      2. call saRegisterNewDevice
      3. update agDevHandle in ossaDeviceRegistrationCB()
    */
    portMaxRate = onePortContext->LinkRate;
    oneDeviceData = tdsaPortSASDeviceAdd(
                                         tiRoot,
                                         onePortContext,
                                         onePortContext->sasIDframe,
                                         agFALSE,
                                         portMaxRate,
                                         IT_NEXUS_TIMEOUT,
                                         0,
                                         SAS_DEVICE_TYPE,
                                         agNULL,
                                         0xFF
                                         );
    if (oneDeviceData)
    {
      if (oneDeviceData->registered == agFALSE)
      {
        /*
          set the timer and wait till the device(directly attached. eg Expander) to be registered.
          Then, in tdsaDeviceRegistrationTimerCB(), tdsaSASUpStreamDiscoverStart() is called
        */
        tdsaDeviceRegistrationTimer(tiRoot, onePortContext, oneDeviceData);
      }
      else
      {
        tdsaSASUpStreamDiscoverStart(tiRoot, onePortContext, oneDeviceData);
      }
    }
  }
  else /* SATAOnlyMode*/
  {
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiSuccess);
  }
  return tiSuccess;
}

#ifdef SATA_ENABLE
/* For the sake of completness; this is the same as  tdsaSATAFullDiscover*/
/*****************************************************************************
*! \brief  tdsaSATAIncrementalDiscover
*
*  Purpose:  This function is called to trigger incremental SATA topology discovery
*            within a portcontext.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*
*  \return:
*           tiSuccess    Discovery initiated.
*           tiError      Discovery could not be initiated at this time.
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdsaSATAIncrementalDiscover(
                            tiRoot_t          *tiRoot,
                            tdsaPortContext_t *onePortContext
                           )
{
  bit32                 ret = tiSuccess;
  tdsaDeviceData_t      *oneDeviceData = agNULL;
  bit32                 deviceType;
  bit8                  phyRate = SAS_CONNECTION_RATE_3_0G;
  bit32                 i;
  tdsaRoot_t            *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t         *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
//  tdsaDeviceData_t      *tdsaDeviceData  = (tdsaDeviceData_t *)tdsaAllShared->DeviceMem;
  tdsaDeviceData_t      *tdsaDeviceData;
  tdList_t              *DeviceListList;

  TI_DBG3(("tdsaSATAIncrementalDiscover: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdsaSATAIncrementalDiscover: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return tiError;
  }

  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  tdsaDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);

  /*  If port is SATA mode */
  /*
    Native SATA mode is decided in ossaHWCB() SAS_LINK_UP or SATA_LINK_UP
   */
  if (onePortContext->nativeSATAMode == agTRUE)
  {
    /* Decode device type */
    deviceType = tdssSATADeviceTypeDecode(onePortContext->remoteSignature);
    /* Create a device descriptor for the SATA device attached to the port */
    if ( deviceType == SATA_PM_DEVICE)
    {
      TI_DBG3(("tdsaSATAIncrementalDiscover: Found a PM device\n"));
      oneDeviceData = tdsaPortSATADeviceAdd(
                                            tiRoot,
                                            onePortContext,
                                            agNULL,
                                            onePortContext->remoteSignature,
                                            agTRUE,
                                            0xF,
                                            phyRate,
                                            agNULL,
                                            0xFF);
    }
    else
    {
      /* already added in ossahwcb() in SATA link up */
      TI_DBG3(("tdsaSATAIncrementalDiscover: Found a DIRECT SATA device\n"));
    }

    /* Process for different device type */
    switch ( deviceType )
    {
      /* if it's PM */
      case SATA_PM_DEVICE:
      {

        TI_DBG3(("tdsaSATAIncrementalDiscover: Process a PM device\n"));
        /* For each port of the PM */
        for ( i = 0; i < SATA_MAX_PM_PORTS; i ++ )
        {
          /* Read the signature */
          /* Decode the device type */
          /* Create device descriptor */
          /* Callback with the discovered devices */
        }
        break;
      }
      /* if it's ATA device */
      case SATA_ATA_DEVICE:
      case SATA_ATAPI_DEVICE:
      {
        TI_DBG3(("tdsaSATAIncrementalDiscover: Process an ATA device. Sending Identify Device cmd\n"));

        /* to-check: for this direct attached one, already added and do nothing */
        /* no longer, discovery sends sata identify device command */
        //tdsaSATAIdentifyDeviceCmdSend(tiRoot, oneDeviceData);

        tdsaSATADiscoverDone(tiRoot, onePortContext, tiSuccess);

        break;
      }
      /* Other devices */
      default:
      {
        /* callback */
        TI_DBG3(("siSATAIncrementalDiscover: Process OTHER SATA device. Just report the device\n"));

        break;
      }
    }
  }
  /* If port is SAS mode */
  else
  {
    TI_DBG3(("tdsaSATAIncrementalDiscover: Discovering attached STP devices  starts....\n"));
    oneDeviceData = tdsaFindRightDevice(tiRoot, onePortContext, tdsaDeviceData);

    tdsaDiscoveringStpSATADevice(tiRoot, onePortContext, oneDeviceData);
  }
  return ret;

}
#endif


/********************  SMP *******************************/

/*****************************************************************************
*! \brief  tdSMPStart
*
*  Purpose:  This function sends SMP request.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   functionCode: SMP function code.
*  \param   pSmpBody: Pointer to SMP payload.
*  \param   smpBodySize: Size of SMP request without SMP header.
*  \param   agRequestType: SPC-specfic request type
*
*  \return:
*           tiSuccess  SMP is sent successfully
*           tiError    SMP is not sent successfully
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit32
tdSMPStart(
           tiRoot_t              *tiRoot,
           agsaRoot_t            *agRoot,
           tdsaDeviceData_t      *oneDeviceData,
           bit32                 functionCode,
           bit8                  *pSmpBody, /* smp payload itself w/o first 4 bytes(header) */
           bit32                 smpBodySize, /* smp payload size w/o first 4 bytes(header) */
           bit32                 agRequestType,
           tiIORequest_t         *CurrentTaskTag,
           bit32                 queueNumber
           )
{
  void                        *osMemHandle;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  bit32                       expectedRspLen = 0;

#ifdef REMOVED
  tdsaRoot_t                  *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
#endif
  tdssSMPRequestBody_t        *tdSMPRequestBody;
  agsaSASRequestBody_t        *agSASRequestBody;
  agsaSMPFrame_t              *agSMPFrame;
  agsaIORequest_t             *agIORequest;
  agsaDevHandle_t             *agDevHandle;
  tdssSMPFrameHeader_t        tdSMPFrameHeader;
  tdsaPortContext_t           *onePortContext = agNULL;
  bit32                       status;

#ifndef DIRECT_SMP
  void                        *IndirectSMPReqosMemHandle;
  bit32                       IndirectSMPReqPhysUpper32;
  bit32                       IndirectSMPReqPhysLower32;
  bit32                       IndirectSMPReqmemAllocStatus;
  bit8                        *IndirectSMPReq;

  void                        *IndirectSMPResposMemHandle;
  bit32                       IndirectSMPRespPhysUpper32;
  bit32                       IndirectSMPRespPhysLower32;
  bit32                       IndirectSMPRespmemAllocStatus;
  bit8                        *IndirectSMPResp;
#endif

  TI_DBG3(("tdSMPStart: start\n"));
  TI_DBG3(("tdSMPStart: oneDeviceData %p\n", oneDeviceData));
  TI_DBG3(("tdSMPStart: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)));
  TI_DBG3(("tdSMPStart: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)));
  TI_DBG3(("tdSMPStart: 2nd sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  TI_DBG3(("tdSMPStart: 2nd sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->tdPortContext;

  if (onePortContext != agNULL)
  {
    TI_DBG3(("tdSMPStart: pid %d\n", onePortContext->id));
    /* increment the number of pending SMP */
    onePortContext->discovery.pendingSMP++;
  }
  else
  {
    TI_DBG1(("tdSMPStart: Wrong!!! onePortContext is NULL\n"));
    return tiError;
  }



  memAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &osMemHandle,
                                   (void **)&tdSMPRequestBody,
                                   &PhysUpper32,
                                   &PhysLower32,
                                   8,
                                   sizeof(tdssSMPRequestBody_t),
                                   agTRUE
                                   );

  if (memAllocStatus != tiSuccess)
  {
    TI_DBG1(("tdSMPStart: ostiAllocMemory failed...\n"));
    return tiError;
  }

  if (tdSMPRequestBody == agNULL)
  {
    TI_DBG1(("tdSMPStart: ostiAllocMemory returned NULL tdSMPRequestBody\n"));
    return tiError;
  }
  /* saves mem handle for freeing later */
  tdSMPRequestBody->osMemHandle = osMemHandle;

  /* saves tdsaDeviceData */
  tdSMPRequestBody->tdDevice = oneDeviceData;

  /* saving port id */
  tdSMPRequestBody->tdPortContext = onePortContext;


  agDevHandle = oneDeviceData->agDevHandle;

  /* save the callback funtion */
  tdSMPRequestBody->SMPCompletionFunc = itdssSMPCompleted; /* in itdcb.c */

  /* for simulate warm target reset */
  tdSMPRequestBody->CurrentTaskTag = CurrentTaskTag;

  /* initializes the number of SMP retries */
  tdSMPRequestBody->retries = 0;

#ifdef TD_INTERNAL_DEBUG  /* debugging */
  TI_DBG4(("tdSMPStart: SMPRequestbody %p\n", tdSMPRequestBody));
  TI_DBG4(("tdSMPStart: callback fn %p\n", tdSMPRequestBody->SMPCompletionFunc));
#endif

  agIORequest = &(tdSMPRequestBody->agIORequest);
  agIORequest->osData = (void *) tdSMPRequestBody;
  agIORequest->sdkData = agNULL; /* SALL takes care of this */


  agSASRequestBody = &(tdSMPRequestBody->agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  TI_DBG3(("tdSMPStart: agIORequest %p\n", agIORequest));
  TI_DBG3(("tdSMPStart: SMPRequestbody %p\n", tdSMPRequestBody));

  /*
    depending on functionCode, set expectedRspLen in smp
  */
  switch (functionCode)
  {
  case SMP_REPORT_GENERAL:
    expectedRspLen = sizeof(smpRespReportGeneral_t) + 4;
    break;
  case SMP_REPORT_MANUFACTURE_INFORMATION:
    expectedRspLen = sizeof(smpRespReportManufactureInfo_t) + 4;
    break;
  case SMP_DISCOVER:
    expectedRspLen = sizeof(smpRespDiscover_t) + 4;
    break;
  case SMP_REPORT_PHY_ERROR_LOG:
    expectedRspLen = 32 - 4;
    break;
  case SMP_REPORT_PHY_SATA:
    expectedRspLen = sizeof(smpRespReportPhySata_t) + 4;
    break;
  case SMP_REPORT_ROUTING_INFORMATION:
    expectedRspLen = sizeof(smpRespReportRouteTable_t) + 4;
    break;
  case SMP_CONFIGURE_ROUTING_INFORMATION:
    expectedRspLen = 4;
    break;
  case SMP_PHY_CONTROL:
    expectedRspLen = 4;
    break;
  case SMP_PHY_TEST_FUNCTION:
    expectedRspLen = 4;
    break;
  case SMP_PMC_SPECIFIC:
    expectedRspLen = 4;
    break;
  default:
    expectedRspLen = 0;
    TI_DBG1(("tdSMPStart: error!!! undefined or unused smp function code 0x%x\n", functionCode));
    return tiError;
  }

  if (tiIS_SPC(agRoot))
  {
#ifdef DIRECT_SMP  /* direct SMP with 48 or less payload */
  if ( (smpBodySize + 4) <= SMP_DIRECT_PAYLOAD_LIMIT) /* 48 */
  {
    TI_DBG3(("tdSMPStart: DIRECT smp payload\n"));
    osti_memset(&tdSMPFrameHeader, 0, sizeof(tdssSMPFrameHeader_t));
    osti_memset(tdSMPRequestBody->smpPayload, 0, SMP_DIRECT_PAYLOAD_LIMIT);

    /* SMP header */
    tdSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
    tdSMPFrameHeader.smpFunction = (bit8)functionCode;
    tdSMPFrameHeader.smpFunctionResult = 0;
    tdSMPFrameHeader.smpReserved = 0;

    osti_memcpy(tdSMPRequestBody->smpPayload, &tdSMPFrameHeader, 4);
//    osti_memcpy((tdSMPRequestBody->smpPayload)+4, pSmpBody, smpBodySize);
    osti_memcpy(&(tdSMPRequestBody->smpPayload[4]), pSmpBody, smpBodySize);   

    /* direct SMP payload eg) REPORT_GENERAL, DISCOVER etc */
    agSMPFrame->outFrameBuf = tdSMPRequestBody->smpPayload;
    agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */
    /* to specify DIRECT SMP response */
    agSMPFrame->inFrameLen = 0;

    /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
    /* force smp repsonse to be direct */
    agSMPFrame->expectedRespLen = 0;
#else
    agSMPFrame->expectedRespLen = expectedRspLen;
#endif
//    tdhexdump("tdSMPStart", (bit8*)agSMPFrame->outFrameBuf, agSMPFrame->outFrameLen);
//    tdhexdump("tdSMPStart new", (bit8*)tdSMPRequestBody->smpPayload, agSMPFrame->outFrameLen);
//    tdhexdump("tdSMPStart - tdSMPRequestBody", (bit8*)tdSMPRequestBody, sizeof(tdssSMPRequestBody_t));
  }
  else
  {
    TI_DBG3(("tdSMPStart: INDIRECT smp payload\n"));
  }

#else

  /* indirect SMP */
  /* allocate Direct SMP request payload */
  IndirectSMPReqmemAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &IndirectSMPReqosMemHandle,
                                   (void **)&IndirectSMPReq,
                                   &IndirectSMPReqPhysUpper32,
                                   &IndirectSMPReqPhysLower32,
                                   8,
                                   smpBodySize + 4,
                                   agFALSE
                                   );

  if (IndirectSMPReqmemAllocStatus != tiSuccess)
  {
    TI_DBG1(("tdSMPStart: ostiAllocMemory failed for indirect SMP request...\n"));
    return tiError;
  }

  if (IndirectSMPReq == agNULL)
  {
    TI_DBG1(("tdSMPStart: ostiAllocMemory returned NULL IndirectSMPReq\n"));
    return tiError;
  }

  /* allocate indirect SMP response payload */
  IndirectSMPRespmemAllocStatus = ostiAllocMemory(
                                   tiRoot,
                                   &IndirectSMPResposMemHandle,
                                   (void **)&IndirectSMPResp,
                                   &IndirectSMPRespPhysUpper32,
                                   &IndirectSMPRespPhysLower32,
                                   8,
                                   expectedRspLen,
                                   agFALSE
                                   );

  if (IndirectSMPRespmemAllocStatus != tiSuccess)
  {
    TI_DBG1(("tdSMPStart: ostiAllocMemory failed for indirect SMP reponse...\n"));
    return tiError;
  }

  if (IndirectSMPResp == agNULL)
  {
    TI_DBG1(("tdSMPStart: ostiAllocMemory returned NULL IndirectSMPResp\n"));
    return tiError;
  }

  /* saves mem handle for freeing later */
  tdSMPRequestBody->IndirectSMPReqosMemHandle = IndirectSMPReqosMemHandle;
  tdSMPRequestBody->IndirectSMPResposMemHandle = IndirectSMPResposMemHandle;

  /* saves Indirect SMP request/repsonse pointer and length for free them later */
  tdSMPRequestBody->IndirectSMPReq = IndirectSMPReq;
  tdSMPRequestBody->IndirectSMPResp = IndirectSMPResp;
  tdSMPRequestBody->IndirectSMPReqLen = smpBodySize + 4;
  tdSMPRequestBody->IndirectSMPRespLen = expectedRspLen;

  /* fill in indirect SMP request fields */
  TI_DBG3(("tdSMPStart: INDIRECT smp payload\n"));

  /* SMP request and response initialization */
  osti_memset(&tdSMPFrameHeader, 0, sizeof(tdssSMPFrameHeader_t));
  osti_memset(IndirectSMPReq, 0, smpBodySize + 4);
  osti_memset(IndirectSMPResp, 0, expectedRspLen);

  /* SMP request header */
  tdSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
  tdSMPFrameHeader.smpFunction = (bit8)functionCode;
  tdSMPFrameHeader.smpFunctionResult = 0;
  tdSMPFrameHeader.smpReserved = 0;

  osti_memcpy(IndirectSMPReq, &tdSMPFrameHeader, 4);
  osti_memcpy(IndirectSMPReq+4, pSmpBody, smpBodySize);

  /* Indirect SMP request */
  agSMPFrame->outFrameBuf = agNULL;
  agSMPFrame->outFrameAddrUpper32 = IndirectSMPReqPhysUpper32;
  agSMPFrame->outFrameAddrLower32 = IndirectSMPReqPhysLower32;
  agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */

  /* Indirect SMP response */
  agSMPFrame->expectedRespLen = expectedRspLen;
  agSMPFrame->inFrameLen = expectedRspLen; /* without last 4 byte crc */
  agSMPFrame->inFrameAddrUpper32 = IndirectSMPRespPhysUpper32;
  agSMPFrame->inFrameAddrLower32 = IndirectSMPRespPhysLower32;
#endif
  }
  else /* SPCv controller */
  {
    /* only direct mode for both request and response */
    TI_DBG3(("tdSMPStart: DIRECT smp payload\n"));
    agSMPFrame->flag = 0;
    osti_memset(&tdSMPFrameHeader, 0, sizeof(tdssSMPFrameHeader_t));
    osti_memset(tdSMPRequestBody->smpPayload, 0, SMP_DIRECT_PAYLOAD_LIMIT);

    /* SMP header */
    tdSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
    tdSMPFrameHeader.smpFunction = (bit8)functionCode;
    tdSMPFrameHeader.smpFunctionResult = 0;
    tdSMPFrameHeader.smpReserved = 0;

    osti_memcpy(tdSMPRequestBody->smpPayload, &tdSMPFrameHeader, 4);
//    osti_memcpy((tdSMPRequestBody->smpPayload)+4, pSmpBody, smpBodySize);
    osti_memcpy(&(tdSMPRequestBody->smpPayload[4]), pSmpBody, smpBodySize);

    /* direct SMP payload eg) REPORT_GENERAL, DISCOVER etc */
    agSMPFrame->outFrameBuf = tdSMPRequestBody->smpPayload;
    agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */
    /* to specify DIRECT SMP response */
    agSMPFrame->inFrameLen = 0;

    /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
    /* force smp repsonse to be direct */
    agSMPFrame->expectedRespLen = 0;
#else
    agSMPFrame->expectedRespLen = expectedRspLen;
#endif
//    tdhexdump("tdSMPStart", (bit8*)agSMPFrame->outFrameBuf, agSMPFrame->outFrameLen);
//    tdhexdump("tdSMPStart new", (bit8*)tdSMPRequestBody->smpPayload, agSMPFrame->outFrameLen);
//    tdhexdump("tdSMPStart - tdSMPRequestBody", (bit8*)tdSMPRequestBody, sizeof(tdssSMPRequestBody_t));
  }


  if (agDevHandle == agNULL)
  {
    TI_DBG1(("tdSMPStart: !!! agDevHandle is NULL !!! \n"));
    return tiError;
  }

  tdSMPRequestBody->queueNumber = queueNumber;
  status = saSMPStart(
                      agRoot,
                      agIORequest,
                      queueNumber, //tdsaAllShared->SMPQNum, //tdsaRotateQnumber(tiRoot, oneDeviceData),
                      agDevHandle,
                      agRequestType,
                      agSASRequestBody,
                      &ossaSMPCompleted
                      );

  if (status == AGSA_RC_SUCCESS)
  {
    /* start SMP timer */
    if (functionCode == SMP_REPORT_GENERAL || functionCode == SMP_DISCOVER ||
        functionCode == SMP_REPORT_PHY_SATA || functionCode == SMP_CONFIGURE_ROUTING_INFORMATION
        )
    {
      tdsaDiscoverySMPTimer(tiRoot, onePortContext, functionCode, tdSMPRequestBody);
    }
    return tiSuccess;
  }
  else if (status == AGSA_RC_BUSY)
  {
    /* set timer */
    if (functionCode == SMP_REPORT_GENERAL || functionCode == SMP_DISCOVER ||
        functionCode == SMP_REPORT_PHY_SATA || functionCode == SMP_CONFIGURE_ROUTING_INFORMATION)
    {
      /* only for discovery related SMPs*/
      tdsaSMPBusyTimer(tiRoot, onePortContext, oneDeviceData, tdSMPRequestBody);
      return tiSuccess;
    }
    else if (functionCode == SMP_PHY_CONTROL)
    {
      ostiFreeMemory(
                     tiRoot,
                     osMemHandle,
                     sizeof(tdssSMPRequestBody_t)
                     );
      return tiBusy;
    }
    else
    {
      ostiFreeMemory(
                     tiRoot,
                     osMemHandle,
                     sizeof(tdssSMPRequestBody_t)
                     );
      return tiBusy;
    }
  }
  else /* AGSA_RC_FAILURE */
  {
    /* discovery failure or task management failure */
    if (functionCode == SMP_REPORT_GENERAL || functionCode == SMP_DISCOVER ||
        functionCode == SMP_REPORT_PHY_SATA || functionCode == SMP_CONFIGURE_ROUTING_INFORMATION)
    {
      tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
    }
    ostiFreeMemory(
                   tiRoot,
                   osMemHandle,
                   sizeof(tdssSMPRequestBody_t)
                   );

    return tiError;
  }
}

#ifdef REMOVED
/*****************************************************************************
*! \brief  tdsaFindLocalLinkRate
*
*  Purpose:  This function finds local link rate.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   tdsaPortStartInfo: Pointer to the port start information.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL bit8
tdsaFindLocalLinkRate(
                    tiRoot_t                  *tiRoot,
                    tdsaPortStartInfo_t       *tdsaPortStartInfo
                    )
{
  bit8 ans = SAS_CONNECTION_RATE_3_0G; /* default */
  bit32 phyProperties;

  phyProperties = tdsaPortStartInfo->agPhyConfig.phyProperties;

  TI_DBG3(("tdsaFindLocalLinkRate: start\n"));
  if (phyProperties & 0x4)
  {
    ans = SAS_CONNECTION_RATE_6_0G;
  }
  if (phyProperties & 0x2)
  {
    ans = SAS_CONNECTION_RATE_3_0G;
  }
  if (phyProperties & 0x1)
  {
    ans = SAS_CONNECTION_RATE_1_5G;
  }
  TI_DBG3(("tdsaFindLocalLinkRate: ans 0x%x\n", ans));
  return ans;
}
#endif
/*****************************************************************************
*! \brief  tdsaConfigureRouteTimer
*
*  Purpose:  This function sets timers for configuring routing of discovery and
*            its callback function.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander.
*  \param   ptdSMPDiscoverResp: Pointer to SMP discover repsonse data.
*
*  \return:
*           None
*
*   \note: called by tdsaDiscoverRespRcvd()
*
*****************************************************************************/
osGLOBAL void
tdsaConfigureRouteTimer(tiRoot_t                 *tiRoot,
                        tdsaPortContext_t        *onePortContext,
                        tdsaExpander_t           *oneExpander,
                        smpRespDiscover_t        *ptdSMPDiscoverResp
                        )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDiscovery_t             *discovery;

  TI_DBG1(("tdsaConfigureRouteTimer: start\n"));
  TI_DBG1(("tdsaConfigureRouteTimer: pid %d\n", onePortContext->id));

  discovery = &(onePortContext->discovery);

  TI_DBG1(("tdsaConfigureRouteTimer: onePortContext %p oneExpander %p ptdSMPDiscoverResp %p\n", onePortContext, oneExpander, ptdSMPDiscoverResp));

  TI_DBG1(("tdsaConfigureRouteTimer: discovery %p \n", discovery));

  TI_DBG1(("tdsaConfigureRouteTimer:  pid %d configureRouteRetries %d\n", onePortContext->id, discovery->configureRouteRetries));

  TI_DBG1(("tdsaConfigureRouteTimer: discovery->status %d\n", discovery->status));

  if (discovery->configureRouteTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->configureRouteTimer
              );
  }

  TI_DBG1(("tdsaConfigureRouteTimer: UsecsPerTick %d\n", Initiator->OperatingOption.UsecsPerTick));
  TI_DBG1(("tdsaConfigureRouteTimer: Timervalue %d\n", CONFIGURE_ROUTE_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick));

  tdsaSetTimerRequest(
                    tiRoot,
                    &discovery->configureRouteTimer,
                    CONFIGURE_ROUTE_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                    tdsaConfigureRouteTimerCB,
                    (void *)onePortContext, 
                    (void *)oneExpander,
                    (void *)ptdSMPDiscoverResp
                   );

  tdsaAddTimer (
              tiRoot,
              &Initiator->timerlist,
              &discovery->configureRouteTimer
              );

  return;
}

/*****************************************************************************
*! \brief  tdsaConfigureRouteTimerCB
*
*  Purpose:  This function is callback function for tdsaConfigureRouteTimer.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaConfigureRouteTimerCB(
                          tiRoot_t    * tiRoot,
                          void        * timerData1,
                          void        * timerData2,
                          void        * timerData3
                         )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaPortContext_t           *onePortContext;
  tdsaExpander_t              *oneExpander;
  smpRespDiscover_t           *ptdSMPDiscoverResp;
  tdsaDiscovery_t             *discovery;

  TI_DBG1(("tdsaConfigureRouteTimerCB: start\n"));

  onePortContext = (tdsaPortContext_t *)timerData1;
  oneExpander = (tdsaExpander_t *)timerData2;
  ptdSMPDiscoverResp = (smpRespDiscover_t *)timerData3;

  discovery = &(onePortContext->discovery);

  TI_DBG1(("tdsaConfigureRouteTimerCB: onePortContext %p oneExpander %p ptdSMPDiscoverResp %p\n", onePortContext, oneExpander, ptdSMPDiscoverResp));

  TI_DBG1(("tdsaConfigureRouteTimerCB: discovery %p\n", discovery));

  TI_DBG1(("tdsaConfigureRouteTimerCB: pid %d configureRouteRetries %d\n", onePortContext->id, discovery->configureRouteRetries));

  TI_DBG1(("tdsaConfigureRouteTimerCB: discovery.status %d\n", discovery->status));

  discovery->configureRouteRetries++;
  if (discovery->configureRouteRetries >= DISCOVERY_RETRIES)
  {
    TI_DBG1(("tdsaConfigureRouteTimerCB: retries are over\n"));
    discovery->configureRouteRetries = 0;
    /* failed the discovery */
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
    if (discovery->configureRouteTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->configureRouteTimer
                   );
    }
    return;
  }


  if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
  {
    TI_DBG1(("tdsaConfigureRouteTimerCB: proceed by calling tdsaSASDownStreamDiscoverExpanderPhy\n"));
    tdhexdump("tdsaConfigureRouteTimerCB", (bit8*)ptdSMPDiscoverResp, sizeof(smpRespDiscover_t));
    discovery->configureRouteRetries = 0;

    tdsaSASDownStreamDiscoverExpanderPhy(tiRoot, onePortContext, oneExpander, ptdSMPDiscoverResp);
  }
  else
  {
    TI_DBG1(("tdsaConfigureRouteTimerCB: setting timer again\n"));
    /* set the timer again */
    tdsaSetTimerRequest(
                        tiRoot,
                        &discovery->configureRouteTimer,
                        CONFIGURE_ROUTE_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                        tdsaConfigureRouteTimerCB,
                        (void *)onePortContext, 
                        (void *)oneExpander,
                        (void *)ptdSMPDiscoverResp
                       );

    tdsaAddTimer (
                  tiRoot,
                  &Initiator->timerlist,
                  &discovery->configureRouteTimer
                  );
   }
//  tdsaReportGeneralSend(tiRoot, oneDeviceData);
  return;
}

/*****************************************************************************
*! \brief  tdsaDiscoveryTimer
*
*  Purpose:  This function sets timers for discovery and its callback
*            function.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoveryTimer(tiRoot_t                 *tiRoot,
                   tdsaPortContext_t        *onePortContext,
                   tdsaDeviceData_t         *oneDeviceData
                   )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDiscovery_t             *discovery;

  TI_DBG1(("tdsaDiscoveryTimer: start\n"));
  TI_DBG1(("tdsaDiscoveryTimer: pid %d\n", onePortContext->id));

  discovery = &(onePortContext->discovery);

  if (discovery->discoveryTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->discoveryTimer
              );
  }

  TI_DBG1(("tdsaDiscoveryTimer: UsecsPerTick %d\n", Initiator->OperatingOption.UsecsPerTick));
  TI_DBG1(("tdsaDiscoveryTimer: Timervalue %d\n", DISCOVERY_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick));

  tdsaSetTimerRequest(
                    tiRoot,
                    &discovery->discoveryTimer,
                    DISCOVERY_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                    tdsaDiscoveryTimerCB,
                    oneDeviceData, 
                    agNULL,
                    agNULL
                   );

  tdsaAddTimer (
              tiRoot,
              &Initiator->timerlist,
              &discovery->discoveryTimer
              );

  return;
}

/*****************************************************************************
*! \brief  tdsaDiscoveryTimerCB
*
*  Purpose:  This function is callback function for discovery timer.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoveryTimerCB(
                       tiRoot_t    * tiRoot,
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                      )
{
  tdsaDeviceData_t            *oneDeviceData;
  oneDeviceData = (tdsaDeviceData_t *)timerData1;

  TI_DBG1(("tdsaDiscoveryTimerCB: start\n"));

  if (oneDeviceData->registered == agTRUE)
  {
    TI_DBG1(("tdsaDiscoveryTimerCB: resumes discovery\n"));
    tdsaReportGeneralSend(tiRoot, oneDeviceData);
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaDeviceRegistrationTimer
*
*  Purpose:  This function sets timers for device registration in discovery
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \return:
*           None
*
*   \note: called by tdsaSASFullDiscover() or tdsaSASIncrementalDiscover()
*          or tdsaDeviceRegistrationTimerCB()
*
*****************************************************************************/
osGLOBAL void
tdsaDeviceRegistrationTimer(tiRoot_t                 *tiRoot,
                            tdsaPortContext_t        *onePortContext,
                            tdsaDeviceData_t         *oneDeviceData
                            )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDiscovery_t             *discovery;

  TI_DBG1(("tdsaDeviceRegistrationTimer: start\n"));
  TI_DBG1(("tdsaDeviceRegistrationTimer: pid %d\n", onePortContext->id));

  discovery = &(onePortContext->discovery);

  if (discovery->deviceRegistrationTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->deviceRegistrationTimer
              );
  }

  TI_DBG1(("tdsaDeviceRegistrationTimer: UsecsPerTick %d\n", Initiator->OperatingOption.UsecsPerTick));
  TI_DBG1(("tdsaDeviceRegistrationTimer: Timervalue %d\n", DEVICE_REGISTRATION_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick));

  tdsaSetTimerRequest(
                    tiRoot,
                    &discovery->deviceRegistrationTimer,
                    DEVICE_REGISTRATION_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                    tdsaDeviceRegistrationTimerCB,
                    onePortContext,
                    oneDeviceData,
                    agNULL
                   );

  tdsaAddTimer (
              tiRoot,
              &Initiator->timerlist,
              &discovery->deviceRegistrationTimer
              );
  return;
}

/*****************************************************************************
*! \brief  tdsaDeviceRegistrationTimerCB
*
*  Purpose:  This function is callback function for tdsaDeviceRegistrationTimer.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDeviceRegistrationTimerCB(
                       tiRoot_t    * tiRoot,
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                      )
{
  tdsaPortContext_t        *onePortContext;
  tdsaDeviceData_t         *oneDeviceData;
  tdsaDiscovery_t          *discovery;

  TI_DBG1(("tdsaDeviceRegistrationTimerCB: start\n"));

  onePortContext = (tdsaPortContext_t *)timerData1;
  oneDeviceData = (tdsaDeviceData_t *)timerData2;
  discovery = &(onePortContext->discovery);

  if (oneDeviceData->registered == agFALSE)
  {
    discovery->deviceRetistrationRetries++;
    if (discovery->deviceRetistrationRetries >= DISCOVERY_RETRIES)
    {
      TI_DBG1(("tdsaDeviceRegistrationTimerCB: retries are over\n"));
      discovery->deviceRetistrationRetries = 0;
      /* failed the discovery */
      tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
      if (discovery->deviceRegistrationTimer.timerRunning == agTRUE)
      {
        tdsaKillTimer(
                      tiRoot,
                      &discovery->deviceRegistrationTimer
                     );
      }
    }
    else
    {
      TI_DBG1(("tdsaDeviceRegistrationTimerCB: keep retrying\n"));
      /* start timer for device registration */
      tdsaDeviceRegistrationTimer(tiRoot, onePortContext, oneDeviceData);
    }
  }
  else
  {
    /* go ahead; continue the discovery */
    discovery->deviceRetistrationRetries = 0;
    tdsaSASUpStreamDiscoverStart(tiRoot, onePortContext, oneDeviceData);
  }
}

/*****************************************************************************
*! \brief  tdsaSMPBusyTimer
*
*  Purpose:  This function sets timers for busy of saSMPStart.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   tdSMPRequestBody: Pointer to the SMP request body.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSMPBusyTimer(tiRoot_t                 *tiRoot,
                 tdsaPortContext_t        *onePortContext,
                 tdsaDeviceData_t         *oneDeviceData,
                 tdssSMPRequestBody_t     *tdSMPRequestBody
                 )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDiscovery_t             *discovery;

  TI_DBG1(("tdsaSMPBusyTimer: start\n"));
  TI_DBG1(("tdsaSMPBusyTimer: pid %d\n", onePortContext->id));

  discovery = &(onePortContext->discovery);

  if (discovery->SMPBusyTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->SMPBusyTimer
              );
  }

  tdsaSetTimerRequest(
                    tiRoot,
                    &discovery->SMPBusyTimer,
                    SMP_BUSY_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                    tdsaSMPBusyTimerCB,
                    onePortContext,
                    oneDeviceData, 
                    tdSMPRequestBody
                   );

  tdsaAddTimer (
              tiRoot,
              &Initiator->timerlist,
              &discovery->SMPBusyTimer
              );
  return;
}

/*****************************************************************************
*! \brief  tdsaSMPBusyTimerCB
*
*  Purpose:  This function is callback function for SMP busy timer.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSMPBusyTimerCB(
                       tiRoot_t    * tiRoot,
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                       )
{
  tdsaRoot_t                  *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t                  *agRoot;
  tdsaPortContext_t           *onePortContext;
  tdsaDeviceData_t            *oneDeviceData;
  tdssSMPRequestBody_t        *tdSMPRequestBody;
  agsaSASRequestBody_t        *agSASRequestBody;
  agsaIORequest_t             *agIORequest;
  agsaDevHandle_t             *agDevHandle;
  tdsaDiscovery_t             *discovery;
  bit32                       status = AGSA_RC_FAILURE;

  TI_DBG1(("tdsaSMPBusyTimerCB: start\n"));

  onePortContext = (tdsaPortContext_t *)timerData1;
  oneDeviceData = (tdsaDeviceData_t *)timerData2;
  tdSMPRequestBody = (tdssSMPRequestBody_t *)timerData3;
  agRoot = oneDeviceData->agRoot;
  agIORequest = &(tdSMPRequestBody->agIORequest);
  agDevHandle = oneDeviceData->agDevHandle;
  agSASRequestBody = &(tdSMPRequestBody->agSASRequestBody);
  discovery = &(onePortContext->discovery);

  discovery->SMPRetries++;

  if (discovery->SMPRetries < SMP_BUSY_RETRIES)
  {
    status = saSMPStart(
                         agRoot,
                         agIORequest,
                         tdsaAllShared->SMPQNum, //tdsaRotateQnumber(tiRoot, oneDeviceData),
                         agDevHandle,
                         AGSA_SMP_INIT_REQ,
                         agSASRequestBody,
                         &ossaSMPCompleted
                         );
  }

  if (status == AGSA_RC_SUCCESS)
  {
    discovery->SMPRetries = 0;
    if (discovery->SMPBusyTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->SMPBusyTimer
                   );
    }
  }
  else if (status == AGSA_RC_FAILURE)
  {
    discovery->SMPRetries = 0;
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
    if (discovery->SMPBusyTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &discovery->SMPBusyTimer
                   );
    }
  }
  else /* AGSA_RC_BUSY */
  {
    if (discovery->SMPRetries >= SMP_BUSY_RETRIES)
    {
      /* done with retris; give up */
      TI_DBG1(("tdsaSMPBusyTimerCB: retries are over\n"));
      discovery->SMPRetries = 0;
      tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
      if (discovery->SMPBusyTimer.timerRunning == agTRUE)
      {
        tdsaKillTimer(
                      tiRoot,
                      &discovery->SMPBusyTimer
                     );
      }
    }
    else
    {
      /* keep retrying */
      tdsaSMPBusyTimer(tiRoot, onePortContext, oneDeviceData, tdSMPRequestBody);
    }
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaBCTimer
*
*  Purpose:  This function sets timers for sending ID device data only for
*            directly attached SATA device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   tdSMPRequestBody: Pointer to the SMP request body.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaBCTimer(tiRoot_t                 *tiRoot,
            tdsaPortContext_t        *onePortContext
           )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDiscovery_t             *discovery;


  TI_DBG1(("tdsaBCTimer: start\n"));

  discovery = &(onePortContext->discovery);

  if (discovery->BCTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->BCTimer
              );
  }

  if (onePortContext->valid == agTRUE)
  {
    tdsaSetTimerRequest(
                        tiRoot,
                        &discovery->BCTimer,
                        BC_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                        tdsaBCTimerCB,
                        onePortContext,
                        agNULL,
                        agNULL
                        );

    tdsaAddTimer(
                 tiRoot,
                 &Initiator->timerlist,
                 &discovery->BCTimer
                );

  }

  return;
}

/*****************************************************************************
*! \brief  tdsaBCTimerCB
*
*  Purpose:  This function is callback function for SATA ID device data.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaBCTimerCB(
              tiRoot_t    * tiRoot,
              void        * timerData1,
              void        * timerData2,
              void        * timerData3
              )
{
  tdsaPortContext_t           *onePortContext;
  tdsaDiscovery_t             *discovery;

  TI_DBG1(("tdsaBCTimerCB: start\n"));

  onePortContext = (tdsaPortContext_t *)timerData1;
  discovery = &(onePortContext->discovery);

  discovery->ResetTriggerred = agFALSE;

  if (onePortContext->valid == agTRUE)
  {
    tdsaDiscover(
                 tiRoot,
                 onePortContext,
                 TDSA_DISCOVERY_TYPE_SAS,
                 TDSA_DISCOVERY_OPTION_INCREMENTAL_START
                );
  }
  if (discovery->BCTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->BCTimer
              );
  }

  return;
}

/*****************************************************************************
*! \brief  tdsaDiscoverySMPTimer
*
*  Purpose:  This function sets timers for sending discovery-related SMP
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   functionCode: SMP function.
*  \param   tdSMPRequestBody: Pointer to the SMP request body.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoverySMPTimer(tiRoot_t                 *tiRoot,
                      tdsaPortContext_t        *onePortContext,
                      bit32                    functionCode, /* smp function code */
                      tdssSMPRequestBody_t     *tdSMPRequestBody
                     )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  tdsaDiscovery_t             *discovery;

  TI_DBG3(("tdsaDiscoverySMPTimer: start\n"));
  TI_DBG3(("tdsaDiscoverySMPTimer: pid %d SMPFn 0x%x\n", onePortContext->id, functionCode));

  /* start the SMP timer which works as SMP application timer */
  discovery = &(onePortContext->discovery);

  if (discovery->DiscoverySMPTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &discovery->DiscoverySMPTimer
              );
  }
  tdsaSetTimerRequest(
                    tiRoot,
                    &discovery->DiscoverySMPTimer,
                    SMP_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                    tdsaDiscoverySMPTimerCB,
                    onePortContext,
                    tdSMPRequestBody,
                    agNULL
                   );

  tdsaAddTimer (
              tiRoot,
              &Initiator->timerlist,
              &discovery->DiscoverySMPTimer
              );

  return;
}

/*****************************************************************************
*! \brief  tdsaDiscoverySMPTimerCB
*
*  Purpose:  This function is callback function for tdsaDiscoverySMPTimer.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaDiscoverySMPTimerCB(
                        tiRoot_t    * tiRoot,
                        void        * timerData1,
                        void        * timerData2,
                        void        * timerData3
                       )
{
  agsaRoot_t                  *agRoot;
  tdsaPortContext_t           *onePortContext;
  bit8                        SMPFunction;
#ifndef DIRECT_SMP
  tdssSMPFrameHeader_t        *tdSMPFrameHeader;
  bit8                        smpHeader[4];
#endif
  tdssSMPRequestBody_t        *tdSMPRequestBody;
  tdsaDiscovery_t             *discovery;
  tdsaDeviceData_t            *oneDeviceData;
  agsaIORequest_t             *agAbortIORequest = agNULL;
  tdIORequestBody_t           *tdAbortIORequestBody = agNULL;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  void                        *osMemHandle;
  agsaIORequest_t             *agToBeAbortIORequest = agNULL;

  TI_DBG1(("tdsaDiscoverySMPTimerCB: start\n"));

  /* no retry
     if discovery related SMP, fail the discovery
     else ....
     be sure to abort SMP
  */
  onePortContext = (tdsaPortContext_t *)timerData1;
  tdSMPRequestBody = (tdssSMPRequestBody_t *)timerData2;

  discovery = &(onePortContext->discovery);
  oneDeviceData = tdSMPRequestBody->tdDevice;
  agToBeAbortIORequest = &(tdSMPRequestBody->agIORequest);
  agRoot = oneDeviceData->agRoot;

#ifdef DIRECT_SMP
  SMPFunction = tdSMPRequestBody->smpPayload[1];
#else
  saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 0, smpHeader, 4);
  tdSMPFrameHeader = (tdssSMPFrameHeader_t *)smpHeader;
  SMPFunction = tdSMPFrameHeader->smpFunction;
#endif

  TI_DBG1(("tdsaDiscoverySMPTimerCB: SMP function 0x%x\n", SMPFunction));

  if (discovery->DiscoverySMPTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
                  tiRoot,
                  &discovery->DiscoverySMPTimer
                 );
  }
  switch (SMPFunction)
  {
  case SMP_REPORT_GENERAL: /* fall through */
  case SMP_DISCOVER:  /* fall through */
  case SMP_CONFIGURE_ROUTING_INFORMATION:  /* fall through */
    TI_DBG1(("tdsaDiscoverySMPTimerCB: failing discovery, SMP function 0x%x\n", SMPFunction));
    tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
    return; 
  case SMP_REPORT_PHY_SATA:
    TI_DBG1(("tdsaDiscoverySMPTimerCB: failing discovery, SMP function SMP_REPORT_PHY_SATA\n"));
    tdsaSATADiscoverDone(tiRoot, onePortContext, tiError);
    break;
  default:
    /* do nothing */
    TI_DBG1(("tdsaDiscoverySMPTimerCB: Error!!!! not allowed case\n"));
    break;
  }

  if (onePortContext->discovery.SeenBC == agTRUE)
  {
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
      TI_DBG1(("tdsaDiscoverySMPTimerCB: ostiAllocMemory failed...\n"));
      return;
    }

    if (tdAbortIORequestBody == agNULL)
    {
      /* let os process IO */
      TI_DBG1(("tdsaDiscoverySMPTimerCB: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
      return;
    }

    /* setup task management structure */
    tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
    /* setting callback */
    tdAbortIORequestBody->IOCompletionFunc = itdssIOAbortedHandler;

    tdAbortIORequestBody->tiDevHandle = (tiDeviceHandle_t *)&(oneDeviceData->tiDeviceHandle);

    /* initialize agIORequest */
    agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
    agAbortIORequest->osData = (void *) tdAbortIORequestBody;
    agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

    /* SMPAbort - abort one */
    saSMPAbort(agRoot,
               agAbortIORequest,
               0,
               oneDeviceData->agDevHandle,
               0, /* abort one */
               agToBeAbortIORequest,
               agNULL
               );

  }
  return;
}


/*****************************************************************************
*! \brief  tdsaSATAIDDeviceTimer
*
*  Purpose:  This function sets timers for sending ID device data only for
*            directly attached SATA device.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   tdSMPRequestBody: Pointer to the SMP request body.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSATAIDDeviceTimer(tiRoot_t                 *tiRoot,
                      tdsaDeviceData_t         *oneDeviceData
                     )
{
  tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t                  *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;

  TI_DBG1(("tdsaSATAIDDeviceTimer: start\n"));

  if (oneDeviceData->SATAIDDeviceTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &oneDeviceData->SATAIDDeviceTimer
              );
  }

  tdsaSetTimerRequest(
                    tiRoot,
                    &oneDeviceData->SATAIDDeviceTimer,
                    SATA_ID_DEVICE_DATA_TIMER_VALUE/Initiator->OperatingOption.UsecsPerTick,
                    tdsaSATAIDDeviceTimerCB,
                    oneDeviceData,
                    agNULL,
                    agNULL
                   );

  tdsaAddTimer (
              tiRoot,
              &Initiator->timerlist,
              &oneDeviceData->SATAIDDeviceTimer
              );

  return;
}

/*****************************************************************************
*! \brief  tdsaSATAIDDeviceTimerCB
*
*  Purpose:  This function is callback function for SATA ID device data.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   timerData1: Pointer to timer-related data structure
*  \param   timerData2: Pointer to timer-related data structure
*  \param   timerData3: Pointer to timer-related data structure
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaSATAIDDeviceTimerCB(
                       tiRoot_t    * tiRoot,
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                       )
{
  tdsaDeviceData_t            *oneDeviceData;

  TI_DBG1(("tdsaSATAIDDeviceTimerCB: start\n"));

  oneDeviceData = (tdsaDeviceData_t *)timerData1;

  /* send identify device data */
  tdssSubAddSATAToSharedcontext(tiRoot, oneDeviceData);

  if (oneDeviceData->SATAIDDeviceTimer.timerRunning == agTRUE)
  {
    tdsaKillTimer(
              tiRoot,
              &oneDeviceData->SATAIDDeviceTimer
              );
  }

  return;
}

#endif /* TD_DISCOVER */
