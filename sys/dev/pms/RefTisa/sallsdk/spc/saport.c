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
/*! \file saport.c
 *  \brief The file implements the functions to handle port
 *
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
#define siTraceFileID 'L'
#endif


extern bit32 gFPGA_TEST;
/******************************************************************************/
/*! \brief Add a SAS device to the discovery list of the port
 *
 *  Add a SAS device from the discovery list of the port
 *
 *  \param agRoot handles for this instance of SAS/SATA LLL
 *  \param pPort
 *  \param sasIdentify
 *  \param sasInitiator
 *  \param smpTimeout
 *  \param itNexusTimeout
 *  \param firstBurstSize
 *  \param dTypeSRate -- device type and link rate
 *  \param flag
 *
 *  \return -the device descriptor-
 */
/*******************************************************************************/
GLOBAL agsaDeviceDesc_t *siPortSASDeviceAdd(
  agsaRoot_t        *agRoot,
  agsaPort_t        *pPort,
  agsaSASIdentify_t sasIdentify,
  bit32             sasInitiator,
  bit32             smpTimeout,
  bit32             itNexusTimeout,
  bit32             firstBurstSize,
  bit8              dTypeSRate,
  bit32             flag
  )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaDeviceDesc_t      *pDevice;

  SA_DBG3(("siPortSASDeviceAdd: start\n"));

  smTraceFuncEnter(hpDBG_VERY_LOUD, "23");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != pPort), "");

  /* Acquire Device Lock */
  ossaSingleThreadedEnter(agRoot, LL_DEVICE_LOCK);

  /* Try to Allocate from device list */
  pDevice = (agsaDeviceDesc_t *) saLlistGetHead(&(saRoot->freeDevicesList));

  /* If device handle available */
  if ( agNULL != pDevice)
  {
    int i;

    /* Remove from free device list */
    saLlistRemove(&(saRoot->freeDevicesList), &(pDevice->linkNode));

    /* Initialize device descriptor */
    if ( agTRUE == sasInitiator )
    {
      pDevice->initiatorDevHandle.sdkData = pDevice;
      pDevice->targetDevHandle.sdkData = agNULL;
    }
    else
    {
      pDevice->initiatorDevHandle.sdkData = agNULL;
      pDevice->targetDevHandle.sdkData = pDevice;
    }

    pDevice->initiatorDevHandle.osData = agNULL;
    pDevice->targetDevHandle.osData = agNULL;

    /* setup device type */
    pDevice->deviceType = (bit8)((dTypeSRate & 0x30) >> SHIFT4);
    SA_DBG3(("siPortSASDeviceAdd: Device Type 0x%x, Port Context %p\n", pDevice->deviceType, pPort));
    pDevice->pPort = pPort;
    saLlistInitialize(&(pDevice->pendingIORequests));

    /* setup sasDeviceInfo */
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.smpTimeout = (bit16)smpTimeout;
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.it_NexusTimeout = (bit16)itNexusTimeout;
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.firstBurstSize = (bit16)firstBurstSize;
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate = dTypeSRate;
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.flag = flag;
    for (i = 0; i < 4; i++)
    {
      pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[i] = sasIdentify.sasAddressHi[i];
      pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[i] = sasIdentify.sasAddressLo[i];
    }
    pDevice->devInfo.sasDeviceInfo.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
    pDevice->devInfo.sasDeviceInfo.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;
    pDevice->devInfo.sasDeviceInfo.phyIdentifier = sasIdentify.phyIdentifier;

    /* Add to discoverd device for the port */
    saLlistAdd(&(pPort->listSASATADevices), &(pDevice->linkNode));

    /* Release Device Lock */
    ossaSingleThreadedLeave(agRoot, LL_DEVICE_LOCK);

    /* Log Messages */
    SA_DBG3(("siPortSASDeviceAdd: sasIdentify addrHI 0x%x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify)));
    SA_DBG3(("siPortSASDeviceAdd: sasIdentify addrLO 0x%x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify)));

  }
  else
  {
    /* Release Device Lock */
    ossaSingleThreadedLeave(agRoot, LL_DEVICE_LOCK);
    SA_ASSERT((agNULL != pDevice), "");
    SA_DBG1(("siPortSASDeviceAdd: device allocation failed\n"));
  }
  SA_DBG3(("siPortSASDeviceAdd: end\n"));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "23");
  return pDevice;
}

/******************************************************************************/
/*! \brief The function to remove a device descriptor
 *
 *  The function to remove a device descriptor
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param pPort  The pointer to the port
 *  \param pDevice The pointer to the device
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siPortDeviceRemove(
  agsaRoot_t        *agRoot,
  agsaPort_t        *pPort,
  agsaDeviceDesc_t  *pDevice,
  bit32             unmap
  )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  bit32        deviceIdx;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "24");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != pPort), "");
  SA_ASSERT((agNULL != pDevice), "");
  SA_ASSERT((SAS_SATA_UNKNOWN_DEVICE != pDevice->deviceType), "");

  /* remove the device from discovered list */
  SA_DBG3(("siPortDeviceRemove(SAS/SATA): DeviceIndex %d Device Context %p\n", pDevice->DeviceMapIndex, pDevice));

  ossaSingleThreadedEnter(agRoot, LL_DEVICE_LOCK);
  saLlistRemove(&(pPort->listSASATADevices), &(pDevice->linkNode));

  /* Reset the device data structure */
  pDevice->pPort = agNULL;
  pDevice->initiatorDevHandle.osData = agNULL;
  pDevice->initiatorDevHandle.sdkData = agNULL;
  pDevice->targetDevHandle.osData = agNULL;
  pDevice->targetDevHandle.sdkData = agNULL;

  saLlistAdd(&(saRoot->freeDevicesList), &(pDevice->linkNode));

  if(unmap)
  {
    /* remove the DeviceMap and MapIndex */
    deviceIdx = pDevice->DeviceMapIndex & DEVICE_ID_BITS;
    OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");

    saRoot->DeviceMap[deviceIdx].DeviceIdFromFW = 0;
    saRoot->DeviceMap[deviceIdx].DeviceHandle = agNULL;
    pDevice->DeviceMapIndex = 0;
  }
  ossaSingleThreadedLeave(agRoot, LL_DEVICE_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "24");

  return;
}

/******************************************************************************/
/*! \brief Add a SATA device to the discovery list of the port
 *
 *  Add a SATA device from the discovery list of the port
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param pPort
 *  \param pSTPBridge
 *  \param pSignature
 *  \param pm
 *  \param pmField
 *  \param smpReqTimeout
 *  \param itNexusTimeout
 *  \param firstBurstSize
 *  \param dTypeSRate
 *
 *  \return -the device descriptor-
 */
/*******************************************************************************/
GLOBAL agsaDeviceDesc_t *siPortSATADeviceAdd(
  agsaRoot_t              *agRoot,
  agsaPort_t              *pPort,
  agsaDeviceDesc_t        *pSTPBridge,
  bit8                    *pSignature,
  bit8                    pm,
  bit8                    pmField,
  bit32                   smpReqTimeout,
  bit32                   itNexusTimeout,
  bit32                   firstBurstSize,
  bit8                    dTypeSRate,
  bit32                   flag
  )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaDeviceDesc_t      *pDevice;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "25");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != pPort), "");

  /* Acquire Device Lock */
  ossaSingleThreadedEnter(agRoot, LL_DEVICE_LOCK);

  /* Try to Allocate from device list */
  pDevice = (agsaDeviceDesc_t *) saLlistGetHead(&(saRoot->freeDevicesList));

  /* If device handle available */
  if ( agNULL != pDevice)
  {
    int i;

    /* Remove from free device list */
    saLlistRemove(&(saRoot->freeDevicesList), &(pDevice->linkNode));

    /* Initialize the device descriptor */
    pDevice->initiatorDevHandle.sdkData = agNULL;
    pDevice->targetDevHandle.sdkData = pDevice;
    pDevice->initiatorDevHandle.osData = agNULL;
    pDevice->targetDevHandle.osData = agNULL;

    pDevice->deviceType = (bit8)((dTypeSRate & 0x30) >> SHIFT4);
    SA_DBG3(("siPortSATADeviceAdd: DeviceType 0x%x Port Context %p\n", pDevice->deviceType, pPort));

    /* setup device common infomation */
    pDevice->devInfo.sataDeviceInfo.commonDevInfo.smpTimeout = (bit16)smpReqTimeout;
    pDevice->devInfo.sataDeviceInfo.commonDevInfo.it_NexusTimeout = (bit16)itNexusTimeout;
    pDevice->devInfo.sataDeviceInfo.commonDevInfo.firstBurstSize = (bit16)firstBurstSize;
    pDevice->devInfo.sataDeviceInfo.commonDevInfo.devType_S_Rate = dTypeSRate;
    pDevice->devInfo.sataDeviceInfo.commonDevInfo.flag = flag;
    for (i = 0; i < 4; i++)
    {
      pDevice->devInfo.sataDeviceInfo.commonDevInfo.sasAddressHi[i] = 0;
      pDevice->devInfo.sataDeviceInfo.commonDevInfo.sasAddressLo[i] = 0;
    }
    /* setup SATA device information */
    pDevice->devInfo.sataDeviceInfo.connection = pm;
    pDevice->devInfo.sataDeviceInfo.portMultiplierField = pmField;
    pDevice->devInfo.sataDeviceInfo.stpPhyIdentifier = 0;
    pDevice->pPort = pPort;

    /* Add to discoverd device for the port */
    saLlistAdd(&(pPort->listSASATADevices), &(pDevice->linkNode));

    /* Release Device Lock */
    ossaSingleThreadedLeave(agRoot, LL_DEVICE_LOCK);
  }
  else
  {
    /* Release Device Lock */
    ossaSingleThreadedLeave(agRoot, LL_DEVICE_LOCK);
    SA_ASSERT((agNULL != pDevice), "");
    SA_DBG1(("siPortSATADeviceAdd: device allocation failed\n"));
  }
  SA_DBG3(("siPortSATADeviceAdd: end\n"));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "25");
  return pDevice;
}

