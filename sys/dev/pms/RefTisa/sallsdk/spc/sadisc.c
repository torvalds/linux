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
/*! \file sadisc.c
 *  \brief The file implements the functions to do SAS/SATA discovery
 */

/******************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'C'
#endif

/******************************************************************************/
/*! \brief Start/Abort SAS/SATA discovery
 *
 *  Start/Abort SAS/SATA discovery
 *
 *  \param agRoot         Handles for this instance of SAS/SATA hardware
 *  \param agPortContext  Pointer to this instance of port context
 *  \param type           Specifies the type(s) of discovery operation to start or cancel
 *  \param option         Specified the discovery option
 *
 *  \return If discovery is started/aborted successfully
 *          - \e AGSA_RC_SUCCESS discovery is started/aborted successfully
 *          - \e AGSA_RC_FAILURE discovery is not started/aborted successfully
 *
 */
/*******************************************************************************/
GLOBAL bit32 saDiscover(
  agsaRoot_t        *agRoot,
  agsaPortContext_t *agPortContext,
  bit32             type,
  bit32             option
  )
{
  /* Currently not supported */
  return AGSA_RC_FAILURE;
}

/******************************************************************************/
/*! \brief Function for target to remove stale initiator device handle
 *
 *  function is called to ask the LL layer to remove all LL layer and SPC firmware
 *  internal resources associated with a device handle
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param agDevHandle  Handle of the device that this I/O request will be made on
 *
 *  \return If the device handle is removed successfully
 *          - \e AGSA_RC_SUCCESS the device handle is removed successfully
 *          - \e AGSA_RC_BUSY the device is busy, cannot be removed now
 *
 */
/*******************************************************************************/
GLOBAL bit32 saDeregisterDeviceHandle(
  agsaRoot_t      *agRoot,
  agsaContext_t   *agContext,
  agsaDevHandle_t *agDevHandle,
  bit32           queueNum
  )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaDeviceDesc_t      *pDevice;
  agsaPort_t            *pPort;
  bit32                 ret = AGSA_RC_SUCCESS;
  bit32                 deviceid, portid;
  bit32                 deviceIdx;

  OS_ASSERT(agDevHandle != agNULL, "saDeregisterDeviceHandle agDevHandle is NULL");

  smTraceFuncEnter(hpDBG_VERY_LOUD, "za");

  if(agNULL == agDevHandle)
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "za");
    return AGSA_RC_FAILURE;
  }

  pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);

  OS_ASSERT(pDevice != agNULL, "saDeregisterDeviceHandle pDevice is NULL");
  if(pDevice == agNULL)
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "za");
    return AGSA_RC_FAILURE;
  }

  /* find device id */
  deviceid = pDevice->DeviceMapIndex;
  deviceIdx = deviceid & DEVICE_ID_BITS;
  OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
  pPort = pDevice->pPort;
  /* find port id */
  portid = pPort->portId;

  SA_DBG3(("saDeregisterDeviceHandle: start DeviceHandle %p\n", agDevHandle));
  SA_DBG1(("saDeregisterDeviceHandle: deviceId 0x%x Device Context %p\n", deviceid, pDevice));

  if ((deviceid != saRoot->DeviceMap[deviceIdx].DeviceIdFromFW) ||
     (pDevice != saRoot->DeviceMap[deviceIdx].DeviceHandle))
  {
    SA_DBG1(("saDeregisterDeviceHandle: Not match failure\n"));
    ret = AGSA_RC_FAILURE;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "za");
    return ret;
  }

  /* Build IOMB and send it to SPC */
  ret = mpiDeregDevHandleCmd(agRoot, agContext, pDevice, deviceid, portid, queueNum);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "za");
  return ret;
}

/******************************************************************************/
/*! \brief Function for target to remove stale initiator device handle
 *
 *  function is called to ask the LL layer to remove all LL layer internal resources
 *  associated with a device handle
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param agDevHandle  Handle of the device that this I/O request will be made on
 *
 *  \return If the device handle is removed successfully
 *          - \e AGSA_RC_SUCCESS the device handle is removed successfully
 *          - \e AGSA_RC_BUSY the device is busy, cannot be removed now
 *
 */
