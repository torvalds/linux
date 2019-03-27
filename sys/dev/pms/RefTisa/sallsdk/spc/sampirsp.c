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
/*! \file sampirsp.c
 *  \brief The file implements the functions of MPI Outbound Response Message
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
#define siTraceFileID 'J'
#endif

/******************************************************************************/
/* Protoytpes */
void saReturnRequestToFreePool(
                            agsaRoot_t          *agRoot,
                            agsaIORequestDesc_t *pRequest
                            );
							
/******************************************************************************/
/*! \brief Process Outbound IOMB Message
 *
 *  Process Outbound IOMB from SPC
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL Layer
 *  \param pMsg1        Pointer of Response IOMB message 1
 *  \param category     category of outbpond IOMB header
 *  \param opcode       Opcode of Outbound IOMB header
 *  \param bc           buffer count of IOMB header
 *
 *  \return success or fail
 *
 */
/*******************************************************************************/
#if 0
FORCEINLINE bit32
mpiParseOBIomb(
  agsaRoot_t        *agRoot,
  bit32             *pMsg1,
  mpiMsgCategory_t  category,
  bit16             opcode
  )
{
  agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32              ret = AGSA_RC_SUCCESS;
  bit32              parserStatus = AGSA_RC_SUCCESS;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "2f");

  switch (opcode)
  {
    case OPC_OUB_COMBINED_SSP_COMP:
    {
      agsaSSPCoalescedCompletionRsp_t  *pIomb = (agsaSSPCoalescedCompletionRsp_t *)pMsg1;
      agsaIORequestDesc_t              *pRequest = agNULL;
      bit32  tag     = 0;
      bit32  sspTag  = 0;
      bit32  count   = 0;

#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSSPCompleted++;
      SA_DBG3(("mpiParseOBIomb, SSP_COMP Response received IOMB=%p %d\n",
         pMsg1, saRoot->LLCounters.IOCounter.numSSPCompleted));
#else
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_COMBINED_SSP_COMP Response received IOMB=%p\n", pMsg1));
#endif
      /* get Tag */
      for (count = 0; count < pIomb->coalescedCount; count++)
      {
        tag = pIomb->sspComplCxt[count].tag;
        sspTag = pIomb->sspComplCxt[count].SSPTag;
        pRequest = (agsaIORequestDesc_t *)saRoot->IOMap[tag].IORequest;
        SA_ASSERT((pRequest), "pRequest");

        if(pRequest == agNULL)
        {
          SA_DBG1(("mpiParseOBIomb,OPC_OUB_COMBINED_SSP_COMP Resp IOMB tag=0x%x, status=0x%x, param=0x%x, SSPTag=0x%x\n", tag, OSSA_IO_SUCCESS, 0, sspTag));
          return(AGSA_RC_FAILURE);
        }
        SA_ASSERT((pRequest->valid), "pRequest->valid");

        SA_DBG3(("mpiParseOBIomb, OPC_OUB_COMBINED_SSP_COMP IOMB tag=0x%x, status=0x%x, param=0x%x, SSPTag=0x%x\n", tag, OSSA_IO_SUCCESS, 0, sspTag));

        /* Completion of SSP without Response Data */
        siIODone( agRoot, pRequest, OSSA_IO_SUCCESS, sspTag);
      }
    }
    break;

    case OPC_OUB_SSP_COMP:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSSPCompleted++;
      SA_DBG3(("mpiParseOBIomb, SSP_COMP Response received IOMB=%p %d\n",
         pMsg1, saRoot->LLCounters.IOCounter.numSSPCompleted));
#else
      SA_DBG3(("mpiParseOBIomb, SSP_COMP Response received IOMB=%p\n", pMsg1));
#endif
      /* process the SSP IO Completed response message */
      mpiSSPCompletion(agRoot, pMsg1);
      break;
    }
    case OPC_OUB_COMBINED_SATA_COMP:
    {
      agsaSATACoalescedCompletionRsp_t    *pIomb;
      agsaIORequestDesc_t       *pRequest;
      bit32                     tag;
      bit32                     count;

    #ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSSPCompleted++;
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_COMBINED_SATA_COMP Response received IOMB=%p %d\n",
         pMsg1, saRoot->LLCounters.IOCounter.numSSPCompleted));
    #else
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_COMBINED_SATA_COMP Response received IOMB=%p\n", pMsg1));
    #endif

      pIomb = (agsaSATACoalescedCompletionRsp_t *)pMsg1;
      /* get Tag */
      for (count = 0; count < pIomb->coalescedCount; count++)
      {
        tag = pIomb->stpComplCxt[count].tag;
        pRequest = (agsaIORequestDesc_t *)saRoot->IOMap[tag].IORequest;
        SA_ASSERT((pRequest), "pRequest");

        if(pRequest == agNULL)
        {
          SA_DBG1(("mpiParseOBIomb,OPC_OUB_COMBINED_SATA_COMP Resp IOMB tag=0x%x, status=0x%x, param=0x%x\n", tag, OSSA_IO_SUCCESS, 0));
          return(AGSA_RC_FAILURE);
        }
        SA_ASSERT((pRequest->valid), "pRequest->valid");

        SA_DBG3(("mpiParseOBIomb, OPC_OUB_COMBINED_SATA_COMP IOMB tag=0x%x, status=0x%x, param=0x%x\n", tag, OSSA_IO_SUCCESS, 0));

        /* Completion of SATA without Response Data */
        siIODone( agRoot, pRequest, OSSA_IO_SUCCESS, 0);
      }
      break;
    }
    case OPC_OUB_SATA_COMP:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSataCompleted++;
      SA_DBG3(("mpiParseOBIomb, SATA_COMP Response received IOMB=%p %d\n",
             pMsg1, saRoot->LLCounters.IOCounter.numSataCompleted));
#else
      SA_DBG3(("mpiParseOBIomb, SATA_COMP Response received IOMB=%p\n", pMsg1));
#endif
      /* process the response message */
      mpiSATACompletion(agRoot, pMsg1);
      break;
    }
    case OPC_OUB_SSP_ABORT_RSP:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSSPAbortedCB++;
#else
      SA_DBG3(("mpiParseOBIomb, SSP_ABORT Response received IOMB=%p\n", pMsg1));
#endif
      /* process the response message */
      parserStatus = mpiSSPAbortRsp(agRoot, (agsaSSPAbortRsp_t *)pMsg1);
      if(parserStatus !=  AGSA_RC_SUCCESS)
      {
         SA_DBG3(("mpiParseOBIomb, mpiSSPAbortRsp FAIL IOMB=%p\n", pMsg1));
      }

      break;
    }
    case OPC_OUB_SATA_ABORT_RSP:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSataAbortedCB++;
#else
      SA_DBG3(("mpiParseOBIomb, SATA_ABORT Response received IOMB=%p\n", pMsg1));