/******************************************************************************/
/*! \brief Invalid a port
 *
 *  Invalid a port
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param pPort
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siPortInvalid(
  agsaRoot_t  *agRoot,
  agsaPort_t  *pPort
  )
{
  agsaLLRoot_t    *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  smTraceFuncEnter(hpDBG_VERY_LOUD, "26");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != pPort), "");

  /* set port's status to invalidating */
  pPort->status |= PORT_INVALIDATING;

  /* Remove from validPort and add the port back to the free port link list */
  ossaSingleThreadedEnter(agRoot, LL_PORT_LOCK);
  saLlistRemove(&(saRoot->validPorts), &(pPort->linkNode));
  saLlistAdd(&(saRoot->freePorts), &(pPort->linkNode));
  pPort->tobedeleted = agFALSE;
  ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "26");

  /* return */
}

/******************************************************************************/
/*! \brief The function to remove a device descriptor
 *
 *  The function to remove a device descriptor
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param pPort  The pointer to the port
 *  \param pDevice The pointer to the device
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siPortDeviceListRemove(
  agsaRoot_t        *agRoot,
  agsaPort_t        *pPort,
  agsaDeviceDesc_t  *pDevice
  )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);

  smTraceFuncEnter(hpDBG_VERY_LOUD, "27");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != pPort), "");
  SA_ASSERT((agNULL != pDevice), "");
  SA_ASSERT((SAS_SATA_UNKNOWN_DEVICE != pDevice->deviceType), "");

  /* remove the device from discovered list */
  SA_DBG3(("siPortDeviceListRemove(SAS/SATA): PortID %d Device Context %p\n", pPort->portId, pDevice));

  ossaSingleThreadedEnter(agRoot, LL_DEVICE_LOCK);
  saLlistRemove(&(pPort->listSASATADevices), &(pDevice->linkNode));

  /* Reset the device data structure */
  pDevice->pPort = agNULL;
  pDevice->initiatorDevHandle.osData = agNULL;
  pDevice->initiatorDevHandle.sdkData = agNULL;
  pDevice->targetDevHandle.osData = agNULL;
  pDevice->targetDevHandle.sdkData = agNULL;

  saLlistAdd(&(saRoot->freeDevicesList), &(pDevice->linkNode));
  ossaSingleThreadedLeave(agRoot, LL_DEVICE_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "27");
  return;
}