/*******************************************************************************/
GLOBAL bit32 siRemoveDevHandle(
  agsaRoot_t      *agRoot,
  agsaDevHandle_t *agDevHandle
  )
{
  agsaDeviceDesc_t      *pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
  agsaPort_t            *pPort;
  bit32                 ret = AGSA_RC_SUCCESS;

  OS_ASSERT(pDevice != agNULL, "siRemoveDevHandle is NULL");
  smTraceFuncEnter(hpDBG_VERY_LOUD,"zb");

  if (pDevice == agNULL)
  {
    SA_DBG1(("siRemoveDevHandle: pDevice is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zb");
    return AGSA_RC_FAILURE;
  }

  /* If it's to remove an initiator device handle */
  if ( &(pDevice->initiatorDevHandle) == agDevHandle )
  {
    (pDevice->initiatorDevHandle).sdkData = agNULL;
  }
  /* If it's to remove an target device handle */
  else if ( &(pDevice->targetDevHandle) == agDevHandle )
  {
    (pDevice->targetDevHandle).sdkData = agNULL;
  }
  else
  {
    SA_ASSERT(agFALSE, "");
  }

  /* remove the device descriptor if it doesn't have either initiator handle and target handle */
  if ( (agNULL == (pDevice->initiatorDevHandle).sdkData)
      && (agNULL == (pDevice->targetDevHandle).sdkData) )
  {
    /* Find the port of the device */
    pPort = pDevice->pPort;

    /* remove the device descriptor free discover list */
    switch ( pDevice->deviceType )
    {
      case STP_DEVICE: /* fall through */
      case SSP_SMP_DEVICE:
      case DIRECT_SATA_DEVICE:
      {
        SA_DBG3(("siRemoveDevHandle: remove device context %p\n", pDevice));
        siPortDeviceRemove(agRoot, pPort, pDevice, agTRUE);
        break;
      }
      default:
      {
        SA_DBG1(("siRemoveDevHandle: switch. Not calling siPortDeviceRemove %d\n", pDevice->deviceType));
        break;
      }
    }
  }
  else
  {
    SA_DBG1(("siRemoveDevHandle: else. Not caling siPortDeviceRemove\n"));
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "zb");
  return ret;
}

/******************************************************************************/
/*! \brief Get Device Handles from a specific local port
 *
 *  Get a Device Handles
 *
 *  \param agRoot         Handles for this instance of SAS/SATA hardware
 *  \param agsaContext    Pointer to this API context
 *  \param agPortContext  Pointer to this instance of port context
 *  \param flags          Device flags
 *  \param agDev[]        Pointer of array of device handles
 *  \param MaxDevs        Specified Maximum number of Device Handles
 *
 *  \return If GetDeviceHandles is successfully or failure
 *          - \e AGSA_RC_SUCCESS GetDeviceHandles is successfully
 *          - \e AGSA_RC_FAILURE GetDeviceHandles is not successfully
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGetDeviceHandles(
  agsaRoot_t        *agRoot,
  agsaContext_t     *agContext,
  bit32             queueNum,
  agsaPortContext_t *agPortContext,
  bit32             flags,
  agsaDevHandle_t   *agDev[],
  bit32             skipCount,
  bit32             MaxDevs
  )
{
  agsaLLRoot_t      *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaPort_t        *pPort = (agsaPort_t *) (agPortContext->sdkData);
  bit32             portIndex, i;
  bit32             ret = AGSA_RC_SUCCESS;

  OS_ASSERT(pPort != agNULL, "saGetDeviceHandles is NULL");
  smTraceFuncEnter(hpDBG_VERY_LOUD,"zc");

  if (pPort == agNULL)
  {
    SA_DBG1(("saGetDeviceHandles: pPort is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zc");
    return AGSA_RC_FAILURE;
  }

  SA_DBG1(("saGetDeviceHandles: start portId %d\n", pPort->portId));

  /* save the device handles arrary pointer */
  for (i = 0; i < MaxDevs; i ++)
  {
    saRoot->DeviceHandle[i] = agDev[i];
  }

  /* send GET_DEVICE_HANDLE IOMB to SPC */
  portIndex = pPort->portId;
  mpiGetDeviceHandleCmd(agRoot, agContext, portIndex, flags, MaxDevs, queueNum, skipCount);

  /* return */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "zc");
  return ret;
}