#endif
      /* process the response message */
      mpiSATAAbortRsp(agRoot, (agsaSATAAbortRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SATA_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, SATA_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSATAEvent(agRoot, (agsaSATAEventRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SSP_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, SSP_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSSPEvent(agRoot, (agsaSSPEventRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SMP_COMP:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSMPCompleted++;
      SA_DBG3(("mpiParseOBIomb, SMP_COMP Response received IOMB=%p, %d\n",
             pMsg1, saRoot->LLCounters.IOCounter.numSMPCompleted));
#else
      SA_DBG3(("mpiParseOBIomb, SMP_COMP Response received IOMB=%p\n", pMsg1));
#endif
      /* process the response message */
      mpiSMPCompletion(agRoot, (agsaSMPCompletionRsp_t *)pMsg1);
      break;
    }
#ifndef BIOS
    case OPC_OUB_ECHO:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numEchoCB++;
      SA_DBG3(("mpiParseOBIomb, ECHO Response received %d\n", saRoot->LLCounters.IOCounter.numEchoCB));
#else
      SA_DBG3(("mpiParseOBIomb, ECHO Response received\n"));
#endif
      /* process the response message */
      mpiEchoRsp(agRoot, (agsaEchoRsp_t *)pMsg1);
      break;
    }
#endif
    case OPC_OUB_GET_NVMD_DATA:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_GET_NVMD_DATA received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetNVMDataRsp(agRoot, (agsaGetNVMDataRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SPC_HW_EVENT:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SPC_HW_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiHWevent(agRoot, (agsaHWEvent_SPC_OUB_t *)pMsg1);
      break;
    }
    case OPC_OUB_HW_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, HW_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiHWevent(agRoot, (agsaHWEvent_SPC_OUB_t *)pMsg1);
      break;
    }
    case OPC_OUB_PHY_START_RESPONSE:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_PHY_START_RESPONSE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiPhyStartEvent( agRoot, (agsaHWEvent_Phy_OUB_t  *)pMsg1  );

      break;
    }
    case OPC_OUB_PHY_STOP_RESPONSE:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_PHY_STOP_RESPONSE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiPhyStopEvent( agRoot, (agsaHWEvent_Phy_OUB_t  *)pMsg1  );
      break;
    }

    case OPC_OUB_LOCAL_PHY_CNTRL:
    {
      SA_DBG3(("mpiParseOBIomb, PHY CONTROL Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiPhyCntrlRsp(agRoot, (agsaLocalPhyCntrlRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SPC_DEV_REGIST:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SPC_DEV_REGIST Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDeviceRegRsp(agRoot, (agsaDeviceRegistrationRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_DEV_REGIST:
    {
      SA_DBG2(("mpiParseOBIomb, DEV_REGISTRATION Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDeviceRegRsp(agRoot, (agsaDeviceRegistrationRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_DEREG_DEV:
    {
      SA_DBG3(("mpiParseOBIomb, DEREGISTRATION DEVICE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDeregDevHandleRsp(agRoot, (agsaDeregDevHandleRsp_t *)pMsg1);
      break;
    }
#ifndef BIOS
    case OPC_OUB_GET_DEV_HANDLE:
    {
      SA_DBG3(("mpiParseOBIomb, GET_DEV_HANDLE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDevHandleRsp(agRoot, (agsaGetDevHandleRsp_t *)pMsg1);
      break;
    }
#endif
    case OPC_OUB_SPC_DEV_HANDLE_ARRIV:
    {
      SA_DBG3(("mpiParseOBIomb, SPC_DEV_HANDLE_ARRIV Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDeviceHandleArrived(agRoot, (agsaDeviceHandleArrivedNotify_t *)pMsg1);
      break;
    }
    case OPC_OUB_DEV_HANDLE_ARRIV:
    {
      SA_DBG3(("mpiParseOBIomb, DEV_HANDLE_ARRIV Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDeviceHandleArrived(agRoot, (agsaDeviceHandleArrivedNotify_t *)pMsg1);
      break;
    }
#if 0 //Sunitha
	case OPC_OUB_THERM_HW_EVENT:
	{
      SA_DBG3(("mpiParseOBIomb, THERM_HW_EVENT Response received IOMB=%p\n", pMsg1));
      ossaLogThermalEvent(agRoot, (agsaThermal_Hw_Event_Notify_t *)pMsg1);
      break;
	}
#endif //Sunitha
    case OPC_OUB_SSP_RECV_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, SSP_RECV_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSSPReqReceivedNotify(agRoot, (agsaSSPReqReceivedNotify_t *)pMsg1);
      break;
    }
    case OPC_OUB_DEV_INFO:
    {
      SA_ASSERT((smIS_SPCV(agRoot)), "smIS_SPCV");
      SA_DBG3(("mpiParseOBIomb, DEV_INFO Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDevInfoRsp(agRoot, (agsaGetDevInfoRspV_t *)pMsg1);
      break;
    }
#ifndef BIOS
    case OPC_OUB_GET_PHY_PROFILE_RSP:
    {
      SA_ASSERT((smIS_SPCV(agRoot)), "smIS_SPCV");
      SA_DBG2(("mpiParseOBIomb, OPC_OUB_GET_PHY_PROFILE_RSP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetPhyProfileRsp(agRoot, (agsaGetPhyProfileRspV_t *)pMsg1);
      break;
    }
    case OPC_OUB_SET_PHY_PROFILE_RSP:
    {
      SA_ASSERT((smIS_SPCV(agRoot)), "smIS_SPCV");
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_PHY_PROFILE_RSP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSetPhyProfileRsp(agRoot, (agsaSetPhyProfileRspV_t *)pMsg1);
      break;
    }
#endif /* BIOS */
    case OPC_OUB_SPC_DEV_INFO:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      SA_DBG3(("mpiParseOBIomb, DEV_INFO Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDevInfoRspSpc(agRoot, (agsaGetDevInfoRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_FW_FLASH_UPDATE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_FW_FLASH_UPDATE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiFwFlashUpdateRsp(agRoot, (agsaFwFlashUpdateRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_FLASH_OP_EXT_RSP:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_FW_FLASH_UPDATE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiFwExtFlashUpdateRsp(agRoot, (agsaFwFlashOpExtRsp_t *)pMsg1);
      break;
    }
#ifndef BIOS
#ifdef SPC_ENABLE_PROFILE
    case OPC_OUB_FW_PROFILE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_FW_PROFILE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiFwProfileRsp(agRoot, (agsaFwProfileRsp_t *)pMsg1);
      break;
    }
#endif
    case OPC_OUB_SET_NVMD_DATA:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_NVMD_DATA received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSetNVMDataRsp(agRoot, (agsaSetNVMDataRsp_t *)pMsg1);
      break;
    }

    case OPC_OUB_GPIO_RESPONSE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_GPIO_RESPONSE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGPIORsp(agRoot, (agsaGPIORsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_GPIO_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_GPIO_RESPONSE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGPIOEventRsp(agRoot, (agsaGPIOEvent_t *)pMsg1);
      break;
    }
#endif  /* BIOS */
    case OPC_OUB_GENERAL_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_GENERAL_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGeneralEventRsp(agRoot, (agsaGeneralEventRsp_t *)pMsg1);
      break;
    }
#ifndef BIOS
    case OPC_OUB_SAS_DIAG_MODE_START_END:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SAS_DIAG_MODE_START_END Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSASDiagStartEndRsp(agRoot, (agsaSASDiagStartEndRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SAS_DIAG_EXECUTE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SAS_DIAG_EXECUTE_RSP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSASDiagExecuteRsp(agRoot, (agsaSASDiagExecuteRsp_t *)pMsg1);
      break;
    }
#endif /* BIOS */
    case OPC_OUB_GET_TIME_STAMP:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_GET_TIME_STAMP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetTimeStampRsp(agRoot, (agsaGetTimeStampRsp_t *)pMsg1);
      break;
    }

    case OPC_OUB_SPC_SAS_HW_EVENT_ACK:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      SA_DBG3(("mpiParseOBIomb,OPC_OUB_SPC_SAS_HW_EVENT_ACK  Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSASHwEventAckRsp(agRoot, (agsaSASHwEventAckRsp_t *)pMsg1);
      break;
    }

    case OPC_OUB_SAS_HW_EVENT_ACK:
    {
      SA_ASSERT((smIS_SPCV(agRoot)), "smIS_SPCV");
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SAS_HW_EVENT_ACK Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSASHwEventAckRsp(agRoot, (agsaSASHwEventAckRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_PORT_CONTROL:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_PORT_CONTROL Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiPortControlRsp(agRoot, (agsaPortControlRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SMP_ABORT_RSP:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numSMPAbortedCB++;
      SA_DBG3(("mpiParseOBIomb, SMP_ABORT Response received IOMB=%p, %d\n",
             pMsg1, saRoot->LLCounters.IOCounter.numSMPAbortedCB));
#else
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SMP_ABORT_RSP Response received IOMB=%p\n", pMsg1));
#endif
      /* process the response message */
      mpiSMPAbortRsp(agRoot, (agsaSMPAbortRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_DEVICE_HANDLE_REMOVAL:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_DEVICE_HANDLE_REMOVAL received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDeviceHandleRemoval(agRoot, (agsaDeviceHandleRemoval_t *)pMsg1);
      break;
    }
    case OPC_OUB_SET_DEVICE_STATE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_DEVICE_STATE received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSetDeviceStateRsp(agRoot, (agsaSetDeviceStateRsp_t *)pMsg1);
      break;
    }

#ifndef BIOS
    case OPC_OUB_GET_DEVICE_STATE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_GET_DEVICE_STATE received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDeviceStateRsp(agRoot, (agsaGetDeviceStateRsp_t *)pMsg1);
      break;
    }
#endif  /* BIOS */

    case OPC_OUB_SET_DEV_INFO:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_DEV_INFO received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSetDevInfoRsp(agRoot, (agsaSetDeviceInfoRsp_t *)pMsg1);
      break;
    }

#ifndef BIOS_DEBUG
    case OPC_OUB_SAS_RE_INITIALIZE:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SAS_RE_INITIALIZE received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSasReInitializeRsp(agRoot, (agsaSasReInitializeRsp_t *)pMsg1);
      break;
    }
#endif  /* BIOS */

    case OPC_OUB_SGPIO_RESPONSE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SGPIO_RESPONSE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSGpioRsp(agRoot, (agsaSGpioRsp_t *)pMsg1);
      break;
    }

#ifndef BIOS
    case OPC_OUB_PCIE_DIAG_EXECUTE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_PCIE_DIAG_EXECUTE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiPCIeDiagExecuteRsp(agRoot, (agsaPCIeDiagExecuteRsp_t *)pMsg1);
      break;
    }
    case 2104: //delray start
    {
      if(smIS_SPC6V(agRoot))
      {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_GET_DFE_DATA_RSP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDFEDataRsp(agRoot, (agsaGetDDEFDataRsp_t *)pMsg1);
      }
      if(smIS_SPC12V(agRoot))
      {
        SA_DBG3(("mpiParseOBIomb, OPC_INB_GET_VIST_CAP Response received IOMB=%p\n", pMsg1));
        mpiGetVisRsp(agRoot, (agsaGetVisCapRsp_t *)pMsg1);
      }  
      else
      {
        SA_DBG1(("mpiParseOBIomb, 2104  Response received IOMB=%p\n", pMsg1));
      }
      break;
    }
#endif   /* BIOS */
    case OPC_OUB_SET_CONTROLLER_CONFIG:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_CONTROLLER_CONFIG Response received IOMB=%p\n", pMsg1));
      mpiSetControllerConfigRsp(agRoot, (agsaSetControllerConfigRsp_t *)pMsg1);
      break;
    }
#ifndef BIOS
    case OPC_OUB_GET_CONTROLLER_CONFIG:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_GET_CONTROLLER_CONFIG Response received IOMB=%p\n", pMsg1));
      mpiGetControllerConfigRsp(agRoot, (agsaGetControllerConfigRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_KEK_MANAGEMENT:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_KEK_MANAGEMENT Response received IOMB=%p\n", pMsg1));
      mpiKekManagementRsp(agRoot, (agsaKekManagementRsp_t *)pMsg1);
      break;
    }
#endif  /* BIOS */
#ifdef UN_USED_FUNC
    case OPC_OUB_DEK_MANAGEMENT:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_DEK_MANAGEMENT Response received IOMB=%p\n", pMsg1));
      mpiDekManagementRsp(agRoot, (agsaDekManagementRsp_t *)pMsg1);
      break;
    }
#endif
#ifndef BIOS
    case OPC_OUB_OPR_MGMT:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_OPR_MGMT Response received IOMB=%p\n", pMsg1));
      mpiOperatorManagementRsp(agRoot, (agsaOperatorMangmenRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_ENC_TEST_EXECUTE:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_ENC_TEST_EXECUTE Response received IOMB=%p\n", pMsg1));
      mpiBistRsp(agRoot, (agsaEncryptBistRsp_t *)pMsg1);
      break;
    }
#endif /* BIOS */
    case OPC_OUB_SET_OPERATOR:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_SET_OPERATOR Response received IOMB=%p\n", pMsg1));
      mpiSetOperatorRsp(agRoot, (agsaSetOperatorRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_GET_OPERATOR:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_GET_OPERATOR Response received IOMB=%p\n", pMsg1));
      mpiGetOperatorRsp(agRoot, (agsaGetOperatorRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_DIF_ENC_OFFLOAD_RSP://delray start
    {
      SA_ASSERT((smIS_SPCV(agRoot)), "smIS_SPCV");
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_DIF_ENC_OFFLOAD_RSP Response received IOMB=%p\n", pMsg1));
      mpiDifEncOffloadRsp(agRoot, (agsaDifEncOffloadRspV_t *)pMsg1);
      break;
    }			//delray end
    default:
    {
#ifdef SALL_API_TEST
      saRoot->LLCounters.IOCounter.numUNKNWRespIOMB++;
      SA_DBG1(("mpiParseOBIomb, UnKnown Response received IOMB=%p, %d\n",
             pMsg1, saRoot->LLCounters.IOCounter.numUNKNWRespIOMB));
#else
      SA_DBG1(("mpiParseOBIomb, Unknown IOMB Response received opcode 0x%X IOMB=%p\n",opcode, pMsg1));
#endif
      break;
    }
  } /* switch */

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2f");

  return ret;

}
#endif

#ifndef BIOS
#endif

/******************************************************************************/
/*! \brief ECHO Response
 *
 *  This routine handles the response of ECHO Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiEchoRsp(
  agsaRoot_t          *agRoot,
  agsaEchoRsp_t       *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "2g");

  SA_DBG3(("mpiEchoRsp: HTAG=0x%x\n", pIomb->tag));

  /* get request from IOMap */
  OSSA_READ_LE_32(agRoot, &tag, pIomb, OSSA_OFFSET_OF(agsaEchoRsp_t, tag));

  pRequest = (agsaIORequestDesc_t *)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiEchoRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x\n", tag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2g");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  ossaEchoCB(agRoot, agContext, (void *)&pIomb->payload[0]);

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiEchoRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2g");
  return ret;
}

/******************************************************************************/
/*! \brief Get NVM Data Response
 *
 *  This routine handles the response of GET NVM Data Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetNVMDataRsp(
  agsaRoot_t          *agRoot,
  agsaGetNVMDataRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               i, dataLen;
  bit32               DlenStatus, tag, iRTdaBnDpsAsNvm;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "2h");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetNVMDataRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &DlenStatus, pIomb, OSSA_OFFSET_OF(agsaGetNVMDataRsp_t, DlenStatus));
  OSSA_READ_LE_32(AGROOT, &iRTdaBnDpsAsNvm, pIomb, OSSA_OFFSET_OF(agsaGetNVMDataRsp_t, iRTdaBnDpsAsNvm));
  OSSA_READ_LE_32(AGROOT, &dataLen, pIomb, OSSA_OFFSET_OF(agsaGetNVMDataRsp_t, NVMData[10])) ;

  SA_DBG1(("mpiGetNVMDataRsp: HTAG=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t *)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetNVMDataRsp: Bad Response IOMB!!! pRequest is NULL.\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2h");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  if (iRTdaBnDpsAsNvm & IRMode)
  {
    /* indirect mode - IR bit set */
    SA_DBG1(("mpiGetNVMDataRsp: OSSA_SUCCESS, IR=1, DataLen=%d\n", dataLen));
    if (((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_CONFIG_SEEPROM) ||
        ((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_VPD_FLASH) ||
        ((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_TWI_DEVICES) ||
        ((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_EXPANSION_ROM) ||
        ((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_IOP_REG_FLASH))
    {
      /* CB for NVMD */
//#ifdef UN_USED_FUNC
      ossaGetNVMDResponseCB(agRoot, agContext, (DlenStatus & NVMD_STAT), INDIRECT_MODE, dataLen, agNULL);
//#endif
    }
    else if (((iRTdaBnDpsAsNvm & NVMD_TYPE) == AAP1_RDUMP) ||
             ((iRTdaBnDpsAsNvm & NVMD_TYPE) == IOP_RDUMP))
    {
#ifdef UN_USED_FUNC
      if ((DlenStatus & NVMD_STAT) == 0)
      {
        /* CB for Register Dump */

        ossaGetRegisterDumpCB(agRoot, agContext, OSSA_SUCCESS);
      }
      else
      {
        /* CB for Register Dump */
        ossaGetRegisterDumpCB(agRoot, agContext, OSSA_FAILURE);
      }
#endif
    }
    else
    {
      /* Should not be happened */
      SA_DBG1(("mpiGetNVMDataRsp: (IR=1)Wrong Device type 0x%x\n", iRTdaBnDpsAsNvm));
    }
  }
  else /* direct mode */
  {
    SA_DBG1(("mpiGetNVMDataRsp: OSSA_SUCCESS, IR=0, DataLen=%d\n", ((DlenStatus & NVMD_LEN) >> SHIFT24)));
    for (i = 0; i < (((DlenStatus & NVMD_LEN) >> SHIFT24)/4); i++)
    {
      SA_DBG1(("mpiGetNVMDataRsp: OSSA_SUCCESS, NVMDATA=0x%x\n", pIomb->NVMData[i]));
    }
    if (((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_CONFIG_SEEPROM) ||
        ((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_VPD_FLASH) ||
        ((iRTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_TWI_DEVICES))
    {
      /* CB for NVMD */
  //    char * safe_type_pun = (char *)(&pIomb->NVMData[0]);
#ifdef UN_USED_FUNC
      ossaGetNVMDResponseCB(agRoot, agContext, (DlenStatus & NVMD_STAT), DIRECT_MODE,
         ((DlenStatus & NVMD_LEN) >> SHIFT24), (agsaFrameHandle_t *)safe_type_pun);
#endif
    }
    else if (((iRTdaBnDpsAsNvm & NVMD_TYPE) == AAP1_RDUMP) ||
             ((iRTdaBnDpsAsNvm & NVMD_TYPE) == IOP_RDUMP))
    {
#ifdef UN_USED_FUNC

      if ((DlenStatus & NVMD_STAT) == 0)
      {
        /* CB for Register Dump */
        ossaGetRegisterDumpCB(agRoot, agContext, OSSA_SUCCESS);
      }
      else
      {
        /* CB for Register Dump */
        ossaGetRegisterDumpCB(agRoot, agContext, OSSA_FAILURE);
      }
#endif
    }
    else
    {
      /* Should not be happened */
      SA_DBG1(("mpiGetNVMDataRsp: (IR=0)Wrong Device type 0x%x\n", iRTdaBnDpsAsNvm));
    }
  }

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetNVMDataRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2h");

  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief Phy Event Response from SPCv
 *
 *  Process Phy Event from SPC
 *
 *  \param agRoot        Handles for this instance of SAS/SATA LL Layer
 *  \param pIomb         pointer of IOMB
 *
 *  \return success or fail
 *
 */
/*******************************************************************************/

GLOBAL bit32 mpiPhyStartEvent(
  agsaRoot_t        *agRoot,
  agsaHWEvent_Phy_OUB_t  *pIomb
  )
{
  bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  bit32                    phyId;
  bit32                    IOMBStatus;
  bit32                    tag;

  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32                HwCBStatus;

  if(saRoot == agNULL)
  {
    SA_DBG1(("mpiPhyStartEvent: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  smTraceFuncEnter(hpDBG_VERY_LOUD, "2H");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaHWEvent_Phy_OUB_t, tag)) ;

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiPhyStartEvent: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x \n", tag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2H");
    return AGSA_RC_FAILURE;
  }

  SA_DBG1(("mpiPhyStartEvent: Status 0x%X PhyId 0x%X\n",pIomb->Status,pIomb->ReservedPhyId));

  OSSA_READ_LE_32(AGROOT, &IOMBStatus, pIomb, OSSA_OFFSET_OF(agsaHWEvent_Phy_OUB_t,Status ));
  OSSA_READ_LE_32(AGROOT, &phyId, pIomb, OSSA_OFFSET_OF(agsaHWEvent_Phy_OUB_t,ReservedPhyId ));

  switch (IOMBStatus)
  {
    case OSSA_MPI_IO_SUCCESS:                  /* PhyStart operation completed successfully */
      HwCBStatus = 0;
      saRoot->phys[phyId].linkstatus = 1;
      SA_DBG1(("mpiPhyStartEvent:MPI_IO_SUCCESS IOMBStatus 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      /* Callback with PHY_UP */
      break;
    case OSSA_MPI_ERR_INVALID_PHY_ID:      /* identifier specified in the PHY_START command is invalid i.e out of supported range for this product. */
      HwCBStatus = 1;
      saRoot->phys[phyId].linkstatus = 0;
      SA_DBG1(("mpiPhyStartEvent: MPI_ERR_INVALID_PHY_ID IOMBStatus 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      ret = AGSA_RC_FAILURE;
      break;
    case OSSA_MPI_ERR_PHY_ALREADY_STARTED:
      HwCBStatus = 2;
      saRoot->phys[phyId].linkstatus = 1;
      SA_DBG1(("mpiPhyStartEvent: MPI_ERR_PHY_ALREADY_STARTED IOMBStatus 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      ret = AGSA_RC_FAILURE;
      break;
    case OSSA_MPI_ERR_INVALID_ANALOG_TBL_IDX:
      HwCBStatus = 4;
      saRoot->phys[phyId].linkstatus = 0;
      SA_DBG1(("mpiPhyStartEvent: MPI_ERR_INVALID_ANALOG_TBL_IDX IOMBStatus 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      ret = AGSA_RC_FAILURE;
      break;
    default:
      HwCBStatus = 3;
      saRoot->phys[phyId].linkstatus = 0;
      SA_DBG1(("mpiPhyStartEvent: Unknown IOMBStatus 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      ret = AGSA_RC_FAILURE;
    break;
  }

  ossaHwCB(agRoot,agNULL, OSSA_HW_EVENT_PHY_START_STATUS ,((HwCBStatus << SHIFT8) | phyId) ,agContext, agNULL);

  /* return the request to free pool */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiPhyStartEvent: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  return(ret);
}


GLOBAL bit32 mpiPhyStopEvent(
  agsaRoot_t        *agRoot,
  agsaHWEvent_Phy_OUB_t  *pIomb
  )
{
  bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32                    phyId;

  bit32                    IOMBStatus;
  bit32                    HwCBStatus;

  bit32                    tag;

  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;

  agsaPhy_t               *pPhy;
  agsaPort_t              *pPort;


  if(saRoot == agNULL)
  {
    SA_DBG1(("mpiPhyStopEvent: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaHWEvent_Phy_OUB_t, tag)) ;

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiPhyStopEvent: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x \n", tag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2H");
    return AGSA_RC_FAILURE;
  }

  OSSA_READ_LE_32(AGROOT, &IOMBStatus, pIomb, OSSA_OFFSET_OF(agsaHWEvent_Phy_OUB_t,Status ));
  OSSA_READ_LE_32(AGROOT, &phyId, pIomb, OSSA_OFFSET_OF(agsaHWEvent_Phy_OUB_t,ReservedPhyId ));
  SA_DBG1(("mpiPhyStopEvent: Status %08X PhyId %08X\n",IOMBStatus,phyId));

  if(smIS_SPCV(agRoot))
  {
      phyId &= 0xff;  // SPCv PHY_ID is one byte wide
  }

  saRoot->phys[phyId].linkstatus = 0;

  switch (IOMBStatus)
  {
    case OSSA_MPI_IO_SUCCESS:                  /* PhyStart operation completed successfully */
      SA_DBG1(("mpiPhyStopEvent:MPI_IO_SUCCESS  0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      HwCBStatus = 0;
      /* Callback with PHY_DOWN */
      break;
    case OSSA_MPI_ERR_INVALID_PHY_ID:      /* identifier specified in the PHY_START command is invalid i.e out of supported range for this product. */
      SA_DBG1(("mpiPhyStopEvent: MPI_ERR_INVALID_PHY_ID 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      HwCBStatus = 1;
      break;
    case OSSA_MPI_ERR_PHY_NOT_STARTED:  /* An attempt to stop a phy which is not started  */
      HwCBStatus = 4;
      SA_DBG1(("mpiPhyStopEvent:  0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      break;

    case OSSA_MPI_ERR_DEVICES_ATTACHED:  /* All the devices in a port need to be deregistered if the PHY_STOP is for the last phy  */
      HwCBStatus = 2;
      SA_DBG1(("mpiPhyStopEvent:  0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      break;

    default:
      HwCBStatus = 3;
      SA_DBG1(("mpiPhyStopEvent: Unknown Status 0x%x for phyId 0x%x\n",IOMBStatus,phyId));
      break;
  }


  if(HwCBStatus == 0)
  {
    pPhy = &(saRoot->phys[phyId]);
    /* get the port of the phy */
    pPort = pPhy->pPort;
    if ( agNULL != pPort )
    {
      SA_DBG1(("siPhyStopCB: phy%d invalidating port\n", phyId));
      /* invalid port state, remove the port */
      pPort->status |= PORT_INVALIDATING;
      saRoot->PortMap[pPort->portId].PortStatus  |= PORT_INVALIDATING;
      /* invalid the port */
      siPortInvalid(agRoot, pPort);
      /* map out the portmap */
      saRoot->PortMap[pPort->portId].PortContext = agNULL;
      saRoot->PortMap[pPort->portId].PortID = PORT_MARK_OFF;
      saRoot->PortMap[pPort->portId].PortStatus  |= PORT_INVALIDATING;
      ossaHwCB(agRoot,&(pPort->portContext) , OSSA_HW_EVENT_PHY_STOP_STATUS, ((HwCBStatus << SHIFT8) | phyId ),agContext, agNULL);
    }
    else
    {
      SA_DBG1(("siPhyStopCB: phy%d - Port is not established\n", phyId));
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_PHY_STOP_STATUS, ((HwCBStatus << SHIFT8) | phyId ) , agContext, agNULL);
    }

    /* set PHY_STOPPED status */
    PHY_STATUS_SET(pPhy, PHY_STOPPED);

    /* Exclude the phy from a port */
    if ( agNULL != pPort )
    {
      /* Acquire port list lock */
      ossaSingleThreadedEnter(agRoot, LL_PORT_LOCK);

      /* Delete the phy from the port */
      pPort->phyMap[phyId] = agFALSE;
      saRoot->phys[phyId].pPort = agNULL;

      /* Release port list lock */
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);
    }

  }
  else
  {
    SA_DBG1(("siPhyStopCB: Error phy%d - Port is not established\n", phyId));
    ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_PHY_STOP_STATUS, ((HwCBStatus << SHIFT8) | phyId ) , agContext, agNULL);
  }

  /* return the request to free pool */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiPhyStartEvent: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  return(ret);
}


/******************************************************************************/
/*! \brief Hardware Event Response from SPC
 *
 *  Process HW Event from SPC
 *
 *  \param agRoot        Handles for this instance of SAS/SATA LL Layer
 *  \param pIomb         pointer of IOMB
 *
 *  \return success or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiHWevent(
  agsaRoot_t            *agRoot,
  agsaHWEvent_SPC_OUB_t *pIomb
  )
{
  bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;
  agsaPortContext_t        *agPortContext;
  agsaSASIdentify_t        *IDframe;
  agsaFisRegDeviceToHost_t *sataFis;
  agsaContext_t            *agContext;
  agsaPort_t               *pPort = agNULL;
  bit32                    phyId;
  bit32                    portId;
  bit32                    Event;
  bit32                    tag, status;
  bit8                     linkRate;
  bit32                    LREventPhyIdPortId;
  bit32                    npipps, eventParam,npip,port_state;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2j");

  SA_ASSERT((agNULL !=saRoot ), "");
  if(saRoot == agNULL)
  {
    SA_DBG1(("mpiHWevent: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  if(smIS_SPC(agRoot))
  {
    OSSA_READ_LE_32(AGROOT, &LREventPhyIdPortId, pIomb, OSSA_OFFSET_OF(agsaHWEvent_SPC_OUB_t, LRStatusEventPhyIdPortId));
    OSSA_READ_LE_32(AGROOT, &npipps, pIomb, OSSA_OFFSET_OF(agsaHWEvent_SPC_OUB_t, NpipPortState));
    OSSA_READ_LE_32(AGROOT, &eventParam, pIomb, OSSA_OFFSET_OF(agsaHWEvent_SPC_OUB_t, EVParam));
    SA_DBG2(("mpiHWEvent: S, LREventPhyIdPortId 0x%08x npipps 0x%08x eventParam 0x%08x\n", LREventPhyIdPortId ,npipps ,eventParam ));

    /* get port context */
    portId = LREventPhyIdPortId & PORTID_MASK;
    smTrace(hpDBG_VERY_LOUD,"QK",portId);
    /* TP:QK portId */

    /* get phyId */
    phyId = (LREventPhyIdPortId & PHY_ID_BITS) >> SHIFT4;

    smTrace(hpDBG_VERY_LOUD,"QK",npipps);
    /* TP:QK npipps */
    smTrace(hpDBG_VERY_LOUD,"QL",portId);
    /* TP:QL portId */
    smTrace(hpDBG_VERY_LOUD,"QM",phyId);
    /* TP:QM phyId */

    SA_DBG1(("mpiHWEvent:SPC, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, (npipps & PORT_STATE_MASK)));
  }
  else
  {
    OSSA_READ_LE_32(AGROOT, &LREventPhyIdPortId, pIomb, OSSA_OFFSET_OF(agsaHWEvent_V_OUB_t, LRStatEventPortId));
    OSSA_READ_LE_32(AGROOT, &npipps, pIomb, OSSA_OFFSET_OF(agsaHWEvent_V_OUB_t, RsvPhyIdNpipRsvPortState));
    OSSA_READ_LE_32(AGROOT, &eventParam, pIomb, OSSA_OFFSET_OF(agsaHWEvent_V_OUB_t, EVParam));
    SA_DBG2(("mpiHWEvent: V, LREventPhyIdPortId 0x%08x npipps 0x%08x eventParam 0x%08x\n", LREventPhyIdPortId ,npipps ,eventParam ));

    smTrace(hpDBG_VERY_LOUD,"QN",npipps);
    /* TP:QN npipps */

    /* get port context */
    portId = LREventPhyIdPortId & PORTID_MASK;

    smTrace(hpDBG_VERY_LOUD,"QO",portId);
    /* TP:QO portId */

    /* get phyId */
    phyId = (npipps & PHY_ID_V_BITS) >> SHIFT16;
    smTrace(hpDBG_VERY_LOUD,"QP",phyId);
    /* TP:QP phyId */

    /* get npipps */
    npip =(npipps & 0xFF00 ) >> SHIFT4;
    port_state  =(npipps & 0xF );
    npipps = npip | port_state; // Make it look like SPCs nipps


    SA_DBG1(("mpiHWEvent: V, PhyID 0x%x PortID 0x%x NPIP 0x%x PS 0x%x npipps 0x%x\n",
                phyId, portId,npip,port_state,npipps));
  }

  Event = (LREventPhyIdPortId & HW_EVENT_BITS) >> SHIFT8;

  /* get Link Rate */
  linkRate = (bit8)((LREventPhyIdPortId & LINK_RATE_MASK) >> SHIFT28);
  /* get status byte */
  status = (LREventPhyIdPortId & STATUS_BITS) >> SHIFT24;

  smTrace(hpDBG_VERY_LOUD,"HA",portId);
  /* TP:HA portId */
  smTrace(hpDBG_VERY_LOUD,"HB",linkRate);
  /* TP:HB linkRate */
  smTrace(hpDBG_VERY_LOUD,"HC",phyId);
  /* TP:HC phyId */
  smTrace(hpDBG_VERY_LOUD,"HD",npipps);
  /* TP:HD npipps */
  smTrace(hpDBG_VERY_LOUD,"HE",status);
  /* TP:HE status */

  if (portId > saRoot->phyCount)
  {
    if (OSSA_PORT_NOT_ESTABLISHED == (npipps & PORT_STATE_MASK))
    {
      /* out of range checking for portId */
      SA_DBG1(("mpiHWEvent: PORT_ID is out of range, PhyID %d PortID %d\n",
                phyId, portId));
      /* port is not estiblished */
      agPortContext = agNULL;
    }
    else
    {
      /* portId is bad and state is correct - should not happen */
      SA_DBG1(("mpiHWEvent: PORT_ID is bad with correct Port State, PhyID %d PortID %d\n",
                phyId, portId));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2j");
      return AGSA_RC_FAILURE;
    }
  }
  else
  {
    SA_DBG2(("mpiHWEvent:PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[portId & PORTID_MASK].PortID,saRoot->PortMap[portId & PORTID_MASK].PortStatus,saRoot->PortMap[portId & PORTID_MASK].PortContext));
    agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId].PortContext;
  }

  if(agPortContext == agNULL)
  {
    SA_DBG1(("mpiHWEvent: agPortContext is NULL, PhyID %d PortID %d\n",
                phyId, portId));
  }

  smTrace(hpDBG_VERY_LOUD,"HF",Event);
  /* TP:HF OSSA_HW_EVENT */

  switch (Event)
  {
    case OSSA_HW_EVENT_SAS_PHY_UP:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_SAS_PHY_UP, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, (npipps & PORT_STATE_MASK)));

      /* get SAS Identify info */
      IDframe = (agsaSASIdentify_t *)&pIomb->sasIdentify;
      /* Callback about SAS link up */
      saRoot->phys[phyId].linkstatus |= 2;
      saRoot->phys[phyId].sasIdentify.phyIdentifier = IDframe->phyIdentifier;
      saRoot->phys[phyId].sasIdentify.deviceType_addressFrameType = IDframe->deviceType_addressFrameType;
    
      si_memcpy(&(saRoot->phys[phyId].sasIdentify.sasAddressHi),&(IDframe->sasAddressHi),4);
      si_memcpy(&(saRoot->phys[phyId].sasIdentify.sasAddressLo),&(IDframe->sasAddressLo),4);
      siEventPhyUpRcvd(agRoot, phyId, IDframe, portId, npipps, linkRate);
      break;
    }
    case OSSA_HW_EVENT_SATA_PHY_UP:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_SATA_PHY_UP, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, (npipps & PORT_STATE_MASK)));

      /* get SATA FIS info */
      saRoot->phys[phyId].linkstatus |= 2;
      sataFis = (agsaFisRegDeviceToHost_t *)&pIomb->sataFis;
      /* Callback about SATA Link Up */
      siEventSATASignatureRcvd(agRoot, phyId, (void *)sataFis, portId, npipps, linkRate);
      break;
    }
    case OSSA_HW_EVENT_SATA_SPINUP_HOLD:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_SATA_SPINUP_HOLD, PhyID %d\n", phyId));
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_SATA_SPINUP_HOLD, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PHY_DOWN:
    {
      agsaPhy_t *pPhy = &(saRoot->phys[phyId]);

      if(pPhy) {
		osti_memset(&pPhy->sasIdentify,0,sizeof(agsaSASIdentify_t));
      }
      saRoot->phys[phyId].linkstatus &= 1;
      if (agNULL != agPortContext)
      {
        pPort = (agsaPort_t *) (agPortContext->sdkData);
      }

      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_DOWN, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));

      /* callback */
      if ( agNULL != pPort )
      {
        if (OSSA_PORT_VALID == (npipps & PORT_STATE_MASK))
        {
          pPort->status &= ~PORT_INVALIDATING;
          saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_DOWN, PhyID %d  ~PORT_INVALIDATING \n", phyId));
        }
        else
        {
          if (OSSA_PORT_INVALID == (npipps & PORT_STATE_MASK))
          {
            /* set port invalid flag */
            pPort->status |= PORT_INVALIDATING;
            saRoot->PortMap[portId].PortStatus  |= PORT_INVALIDATING;
            SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_DOWN PortInvalid portID %d PortContext %p NPIP 0x%x\n", portId, agPortContext,npipps));
          }
          else
          {
            if (OSSA_PORT_IN_RESET == (npipps & PORT_STATE_MASK))
            {
              SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_DOWN PortInReset portID %d PortContext %p\n", portId, agPortContext));
            }
            else
            {
              SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_DOWN Not PortInReset portID %d PortContext %p\n", portId, agPortContext));
            }
          }
        }

        /* report PhyId, NPIP, PortState */
        phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
        /* Callback with PHY_DOWN */
        ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_DOWN, phyId, agNULL, agNULL);
      }
      else
      {
        /* no portcontext.- error */
        SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_DOWN PhyDown pPort is NULL.\n"));
      }

      /* set PHY_DOWN status */
      PHY_STATUS_SET(pPhy, PHY_DOWN);
      break;
    }
    case OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC:
    {
      agsaPhyErrCountersPage_t errorParam;
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
      errorParam.inboundCRCError = eventParam;
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC, phyId, (void *)&errorParam, agNULL);
      break;
    }
    case OSSA_HW_EVENT_HARD_RESET_RECEIVED:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_HARD_RESET_RECEIVED, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_HARD_RESET_RECEIVED, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD:
    {
      agsaPhyErrCountersPage_t errorParam;
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_LINK_ERR_INVALID_DWORD, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
      errorParam.invalidDword = eventParam;
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD, phyId, (void *)&errorParam, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR:
    {
      agsaPhyErrCountersPage_t errorParam;
      SA_DBG3(("mpiHWEvent: OSSA_HW_EVENT_LINK_ERR_DISPARITY_ERROR, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
      errorParam.runningDisparityError = eventParam;
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR, phyId, (void *)&errorParam, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION:
    {
      agsaPhyErrCountersPage_t errorParam;
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_LINK_ERR_CODE_VIOLATION, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
      errorParam.codeViolation = eventParam;
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION, phyId, (void *)&errorParam, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH:
    {
      agsaPhyErrCountersPage_t errorParam;
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_LINK_ERR_LOSS_OF_DWORD_SYNCH, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
      errorParam.lossOfDwordSynch = eventParam;
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH, phyId, (void *)&errorParam, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
        phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));

      if (agNULL != agPortContext)
      {
        pPort = (agsaPort_t *) (agPortContext->sdkData);
      }
      else
      {
        SA_ASSERT((agPortContext), "agPortContext agNULL was there a PHY UP?");
        return(AGSA_RC_FAILURE);
      }

      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO, phyId, agNULL, agNULL);

      if (OSSA_PORT_VALID == (npipps & PORT_STATE_MASK))
      {
         pPort->status &= ~PORT_INVALIDATING;
         saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
         SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO NOT PORT_INVALIDATING portID %d PortContext %p\n", portId, agPortContext));
      }
      else
      {
        if (OSSA_PORT_INVALID == (npipps & PORT_STATE_MASK))
        {
          /* set port invalid flag */
          pPort->status |= PORT_INVALIDATING;
          saRoot->PortMap[portId].PortStatus  |= PORT_INVALIDATING;
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO PORT_INVALIDATING portID %d PortContext %p\n", portId, agPortContext));
         }
        else
        {
          if (OSSA_PORT_IN_RESET == (npipps & PORT_STATE_MASK))
          {
            SA_DBG1(("mpiHWEvent: PortInReset portID %d PortContext %p\n", portId, agPortContext));
          }
        }
      }
      break;
    }
    case OSSA_HW_EVENT_PORT_RECOVER:
    {
      if (agNULL != agPortContext)
      {
        pPort = (agsaPort_t *) (agPortContext->sdkData);
      }

      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RECOVER, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
        phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));

      if (OSSA_PORT_VALID == (npipps & PORT_STATE_MASK))
      {
        if (agNULL != pPort)
        {
          /* reset port invalid flag */
          pPort->status &= ~PORT_INVALIDATING;
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RECOVER NOT PORT_INVALIDATING portID %d PortContext %p\n", portId, agPortContext));
        }
        saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
      }
      /* get SAS Identify info */
      IDframe = (agsaSASIdentify_t *)&pIomb->sasIdentify;
      /* report PhyId, NPIP, PortState and LinkRate */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16) | (linkRate << SHIFT8);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PORT_RECOVER, phyId, agNULL, (void *)IDframe);
      break;
    }
    case OSSA_HW_EVENT_PHY_STOP_STATUS:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS PhyId=0x%x, status=0x%x eventParam=0x%x\n", phyId, status,eventParam));
      OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaHWEvent_SPC_OUB_t, EVParam));

      switch(eventParam)
      {
        case 0:
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS Stopped 0\n" ));
        break;
        case 1:
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS INVALID_PHY 1\n" ));
        break;
        case 2:
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS DEVICES_ATTACHED 2\n" ));
        break;
        case 3:
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS OTHER_FAILURE 3\n" ));
        break;
        case 4:
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS PHY_NOT_ENABLED 4\n" ));
        break;
        default:
          SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS Unknown code 0x%x\n", eventParam));
          break;
      }

      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_STOP_STATUS phyId 0x%x status 0x%x eventParam 0x%x\n", phyId, status,eventParam));
      /* get request from IOMap */
      pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
      SA_ASSERT((pRequest), "pRequest NULL");
      SA_ASSERT((pRequest->valid), "pRequest->valid");

      agContext = saRoot->IOMap[tag].agContext;

      siPhyStopCB(agRoot, phyId, status, agContext, portId, npipps);

      /* remove the request from IOMap */
      saRoot->IOMap[tag].Tag = MARK_OFF;
      saRoot->IOMap[tag].IORequest = agNULL;
      saRoot->IOMap[tag].agContext = agNULL;

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("mpiHWevent: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      break;
    }
    case OSSA_HW_EVENT_BROADCAST_CHANGE:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_BROADCAST_CHANGE, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_BROADCAST_CHANGE, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_BROADCAST_SES:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_BROADCAST_CHANGE_SES, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_BROADCAST_SES, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_BROADCAST_EXP:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_BROADCAST_EXP, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_BROADCAST_EXP, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_ID_FRAME_TIMEOUT:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_ID_FRAME_TIMEOUT, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_ID_FRAME_TIMEOUT, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PHY_START_STATUS:
    {
      OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaHWEvent_SPC_OUB_t, EVParam)) ;
      /* get request from IOMap */
      pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;

      SA_ASSERT((pRequest), "pRequest");
      if( pRequest == agNULL)
      {
         SA_DBG1(("mpiHWevent: pRequest (%p) NULL\n", pRequest));
         ret = AGSA_RC_FAILURE;
         break;
      }

      agContext = saRoot->IOMap[tag].agContext;

      /* makeup for CB */
      status = (status << 8) | phyId;
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_PHY_START_STATUS, status, agContext, agNULL);

      /* remove the request from IOMap */
      saRoot->IOMap[tag].Tag = MARK_OFF;
      saRoot->IOMap[tag].IORequest = agNULL;
      saRoot->IOMap[tag].agContext = agNULL;

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_ASSERT((pRequest->valid), "pRequest->valid");
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("mpiHWevent: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_START_STATUS, PhyID %d\n", phyId));

      break;
    }
    case OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED:
    {
      agsaPhyErrCountersPage_t errorParam;
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED, PhyID %d PortID %d NPIP 0x%x PS 0x%x\n",
                phyId, portId, (npipps & PHY_IN_PORT_MASK) >> SHIFT4, npipps & PORT_STATE_MASK));
      /* report PhyId, NPIP, PortState */
      si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
      errorParam.phyResetProblem = eventParam;
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED, phyId, (void *)&errorParam, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PORT_RESET_TIMER_TMO:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RESET_TIMER_TMO, PhyID %d PortID %d\n", phyId, portId));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PORT_RESET_TIMER_TMO, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_PORT_RESET_COMPLETE:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_PORT_RESET_COMPLETE, PhyID %d PortID %d\n", phyId, portId));
      /* get SAS Identify info */
      IDframe = (agsaSASIdentify_t *)&pIomb->sasIdentify;
      /* report PhyId, NPIP, PortState and LinkRate */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16) | (linkRate << SHIFT8);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_PORT_RESET_COMPLETE, phyId, agNULL, (void *)IDframe);
      break;
    }
    case OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT, PhyID %d PortID %d\n", phyId, portId));
      /* report PhyId, NPIP, PortState */
      phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT, phyId, agNULL, agNULL);
      break;
    }
    case OSSA_HW_EVENT_IT_NEXUS_LOSS:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_IT_NEXUS_LOSS, PhyID %d PortID %d status 0x%X\n", phyId, portId,status));
      break;
    }
    case OSSA_HW_EVENT_OPEN_RETRY_BACKOFF_THR_ADJUSTED:
    {
      SA_DBG1(("mpiHWEvent: OSSA_HW_EVENT_OPEN_RETRY_BACKOFF_THR_ADJUSTED, PhyID %d PortID %d status 0x%X\n", phyId, portId,status));
      ossaHwCB(agRoot, agPortContext, OSSA_HW_EVENT_OPEN_RETRY_BACKOFF_THR_ADJUSTED, phyId, agNULL, agNULL);
      break;
    }

    default:
    {
      SA_DBG1(("mpiHWEvent: Unknown HW Event 0x%x status 0x%X\n", Event ,status));
      break;
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2j");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SMP Completion
 *
 *  This function handles the SMP completion.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param pIomb        pointer of Message1
 *  \param bc           buffer count
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSMPCompletion(
  agsaRoot_t             *agRoot,
  agsaSMPCompletionRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32               status;
  bit32               tag;
  bit32               param;
  agsaIORequestDesc_t *pRequest;

  SA_DBG3(("mpiSMPCompletion: start, HTAG=0x%x\n", pIomb->tag));

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2k");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSMPCompletionRsp_t, tag)) ;
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSMPCompletionRsp_t, status)) ;
  OSSA_READ_LE_32(AGROOT, &param, pIomb, OSSA_OFFSET_OF(agsaSMPCompletionRsp_t, param)) ;
   /* get SMP request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSMPCompletion: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x PARAM=0x%x\n", tag, status, param));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2k");
    return AGSA_RC_FAILURE;
  }

  switch (status)
  {
  case OSSA_IO_SUCCESS:
    SA_DBG3(("mpiSMPCompletion: OSSA_IO_SUCCESS HTAG = 0x%x\n", tag));
    /* process message */
    siSMPRespRcvd(agRoot, pIomb, param, tag);
    break;

  case OSSA_IO_OVERFLOW:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OVERFLOW HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OVERFLOW++;
    /* SMP failed */
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_ABORTED:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_ABORTED HTAG = 0x%x\n", tag));

    saRoot->IoErrorCount.agOSSA_IO_ABORTED++;
#ifdef SA_PRINTOUT_IN_WINDBG
#ifndef DBG
        DbgPrint("agOSSA_IO_ABORTED  %d\n",  saRoot->IoErrorCount.agOSSA_IO_ABORTED);
#endif /* DBG  */
#endif /* SA_PRINTOUT_IN_WINDBG  */
    /* SMP failed */
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_NO_DEVICE:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_NO_DEVICE HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_NO_DEVICE++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_ERROR_HW_TIMEOUT:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_ERROR_HW_TIMEOUT HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_ERROR_HW_TIMEOUT++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_XFER_ERROR_BREAK:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_XFER_ERROR_BREAK HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_BREAK++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_XFER_ERROR_PHY_NOT_READY:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_XFER_ERROR_PHY_NOT_READY HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_PHY_NOT_READY++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_BREAK:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_BREAK HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_BREAK++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_XFER_ERROR_RX_FRAME:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_XFER_ERROR_RX_FRAME HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_RX_FRAME++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_XFER_OPEN_RETRY_TIMEOUT:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_XFER_OPEN_RETRY_TIMEOUT HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_ERROR_INTERNAL_SMP_RESOURCE++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_PORT_IN_RESET:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_PORT_IN_RESET HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_PORT_IN_RESET++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_DS_NON_OPERATIONAL:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_DS_NON_OPERATIONAL HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_DS_NON_OPERATIONAL++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_DS_IN_RECOVERY:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_DS_IN_RECOVERY HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_DS_IN_RECOVERY++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_ABORT_IN_PROGRESS:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_ABORT_IN_PROGRESS HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_ABORT_IN_PROGRESS++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_ABORT_DELAYED:
    SA_DBG1(("mpiSMPCompletion:OSSA_IO_ABORT_DELAYED  HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_ABORT_DELAYED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_INVALID_LENGTH:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_INVALID_LENGTH HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_INVALID_LENGTH++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_DS_INVALID:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_DS_INVALID HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_DS_INVALID++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_XFER_READ_COMPL_ERR:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_XFER_READ_COMPL_ERR HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_XFER_READ_COMPL_ERR++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
    SA_DBG1(("mpiSMPCompletion: OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_MPI_ERR_OFFLOAD_DIF_OR_ENC_NOT_ENABLED:
    SA_DBG1(("mpiSMPCompletion: OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_MPI_ERR_OFFLOAD_DIF_OR_ENC_NOT_ENABLED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  case OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED:
    SA_DBG1(("mpiSMPCompletion: OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED HTAG = 0x%x\n", tag));
    saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED++;
    siAbnormal(agRoot, pRequest, status, 0, 0);
    break;

  default:
    SA_DBG1(("mpiSMPCompletion: Unknown Status = 0x%x Tag 0x%x\n", status, tag));
    saRoot->IoErrorCount.agOSSA_IO_UNKNOWN_ERROR++;
    /* not allowed case. Therefore, assert */
    SA_ASSERT((agFALSE), "mpiSMPCompletion: Unknown Status");
    break;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2k");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Get Device Handle Command Response
 *
 *  This function handles the response of Get Device Handle Command.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param pIomb        pointer of Message
 *  \param bc           buffer count
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDevHandleRsp(
  agsaRoot_t             *agRoot,
  agsaGetDevHandleRsp_t  *pIomb
  )
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaPortContext_t   *agPortContext;
  agsaContext_t       *agContext;
  agsaDeviceDesc_t    *pDevice;
  bit8 portId;
  bit32 deviceid=0, deviceIdc, i;
  bit32 DeviceIdcPortId, tag;

  SA_DBG3(("mpiGetDevHandleRsp: start, HTAG=0x%x\n", pIomb->tag));

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2m");

  OSSA_READ_LE_32(AGROOT, &DeviceIdcPortId, pIomb, OSSA_OFFSET_OF(agsaGetDevHandleRsp_t, DeviceIdcPortId)) ;
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetDevHandleRsp_t, tag)) ;
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetDevHandleRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x DeviceIdcPortId=0x%x\n", tag, DeviceIdcPortId));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2m");
    return AGSA_RC_FAILURE;
  }

  /* get port context */
  portId = (bit8)(DeviceIdcPortId & PORTID_MASK);
  SA_DBG2(("mpiGetDevHandleRsp:PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[portId & PORTID_MASK].PortID,saRoot->PortMap[portId & PORTID_MASK].PortStatus,saRoot->PortMap[portId & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId].PortContext;

  /* get Device ID count */
  deviceIdc = (bit8)((DeviceIdcPortId & DEVICE_IDC_BITS) >> SHIFT8);

  /* based on the deviceIDC to get all device handles */
  for (i = 0; i < deviceIdc; i++)
  {
    OSSA_READ_LE_32(AGROOT, &deviceid, pIomb, OSSA_OFFSET_OF(agsaGetDevHandleRsp_t, deviceId[i])) ;
    /* find device handle from device index */
    pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle;
    if (pDevice->targetDevHandle.sdkData)
     saRoot->DeviceHandle[i] = &(pDevice->targetDevHandle);
    else
     saRoot->DeviceHandle[i] = &(pDevice->initiatorDevHandle);
  }

  SA_DBG1(("mpiGetDevHandleRsp:deviceid 0x%x  0x%x\n",deviceid, (deviceid & DEVICE_ID_BITS)));
  /* call back oslayer */
  ossaGetDeviceHandlesCB(agRoot, agContext, agPortContext, saRoot->DeviceHandle, deviceIdc);

  /* return the request to free pool */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetDevHandleRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2m");

  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Phy Control Command Response
 *
 *  This function handles the response of PHY Control Command.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiPhyCntrlRsp(
  agsaRoot_t             *agRoot,
  agsaLocalPhyCntrlRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext = agNULL;
  bit32               phyId, operation, status, tag, phyOpId;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2n");

  SA_DBG3(("mpiPhyCntrlRsp: start, HTAG=0x%x,\n", pIomb->tag));

  /* get tag */
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaLocalPhyCntrlRsp_t, tag)) ;
  OSSA_READ_LE_32(AGROOT, &phyOpId, pIomb, OSSA_OFFSET_OF(agsaLocalPhyCntrlRsp_t, phyOpId)) ;
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaLocalPhyCntrlRsp_t, status)) ;
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiPhyCntrlRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x PhyOpId=0x%x\n", tag, status, phyOpId));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2n");
    return AGSA_RC_FAILURE;
  }
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  phyId = phyOpId & LOCAL_PHY_PHYID;
  operation = (phyOpId & LOCAL_PHY_OP_BITS) >> SHIFT8;


  SA_DBG3(("mpiPhyCntrlRsp: phyId=0x%x Operation=0x%x Status=0x%x\n", phyId, operation, status));

  if( pRequest->completionCB == agNULL )
  {
    /* call back with the status */
    ossaLocalPhyControlCB(agRoot, agContext, phyId, operation, status, agNULL);
  }
  else
  {
    (*(ossaLocalPhyControlCB_t)(pRequest->completionCB))(agRoot, agContext, phyId, operation, status, agNULL );
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiPhyCntrlRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2n");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Device Register Command Response
 *
 *  This function handles the response of Device Register Command.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDeviceRegRsp(
  agsaRoot_t    *agRoot,
  agsaDeviceRegistrationRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = agNULL;
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               deviceId;
  agsaDeviceDesc_t    *pDevice = agNULL;
  agsaDeviceDesc_t    *pDeviceRemove = agNULL;
  bit32               deviceIdx,status, tag;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2p");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  SA_DBG3(("mpiDeviceRegRsp: start, HTAG=0x%x\n", pIomb->tag));

  SA_ASSERT((NULL != saRoot->DeviceRegistrationCB), "DeviceRegistrationCB can not be NULL");
  OSSA_READ_LE_32(AGROOT, &deviceId, pIomb, OSSA_OFFSET_OF(agsaDeviceRegistrationRsp_t, deviceId)) ;
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaDeviceRegistrationRsp_t, tag)) ;
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaDeviceRegistrationRsp_t, status)) ;

  SA_DBG1(("mpiDeviceRegRsp: deviceID 0x%x \n", deviceId));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiDeviceRegRsp: Bad IOMB!!! pRequest is NULL. TAG=0x%x, STATUS=0x%x DEVICEID=0x%x\n", tag, status, deviceId));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2p");
    return AGSA_RC_FAILURE;
  }

  pDevice = pRequest->pDevice;

  agContext = saRoot->IOMap[tag].agContext;
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  /* get Device Id or status */
  SA_DBG3(("mpiDeviceRegRsp: hosttag 0x%x\n", tag));
  SA_DBG3(("mpiDeviceRegRsp: deviceID 0x%x Device Context %p\n", deviceId, pDevice));

  if (agNULL == pDevice)
  {
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiDeviceRegRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("mpiDeviceRegRsp: warning!!! no device is found\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2p");
    return AGSA_RC_FAILURE;
  }

  if (agNULL == saRoot->DeviceRegistrationCB)
  {
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiDeviceRegRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("mpiDeviceRegRsp: warning!!! no DeviceRegistrationCB is found\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2p");
    return AGSA_RC_FAILURE;
  }


  if(smIS_SPCV(agRoot))
  {
    switch( status)
    {
      case 0:
          status = OSSA_SUCCESS;
          break;
      case MPI_ERR_DEVICE_HANDLE_UNAVAILABLE:
          status = OSSA_FAILURE_OUT_OF_RESOURCE;
          break;
      case MPI_ERR_DEVICE_ALREADY_REGISTERED:
          status = OSSA_FAILURE_DEVICE_ALREADY_REGISTERED;
          break;
      case MPI_ERR_PHY_ID_INVALID:
          status = OSSA_FAILURE_INVALID_PHY_ID;
          break;
      case MPI_ERR_PHY_ID_ALREADY_REGISTERED:
          status = OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED;
          break;
      case MPI_ERR_PORT_INVALID_PORT_ID:
          status = OSSA_FAILURE_PORT_ID_OUT_OF_RANGE;
          break;
      case MPI_ERR_PORT_STATE_NOT_VALID:
          status = OSSA_FAILURE_PORT_NOT_VALID_STATE;
          break;
      case MPI_ERR_DEVICE_TYPE_NOT_VALID:
          status = OSSA_FAILURE_DEVICE_TYPE_NOT_VALID;
          break;
      default:
        SA_ASSERT((0), "DeviceRegistration Unknown status");
        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        pRequest->valid = agFALSE;
        /* return the request to free pool */
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("mpiDeviceRegRsp: saving pRequest (%p) for later use\n", pRequest));
          saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
        }
        else
        {
          /* return the request to free pool */
          saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
        }
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

        return AGSA_RC_FAILURE;
    }
  }

  switch (status)
  {
  case OSSA_SUCCESS:
    /* mapping the device handle and device id */
    deviceIdx = deviceId & DEVICE_ID_BITS;
    OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
    saRoot->DeviceMap[deviceIdx].DeviceIdFromFW = deviceId;
    saRoot->DeviceMap[deviceIdx].DeviceHandle = (void *)pDevice;
    pDevice->DeviceMapIndex = deviceId;

    (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                  agContext,
                                                                  OSSA_SUCCESS,
                                                                  &pDevice->targetDevHandle,
                                                                  deviceId
                                                                  );

    break;
  case OSSA_FAILURE_OUT_OF_RESOURCE:
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_OUT_OF_RESOURCE\n"));
    /* remove device from LL device list */
    siPortDeviceRemove(agRoot, pDevice->pPort, pDevice, agFALSE);

    /* call ossaDeviceRegistrationCB_t */
    (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                  agContext,
                                                                  OSSA_FAILURE_OUT_OF_RESOURCE,
                                                                  &pDevice->targetDevHandle,
                                                                  deviceId
                                                                  );


    break;
  case OSSA_FAILURE_DEVICE_ALREADY_REGISTERED:
    /* get original device handle and device id */
    pDeviceRemove = pDevice;
    deviceIdx = deviceId & DEVICE_ID_BITS;
    OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
    pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceIdx].DeviceHandle;
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_DEVICE_ALREADY_REGISTERED, existing deviceContext %p\n", pDevice));
    /* no auto registration */
    if (pDevice != agNULL)
    {
      /* remove device from LL device list */
      siPortDeviceListRemove(agRoot, pDevice->pPort, pDeviceRemove);

      /* call ossaDeviceRegistrationCB_t */
      (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                    agContext,
                                                                    OSSA_FAILURE_DEVICE_ALREADY_REGISTERED,
                                                                    &pDevice->targetDevHandle,
                                                                    deviceId
                                                                    );
    }
    else
    {
      SA_DBG1(("mpiDeviceRegRsp: pDevice is NULL. TAG=0x%x, STATUS=0x%x DEVICEID=0x%x\n", tag, status, deviceId));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2p");
      return AGSA_RC_FAILURE;
    }

    break;
  case OSSA_FAILURE_INVALID_PHY_ID:
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_INVALID_PHY_ID\n"));
    /* remove device from LL device list */
    siPortDeviceRemove(agRoot, pDevice->pPort, pDevice, agFALSE);

    /* call ossaDeviceRegistrationCB_t */
    (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                  agContext,
                                                                  OSSA_FAILURE_INVALID_PHY_ID,
                                                                  &pDevice->targetDevHandle,
                                                                  deviceId
                                                                  );
    break;
  case OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED:
    /* get original device handle and device id */
    pDeviceRemove = pDevice;
    deviceIdx = deviceId & DEVICE_ID_BITS;
    OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
    pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceIdx].DeviceHandle;
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED, existing deviceContext %p\n", pDevice));
    /* no auto registration */
    if (pDevice != agNULL)
    {
      /* remove device from LL device list */
      siPortDeviceListRemove(agRoot, pDevice->pPort, pDeviceRemove);

      /* call ossaDeviceRegistrationCB_t */
      (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                    agContext,
                                                                    OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED,
                                                                    &pDevice->targetDevHandle,
                                                                    deviceId
                                                                    );
    }
    else
    {
      SA_DBG1(("mpiDeviceRegRsp: pDevice is NULL. TAG=0x%x, STATUS=0x%x DEVICEID=0x%x\n", tag, status, deviceId));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2p");
      return AGSA_RC_FAILURE;
    }

    break;
  case OSSA_FAILURE_PORT_ID_OUT_OF_RANGE:
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_OUT_OF_RESOURCE\n"));
    /* remove device from LL device list */
    siPortDeviceRemove(agRoot, pDevice->pPort, pDevice, agFALSE);

    /* call ossaDeviceRegistrationCB_t */
    (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                  agContext,
                                                                  OSSA_FAILURE_PORT_ID_OUT_OF_RANGE,
                                                                  &pDevice->targetDevHandle,
                                                                  deviceId
                                                                  );
    break;
  case OSSA_FAILURE_PORT_NOT_VALID_STATE:
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_PORT_NOT_VALID_STATE\n"));
    /* remove device from LL device list */
    siPortDeviceRemove(agRoot, pDevice->pPort, pDevice, agFALSE);

    /* call ossaDeviceRegistrationCB_t */
    (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                  agContext,
                                                                  OSSA_FAILURE_PORT_NOT_VALID_STATE,
                                                                  &pDevice->targetDevHandle,
                                                                  deviceId
                                                                  );
    break;
  case OSSA_FAILURE_DEVICE_TYPE_NOT_VALID:
    SA_DBG1(("mpiDeviceRegRsp: OSSA_FAILURE_DEVICE_TYPE_NOT_VALID\n"));
    /* remove device from LL device list */
    siPortDeviceRemove(agRoot, pDevice->pPort, pDevice, agFALSE);
    /* call ossaDeviceRegistrationCB_t */
    (*(ossaDeviceRegistrationCB_t)(saRoot->DeviceRegistrationCB))(agRoot,
                                                                  agContext,
                                                                  OSSA_FAILURE_DEVICE_TYPE_NOT_VALID,
                                                                  &pDevice->targetDevHandle,
                                                                  deviceId
                                                                  );
    break;
  default:
    SA_DBG3(("mpiDeviceRegRsp, unknown status in response %d\n", status));
    break;
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiDeviceRegRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "2p");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Deregister Device Command Response
 *
 *  This function handles the response of Deregister Command.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDeregDevHandleRsp(
  agsaRoot_t              *agRoot,
  agsaDeregDevHandleRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDevHandle_t     *agDevHandle;
  agsaContext_t       *agContext;
  agsaDeviceDesc_t    *pDevice;
  bit32               deviceIdx, status, tag;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2r");
  SA_ASSERT((NULL != saRoot->DeviceDeregistrationCB), "DeviceDeregistrationCB can not be NULL");

  SA_DBG3(("mpiDeregDevHandleRsp: start, HTAG=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaDeregDevHandleRsp_t, tag)) ;
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaDeregDevHandleRsp_t, status)) ;
  OSSA_READ_LE_32(AGROOT, &deviceIdx, pIomb, OSSA_OFFSET_OF(agsaDeregDevHandleRsp_t, deviceId)) ;
  /* get request from IOMap */

  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiDeregDevHandleRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x deviceIdx 0x%x\n", tag, status,deviceIdx));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2r");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  pDevice = pRequest->pDevice;
  if (pDevice != agNULL)
  {
    if (pDevice->targetDevHandle.sdkData)
    {
      agDevHandle = &(pDevice->targetDevHandle);
    }
    else
    {
      agDevHandle = &(pDevice->initiatorDevHandle);
    }
  }
  else
  {
    SA_DBG1(("mpiDeregDevHandleRsp: pDevice is NULL"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2r");
    return AGSA_RC_FAILURE;
  }

  if (agNULL == agDevHandle)
  {
    SA_DBG1(("mpiDeregDevHandleRsp: warning!!! no deviceHandle is found"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2r");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiDeregDevHandleRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  SA_DBG1(("mpiDeregDevHandleRsp: deviceID 0x%x Device Context %p\n", pDevice->DeviceMapIndex, pDevice));

  if (agNULL == saRoot->DeviceDeregistrationCB)
  {
    SA_DBG1(("mpiDeregDevHandleRsp: warning!!! no DeviceDeregistrationCB is found"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2r");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiDeregDevHandleRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  switch (status)
  {
    case OSSA_SUCCESS:
     (*(ossaDeregisterDeviceHandleCB_t)(saRoot->DeviceDeregistrationCB))(agRoot,
                                                                agContext,
                                                                agDevHandle,
                                                                OSSA_SUCCESS
                                                                );
      siRemoveDevHandle(agRoot, agDevHandle);
      break;
    case OSSA_ERR_DEVICE_HANDLE_INVALID:
    case OSSA_INVALID_HANDLE:
      (*(ossaDeregisterDeviceHandleCB_t)(saRoot->DeviceDeregistrationCB))(agRoot,
                                                                agContext,
                                                                agDevHandle,
                                                                status
                                                                );
// already removed and no device to remove
//      siRemoveDevHandle(agRoot, agDevHandle);
      SA_DBG1(("mpiDeregDevRegRsp, OSSA_INVALID_HANDLE status in response %d\n", status));
      break;
    case OSSA_ERR_DEVICE_BUSY:
      (*(ossaDeregisterDeviceHandleCB_t)(saRoot->DeviceDeregistrationCB))(agRoot,
                                                                agContext,
                                                                agDevHandle,
                                                                status
                                                                );
      SA_DBG1(("mpiDeregDevRegRsp, OSSA_ERR_DEVICE_BUSY status in response %d\n", status));
      ret = AGSA_RC_BUSY;
      break;
    default:
      SA_DBG1(("mpiDeregDevRegRsp, unknown status in response 0x%X\n", status));
      break;
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiDeregDevHandleRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2r");
  return ret;
}


/******************************************************************************/
/*! \brief Get Phy Profile Response SPCv
 *
 *  This routine handles the response of Get Phy Profile Command Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Message
 *
 *  \return sucess or fail
 *  SPC  only
 */
/*******************************************************************************/

GLOBAL bit32 mpiGetPhyProfileRsp(
  agsaRoot_t             *agRoot,
  agsaGetPhyProfileRspV_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32              status, tag;

  bit32          Reserved_SOP_PHYID;
  bit32          PhyId;
  bit32          SOP;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2J");
  OSSA_READ_LE_32(agRoot, &status, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t, status));
  OSSA_READ_LE_32(agRoot, &tag, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t, tag));
  /* get TAG */
  SA_DBG1(("mpiGetPhyProfileRsp: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetPhyProfileRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2J");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  OSSA_READ_LE_32(agRoot, &Reserved_SOP_PHYID, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,Reserved_Ppc_SOP_PHYID ));

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");
  SA_DBG1(("mpiGetPhyProfileRsp:   %p\n",pIomb));
  SA_DBG1(("mpiGetPhyProfileRsp: completionCB %p\n",pRequest->completionCB ));

  SOP = (Reserved_SOP_PHYID & 0xFF00) >> SHIFT8;
  PhyId = Reserved_SOP_PHYID & 0xFF;

  /* check status success or failure */
  if (status)
  {
    /* status is FAILED */
    SA_DBG1(("mpiGetPhyProfileRsp:AGSA_RC_FAILURE  0x%08X\n", status));
    switch(SOP)
    {
      case AGSA_SAS_PHY_ERR_COUNTERS_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_ERR_COUNTERS_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_SAS_PHY_BW_COUNTERS_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: GET_SAS_PHY_BW_COUNTERS SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_SAS_PHY_GENERAL_STATUS_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_GENERAL_STATUS_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_PHY_SNW3_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_PHY_SNW3_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_PHY_RATE_CONTROL_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_PHY_RATE_CONTROL_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      case AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL );
        break;
      }
      default:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: undefined SOP 0x%x\n", SOP));
        break;
      }
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2J");
    return AGSA_RC_FAILURE;
  }
  else
  {
    SA_DBG1(("mpiGetPhyProfileRsp: SUCCESS type 0x%X\n",SOP ));
    switch(SOP)
    {
      case AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE:
        /* call back with the status */
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE PhyId %d\n",PhyId));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , agNULL);
        break;
      case AGSA_SAS_PHY_ERR_COUNTERS_PAGE:
      {

        agsaPhyErrCountersPage_t Errors;

        OSSA_READ_LE_32(agRoot, &Errors.invalidDword,          pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &Errors.runningDisparityError, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));
        OSSA_READ_LE_32(agRoot, &Errors.codeViolation,         pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[2] ));
        OSSA_READ_LE_32(agRoot, &Errors.lossOfDwordSynch,      pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[3] ));
        OSSA_READ_LE_32(agRoot, &Errors.phyResetProblem,       pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[4] ));
        OSSA_READ_LE_32(agRoot, &Errors.inboundCRCError,       pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[5] ));

        /* call back with the status */
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , &Errors);
        /* status is SUCCESS */

        SA_DBG3(("mpiGetPhyProfileRsp: pIomb %p\n",pIomb));
        SA_DBG1(("mpiGetPhyProfileRsp: Reserved_SOP_PHYID    0x%08X\n",Reserved_SOP_PHYID));
        SA_DBG1(("mpiGetPhyProfileRsp: invalidDword          0x%08X\n",Errors.invalidDword ));
        SA_DBG1(("mpiGetPhyProfileRsp: runningDisparityError 0x%08X\n",Errors.runningDisparityError ));
        SA_DBG1(("mpiGetPhyProfileRsp: codeViolation         0x%08X\n",Errors.codeViolation ));
        SA_DBG1(("mpiGetPhyProfileRsp: lossOfDwordSynch      0x%08X\n",Errors.lossOfDwordSynch ));
        SA_DBG1(("mpiGetPhyProfileRsp: phyResetProblem       0x%08X\n",Errors.phyResetProblem ));
        SA_DBG1(("mpiGetPhyProfileRsp: inboundCRCError       0x%08X\n",Errors.inboundCRCError ));
        break;

      }
      case AGSA_SAS_PHY_BW_COUNTERS_PAGE:
      {

        agsaPhyBWCountersPage_t  bw_counts;
        OSSA_READ_LE_32(agRoot, &bw_counts.TXBWCounter, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &bw_counts.RXBWCounter, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));

        SA_DBG1(("mpiGetPhyProfileRsp: GET_SAS_PHY_BW_COUNTERS TX 0x%08X RX 0x%08X\n",bw_counts.TXBWCounter,bw_counts.RXBWCounter));
        /* call back with the status */
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, &bw_counts);
        break;
      }
      case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
      {
        agsaPhyAnalogSettingsPage_t analog;

        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE 0x%X\n",SOP));
        OSSA_READ_LE_32(agRoot, &analog.Dword0, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword1, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword2, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[2] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword3, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[3] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword4, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[4] ));
          /* call back with the status */
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, &analog);
        break;
      }

      case AGSA_SAS_PHY_GENERAL_STATUS_PAGE:
      {
        agsaSASPhyGeneralStatusPage_t GenStatus;
        OSSA_READ_LE_32(agRoot, &GenStatus.Dword0, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &GenStatus.Dword1, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_GENERAL_STATUS_PAGE SOP 0x%x 0x%x 0x%x\n", SOP,GenStatus.Dword0,GenStatus.Dword1));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , &GenStatus );
        break;
      }
      case AGSA_PHY_SNW3_PAGE:
      {
        agsaPhySNW3Page_t Snw3;
        OSSA_READ_LE_32(agRoot, &Snw3.LSNW3, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &Snw3.RSNW3, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));

        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_PHY_SNW3_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , &Snw3 );
        break;
      }
      case AGSA_PHY_RATE_CONTROL_PAGE:
      {
        agsaPhyRateControlPage_t RateControl;
        OSSA_READ_LE_32(agRoot, &RateControl.Dword0, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &RateControl.Dword1, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));
        OSSA_READ_LE_32(agRoot, &RateControl.Dword2, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[2] ));
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_PHY_RATE_CONTROL_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , &RateControl );
        break;
      }
      case AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE:
      {
        agsaSASPhyOpenRejectRetryBackOffThresholdPage_t Backoff;
        OSSA_READ_LE_32(agRoot, &Backoff.Dword0, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &Backoff.Dword1, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[1] ));
        OSSA_READ_LE_32(agRoot, &Backoff.Dword2, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[2] ));
        OSSA_READ_LE_32(agRoot, &Backoff.Dword3, pIomb, OSSA_OFFSET_OF(agsaGetPhyProfileRspV_t,PageSpecificArea[3] ));
        SA_DBG1(("mpiGetPhyProfileRsp: AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE SOP 0x%x\n", SOP));
        ossaGetPhyProfileCB(agRoot, agContext, status, SOP, PhyId , &Backoff );
        break;
      }
      default:
      {
        SA_DBG1(("mpiGetPhyProfileRsp: undefined successful SOP 0x%x\n", SOP));
        break;
      }

    }
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetPhyProfileRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2J");
  return ret;
}