/******************************************************************************/
/*! \brief Initiate a Port COntrol IOMB command
 *
 *  This function is called to initiate a Port COntrol command to the SPC.
 *  The completion of this function is reported in ossaPortControlCB().
 *
 *  \param agRoot        handles for this instance of SAS/SATA hardware
 *  \param agContext     the context of this API
 *  \param queueNum      queue number
 *  \param agPortContext point to the event source structure
 *  \param param0        parameter 0
 *  \param param1        parameter 1
 *
 *  \return - successful or failure
 */
/*******************************************************************************/
GLOBAL bit32 saPortControl(
  agsaRoot_t            *agRoot,
  agsaContext_t         *agContext,
  bit32                 queueNum,
  agsaPortContext_t     *agPortContext,
  bit32                 portOperation,
  bit32                 param0,
  bit32                 param1
  )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t  *pRequest;
  agsaPort_t           *pPort;
  bit32                ret = AGSA_RC_SUCCESS;
  bit32                opportId;
  agsaPortControlCmd_t payload;
  bit32               using_reserved = agFALSE;


  /* sanity check */
  SA_ASSERT((agNULL !=saRoot ), "");
  SA_ASSERT((agNULL != agPortContext), "");
  if(saRoot == agNULL)
  {
    SA_DBG1(("saPortControl: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  smTraceFuncEnter(hpDBG_VERY_LOUD, "28");

  SA_DBG1(("saPortControl: portContext %p portOperation 0x%x param0 0x%x param1 0x%x\n", agPortContext, portOperation, param0, param1));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/
  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));
    /* If no LL Control request entry available */
    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG2(("saPortControl, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saPortControl, No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "28");
      return AGSA_RC_BUSY;
    }
  }

  /* If LL Control request entry avaliable */
  if( using_reserved )
  {
    saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  SA_ASSERT((!pRequest->valid), "The pRequest is in use");
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* build IOMB command and send to SPC */
  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(agsaPortControlCmd_t));

  /* find port id */
  pPort = (agsaPort_t *) (agPortContext->sdkData);
  opportId = (pPort->portId & PORTID_MASK) | (portOperation << SHIFT8);
  /* set tag */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPortControlCmd_t, tag), pRequest->HTag);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPortControlCmd_t, portOPPortId), opportId);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPortControlCmd_t, Param0), param0);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPortControlCmd_t, Param1), param1);

  SA_DBG1(("saPortControl: portId 0x%x portOperation 0x%x\n", (pPort->portId & PORTID_MASK),portOperation));

  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_PORT_CONTROL, IOMB_SIZE64, queueNum);
  if (AGSA_RC_SUCCESS != ret)
  {
    /* remove the request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    if (saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("saPortControl: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saPortControl, sending IOMB failed\n" ));
  }
  else
  {
    if (portOperation == AGSA_PORT_HARD_RESET)
    {
      SA_DBG1(("saPortControl,0x%x AGSA_PORT_HARD_RESET 0x%x param0 0x%x\n",
                pPort->portId, param0, param0 & AUTO_HARD_RESET_DEREG_FLAG));
      saRoot->autoDeregDeviceflag[pPort->portId & PORTID_MASK] = param0 & AUTO_HARD_RESET_DEREG_FLAG;
    }
    else if (portOperation == AGSA_PORT_CLEAN_UP)
    {
      SA_DBG1(("saPortControl, 0x%x AGSA_PORT_CLEAN_UP param0 0x%x %d\n", pPort->portId, param0,((param0 & AUTO_FW_CLEANUP_DEREG_FLAG) ? 0:1)));
      saRoot->autoDeregDeviceflag[pPort->portId & PORTID_MASK] = ((param0 & AUTO_FW_CLEANUP_DEREG_FLAG) ? 0:1);
    }
    SA_DBG1(("saPortControl, sending IOMB SUCCESS, portId 0x%x autoDeregDeviceflag=0x%x\n", pPort->portId,saRoot->autoDeregDeviceflag[pPort->portId & PORTID_MASK]));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "28");

  return ret;
}

/**
 * saEncryptGetMode()
 *
 *     Returns the status, working state and sector size
 *     registers of the encryption engine
 *
 * @param saRoot
 * @param encryptInfo
 *
 * @return
 */
GLOBAL bit32 saEncryptGetMode(agsaRoot_t        *agRoot,
                              agsaContext_t     *agContext,
                              agsaEncryptInfo_t *encryptInfo)
{
    bit32 ret = AGSA_RC_NOT_SUPPORTED;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"29");
    agContext = agContext; /* Lint*/
    SA_DBG4(("saEncryptGetMode, encryptInfo %p\n",encryptInfo ));
    if(smIS_SPCV(agRoot))
    {
      bit32 ScratchPad1 =0;
      bit32 ScratchPad3 =0;

      encryptInfo->status = 0;
      encryptInfo->encryptionCipherMode = 0;
      encryptInfo->encryptionSecurityMode = 0;
      encryptInfo->flag = 0;

      ScratchPad1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register);
      ScratchPad3 = ossaHwRegRead(agRoot,V_Scratchpad_3_Register);
      if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) ==  SCRATCH_PAD1_V_RAAE_MASK)
      {
        if((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK) == SCRATCH_PAD3_V_ENC_READY ) /* 3 */
        {
          if( ScratchPad3 & SCRATCH_PAD3_V_XTS_ENABLED)
          {
            encryptInfo->encryptionCipherMode = agsaEncryptCipherModeXTS;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMF_ENABLED )
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMF;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMA_ENABLED)
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMA;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMB_ENABLED )
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMB;
          }
          encryptInfo->status = AGSA_RC_SUCCESS;
          ret = AGSA_RC_SUCCESS;
        }
        else if((ScratchPad3 & SCRATCH_PAD3_V_ENC_READY) == SCRATCH_PAD3_V_ENC_DISABLED) /* 0 */
        {
          SA_DBG1(("saEncryptGetMode, SCRATCH_PAD3_V_ENC_DISABLED 1 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
          encryptInfo->status = 0xFFFF;
          encryptInfo->encryptionCipherMode = 0;
          encryptInfo->encryptionSecurityMode = 0;
          ret = AGSA_RC_NOT_SUPPORTED;
        }
        else if((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK ) == SCRATCH_PAD3_V_ENC_DIS_ERR) /* 1 */
        {
          SA_DBG1(("saEncryptGetMode, SCRATCH_PAD3_V_ENC_DIS_ERR 1 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
          encryptInfo->status = (ScratchPad3 & SCRATCH_PAD3_V_ERR_CODE ) >> SHIFT16;
          if( ScratchPad3 & SCRATCH_PAD3_V_XTS_ENABLED)
          {
            encryptInfo->encryptionCipherMode = agsaEncryptCipherModeXTS;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMF_ENABLED )
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMF;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMA_ENABLED)
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMA;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMB_ENABLED )
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMB;
          }
          ret = AGSA_RC_FAILURE;
        }
        else if((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK ) == SCRATCH_PAD3_V_ENC_ENA_ERR) /* 2 */
        {

          SA_DBG1(("saEncryptGetMode, SCRATCH_PAD3_V_ENC_ENA_ERR 1 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
          encryptInfo->status = (ScratchPad3 & SCRATCH_PAD3_V_ERR_CODE ) >> SHIFT16;
          if( ScratchPad3 & SCRATCH_PAD3_V_XTS_ENABLED)
          {
            encryptInfo->encryptionCipherMode = agsaEncryptCipherModeXTS;
            SA_DBG1(("saEncryptGetMode, SCRATCH_PAD3_V_ENC_ENA_ERR 2 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMF_ENABLED )
          {
            SA_DBG1(("saEncryptGetMode, SCRATCH_PAD3_V_ENC_ENA_ERR 3 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
            encryptInfo->encryptionSecurityMode = agsaEncryptSMF;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMA_ENABLED)
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMA;
          }
          if( (ScratchPad3 & SCRATCH_PAD3_V_SM_MASK ) == SCRATCH_PAD3_V_SMB_ENABLED )
          {
            encryptInfo->encryptionSecurityMode = agsaEncryptSMB;
          }

          SA_DBG1(("saEncryptGetMode,encryptInfo status 0x%08X CipherMode 0x%X SecurityMode 0x%X\n" ,
              encryptInfo->status,
              encryptInfo->encryptionCipherMode,
              encryptInfo->encryptionSecurityMode));

#ifdef CCFLAGS_SPCV_FPGA_REVB /*The FPGA platform hasn't EEPROM*/
          ret = AGSA_RC_SUCCESS;
#else
          ret = AGSA_RC_FAILURE;
#endif
        }
      }
      else  if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) ==  SCRATCH_PAD1_V_RAAE_ERR)
      {
        SA_DBG1(("saEncryptGetMode, SCRATCH_PAD1_V_RAAE_ERR 1 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
        ret = AGSA_RC_FAILURE;
      }
      else  if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) == 0x0 )
      {
        SA_DBG1(("saEncryptGetMode, RAAE not ready AGSA_RC_BUSY 1 0x%08X 3 0x%08X\n",ScratchPad1,ScratchPad3 ));
        ret = AGSA_RC_BUSY;
      }
      if(ScratchPad3 & SCRATCH_PAD3_V_AUT)
      {
        encryptInfo->flag |= OperatorAuthenticationEnable_AUT;
      }
      if(ScratchPad3 & SCRATCH_PAD3_V_ARF)
      {
        encryptInfo->flag |= ReturnToFactoryMode_ARF;
      }

      SA_DBG2(("saEncryptGetMode, encryptionCipherMode 0x%x encryptionSecurityMode 0x%x flag 0x%x status 0x%x\n",
                encryptInfo->encryptionCipherMode,
                encryptInfo->encryptionSecurityMode,
                encryptInfo->flag,
                encryptInfo->status));
      SA_DBG2(("saEncryptGetMode, ScratchPad3 0x%x returns 0x%x\n",ScratchPad3, ret));

    }
    else
    {
      SA_DBG1(("saEncryptGetMode, SPC AGSA_RC_NOT_SUPPORTED\n"));
    }

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "29");
    return ret;
}

/**/
GLOBAL bit32 saEncryptSetMode (
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             queueNum,
                      agsaEncryptInfo_t *mode
                      )

{
  bit32 ret = AGSA_RC_NOT_SUPPORTED;
  agsaSetControllerConfigCmd_t agControllerConfig;
  agsaSetControllerConfigCmd_t *pagControllerConfig = &agControllerConfig;
  bit32 smode = 0;

  if(smIS_SPCV(agRoot))
  {
    bit32 ScratchPad1 =0;

    ScratchPad1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register);
    if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) ==  SCRATCH_PAD1_V_RAAE_MASK)
    {
      si_memset(pagControllerConfig,0,sizeof(agsaSetControllerConfigCmd_t));

      SA_DBG2(("saEncryptSetMode, encryptionCipherMode 0x%x encryptionSecurityMode 0x%x status 0x%x\n",
                                          mode->encryptionCipherMode,
                                          mode->encryptionSecurityMode,
                                          mode->status
                                          ));

      smode = mode->encryptionSecurityMode;

      if( mode->encryptionCipherMode & agsaEncryptCipherModeXTS)
      {
        smode |= 1 << SHIFT22;
      }


      pagControllerConfig->pageCode = AGSA_ENCRYPTION_CONTROL_PARM_PAGE | smode;
      pagControllerConfig->tag =0;

      SA_DBG2(("saEncryptSetMode,tag 0x%x pageCode 0x%x\n",
                                          pagControllerConfig->tag,
                                          pagControllerConfig->pageCode
                                          ));

      SA_DBG2(("saEncryptSetMode, 0x%x 0x%x 0x%x 0x%x\n",
                                          pagControllerConfig->configPage[0],
                                          pagControllerConfig->configPage[1],
                                          pagControllerConfig->configPage[2],
                                          pagControllerConfig->configPage[3]
                                          ));

      SA_DBG2(("saEncryptSetMode, 0x%x 0x%x 0x%x 0x%x\n",
                                          pagControllerConfig->configPage[4],
                                          pagControllerConfig->configPage[5],
                                          pagControllerConfig->configPage[6],
                                          pagControllerConfig->configPage[7]
                                          ));

      SA_DBG2(("saEncryptSetMode, 0x%x 0x%x 0x%x 0x%x\n",
                                          pagControllerConfig->configPage[8],
                                          pagControllerConfig->configPage[9],
                                          pagControllerConfig->configPage[10],
                                          pagControllerConfig->configPage[11]
                                          ));

      ret = mpiSetControllerConfigCmd(agRoot,agContext,pagControllerConfig,queueNum,agTRUE);

      SA_DBG2(("saEncryptSetMode,  pageCode 0x%x tag 0x%x status 0x%x\n",
                                        pagControllerConfig->pageCode,
                                        pagControllerConfig->tag,
                                        ret
                                        ));
    }
    else
    {
      SA_DBG2(("saEncryptSetMode,ScratchPad1 not ready %08X\n",ScratchPad1 ));
      ret = AGSA_RC_BUSY;
    }

  }
  return ret;
}