/******************************************************************************/
/*! \brief Register New Device from a specific local port
 *
 *  Register New Device API
 *
 *  \param agRoot         Handles for this instance of SAS/SATA hardware
 *  \param agContext      Pointer to this API context
 *  \param agDeviceInfo   Pointer to this instance of device info
 *  \param agPortContext  Pointer to this instance of port context
 *
 *  \return If discovery is started/aborted successfully
 *          - \e AGSA_RC_SUCCESS discovery is started/aborted successfully
 *          - \e AGSA_RC_FAILURE discovery is not started/aborted successfully
 *
 */
/*******************************************************************************/
GLOBAL bit32 saRegisterNewDevice(
  agsaRoot_t            *agRoot,
  agsaContext_t         *agContext,
  bit32                 queueNum,
  agsaDeviceInfo_t      *agDeviceInfo,
  agsaPortContext_t     *agPortContext,
  bit16                 hostAssignedDeviceId
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaRegDevCmd_t     payload;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaPort_t          *pPort = (agsaPort_t *) (agPortContext->sdkData);
  agsaSASIdentify_t   remoteIdentify;
  bit32               i, phyId, sDTypeRate;
  agsaDeviceDesc_t    *pDevice = agNULL;

  OS_ASSERT(pPort != agNULL, "saRegisterNewDevice is NULL");
  OS_ASSERT(saRoot != agNULL, "saRoot is NULL");
  smTraceFuncEnter(hpDBG_VERY_LOUD,"zd");

  if(saRoot == agNULL)
  {
    SA_DBG1(("saRegisterNewDevice: saRoot == agNULL\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zd");
    return(AGSA_RC_FAILURE);
  }

  if (pPort == agNULL)
  {
    SA_DBG1(("saRegisterNewDevice: pPort is NULL \n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "zd");
    return AGSA_RC_FAILURE;
  }

  SA_DBG2(("saRegisterNewDevice: start portId %d Port Context %p\n", pPort->portId, agPortContext));

  SA_DBG2(("saRegisterNewDevice: smpTimeout 0x%x\n", agDeviceInfo->smpTimeout));
  SA_DBG2(("saRegisterNewDevice: it_NexusTimeout 0x%x\n", agDeviceInfo->it_NexusTimeout));
  SA_DBG2(("saRegisterNewDevice: firstBurstSize 0x%x\n", agDeviceInfo->firstBurstSize));
  SA_DBG2(("saRegisterNewDevice: devType_S_Rate 0x%x\n", agDeviceInfo->devType_S_Rate));
  SA_DBG2(("saRegisterNewDevice: flag 0x%x\n", agDeviceInfo->flag));
  SA_DBG2(("saRegisterNewDevice: hostAssignedDeviceId  0x%x\n",hostAssignedDeviceId ));
  SA_DBG2(("saRegisterNewDevice: Addr 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x\n",
          agDeviceInfo->sasAddressHi[0],agDeviceInfo->sasAddressHi[1],agDeviceInfo->sasAddressHi[2],agDeviceInfo->sasAddressHi[3],
          agDeviceInfo->sasAddressLo[0],agDeviceInfo->sasAddressLo[1],agDeviceInfo->sasAddressLo[2],agDeviceInfo->sasAddressLo[3] ));

  agDeviceInfo->devType_S_Rate &= DEV_LINK_RATE;

  /*
    Using agsaDeviceInfo_t, fill in only sas address and device type
    of identify address frame
  */
  si_memset(&remoteIdentify, 0, sizeof(agsaSASIdentify_t));
  for (i=0;i<4;i++)
  {
    remoteIdentify.sasAddressHi[i] = agDeviceInfo->sasAddressHi[i];
    remoteIdentify.sasAddressLo[i] = agDeviceInfo->sasAddressLo[i];
  }
  remoteIdentify.deviceType_addressFrameType = (bit8)(agDeviceInfo->devType_S_Rate & 0xC0);

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests)); /**/
    if(agNULL != pRequest)
    {
      saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      SA_DBG1(("saRegisterNewDevice, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      SA_DBG1(("saRegisterNewDevice, No request from free list Not using saRoot->freeReservedRequests\n"));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "zd");
      return AGSA_RC_BUSY;
    }
  }
  else
  {
    /* If LL Control request entry avaliable */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* checking bit5 for SATA direct device */
  if (!(agDeviceInfo->devType_S_Rate & 0x20))
  {
    /* SAS device */
    /* Add SAS device to the device list */
    pDevice = siPortSASDeviceAdd(agRoot,
                       pPort,
                       remoteIdentify,
                       agFALSE,
                       agDeviceInfo->smpTimeout,
                       agDeviceInfo->it_NexusTimeout,
                       agDeviceInfo->firstBurstSize,
                       agDeviceInfo->devType_S_Rate,
                       (agDeviceInfo->flag & DEV_INFO_MASK));
   }
   else
   {
    /* SATA device */
    /* Add SATA device to the device list */
    pDevice = siPortSATADeviceAdd(agRoot,
                                  pPort,
                                  agNULL,
                                  agNULL, /* no signature */
                                  agFALSE,
                                  0,
                                  agDeviceInfo->smpTimeout,
                                  agDeviceInfo->it_NexusTimeout,
                                  agDeviceInfo->firstBurstSize,
                                  agDeviceInfo->devType_S_Rate,
                                  (agDeviceInfo->flag & DEV_INFO_MASK));
    }

    SA_DBG1(("saRegisterNewDevice: Device Context %p, TypeRate 0x%x\n", pDevice, agDeviceInfo->devType_S_Rate));

    pRequest->pDevice = pDevice;

    /* adjust the flag bit to build the IOMB; use only bit0 and 1 */
    sDTypeRate = agDeviceInfo->devType_S_Rate << SHIFT24;
    sDTypeRate |= (agDeviceInfo->flag & 0x01);
    /* set AWT flag */
    sDTypeRate |= (agDeviceInfo->flag & 0x02) << 1;

    /* If the host assigned device ID is used, then set the HA bit. */
    if ( hostAssignedDeviceId != 0 )
    {
      sDTypeRate |= 2;
      SA_DBG3(("saRegisterNewDevice:hostAssignedDeviceId 0x%x sDTypeRate 0x%x\n",hostAssignedDeviceId,sDTypeRate ));
    }

    /* Add the MCN field */

    sDTypeRate |= ((agDeviceInfo->flag >> DEV_INFO_MCN_SHIFT) & 0xf) << 4;

    /* Add the IR field */
    sDTypeRate |= ((agDeviceInfo->flag >> DEV_INFO_IR_SHIFT) & 0x1) <<  3;

    /* Add the ATAPI protocol flag */
    sDTypeRate |= ((agDeviceInfo->flag & ATAPI_DEVICE_FLAG) << SHIFT9 );

    /* Add the AWT  flag */
    sDTypeRate |= (agDeviceInfo->flag & AWT_DEVICE_FLAG) ? (1 << SHIFT2) : 0;

    /* Add the XFER_READY flag  */
    sDTypeRate |= (agDeviceInfo->flag & XFER_RDY_PRIORTY_DEVICE_FLAG) ? (1 << SHIFT31) : 0;
    if(agDeviceInfo->flag & XFER_RDY_PRIORTY_DEVICE_FLAG)
    {
      SA_DBG1(("saRegisterNewDevice: sflag XFER_RDY_PRIORTY_DEVICE_FLAG sDTypeRate 0x%x\n",sDTypeRate ));
    }
#ifdef CCFLAG_FORCE_AWT_ON
    sDTypeRate |= (1 << SHIFT2);
    SA_DBG1(("saRegisterNewDevice: Force AWT_DEVICE_FLAG sDTypeRate 0x%x\n",sDTypeRate ));
#endif /* CCFLAG_FORCE_AWT_ON */

    /* create payload for IOMB */
    si_memset(&payload, 0, sizeof(agsaRegDevCmd_t));

    SA_DBG2(("saRegisterNewDevice,flag 0x%08X\n",agDeviceInfo->flag));
    if ((agDeviceInfo->devType_S_Rate & 0x30) == 0x20)
    {
      if(smIS_SPC(agRoot))
      {
        /* direct SATA device */
        phyId = (agDeviceInfo->flag & 0xF0);
      }
      else
      {
        phyId = (agDeviceInfo->flag & 0xF0) << SHIFT4;
      }
    }
    else
    {
      phyId = 0;
    }

    smTrace(hpDBG_VERY_LOUD,"QQ",phyId);
    /* TP:QQ phyId */
    smTrace(hpDBG_VERY_LOUD,"QR",pPort->portId);
    /* TP:QR portId */
    smTrace(hpDBG_VERY_LOUD,"QS",sDTypeRate);
    /* TP:QS sDTypeRate */
    smTrace(hpDBG_VERY_LOUD,"QT",agDeviceInfo->it_NexusTimeout);
    /* TP:QT agDeviceInfo->it_NexusTimeout */

    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaRegDevCmd_t, phyIdportId), (bit32)(pPort->portId & PORTID_MASK) | phyId);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaRegDevCmd_t, dTypeLRateAwtHa), sDTypeRate);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaRegDevCmd_t, ITNexusTimeOut), (agDeviceInfo->it_NexusTimeout));

    smTrace(hpDBG_VERY_LOUD,"QT",(bit32)(pPort->portId & PORTID_MASK) | phyId);
    /* TP:QT phyIdportId */
    /* no conversion is needed since SAS address is in BE format */
    payload.sasAddrHi = *(bit32*)agDeviceInfo->sasAddressHi;
    payload.sasAddrLo = *(bit32*)agDeviceInfo->sasAddressLo;

    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaRegDevCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaRegDevCmd_t, DeviceId), ((bit32)hostAssignedDeviceId) << 16);

    if(smIS_SPC(agRoot))
    {
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SPC_REG_DEV, IOMB_SIZE64, queueNum);
    }
    else
    {
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_REG_DEV, IOMB_SIZE64, queueNum);
    }

    if (AGSA_RC_SUCCESS != ret)
    {
      /* return the request to free pool */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saRegisterNewDevice: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saRegisterNewDevice, sending IOMB failed\n" ));
    }
    SA_DBG3(("saRegisterNewDevice: end\n"));

    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "zd");
    return ret;
}