GLOBAL bit32 mpiSetPhyProfileRsp(
  agsaRoot_t             *agRoot,
  agsaSetPhyProfileRspV_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32                status, tag;

  bit32           Reserved_Ppc_PHYID;
  bit32           PhyId;
  bit16           SOP;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2Q");
  OSSA_READ_LE_32(agRoot, &status, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t, status));
  OSSA_READ_LE_32(agRoot, &tag, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t, tag));
  OSSA_READ_LE_32(agRoot, &Reserved_Ppc_PHYID, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t, Reserved_Ppc_PHYID));
  /* get TAG */
  SA_DBG1(("mpiSetPhyProfileRsp: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSetPhyProfileRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2Q");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_DBG1(("mpiSetPhyProfileRsp:   %p\n",pIomb));

  SOP = pRequest->SOP;
  PhyId = Reserved_Ppc_PHYID & 0xFF;

  /* check status success or failure */
  if (status)
  {
    /* status is FAILED */
    SA_DBG1(("mpiSetPhyProfileRsp:AGSA_RC_FAILURE  0x%08X\n", status));
    switch(SOP)
    {
      case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE SOP 0x%x\n", SOP));
        ossaSetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, agNULL );
        break;
      }
      case AGSA_PHY_SNW3_PAGE:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: AGSA_PHY_SNW3_PAGE SOP 0x%x\n", SOP));
        ossaSetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, agNULL );
        break;
      }

      case AGSA_PHY_RATE_CONTROL_PAGE:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: AGSA_PHY_RATE_CONTROL_PAGE SOP 0x%x\n", SOP));
        ossaSetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, agNULL );
        break;
      }
     case AGSA_SAS_PHY_MISC_PAGE:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: AGSA_SAS_PHY_MISC_PAGE SOP 0x%x\n", SOP));
        ossaSetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, agNULL );
        break;
      }

      default:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: undefined SOP 0x%x\n", SOP));
        break;
      }
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2Q");
    return AGSA_RC_FAILURE;
  }
  else
  {
    SA_DBG1(("mpiSetPhyProfileRsp: SUCCESS type 0x%X\n",SOP ));
    switch(SOP)
    {
      case AGSA_PHY_SNW3_PAGE:
      case AGSA_PHY_RATE_CONTROL_PAGE:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: Status 0x%x SOP 0x%x PhyId %d\n",status, SOP, PhyId));
        ossaSetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, agNULL );
        break;

      }
      case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
      {
        agsaPhyAnalogSettingsPage_t analog;

        SA_DBG1(("mpiSetPhyProfileRsp: AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE 0x%X\n",SOP));
        OSSA_READ_LE_32(agRoot, &analog.Dword0, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t,PageSpecificArea[0] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword1, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t,PageSpecificArea[1] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword2, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t,PageSpecificArea[2] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword3, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t,PageSpecificArea[3] ));
        OSSA_READ_LE_32(agRoot, &analog.Dword4, pIomb, OSSA_OFFSET_OF(agsaSetPhyProfileRspV_t,PageSpecificArea[4] ));
          /* call back with the status */
        ossaSetPhyProfileCB(agRoot, agContext, status, SOP, PhyId, &analog );
        break;
      }
      default:
      {
        SA_DBG1(("mpiSetPhyProfileRsp: undefined successful SOP 0x%x\n", SOP));
        break;
      }

    }
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  SA_DBG1(("mpiSetPhyProfileRsp: completionCB %p\n",pRequest->completionCB ));

  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSetPhyProfileRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2Q");
  return ret;
}