/**
 * saEncryptKekUpdate()
 *
 *     Replace a KEK within the controller
 *
 * @param saRoot
 * @param flags
 * @param newKekIndex
 * @param wrapperKekIndex
 * @param encryptKekBlob
 *
 * @return
 */
GLOBAL bit32 saEncryptKekUpdate(
                    agsaRoot_t         *agRoot,
                    agsaContext_t      *agContext,
                    bit32              queueNum,
                    bit32              flags,
                    bit32              newKekIndex,
                    bit32              wrapperKekIndex,
                    bit32              blobFormat,
                    agsaEncryptKekBlob_t *encryptKekBlob
                    )
{
  agsaKekManagementCmd_t     payload;
  bit32 ret, i;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"30");

  SA_DBG2(("saEncryptKekUpdate, flags 0x%x newKekIndex 0x%x wrapperKekIndex 0x%x encryptKekBlob %p\n",flags,newKekIndex,wrapperKekIndex,encryptKekBlob));
  SA_DBG2(("saEncryptKekUpdate, 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n",
                                encryptKekBlob->kekBlob[0],encryptKekBlob->kekBlob[1],
                                encryptKekBlob->kekBlob[2],encryptKekBlob->kekBlob[3],
                                encryptKekBlob->kekBlob[4],encryptKekBlob->kekBlob[5],
                                encryptKekBlob->kekBlob[6],encryptKekBlob->kekBlob[7]));
  SA_DBG2(("saEncryptKekUpdate, 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n",
                                encryptKekBlob->kekBlob[ 8],encryptKekBlob->kekBlob[ 9],
                                encryptKekBlob->kekBlob[10],encryptKekBlob->kekBlob[11],
                                encryptKekBlob->kekBlob[12],encryptKekBlob->kekBlob[13],
                                encryptKekBlob->kekBlob[14],encryptKekBlob->kekBlob[15]));
  SA_DBG2(("saEncryptKekUpdate, 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n",
                                encryptKekBlob->kekBlob[16],encryptKekBlob->kekBlob[17],
                                encryptKekBlob->kekBlob[18],encryptKekBlob->kekBlob[19],
                                encryptKekBlob->kekBlob[20],encryptKekBlob->kekBlob[21],
                                encryptKekBlob->kekBlob[22],encryptKekBlob->kekBlob[23]));
  SA_DBG2(("saEncryptKekUpdate, 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n",
                                encryptKekBlob->kekBlob[24],encryptKekBlob->kekBlob[25],
                                encryptKekBlob->kekBlob[26],encryptKekBlob->kekBlob[27],
                                encryptKekBlob->kekBlob[28],encryptKekBlob->kekBlob[29],
                                encryptKekBlob->kekBlob[30],encryptKekBlob->kekBlob[31]));
  SA_DBG2(("saEncryptKekUpdate, 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n",
                                encryptKekBlob->kekBlob[32],encryptKekBlob->kekBlob[33],
                                encryptKekBlob->kekBlob[34],encryptKekBlob->kekBlob[35],
                                encryptKekBlob->kekBlob[36],encryptKekBlob->kekBlob[37],
                                encryptKekBlob->kekBlob[38],encryptKekBlob->kekBlob[39]));
  SA_DBG2(("saEncryptKekUpdate, 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n",
                                encryptKekBlob->kekBlob[40],encryptKekBlob->kekBlob[41],
                                encryptKekBlob->kekBlob[42],encryptKekBlob->kekBlob[43],
                                encryptKekBlob->kekBlob[44],encryptKekBlob->kekBlob[45],
                                encryptKekBlob->kekBlob[46],encryptKekBlob->kekBlob[47]));
  /* create payload for IOMB */
  si_memset(&payload, 0, sizeof(agsaKekManagementCmd_t));

  OSSA_WRITE_LE_32(agRoot,
                   &payload,
                   OSSA_OFFSET_OF(agsaKekManagementCmd_t, NEWKIDX_CURKIDX_KBF_Reserved_SKNV_KSOP),
                   (newKekIndex << SHIFT24) | (wrapperKekIndex << SHIFT16) | blobFormat << SHIFT14 | (flags << SHIFT8) | KEK_MGMT_SUBOP_UPDATE);
  for (i = 0; i < 12; i++)
  {

    OSSA_WRITE_LE_32(agRoot,
                    &payload,
                    OSSA_OFFSET_OF(agsaKekManagementCmd_t, kekBlob[i ]),
                    (bit32)*(bit32*)&encryptKekBlob->kekBlob[i * sizeof(bit32)] );
/**/
    }

  ret = mpiKekManagementCmd(agRoot, agContext, &payload, queueNum );

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "30");
  return ret;
}