/******************************************************************************/
/*! \brief Register a callback for a specific event
 *
 *  Register a callback for a Event API
 *
 *  \param agRoot          Handles for this instance of SAS/SATA hardware
 *  \param eventSourceType Event Type
 *  \param callbackPtr     Function pointer to OS layer
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *          - \e AGSA_RC_FAILURE
 *
 */
/*******************************************************************************/
GLOBAL bit32 saRegisterEventCallback(
                        agsaRoot_t                *agRoot,
                        bit32                     eventSourceType,
                        ossaGenericCB_t           callbackPtr
                        )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32               ret = AGSA_RC_FAILURE;

  SA_DBG3(("saRegisterEventCallback: start\n"));
  switch (eventSourceType)
  {
    case OSSA_EVENT_SOURCE_DEVICE_HANDLE_ADDED:
      saRoot->DeviceRegistrationCB =  (ossaDeviceRegistrationCB_t)callbackPtr;
      ret = AGSA_RC_SUCCESS;
      break;
    case OSSA_EVENT_SOURCE_DEVICE_HANDLE_REMOVED:
      saRoot->DeviceDeregistrationCB = (ossaDeregisterDeviceHandleCB_t) callbackPtr;
      ret = AGSA_RC_SUCCESS;
      break;
    default:
      SA_DBG1(("saRegisterEventCallback: not allowed case %d\n", eventSourceType));
      ret = AGSA_RC_FAILURE;
      break;
  }
  return ret;
}