/******************************************************************************/
/*! \brief Get Device Information Response
 *
 *  This routine handles the response of Get Device Info Command Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Message
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDevInfoRsp(
  agsaRoot_t          *agRoot,
  agsaGetDevInfoRspV_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  agsaContext_t       *agContext;
  agsaDeviceInfo_t    commonDevInfo;
  bit32               ARSrateSMPTimeOutPortID, IRMcnITNexusTimeOut, status, tag;
  bit32               deviceid;
  bit32               sasAddrHi;
  bit32               sasAddrLow;
#if defined(SALLSDK_DEBUG)
  bit32               option;
#endif /* SALLSDK_DEBUG */

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2M");
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRspV_t, status));
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRspV_t, tag));
  /* get TAG */
  SA_DBG3(("mpiGetDevInfoRsp: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetDevInfoRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2M");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* check status success or failure */
  if (status)
  {
    /* status is FAILED */
    ossaGetDeviceInfoCB(agRoot, agContext, agNULL, OSSA_DEV_INFO_INVALID_HANDLE, agNULL);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2M");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
#if defined(SALLSDK_DEBUG)
    option = (bit32)pRequest->DeviceInfoCmdOption;
#endif /* SALLSDK_DEBUG */
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiGetDevInfoRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  /* status is SUCCESS */
  OSSA_READ_LE_32(AGROOT, &deviceid, pIomb,                OSSA_OFFSET_OF(agsaGetDevInfoRspV_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &ARSrateSMPTimeOutPortID, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRspV_t, ARSrateSMPTimeOutPortID));
  OSSA_READ_LE_32(AGROOT, &IRMcnITNexusTimeOut, pIomb,       OSSA_OFFSET_OF(agsaGetDevInfoRspV_t, IRMcnITNexusTimeOut));
  OSSA_READ_LE_32(AGROOT, &sasAddrHi, pIomb,       OSSA_OFFSET_OF(agsaGetDevInfoRspV_t,sasAddrHi[0] ));
  OSSA_READ_LE_32(AGROOT, &sasAddrLow, pIomb,       OSSA_OFFSET_OF(agsaGetDevInfoRspV_t,sasAddrLow[0] ));

  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle;
  if (pDevice != agNULL)
  {
    if (pDevice->targetDevHandle.sdkData)
    {
      agDevHandle = &(pDevice->targetDevHandle);
    }
    else
    {
      agDevHandle = &(pDevice->initiatorDevHandle);
    }
  }
  else
  {
    SA_DBG1(("mpiGetDevInfoRsp: pDevice is NULL"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2M");
    return AGSA_RC_FAILURE;
  }

  if (agDevHandle == agNULL)
  {
    SA_DBG1(("mpiGetDevInfoRsp: warning!!! no deviceHandle is found"));
    ossaGetDeviceInfoCB(agRoot, agContext, agNULL, OSSA_DEV_INFO_INVALID_HANDLE, agNULL);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2M");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
#if defined(SALLSDK_DEBUG)
    option = (bit32)pRequest->DeviceInfoCmdOption;
#endif /* SALLSDK_DEBUG */
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiGetDevInfoRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  /* setup common device information */
  si_memset(&commonDevInfo, 0, sizeof(agsaDeviceInfo_t));
  commonDevInfo.smpTimeout       = (bit16)((ARSrateSMPTimeOutPortID >> SHIFT8 ) & SMPTO_VBITS);
  commonDevInfo.it_NexusTimeout  = (bit16)(IRMcnITNexusTimeOut & NEXUSTO_VBITS);
  commonDevInfo.firstBurstSize   = (bit16)((IRMcnITNexusTimeOut >> SHIFT16) & FIRST_BURST_MCN);
  commonDevInfo.devType_S_Rate   = (bit8)((ARSrateSMPTimeOutPortID >> SHIFT24) & 0x3f);
  commonDevInfo.flag = (bit32)((ARSrateSMPTimeOutPortID >> SHIFT30 ) & FLAG_VBITS);
  commonDevInfo.flag |= IRMcnITNexusTimeOut & 0xf0000;
  if (IRMcnITNexusTimeOut & 0x1000000)
  {
    commonDevInfo.flag |= 0x100000;
  }

  /* check SAS device then copy SAS Address */
  if ( ((ARSrateSMPTimeOutPortID & DEV_TYPE_BITS) >> SHIFT28 == 0x00) ||
       ((ARSrateSMPTimeOutPortID & DEV_TYPE_BITS) >> SHIFT28 == 0x01)) 
  {
    /* copy the sasAddressHi byte-by-byte : no endianness */
    commonDevInfo.sasAddressHi[0] = pIomb->sasAddrHi[0];
    commonDevInfo.sasAddressHi[1] = pIomb->sasAddrHi[1];
    commonDevInfo.sasAddressHi[2] = pIomb->sasAddrHi[2];
    commonDevInfo.sasAddressHi[3] = pIomb->sasAddrHi[3];

    /* copy the sasAddressLow byte-by-byte : no endianness */
    commonDevInfo.sasAddressLo[0] = pIomb->sasAddrLow[0];
    commonDevInfo.sasAddressLo[1] = pIomb->sasAddrLow[1];
    commonDevInfo.sasAddressLo[2] = pIomb->sasAddrLow[2];
    commonDevInfo.sasAddressLo[3] = pIomb->sasAddrLow[3];
  }

  /* copy common device information to SAS and SATA device common header*/
  si_memcpy(&pDevice->devInfo.sasDeviceInfo.commonDevInfo, &commonDevInfo, sizeof(agsaDeviceInfo_t));
  si_memcpy(&pDevice->devInfo.sataDeviceInfo.commonDevInfo, &commonDevInfo, sizeof(agsaDeviceInfo_t));

  /* setup device firstBurstSize infomation */
  pDevice->devInfo.sataDeviceInfo.commonDevInfo.firstBurstSize =
       (bit16)((IRMcnITNexusTimeOut >> SHIFT16) & FIRST_BURST);

  /* Display Device Information */
  SA_DBG3(("mpiGetDevInfoRsp: smpTimeout=0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.smpTimeout));
  SA_DBG3(("mpiGetDevInfoRsp: it_NexusTimeout=0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.it_NexusTimeout));
  SA_DBG3(("mpiGetDevInfoRsp: firstBurstSize=0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.firstBurstSize));
  SA_DBG3(("mpiGetDevInfoRsp: devType_S_Rate=0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate));

  /*
  D518 P2I[15-12]: Disk  HP      DG0146FAMWL     , HPDE, WWID=5000c500:17459a31, 6.0G
  */

  SA_DBG1(("mpiGetDevInfoRsp: Device 0x%08X flag 0x%08X %s WWID= %02x%02x%02x%02x:%02x%02x%02x%02x, %s\n",
    deviceid,
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.flag,
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x20 ? "SATA DA" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x10 ? "SSP/SMP" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x0 ? "  STP  " : "Unknown",

    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[3],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[2],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[1],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[0],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[3],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[2],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[1],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[0],

    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 8  ? " 1.5G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 9  ? " 3.0G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 10 ? " 6.0G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 11 ? "12.0G" : "????" ));

  ossaGetDeviceInfoCB(agRoot, agContext, agDevHandle, OSSA_DEV_INFO_NO_EXTENDED_INFO, &commonDevInfo);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
#if defined(SALLSDK_DEBUG)
  option = (bit32)pRequest->DeviceInfoCmdOption;
#endif /* SALLSDK_DEBUG */
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetDevInfoRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2M");
  return ret;
}

/******************************************************************************/
/*! \brief Get Device Information Response
 *
 *  This routine handles the response of Get Device Info Command Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Message
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDevInfoRspSpc(
  agsaRoot_t          *agRoot,
  agsaGetDevInfoRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  agsaContext_t       *agContext;
  bit32               dTypeSrateSMPTOPortID, FirstBurstSizeITNexusTimeOut, status, tag;
  bit32               deviceid;
  bit32               sasAddrHi;
  bit32               sasAddrLow;
  bit32               Info_avail = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2t");
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, tag));
  /* get TAG */
  SA_DBG3(("mpiGetDevInfoRspSpc: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetDevInfoRspSpc: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2t");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* check status success or failure */
  if (status)
  {
    /* status is FAILED */
    ossaGetDeviceInfoCB(agRoot, agContext, agNULL, OSSA_DEV_INFO_INVALID_HANDLE, agNULL);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2t");
    return AGSA_RC_FAILURE;
  }

  /* status is SUCCESS */
  OSSA_READ_LE_32(AGROOT, &deviceid, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &dTypeSrateSMPTOPortID, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, dTypeSrateSMPTOArPortID));
  OSSA_READ_LE_32(AGROOT, &FirstBurstSizeITNexusTimeOut, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, FirstBurstSizeITNexusTimeOut));
  OSSA_READ_LE_32(AGROOT, &sasAddrHi, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, sasAddrHi[0]));
  OSSA_READ_LE_32(AGROOT, &sasAddrLow, pIomb, OSSA_OFFSET_OF(agsaGetDevInfoRsp_t, sasAddrLow[0]));


  SA_DBG2(("mpiGetDevInfoRspSpc:deviceid                     0x%08X\n",deviceid));
  SA_DBG2(("mpiGetDevInfoRspSpc:dTypeSrateSMPTOPortID        0x%08X\n",dTypeSrateSMPTOPortID));
  SA_DBG2(("mpiGetDevInfoRspSpc:FirstBurstSizeITNexusTimeOut 0x%08X\n",FirstBurstSizeITNexusTimeOut));
  SA_DBG2(("mpiGetDevInfoRspSpc:sasAddrHi                    0x%08X\n",sasAddrHi));
  SA_DBG2(("mpiGetDevInfoRspSpc:sasAddrLow                   0x%08X\n",sasAddrLow));


  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle;
  if (pDevice != agNULL)
  {
    if (pDevice->targetDevHandle.sdkData)
    {
      agDevHandle = &(pDevice->targetDevHandle);
    }
    else
    {
      agDevHandle = &(pDevice->initiatorDevHandle);
    }
  }
  else
  {
    SA_DBG1(("mpiGetDevInfoRspSpc: pDevice is NULL"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2t");
    return AGSA_RC_FAILURE;
  }

  if (agDevHandle == agNULL)
  {
    SA_DBG1(("mpiGetDevInfoRspSpc: warning!!! no deviceHandle is found"));
    ossaGetDeviceInfoCB(agRoot, agContext, agNULL, OSSA_DEV_INFO_INVALID_HANDLE, agNULL);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2t");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiGetDevInfoRspSpc: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  Info_avail = OSSA_DEV_INFO_NO_EXTENDED_INFO;

  /* setup device common infomation */
  pDevice->devInfo.sasDeviceInfo.commonDevInfo.smpTimeout =
    (bit16)((dTypeSrateSMPTOPortID >> SHIFT8 ) & SMPTO_BITS);

  pDevice->devInfo.sataDeviceInfo.commonDevInfo.smpTimeout =
    (bit16)((dTypeSrateSMPTOPortID >> SHIFT8 ) & SMPTO_BITS);

  pDevice->devInfo.sasDeviceInfo.commonDevInfo.it_NexusTimeout =
    (bit16)(FirstBurstSizeITNexusTimeOut & NEXUSTO_BITS);

  pDevice->devInfo.sataDeviceInfo.commonDevInfo.it_NexusTimeout =
    (bit16)(FirstBurstSizeITNexusTimeOut & NEXUSTO_BITS);

  pDevice->devInfo.sasDeviceInfo.commonDevInfo.firstBurstSize =
    (bit16)((FirstBurstSizeITNexusTimeOut >> SHIFT16) & FIRST_BURST);

  pDevice->devInfo.sataDeviceInfo.commonDevInfo.firstBurstSize =
    (bit16)((FirstBurstSizeITNexusTimeOut >> SHIFT16) & FIRST_BURST);

  pDevice->devInfo.sasDeviceInfo.commonDevInfo.flag = (bit32)((dTypeSrateSMPTOPortID >> SHIFT4 ) & FLAG_BITS);

  pDevice->devInfo.sataDeviceInfo.commonDevInfo.flag = (bit32)((dTypeSrateSMPTOPortID >> SHIFT4 ) & FLAG_BITS);

  pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate =
    (bit8)((dTypeSrateSMPTOPortID >> SHIFT24) & LINK_RATE_BITS);

  pDevice->devInfo.sataDeviceInfo.commonDevInfo.devType_S_Rate =
    (bit8)((dTypeSrateSMPTOPortID >> SHIFT24) & LINK_RATE_BITS);

  /* check SAS device then copy SAS Address */
  if ( ((dTypeSrateSMPTOPortID & DEV_TYPE_BITS) >> SHIFT28 == 0x00) ||
       ((dTypeSrateSMPTOPortID & DEV_TYPE_BITS) >> SHIFT28 == 0x01)) 
  {
    /* copy the sasAddressHi byte-by-byte : no endianness */
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[0] = pIomb->sasAddrHi[0];
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[1] = pIomb->sasAddrHi[1];
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[2] = pIomb->sasAddrHi[2];
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[3] = pIomb->sasAddrHi[3];

    /* copy the sasAddressLow byte-by-byte : no endianness */
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[0] = pIomb->sasAddrLow[0];
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[1] = pIomb->sasAddrLow[1];
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[2] = pIomb->sasAddrLow[2];
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[3] = pIomb->sasAddrLow[3];
  }

  /* Display Device Information */
  SA_DBG3(("mpiGetDevInfoRspSpc: smpTimeout=     0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.smpTimeout));
  SA_DBG3(("mpiGetDevInfoRspSpc: it_NexusTimeout=0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.it_NexusTimeout));
  SA_DBG3(("mpiGetDevInfoRspSpc: firstBurstSize= 0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.firstBurstSize));
  SA_DBG3(("mpiGetDevInfoRspSpc: devType_S_Rate= 0x%x\n", pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate));


  SA_DBG1(("Device SPC deviceid 0x%08X flag 0x%08X %s WWID= %02x%02x%02x%02x:%02x%02x%02x%02x, %s\n",
    deviceid,
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.flag,
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x20 ? "SATA DA" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x10 ? "SSP/SMP" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x0 ? "  STP  " : "Unknown",

    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[3],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[2],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[1],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[0],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[3],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[2],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[1],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[0],

    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 8  ? " 1.5G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 9  ? " 3.0G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 10 ? " 6.0G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 11 ? "12.0G" : "????" ));

  ossaGetDeviceInfoCB(agRoot, agContext, agDevHandle, Info_avail, &pDevice->devInfo.sasDeviceInfo.commonDevInfo);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetDevInfoRspSpc: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2t");
  return ret;
}

/******************************************************************************/
/*! \brief Set Device Information Response
 *
 *  This routine handles the response of Set Device Info Command Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Message
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetDevInfoRsp(
  agsaRoot_t             *agRoot,
  agsaSetDeviceInfoRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  agsaContext_t       *agContext;
  bit32               tag, status, deviceid, option, param;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2v");
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSetDeviceInfoRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSetDeviceInfoRsp_t, tag));
  /* get TAG */
  SA_DBG3(("mpiSetDevInfoRsp: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSetDevInfoRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2v");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");
  /* check status success or failure */
  if (status)
  {
    /* status is FAILED */
    if (pRequest->completionCB == agNULL)
    {
      SA_DBG1(("mpiSetDevInfoRsp: status is FAILED pRequest->completionCB == agNULL\n" ));
      ossaSetDeviceInfoCB(agRoot, agContext, agNULL, status, 0, 0);
    }
    else
    {
      SA_DBG1(("mpiSetDevInfoRsp: status is FAILED use CB\n" ));
      (*(ossaSetDeviceInfoCB_t)(pRequest->completionCB))(agRoot, agContext, agNULL, status, 0, 0);
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2v");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  /* status is SUCCESS */
  OSSA_READ_LE_32(AGROOT, &deviceid, pIomb, OSSA_OFFSET_OF(agsaSetDeviceInfoRsp_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &option, pIomb, OSSA_OFFSET_OF(agsaSetDeviceInfoRsp_t, SA_SR_SI));
  OSSA_READ_LE_32(AGROOT, &param, pIomb, OSSA_OFFSET_OF(agsaSetDeviceInfoRsp_t, A_R_ITNT));

  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle;
  if (pDevice != agNULL)
  {
    if (pDevice->targetDevHandle.sdkData)
    {
      agDevHandle = &(pDevice->targetDevHandle);
    }
    else
    {
      agDevHandle = &(pDevice->initiatorDevHandle);
    }
  }
  else
  {
    SA_DBG1(("mpiSetDevInfoRsp: pDevice is NULL"));
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2v");
    return AGSA_RC_FAILURE;
  }

  if (agDevHandle == agNULL)
  {
    SA_DBG1(("mpiSetDevInfoRsp: warning!!! no deviceHandle is found"));
    if (pRequest->completionCB == agNULL)
    {
      ossaSetDeviceInfoCB(agRoot, agContext, agNULL, OSSA_IO_NO_DEVICE, 0, 0);
    }
    else
    {
      (*(ossaSetDeviceInfoCB_t)(pRequest->completionCB))(agRoot, agContext, agNULL, OSSA_IO_NO_DEVICE, 0, 0);
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2v");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  SA_DBG2(("mpiSetDevInfoRsp:, option 0x%X param 0x%X\n", option, param));

  if(smIS_SPCV(agRoot))
  {
    SA_DBG2(("mpiSetDevInfoRsp:was option 0x%X param 0x%X\n", option, param));
    SA_DBG2(("mpiSetDevInfoRsp:pDevice->option 0x%X pDevice->param 0x%X\n", pDevice->option, pDevice->param));
    option |= pDevice->option;
    param |= pDevice->param;
    SA_DBG2(("mpiSetDevInfoRsp:now option 0x%X param 0x%X\n", option, param));
    if (pRequest->completionCB == agNULL)
    {
      ossaSetDeviceInfoCB(agRoot, agContext, agDevHandle, OSSA_SUCCESS, option, param);
    }
    else
    {
      (*(ossaSetDeviceInfoCB_t)(pRequest->completionCB))(agRoot, agContext, agDevHandle, OSSA_SUCCESS, option, param);
    }
  }
  else
  {
    SA_DBG2(("mpiSetDevInfoRsp:, option 0x%X param 0x%X\n", option, param));
    if (pRequest->completionCB == agNULL)
    {
      ossaSetDeviceInfoCB(agRoot, agContext, agDevHandle, OSSA_SUCCESS, option, param);
    }
    else
    {
      (*(ossaSetDeviceInfoCB_t)(pRequest->completionCB))(agRoot, agContext, agDevHandle, OSSA_SUCCESS, option, param);
    }
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2v");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SSP Event
 *
 *  This function handles the SAS Event.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSSPEvent(
  agsaRoot_t        *agRoot,
  agsaSSPEventRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaPortContext_t   *agPortContext;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  bit32               event,deviceId;
  bit32               deviceIdx, tag, portId_tmp;
  bit32               SSPTag;
  bit16               sspTag;
  bit8                portId;

  agsaDifDetails_t Dif_details;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2u");

  OSSA_READ_LE_32(AGROOT, &event, pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, event));
  OSSA_READ_LE_32(AGROOT, &deviceId, pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &portId_tmp, pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, portId));
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &SSPTag, pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, SSPTag));


  sspTag = (bit16)(SSPTag & SSPTAG_BITS);

  /* get IORequest from IOMap */
  pRequest  = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;

  SA_ASSERT((pRequest), "pRequest");

  if(agNULL == pRequest)
  {
    SA_DBG1(("mpiSSPEvent: agNULL == pRequest event 0x%X\n", event));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2u");
    return AGSA_RC_FAILURE;
  }

  /* get port context */
  portId = (bit8)(portId_tmp & PORTID_MASK);
  SA_DBG2(("mpiSSPEvent:PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[portId & PORTID_MASK].PortID,saRoot->PortMap[portId & PORTID_MASK].PortStatus,saRoot->PortMap[portId & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId].PortContext;
  /* get device Id */
  deviceIdx = deviceId & DEVICE_ID_BITS;
  OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceIdx].DeviceHandle;

  if( agNULL == pDevice )
  {
    OS_ASSERT(pDevice, "pDevice");
    agDevHandle = agNULL;
  }
  else
  {
    if (pDevice->targetDevHandle.sdkData)
    {
      agDevHandle = &(pDevice->targetDevHandle);
    }
    else
    {
      agDevHandle = &(pDevice->initiatorDevHandle);
    }
  }

  switch (event)
  {
    case  OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
    case  OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
    case  OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
    case  OSSA_IO_XFR_ERROR_DIF_MISMATCH:
    {

      SA_DBG1(("mpiSSPEvent:  DIF Event 0x%x HTAG = 0x%x\n", event, tag));

      OSSA_READ_LE_32(AGROOT, &Dif_details.UpperLBA,           pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, EVT_PARAM0_or_LBAH));
      OSSA_READ_LE_32(AGROOT, &Dif_details.LowerLBA,           pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, EVT_PARAM1_or_LBAL));
      OSSA_READ_LE_32(AGROOT, &Dif_details.sasAddressHi,       pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, SAS_ADDRH));
      OSSA_READ_LE_32(AGROOT, &Dif_details.sasAddressLo,       pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, SAS_ADDRL));
      OSSA_READ_LE_32(AGROOT, &Dif_details.ExpectedCRCUDT01,   pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, UDT1_E_UDT0_E_CRC_E));
      OSSA_READ_LE_32(AGROOT, &Dif_details.ExpectedUDT2345,    pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, UDT5_E_UDT4_E_UDT3_E_UDT2_E));
      OSSA_READ_LE_32(AGROOT, &Dif_details.ActualCRCUDT01,     pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, UDT1_A_UDT0_A_CRC_A));
      OSSA_READ_LE_32(AGROOT, &Dif_details.ActualUDT2345,      pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, UDT5_A_UDT4_A_UDT3_A_UDT2_A));
      OSSA_READ_LE_32(AGROOT, &Dif_details.DIFErrDevID,        pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, HW_DEVID_Reserved_DIF_ERR));
      OSSA_READ_LE_32(AGROOT, &Dif_details.ErrBoffsetEDataLen, pIomb, OSSA_OFFSET_OF(agsaSSPEventRsp_t, EDATA_LEN_ERR_BOFF));

      SA_DBG2(("mpiSSPEvent: UpperLBA.         0x%08X LowerLBA.           0x%08X\n",Dif_details.UpperLBA,         Dif_details.LowerLBA));
      SA_DBG2(("mpiSSPEvent: sasAddressHi.     0x%02X%02X%02X%02X sasAddressLo.       0x%02X%02X%02X%02X\n",
                          Dif_details.sasAddressHi[0],Dif_details.sasAddressHi[1],Dif_details.sasAddressHi[2],Dif_details.sasAddressHi[3],
                          Dif_details.sasAddressLo[0],Dif_details.sasAddressLo[1],Dif_details.sasAddressLo[2],Dif_details.sasAddressLo[3]));
      SA_DBG2(("mpiSSPEvent: ExpectedCRCUDT01. 0x%08X ExpectedUDT2345.    0x%08X\n",Dif_details.ExpectedCRCUDT01, Dif_details.ExpectedUDT2345));
      SA_DBG2(("mpiSSPEvent: ActualCRCUDT01.   0x%08X ActualUDT2345.      0x%08X\n",Dif_details.ActualCRCUDT01,   Dif_details.ActualUDT2345));
      SA_DBG2(("mpiSSPEvent: DIFErrDevID.      0x%08X ErrBoffsetEDataLen. 0x%08X\n",Dif_details.DIFErrDevID,      Dif_details.ErrBoffsetEDataLen));
    }

    default:
    {
      SA_DBG3(("mpiSSPEvent:  Non DIF event"));
      break;
    }
  }


  /* get event */
  switch (event)
  {
    case OSSA_IO_OVERFLOW:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OVERFLOW tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OVERFLOW++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_BREAK:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_BREAK tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_BREAK++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_PHY_NOT_READY:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_PHY_NOT_READY tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_PHY_NOT_READY++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_BREAK:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_BREAK tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_BREAK++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_NAK_RECEIVED:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_NAK_RECEIVED tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_NAK_RECEIVED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_OFFSET_MISMATCH:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_OFFSET_MISMATCH tag 0x%x ssptag 0x%x\n", tag, sspTag));
#ifdef SA_ENABLE_PCI_TRIGGER
      if( saRoot->swConfig.PCI_trigger & PCI_TRIGGER_OFFSET_MISMATCH )
      {
        siPCITriger(agRoot);
      }
#endif /* SA_ENABLE_PCI_TRIGGER */
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_OFFSET_MISMATCH++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_OPEN_RETRY_TIMEOUT:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_OPEN_RETRY_TIMEOUT tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_CMD_FRAME_ISSUED:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_CMD_FRAME_ISSUED tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_CMD_FRAME_ISSUED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_UNEXPECTED_PHASE++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case  OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
    {
      SA_DBG1(("mpiSSPEvent:OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED HTAG = 0x%x sspTag = 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case  OSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
    case  OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag,sizeof(agsaDifDetails_t),&Dif_details);
      break;
    }
    case  OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag,sizeof(agsaDifDetails_t),&Dif_details);
      break;
    }
    case  OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag,sizeof(agsaDifDetails_t),&Dif_details);
      break;
    }
    case  OSSA_IO_XFR_ERROR_DIF_MISMATCH:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFR_ERROR_DIF_MISMATCH tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DIF_MISMATCH++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag,sizeof(agsaDifDetails_t),&Dif_details);
      break;
    }
    case  OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERR_EOB_DATA_OVERRUN++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_READ_COMPL_ERR:
    {
      SA_DBG1(("mpiSSPEvent: OSSA_IO_XFER_READ_COMPL_ERR tag 0x%x ssptag 0x%x\n", tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_XFER_READ_COMPL_ERR++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0, agNULL);
      break;
    }
    default:
    {
      SA_DBG1(("mpiSSPEvent:  Unknown Event 0x%x tag 0x%x ssptag 0x%x\n", event, tag, sspTag));
      saRoot->IoEventCount.agOSSA_IO_UNKNOWN_ERROR++;
      ossaSSPEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, sspTag, 0,agNULL);
      break;
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2u");
  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SATA Event
 *
 *  This function handles the SATA Event.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSATAEvent(
  agsaRoot_t         *agRoot,
  agsaSATAEventRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest = agNULL;
  agsaPortContext_t   *agPortContext;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  bit32               deviceIdx, portId_tmp, event, tag, deviceId;
  bit8                portId;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2w");

  /* get port context */
  OSSA_READ_LE_32(AGROOT, &portId_tmp, pIomb, OSSA_OFFSET_OF(agsaSATAEventRsp_t, portId));
  OSSA_READ_LE_32(AGROOT, &deviceId, pIomb, OSSA_OFFSET_OF(agsaSATAEventRsp_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &event, pIomb, OSSA_OFFSET_OF(agsaSATAEventRsp_t, event));
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSATAEventRsp_t, tag));

  if (OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE != event)
  {
    /* get IORequest from IOMap */
    pRequest  = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  }
  /* get port context - only for OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE */
  portId = (bit8)(portId_tmp & PORTID_MASK);
  SA_DBG2(("mpiSATAEvent:PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[portId & PORTID_MASK].PortID,saRoot->PortMap[portId & PORTID_MASK].PortStatus,saRoot->PortMap[portId & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId].PortContext;
  /* get device Id - only for OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE*/
  deviceIdx = deviceId & DEVICE_ID_BITS;
  OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceIdx].DeviceHandle;
  agDevHandle = &(pDevice->targetDevHandle);

  /* get event */
  switch (event)
  {
    case OSSA_IO_OVERFLOW:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OVERFLOW HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OVERFLOW++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_BREAK:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_BREAK HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_BREAK++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_PHY_NOT_READY:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_PHY_NOT_READY HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_PHY_NOT_READY++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_BREAK:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_BREAK HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_BREAK++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }

    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
    {
      SA_DBG1(("mpiSATAEvent:  HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
    {
      SA_DBG1(("mpiSATAEvent:OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED  HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_NAK_RECEIVED:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_NAK_RECEIVED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_NAK_RECEIVED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE++;
      ossaSATAEvent(agRoot, agNULL, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_OFFSET_MISMATCH:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_OFFSET_MISMATCH HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_OFFSET_MISMATCH++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_OPEN_RETRY_TIMEOUT:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_OPEN_RETRY_TIMEOUT HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_PEER_ABORTED:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_PEER_ABORTED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_PEER_ABORTED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_CMD_FRAME_ISSUED:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_CMD_FRAME_ISSUED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_CMD_FRAME_ISSUED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
    {
      SA_DBG1(("mpiSATAEvent, OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE:
    {
      SA_DBG1(("mpiSATAEvent, OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_UNEXPECTED_PHASE++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN:
    {
      SA_DBG1(("mpiSATAEvent, OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED:
    {
      SA_DBG1(("mpiSATAEvent, OSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_PIO_SETUP_ERROR:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_PIO_SETUP_ERROR HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_PIO_SETUP_ERROR++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFR_ERROR_DIF_MISMATCH:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFR_ERROR_DIF_MISMATCH HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_DIF_MISMATCH++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERR_EOB_DATA_OVERRUN++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    case OSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT:
    {
      SA_DBG1(("mpiSATAEvent: OSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT HTAG = 0x%x\n", tag));
      saRoot->IoEventCount.agOSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
    default:
    {
      SA_DBG1(("mpiSATAEvent: Unknown Event 0x%x HTAG = 0x%x\n", event, tag));
      saRoot->IoEventCount.agOSSA_IO_UNKNOWN_ERROR++;
      ossaSATAEvent(agRoot, pRequest->pIORequestContext, agPortContext, agDevHandle, event, 0, agNULL);
      break;
    }
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2w");
  return ret;
}

/******************************************************************************/
/*! \brief Set NVM Data Response
 *
 *  This routine handles the response of SET NVM Data Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetNVMDataRsp(
  agsaRoot_t          *agRoot,
  agsaSetNVMDataRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag, status, iPTdaBnDpsAsNvm;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2x");

  SA_DBG1(("mpiSetNVMDataRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSetNVMDataRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &iPTdaBnDpsAsNvm, pIomb, OSSA_OFFSET_OF(agsaSetNVMDataRsp_t, iPTdaBnDpsAsNvm));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSetNVMDataRsp_t, status));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSetNVMDataRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2x");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  if (((iPTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_CONFIG_SEEPROM) ||
      ((iPTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_VPD_FLASH) ||
      ((iPTdaBnDpsAsNvm & NVMD_TYPE) == AGSA_NVMD_TWI_DEVICES))
  {
    /* CB for VPD for SEEPROM-0, VPD_FLASH and TWI */
    ossaSetNVMDResponseCB(agRoot, agContext, (status & NVMD_STAT));
  }
  else
  {
    /* should not happend */
    SA_DBG1(("mpiSetNVMDataRsp: NVMD is wrong. TAG=0x%x STATUS=0x%x\n", tag, (iPTdaBnDpsAsNvm & NVMD_TYPE)));
    ret = AGSA_RC_FAILURE;
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2x");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SSP ABORT Response
 *
 *  This function handles the SSP Abort Response.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSSPAbortRsp(
  agsaRoot_t         *agRoot,
  agsaSSPAbortRsp_t  *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDeviceDesc_t    *pDevice;
  bit32               tag, status, scope;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2y");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSSPAbortRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSSPAbortRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &scope, pIomb, OSSA_OFFSET_OF(agsaSSPAbortRsp_t, scp));
  scope &= 3; 
  /* get IORequest from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;

  if (agNULL == pRequest)
  {
    /* remove the SSP_ABORT or SATA_ABORT request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;
    SA_ASSERT((pRequest), "pRequest");
    SA_DBG1(("mpiSSPAbortRsp: the request is NULL. Tag=%x\n", tag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2y");
    return AGSA_RC_FAILURE;
  }


  if ( agTRUE == pRequest->valid )
  {
    pDevice = pRequest->pDevice;
    SA_ASSERT((pRequest->pDevice), "pRequest->pDevice");

    SA_DBG3(("mpiSSPAbortRsp: request abort is valid Htag 0x%x\n", tag));
    /* remove the SSP_ABORT or SATA_ABORT request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;

    if( pRequest->completionCB == agNULL )
    {
      ossaSSPAbortCB(agRoot, pRequest->pIORequestContext, scope, status);
    }
    else
    {
      (*(ossaGenericAbortCB_t)(pRequest->completionCB))(agRoot, pRequest->pIORequestContext, scope, status);
    }

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* Delete the request from the pendingIORequests */
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));

    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiSSPAbortRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }

    if(scope)
    {
      siCountActiveIORequestsOnDevice( agRoot, pDevice->DeviceMapIndex );
    }

    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  }
  else
  {
    ret = AGSA_RC_FAILURE;
    SA_DBG1(("mpiSSPAbortRsp: the request is not valid any more. Tag=%x\n", pRequest->HTag));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2y");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SATA ABORT Response
 *
 *  This function handles the SATA Event.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSATAAbortRsp(
  agsaRoot_t         *agRoot,
  agsaSATAAbortRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDeviceDesc_t    *pDevice;
  bit32               tag, status, scope;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3B");
  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSATAAbortRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSATAAbortRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &scope, pIomb, OSSA_OFFSET_OF(agsaSATAAbortRsp_t, scp));

  /* get IORequest from IOMap */
  pRequest  = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;

  if (agNULL == pRequest)
  {
    /* remove the SSP_ABORT or SATA_ABORT request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;
    SA_DBG1(("mpiSATAAbortRsp: the request is NULL. Tag=%x\n", tag));
    SA_ASSERT((pRequest), "pRequest");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3B");
    return AGSA_RC_FAILURE;
  }

  if ( agTRUE == pRequest->valid )
  {
    pDevice = pRequest->pDevice;
    SA_ASSERT((pRequest->pDevice), "pRequest->pDevice");

    SA_DBG3(("mpiSATAAbortRsp: request abort is valid Htag 0x%x\n", tag));

    if( pRequest->completionCB == agNULL )
    {
      ossaSATAAbortCB(agRoot, pRequest->pIORequestContext, scope, status);
    }
    else
    {
      (*(ossaGenericAbortCB_t)(pRequest->completionCB))(agRoot, pRequest->pIORequestContext, scope, status);
    }
    /* remove the SATA_ABORT request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;
    saRoot->IOMap[tag].agContext = agNULL;

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* Delete the request from the pendingIORequests */
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  }
  else
  {
    ret = AGSA_RC_FAILURE;
    SA_DBG1(("mpiSATAAbortRsp: the request is not valid any more. Tag=%x\n", pRequest->HTag));
  }


  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3B");
  return ret;
}

/******************************************************************************/
/*! \brief Set GPIO Response
 *
 *  This routine handles the response of GPIO Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGPIORsp(
  agsaRoot_t          *agRoot,
  agsaGPIORsp_t       *pIomb
  )
{
  bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaContext_t            *agContext;
  agsaIORequestDesc_t      *pRequest;
  agsaGpioPinSetupInfo_t   pinSetupInfo;
  agsaGpioEventSetupInfo_t eventSetupInfo;
  bit32 GpioIe, OT11_0, OT19_12, GPIEVChange, GPIEVFall, GPIEVRise, GpioRdVal, tag;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5C");

  SA_DBG3(("mpiGPIORsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGPIORsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x\n", tag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5C");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* set payload to zeros */
  si_memset(&pinSetupInfo, 0, sizeof(agsaGpioPinSetupInfo_t));
  si_memset(&eventSetupInfo, 0, sizeof(agsaGpioEventSetupInfo_t));

  OSSA_READ_LE_32(AGROOT, &GpioIe, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, GpioIe));
  OSSA_READ_LE_32(AGROOT, &OT11_0, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, OT11_0));
  OSSA_READ_LE_32(AGROOT, &OT19_12, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, OT19_12));
  OSSA_READ_LE_32(AGROOT, &GPIEVChange, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, GPIEVChange));
  OSSA_READ_LE_32(AGROOT, &GPIEVFall, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, GPIEVFall));
  OSSA_READ_LE_32(AGROOT, &GPIEVRise, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, GPIEVRise));
  OSSA_READ_LE_32(AGROOT, &GpioRdVal, pIomb, OSSA_OFFSET_OF(agsaGPIORsp_t, GpioRdVal));
  pinSetupInfo.gpioInputEnabled = GpioIe;
  pinSetupInfo.gpioTypePart1 = OT11_0;
  pinSetupInfo.gpioTypePart2 = OT19_12;
  eventSetupInfo.gpioEventLevel = GPIEVChange;
  eventSetupInfo.gpioEventFallingEdge = GPIEVFall;
  eventSetupInfo.gpioEventRisingEdge = GPIEVRise;

  ossaGpioResponseCB(agRoot, agContext, OSSA_IO_SUCCESS, GpioRdVal,
                     &pinSetupInfo,
                     &eventSetupInfo);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGPIORsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5C");
  return ret;
}

/******************************************************************************/
/*! \brief Set GPIO Event Response
 *
 *  This routine handles the response of GPIO Event
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGPIOEventRsp(
  agsaRoot_t          *agRoot,
  agsaGPIOEvent_t     *pIomb
  )
{
  bit32       ret = AGSA_RC_SUCCESS;
  bit32       GpioEvent;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3D");

  OSSA_READ_LE_32(AGROOT, &GpioEvent, pIomb, OSSA_OFFSET_OF(agsaGPIOEvent_t, GpioEvent));

  ossaGpioEvent(agRoot, GpioEvent);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3D");
  return ret;
}

/******************************************************************************/
/*! \brief SAS Diagnostic Start/End Response
 *
 *  This routine handles the response of SAS Diagnostic Start/End Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSASDiagStartEndRsp(
  agsaRoot_t               *agRoot,
  agsaSASDiagStartEndRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag, Status;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2F");

  SA_DBG3(("mpiSASDiagStartEndRsp: HTAG=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSASDiagStartEndRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &Status, pIomb, OSSA_OFFSET_OF(agsaSASDiagStartEndRsp_t, Status));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSASDiagStartEndRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, Status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2F");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  switch(Status)
  {

    case OSSA_DIAG_SE_SUCCESS:
      SA_DBG3(("mpiSASDiagStartEndRsp: Status OSSA_DIAG_SE_SUCCESS 0x%X \n", Status));
      break;
    case OSSA_DIAG_SE_INVALID_PHY_ID:
      SA_DBG1(("mpiSASDiagStartEndRsp: Status OSSA_DIAG_SE_INVALID_PHY_ID 0x%X \n", Status));
      break;
    case OSSA_DIAG_PHY_NOT_DISABLED:
      SA_DBG1(("mpiSASDiagStartEndRsp: Status OSSA_DIAG_PHY_NOT_DISABLED Status 0x%X \n", Status));
      break;
    case OSSA_DIAG_OTHER_FAILURE:
      if(smIS_SPCV(agRoot))
      {
        SA_DBG1(("mpiSASDiagStartEndRsp: Status OSSA_DIAG_OTHER_FAILURE Status 0x%X \n", Status));
      }
      else
      {
        SA_DBG1(("mpiSASDiagStartEndRsp: Status OSSA_DIAG_OPCODE_INVALID Status 0x%X \n", Status));
      }
      break;
    default:
      SA_DBG1(("mpiSASDiagStartEndRsp:Status UNKNOWN 0x%X \n", Status));
      break;
  }

  ossaSASDiagStartEndCB(agRoot, agContext, Status);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSASDiagStartEndRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2F");
  return ret;
}

/******************************************************************************/
/*! \brief SAS Diagnostic Execute Response
 *
 *  This routine handles the response of SAS Diagnostic Execute Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSASDiagExecuteRsp(
  agsaRoot_t               *agRoot,
  agsaSASDiagExecuteRsp_t  *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag, Status, CmdTypeDescPhyId, ReportData;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"3G");

  SA_DBG3(("mpiSASDiagExecuteRsp: HTAG=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSASDiagExecuteRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &Status, pIomb, OSSA_OFFSET_OF(agsaSASDiagExecuteRsp_t, Status));
  OSSA_READ_LE_32(AGROOT, &CmdTypeDescPhyId, pIomb, OSSA_OFFSET_OF(agsaSASDiagExecuteRsp_t, CmdTypeDescPhyId));
  OSSA_READ_LE_32(AGROOT, &ReportData, pIomb, OSSA_OFFSET_OF(agsaSASDiagExecuteRsp_t, ReportData));
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSASDiagExecuteRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x STATUS=0x%x\n", tag, Status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3G");
    return AGSA_RC_FAILURE;
  }

  switch(Status)
  {

    case OSSA_DIAG_SUCCESS:
      SA_DBG3(("mpiSASDiagExecuteRsp: Status OSSA_DIAG_SUCCESS 0x%X \n", Status));
      break;
    case OSSA_DIAG_INVALID_COMMAND:
      if(smIS_SPCV(agRoot))
      {
        SA_DBG1(("mpiSASDiagExecuteRsp: Status OSSA_DIAG_INVALID_COMMAND Status 0x%X \n", Status));
      }
      else
      {
        SA_DBG1(("mpiSASDiagExecuteRsp: Status OSSA_DIAG_FAIL Status 0x%X \n", Status));
      }
      break;
    case OSSA_REGISTER_ACCESS_TIMEOUT:
      SA_DBG1(("mpiSASDiagExecuteRsp: Status OSSA_REGISTER_ACCESS_TIMEOUT Status 0x%X \n", Status));
      break;
    case OSSA_DIAG_NOT_IN_DIAGNOSTIC_MODE:
      SA_DBG1(("mpiSASDiagExecuteRsp: Status OSSA_DIAG_NOT_IN_DIAGNOSTIC_MODE Status 0x%X \n", Status));
      break;
    case OSSA_DIAG_INVALID_PHY:
      SA_DBG1(("mpiSASDiagExecuteRsp: Status OSSA_DIAG_INVALID_PHY Status 0x%X \n", Status));
      break;
    case OSSA_MEMORY_ALLOC_FAILURE:
      SA_DBG1(("mpiSASDiagExecuteRsp: Status  Status 0x%X \n", Status));
      break;

    default:
      SA_DBG1(("mpiSASDiagExecuteRsp:Status UNKNOWN 0x%X \n", Status));
      break;
  }


  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  ossaSASDiagExecuteCB(agRoot, agContext, Status, CmdTypeDescPhyId, ReportData);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSASDiagExecuteRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3G");

  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief SAS General Event Notification Response
 *
 *  This routine handles the response of Inbound IOMB Command with error case
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGeneralEventRsp(
  agsaRoot_t               *agRoot,
  agsaGeneralEventRsp_t    *pIomb
  )
{
  bit32                 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32                 i;
  bit32                 status;
  bit32                 tag;
  agsaIORequestDesc_t   *pRequest;
  agsaDeviceDesc_t      *pDevice;
  agsaContext_t         *agContext = NULL;
  agsaGeneralEventRsp_t GenEventData;
  agsaHWEventEncrypt_t  agEvent;
  bit16                 OpCode = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3H");

  si_memset(&GenEventData,0,sizeof(agsaGeneralEventRsp_t));

  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaGeneralEventRsp_t, status));

  SA_DBG3(("mpiGeneralEventRsp:  %p\n", pIomb));

  SA_DBG1(("mpiGeneralEventRsp: OpCode 0x%X status 0x%x\n",pIomb->inbIOMBpayload[0] & OPCODE_BITS, status));

  for (i = 0; i < GENERAL_EVENT_PAYLOAD; i++)
  {
    OSSA_READ_LE_32(AGROOT, &GenEventData.inbIOMBpayload[i], pIomb, OSSA_OFFSET_OF(agsaGeneralEventRsp_t,inbIOMBpayload[i] ));
  }
  SA_DBG1(("mpiGeneralEventRsp: inbIOMBpayload 0x%08x 0x%08x 0x%08x 0x%08x\n",
                                    GenEventData.inbIOMBpayload[0],GenEventData.inbIOMBpayload[1],
                                    GenEventData.inbIOMBpayload[2],GenEventData.inbIOMBpayload[3] ));
  SA_DBG1(("mpiGeneralEventRsp: inbIOMBpayload 0x%08x 0x%08x 0x%08x 0x%08x\n",
                                    GenEventData.inbIOMBpayload[4],GenEventData.inbIOMBpayload[8],
                                    GenEventData.inbIOMBpayload[6],GenEventData.inbIOMBpayload[7] ));

  switch (status) /*status  */
  {

    case GEN_EVENT_IOMB_V_BIT_NOT_SET:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_IOMB_V_BIT_NOT_SET\n" ));
      break;
    case GEN_EVENT_INBOUND_IOMB_OPC_NOT_SUPPORTED:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_INBOUND_IOMB_OPC_NOT_SUPPORTED\n" ));
      break;
    case GEN_EVENT_IOMB_INVALID_OBID:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_IOMB_INVALID_OBID\n" ));
      break;
    case GEN_EVENT_DS_IN_NON_OPERATIONAL:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_DS_IN_NON_OPERATIONAL\n" ));
      break;
    case GEN_EVENT_DS_IN_RECOVERY:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_DS_IN_RECOVERY\n" ));
      break;
    case GEN_EVENT_DS_INVALID:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_DS_INVALID\n" ));
      break;
    case GEN_EVENT_IO_XFER_READ_COMPL_ERR:
      SA_DBG1(("mpiGeneralEventRsp: GEN_EVENT_IO_XFER_READ_COMPL_ERR 0x%x 0x%x 0x%x\n",
                GenEventData.inbIOMBpayload[0],
                GenEventData.inbIOMBpayload[1],
                GenEventData.inbIOMBpayload[1] ));
      ossaGeneralEvent(agRoot, status, agContext, GenEventData.inbIOMBpayload);
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3H");
      return(ret);
    default:
      SA_DBG1(("mpiGeneralEventRsp: Unknown General Event status!!! 0x%x\n", status));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3H");
      return AGSA_RC_FAILURE;
  }

  OpCode = (bit16)(GenEventData.inbIOMBpayload[0] & OPCODE_BITS);
  tag = GenEventData.inbIOMBpayload[1];
  SA_DBG1(("mpiGeneralEventRsp:OpCode 0x%X [0] 0x%08x\n" ,OpCode,(bit16)(GenEventData.inbIOMBpayload[0] & OPCODE_BITS)));

  switch (OpCode) /* OpCode */
    {
      case OPC_INB_DEV_HANDLE_ACCEPT:
      case OPC_INB_ECHO:
      case OPC_INB_FW_FLASH_UPDATE:
      case OPC_INB_GET_NVMD_DATA:
      case OPC_INB_SET_NVMD_DATA:
      case OPC_INB_DEREG_DEV_HANDLE:
      case OPC_INB_SPC_GET_DEV_INFO:
      case OPC_INB_GET_DEV_HANDLE:
      case OPC_INB_SPC_REG_DEV:
      case OPC_INB_SAS_DIAG_EXECUTE:
      case OPC_INB_SAS_DIAG_MODE_START_END:
      case OPC_INB_PHYSTART:
      case OPC_INB_PHYSTOP:
      case OPC_INB_LOCAL_PHY_CONTROL:
      case OPC_INB_GPIO:
      case OPC_INB_GET_TIME_STAMP:
      case OPC_INB_PORT_CONTROL:
      case OPC_INB_SET_DEVICE_STATE:
      case OPC_INB_GET_DEVICE_STATE:
      case OPC_INB_SET_DEV_INFO:
//      case OPC_INB_PCIE_DIAG_EXECUTE:
      case OPC_INB_SAS_HW_EVENT_ACK:
      case OPC_INB_SAS_RE_INITIALIZE:
      case OPC_INB_KEK_MANAGEMENT:
      case OPC_INB_SET_OPERATOR:
      case OPC_INB_GET_OPERATOR:
//      case OPC_INB_SGPIO:

#ifdef SPC_ENABLE_PROFILE
      case OPC_INB_FW_PROFILE:
#endif
          /* Uses the tag table, so we have to free it up */

          SA_ASSERT((tag < AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs),
                    "OPC_OUB_GENERAL_EVENT tag out of range");
          SA_ASSERT((saRoot->IOMap[ tag < (AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs) ? tag : 0 ].Tag != MARK_OFF),
                    "OPC_OUB_GENERAL_EVENT tag not in use 1");

#if defined(SALLSDK_DEBUG)
          if (tag > AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs)
          {
            smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "3H");
            return AGSA_RC_FAILURE;
          }
#endif /* SALLSDK_DEBUG */

          SA_DBG1(("mpiGeneralEventRsp:OpCode found 0x%x htag 0x%x\n",OpCode, tag));
          /* get agContext */
          agContext = saRoot->IOMap[tag].agContext;
          /* get request from IOMap */
          pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
          if(pRequest)
          {
            /* remove the request from IOMap */
            saRoot->IOMap[tag].Tag = MARK_OFF;
            saRoot->IOMap[tag].IORequest = agNULL;
            saRoot->IOMap[tag].agContext = agNULL;

            ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
            SA_ASSERT((pRequest->valid), "pRequest->valid");
            pRequest->valid = agFALSE;
            /* return the request to free pool */
            if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
            {
              SA_DBG1(("mpiGeneralEventRsp: saving pRequest (%p) for later use\n", pRequest));
              saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
            }
            else
            {
              /* return the request to free pool */
              saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
            }
            ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
          }
          else
          {
            SA_DBG1(("mpiGeneralEventRsp:pRequest (%p) NULL\n", pRequest));
            ret =  AGSA_RC_FAILURE;
          }
          break;
      /* ????  */
      case OPC_INB_SATA_HOST_OPSTART:
      case OPC_INB_SATA_ABORT:
      case OPC_INB_SSPINIIOSTART:
      case OPC_INB_SSPINITMSTART:
      case OPC_INB_SSPINIEXTIOSTART:
      case OPC_INB_SSPTGTIOSTART:
      case OPC_INB_SSPTGTRSPSTART:
      case OPC_INB_SSP_DIF_ENC_OPSTART:
      case OPC_INB_SATA_DIF_ENC_OPSTART:

      case OPC_INB_SSP_ABORT:
      case OPC_INB_SMP_REQUEST:
      case OPC_INB_SMP_ABORT:
      {
        /* Uses the tag table, so we have to free it up */
        SA_DBG1(("mpiGeneralEventRsp:OpCode found 0x%x htag 0x%x\n",OpCode, tag));

        tag = GenEventData.inbIOMBpayload[1];

        SA_ASSERT((tag < AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs),
                  "OPC_OUB_GENERAL_EVENT tag out of range");
        SA_ASSERT((saRoot->IOMap[ tag < (AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs) ? tag : 0 ].Tag != MARK_OFF),
                  "OPC_OUB_GENERAL_EVENT tag not in use 2");
#if defined(SALLSDK_DEBUG)
        if (tag > AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs)
        {
          smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "3H");
          return AGSA_RC_FAILURE;
        }
#endif
          /* get request from IOMap */
        pRequest  = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
        if(pRequest)
        {
          pDevice   = pRequest->pDevice;
          /* return the request to free pool */
          /* get IORequestContext */
          agContext = (agsaContext_t *)pRequest->pIORequestContext;
          /* remove the request from IOMap */
          saRoot->IOMap[tag].Tag = MARK_OFF;
          saRoot->IOMap[tag].IORequest = agNULL;
          saRoot->IOMap[tag].agContext = agNULL;

          ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
          SA_ASSERT((pRequest->valid), "pRequest->valid");
          pRequest->valid = agFALSE;
          saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
          if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
          {
            SA_DBG1(("mpiGeneralEventRsp: saving pRequest (%p) for later use\n", pRequest));
            saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
          }
          else
          {
            /* return the request to free pool */
            saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
          }
          ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
          break;
        }
        else
        {
          SA_DBG1(("mpiGeneralEventRsp:pRequest (%p) NULL\n", pRequest));
          ret =  AGSA_RC_FAILURE;
        }
      }
    default:
    {
        SA_DBG1(("mpiGeneralEventRsp:OpCode Not found 0x%x htag 0x%x\n",OpCode, tag));
        ret =  AGSA_RC_FAILURE;

        /* Uses the tag table, so we have to free it up */
        tag = GenEventData.inbIOMBpayload[1];

        SA_ASSERT((tag < AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs),
                  "OPC_OUB_GENERAL_EVENT tag out of range");
        SA_ASSERT((saRoot->IOMap[ tag < (AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs) ? tag : 0 ].Tag != MARK_OFF),
                  "OPC_OUB_GENERAL_EVENT tag not in use 3");

#if defined(SALLSDK_DEBUG)
        if (tag > AGSA_MAX_VALID_PORTS * saRoot->swConfig.maxActiveIOs)
        {
          smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "3H");
          return AGSA_RC_FAILURE;
        }
#endif
        /* get agContext */
        agContext = saRoot->IOMap[tag].agContext;
        /* get request from IOMap */
        pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
        if (pRequest == agNULL)
        {
          smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "3H");
          return AGSA_RC_FAILURE;
        }

        /* remove the request from IOMap */
        saRoot->IOMap[tag].Tag = MARK_OFF;
        saRoot->IOMap[tag].IORequest = agNULL;
        saRoot->IOMap[tag].agContext = agNULL;

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        SA_ASSERT((pRequest->valid), "pRequest->valid");
        pRequest->valid = agFALSE;
        /* return the request to free pool */
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("mpiGeneralEventRsp: saving pRequest (%p) for later use\n", pRequest));
          saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
        }
        else
        {
          /* return the request to free pool */
          saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
        }
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        break;
      }
      ret =  AGSA_RC_FAILURE;

    }

  switch (OpCode) /* OpCode */
  {

    case OPC_INB_KEK_MANAGEMENT:
    {  
      bit32 flags = GenEventData.inbIOMBpayload[2];

      SA_DBG1(("mpiGeneralEventRsp: OPC_INB_KEK_MANAGEMENT 0x%x htag 0x%x flags 0x%x\n",OpCode, tag, flags));
      if (flags & 0xFF00) /* update and store*/
      {
        agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE;
        SA_DBG1(("mpiGeneralEventRsp: OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE\n"));
      }
      else /* update */
      {
        agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_UPDATE;
        SA_DBG1(("mpiGeneralEventRsp: OSSA_HW_ENCRYPT_KEK_UPDATE\n"));
      }
      agEvent.status = OSSA_INVALID_ENCRYPTION_SECURITY_MODE;
      si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
      agEvent.status = status;

      SA_DBG1(("mpiGeneralEventRsp: ossaHwCB OSSA_HW_EVENT_ENCRYPTION\n" ));
      ossaHwCB(agRoot, NULL, OSSA_HW_EVENT_ENCRYPTION, 0, (void*)&agEvent, agContext);
      break;
    }
    case OPC_INB_OPR_MGMT:
         si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
         agEvent.status = status;
         agEvent.encryptOperation = OSSA_HW_ENCRYPT_OPERATOR_MANAGEMENT;

         SA_DBG1(("mpiGeneralEventRsp: OSSA_HW_ENCRYPT_OPERATOR_MANAGEMENT\n" ));
         ossaOperatorManagementCB(agRoot, agContext, status, 0);
         break;
    case OPC_INB_SET_OPERATOR:
         SA_DBG1(("mpiGeneralEventRsp: OSSA_HW_ENCRYPT_SET_OPERATOR\n" ));
         ossaSetOperatorCB(agRoot,agContext,0xFF,0xFF );
         break;
    case OPC_INB_GET_OPERATOR:
         SA_DBG1(("mpiGeneralEventRsp: OSSA_HW_ENCRYPT_GET_OPERATOR\n" ));
         ossaGetOperatorCB(agRoot,agContext,0xFF,0xFF,0xFF,0xFF,agNULL );
         break;
    case OPC_INB_ENC_TEST_EXECUTE:
         si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
         agEvent.status = status;
         agEvent.encryptOperation = OSSA_HW_ENCRYPT_TEST_EXECUTE;

         SA_DBG1(("mpiGeneralEventRsp: OSSA_HW_ENCRYPT_TEST_EXECUTE\n" ));
         ossaHwCB(agRoot, NULL, OSSA_HW_EVENT_ENCRYPTION, 0, (void*)&agEvent, agContext);
         break;
    default:
         SA_DBG1(("mpiGeneralEventRsp: MGMNT OpCode Not found 0x%x\n",OpCode ));
         ossaGeneralEvent(agRoot, status, agContext, GenEventData.inbIOMBpayload);
         break;
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "3H");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SSP Request Received Event (target mode)
 *
 *  This function handles the SSP Request Received Event.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pMsg1        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSSPReqReceivedNotify(
  agsaRoot_t *agRoot,
  agsaSSPReqReceivedNotify_t *pMsg1)
{
  bit32            ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t     *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t *pDevice;
  bit32            deviceid, iniTagSSPIul, frameTypeHssa, TlrHdsa;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3J");
  /* convert endiness if necassary */
  OSSA_READ_LE_32(AGROOT, &deviceid, pMsg1, OSSA_OFFSET_OF(agsaSSPReqReceivedNotify_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &iniTagSSPIul, pMsg1, OSSA_OFFSET_OF(agsaSSPReqReceivedNotify_t, iniTagSSPIul));
  OSSA_READ_LE_32(AGROOT, &frameTypeHssa, pMsg1, OSSA_OFFSET_OF(agsaSSPReqReceivedNotify_t, frameTypeHssa));
  OSSA_READ_LE_32(AGROOT, &TlrHdsa, pMsg1, OSSA_OFFSET_OF(agsaSSPReqReceivedNotify_t, TlrHdsa));
  /* deviceId -> agDeviceHandle */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle;

  if (agNULL == pDevice)
  {
    SA_DBG1(("mpiSSPReqReceivedNotify: warning!!! no deviceHandle is found"));
  }
  else
  {
    /* type punning only safe through char *. See gcc -fstrict_aliasing. */
    char * safe_type_pun = (char *)&(pMsg1->SSPIu[0]);
    if( pDevice->initiatorDevHandle.sdkData != agNULL)
    {
      ossaSSPReqReceived(agRoot, &(pDevice->initiatorDevHandle),
                        (agsaFrameHandle_t *)safe_type_pun,
                        (bit16)((iniTagSSPIul >> SHIFT16) & INITTAG_BITS),
                        ((frameTypeHssa >> SHIFT24) & FRAME_TYPE) |
                       ((TlrHdsa >> SHIFT16) & TLR_BITS),
                        (iniTagSSPIul & SSPIUL_BITS));
    }else if( pDevice->targetDevHandle.sdkData != agNULL)
    {
      ossaSSPReqReceived(agRoot, &(pDevice->targetDevHandle),
                        (agsaFrameHandle_t *)safe_type_pun,
                        (bit16)((iniTagSSPIul >> SHIFT16) & INITTAG_BITS),
                        ((frameTypeHssa >> SHIFT24) & FRAME_TYPE) |
                       ((TlrHdsa >> SHIFT16) & TLR_BITS),
                        (iniTagSSPIul & SSPIUL_BITS));
    }else
    {
      SA_ASSERT(0, "Device handle sdkData not set");
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3J");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Device Handle Arrived Event (target mode)
 *
 *  This function handles the Device Handle Arrived Event.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pMsg1        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDeviceHandleArrived(
  agsaRoot_t *agRoot,
  agsaDeviceHandleArrivedNotify_t *pMsg1)
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t    *pDevice;
  agsaPort_t          *pPort;
  agsaSASDeviceInfo_t pDeviceInfo;
  agsaPortContext_t   *agPortContext;
  agsaSASIdentify_t   remoteIdentify;
  bit32               CTag;
  bit32               FwdDeviceId;
  bit32               ProtConrPortId;
  bit32               portId;
  bit32               conRate;
  bit8                i, protocol, dTypeSRate;
  bit32               HostAssignedId;

  if(saRoot == agNULL)
  {
    SA_ASSERT((saRoot != agNULL), "saRoot");
    return AGSA_RC_FAILURE;
  }

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3L");
  /* convert endiness if necassary */
  OSSA_READ_LE_32(AGROOT, &CTag, pMsg1, OSSA_OFFSET_OF(agsaDeviceHandleArrivedNotify_t, CTag));
  OSSA_READ_LE_32(AGROOT, &FwdDeviceId, pMsg1, OSSA_OFFSET_OF(agsaDeviceHandleArrivedNotify_t, HostAssignedIdFwdDeviceId));
  OSSA_READ_LE_32(AGROOT, &ProtConrPortId, pMsg1, OSSA_OFFSET_OF(agsaDeviceHandleArrivedNotify_t, ProtConrPortId));


  if(smIS_SPCV(agRoot))
  {
    portId = ProtConrPortId & PortId_V_MASK;
    conRate = (ProtConrPortId & Conrate_V_MASK ) >> Conrate_V_SHIFT;

    HostAssignedId = (FwdDeviceId & 0xFFFF0000) >> SHIFT16;
    if(HostAssignedId)
    {
      SA_DBG1(("mpiDeviceHandleArrived: HostAssignedId 0x%X\n",HostAssignedId));
    }
  }
  else
  {
    portId = ProtConrPortId & PortId_SPC_MASK;
    conRate = (ProtConrPortId & Conrate_SPC_MASK ) >> Conrate_SPC_SHIFT;
  }
  protocol =(bit8)((ProtConrPortId & PROTOCOL_BITS ) >> PROTOCOL_SHIFT);

  SA_DBG1(("mpiDeviceHandleArrived: New Port portID %d deviceid 0x%X conRate 0x%X protocol 0x%X\n",portId, FwdDeviceId,conRate,protocol));

  /* Port Map */
  agPortContext = saRoot->PortMap[portId].PortContext;
  if (agNULL == agPortContext)
  {
    ossaSingleThreadedEnter(agRoot, LL_PORT_LOCK);
    /* new port */
    /* Acquire port list lock */
    /* Allocate a free port */
    pPort = (agsaPort_t *) saLlistGetHead(&(saRoot->freePorts));
    if (agNULL != pPort)
    {
      saLlistRemove(&(saRoot->freePorts), &(pPort->linkNode));

      /* setup the port data structure */
      pPort->portContext.osData = agNULL;
      pPort->portContext.sdkData = pPort;
      pPort->tobedeleted = agFALSE;
      /* Add to valid port list */
      saLlistAdd(&(saRoot->validPorts), &(pPort->linkNode));
      /* Release port list lock */
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);

      /* Setup portMap based on portId */
      saRoot->PortMap[portId].PortID = portId;
      saRoot->PortMap[portId].PortContext = &(pPort->portContext);
      saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
      pPort->portId = portId;

      pPort->status &= ~PORT_INVALIDATING;
      SA_DBG3(("mpiDeviceHandleArrived: ~PORT_INVALIDATING New Port portID %d PortContext %p\n",saRoot->PortMap[pPort->portId].PortID , &pPort->portContext));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);
      SA_DBG2(("mpiDeviceHandleArrived:Port NULL\n"));
      /* pPort is agNULL*/
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3L");
      return AGSA_RC_FAILURE;
    }
  }
  else
  {
    /* exist port */
    pPort = (agsaPort_t *) (agPortContext->sdkData);
    pPort->status &= ~PORT_INVALIDATING;
    pPort->portId =portId;
    saRoot->PortMap[pPort->portId].PortStatus  &= ~PORT_INVALIDATING;

    SA_DBG1(("mpiDeviceHandleArrived: ~PORT_INVALIDATING Old port portID %d PortContext %p\n", portId, &pPort->portContext));

  }
  /* build Device Information structure */
  si_memset(&pDeviceInfo, 0, sizeof(agsaSASDeviceInfo_t));
  if (ProtConrPortId & PROTOCOL_BITS)
  {
    protocol = SA_IDFRM_SSP_BIT; /* SSP */
    pDeviceInfo.commonDevInfo.devType_S_Rate = (bit8)(conRate | 0x10);

  }
  else
  {
    protocol = SA_IDFRM_SMP_BIT; /* SMP */
    pDeviceInfo.commonDevInfo.devType_S_Rate = (bit8)conRate;
  }
  pDeviceInfo.initiator_ssp_stp_smp = protocol;
  pDeviceInfo.numOfPhys = 1;
  pDeviceInfo.commonDevInfo.sasAddressHi[0] = pMsg1->sasAddrHi[0];
  pDeviceInfo.commonDevInfo.sasAddressHi[1] = pMsg1->sasAddrHi[1];
  pDeviceInfo.commonDevInfo.sasAddressHi[2] = pMsg1->sasAddrHi[2];
  pDeviceInfo.commonDevInfo.sasAddressHi[3] = pMsg1->sasAddrHi[3];
  pDeviceInfo.commonDevInfo.sasAddressLo[0] = pMsg1->sasAddrLow[0];
  pDeviceInfo.commonDevInfo.sasAddressLo[1] = pMsg1->sasAddrLow[1];
  pDeviceInfo.commonDevInfo.sasAddressLo[2] = pMsg1->sasAddrLow[2];
  pDeviceInfo.commonDevInfo.sasAddressLo[3] = pMsg1->sasAddrLow[3];
  pDeviceInfo.commonDevInfo.flag = 0;
  pDeviceInfo.commonDevInfo.it_NexusTimeout = ITL_TO_DEFAULT;

  /* deviceId -> agDeviceHandle */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[FwdDeviceId & DEVICE_ID_BITS].DeviceHandle;

  if (agNULL == pDevice)
  {
    /* new device */
    si_memset(&remoteIdentify, 0, sizeof(agsaSASIdentify_t));
    for (i=0;i<4;i++)
    {
      remoteIdentify.sasAddressHi[i] = pMsg1->sasAddrHi[i];
      remoteIdentify.sasAddressLo[i] = pMsg1->sasAddrLow[i];
    }
    remoteIdentify.deviceType_addressFrameType = (bit8)(pDeviceInfo.commonDevInfo.devType_S_Rate & 0xC0);
    dTypeSRate = pDeviceInfo.commonDevInfo.devType_S_Rate;
    /* get Device from free Device List */
    pDevice = siPortSASDeviceAdd(agRoot, pPort, remoteIdentify, agTRUE, SMP_TO_DEFAULT, ITL_TO_DEFAULT, 0, dTypeSRate, 0);
    if (agNULL == pDevice)
    {
      SA_DBG1(("mpiDeviceHandleArrived: Device Handle is NULL, Out of Resources Error.\n"));
    }
    else
    {
      bit32 AccStatus = 0;
      bit32 SaveId = FwdDeviceId & 0xFFFF;
      /* mapping the device handle and device id */
      saRoot->DeviceMap[FwdDeviceId & DEVICE_ID_BITS].DeviceIdFromFW = FwdDeviceId;
      saRoot->DeviceMap[FwdDeviceId & DEVICE_ID_BITS].DeviceHandle = (void *)pDevice;
      pDevice->DeviceMapIndex = FwdDeviceId;
      SA_DBG2(("mpiDeviceHandleArrived: New deviceID 0x%x Device Context %p DeviceTypeSRate 0x%x\n", FwdDeviceId, pDevice, dTypeSRate));

      /* Call Back */
      AccStatus = ossaDeviceHandleAccept(agRoot, &(pDevice->initiatorDevHandle), &pDeviceInfo, agPortContext,&FwdDeviceId );

      HostAssignedId = (FwdDeviceId & 0xFFFF0000) >> SHIFT16;
      if(HostAssignedId)
      {
        if( SaveId == (FwdDeviceId & 0xFFFF)  )
        {

          saRoot->DeviceMap[FwdDeviceId & DEVICE_ID_BITS].DeviceIdFromFW = FwdDeviceId;
          pDevice->DeviceMapIndex = FwdDeviceId;

          SA_DBG1(("mpiDeviceHandleArrived:FwdDeviceId 0x%x HostAssignedId 0x%x\n",FwdDeviceId,HostAssignedId));
        }
        else
        {
          SA_DBG1(("mpiDeviceHandleArrived:Id mangled expect 0x%x Got 0x%x\n",SaveId, (FwdDeviceId & 0xFFFF)));
          ret = AGSA_RC_FAILURE;
        }
      }

      /* get AWT flag and ITLN_TMO value */

      if(AccStatus == OSSA_RC_ACCEPT )
      {
        /* build DEVICE_HANDLE_ACCEPT IOMB and send to SPC with action=accept */
        mpiDevHandleAcceptCmd(agRoot, agNULL, CTag, FwdDeviceId, 0, pDeviceInfo.commonDevInfo.flag, pDeviceInfo.commonDevInfo.it_NexusTimeout, 0);
      }
      else
      {
        mpiDevHandleAcceptCmd(agRoot, agNULL, CTag, FwdDeviceId, 1, pDeviceInfo.commonDevInfo.flag, pDeviceInfo.commonDevInfo.it_NexusTimeout, 0);
      }
    }
  }

  SA_DBG1(("mpiDeviceHandleArrived Device 0x%08X flag 0x%08X %s WWID= %02x%02x%02x%02x:%02x%02x%02x%02x, %s\n",
    FwdDeviceId,
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.flag,
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x20 ? "SATA DA" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x10 ? "SSP/SMP" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF0) == 0x0 ? "  STP  " : "Unknown",

    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[3],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[2],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[1],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressHi[0],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[3],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[2],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[1],
    pDevice->devInfo.sasDeviceInfo.commonDevInfo.sasAddressLo[0],

    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 8  ? " 1.5G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 9  ? " 3.0G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 10 ? " 6.0G" :
    (pDevice->devInfo.sasDeviceInfo.commonDevInfo.devType_S_Rate & 0xF) == 11 ? "12.0G" : "????" ));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3L");
  return ret;
}

/******************************************************************************/
/*! \brief Get Time Stamp Response
 *
 *  This routine handles the response of Get Time Stamp Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetTimeStampRsp(
  agsaRoot_t               *agRoot,
  agsaGetTimeStampRsp_t    *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag, timeStampLower, timeStampUpper;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3M");

  SA_DBG3(("mpiGetTimeStampRsp: HTAG=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetTimeStampRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &timeStampLower, pIomb, OSSA_OFFSET_OF(agsaGetTimeStampRsp_t, timeStampLower));
  OSSA_READ_LE_32(AGROOT, &timeStampUpper, pIomb, OSSA_OFFSET_OF(agsaGetTimeStampRsp_t, timeStampUpper));
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetTimeStampRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x\n", tag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3M");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  SA_DBG3(("mpiGetTimeStampRsp: timeStampLower 0x%x timeStampUpper 0x%x\n", timeStampLower, timeStampUpper));

  ossaGetTimeStampCB(agRoot, agContext, timeStampLower, timeStampUpper);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetTimeStampRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3M");
  return ret;
}

/******************************************************************************/
/*! \brief SAS HW Event Ack Response
 *
 *  This routine handles the response of SAS HW Event Ack Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSASHwEventAckRsp(
  agsaRoot_t               *agRoot,
  agsaSASHwEventAckRsp_t   *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  agsaPort_t          *pPort;
  bit32               tag, status;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2N");

  SA_DBG2(("mpiSASHwEventAckRsp: Htag=0x%x %p\n", pIomb->tag,pIomb));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSASHwEventAckRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSASHwEventAckRsp_t, status));
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSASHwEventAckRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x Status=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2N");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  SA_ASSERT((pRequest->valid), "pRequest->valid");

  SA_DBG1(("mpiSASHwEventAckRsp: status 0x%x Htag=0x%x HwAckType=0x%x\n",status,pIomb->tag,pRequest->HwAckType ));

  ossaHwEventAckCB(agRoot, agContext, status);

  pPort = pRequest->pPort;
  if (agNULL != pPort)
  {
    SA_DBG1(("mpiSASHwEventAckRsp: pPort %p tobedeleted %d\n", pPort, pPort->tobedeleted));
    if (pPort->status & PORT_INVALIDATING &&  pPort->tobedeleted )
    {
      SA_DBG1(("mpiSASHwEventAckRsp: PORT_INVALIDATING portInvalid portID %d pPort %p, nulling out PortContext\n", pPort->portId, pPort));
      /* invalid the port */
      siPortInvalid(agRoot, pPort);
      /* map out the portmap */
      saRoot->PortMap[pPort->portId].PortContext = agNULL;
      saRoot->PortMap[pPort->portId].PortID = PORT_MARK_OFF;
      saRoot->PortMap[pPort->portId].PortStatus  |= PORT_INVALIDATING;
    }
    else
    {
      SA_DBG1(("mpiSASHwEventAckRsp:pPort->status 0x%x Htag=0x%x %p\n",pPort->status, pIomb->tag,pIomb));
    }
  }
  else
  {
    SA_DBG1(("mpiSASHwEventAckRsp: pPort is NULL, no portId, HTag=0x%x\n", tag));
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSASHwEventAckRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2N");
  return ret;
}

/******************************************************************************/
/*! \brief Port Control Response
 *
 *  This routine handles the response of SAS HW Event Ack Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiPortControlRsp(
  agsaRoot_t           *agRoot,
  agsaPortControlRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest = agNULL;
  agsaContext_t       *agContext = agNULL;
  agsaPortContext_t   *agPortContext = agNULL;
  bit32               tag;
  bit32               port =0;
  bit32               operation =0;
  bit32               status =0;
  bit32               portState =0;
  bit32               portOperation =0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3O");

  SA_DBG2(("mpiPortControlRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaPortControlRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &operation, pIomb, OSSA_OFFSET_OF(agsaPortControlRsp_t, portOPPortId));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaPortControlRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &portState, pIomb, OSSA_OFFSET_OF(agsaPortControlRsp_t,rsvdPortState ));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiPortControlRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x Status=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3O");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  if(!pRequest->valid)
  {
    SA_DBG1(("mpiPortControlRsp: pRequest->valid %d not set\n", pRequest->valid));
  }

  SA_DBG2(("mpiPortControlRsp: pRequest->completionCB %p\n", pRequest->completionCB));

  port = operation & PORTID_MASK;

  if(port < AGSA_MAX_VALID_PORTS )
  {
    SA_DBG2(("mpiPortControlRsp: PortID 0x%x PortStatus 0x%x PortContext %p\n",
           saRoot->PortMap[port].PortID,
           saRoot->PortMap[port].PortStatus,
           saRoot->PortMap[port].PortContext));

    agPortContext = (agsaPortContext_t *)saRoot->PortMap[port].PortContext;
  }
  SA_DBG2(("mpiPortControlRsp: PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[operation & PORTID_MASK].PortID,saRoot->PortMap[operation & PORTID_MASK].PortStatus,saRoot->PortMap[operation & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[operation & PORTID_MASK].PortContext;
  SA_DBG1(("mpiPortControlRsp: agPortContext %p\n",agPortContext ));


  SA_DBG2(("mpiPortControlRsp: portID 0x%x status 0x%x\n", (operation & PORTID_MASK), status));

  SA_DBG1(("mpiPortControlRsp: portID 0x%x status 0x%x agPortContext %p\n",port, status,agPortContext));

  portOperation = (((operation & LOCAL_PHY_OP_BITS) >> SHIFT8) | (portState << SHIFT28) );

  SA_DBG1(("mpiPortControlRsp: portState 0x%x operation 0x%x portOperation 0x%x\n",portState, operation,portOperation ));

  switch(portOperation)
  {
    case AGSA_PORT_SET_SMP_PHY_WIDTH:
      SA_DBG1(("mpiPortControlRsp: AGSA_PORT_SET_SMP_PHY_WIDTH  operation 0x%x\n",operation ));
      break;
    case AGSA_PORT_SET_PORT_RECOVERY_TIME:
      SA_DBG1(("mpiPortControlRsp: AGSA_PORT_SET_PORT_RECOVERY_TIME  operation 0x%x\n",operation ));
      break;
    case AGSA_PORT_IO_ABORT:
      SA_DBG1(("mpiPortControlRsp: AGSA_PORT_IO_ABORT  operation 0x%x\n",operation ));
      break;
    case AGSA_PORT_SET_PORT_RESET_TIME:
      SA_DBG1(("mpiPortControlRsp: AGSA_PORT_SET_PORT_RESET_TIME  operation 0x%x\n",operation ));
      break;
    case AGSA_PORT_HARD_RESET:
      SA_DBG1(("mpiPortControlRsp: AGSA_PORT_HARD_RESET  operation 0x%x\n",operation ));
      break;
    case AGSA_PORT_CLEAN_UP:
      SA_DBG1(("mpiPortControlRsp: AGSA_PORT_CLEAN_UP  operation 0x%x\n",operation ));
      break;
    case AGSA_STOP_PORT_RECOVERY_TIMER:
      SA_DBG1(("mpiPortControlRsp: AGSA_STOP_PORT_RECOVERY_TIMER  operation 0x%x\n",operation ));
      break;
    default:
    {
      SA_DBG1(("mpiPortControlRsp: Unknown  operation 0x%x\n",operation ));
    }
  }

  ossaPortControlCB(agRoot, agContext, agPortContext, portOperation, status);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiPortControlRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3O");
  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SMP ABORT Response
 *
 *  This function handles the SMP Abort Response.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSMPAbortRsp(
  agsaRoot_t         *agRoot,
  agsaSMPAbortRsp_t  *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDeviceDesc_t    *pDevice;
  bit32               tag, scp, status;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3P");

  SA_DBG3(("mpiSMPAbortRsp: HTag=0x%x Status=0x%x\n", pIomb->tag, pIomb->status));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSMPAbortRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSMPAbortRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &scp, pIomb, OSSA_OFFSET_OF(agsaSMPAbortRsp_t, scp));

  /* get IORequest from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;

  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSMPAbortRsp: pRequest is NULL, HTag=0x%x Status=0x%x\n", pIomb->tag, pIomb->status));
    SA_ASSERT((pRequest), "pRequest");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3P");
    return AGSA_RC_FAILURE;
  }

  if ( agTRUE == pRequest->valid )
  {
    pDevice = pRequest->pDevice;
    SA_ASSERT((pRequest->pDevice), "pRequest->pDevice");

    SA_DBG3(("mpiSMPAbortRsp: request abort is valid Htag 0x%x\n", tag));

    /* remove the SSP_ABORT or SATA_ABORT request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;
    saRoot->IOMap[tag].agContext = agNULL;

    if( pRequest->completionCB == agNULL )
    {
      SA_DBG1(("mpiSMPAbortRsp: ************************************************* Valid for Expander only tag 0x%x\n", tag));
      ossaSMPAbortCB(agRoot, pRequest->pIORequestContext, scp, status);
    }
    else
    {
      (*(ossaGenericAbortCB_t)(pRequest->completionCB))(agRoot, pRequest->pIORequestContext, scp, status);
    }

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* Delete the request from the pendingIORequests */
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiSMPAbortRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  }
  else
  {
    ret = AGSA_RC_FAILURE;
    SA_DBG1(("mpiSMPAbortRsp: the request is not valid any more. Tag=%x\n", pRequest->HTag));
  }


  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3P");

  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Device Handle Arrived Event (target mode)
 *
 *  This function handles the Device Handle Arrived Event.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pMsg1        pointer of Message
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDeviceHandleRemoval(
  agsaRoot_t *agRoot,
  agsaDeviceHandleRemoval_t *pMsg1)
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t    *pDevice;
  agsaPortContext_t   *agPortContext;
  bit32               portId;
  bit32               deviceid, deviceIdx;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3R");

  /* convert endiness if necassary */
  OSSA_READ_LE_32(AGROOT, &portId, pMsg1, OSSA_OFFSET_OF(agsaDeviceHandleRemoval_t, portId));
  OSSA_READ_LE_32(AGROOT, &deviceid, pMsg1, OSSA_OFFSET_OF(agsaDeviceHandleRemoval_t, deviceId));

  SA_DBG3(("mpiDeviceHandleRemoval: portId=0x%x deviceId=0x%x\n", portId, deviceid));

  pDevice = saRoot->DeviceMap[deviceid & DEVICE_ID_BITS].DeviceHandle;
  SA_DBG2(("mpiDeviceHandleRemoval:PortID 0x%x PortStatus 0x%x PortContext %p\n",
          saRoot->PortMap[portId & PORTID_MASK].PortID,
          saRoot->PortMap[portId & PORTID_MASK].PortStatus,
          saRoot->PortMap[portId & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId & PORTID_MASK].PortContext;

  /* Call Back */
  SA_DBG1(("mpiDeviceHandleRemoval: portId=0x%x deviceId=0x%x autoDeregDeviceflag=0x%x\n", portId, deviceid,saRoot->autoDeregDeviceflag[portId & PORTID_MASK]));
  if (pDevice->targetDevHandle.sdkData)
  {
    ossaDeviceHandleRemovedEvent(agRoot, &(pDevice->targetDevHandle), agPortContext);

    if (saRoot->autoDeregDeviceflag[portId & PORTID_MASK])
    {
      /* remove the DeviceMap and MapIndex */
      deviceIdx = pDevice->DeviceMapIndex & DEVICE_ID_BITS;
      SA_DBG1(("mpiDeviceHandleRemoval: A  Freed portId=0x%x deviceId=0x%x\n", portId, deviceid));
      OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");

      saRoot->DeviceMap[deviceIdx].DeviceIdFromFW = 0;
      saRoot->DeviceMap[deviceIdx].DeviceHandle = agNULL;
      pDevice->DeviceMapIndex = 0;

      /* Reset the device data structure */
      pDevice->pPort = agNULL;
      pDevice->targetDevHandle.sdkData = agNULL;
      pDevice->targetDevHandle.osData = agNULL;
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      saLlistAdd(&(saRoot->freeDevicesList), &(pDevice->linkNode));
      SA_DBG1(("mpiDeviceHandleRemoval: portId=0x%x deviceId=0x%x\n", portId, deviceid));

      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    }
  }
  else
  {
    if (pDevice->initiatorDevHandle.sdkData)
    {
      ossaDeviceHandleRemovedEvent(agRoot, &(pDevice->initiatorDevHandle), agPortContext);

      if (saRoot->autoDeregDeviceflag[portId & PORTID_MASK])
      {
        /* remove the DeviceMap and MapIndex */
        deviceIdx = pDevice->DeviceMapIndex & DEVICE_ID_BITS;
        SA_DBG1(("mpiDeviceHandleRemoval: A  Freed portId=0x%x deviceId=0x%x\n", portId, deviceid));
        OS_ASSERT(deviceIdx < MAX_IO_DEVICE_ENTRIES, "deviceIdx MAX_IO_DEVICE_ENTRIES");
        saRoot->DeviceMap[deviceIdx].DeviceIdFromFW = 0;
        saRoot->DeviceMap[deviceIdx].DeviceHandle = agNULL;
        pDevice->DeviceMapIndex = 0;

        /* Reset the device data structure */
        pDevice->pPort = agNULL;
        pDevice->initiatorDevHandle.sdkData = agNULL;
        pDevice->initiatorDevHandle.osData = agNULL;
        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        saLlistAdd(&(saRoot->freeDevicesList), &(pDevice->linkNode));
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      }
    }
    else
    {
      /* no callback because bad device_id */
      SA_DBG1(("mpiDeviceHandleRemoval: Bad Device Handle, deviceId=0x%x\n", deviceid));
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3R");
  return ret;
}

/******************************************************************************/
/*! \brief Set Device State Response
 *
 *  This routine handles the response of SET Device State Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetDeviceStateRsp(
  agsaRoot_t             *agRoot,
  agsaSetDeviceStateRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  agsaContext_t       *agContext;
  bit32               tag, status, deviceState, deviceId;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3Q");

  SA_DBG1(("mpiSetDeviceStateRsp: HTag=0x%x, deviceId=0x%x\n", pIomb->tag, pIomb->deviceId));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSetDeviceStateRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &deviceId, pIomb, OSSA_OFFSET_OF(agsaSetDeviceStateRsp_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSetDeviceStateRsp_t, status));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSetDeviceStateRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3Q");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* status is SUCCESS */
  OSSA_READ_LE_32(AGROOT, &deviceState, pIomb, OSSA_OFFSET_OF(agsaSetDeviceStateRsp_t, pds_nds));

  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceId & DEVICE_ID_BITS].DeviceHandle;
  if (agNULL == pDevice)
  {
    SA_DBG1(("mpiSetDeviceStateRsp: DeviceHandle is NULL!!! deviceId=0x%x TAG=0x%x STATUS=0x%x \n", deviceId, tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3Q");
    return AGSA_RC_FAILURE;
  }

  if (pDevice->targetDevHandle.sdkData)
  {
    agDevHandle = &(pDevice->targetDevHandle);
  }
  else
  {
    agDevHandle = &(pDevice->initiatorDevHandle);
  }

  if (agDevHandle == agNULL)
  {
    SA_DBG1(("mpiSetDeviceStateRsp: warning!!! no deviceHandle is found"));
    ossaSetDeviceStateCB(agRoot, agContext, agNULL, OSSA_IO_NO_DEVICE, 0, 0);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "3Q");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiSetDeviceStateRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  ossaSetDeviceStateCB(agRoot, agContext, agDevHandle, status, (deviceState & NDS_BITS),
                      (deviceState & PDS_BITS) >> SHIFT4);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSetDeviceStateRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "3Q");
  return ret;
}

/******************************************************************************/
/*! \brief Get Device State Response
 *
 *  This routine handles the response of GET Device State Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDeviceStateRsp(
  agsaRoot_t             *agRoot,
  agsaGetDeviceStateRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaDevHandle_t     *agDevHandle;
  agsaDeviceDesc_t    *pDevice;
  agsaContext_t       *agContext;
  bit32               tag, status, deviceId, deviceState;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3W");

  SA_DBG1(("mpiGetDeviceStateRsp: HTag=0x%x, deviceId=0x%x\n", pIomb->tag, pIomb->deviceId));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetDeviceStateRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &deviceId, pIomb, OSSA_OFFSET_OF(agsaGetDeviceStateRsp_t, deviceId));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaGetDeviceStateRsp_t, status));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetDeviceStateRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3W");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* status is SUCCESS */
  OSSA_READ_LE_32(AGROOT, &deviceState, pIomb, OSSA_OFFSET_OF(agsaGetDeviceStateRsp_t, ds));

  /* find device handle from device index */
  pDevice = (agsaDeviceDesc_t *)saRoot->DeviceMap[deviceId & DEVICE_ID_BITS].DeviceHandle;
  if (pDevice != agNULL)
  {
    if (pDevice->targetDevHandle.sdkData)
    {
      agDevHandle = &(pDevice->targetDevHandle);
    }
    else
    {
      agDevHandle = &(pDevice->initiatorDevHandle);
    }
  }
  else
  {
    SA_DBG1(("mpiGetDeviceStateRsp: pDevice is NULL"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3W");
    return AGSA_RC_FAILURE;
  }

  if (agDevHandle == agNULL)
  {
    SA_DBG1(("mpiGetDeviceStateRsp: warning!!! no deviceHandle is found"));
    ossaGetDeviceStateCB(agRoot, agContext, agNULL, OSSA_IO_NO_DEVICE, 0);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "3W");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiGetDeviceStateRsp: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  ossaGetDeviceStateCB(agRoot, agContext, agDevHandle, status, deviceState);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetDeviceStateRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "3W");
  return ret;
}

/******************************************************************************/
/*! \brief SAS ReInitialize Response
 *
 *  This routine handles the response of SAS Reinitialize Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSasReInitializeRsp(
  agsaRoot_t               *agRoot,
  agsaSasReInitializeRsp_t *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  agsaSASReconfig_t   SASReconfig;
  bit32               tag, status, setFlags, MaxPorts;
  bit32               openRejReCmdData, sataHOLTMO;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3X");

  SA_DBG1(("mpiSasReInitializeRsp: HTag=0x%x, status=0x%x\n", pIomb->tag, pIomb->status));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSasReInitializeRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSasReInitializeRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &setFlags, pIomb, OSSA_OFFSET_OF(agsaSasReInitializeRsp_t, setFlags));
  OSSA_READ_LE_32(AGROOT, &MaxPorts, pIomb, OSSA_OFFSET_OF(agsaSasReInitializeRsp_t, MaxPorts));
  OSSA_READ_LE_32(AGROOT, &openRejReCmdData, pIomb, OSSA_OFFSET_OF(agsaSasReInitializeRsp_t, openRejReCmdData));
  OSSA_READ_LE_32(AGROOT, &sataHOLTMO, pIomb, OSSA_OFFSET_OF(agsaSasReInitializeRsp_t, sataHOLTMO));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSasReInitializeRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3X");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  SASReconfig.flags = setFlags;
  SASReconfig.maxPorts = (bit8)(MaxPorts & 0xFF);
  SASReconfig.openRejectRetriesCmd = (bit16)((openRejReCmdData & 0xFFFF0000) >> SHIFT16);
  SASReconfig.openRejectRetriesData = (bit16)(openRejReCmdData & 0x0000FFFF);
  SASReconfig.sataHolTmo = (bit16)(sataHOLTMO & 0xFFFF);
  ossaReconfigSASParamsCB(agRoot, agContext, status, &SASReconfig);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSasReInitializeRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3X");

  return ret;
}

/******************************************************************************/
/*! \brief serial GPIO Response
 *
 *  This routine handles the response of serial GPIO Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSGpioRsp(
  agsaRoot_t        *agRoot,
  agsaSGpioRsp_t    *pInIomb
  )
{
  bit32                     ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t              *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t       *pRequest = NULL;
  agsaContext_t             *agContext = NULL;
  bit32                     i, tag, resultFunctionFrameType;
  agsaSGpioReqResponse_t    SgpioResponse = {0};

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3Y");

  SA_DBG3(("mpiSGpioRsp: HTAG=0x%x\n", pInIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pInIomb, OSSA_OFFSET_OF(agsaSGpioRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &resultFunctionFrameType, pInIomb, OSSA_OFFSET_OF(agsaSGpioRsp_t, resultFunctionFrameType));
  
  SgpioResponse.smpFrameType = resultFunctionFrameType & 0xFF;
  SgpioResponse.function = (resultFunctionFrameType & 0xFF00) >> 8;
  SgpioResponse.functionResult = (resultFunctionFrameType & 0xFF0000) >> 16;
  
  if (SA_SAS_SMP_READ_GPIO_REGISTER == SgpioResponse.function)
  {
    for (i = 0; i < OSSA_SGPIO_MAX_READ_DATA_COUNT; i++)
    {
      OSSA_READ_LE_32(AGROOT, &SgpioResponse.readWriteData[i], pInIomb, OSSA_OFFSET_OF(agsaSGpioRsp_t, readData) + (i * 4));
    }
  }

  /* Get the request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSGpioRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x STATUS=0x%x\n", tag, SgpioResponse.functionResult));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3Y");
    ret = AGSA_RC_FAILURE;
  }
  else
  {
    agContext = saRoot->IOMap[tag].agContext;
    ossaSGpioCB(agRoot, agContext, &SgpioResponse);

    /* Return the request to free pool */
    saReturnRequestToFreePool(agRoot, pRequest);

    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3Y");
  }
  
  return ret;
}

/******************************************************************************/
/*! \brief PCIE Diagnostics Response
 *
 *  This routine handles the response of PCIE Diagnostics Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiPCIeDiagExecuteRsp(
  agsaRoot_t                *agRoot,
  void                      *pInIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = agNULL;
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag, Status, Command;
  agsaPCIeDiagResponse_t pciediadrsp;
  bit32  *pIomb = (bit32  *)pInIomb;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3Z");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  si_memset(&pciediadrsp, 0, sizeof(agsaPCIeDiagResponse_t));

  if(smIS_SPCV(agRoot))
  {
    OSSA_READ_LE_32(AGROOT, &tag,                  pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,tag));
    OSSA_READ_LE_32(AGROOT, &Command,              pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,CmdTypeDesc));
    OSSA_READ_LE_32(AGROOT, &Status,               pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,Status));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.ERR_BLKH, pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,ERR_BLKH ));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.ERR_BLKL, pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,ERR_BLKL ));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.DWord8,   pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,DWord8 ));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.DWord9,   pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,DWord9 ));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.DWord10,  pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,DWord10 ));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.DWord11,  pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,DWord11 ));
    OSSA_READ_LE_32(AGROOT, &pciediadrsp.DIF_ERR,  pIomb, OSSA_OFFSET_OF(agsaPCIeDiagExecuteRsp_t,DIF_ERR ));
    SA_DBG3(("mpiPCIeDiagExecuteRsp: HTAG=0x%x\n",tag));
  }
  else
  {
    OSSA_READ_LE_32(AGROOT, &tag,        pIomb,           OSSA_OFFSET_OF(agsa_SPC_PCIeDiagExecuteRsp_t,tag));
    OSSA_READ_LE_32(AGROOT, &Command,    pIomb,           OSSA_OFFSET_OF(agsa_SPC_PCIeDiagExecuteRsp_t,CmdTypeDesc));
    OSSA_READ_LE_32(AGROOT, &Status,     pIomb,           OSSA_OFFSET_OF(agsa_SPC_PCIeDiagExecuteRsp_t,Status));
    SA_DBG3(("mpiPCIeDiagExecuteRsp: SPC HTAG=0x%x\n",tag));
  }

  switch(Status)
  {
    case OSSA_PCIE_DIAG_SUCCESS:
      SA_DBG3(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_SUCCESS TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_IO_INVALID_LENGTH:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_IO_INVALID_LENGTH TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_INVALID_COMMAND:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_INVALID_COMMAND TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_INTERNAL_FAILURE:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_INTERNAL_FAILURE TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_INVALID_CMD_TYPE:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_INVALID_CMD_TYPE TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_INVALID_CMD_DESC:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_INVALID_CMD_DESC TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_CRC_MISMATCH:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_CRC_MISMATCH TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_INVALID_PCIE_ADDR:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_INVALID_PCIE_ADDR TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_INVALID_BLOCK_SIZE:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_INVALID_BLOCK_SIZE TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_LENGTH_NOT_BLOCK_SIZE_ALIGNED:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_LENGTH_NOT_BLOCK_SIZE_ALIGNED TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_MISMATCH:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_MISMATCH TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    case OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
      SA_DBG1(("mpiPCIeDiagExecuteRsp: OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
    default:
      SA_DBG1(("mpiPCIeDiagExecuteRsp:  UNKNOWN status TAG=0x%x STATUS=0x%x\n", tag, Status));
      break;
  }
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiPCIeDiagExecuteRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x STATUS=0x%x\n", tag, Status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3Z");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  ossaPCIeDiagExecuteCB(agRoot, agContext, Status, Command,&pciediadrsp);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiPCIeDiagExecuteRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3Z");

  /* return value */
  return ret;
}
/******************************************************************************/
/*! \brief Get DFE Data command Response
 *
 *  This routine handles the response of Get DFE Data command Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDFEDataRsp(
  agsaRoot_t    *agRoot,
  void          *pIomb
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = agNULL;
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag = 0, status = 0, In_Ln = 0, MCNT = 0, NBT = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2Y");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  if(smIS_SPCV(agRoot))
  {
    OSSA_READ_LE_32(AGROOT, &tag,                pIomb, OSSA_OFFSET_OF(agsaGetDDEFDataRsp_t,tag));
    OSSA_READ_LE_32(AGROOT, &status,             pIomb, OSSA_OFFSET_OF(agsaGetDDEFDataRsp_t,status));
    OSSA_READ_LE_32(AGROOT, &In_Ln,              pIomb, OSSA_OFFSET_OF(agsaGetDDEFDataRsp_t,reserved_In_Ln));
    OSSA_READ_LE_32(AGROOT, &MCNT,               pIomb, OSSA_OFFSET_OF(agsaGetDDEFDataRsp_t,MCNT));
    OSSA_READ_LE_32(AGROOT, &NBT,                pIomb, OSSA_OFFSET_OF(agsaGetDDEFDataRsp_t,NBT));
  }
  else
  {
    /* SPC does not support this command */
  }

  switch(status)
  {
    case OSSA_DFE_MPI_IO_SUCCESS:
      SA_DBG3(("mpiGetDFEDataRsp: OSSA_DFE_MPI_IO_SUCCESS TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    case OSSA_DFE_DATA_OVERFLOW:
      SA_DBG1(("mpiGetDFEDataRsp: OSSA_DFE_DATA_OVERFLOW TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    case OSSA_DFE_MPI_ERR_RESOURCE_UNAVAILABLE:
      SA_DBG1(("mpiGetDFEDataRsp: OSSA_DFE_MPI_ERR_RESOURCE_UNAVAILABLE TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    case OSSA_DFE_CHANNEL_DOWN:
      SA_DBG1(("mpiGetDFEDataRsp: OSSA_DFE_CHANNEL_DOWN TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    case OSSA_DFE_MEASUREMENT_IN_PROGRESS:
      SA_DBG1(("mpiGetDFEDataRsp: OSSA_DFE_MEASUREMENT_IN_PROGRESS TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    case OSSA_DFE_CHANNEL_INVALID:
      SA_DBG1(("mpiGetDFEDataRsp: OSSA_DFE_CHANNEL_INVALID TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    case OSSA_DFE_DMA_FAILURE:
      SA_DBG1(("mpiGetDFEDataRsp: OSSA_DFE_DMA_FAILURE TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
    default:
      SA_DBG1(("mpiGetDFEDataRsp:  UNKNOWN status TAG=0x%x STATUS=0x%x\n", tag, status));
      break;
   }

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetDFEDataRsp: Bad Response IOMB!!! pRequest is NULL.TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2Y");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  ossaGetDFEDataCB(agRoot, agContext, status, NBT);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetDFEDataRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2Y");

  return ret;
}


/******************************************************************************/
/*! \brief SAS Set Controller Config Response
 *
 *  This routine handles the response of Set Controller Config Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetControllerConfigRsp(
  agsaRoot_t                   *agRoot,
  agsaSetControllerConfigRsp_t *pIomb
  )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest;
  agsaHWEventMode_t     agMode;
  bit32                 status, errorQualifierPage, tag;
  bit32                 errorQualifier;
  bit32                 pagetype;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3a");

  SA_DBG1(("mpiSetControllerConfigRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSetControllerConfigRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSetControllerConfigRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &errorQualifierPage, pIomb, OSSA_OFFSET_OF(agsaSetControllerConfigRsp_t, errorQualifierPage));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSetControllerConfigRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3a");
    return AGSA_RC_FAILURE;
  }

  si_memset(&agMode, 0, sizeof(agsaHWEventMode_t));
  agMode.modePageOperation = agsaModePageSet;
  agMode.status = status;
  agMode.context = saRoot->IOMap[tag].agContext;
  errorQualifier = (errorQualifierPage & 0xFFFF0000) >> SHIFT16;
  pagetype = (errorQualifierPage & 0xFF);

  if(status )
  {
    SA_DBG1(("mpiSetControllerConfigRsp: Error detected tag 0x%x pagetype 0x%x status 0x%x errorQualifier 0x%x\n", 
      tag, pagetype,status, errorQualifier));
  }
  else
  {
    SA_DBG1(("mpiSetControllerConfigRsp: tag 0x%x pagetype 0x%x status 0x%x\n", tag, pagetype,status ));
  }


  switch( pagetype)
  {
    case AGSA_ENCRYPTION_DEK_CONFIG_PAGE:
    case AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE:
    case AGSA_INTERRUPT_CONFIGURATION_PAGE:
    case AGSA_ENCRYPTION_HMAC_CONFIG_PAGE:
    case AGSA_IO_GENERAL_CONFIG_PAGE:
    /*case AGSA_ENCRYPTION_CONTROL_PARM_PAGE:*/
      /* Report the event before freeing the IOMB */
      SA_DBG1(("mpiSetControllerConfigRsp:OSSA_HW_EVENT_MODE\n"));
      ossaHwCB(agRoot,agMode.context, OSSA_HW_EVENT_MODE, errorQualifierPage, (void *) &agMode, 0);
  

      break;

    case AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE:
      SA_DBG1(("mpiSetControllerConfigRsp:warning!!!! GENERAL_CONFIG_PAGE is read only, cannot be set\n"));
      break;

    /* why we need to read the scrach pad register when handling ENCRYPTION_SECURITY_PARM_PAGE??? */
    case AGSA_ENCRYPTION_CONTROL_PARM_PAGE:
    {
      bit32 ScratchPad1 = 0;
      bit32 ScratchPad3 = 0;
      agsaEncryptInfo_t encrypt;
      agsaEncryptInfo_t *encryptInfo = &encrypt;
      SA_DBG1(("mpiSetControllerConfigRsp: AGSA_ENCRYPTION_CONTROL_PARM_PAGE\n" ));

      if( pRequest->modePageContext)
      {
        pRequest->modePageContext = agFALSE;
      }

      si_memset(&encrypt, 0, sizeof(agsaEncryptInfo_t));
      encryptInfo->status = 0;
      encryptInfo->encryptionCipherMode = 0;
      encryptInfo->encryptionSecurityMode = 0;

      ScratchPad1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register);
      ScratchPad3 = ossaHwRegRead(agRoot,V_Scratchpad_3_Register);
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
      if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) ==  SCRATCH_PAD1_V_RAAE_MASK)
      {
        if((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK) == SCRATCH_PAD3_V_ENC_READY ) /* 3 */
        {
          encryptInfo->status = AGSA_RC_SUCCESS;
        }
        else if((ScratchPad3 & SCRATCH_PAD3_V_ENC_READY) == SCRATCH_PAD3_V_ENC_DISABLED) /* 0 */
        {
          encryptInfo->status = 0xFFFF;
          encryptInfo->encryptionCipherMode = 0;
          encryptInfo->encryptionSecurityMode = 0;
        }
        else if((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK ) == SCRATCH_PAD3_V_ENC_DIS_ERR) /* 1 */
        {
          encryptInfo->status = (ScratchPad3 & SCRATCH_PAD3_V_ERR_CODE ) >> SHIFT16;
        }
        else if((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK ) == SCRATCH_PAD3_V_ENC_ENA_ERR) /* 2 */
        {
          encryptInfo->status = (ScratchPad3 & SCRATCH_PAD3_V_ERR_CODE ) >> SHIFT16;
        }
      }
      else  if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) ==  SCRATCH_PAD1_V_RAAE_ERR)
      {
        SA_DBG1(("mpiSetControllerConfigRsp, RAAE not ready SPC AGSA_RC_FAILURE\n"));
        encryptInfo->status = 0xFFFF;
        encryptInfo->encryptionCipherMode = 0;
        encryptInfo->encryptionSecurityMode = 0;
      }
      else  if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) == 0x0 )
      {
        SA_DBG2(("mpiSetControllerConfigRsp, RAAE not ready AGSA_RC_BUSY\n"));
      }

      SA_DBG2(("mpiSetControllerConfigRsp, encryptionCipherMode 0x%x encryptionSecurityMode 0x%x status 0x%x\n",
                encryptInfo->encryptionCipherMode,
                encryptInfo->encryptionSecurityMode,
                encryptInfo->status));
      SA_DBG2(("mpiSetControllerConfigRsp, ScratchPad3 0x%x\n",ScratchPad3));
      SA_DBG1(("mpiSetControllerConfigRsp:AGSA_ENCRYPTION_CONTROL_PARM_PAGE 0x%X\n", agMode.modePageOperation));
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_SECURITY_MODE, errorQualifier, (void *)encryptInfo, agMode.context);
      break;
    }

    default:
      SA_DBG1(("mpiSetControllerConfigRsp: Unknown page code 0x%X\n", pagetype));
      break;
  }

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSetControllerRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3a");
  return AGSA_RC_SUCCESS;

}

/******************************************************************************/
/*! \brief SAS Get Controller Config Response
 *
 *  This routine handles the response of Get Controller Config Command
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetControllerConfigRsp(
  agsaRoot_t               *agRoot,
  agsaGetControllerConfigRsp_t *pIomb
  )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest;
  agsaHWEventMode_t     agMode;
  bit32                 status, errorQualifier, tag;
  bit32                 configPage[12];

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3b");

  si_memset(&agMode, 0, sizeof(agsaHWEventMode_t));
  si_memset(configPage, 0, sizeof(configPage));


  SA_DBG2(("mpiGetControllerConfigRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &errorQualifier, pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t, errorQualifier));
  OSSA_READ_LE_32(AGROOT, &configPage[0],  pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t,configPage[0] ));
  OSSA_READ_LE_32(AGROOT, &configPage[1],  pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t,configPage[1] ));
  OSSA_READ_LE_32(AGROOT, &configPage[2],  pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t,configPage[2] ));
  OSSA_READ_LE_32(AGROOT, &configPage[3],  pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t,configPage[3] ));
  OSSA_READ_LE_32(AGROOT, &configPage[4],  pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t,configPage[4] ));
  OSSA_READ_LE_32(AGROOT, &configPage[5],  pIomb, OSSA_OFFSET_OF(agsaGetControllerConfigRsp_t,configPage[5] ));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetControllerConfigRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3b");
    return AGSA_RC_FAILURE;
  }

  si_memset(&agMode, 0, sizeof(agsaHWEventMode_t));
  agMode.modePageOperation = agsaModePageGet;
  agMode.status = status;

  SA_DBG1(("mpiGetControllerConfigRsp: page 0x%x status 0x%x errorQualifier 0x%x \n", (pIomb->configPage[0] & 0xFF),status, errorQualifier));

  switch (pIomb->configPage[0] & 0xFF)
  {
  case AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE:
      agMode.modePageLen = sizeof(agsaSASProtocolTimerConfigurationPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE page len 0x%x \n",agMode.modePageLen));
      break;
  case AGSA_INTERRUPT_CONFIGURATION_PAGE:
      agMode.modePageLen = sizeof(agsaInterruptConfigPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_INTERRUPT_CONFIGURATION_PAGE page len 0x%x \n",agMode.modePageLen));
      break;
  case AGSA_IO_GENERAL_CONFIG_PAGE:
      agMode.modePageLen = sizeof(agsaIoGeneralPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_IO_GENERAL_CONFIG_PAGE page len 0x%x \n",agMode.modePageLen));
      break;
  case AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE:
      agMode.modePageLen = sizeof(agsaEncryptGeneralPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE page len 0x%x \n",agMode.modePageLen));
#ifdef HIALEAH_ENCRYPTION
      saRoot->EncGenPage.numberOfKeksPageCode = configPage[0];
      saRoot->EncGenPage.KeyCardIdKekIndex    = configPage[1];
      saRoot->EncGenPage.KeyCardId3_0         = configPage[2];
      saRoot->EncGenPage.KeyCardId7_4         = configPage[3];
      saRoot->EncGenPage.KeyCardId11_8        = configPage[4];

      SA_DBG1(("mpiGetControllerConfigRsp: numberOfKeksPageCode 0x%x\n",saRoot->EncGenPage.numberOfKeksPageCode));
      SA_DBG1(("mpiGetControllerConfigRsp: KeyCardIdKekIndex    0x%x\n",saRoot->EncGenPage.KeyCardIdKekIndex));
      SA_DBG1(("mpiGetControllerConfigRsp: KeyCardId3_0         0x%x\n",saRoot->EncGenPage.KeyCardId3_0));
      SA_DBG1(("mpiGetControllerConfigRsp: KeyCardId7_4         0x%x\n",saRoot->EncGenPage.KeyCardId7_4));
      SA_DBG1(("mpiGetControllerConfigRsp: KeyCardId11_8        0x%x\n",saRoot->EncGenPage.KeyCardId11_8));
#endif /* HIALEAH_ENCRYPTION */

      break;
  case AGSA_ENCRYPTION_DEK_CONFIG_PAGE:
      agMode.modePageLen = sizeof(agsaEncryptDekConfigPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_ENCRYPTION_DEK_CONFIG_PAGE page len 0x%x \n",agMode.modePageLen));
      break;
  case AGSA_ENCRYPTION_CONTROL_PARM_PAGE:
      agMode.modePageLen = sizeof(agsaEncryptControlParamPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_ENCRYPTION_CONTROL_PARM_PAGE page len 0x%x \n",agMode.modePageLen));
      break;
  case AGSA_ENCRYPTION_HMAC_CONFIG_PAGE:
      agMode.modePageLen = sizeof(agsaEncryptHMACConfigPage_t);
      SA_DBG1(("mpiGetControllerConfigRsp: AGSA_ENCRYPTION_HMAC_CONFIG_PAGE page len 0x%x \n",agMode.modePageLen));
      break;
  default:
      agMode.modePageLen = 0;
      SA_DBG1(("mpiGetControllerConfigRsp: Unknown !!! page len 0x%x \n",agMode.modePageLen));
      break;
  }

  agMode.modePage = (void *) &pIomb->configPage[0];
  agMode.context = saRoot->IOMap[tag].agContext;

  /* Report the event before freeing the IOMB */
  ossaHwCB(agRoot, NULL, OSSA_HW_EVENT_MODE, errorQualifier, (void *) &agMode, 0);

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetControllerRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3b");
  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief KEK Management Response
 *
 *  This routine handles the response of the KEK management message
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiKekManagementRsp(
  agsaRoot_t               *agRoot,
  agsaKekManagementRsp_t *pIomb
  )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest;
  agsaContext_t         *agContext;
  agsaHWEventEncrypt_t  agEvent;
  bit32                 status, errorQualifier, tag, flags;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2A");

  SA_DBG1(("mpiKekManagementRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaKekManagementRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaKekManagementRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &flags, pIomb, OSSA_OFFSET_OF(agsaKekManagementRsp_t, flags));
  OSSA_READ_LE_32(AGROOT, &errorQualifier, pIomb, OSSA_OFFSET_OF(agsaKekManagementRsp_t, errorQualifier));


  SA_DBG1(("mpiKekManagementRsp:status 0x%x flags 0x%x errorQualifier 0x%x\n", status, flags, errorQualifier));

  si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
  if ((flags & 0xFF) == KEK_MGMT_SUBOP_UPDATE)
  {
    SA_DBG1(("mpiKekManagementRsp:KEK_MGMT_SUBOP_UPDATE 0x%x \n", status));
    if (flags & 0xFF00) /* update and store*/
    {
      agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE;
    }
    else /* update */
    {
      agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_UPDATE;
    }
    agEvent.status = status;
    if (status == OSSA_MPI_ENC_ERR_ILLEGAL_KEK_PARAM)
    {
        agEvent.eq = errorQualifier;
    }
    agEvent.info = 0;
    /* Store the new KEK index in agEvent.handle */
    agEvent.handle = (void *) ((bitptr) (flags >> 24));
    /* Store the current KEK index in agEvent.param */
    agEvent.param = (void *) ((bitptr) (flags >> 16) & 0xFF);

  }

  else if ((flags & 0xFF) == KEK_MGMT_SUBOP_INVALIDATE)
  {
      agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_INVALIDTE;
      agEvent.status = status;
      if (status == OSSA_MPI_ENC_ERR_ILLEGAL_KEK_PARAM)
      {
          agEvent.eq = errorQualifier;
      }
      agEvent.info = 0;
      /* Store the new KEK index in agEvent.handle */
      agEvent.handle = (void *) ((bitptr) (flags >> 24));
      /* Store the current KEK index in agEvent.param */
      agEvent.param = (void *) ((bitptr) (flags >> 16) & 0xFF);
  }

  else if ((flags & 0xFF) == KEK_MGMT_SUBOP_KEYCARDINVALIDATE)
  {
     agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE;
      agEvent.status = status;
      if (status == OSSA_MPI_ENC_ERR_ILLEGAL_KEK_PARAM)
      {
          agEvent.eq = errorQualifier;
      }
      agEvent.info = 0;
      /* Store the new KEK index in agEvent.handle */
      agEvent.handle = (void *) ((bitptr) (flags >> 24));
      /* Store the current KEK index in agEvent.param */
      agEvent.param = (void *) ((bitptr) (flags >> 16) & 0xFF);

  }

  else if ((flags & 0xFF) == KEK_MGMT_SUBOP_KEYCARDUPDATE)
  {
     agEvent.encryptOperation = OSSA_HW_ENCRYPT_KEK_UPDATE;
      agEvent.status = status;
      if (status == OSSA_MPI_ENC_ERR_ILLEGAL_KEK_PARAM)
      {
          agEvent.eq = errorQualifier;
      }
      agEvent.info = 0;
      /* Store the new KEK index in agEvent.handle */
      agEvent.handle = (void *) ((bitptr) (flags >> 24));
      /* Store the current KEK index in agEvent.param */
      agEvent.param = (void *) ((bitptr) (flags >> 16) & 0xFF);

  }
  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiKekManagementRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2A");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaHwCB(agRoot, NULL, OSSA_HW_EVENT_ENCRYPTION, 0, (void *) &agEvent, agContext);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiKekManagementRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2A");

  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief DEK Management Response
 *
 *  This routine handles the response of the DEK management message
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDekManagementRsp(
  agsaRoot_t               *agRoot,
  agsaDekManagementRsp_t   *pIomb
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  agsaHWEventEncrypt_t agEvent;
  bit32               flags, status, errorQualifier, tag, dekIndex;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2B");

  SA_DBG1(("mpiDekManagementRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaDekManagementRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaDekManagementRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &flags, pIomb, OSSA_OFFSET_OF(agsaDekManagementRsp_t, flags));
  OSSA_READ_LE_32(AGROOT, &errorQualifier, pIomb, OSSA_OFFSET_OF(agsaDekManagementRsp_t, errorQualifier));
  OSSA_READ_LE_32(AGROOT, &dekIndex, pIomb, OSSA_OFFSET_OF(agsaDekManagementRsp_t, dekIndex));

  SA_DBG2(("mpiDekManagementRsp:tag =0x%x\n",tag ));
  SA_DBG2(("mpiDekManagementRsp:status =0x%x\n", status));
  SA_DBG2(("mpiDekManagementRsp:flags =0x%x\n",flags ));
  SA_DBG2(("mpiDekManagementRsp:errorQualifier =0x%x\n", errorQualifier));
  SA_DBG2(("mpiDekManagementRsp:dekIndex =0x%x\n",dekIndex ));

  si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
  if ((flags & 0xFF) == DEK_MGMT_SUBOP_UPDATE)
  {
     agEvent.encryptOperation = OSSA_HW_ENCRYPT_DEK_UPDATE;
  }
  else
  {
     agEvent.encryptOperation = OSSA_HW_ENCRYPT_DEK_INVALIDTE;
  }
  agEvent.status = status;
  if (status == OSSA_MPI_ENC_ERR_ILLEGAL_DEK_PARAM || OSSA_MPI_ERR_DEK_MANAGEMENT_DEK_UNWRAP_FAIL)
  {
    agEvent.eq = errorQualifier;
  }
  /* Store the DEK in agEvent.info */
  agEvent.info = (flags >> 8) & 0xF;
  /* Store the KEK index in agEvent.handle */
  agEvent.handle = (void *) ((bitptr) (flags >> 24));
  /* Store the DEK index in agEvent.param */
  agEvent.param = (void *) (bitptr) dekIndex;

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiDekManagementRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2B");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaHwCB(agRoot, NULL, OSSA_HW_EVENT_ENCRYPTION, 0, (void *) &agEvent,agContext );

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiDekManagementRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2B");

  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief Operator Management Response
 *
 *  This routine handles the response of the Operator management message
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiOperatorManagementRsp(
  agsaRoot_t                *agRoot,
  agsaOperatorMangmenRsp_t  *pIomb
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  agsaHWEventEncrypt_t agEvent;
  bit32               OPRIDX_AUTIDX_R_OMO,status, errorQualifier, tag;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"36");

  SA_DBG1(("mpiOperatorManagementRsp: HTag=0x%x\n", pIomb->tag));

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaOperatorMangmenRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaOperatorMangmenRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &OPRIDX_AUTIDX_R_OMO, pIomb, OSSA_OFFSET_OF(agsaOperatorMangmenRsp_t, OPRIDX_AUTIDX_R_OMO));
  OSSA_READ_LE_32(AGROOT, &errorQualifier, pIomb, OSSA_OFFSET_OF(agsaOperatorMangmenRsp_t, errorQualifier));

  SA_DBG2(("mpiOperatorManagementRsp:tag =0x%x\n",tag ));
  SA_DBG2(("mpiOperatorManagementRsp:status =0x%x\n", status));
  SA_DBG2(("mpiOperatorManagementRsp:OPRIDX_AUTIDX_R_OMO =0x%x\n",OPRIDX_AUTIDX_R_OMO ));
  SA_DBG2(("mpiOperatorManagementRsp:errorQualifier =0x%x\n", errorQualifier));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiOperatorManagementRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "36");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
  agEvent.status = status;
  agEvent.info = OPRIDX_AUTIDX_R_OMO;
  agEvent.encryptOperation = OSSA_HW_ENCRYPT_OPERATOR_MANAGEMENT;
  if (status == OPR_MGMT_MPI_ENC_ERR_OPR_PARAM_ILLEGAL)
  {
    agEvent.eq = errorQualifier;
  }

  ossaOperatorManagementCB(agRoot, agContext, status, errorQualifier);

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiOperatorManagementRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "36");

  return AGSA_RC_SUCCESS;
}

GLOBAL bit32 mpiBistRsp(
  agsaRoot_t               *agRoot,
  agsaEncryptBistRsp_t     *pIomb
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  agsaHWEventEncrypt_t agEvent;
  bit32               status;
  bit32               results[11];
  bit32               length;
  bit32               subop;
  bit32               tag;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"37");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &subop, pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, subop));
  OSSA_READ_LE_32(AGROOT, &results[0], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[0]));
  OSSA_READ_LE_32(AGROOT, &results[1], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[1]));
  OSSA_READ_LE_32(AGROOT, &results[2], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[2]));
  OSSA_READ_LE_32(AGROOT, &results[3], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[3]));
  OSSA_READ_LE_32(AGROOT, &results[4], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[4]));
  OSSA_READ_LE_32(AGROOT, &results[5], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[5]));
  OSSA_READ_LE_32(AGROOT, &results[6], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[6]));
  OSSA_READ_LE_32(AGROOT, &results[7], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[7]));
  OSSA_READ_LE_32(AGROOT, &results[8], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[8]));
  OSSA_READ_LE_32(AGROOT, &results[9], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[9]));
  OSSA_READ_LE_32(AGROOT, &results[10], pIomb, OSSA_OFFSET_OF(agsaEncryptBistRsp_t, testResults[10]));

  subop &= 0xFF;
  SA_DBG1(("mpiBistRsp: HTag=0x%x subops =0x%x status =0x%x\n",pIomb->tag, subop, status));

  switch(subop)
  {
    case AGSA_BIST_TEST:
      length =  sizeof(agsaEncryptSelfTestStatusBitMap_t);
      break;
    case AGSA_SHA_TEST:
      length = sizeof(agsaEncryptSHATestResult_t);
      break;
    case AGSA_HMAC_TEST:
      length = sizeof(agsaEncryptHMACTestResult_t);
      break;
    default:
      length = 0;
      break;
  }

  si_memset(&agEvent, 0, sizeof(agsaHWEventEncrypt_t));
  agEvent.status = status;
  agEvent.encryptOperation = OSSA_HW_ENCRYPT_TEST_EXECUTE;
  agEvent.info = length;
  agEvent.eq   = subop;
  agEvent.handle = agNULL;
  agEvent.param = &results;

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiBistRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "37");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  ossaHwCB(agRoot, NULL, OSSA_HW_EVENT_ENCRYPTION, 0, (void*)&agEvent, agContext);

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiBistRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "37");

  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief Set Operator Response
 *
 *  This routine handles the response of the Operator management message
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetOperatorRsp(
  agsaRoot_t               *agRoot,
  agsaSetOperatorRsp_t     *pIomb
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest = agNULL;
  agsaContext_t       *agContext = agNULL;
  bit32               ERR_QLFR_OPRIDX_PIN_ACS, OPRIDX_PIN_ACS, status, errorQualifier, tag = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"38");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaSetOperatorRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaSetOperatorRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &ERR_QLFR_OPRIDX_PIN_ACS, pIomb, OSSA_OFFSET_OF(agsaSetOperatorRsp_t, ERR_QLFR_OPRIDX_PIN_ACS));

  errorQualifier = ERR_QLFR_OPRIDX_PIN_ACS >> 16;
  OPRIDX_PIN_ACS = ERR_QLFR_OPRIDX_PIN_ACS & 0xFFFF;

  SA_DBG1(("mpiSetOperatorRsp: HTag=0x%x ERR_QLFR=0x%x OPRIDX_PIN_ACS=0x%x \n",tag, errorQualifier, OPRIDX_PIN_ACS));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiSetOperatorRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "38");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;


  ossaSetOperatorCB(agRoot,agContext,status,errorQualifier );

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiSetOperatorRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "38");

  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief Get Operator Response
 *
 *  This routine handles the response of the Operator management message
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetOperatorRsp(
  agsaRoot_t               *agRoot,
  agsaGetOperatorRsp_t     *pIomb
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32                Num_Option, NumOperators ,status, tag;
  bit8                 option, Role = 0;
  bit32                IDstr[8];
  bit8                *tmpIDstr = agNULL;
  agsaID_t            *IDString = agNULL;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3f");

  si_memset(&IDstr, 0, sizeof(IDstr));
  OSSA_READ_LE_32(AGROOT, &tag,         pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status,      pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &Num_Option,  pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, Num_Option));
  OSSA_READ_LE_32(AGROOT, &IDstr[0],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[0]));
  OSSA_READ_LE_32(AGROOT, &IDstr[1],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[1]));
  OSSA_READ_LE_32(AGROOT, &IDstr[2],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[2]));
  OSSA_READ_LE_32(AGROOT, &IDstr[3],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[3]));
  OSSA_READ_LE_32(AGROOT, &IDstr[4],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[4]));
  OSSA_READ_LE_32(AGROOT, &IDstr[5],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[5]));
  OSSA_READ_LE_32(AGROOT, &IDstr[6],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[6]));
  OSSA_READ_LE_32(AGROOT, &IDstr[7],    pIomb, OSSA_OFFSET_OF(agsaGetOperatorRsp_t, IDString[7]));

  SA_DBG1(("mpiGetOperatorRsp:tag=0x%x status=0x%x Num_Option=0x%x IDString_Role=0x%x\n",
           tag, status, Num_Option, IDstr[0]));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetOperatorRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3f");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  option = Num_Option & 0xFF;
  NumOperators = (Num_Option >> SHIFT8) & 0xFF;
  /* current operator's Role/ID, valid only if option == 1 */
  if ( option == 1)
  {
    /* extra the role value as parameter */
    Role = IDstr[0] & 0xFF;
    tmpIDstr = (bit8*)&IDstr[0];
    tmpIDstr++; /* skip role byte */
    IDString = (agsaID_t *)tmpIDstr;
    SA_DBG1(("mpiGetOperatorRsp: OSSA_IO_SUCCESS\n"));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[0], IDString->ID[1], IDString->ID[2], IDString->ID[3]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[4], IDString->ID[5], IDString->ID[6], IDString->ID[7]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[8], IDString->ID[9], IDString->ID[10],IDString->ID[11]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[12],IDString->ID[13],IDString->ID[14],IDString->ID[15]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[16],IDString->ID[17],IDString->ID[18],IDString->ID[19]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[20],IDString->ID[21],IDString->ID[22],IDString->ID[23]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x 0x%02x\n",IDString->ID[24],IDString->ID[25],IDString->ID[26],IDString->ID[27]));
    SA_DBG2(("mpiGetOperatorRsp: 0x%02x 0x%02x 0x%02x\n",       IDString->ID[28],IDString->ID[29],IDString->ID[30]));
  }

  SA_DBG1(("mpiGetOperatorRsp:status 0x%x option 0x%x Role 0x%x\n",status,option,Role ));

  ossaGetOperatorCB(agRoot,agContext,status,option,NumOperators ,Role,IDString );

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiGetOperatorRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3f");

  return AGSA_RC_SUCCESS;
}