#ifdef HIALEAH_ENCRYPTION

GLOBAL bit32 saEncryptHilUpdate(
                    agsaRoot_t         *agRoot,
                    agsaContext_t      *agContext,
                    bit32              queueNum
                    )
{
    agsaKekManagementCmd_t     payload;

    bit32 ScratchPad1 =0;
    bit32 ScratchPad3 =0;
    bit32 ret =0;

    ScratchPad1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register);
    ScratchPad3 = ossaHwRegRead(agRoot,V_Scratchpad_3_Register);


    smTraceFuncEnter(hpDBG_VERY_LOUD,"xxx");

    SA_DBG2(("saEncryptHilUpdate ScratchPad1 0x08%x ScratchPad3 0x08%x\n",ScratchPad1,ScratchPad3));
    /* create payload for IOMB */
    si_memset(&payload, 0, sizeof(agsaKekManagementCmd_t));

    OSSA_WRITE_LE_32(agRoot, 
                     &payload, 
                     OSSA_OFFSET_OF(agsaKekManagementCmd_t, NEWKIDX_CURKIDX_KBF_Reserved_SKNV_KSOP), 
                     (1 << SHIFT24) | (1 << SHIFT16) | (1 << SHIFT8) | KEK_MGMT_SUBOP_KEYCARDUPDATE);