/******************************************************************************/
/*! \brief Get Device Information
 *
 *  Get SAS/SATA device information API
 *
 *  \param agRoot          Handles for this instance of SAS/SATA hardware
 *  \param option          device general information or extended information
 *  \param agDevHandle     Pointer of device handle
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *          - \e AGSA_RC_FAILURE
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGetDeviceInfo(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     option,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle
                        )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t    *pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
  bit32               deviceid;
  bit32               ret = AGSA_RC_FAILURE;

  OS_ASSERT(pDevice != agNULL, "saGetDeviceInfo is NULL");
  smTraceFuncEnter(hpDBG_VERY_LOUD,"ze");

  if (pDevice == agNULL)
  {
    SA_DBG1(("saGetDeviceInfo: pDevice is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "ze");
    return AGSA_RC_FAILURE;
  }

  /* Get deviceid */
  deviceid = pDevice->DeviceMapIndex;
  SA_DBG3(("saGetDeviceInfo: start pDevice %p, deviceId %d\n", pDevice, deviceid));

  /* verify the agDeviceHandle with the one in the deviceMap */
  if ((deviceid != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceIdFromFW) ||
     (pDevice != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle))
  {
    SA_DBG1(("saGetDeviceInfo: Not match failure or device not exist\n"));
    ret = AGSA_RC_FAILURE;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "ze");
    return ret;
  }

  /* send IOMB to the SPC */
  ret = mpiGetDeviceInfoCmd(agRoot, agContext, deviceid, option, queueNum);

  SA_DBG3(("saGetDeviceInfo: end\n"));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "ze");
  return ret;
}