GLOBAL bit32 mpiGetVHistRsp(
   agsaRoot_t         *agRoot,
   agsaGetVHistCapRsp_t *pIomb
  )
{

  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = agNULL;
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;

  bit32    tag = 0;           /* 1 */
  bit32    status = 0;        /* 2 */
  bit32    channel;          /* 3 */
  bit32    BistLo;           /* 4 */
  bit32    BistHi;           /* 5 */
  bit32    BytesXfered = 0;  /* 6 */
  bit32    PciLo;            /* 7 */
  bit32    PciHi;            /* 8 */
  bit32    PciBytecount = 0;  /* 9 */

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3K");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  if(smIS_SPC12V(agRoot))
  {
    OSSA_READ_LE_32(AGROOT, &tag,          pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,tag));
    OSSA_READ_LE_32(AGROOT, &status,       pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,status));
    OSSA_READ_LE_32(AGROOT, &channel,      pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,channel));
    OSSA_READ_LE_32(AGROOT, &BistLo,       pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,BistLo));
    OSSA_READ_LE_32(AGROOT, &BistHi,       pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,BistHi));
    OSSA_READ_LE_32(AGROOT, &BytesXfered,  pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,BytesXfered));
    OSSA_READ_LE_32(AGROOT, &PciLo,        pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,PciLo));
    OSSA_READ_LE_32(AGROOT, &PciHi,        pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,PciHi));
    OSSA_READ_LE_32(AGROOT, &PciBytecount, pIomb, OSSA_OFFSET_OF(agsaGetVHistCapRsp_t,PciBytecount));
  }
  else
  {
    /* SPC does not support this command */
    SA_DBG1(("mpiGetVHistRsp: smIS_SPC12V only\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3K");
    return AGSA_RC_FAILURE;
  }

  SA_DBG3(("mpiGetVHistRsp: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiGetVHistRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3K");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* check status success or failure */
  if (status)
  {
    SA_DBG1(("mpiGetVHistRsp: status is FAILED, status = %x\n", status ));

    if (pRequest->completionCB == agNULL)
    {
      ossaVhistCaptureCB(agRoot, agContext, status, BytesXfered);
    }
    else
    {
      (*(ossaVhistCaptureCB_t)(pRequest->completionCB))(agRoot, agContext, status, BytesXfered);
    }

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "3K");
    return AGSA_RC_FAILURE;
  }

  /* status is SUCCESS */
  SA_DBG1(("mpiGetVHistRsp: status is SUCCESS\n" ));

  if (pRequest->completionCB == agNULL)
  {
    ossaVhistCaptureCB(agRoot, agContext, status, BytesXfered);
  }
  else
  {
    (*(ossaVhistCaptureCB_t)(pRequest->completionCB))(agRoot, agContext, status, BytesXfered);
  }
  
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "3K");

  return ret;
}