/**/

    ret = mpiKekManagementCmd(agRoot, agContext, &payload, queueNum );

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xxx");
    return ret;
}
#endif /* HIALEAH_ENCRYPTION */

/**
 * saEncryptKekInvalidate()
 *
 *     Remove a KEK from the controller
 *
 * @param saRoot
 * @param flags
 * @param newKekIndex
 * @param wrapperKekIndex
 * @param encryptKekBlob
 *
 * @return
 */
GLOBAL bit32 saEncryptKekInvalidate(
                     agsaRoot_t        *agRoot,
                     agsaContext_t     *agContext,
                     bit32             queueNum,
                     bit32             kekIndex
                     )
{
    agsaKekManagementCmd_t     payload;
    bit32 ret;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"31");

    SA_DBG2(("saEncryptKekInvalidate, kekIndex 0x%x \n",kekIndex));


    /* create payload for IOMB */
    si_memset(&payload, 0, sizeof(agsaDekManagementCmd_t));

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaKekManagementCmd_t, NEWKIDX_CURKIDX_KBF_Reserved_SKNV_KSOP),
                     kekIndex << SHIFT16 | KEK_MGMT_SUBOP_INVALIDATE);

    ret = mpiKekManagementCmd(agRoot, agContext, &payload,  queueNum );

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "31");
    return ret;
}

/**
 * saEncryptDekCacheUpdate()
 *
 *     Replace a DEK within the controller cache
 *
 * @param saRoot
 * @param kekIndex
 * @param dekTableSelect
 * @param dekAddrHi
 * @param dekAddrLo
 * @param dekIndex
 * @param dekNumberOfEntries
 *
 * @return
 */
GLOBAL bit32 saEncryptDekCacheUpdate(
                     agsaRoot_t        *agRoot,
                     agsaContext_t     *agContext,
                     bit32             queueNum,
                     bit32             kekIndex,
                     bit32             dekTableSelect,
                     bit32             dekAddrHi,
                     bit32             dekAddrLo,
                     bit32             dekIndex,
                     bit32             dekNumberOfEntries,
                     bit32             dekBlobFormat,
                     bit32             dekTableKeyEntrySize
                     )
{
    agsaDekManagementCmd_t    payload;
    bit32 ret;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"32");

    SA_DBG2(("saEncryptDekCacheUpdate, kekIndex 0x%x dekTableSelect 0x%x dekAddrHi 0x%x dekAddrLo 0x%x\n",
                     kekIndex,
                     dekTableSelect,
                     dekAddrHi,
                     dekAddrLo ));
    SA_DBG2(("saEncryptDekCacheUpdate, dekIndex 0x%x dekNumberOfEntries 0x%x dekBlobFormat 0x%x dekTableKeyEntrySize 0x%x\n",
                     dekIndex,
                     dekNumberOfEntries,
                     dekBlobFormat,
                     dekTableKeyEntrySize));

    /* create payload for IOMB */
    si_memset(&payload, 0, sizeof(agsaDekManagementCmd_t));

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, KEKIDX_Reserved_TBLS_DSOP),
                     (kekIndex << SHIFT24) | (dekTableSelect << SHIFT8) | DEK_MGMT_SUBOP_UPDATE);

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, dekIndex),
                     dekIndex);

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, tableAddrLo),
                     dekAddrLo);

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, tableAddrHi),
                     dekAddrHi);

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, tableEntries),
                     dekNumberOfEntries);

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, Reserved_DBF_TBL_SIZE),
                     dekBlobFormat << SHIFT8 | dekTableKeyEntrySize );

    ret = mpiDekManagementCmd(agRoot, agContext, &payload, queueNum);

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "32");
    return ret;
}

/**
 * saEncryptDekCacheInvalidate()
 *
 *     Remove a DEK from the controller cache
 *
 * @param saRoot
 * @param kekIndex
 * @param dekTable
 * @param dekAddrHi
 * @param dekAddrLo
 * @param dekIndex
 * @param dekNumberOfEntries
 *
 * @return
 */