/******************************************************************************/
/*! \brief Set Device Information
 *
 *  Set SAS/SATA device information API
 *
 *  \param agRoot          Handles for this instance of SAS/SATA hardware
 *  \param agContext       Pointer to this API context
 *  \param queueNum        IQ/OQ number
 *  \param agDevHandle     Pointer of device handle
 *  \param option          device general information or extended information
 *  \param param           Parameter of Set Device Infomation
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *          - \e AGSA_RC_FAILURE
 *
 */
/*******************************************************************************/
GLOBAL bit32 saSetDeviceInfo(
                        agsaRoot_t            *agRoot,
                        agsaContext_t         *agContext,
                        bit32                 queueNum,
                        agsaDevHandle_t       *agDevHandle,
                        bit32                  option,
                        bit32                  param,
                        ossaSetDeviceInfoCB_t  agCB
                        )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t    *pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
  bit32               deviceid;
  bit32               ret = AGSA_RC_FAILURE;

  OS_ASSERT(pDevice != agNULL, "saSetDeviceInfo is NULL");
  smTraceFuncEnter(hpDBG_VERY_LOUD,"zf");

  SA_DBG2(("saSetDeviceInfo: start pDevice %p, option=0x%x param=0x0%x\n", pDevice, option, param));
  if(agNULL ==  pDevice )
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zf");
    return ret;
  }


  /* Get deviceid */
  deviceid = pDevice->DeviceMapIndex;
  pDevice->option = option;
  pDevice->param = param;

  SA_DBG3(("saSetDeviceInfo: deviceId %d\n", deviceid));

  /* verify the agDeviceHandle with the one in the deviceMap */
  if ((deviceid != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceIdFromFW) ||
     (pDevice != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle))
  {
    SA_DBG1(("saSetDeviceInfo: Not match failure or device not exist\n"));
    ret = AGSA_RC_FAILURE;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "zf");
    return ret;
  }

  /* send IOMB to the SPC */
  ret = mpiSetDeviceInfoCmd(agRoot, agContext, deviceid, option, queueNum, param, agCB);

  SA_DBG3(("saSetDeviceInfo: end\n"));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "zf");
  return ret;
}