/******************************************************************************/
/*! \brief DifEncOffload Response
 *
 *  This routine handles the response of the DifEncOffload Response
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        Pointer of IOMB Mesage
 *
 *  \return sucess or fail
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDifEncOffloadRsp(
  agsaRoot_t               *agRoot,
  agsaDifEncOffloadRspV_t  *pIomb
  )
{

  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;
  bit32               tag, status;
  agsaOffloadDifDetails_t details;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3F");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, status));
  /* get TAG */
  SA_DBG3(("mpiDifEncOffloadRsp: HTag=0x%x\n", tag));

  /* get request from IOMap */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    SA_DBG1(("mpiDifEncOffloadRsp: Bad Response IOMB!!! pRequest is NULL. TAG=0x%x STATUS=0x%x\n", tag, status));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3F");
    return AGSA_RC_FAILURE;
  }

  agContext = saRoot->IOMap[tag].agContext;

  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* check status success or failure */
  if (status)
  {
    SA_DBG1(("mpiDifEncOffloadRsp: status is FAILED, status = %x\n", status ));

    if (status == OSSA_IO_XFR_ERROR_DIF_MISMATCH || status == OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH ||
        status == OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH || status == OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH)
    {
      si_memset(&details, 0, sizeof(agsaOffloadDifDetails_t));
      OSSA_READ_LE_32(AGROOT, &details.ExpectedCRCUDT01, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, ExpectedCRCUDT01));
      OSSA_READ_LE_32(AGROOT, &details.ExpectedUDT2345, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, ExpectedUDT2345));
      OSSA_READ_LE_32(AGROOT, &details.ActualCRCUDT01, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, ActualCRCUDT01));
      OSSA_READ_LE_32(AGROOT, &details.ActualUDT2345, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, ActualUDT2345));
      OSSA_READ_LE_32(AGROOT, &details.DIFErr, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, DIFErr));
      OSSA_READ_LE_32(AGROOT, &details.ErrBoffset, pIomb, OSSA_OFFSET_OF(agsaDifEncOffloadRspV_t, ErrBoffset));

      if (pRequest->completionCB == agNULL)
      {
        ossaDIFEncryptionOffloadStartCB(agRoot, agContext, status, &details);
      }
      else
      {
        (*(ossaDIFEncryptionOffloadStartCB_t)(pRequest->completionCB))(agRoot, agContext, status, &details);
      }
    }
    else
    {
      if (pRequest->completionCB == agNULL)
      {
        ossaDIFEncryptionOffloadStartCB(agRoot, agContext, status, agNULL);
      }
      else
      {
        (*(ossaDIFEncryptionOffloadStartCB_t)(pRequest->completionCB))(agRoot, agContext, status, agNULL);
      }
    }

    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3F");

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    return AGSA_RC_FAILURE;
  }

  /* status is SUCCESS */
  SA_DBG1(("mpiDifEncOffloadRsp: status is SUCCESS\n" ));

  if (pRequest->completionCB == agNULL)
  {
    ossaDIFEncryptionOffloadStartCB(agRoot, agContext, status, agNULL);
  }
  else
  {
    (*(ossaDIFEncryptionOffloadStartCB_t)(pRequest->completionCB))(agRoot, agContext, status, agNULL);
  }
  
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "3F");

  return ret;
}