GLOBAL bit32 saEncryptDekCacheInvalidate(
                    agsaRoot_t         *agRoot,
                    agsaContext_t      *agContext,
                    bit32              queueNum,
                    bit32              dekTable,
                    bit32              dekIndex
                    )
{
    agsaDekManagementCmd_t     payload;
    bit32 ret;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"33");

    SA_DBG2(("saEncryptDekCacheInvalidate,dekTable  0x%x dekIndex 0x%x\n",dekTable,dekIndex));

    /* create payload for IOMB */
    si_memset(&payload, 0, sizeof(agsaDekManagementCmd_t));

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, KEKIDX_Reserved_TBLS_DSOP),
                     (dekTable << SHIFT8) | DEK_MGMT_SUBOP_INVALIDATE);

    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, dekIndex),
                     dekIndex);

    /* Assume all DEKs are 80 bytes*/
    OSSA_WRITE_LE_32(agRoot,
                     &payload,
                     OSSA_OFFSET_OF(agsaDekManagementCmd_t, Reserved_DBF_TBL_SIZE),
                     4);

    ret = mpiDekManagementCmd(agRoot, agContext, &payload, queueNum);

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "33");
    return ret;
}

/**
 * saDIFEncryptionOffloadStart()
 *
 *     initiate the SPCv controller offload function 
 *
 * @param saRoot
 * @param agContext
 * @param queueNum
 * @param op
 * @param agsaDifEncPayload
 * @param agCB
 *
 * @return
 */
GLOBAL bit32 saDIFEncryptionOffloadStart(
                          agsaRoot_t         *agRoot,
                          agsaContext_t      *agContext,
                          bit32               queueNum,
                          bit32               op,
                          agsaDifEncPayload_t *agsaDifEncPayload,
                          ossaDIFEncryptionOffloadStartCB_t agCB)
{
  bit32 ret = AGSA_RC_FAILURE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3I");
  SA_DBG1(("saDIFEncryptionOffloadStart: start op=%d, agsaDifEncPayload=%p\n", op, agsaDifEncPayload));

  if(smIS_SPCV(agRoot))
  {
    ret = mpiDIFEncryptionOffloadCmd(agRoot, agContext, queueNum, op, agsaDifEncPayload, agCB);
  }
  else
  {
    SA_DBG1(("saDIFEncryptionOffloadStart: spcv only AGSA_RC_FAILURE \n"));
  }

  SA_DBG1(("saDIFEncryptionOffloadStart: end status 0x%x\n",ret));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3I");
  return ret;
}

/**
 * saSetControllerConfig()
 *
 *     Update a controller mode page
 *
 * @param saRoot
 * @param modePage
 * @param length
 * @param buffer
 * @param agContext
 *
 * @return
 */
GLOBAL bit32 saSetControllerConfig(
                      agsaRoot_t        *agRoot,
                      bit32             queueNum,
                      bit32             modePage,
                      bit32             length,
                      void              *buffer,
                      agsaContext_t     *agContext
                      )
{
    agsaSetControllerConfigCmd_t agControllerConfig;
    bit32 *src;
    bit32 i, ret;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"34");


    if(smIS_SPCV(agRoot))
    {

      SA_DBG2(("saSetControllerConfig: queueNum %d modePage 0x%x length %d\n",queueNum,modePage,length ));

      /* If the page is well known, validate the size of the buffer */
      if (((modePage == AGSA_INTERRUPT_CONFIGURATION_PAGE)   && (length != sizeof(agsaInterruptConfigPage_t )))    ||
           ((modePage == AGSA_ENCRYPTION_DEK_CONFIG_PAGE)    && (length != sizeof(agsaEncryptDekConfigPage_t)))     ||
           ((modePage == AGSA_ENCRYPTION_CONTROL_PARM_PAGE)  && (length != sizeof(agsaEncryptControlParamPage_t ))) ||
           ((modePage == AGSA_ENCRYPTION_HMAC_CONFIG_PAGE)   && (length != sizeof(agsaEncryptHMACConfigPage_t )))   ||
           ((modePage == AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE) && (length != sizeof(agsaSASProtocolTimerConfigurationPage_t )))  )
      {
        SA_DBG1(("saSetControllerConfig: AGSA_RC_FAILURE queueNum %d modePage 0x%x length %d\n",queueNum,modePage,length ));
        ret = AGSA_RC_FAILURE;
      }
      else if(modePage == AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE)
      {
        SA_DBG1(("saSetControllerConfig: Warning!!!!GENERAL_CONFIG_PAGE cannot be set\n"));
        ret = AGSA_RC_FAILURE;
      }
      else
      {
        /* Copy the raw mode page data into something that can be wrapped in an IOMB. */
        si_memset(&agControllerConfig, 0, sizeof(agsaSetControllerConfigCmd_t));

        agControllerConfig.tag = 0;  /*HTAG */

        src = (bit32 *) buffer;

        for (i = 0; i < (length / 4); i++)
        {
          OSSA_WRITE_LE_32(agRoot,
                           &agControllerConfig,
                           OSSA_OFFSET_OF(agsaSetControllerConfigCmd_t, pageCode) + (i * 4),
                           *src);

          src++;
        }
        ret = mpiSetControllerConfigCmd(agRoot, agContext, &agControllerConfig, queueNum,agFALSE);
        if(ret)
        {
          SA_DBG1(("saSetControllerConfig: AGSA_RC_FAILURE (sending) queueNum %d modePage 0x%x length %d\n",queueNum,modePage,length ));
        }

      }
    }
    else
    {
      SA_DBG1(("saSetControllerConfig: spcv only AGSA_RC_FAILURE queueNum %d modePage 0x%x length %d\n",queueNum,modePage,length ));
      ret = AGSA_RC_FAILURE;
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "34");
    return ret;
}


/**
 * saGetControllerConfig()
 *
 *     Retrieve the contents of a controller mode page
 *
 * @param saRoot
 * @param modePage
 * @param agContext
 *
 * @return
 */