/******************************************************************************/
/*! \brief Get Device State
 *
 *  Get SAS/SATA device state API
 *
 *  \param agRoot          Handles for this instance of SAS/SATA hardware
 *  \param agContext       Pointer to this API context
 *  \param queueNum        IQ/OQ number
 *  \param agDevHandle     Pointer of device handler
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *          - \e AGSA_RC_FAILURE
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGetDeviceState(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaDevHandle_t           *agDevHandle
                        )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t    *pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
  bit32               deviceid;
  bit32               ret = AGSA_RC_FAILURE;

  OS_ASSERT(pDevice != agNULL, "saGetDeviceState is NULL");
  smTraceFuncEnter(hpDBG_VERY_LOUD,"zg");

  if (pDevice == agNULL)
  {
    SA_DBG1(("saGetDeviceState: pDevice is NULL \n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zg");
    return AGSA_RC_FAILURE;
  }

  SA_DBG3(("saGetDeviceState: start pDevice %p\n", pDevice));

  /* Get deviceid */
  deviceid = pDevice->DeviceMapIndex;

  /* verify the agDeviceHandle with the one in the deviceMap */
  if ((deviceid != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceIdFromFW) ||
     (pDevice != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle))
  {
    SA_DBG1(("saGetDeviceState: Not match failure or device not exist\n"));
    ret = AGSA_RC_FAILURE;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "zg");
    return ret;
  }

  /* send IOMB to the SPC */
  ret = mpiGetDeviceStateCmd(agRoot, agContext, deviceid, queueNum);

  SA_DBG3(("saGetDeviceState: end\n"));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "zg");
  return ret;
}

/******************************************************************************/
/*! \brief Set Device State
 *
 *  Set SAS/SATA device state API
 *
 *  \param agRoot          Handles for this instance of SAS/SATA hardware
 *  \param agContext       Pointer to this API context
 *  \param queueNum        IQ/OQ number
 *  \param agDevHandle     Pointer of device handler
 *  \param newDeviceState  new device state
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *          - \e AGSA_RC_FAILURE
 *
 */
/*******************************************************************************/
GLOBAL bit32 saSetDeviceState(
                        agsaRoot_t      *agRoot,
                        agsaContext_t   *agContext,
                        bit32           queueNum,
                        agsaDevHandle_t *agDevHandle,
                        bit32            newDeviceState
                        )
{
  agsaLLRoot_t        *saRoot;
  agsaDeviceDesc_t    *pDevice;
  bit32               deviceid;
  bit32               ret = AGSA_RC_FAILURE;

  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  OS_ASSERT(saRoot != agNULL, "saSetDeviceState saRoot");

  if(saRoot == agNULL )
  {
    SA_DBG1(("saSetDeviceState: saRoot is NULL\n"));
    return ret;
  }

  OS_ASSERT(agDevHandle != agNULL, "saSetDeviceState agDevHandle  is NULL");

  smTraceFuncEnter(hpDBG_VERY_LOUD,"zh");

  if(agDevHandle == agNULL )
  {
    SA_DBG1(("saSetDeviceState: agDevHandle is NULL\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zh");
    return ret;
  }

  pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);

  OS_ASSERT(pDevice != agNULL, "saSetDeviceState pDevice is NULL");

  SA_DBG3(("saSetDeviceState: start pDevice %p\n", pDevice));

  if(pDevice == agNULL )
  {
    SA_DBG1(("saSetDeviceState: pDevice is NULL\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "zh");
    return ret;
  }
  /* Get deviceid */
  deviceid = pDevice->DeviceMapIndex;

  /* verify the agDeviceHandle with the one in the deviceMap */
  if ((deviceid != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceIdFromFW) ||
     (pDevice != saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle))
  {
    SA_DBG1(("saSetDeviceState: Not match failure or device not exist\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "zh");
    return ret;
  }

  /* send IOMB to the SPC */
  ret = mpiSetDeviceStateCmd(agRoot, agContext, deviceid, newDeviceState, queueNum);

  SA_DBG3(("saSetDeviceState: end\n"));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "zh");
  return ret;
}