GLOBAL bit32 saGetControllerConfig(
                      agsaRoot_t        *agRoot,
                      bit32             queueNum,
                      bit32             modePage,
                      bit32             flag0,
                      bit32             flag1,
                      agsaContext_t     *agContext
                      )
{
    bit32 ret;
    agsaGetControllerConfigCmd_t agControllerConfig;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"35");

    SA_DBG2(("saGetControllerConfig, modePage 0x%x  agContext %p flag0 0x%08x flag1 0x%08x\n",modePage,agContext, flag0, flag1 ));
    if(smIS_SPCV(agRoot))
    {
      si_memset(&agControllerConfig, 0, sizeof(agsaGetControllerConfigCmd_t));

      agControllerConfig.pageCode = modePage;
      if(modePage == AGSA_INTERRUPT_CONFIGURATION_PAGE)
      {
        agControllerConfig.INT_VEC_MSK0 = flag0;
        agControllerConfig.INT_VEC_MSK1 = flag1;
      }
      ret = mpiGetControllerConfigCmd(agRoot, agContext, &agControllerConfig, queueNum);
    }
    else
    {
      SA_DBG1(("saGetControllerConfig: spcv only AGSA_RC_FAILURE queueNum %d modePage 0x%x flag0 0x%08x flag1 0x%08x\n",queueNum,modePage, flag0, flag1 ));
      ret = AGSA_RC_FAILURE;
    }

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "35");
    return ret;
}

GLOBAL bit32 saEncryptSelftestExecute (
                        agsaRoot_t    *agRoot,
                        agsaContext_t *agContext,
                        bit32          queueNum,
                        bit32          type,
                        bit32          length,
                        void          *TestDescriptor)
{
  bit32 ret = AGSA_RC_SUCCESS;

  agsaEncryptBist_t bist;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2e");
  si_memset(&bist, 0, (sizeof(agsaEncryptBist_t)));

  SA_DBG1(("saEncryptSelftestExecute, enter\n" ));
  bist.r_subop = (type & 0xFF);

  si_memcpy(&bist.testDiscption,TestDescriptor,length );

  /* setup IOMB payload */
  ret = mpiEncryptBistCmd( agRoot, queueNum, agContext, &bist );

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2e");

  return (ret);
}
GLOBAL bit32 saOperatorManagement(
                        agsaRoot_t           *agRoot,
                        agsaContext_t        *agContext,
                        bit32                 queueNum,
                        bit32                 flag,
                        bit8                  role,
                        agsaID_t             *id,
                        agsaEncryptKekBlob_t *kblob)
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaOperatorMangmentCmd_t opmcmd;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2i");

  SA_DBG1(("saOperatorManagement, enter\n" ));

  si_memset(&opmcmd, 0, sizeof(agsaOperatorMangmentCmd_t));
  /*role = ((flag & SA_OPR_MGMNT_FLAG_MASK) >> SA_OPR_MGMNT_FLAG_SHIFT);*/

  flag = (flag & ~SA_OPR_MGMNT_FLAG_MASK);

  opmcmd.OPRIDX_AUTIDX_R_KBF_PKT_OMO = flag;

  opmcmd.IDString_Role[0] = (bit8)role;
  SA_DBG1(("saOperatorManagement, role 0x%X flags 0x%08X\n", role, opmcmd.OPRIDX_AUTIDX_R_KBF_PKT_OMO ));

  si_memcpy(&opmcmd.IDString_Role[1], id->ID, AGSA_ID_SIZE);
  si_memcpy(&opmcmd.Kblob, kblob, sizeof(agsaEncryptKekBlob_t));

  /* setup IOMB payload */
  ret = mpiOperatorManagementCmd(agRoot, queueNum, agContext, &opmcmd);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2i");

  return (ret);
}

/*
    The command is for an operator to login to/logout from SPCve.
    Only when all IOs are quiesced, can an operator logout.

    flag:
      Access type (ACS) [4 bits]
        0x1: login
        0x2: logout
        Others: reserved
      KEYopr pinned in the KEK RAM (PIN) [1 bit]
        0: not pinned, operator ID table will be searched during authentication.
        1: pinned, OPRIDX is referenced to unwrap the certificate.
      KEYopr Index in the KEK RAM (OPRIDX) [8 bits]
        If KEYopr is pinned in the KEK RAM, OPRIDX is to reference to the KEK for authentication

    cert
      Operator Certificate (CERT) [40 bytes]
  
    response calls ossaSetOperatorCB
*/

GLOBAL bit32
saSetOperator(
  agsaRoot_t     *agRoot,
  agsaContext_t  *agContext,
  bit32           queueNum,
  bit32           flag,
  void           *cert
  )
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaSetOperatorCmd_t  SetOperatorCmd;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3c");
  SA_DBG1(("saSetOperator, flag 0x%x cert %p\n",flag, cert));

  /* initialize set operator IOMB */
  si_memset(&SetOperatorCmd, 0, sizeof(agsaSetOperatorCmd_t));
  SetOperatorCmd.OPRIDX_PIN_ACS = flag;
  si_memcpy((bit8*)SetOperatorCmd.cert, (bit8*)cert, 40);

  /* setup IOMB payload */
  ret = mpiSetOperatorCmd(agRoot, queueNum, agContext, &SetOperatorCmd);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3c");
  return (ret);
}

/*
    The command is to get role and ID of either current or all operators from SPCve.
    Option
        0x1: current operator
        0x2: all operators
        Others: reserved

    OprBufAddr
        the host buffer address to store the role and ID of all operators. Valid only when option == 0x2.
        Buffer size must be 1KB to store max 32 operators's role and ID.
    response calls ossaGetOperatorCB
*/
GLOBAL bit32
saGetOperator(
  agsaRoot_t     *agRoot,
  agsaContext_t  *agContext,
  bit32           queueNum,
  bit32           option,
  bit32           AddrHi,
  bit32           AddrLo
  )
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaGetOperatorCmd_t  GetOperatorCmd;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3d");
  SA_DBG1(("saGetOperator, option 0x%x 0x%08x_%08x\n",option,AddrHi,AddrLo ));

  /* initialize get operator IOMB */
  si_memset(&GetOperatorCmd, 0, sizeof(agsaGetOperatorCmd_t));
  GetOperatorCmd.option = option;
  GetOperatorCmd.OprBufAddrLo = AddrLo;
  GetOperatorCmd.OprBufAddrHi = AddrHi;

  /* setup IOMB payload */
  ret = mpiGetOperatorCmd(agRoot, queueNum, agContext, &GetOperatorCmd);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3d");

  return (ret);
}

