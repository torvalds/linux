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
/*! \file sahw.c
 *  \brief The file implements the functions for reset and shutdown
 */
/******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_ENABLE_HDA_FUNCTIONS
#ifndef SA_EXCLUDE_FW_IMG
/*
#include "istrimg.h"
#include "ilaimg.h"
#include "aap1img.h"
#include "iopimg.h"
*/
#endif
#endif
#if defined(SALLSDK_DEBUG)
extern bit32 gLLSoftResetCounter;
#endif

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'E'
#endif


bit32 gWait_3 = 3;
bit32 gWait_2 = 2;

bit32 gWaitmSec = 0;



LOCAL bit32 si_V_SoftReset(agsaRoot_t  *agRoot, bit32       signature);


LOCAL bit32 siSpcSoftResetRDYChk(agsaRoot_t *agRoot);

#ifdef SA_ENABLE_HDA_FUNCTIONS
LOCAL void siPciMemCpy(agsaRoot_t *agRoot, bit32 dstoffset, void *src,
                       bit32 DWcount, bit32 busBaseNumber);

LOCAL bit32 siBar4Cpy(agsaRoot_t  *agRoot, bit32 offset, bit8 *parray, bit32 array_size);
#endif

/******************************************************************************/
/*! \brief Function to reset the Hardware
 *
 *  The saHwReset() function is called to reset the SAS/SATA HW controller
 *  All outstanding I/Os are explicitly aborted.
 *  This API need to access before saInitialize() so checking saRoot is needed
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param resetType    The reset type
 *  \param resetParm    The paramter passed for reset operation
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void saHwReset(
                     agsaRoot_t  *agRoot,
                     bit32       resetType,
                     bit32       resetParm
                     )
{
  agsaLLRoot_t *saRoot = agNULL;
  bit32        ret = AGSA_RC_SUCCESS;
  bit32        value;
  bit32        sysIntsActive = agFALSE;
#if defined(SALLSDK_DEBUG)
  bit32        value1;
  agsaControllerStatus_t controllerStatus;
  agsaFatalErrorInfo_t fatal_error;
#endif

#ifdef SOFT_RESET_TEST
  DbgPrint("Reset Start\n");
#endif

  smTraceFuncEnter(hpDBG_VERY_LOUD, "5a");

  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "");
  if (agNULL != agRoot)
  {
    if (agNULL != agRoot->sdkData)
    {
      saRoot = (agsaLLRoot_t*) agRoot->sdkData;
      sysIntsActive =  saRoot->sysIntsActive;
      if(sysIntsActive)
      {
        saSystemInterruptsActive(agRoot,agFALSE);
      }
    }
  }
  else
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5a");
    return;
  }


#if defined(SALLSDK_DEBUG)
  {
    if (agNULL != agRoot->sdkData)
    {
      /* check fatal errors */
      value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
      value1 = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2);
      /* check AAP error */
      if( smIS_SPC(agRoot) )
      {
        value &= SCRATCH_PAD_STATE_MASK;
        value1 &= SCRATCH_PAD_STATE_MASK;

        if ((SCRATCH_PAD1_ERR == value) || (SCRATCH_PAD2_ERR == value1))
        {

          si_memset(&fatal_error, 0, sizeof(agsaFatalErrorInfo_t));
          /* read detail fatal errors */
          value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0, MSGU_SCRATCH_PAD_0);
          fatal_error.errorInfo0 = value;
          SA_DBG1(("saHwReset: ScratchPad0 AAP error code 0x%x\n", value));
          value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
          fatal_error.errorInfo1 = value;
          /* AAP error state */
          SA_DBG1(("saHwReset: AAP error state and error code 0x%x\n", value));
          value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2);
          fatal_error.errorInfo2 = value;
          /* IOP error state */
          SA_DBG1(("saHwReset: IOP error state and error code 0x%x\n", value));
          value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3, MSGU_SCRATCH_PAD_3);
          SA_DBG1(("saHwReset: ScratchPad3 IOP error code 0x%x\n", value));
          fatal_error.errorInfo3 = value;
          if (agNULL != saRoot)
          {
            fatal_error.regDumpBusBaseNum0 = saRoot->mainConfigTable.regDumpPCIBAR;
            fatal_error.regDumpBusBaseNum1 = saRoot->mainConfigTable.regDumpPCIBAR;
            fatal_error.regDumpLen0 = saRoot->mainConfigTable.FatalErrorDumpLength0;
            fatal_error.regDumpLen1 = saRoot->mainConfigTable.FatalErrorDumpLength1;
            fatal_error.regDumpOffset0 = saRoot->mainConfigTable.FatalErrorDumpOffset0;
            fatal_error.regDumpOffset1 = saRoot->mainConfigTable.FatalErrorDumpOffset1;
          }

          /* Call Back with error */
          SA_DBG1(("saHwReset: OSSA_HW_EVENT_MALFUNCTION SPC SP1 0x%x\n", value1));
          ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_MALFUNCTION, 0, (void *)&fatal_error, agNULL);
        }
      }
      else
      {
        if( ( (value & SCRATCH_PAD1_V_BOOTLDR_ERROR) == SCRATCH_PAD1_V_BOOTLDR_ERROR))
        {
          SA_DBG1(("saHwReset: ScratchPad1 SCRATCH_PAD1_V_BOOTLDR_ERROR 0x%x\n", value));
        }
        if(SCRATCH_PAD1_V_ERROR_STATE(value))
        {
          SA_DBG1(("saHwReset: ScratchPad1 SCRATCH_PAD1_V_ERROR_STATE  0x%x\n",SCRATCH_PAD1_V_ERROR_STATE(value) ));
        }
        if( (value & SCRATCH_PAD1_V_READY) == SCRATCH_PAD1_V_READY )
        {
          SA_DBG1(("saHwReset: ScratchPad1 SCRATCH_PAD1_V_READY  0x%x\n", value));
        }
      }
      saGetControllerStatus(agRoot, &controllerStatus);
      if (agNULL != saRoot)
      {
        /* display all pending Ios */
        siDumpActiveIORequests(agRoot, saRoot->swConfig.maxActiveIOs);
      }
    }
  }
#endif /* SALLSDK_DEBUG */

  /* Check the resetType */
  switch (resetType)
  {
    /* Reset the whole chip */
    case AGSA_CHIP_RESET:
    {
      /* callback with RESET_START */
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_START, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);

      if (agNULL != agRoot->sdkData && agNULL != saRoot)
      {
        /* Set chip status */
        saRoot->chipStatus |= CHIP_RESETTING;

        /* Disable all interrupt */
        saSystemInterruptsActive(agRoot,agFALSE);
      }

      /* do chip reset */
      siChipReset(agRoot);

      if (agNULL != saRoot)
      {
        /* clear up the internal resource */
        siInitResources(agRoot,
                        &saRoot->memoryAllocated,
                        &saRoot->hwConfig,
                        &saRoot->swConfig,
                        saRoot->usecsPerTick);
      }

      /* callback with CHIP_RESET_COMPLETE with OSSA_SUCCESS */
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_COMPLETE, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);

      if (agNULL != saRoot)
      {
          /* mask off reset FW status */
          saRoot->chipStatus &= ~CHIP_RESETTING;
      }
      break;
    }
    case AGSA_SOFT_RESET:
    {

      if( smIS_SPCV(agRoot) )
      {
        SA_DBG1(("saHwReset: AGSA_SOFT_RESET chip type V %d\n",smIS_SPCV(agRoot) ));
        ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_START, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);
        if (agNULL != saRoot)
        {
          saRoot->ResetStartTick = saRoot->timeTick;
          saCountActiveIORequests( agRoot);
	} //delray end

        ret = siChipResetV( agRoot, SPC_SOFT_RESET_SIGNATURE );
	
	if(agNULL !=saRoot)
	{
           /* clear up the internal resource */
          siInitResources(agRoot,
                          &saRoot->memoryAllocated,
                          &saRoot->hwConfig,
                          &saRoot->swConfig,
                          saRoot->usecsPerTick);
        }

        if (AGSA_RC_SUCCESS == ret)
        {
           /* callback with CHIP_RESET_COMPLETE with OSSA_SUCCESS */
          SA_DBG1(("saHwReset: siChipResetV AGSA_RC_SUCCESS\n" ));
          ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_COMPLETE, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);
        }
        else
        {
          /* callback with CHIP_RESET_COMPLETE with OSSA_FAILURE */
          SA_DBG1(("saHwReset: siChipResetV not AGSA_RC_SUCCESS (0x%x)\n" ,ret));
          ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_COMPLETE, OSSA_FAILURE << SHIFT8, agNULL, agNULL);
          if (agNULL != saRoot)
          {
            saRoot->ResetFailed = agTRUE;
            SA_DBG1(("saHwReset: siChipResetV saRoot->ResetFailed  ret (0x%x)\n" ,ret));
          }

        }
        break;
      }
      else
      {
        if (agNULL != saRoot)
        {
          /* get register dump from GSM and save it to LL local memory */
          siGetRegisterDumpGSM(agRoot, (void *)&saRoot->registerDump0[0],
               REG_DUMP_NUM0, 0, saRoot->mainConfigTable.FatalErrorDumpLength0);
          siGetRegisterDumpGSM(agRoot, (void *)&saRoot->registerDump1[0],
               REG_DUMP_NUM1, 0, saRoot->mainConfigTable.FatalErrorDumpLength1);
        }

        /* callback with RESET_START */
        ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_START, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);

        if (agNULL != agRoot->sdkData && agNULL != saRoot)
        {
          /* Set chip status */
          saRoot->chipStatus |= CHIP_RESET_FW;

          /* Disable all interrupt */
          saSystemInterruptsActive(agRoot,agFALSE);
          saCountActiveIORequests( agRoot); //delray start

        }

        /* check HDA mode */
        value = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;

        if (value == BOOTTLOADERHDA_IDLE)
        {
          /* HDA mode */
          SA_DBG1(("saHwReset: HDA mode, value = 0x%x\n", value));
          ret = AGSA_RC_HDA_NO_FW_RUNNING;
        }
        else
        {
          /* do Soft Reset */
          ret = siSpcSoftReset(agRoot, SPC_SOFT_RESET_SIGNATURE);
        }
	if(agNULL !=saRoot)
	{
          /* clear up the internal resource */
          siInitResources(agRoot,
                          &saRoot->memoryAllocated,
                          &saRoot->hwConfig,
                          &saRoot->swConfig,
                          saRoot->usecsPerTick);
	}
        if (AGSA_RC_SUCCESS == ret)
        {
          /* callback with CHIP_RESET_COMPLETE with OSSA_SUCCESS */
          ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_COMPLETE, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);
        }
        else if (AGSA_RC_HDA_NO_FW_RUNNING == ret)
        {
          /* callback with CHIP_RESET_COMPLETE with OSSA_CHIP_FAILED */
          ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_COMPLETE, OSSA_SUCCESS << SHIFT8, agNULL, agNULL);
        }
        else
        {
          /* callback with CHIP_RESET_COMPLETE with OSSA_FAILURE */
          ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_RESET_COMPLETE, (OSSA_FAILURE << SHIFT8), agNULL, agNULL);
        }

        if (agNULL != saRoot)
        {
          /* mask off reset FW status */
          saRoot->chipStatus &= ~CHIP_RESET_FW;
        }
        break;
      }
    }
    /* Unsupported type */
    default:
    {
      SA_DBG1(("saHwReset: Unsupported reset type %X\n",resetType));
      break;
    }
  }

  if (agNULL != saRoot)
  {
    if(sysIntsActive &&  ret == AGSA_RC_SUCCESS)
    {
      saSystemInterruptsActive(agRoot,agTRUE);
    }

    saCountActiveIORequests( agRoot);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5a");
  }

  return;
}

/******************************************************************************/
/*! \brief Function to shutdown the Hardware
 *
 *  The saHwShutdown() function is called to discontinue the use of the SAS/SATA
 *  hardware. Upon return, the SASA/SAT hardware instance does not generate any
 *  interrupts or any other bus accesses. All LL Layer hardware host resources
 * (i.e. both cached and noncached memory) are no longer owned by the LL Layer.
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void saHwShutdown(
                        agsaRoot_t  *agRoot
                        )
{
  agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32 spad0 = 0;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"5b");

  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "");
  SA_DBG1(("saHwShutdown: Shutting down .....\n"));

  if (agRoot->sdkData)
  {

    spad0 = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

    if(0xFFFFFFFF ==  spad0)
    {
      SA_ASSERT(0xFFFFFFFF ==  spad0, "saHwShutdown Chip PCI dead");

      SA_DBG1(("saHwShutdown: Chip PCI dead  SCRATCH_PAD0 0x%x\n", spad0));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5b");
      return;
    }


#if defined(SALLSDK_DEBUG)
    SA_DBG1(("saHwShutdown: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0, MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("saHwShutdown: SCRATCH_PAD1 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1)));
    SA_DBG1(("saHwShutdown: SCRATCH_PAD2 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2)));
    SA_DBG1(("saHwShutdown: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3, MSGU_SCRATCH_PAD_3)));

    if(1)
    {
      mpiOCQueue_t         *circularQ;
      int i;
      SA_DBG4(("saHwShutdown:\n"));
      for ( i = 0; i < saRoot->QueueConfig.numOutboundQueues; i++ )
      {
        circularQ = &saRoot->outboundQueue[i];
        OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
        if(circularQ->producerIdx != circularQ->consumerIdx)
        {
          SA_DBG1(("saHwShutdown: PI 0x%03x CI 0x%03x\n",circularQ->producerIdx, circularQ->consumerIdx ));
        }
      }
    }
#endif /* SALLSDK_DBG */

    if(smIS_SPCV(agRoot))
    {

      siScratchDump(agRoot);

      SA_DBG1(("saHwShutdown: SPC_V\n" ));
    }
    /* Set chip status */
    saRoot->chipStatus |= CHIP_SHUTDOWN;

    /* Un-Initialization Configuration Table */
    mpiUnInitConfigTable(agRoot);
    if (saRoot->swConfig.hostDirectAccessSupport && !saRoot->swConfig.hostDirectAccessMode)
    {
      /* HDA mode -  do HDAsoftReset */
      if(smIS_SPC(agRoot))
      {
        /* HDA soft reset */
        siSpcSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
      }
      if(smIS_SPCV(agRoot))
      {
        siChipResetV(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
        SA_DBG1(("saHwShutdown: HDA saRoot->ChipId == VEN_DEV_SPCV\n"));
      }
    }
    else
    {
      /*  do Normal softReset */
      if(smIS_SPC(agRoot))
      {
        /* Soft Reset the SPC */
        siSpcSoftReset(agRoot, SPC_SOFT_RESET_SIGNATURE);
      }
      if(smIS_SPCV(agRoot))
      {
        SA_DBG1(("saHwShutdown: saRoot->ChipId == VEN_DEV_SPCV\n"));
        siChipResetV(agRoot, SPC_SOFT_RESET_SIGNATURE);
      }

    }

    /* clean the LL resources */
    siInitResources(agRoot,
                    &saRoot->memoryAllocated,
                    &saRoot->hwConfig,
                    &saRoot->swConfig,
                    saRoot->usecsPerTick);
    SA_DBG1(("saHwShutdown: Shutting down Complete\n"));
  }
  else
  {
    SA_DBG1(("saHwShutdown: No saRoot\n"));
    if( smIS_SPCV(agRoot) )
    {
      siChipResetV(agRoot, SPC_SOFT_RESET_SIGNATURE);
    }
    else
    {
       siSpcSoftReset(agRoot, SPC_SOFT_RESET_SIGNATURE);
    }
  }
  /* agroot/saroot null do not access -trace OK */

  SA_ASSERT( (agNULL != agRoot), "10");
  /* return */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5b");
  return;
}


/******************************************************************************/
/*! \brief Generic Reset
 *
 *  The siChipReset() function is called to reset the SPC chip. Upon return,
 *  the SPC chip got reset. The PCIe bus got reset.
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *
 *  \return -void-
 */
/*******************************************************************************/

GLOBAL void siChipReset(
                      agsaRoot_t  *agRoot
                )
{
  agsaLLRoot_t      *saRoot;

  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "");

  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  if(agNULL != saRoot)
  {
    smTraceFuncEnter(hpDBG_VERY_LOUD,"2C");

    SA_DBG1(("siChipReset: saRoot->ChipId == VEN_DEV_SPCV\n"));
    if(smIS_SPC(agRoot) )
    {
      /* Soft Reset the SPC */
      siChipResetSpc(   agRoot);
    }else /* saRoot->ChipId == VEN_DEV_SPCV */
    {
      siChipResetV( agRoot, SPC_SOFT_RESET_SIGNATURE);
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2C");
  }

}


/******************************************************************************/
/*! \brief Function to Reset the SPC V Hardware
 *
 *  The siChipResetV() function is called to reset the SPC chip. Upon return,
 *  the SPC chip got reset. The PCIe bus got reset.
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *
 *  \return -void-
 */
/*******************************************************************************/

GLOBAL bit32 siChipResetV(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       )
{
  bit32 regVal;
  bit32 returnVal = AGSA_RC_SUCCESS;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3A");
  smTrace(hpDBG_LOUD,"Lr",ossaTimeStamp64(agRoot));
  regVal = ossaHwRegReadExt(agRoot,PCIBAR0 ,V_SoftResetRegister );

  SA_DBG1(("siChipResetV: signature %X V_SoftResetRegister %X\n",signature,regVal));

  if (signature == SPC_SOFT_RESET_SIGNATURE)
  {
    SA_DBG1(("siChipResetV: SPC_SOFT_RESET_SIGNATURE 0x%X\n",regVal));
    regVal = SPCv_Reset_Write_NormalReset;
  }
  else if (signature == SPC_HDASOFT_RESET_SIGNATURE)
  {
    SA_DBG1(("siChipResetV: SPCv load HDA 0x%X\n",regVal));
    regVal = SPCv_Reset_Write_SoftResetHDA;
  }
  else
  {
    SA_DBG1(("siChipResetV: Invalid SIGNATURE 0x%X  regVal 0x%X  a\n",signature ,regVal));
    regVal = 1;
  }

  smTrace(hpDBG_LOUD,"Ls",ossaTimeStamp64(agRoot));
  ossaHwRegWriteExt(agRoot, PCIBAR0, V_SoftResetRegister, regVal); /* siChipResetV */
  smTrace(hpDBG_LOUD,"Lt",ossaTimeStamp64(agRoot));
  ossaStallThread(agRoot, (500 * 1000)); /* wait 500 milliseconds or PCIe will hang */
  /* Soft reset sequence (Normal mode) */
  smTrace(hpDBG_LOUD,"Lv",ossaTimeStamp64(agRoot));

  if (signature == SPC_HDASOFT_RESET_SIGNATURE)
  {
    bit32 hda_status;

    hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28));

    SA_DBG1(("siChipResetV: hda_status 0x%x\n",hda_status));

    if((hda_status  & SPC_V_HDAR_RSPCODE_MASK)  != SPC_V_HDAR_IDLE)
    {
      SA_DBG1(("siChipResetV:SPC_HDASOFT_RESET_SIGNATURE SCRATCH_PAD1 = 0x%x \n",ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)));
    }

    SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE %X\n",regVal));

    regVal =   ossaHwRegReadExt(agRoot, PCIBAR0, V_SoftResetRegister ); /* siChipResetV */
    SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE  %X\n",regVal));

    if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_NoReset)
    {
      SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE AGSA_RC_FAILURE %X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_NormalResetOccurred  )
    {
      SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE AGSA_RC_FAILURE %X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_SoftResetHDAOccurred)
    {
      SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE AGSA_RC_SUCCESS %X\n",regVal));
      returnVal = AGSA_RC_SUCCESS;
    }
    if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_ChipResetOccurred)
    {
      SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE AGSA_RC_FAILURE %X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    if(regVal  == 0xFFFFFFFF)
    {
      SA_DBG1(("siChipResetV: SPC_HDASOFT_RESET_SIGNATURE AGSA_RC_FAILURE %X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }

    SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x a\n",ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)));

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3A");
    return returnVal;
  }
  else if (signature == SPC_SOFT_RESET_SIGNATURE)
  {
    bit32 SCRATCH_PAD1;
    bit32 max_wait_time;
    bit32 max_wait_count;
    smTrace(hpDBG_LOUD,"Lw",ossaTimeStamp64(agRoot));
    regVal =   ossaHwRegReadExt(agRoot, PCIBAR0, V_SoftResetRegister ); /* siChipResetV */
    SA_DBG1(("siChipResetV: SPC_SOFT_RESET_SIGNATURE  0x%X\n",regVal));

    if(regVal  == 0xFFFFFFFF)
    {
      SA_DBG1(("siChipResetV: SPC_SOFT_RESET_SIGNATURE AGSA_RC_FAILURE %X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    else if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_NoReset)
    {
      SA_DBG1(("siChipResetV:SPC_SOFT_RESET_SIGNATURE  AGSA_RC_FAILURE %X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    else if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_SoftResetHDAOccurred)
    {
      SA_DBG1(("siChipResetV: SPC_SOFT_RESET_SIGNATURE AGSA_RC_FAILURE 0x%X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    else if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_ChipResetOccurred)
    {
      SA_DBG1(("siChipResetV: SPC_SOFT_RESET_SIGNATURE AGSA_RC_FAILURE 0x%X\n",regVal));
      returnVal = AGSA_RC_FAILURE;
    }
    else if((regVal & SPCv_Reset_Read_Mask) == SPCv_Reset_Read_NormalResetOccurred  )
    {
      SA_DBG1(("siChipResetV: SPC_SOFT_RESET_SIGNATURE AGSA_RC_SUCCESS 0x%X\n",regVal));
      returnVal = AGSA_RC_SUCCESS;
    }
    SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x b\n",ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)));

    if( returnVal != AGSA_RC_SUCCESS)
    {
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)  & SCRATCH_PAD1_V_BOOTSTATE_MASK;
      if(SCRATCH_PAD1 == SCRATCH_PAD1_V_BOOTSTATE_HDA_SEEPROM )
      {
        SA_DBG1(("siChipResetV: Reset done FW did not start BOOTSTATE_HDA_SEEPROM\n"));
        return (returnVal);
      }
      else if(SCRATCH_PAD1 ==  SCRATCH_PAD1_V_BOOTSTATE_HDA_BOOTSTRAP)
      {
        SA_DBG1(("siChipResetV: Reset done FW did not start BOOTSTATE_HDA_BOOTSTRAP\n"));
        return (returnVal);
      }
      else if(SCRATCH_PAD1 == SCRATCH_PAD1_V_BOOTSTATE_HDA_SOFTRESET )
      {
        SA_DBG1(("siChipResetV: Reset done FW did not start BOOTSTATE_HDA_SOFTRESET\n"));
        return (returnVal);
      }
      else if(SCRATCH_PAD1 == SCRATCH_PAD1_V_BOOTSTATE_CRIT_ERROR )
      {
        SA_DBG1(("siChipResetV: Reset done FW did not start BOOTSTATE_CRIT_ERROR\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3A");
        return (returnVal);
      }
    }

     /* RESET */
    smTrace(hpDBG_LOUD,"Lx",ossaTimeStamp64(agRoot));
    max_wait_time = (100 * 1000); /* wait 100 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while ((SCRATCH_PAD1  == 0xFFFFFFFF  ) && (max_wait_count -= WAIT_INCREMENT));

    smTrace(hpDBG_LOUD,"Ly",ossaTimeStamp64(agRoot));
    SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x (0x%x) PCIe ready took %d\n", SCRATCH_PAD1,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));
    /* ILA */
    max_wait_time = (1000 * 1000); /* wait 1000 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_ILA_MASK) != SCRATCH_PAD1_V_ILA_MASK) && (max_wait_count -= WAIT_INCREMENT));
    SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x SCRATCH_PAD1_V_ILA_MASK (0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_ILA_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

    if (!max_wait_count)
    {
      returnVal = AGSA_RC_FAILURE;
      SA_DBG1(("siChipResetV:Timeout SCRATCH_PAD1_V_ILA_MASK (0x%x)  not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_ILA_MASK, SCRATCH_PAD1));
    }
    /* RAAE */
    smTrace(hpDBG_LOUD,"Lz",ossaTimeStamp64(agRoot));
    max_wait_time = (1800 * 1000); /* wait 1800 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_RAAE_MASK) != SCRATCH_PAD1_V_RAAE_MASK) && (max_wait_count -= WAIT_INCREMENT));

    SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x SCRATCH_PAD1_V_RAAE_MASK (0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_RAAE_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

    if (!max_wait_count)
    {
      returnVal = AGSA_RC_FAILURE;
      SA_DBG1(("siChipResetV:Timeout SCRATCH_PAD1_V_RAAE_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_RAAE_MASK, SCRATCH_PAD1));
    }
    /* IOP0 */
    smTrace(hpDBG_LOUD,"La",ossaTimeStamp64(agRoot));
    max_wait_time = (600 * 1000); /* wait 600 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP0_MASK) != SCRATCH_PAD1_V_IOP0_MASK) && (max_wait_count -= WAIT_INCREMENT));
    SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x  SCRATCH_PAD1_V_IOP0_MASK(0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_IOP0_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

    if (!max_wait_count)
    {
      returnVal = AGSA_RC_FAILURE;
      SA_DBG1(("siChipResetV:Timeout SCRATCH_PAD1_V_IOP0_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_IOP0_MASK ,SCRATCH_PAD1));
    }

    if(smIS_SPCV_2_IOP(agRoot))
    {
      /* IOP1 */
      smTrace(hpDBG_LOUD,"Lb",ossaTimeStamp64(agRoot));
      max_wait_time = (200 * 1000); /* wait 200 milliseconds */
      max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
      do
      {
        ossaStallThread(agRoot, WAIT_INCREMENT);
        SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
      } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP1_MASK) != SCRATCH_PAD1_V_IOP1_MASK) && (max_wait_count -= WAIT_INCREMENT));
      SA_DBG1(("siChipResetV:SCRATCH_PAD1 = 0x%x SCRATCH_PAD1_V_IOP1_MASK (0x%x) (0x%x)(0x%x)\n", SCRATCH_PAD1,SCRATCH_PAD1_V_IOP1_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

      if (!max_wait_count)
      {
        returnVal = AGSA_RC_FAILURE;
        SA_DBG1(("siChipResetV: SCRATCH_PAD1_V_IOP1_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_IOP1_MASK, SCRATCH_PAD1));
      }
    }
    smTrace(hpDBG_LOUD,"Lc",ossaTimeStamp64(agRoot));
    regVal = ossaHwRegReadExt(agRoot,PCIBAR0 ,V_SoftResetRegister );
    SA_DBG1(("siChipResetV: Reset done 0x%X ERROR_STATE 0x%X\n",regVal,
    SCRATCH_PAD1_V_ERROR_STATE( ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1) ) ));
    if(SCRATCH_PAD1_V_ERROR_STATE( ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)) )
    {
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "3A");
      return AGSA_RC_FAILURE;
    }

  }
  else  /* signature = unknown */
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "3A");
    return AGSA_RC_FAILURE;
  }

  smTrace(hpDBG_LOUD,"Ld",ossaTimeStamp64(agRoot));

  SA_DBG1(("siChipResetV: out V_SoftResetRegister  %08X\n",  ossaHwRegReadExt(agRoot, PCIBAR0, V_SoftResetRegister) ));
#ifdef SOFT_RESET_TEST
  DbgPrint("SCRATCH_PAD1 = 0x%x \n",ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1));
#endif
  smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "3A");
  return returnVal;

}
/******************************************************************************/
/*! \brief Function to Reset the SPC Hardware
 *
 *  The siChipResetSpc() function is called to reset the SPC chip. Upon return,
 *  the SPC chip got reset. The PCIe bus got reset.
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siChipResetSpc(
                      agsaRoot_t  *agRoot
                      )
{
    bit32        regVal;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"5c");

    SA_DBG1(("siChipResetSpc: Chip Reset start\n"));

    /* Reset the chip */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_RESET);
    regVal &= ~(SPC_REG_RESET_DEVICE);
    ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, regVal); /* siChipResetSpc */

    /* delay 10 usec */
    ossaStallThread(agRoot, WAIT_INCREMENT);

    /* bring chip reset out of reset */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_RESET);
    regVal |= SPC_REG_RESET_DEVICE;
    ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, regVal); /* siChipResetSpc */

    /* delay 10 usec */
    ossaStallThread(agRoot, WAIT_INCREMENT);

    /* wait for 20 msec until the firmware gets reloaded */
    ossaStallThread(agRoot, (20 * 1000));

    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5c");

    SA_DBG1(("siChipResetSpc: Chip Reset Complete\n"));

    return;
}


GLOBAL bit32 siSoftReset(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       )
{
  bit32 ret = AGSA_RC_SUCCESS;

  if(smIS_SPCV(agRoot))
  {
    ret = si_V_SoftReset(agRoot, signature  );
  }
  else
  {
    ret = siSpcSoftReset(agRoot, signature  );
  }

  return(ret);
}

LOCAL bit32 si_V_SoftReset(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       )
{

  bit32 ret = AGSA_RC_SUCCESS;

  ret = siChipResetV(agRoot, signature);

  if (signature == SPC_SOFT_RESET_SIGNATURE)
  {
    SA_DBG1(("si_V_SoftReset:SPC_SOFT_RESET_SIGNATURE\n"));
  }
  else if (signature == SPC_HDASOFT_RESET_SIGNATURE)
  {
    SA_DBG1(("si_V_SoftReset: SPC_HDASOFT_RESET_SIGNATURE\n"));
  }

  SA_DBG1(("si_V_SoftReset: Reset Complete status 0x%X\n",ret));
  return ret;
}

/******************************************************************************/
/*! \brief Function to soft/FW reset the SPC
 *
 *  The siSpcSoftReset() function is called to soft reset SPC. Upon return,
 *  the SPC FW got reset. The PCIe bus is not touched.
 *
 *  \param agRoot    handles for this instance of SAS/SATA hardware
 *  \param signature soft reset normal signature or HDA soft reset signature
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL bit32 siSpcSoftReset(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       )
{
    spc_configMainDescriptor_t mainCfg;
    bit32                      regVal, toggleVal;
    bit32                      max_wait_time;
    bit32                      max_wait_count;
    bit32                      regVal1, regVal2, regVal3;


    /* sanity check */
    SA_ASSERT( (agNULL != agRoot), "agNULL != agRoot");
    if(agNULL != agRoot->sdkData)
    {
      smTraceFuncEnter(hpDBG_VERY_LOUD,"5t");
    }

    SA_DBG1(("siSpcSoftReset: start\n"));


#if defined(SALLSDK_DEBUG)
    /* count SoftReset */
    gLLSoftResetCounter++;
    SA_DBG1(("siSpcSoftReset: ResetCount = 0x%x\n", gLLSoftResetCounter));
#endif

    /* step1: Check FW is ready for soft reset */

    smTrace(hpDBG_VERY_LOUD,"Q1", 1);
    /* TP:Q1 siSpcSoftReset */

    if(AGSA_RC_FAILURE == siSpcSoftResetRDYChk(agRoot))
    {
      SA_DBG1(("siSoftReset:siSoftResetRDYChk failed\n"));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5t");
      }
      return AGSA_RC_FAILURE;
    }

     /* step 2: clear NMI status register on AAP1 and IOP, write the same value to clear */
    /* map 0x60000 to BAR4(0x20), BAR2(win) */
    smTrace(hpDBG_VERY_LOUD,"Q2", 2);
    /* TP:Q2 siSpcSoftReset */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, MBIC_AAP1_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", MBIC_AAP1_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5t");
      }

      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",1));
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, MBIC_NMI_ENABLE_VPE0_IOP);
    SA_DBG1(("MBIC(A) - NMI Enable VPE0 (IOP): = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR2, MBIC_NMI_ENABLE_VPE0_IOP, 0x0);   /* siSpcSoftReset */

    /* map 0x70000 to BAR4(0x20), BAR2(win) */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, MBIC_IOP_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", MBIC_IOP_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "5t");
      }
      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",2));
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, MBIC_NMI_ENABLE_VPE0_AAP1);
    SA_DBG1(("MBIC(A) - NMI Enable VPE0 (AAP1): = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR2, MBIC_NMI_ENABLE_VPE0_AAP1, 0x0); /* siSpcSoftReset */

    regVal = ossaHwRegReadExt(agRoot, PCIBAR1, PCIE_EVENT_INTERRUPT_ENABLE);
    SA_DBG1(("PCIE - Event Interrupt Enable Register: = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR1, PCIE_EVENT_INTERRUPT_ENABLE, 0x0); /* siSpcSoftReset */

    regVal = ossaHwRegReadExt(agRoot, PCIBAR1, PCIE_EVENT_INTERRUPT);
    SA_DBG1(("PCIE - Event Interrupt Register: = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR1, PCIE_EVENT_INTERRUPT, regVal);  /* siSpcSoftReset */

    regVal = ossaHwRegReadExt(agRoot, PCIBAR1, PCIE_ERROR_INTERRUPT_ENABLE);
    SA_DBG1(("PCIE - Error Interrupt Enable Register: = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR1, PCIE_ERROR_INTERRUPT_ENABLE, 0x0); /* siSpcSoftReset */

    regVal = ossaHwRegReadExt(agRoot, PCIBAR1, PCIE_ERROR_INTERRUPT);
    SA_DBG1(("PCIE - Error Interrupt Register: = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR1, PCIE_ERROR_INTERRUPT, regVal); /* siSpcSoftReset */

    /* read the scratch pad 1 register bit 2 */
    regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1) & SCRATCH_PAD1_RST;
    toggleVal = regVal ^ SCRATCH_PAD1_RST;

    /* set signature in host scratch pad0 register to tell SPC that the host performs the soft reset */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_0, signature);

    /* read required registers for confirmming */
    /* map 0x0700000 to BAR4(0x20), BAR2(win) */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, GSM_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", GSM_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "5t");
      }
      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",3));
      return AGSA_RC_FAILURE;
    }

    SA_DBG1(("GSM 0x0 (0x00007b88) - GSM Configuration and Reset = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_CONFIG_RESET)));

    smTrace(hpDBG_VERY_LOUD,"Q3", 3);
    /* TP:Q3 siSpcSoftReset */

    /* step 3: host read GSM Configuration and Reset register */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_CONFIG_RESET);
    /* Put those bits to low */
    /* GSM XCBI offset = 0x70 0000
      0x00 Bit 13 COM_SLV_SW_RSTB 1
      0x00 Bit 12 QSSP_SW_RSTB 1
      0x00 Bit 11 RAAE_SW_RSTB 1
      0x00 Bit 9   RB_1_SW_RSTB 1
      0x00 Bit 8   SM_SW_RSTB 1
      */
    regVal &= ~(0x00003b00);
    /* host write GSM Configuration and Reset register */
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_CONFIG_RESET, regVal); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x0 (0x00007b88 ==> 0x00004088) - GSM Configuration and Reset is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_CONFIG_RESET)));

#if defined(SALLSDK_DEBUG)
    /* debugging messge */
    SA_DBG1(("GSM 0x700018 - RAM ECC Double Bit Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, RAM_ECC_DB_ERR)));

    SA_DBG1(("GSM 0x700058 - Read Address Parity Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_INDIC)));
    SA_DBG1(("GSM 0x700060 - Write Address Parity Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_INDIC)));
    SA_DBG1(("GSM 0x700068 - Write Data Parity Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_INDIC)));
#endif

    /* step 4: */
    /* disable GSM - Read Address Parity Check */
    smTrace(hpDBG_VERY_LOUD,"Q4", 4);
    /* TP:Q4 siSpcSoftReset */
    regVal1 = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_CHECK);
    SA_DBG1(("GSM 0x700038 - Read Address Parity Check Enable = 0x%x\n", regVal1));
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_CHECK, 0x0); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x700038 - Read Address Parity Check Enable is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_CHECK)));

    /* disable GSM - Write Address Parity Check */
    regVal2 = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_CHECK);
    SA_DBG1(("GSM 0x700040 - Write Address Parity Check Enable = 0x%x\n", regVal2));
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_CHECK, 0x0); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x700040 - Write Address Parity Check Enable is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_CHECK)));

    /* disable GSM - Write Data Parity Check */
    regVal3 = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_CHECK);
    SA_DBG1(("GSM 0x300048 - Write Data Parity Check Enable = 0x%x\n", regVal3));
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_CHECK, 0x0); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x700048 - Write Data Parity Check Enable is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_CHECK)));
    /* step 5-a: delay 10 usec */
    smTrace(hpDBG_VERY_LOUD,"Q5", 5);
    /* TP:Q5 siSpcSoftReset */
    ossaStallThread(agRoot, 10);

    /* step 5-b: set GPIO-0 output control to tristate anyway */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, GPIO_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", GPIO_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "5t");
      }
      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",4));
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, GPIO_GPIO_0_0UTPUT_CTL_OFFSET);
    SA_DBG1(("GPIO Output Control Register: = 0x%x\n", regVal));
    /* set GPIO-0 output control to tri-state */
    regVal &= 0xFFFFFFFC;
    ossaHwRegWriteExt(agRoot, PCIBAR2, GPIO_GPIO_0_0UTPUT_CTL_OFFSET, regVal); /* siSpcSoftReset */

    /* Step 6: Reset the IOP and AAP1 */
    /* map 0x00000 to BAR4(0x20), BAR2(win) */
    smTrace(hpDBG_VERY_LOUD,"Q6", 6);
    /* TP:Q6 siSpcSoftReset */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, SPC_TOP_LEVEL_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", SPC_TOP_LEVEL_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "5t");
      }
      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",5));
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_RESET);
    SA_DBG1(("Top Register before resetting IOP/AAP1: = 0x%x\n", regVal));
    regVal &= ~(SPC_REG_RESET_PCS_IOP_SS | SPC_REG_RESET_PCS_AAP1_SS);
    ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, regVal); /* siSpcSoftReset */

    /* step 7: Reset the BDMA/OSSP */
    smTrace(hpDBG_VERY_LOUD,"Q7", 7);
    /* TP:Q7 siSpcSoftReset */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_RESET);
    SA_DBG1(("Top Register before resetting BDMA/OSSP: = 0x%x\n", regVal));
    regVal &= ~(SPC_REG_RESET_BDMA_CORE | SPC_REG_RESET_OSSP);
    ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, regVal); /* siSpcSoftReset */

    /* step 8: delay 10 usec */
    smTrace(hpDBG_VERY_LOUD,"Q8", 8);
    /* TP:Q8 siSpcSoftReset */

    ossaStallThread(agRoot, WAIT_INCREMENT);

    /* step 9: bring the BDMA and OSSP out of reset */
    smTrace(hpDBG_VERY_LOUD,"Q9", 9);
    /* TP:Q9 siSpcSoftReset */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_RESET);
    SA_DBG1(("Top Register before bringing up BDMA/OSSP: = 0x%x\n", regVal));
    regVal |= (SPC_REG_RESET_BDMA_CORE | SPC_REG_RESET_OSSP);
    ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, regVal); /* siSpcSoftReset */

    /* step 10: delay 10 usec */
    smTrace(hpDBG_VERY_LOUD,"QA", 10);
    /* TP:QA siSpcSoftReset */
    ossaStallThread(agRoot, WAIT_INCREMENT);

    /* step 11: reads and sets the GSM Configuration and Reset Register */
    /* map 0x0700000 to BAR4(0x20), BAR2(win) */
    smTrace(hpDBG_VERY_LOUD,"QB", 11);
    /* TP:QB siSpcSoftReset */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, GSM_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", GSM_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "5t");
      }
      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",5));
      return AGSA_RC_FAILURE;
    }
    SA_DBG1(("GSM 0x0 (0x00007b88) - GSM Configuration and Reset = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_CONFIG_RESET)));
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_CONFIG_RESET);
    /* Put those bits to high */
    /* GSM XCBI offset = 0x70 0000
      0x00 Bit 13 COM_SLV_SW_RSTB 1
      0x00 Bit 12 QSSP_SW_RSTB 1
      0x00 Bit 11 RAAE_SW_RSTB 1
      0x00 Bit 9   RB_1_SW_RSTB 1
      0x00 Bit 8   SM_SW_RSTB 1
      */
    regVal |= (GSM_CONFIG_RESET_VALUE);
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_CONFIG_RESET, regVal); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x0 (0x00004088 ==> 0x00007b88) - GSM Configuration and Reset is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_CONFIG_RESET)));

#if defined(SALLSDK_DEBUG)
    /* debugging messge */
    SA_DBG1(("GSM 0x700018 - RAM ECC Double Bit Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, RAM_ECC_DB_ERR)));
    SA_DBG1(("GSM 0x700058 - Read Address Parity Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_INDIC)));
    SA_DBG1(("GSM 0x700060 - Write Address Parity Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_INDIC)));
    SA_DBG1(("GSM 0x700068 - Write Data Parity Error Indication = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_INDIC)));
#endif

    /* step 12: Restore GSM - Read Address Parity Check */
    smTrace(hpDBG_VERY_LOUD,"QC", 12);
    /* TP:QC siSpcSoftReset */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_CHECK); /* just for debugging */
    SA_DBG1(("GSM 0x700038 - Read Address Parity Check Enable = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_CHECK, regVal1); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x700038 - Read Address Parity Check Enable is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_READ_ADDR_PARITY_CHECK)));

    /* Restore GSM - Write Address Parity Check */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_CHECK); /* just for debugging */
    SA_DBG1(("GSM 0x700040 - Write Address Parity Check Enable = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_CHECK, regVal2); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x700040 - Write Address Parity Check Enable is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_ADDR_PARITY_CHECK)));

    /* Restore GSM - Write Data Parity Check */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_CHECK); /* just for debugging */
    SA_DBG1(("GSM 0x700048 - Write Data Parity Check Enable = 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_CHECK, regVal3); /* siSpcSoftReset */
    SA_DBG1(("GSM 0x700048 - Write Data Parity Check Enable is set to = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR2, GSM_WRITE_DATA_PARITY_CHECK)));

    /* step 13: bring the IOP and AAP1 out of reset */
    /* map 0x00000 to BAR4(0x20), BAR2(win) */
    smTrace(hpDBG_VERY_LOUD,"QD", 13);
    /* TP:QD siSpcSoftReset */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, SPC_TOP_LEVEL_ADDR_BASE))
    {
      SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", SPC_TOP_LEVEL_ADDR_BASE));
      if(agNULL != agRoot->sdkData)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "5t");
      }
      SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",7));
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_RESET);
    SA_DBG1(("Top Register before bringing up IOP/AAP1: = 0x%x\n", regVal));
    regVal |= (SPC_REG_RESET_PCS_IOP_SS | SPC_REG_RESET_PCS_AAP1_SS);
    ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_RESET, regVal); /* siSpcSoftReset */

    if (signature == SPC_SOFT_RESET_SIGNATURE)
    {
      /* step 14: delay 20 milli - Normal Mode */
      ossaStallThread(agRoot, WAIT_INCREMENT);
    }else if (signature == SPC_HDASOFT_RESET_SIGNATURE)
    {
      /* step 14: delay 200 milli - HDA Mode */
      ossaStallThread(agRoot, 200 * 1000);
    }

    /* check Soft Reset Normal mode or Soft Reset HDA mode */
    if (signature == SPC_SOFT_RESET_SIGNATURE)
    {
        /* step 15 (Normal Mode): wait until scratch pad1 register bit 2 toggled */
        max_wait_time = WAIT_SECONDS(2);  /* 2 sec */
        max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
        do
        {
            ossaStallThread(agRoot, WAIT_INCREMENT);
            regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1) & SCRATCH_PAD1_RST;
        } while ((regVal != toggleVal) && (max_wait_count -=WAIT_INCREMENT));

        if ( !max_wait_count)
        {
            regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
            SA_DBG1(("siSpcSoftReset: TIMEOUT:ToggleVal 0x%x, MSGU_SCRATCH_PAD1 = 0x%x\n", toggleVal, regVal));
            if(agNULL != agRoot->sdkData)
            {
              smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "5t");
            }
#if defined(SALLSDK_DEBUG)
            SA_DBG1(("siSpcSoftReset: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0)));
            SA_DBG1(("siSpcSoftReset: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2)));
            SA_DBG1(("siSpcSoftReset: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif
            SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",8));
            return AGSA_RC_FAILURE;
        }

    /* step 16 (Normal)step 15 (HDA) - Clear ODMR and ODCR */
        smTrace(hpDBG_VERY_LOUD,"QG", 16);
        /* TP:QG siSpcSoftReset */

        ossaHwRegWrite(agRoot, MSGU_ODCR, ODCR_CLEAR_ALL);
        ossaHwRegWrite(agRoot, MSGU_ODMR, ODMR_CLEAR_ALL);
    }
    else if (signature == SPC_HDASOFT_RESET_SIGNATURE)
    {
      if(agNULL != agRoot->sdkData)
      {
        SA_DBG1(("siSpcSoftReset: HDA Soft Reset Complete\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "5t");
      }
      return AGSA_RC_SUCCESS;
    }


    /* step 17 (Normal Mode): wait for the FW and IOP to get ready - 1 sec timeout */
    /* Wait for the SPC Configuration Table to be ready */
    if (mpiWaitForConfigTable(agRoot, &mainCfg) == AGSA_RC_FAILURE)
    {
       regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
       /* return error if MPI Configuration Table not ready */
       SA_DBG1(("siSpcSoftReset: SPC FW not ready SCRATCH_PAD1 = 0x%x\n", regVal));
       regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);
       /* return error if MPI Configuration Table not ready */
       SA_DBG1(("siSpcSoftReset: SPC FW not ready SCRATCH_PAD2 = 0x%x\n", regVal));
       if(agNULL != agRoot->sdkData)
       {
          smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "5t");
       }
#if defined(SALLSDK_DEBUG)
       SA_DBG1(("siSpcSoftReset: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0)));
       SA_DBG1(("siSpcSoftReset: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif
       SA_DBG1(("siSpcSoftReset: Soft Reset AGSA_RC_FAILURE %d\n",9));
            return AGSA_RC_FAILURE;
    }
    smTrace(hpDBG_VERY_LOUD,"QI", 18);
    /* TP:QI siSpcSoftReset */

    if(agNULL != agRoot->sdkData)
    {
      smTraceFuncExit(hpDBG_VERY_LOUD, 'l', "5t");
    }

    SA_DBG1(("siSpcSoftReset: Soft Reset Complete\n"));

    return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief Function to do BAR shifting
 *
 *  The siBarShift() function is called to shift BAR base address
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param shiftValue shifting value
 *
 *  \return success or fail
 */
/*******************************************************************************/
GLOBAL bit32 siBar4Shift(
                      agsaRoot_t  *agRoot,
                      bit32       shiftValue
                      )
{
    bit32 regVal;
    bit32 max_wait_time;
    bit32 max_wait_count;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"5e");
    smTrace(hpDBG_VERY_LOUD,"GA",shiftValue);
    /* TP:GA shiftValue */

    SA_DBG2(("siBar4Shift: shiftValue 0x%x\n",shiftValue));

    if(smIS_SPCV(agRoot) )
    {
      ossaHwRegWriteExt(agRoot, PCIBAR0, V_MEMBASE_II_ShiftRegister, shiftValue);
      /* confirm the setting is written */
      max_wait_time = WAIT_SECONDS(1);  /* 1 sec */
      max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
      do
      {
        ossaStallThread(agRoot, WAIT_INCREMENT);
        regVal = ossaHwRegReadExt(agRoot, PCIBAR0, V_MEMBASE_II_ShiftRegister);
      } while ((regVal != shiftValue) && (max_wait_count -= WAIT_INCREMENT));

      if (!max_wait_count)
      {
        SA_DBG1(("siBar4Shift: TIMEOUT: SPC_IBW_AXI_TRANSLATION_LOW = 0x%x\n", regVal));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5e");
        return AGSA_RC_FAILURE;
      }
    }
    else if(smIS_SPC(agRoot))
    {
      /* program the inbound AXI translation Lower Address */
      ossaHwRegWriteExt(agRoot, PCIBAR1, SPC_IBW_AXI_TRANSLATION_LOW, shiftValue);

      /* confirm the setting is written */
      max_wait_time = WAIT_SECONDS(1);  /* 1 sec */
      max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
      do
      {
        ossaStallThread(agRoot, WAIT_INCREMENT);
        regVal = ossaHwRegReadExt(agRoot, PCIBAR1, SPC_IBW_AXI_TRANSLATION_LOW);
      } while ((regVal != shiftValue) && (max_wait_count -= WAIT_INCREMENT));

      if (!max_wait_count)
      {
        SA_DBG1(("siBar4Shift: TIMEOUT: SPC_IBW_AXI_TRANSLATION_LOW = 0x%x\n", regVal));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5e");
        return AGSA_RC_FAILURE;
      }
    }
    else
    {
        SA_DBG1(("siBar4Shift: hba type is not support\n"));
        return AGSA_RC_FAILURE;
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "5e");

    return AGSA_RC_SUCCESS;
}

#ifdef SA_ENABLE_HDA_FUNCTIONS
/******************************************************************************/
/*! \brief Function to force HDA mode the SPC
 *
 *  The siHDAMode() function is called to force to HDA mode. Upon return,
 *  the SPC FW loaded. The PCIe bus is not touched.
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param HDAMode 0 - HDA soft reset mode, 1 - HDA mode
 *  \param fwImg points to structure containing fw images
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL bit32 siHDAMode(
                      agsaRoot_t  *agRoot,
                      bit32       HDAMode,
                      agsaFwImg_t *userFwImg
                      )
{
    spc_configMainDescriptor_t mainCfg;
    bit32                      regVal;
    bit32                      max_wait_time;
    bit32                      max_wait_count;
    agsaFwImg_t                flashImg;
    bit32                      startTime, endTime; // TestBase
    bit32                      stepTime[12]; // TestBase

    bit32 HDA_Been_Reset = agFALSE;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"5d");

    /* sanity check */
    SA_ASSERT( (agNULL != agRoot), "");

    SA_DBG1(("siHDAMode: start\n"));

    si_memset(&flashImg, 0, sizeof(flashImg));
#ifndef SA_EXCLUDE_FW_IMG

    /* Set up built-in (default) FW image pointers */
/*
    flashImg.aap1Img = (bit8*)(&aap1array);
    flashImg.aap1Len = sizeof(aap1array);
    flashImg.ilaImg  = (bit8*)(&ilaarray);
    flashImg.ilaLen  = sizeof(ilaarray);
    flashImg.iopImg  = (bit8*)(&ioparray);
    flashImg.iopLen  = sizeof(ioparray);
*/
#endif
    TryAfterReset:

    /* Set up user FW image pointers (if passed in) */
    if (userFwImg)
    {
      SA_DBG1(("siHDAMode: User fw structure @ %p\n",userFwImg));
      if (userFwImg->aap1Img && userFwImg->aap1Len)
      {
        flashImg.aap1Img = userFwImg->aap1Img;
        flashImg.aap1Len = userFwImg->aap1Len;
        SA_DBG1(("siHDAMode: User fw aap1 @ %p (%d)\n", flashImg.aap1Img, flashImg.aap1Len));
      }
      if (userFwImg->ilaImg && userFwImg->ilaLen)
      {
        flashImg.ilaImg = userFwImg->ilaImg;
        flashImg.ilaLen = userFwImg->ilaLen;
        SA_DBG1(("siHDAMode: User fw ila @ %p (%d)\n",  flashImg.ilaImg, flashImg.ilaLen));
      }
      if (userFwImg->iopImg && userFwImg->iopLen)
      {
        flashImg.iopImg = userFwImg->iopImg;
        flashImg.iopLen = userFwImg->iopLen;
        SA_DBG1(("siHDAMode: User fw iop @ %p (%d)\n", flashImg.iopImg, flashImg.iopLen));
      }
      if (userFwImg->istrImg && userFwImg->istrLen)
      {
        flashImg.istrImg = userFwImg->istrImg;
        flashImg.istrLen = userFwImg->istrLen;
        SA_DBG1(("siHDAMode: User fw istr @ %p (%d)\n", flashImg.istrImg, flashImg.istrLen));
      }
    }
    else
    {
      SA_DBG1(("siHDAMode: user supplied FW is not found\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5d");
      return AGSA_RC_FAILURE;
    }

#ifdef SA_EXCLUDE_FW_IMG
    /* Check that fw images are setup properly */
    if (!(flashImg.aap1Img && flashImg.aap1Len &&
          flashImg.ilaImg  && flashImg.ilaLen  &&
          flashImg.iopImg  && flashImg.iopLen  &&
          flashImg.istrImg && flashImg.istrLen))
    {
      SA_DBG1(("siHDAMode: Built-in FW img excluded and not user defined.\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5d");
      return AGSA_RC_FAILURE;
    }
#endif

    /* Check HDA mode with Soft Reset */
    if (!HDAMode)
    {
      /* Try soft reset until it goes into HDA mode */
      siSpcSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);

      /* read response state */
      regVal = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;
      if (regVal != BOOTTLOADERHDA_IDLE)
      {
        /* Can not go into HDA mode with 200 ms wait - HDA Soft Reset failed */
        SA_DBG1(("siHDAMode: HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET = 0x%x\n", regVal));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "5d");
        return AGSA_RC_FAILURE;
      }

      /* HDA Mode - Clear ODMR and ODCR */
      ossaHwRegWrite(agRoot, MSGU_ODCR, ODCR_CLEAR_ALL);
      ossaHwRegWrite(agRoot, MSGU_ODMR, ODMR_CLEAR_ALL);
    }

    /* Step 1: Poll BOOTTLOADERHDA_IDLE - HDA mode */
    SA_DBG1(("siHDAMode: Step1:Poll for HDAR_IDLE\n"));
    max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      regVal = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;
    } while ((regVal != BOOTTLOADERHDA_IDLE) && (max_wait_count -= WAIT_INCREMENT));

    if (!max_wait_count)
    {

      if( !HDA_Been_Reset )
      {

        SA_DBG1(("siHDAMode: Reset: Step1:regVal =0x%x expect 0x%x\n",  regVal,ILAHDA_AAP1_IMG_GET ));
        siSpcSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
        HDA_Been_Reset  = agTRUE;
        goto TryAfterReset;

      }

      SA_DBG1(("siHDAMode: Step1:TIMEOUT: HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET = 0x%x\n", regVal));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "5d");
      return AGSA_RC_FAILURE;
    }

    /* Step 2: Push the init string to 0x0047E000 & data compare */
    SA_DBG1(("siHDAMode: Step2:Push the init string to 0x0047E000!\n"));

    if (AGSA_RC_FAILURE == siBar4Cpy(agRoot, ILA_ISTR_ADDROFFSETHDA, flashImg.istrImg, flashImg.istrLen))
    {
      SA_DBG1(("siHDAMode: Step2:Copy ISTR array to 0x%x failed\n", ILA_ISTR_ADDROFFSETHDA));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "5d");
      return AGSA_RC_FAILURE;
    }

    /* Tell FW ISTR is ready */
    regVal = (HDA_ISTR_DONE | (bit32)flashImg.istrLen);
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_3, regVal);
    SA_DBG1(("siHDAMode: Step2:Host Scratchpad 3 (AAP1-ISTR): 0x%x\n", regVal));

    stepTime[2] = ossaTimeStamp(agRoot);  // TestBase 
    SA_DBG1(("siHDAMode: End Step2: (step_time[2] = %d)\n", stepTime[2]));  // TestBase 

    /* Step 3: Write the HDA mode SoftReset signature */
    SA_DBG1(("siHDAMode: Step3:Set Signature!\n"));
    /* set signature in host scratch pad0 register to tell SPC that the host performs the HDA mode */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_0, SPC_HDASOFT_RESET_SIGNATURE);

    stepTime[3] = ossaTimeStamp(agRoot);  // TestBase 
    SA_DBG1(("siHDAMode: End Step3: (step_time[3] =  %d)\n", stepTime[3]));  // TestBase 

    // Priya (Apps) requested that the FW load time measurement be started here
    startTime = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: Step4: Ready to push ILA to 0x00400000! (start_time =  %d)\n", startTime));  // TestBase 

    /* Step 4: Push the ILA image to 0x00400000 */
    SA_DBG1(("siHDAMode: Step4:Push the ILA to 0x00400000!\n"));

    if (AGSA_RC_FAILURE == siBar4Cpy(agRoot, 0x0, flashImg.ilaImg, flashImg.ilaLen))
    {
      SA_DBG1(("siHDAMode:Step4:Copy ILA array to 0x%x failed\n", 0x0));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "5d");
      return AGSA_RC_FAILURE;
    }

    stepTime[4] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step4: (step_time[4] = %d, %d ms)\n", stepTime[4], (stepTime[4] - startTime)));  // TestBase 

    /* Step 5: Tell boot ROM to authenticate ILA and execute it */
    ossaHwRegWriteExt(agRoot, PCIBAR3, HDA_CMD_OFFSET1MB, 0);
    ossaHwRegWriteExt(agRoot, PCIBAR3, HDA_CMD_OFFSET1MB+HDA_PAR_LEN_OFFSET, flashImg.ilaLen);
    regVal = (ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_SEQ_ID_BITS ) >> SHIFT16;
    regVal ++;
    regVal = (HDA_C_PA << SHIFT24) | (regVal << SHIFT16) | HDAC_EXEC_CMD;
    SA_DBG1(("siHDAMode: Step5:Execute ILA CMD: 0x%x\n", regVal));
    ossaHwRegWriteExt(agRoot, PCIBAR3, HDA_CMD_OFFSET1MB+HDA_CMD_CODE_OFFSET, regVal); /* Execute Command */

    stepTime[5] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step5: (step_time[5] = %d, %d ms)\n", stepTime[5], (stepTime[5] - startTime)));  // TestBase 


    /* Step 6: Checking response status from boot ROM, HDAR_EXEC (good), HDAR_BAD_CMD and HDAR_BAD_IMG */
    SA_DBG1(("siHDAMode: Step6:Checking boot ROM reponse status!\n"));
    max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      regVal = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;
      if ((HDAR_EXEC == regVal) || (HDAR_BAD_IMG == regVal) || (HDAR_BAD_CMD == regVal))
        break;
    } while (max_wait_count-=WAIT_INCREMENT);

    if (HDAR_BAD_IMG == regVal)
    {
      SA_DBG1(("siHDAMode: Step6:BAD IMG: HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET = 0x%x\n", regVal));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "5d");
      return AGSA_RC_FAILURE;
    }
    if (HDAR_BAD_CMD == regVal)
    {
      SA_DBG1(("siHDAMode: Step6:BAD IMG: HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET = 0x%x\n", regVal));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "5d");
      return AGSA_RC_FAILURE;
    }
    if (!max_wait_count)
    {
      SA_DBG1(("siHDAMode: Step6:TIMEOUT: HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET = 0x%x\n", regVal));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "5d");
      return AGSA_RC_FAILURE;
    }

    stepTime[6] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step6: (step_time[6] = %d, %d ms)\n", stepTime[6], (stepTime[6] - startTime)));  // TestBase 

    /* Step 7: Poll ILAHDA_AAP1IMGGET/Offset in MSGU Scratchpad 0 */
    /* Check MSGU Scratchpad 1 [1,0] == 00 */
    SA_DBG1(("siHDAMode: Step7:Poll ILAHDA_AAP1_IMG_GET!\n"));
    regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1) & SCRATCH_PAD1_RST;
    SA_DBG1(("siHDAMode: Step7:MSG Scratchpad 1: 0x%x\n", regVal));
    max_wait_time = WAIT_SECONDS(gWait_3);  /* 3 sec */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0) >> SHIFT24;
    } while ((regVal != ILAHDA_AAP1_IMG_GET) && (max_wait_count -= WAIT_INCREMENT));

    if (!max_wait_count)
    {

      if( !HDA_Been_Reset )
      {

        SA_DBG1(("siHDAMode: Reset: Step7:regVal =0x%x expect 0x%x\n",  regVal,ILAHDA_AAP1_IMG_GET ));
        siSpcSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
        HDA_Been_Reset  = agTRUE;
        goto TryAfterReset;

      }

      SA_DBG1(("siHDAMode: TIMEOUT: Step7:regVal =0x%x expect 0x%x\n",  regVal,ILAHDA_AAP1_IMG_GET ));
#if defined(SALLSDK_DEBUG)
      SA_DBG1(("siHDAMode: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif
      smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "5d");
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0);
    SA_DBG1(("siHDAMode: Step7:MSG Scratchpad 0: 0x%x\n", regVal));
    regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0) & 0x00FFFFFF;

    stepTime[7] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step7: (step_time[7] = %d, %d ms)\n", stepTime[7], (stepTime[7] - startTime)));  // TestBase 

    /* Step 8: Copy AAP1 image, update the Host Scratchpad 3 */
    SA_DBG1(("siHDAMode: Step8:Push the AAP1 to 0x00400000 plus 0x%x\n", regVal));

    if (AGSA_RC_FAILURE == siBar4Cpy(agRoot, regVal, flashImg.aap1Img, flashImg.aap1Len))
    {
      SA_DBG1(("siHDAMode: Step8:Copy AAP1 array to 0x%x failed\n", regVal));
#if defined(SALLSDK_DEBUG)
      SA_DBG1(("siHDAMode: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif
      smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "5d");
      return AGSA_RC_FAILURE;
    }

    regVal = (HDA_AAP1_DONE | (bit32)flashImg.aap1Len);
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_3, regVal);
    SA_DBG1(("siHDAMode: Step8:Host Scratchpad 3 (AAP1): 0x%x\n", regVal));

    stepTime[8] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step8: (step_time[8] = %d, %d ms)\n", stepTime[8], (stepTime[8] - startTime)));  // TestBase 

    /* Step 9: Poll ILAHDA_IOPIMGGET/Offset in MSGU Scratchpad 0 */
    SA_DBG1(("siHDAMode: Step9:Poll ILAHDA_IOP_IMG_GET!\n"));
    max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0) >> SHIFT24;
    } while ((regVal != ILAHDA_IOP_IMG_GET) && (max_wait_count -= WAIT_INCREMENT));

    if (!max_wait_count)
    {
      SA_DBG1(("siHDAMode: Step9:TIMEOUT:MSGU_SCRATCH_PAD_0 = 0x%x\n", regVal));
#if defined(SALLSDK_DEBUG)
      SA_DBG1(("siHDAMode: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif
      smTraceFuncExit(hpDBG_VERY_LOUD, 'l', "5d");
      return AGSA_RC_FAILURE;
    }
    regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0);
    SA_DBG1(("siHDAMode: Step9:MSG Scratchpad 0: 0x%x\n", regVal));
    regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0) & HDA_GSM_OFFSET_BITS;

    stepTime[9] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step9: (step_time[9] = %d, %d ms)\n", stepTime[9], (stepTime[9] - startTime)));  // TestBase 

    // saHdaLoadForceHalt(agRoot);  // TestBase

    /* Step 10: Copy IOP image, update the Host Scratchpad 3 */
    SA_DBG1(("siHDAMode: Step10:Push the IOP to 0x00400000 plus 0x%x!\n", regVal));

    if (AGSA_RC_FAILURE == siBar4Cpy(agRoot, regVal, flashImg.iopImg, flashImg.iopLen))
    {
      SA_DBG1(("siHDAMode: Step10:Copy IOP array to 0x%x failed\n", regVal));
#if defined(SALLSDK_DEBUG)
      SA_DBG1(("siHDAMode: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2)));
      SA_DBG1(("siHDAMode: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif
      smTraceFuncExit(hpDBG_VERY_LOUD, 'm', "5d");
      return AGSA_RC_FAILURE;
    }

    regVal = (HDA_IOP_DONE | (bit32)flashImg.iopLen);
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_3, regVal);
    SA_DBG1(("siHDAMode: Step10:Host Scratchpad 3 (IOP): 0x%x\n", regVal));

    stepTime[10] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step10: (step_time[10] = %d, %d ms)\n", stepTime[10], (stepTime[10] - startTime)));  // TestBase 

    /* Clear the signature */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_0, 0);

    /* step 11: wait for the FW and IOP to get ready - 1 sec timeout */
    /* Wait for the SPC Configuration Table to be ready */
    stepTime[11] = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: Start Step11: Wait for FW ready. (step_time[11.1] =  %d, %d ms)\n", stepTime[11], (stepTime[11] - startTime))); // TestBase 

    endTime = ossaTimeStamp(agRoot);
    SA_DBG1(("siHDAMode: End Step11: FW ready! (end_time= %d, fw_load_time = %d ms)\n", endTime, endTime - startTime)); // TestBase 

    SA_DBG1(("siHDAMode: Step11:Poll for FW ready!\n"));
    if (mpiWaitForConfigTable(agRoot, &mainCfg) == AGSA_RC_FAILURE)
    {
      regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
      /* return error if MPI Configuration Table not ready */
      SA_DBG1(("siHDAMode: Step11:SPC FW not ready SCRATCH_PAD1 = 0x%x\n", regVal));
      regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);
      /* return error if MPI Configuration Table not ready */
      SA_DBG1(("siHDAMode: Step11:SPC FW not ready SCRATCH_PAD2 = 0x%x\n", regVal));
      /* read detail fatal errors */
      regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0);
      SA_DBG1(("siHDAMode: Step11:ScratchPad0 AAP error code 0x%x\n", regVal));
      regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3);
      SA_DBG1(("siHDAMode: Step11:ScratchPad3 IOP error code 0x%x\n", regVal));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'n', "5d");
      return AGSA_RC_FAILURE;
    }

    smTraceFuncExit(hpDBG_VERY_LOUD, 'o', "5d");

    SA_DBG1(("siHDAMode: HDA Mode Complete\n"));

    return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief memcopy cross PCI from host memory to card memory
 *
 *  \param agRoot        handles for this instance of SAS/SATA hardware
 *  \param dstoffset     distination offset
 *  \param src           source pointer
 *  \param DWcount       DWord count
 *  \param busBaseNumber PCI Bus Base number
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
LOCAL void siPciMemCpy(agsaRoot_t *agRoot,
                       bit32 dstoffset,
                       void *src, 
                       bit32 DWcount,
                       bit32 busBaseNumber
                       )
{
    bit32 i, val;
    bit32 *src1;

    src1 = (bit32 *)src;

    for (i= 0; i < DWcount; i++)
    {
        val = BIT32_TO_LEBIT32(src1[i]);
        ossaHwRegWriteExt(agRoot, busBaseNumber, (dstoffset + i * 4), val);
    }

    return;
}

/******************************************************************************/
/*! \brief Function to copy FW array
 *
 *  The siBar4Cpy() function is called to copy FW array via BAR4
 *  (PCIe spec: BAR4, MEMBASE-III in PM, PCIBAR2 in host driver)
 *  in 64-KB MEMBASE MODE.
 *
 *  \param agRoot     handles for this instance of SAS/SATA hardware
 *  \param offset     destination offset
 *  \param parray     pointer of array
 *  \param array_size size of array
 *
 *  \return AGSA_RC_SUCCESS or AGSA_RC_FAILURE
 */
/*******************************************************************************/
LOCAL bit32 siBar4Cpy(
                      agsaRoot_t  *agRoot,
                      bit32       offset,
                      bit8        * parray,
                      bit32       array_size
                      )
{
    bit32       dest_shift_addr, dest_offset, cpy_size;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"5f");

    /* first time to shift */
    dest_shift_addr = (GSMSM_AXI_LOWERADDR+offset) & SHIFT_MASK;
    dest_offset = offset & OFFSET_MASK;
    do
    {
        if (AGSA_RC_FAILURE == siBar4Shift(agRoot, dest_shift_addr))
        {
            SA_DBG1(("siHDAMode:Shift Bar4 to 0x%x failed\n", dest_shift_addr));
            smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5f");
            return AGSA_RC_FAILURE;
        }

        if ((dest_offset+array_size) > SIZE_64KB)
        {
            cpy_size = SIZE_64KB - dest_offset;
        }
        else
            cpy_size = array_size;

        siPciMemCpy(agRoot, dest_offset, parray, (bit32)(CEILING(cpy_size,4)), PCIBAR2);

        array_size -= cpy_size;
        dest_shift_addr += SIZE_64KB;
        dest_offset = 0;
        parray = parray + cpy_size;
    } while (array_size !=0 );

    /* Shift back to BAR4 original address */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, 0x0))
    {
        SA_DBG1(("siHDAMode:Shift Bar4 to 0x%x failed\n", 0x0));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5f");
        return AGSA_RC_FAILURE;
    }

    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "5f");

    return AGSA_RC_SUCCESS;
}

GLOBAL
bit32 siHDAMode_V(
                      agsaRoot_t  *agRoot,
                      bit32       HDAMode,
                      agsaFwImg_t *userFwImg
                      )
{
  bit32 returnVal = AGSA_RC_FAILURE;
  bit32 save,i,biggest;
  bit32 hda_status;
  bit32 hda_command_complete = 0;
  bit32 max_wait_time;
  bit32 max_wait_count;
  bit32 seq_id = 0;
  bit32 base_Hi = 0;
  bit32 base_Lo = 0;
  bit8 * pbase;

  spcv_hda_cmd_t hdacmd;
  spcv_hda_rsp_t hdarsp;

  agsaLLRoot_t      *saRoot;

  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "");

  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  /* sanity check */
  SA_ASSERT( (agNULL != saRoot), "saRoot is NULL");

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2W");

  SA_DBG1(("siHDAMode_V: HDAMode %X\n",HDAMode));

  siScratchDump(agRoot);
  if( agNULL == userFwImg)
  {
    SA_DBG1(("siHDAMode_V: No image agNULL == userFwImg\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2W");
    return returnVal;
  }

  hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28));

  SA_DBG1(("siHDAMode_V: hda_status 0x%08X\n",hda_status ));
  SA_DBG1(("siHDAMode_V:                                                                   STEP 1\n"));

  smTrace(hpDBG_VERY_LOUD,"2X",1 );
  /* TP:2X STEP 1 */

  /* Find largest Physical chunk memory */
  for(i=0,biggest = 0,save = 0; i < saRoot->memoryAllocated.count; i++)
  {
    if( saRoot->memoryAllocated.agMemory[i].totalLength > biggest)
    {

      if(biggest < saRoot->memoryAllocated.agMemory[i].totalLength)
      {
        save = i;
        biggest = saRoot->memoryAllocated.agMemory[i].totalLength;
      }

    }
  }
/*
Step 1 The host reads the HDA response field RSP_CODE at byte offset 28:29 of the response block
for HDAR_IDLE (0x8002) via MEMBASE-I. A value other than HDAR_IDLE (0x8002) indicates that the
SPCv controller is not in HDA mode. Follow the steps described in Section 4.21.1 to bring the
SPCv controller into HDA mode. When the host reads the correct RSP_CODE, it indicates that the
SPCv controller boot ROM is ready to proceed to the next step of HDA initialization
*/

  base_Hi = saRoot->memoryAllocated.agMemory[save].phyAddrUpper;
  base_Lo = saRoot->memoryAllocated.agMemory[save].phyAddrLower;
  pbase = saRoot->memoryAllocated.agMemory[save].virtPtr;
  SA_DBG1(("siHDAMode_V:Use DMA memory at [%d] size 0x%x (%d) DMA Loc U 0x%08x L 0x%08x @%p\n",save,
                                biggest,
                                biggest,
                                base_Hi,
                                base_Lo,
                                pbase
                               ));


  SA_DBG1(("siHDAMode_V: HDA aap1Img %p len %8d 0x%x\n", userFwImg->aap1Img, userFwImg->aap1Len , userFwImg->aap1Len ));
  SA_DBG1(("siHDAMode_V: HDA ilaImg  %p len %8d 0x%x\n", userFwImg->ilaImg,  userFwImg->ilaLen ,  userFwImg->ilaLen ));
  SA_DBG1(("siHDAMode_V: HDA iopImg  %p len %8d 0x%x\n", userFwImg->iopImg,  userFwImg->iopLen  , userFwImg->iopLen ));
  if(userFwImg->aap1Len > biggest)
  {
    SA_DBG1(("siHDAMode_V: HDA DMA area too small %d < %d aap1Len\n", biggest ,userFwImg->aap1Len));
    SA_ASSERT( (agNULL != agRoot), "aap1Len > biggest");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2W");
    return returnVal;
  }
  if(userFwImg->ilaLen > biggest)
  {
    SA_DBG1(("siHDAMode_V: HDA DMA area too small %d < %d ilaLen\n", biggest ,userFwImg->ilaLen));
    SA_ASSERT( (agNULL != agRoot), "ilaLen > biggest");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2W");
    return returnVal;
  }
  if(userFwImg->iopLen > biggest)
  {
    SA_DBG1(("siHDAMode_V: HDA DMA area too small %d < %d iopLen\n", biggest ,userFwImg->iopLen));
    SA_ASSERT( (agNULL != agRoot), "iopLen > biggest");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2W");
    return returnVal;
  }


  if(HDA_STEP_2)
  { /* ILA */
    si_memset(pbase, 0, biggest);

    if( userFwImg->ilaLen < biggest)
    {
      si_memcpy(pbase,userFwImg->ilaImg, userFwImg->ilaLen );
    }
    else
    {
      SA_DBG1(("siHDAMode_V:  userFwImg->ilaLen 0x%x < biggest 0x%x\n",userFwImg->ilaLen,biggest));
    }

    si_memset(&hdacmd,0,sizeof(spcv_hda_cmd_t));
    si_memset(&hdarsp,0,sizeof(spcv_hda_rsp_t));

    hda_status = ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28);
    if((hda_status  & SPC_V_HDAR_RSPCODE_MASK)  == SPC_V_HDAR_IDLE)
    {

      hdacmd.cmdparm_0 = base_Lo; /* source DmaBase_l*/
      hdacmd.cmdparm_1 = base_Hi; /* source DmaBase_u*/
      hdacmd.cmdparm_2 = 0x1e200000; /* destin */
      hdacmd.cmdparm_3 = 0; /* destin */
      hdacmd.cmdparm_4 = userFwImg->ilaLen ; /* length */
      hdacmd.cmdparm_5 = 0;/* not used */
      hdacmd.cmdparm_6 = 0;/* not used */
      seq_id++;
      hdacmd.C_PA_SEQ_ID_CMD_CODE = ( SPC_V_HDAC_PA << SHIFT24 ) | ( seq_id << SHIFT16 )| SPC_V_HDAC_DMA;

      SA_DBG1(("siHDAMode_V:          Write SPC_V_HDAC_DMA                                     STEP 2\n"));
      /*
      Step 2
      The host writes the HDAC_DMA (0x000 24) in the command field CMD_CODE via MEMBASE-I
      for issuing the DMA command to ask the boot ROM to pull the ILA image via DMA into
      GSM with the following parameters set up first:
      Parameter 1:0: Host physical address for holding the HDA-ILA image.
      Parameter 3:2: GSM physical address 0x1E20_0000.
      Parameter 4: the length of the HDAILA  image.
      */

      SA_DBG2(("siHDAMode_V: Write ILA to offset %X\n",hdacmd.cmdparm_2));

      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+0,hdacmd.cmdparm_0);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+4,hdacmd.cmdparm_1);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+8,hdacmd.cmdparm_2);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+12,hdacmd.cmdparm_3);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+16,hdacmd.cmdparm_4);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+20,hdacmd.cmdparm_5);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+24,hdacmd.cmdparm_6);
      ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+28,hdacmd.C_PA_SEQ_ID_CMD_CODE);

      SA_DBG2(("siHDAMode_V:  Command 0 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+0),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+4),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+8),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+12),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+16),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+20),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+24),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+28) ));

      SA_DBG2(("siHDAMode_V: command %X\n",hdacmd.C_PA_SEQ_ID_CMD_CODE ));

      max_wait_time = (2000 * 1000); /* wait 2 seconds */
      max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
      hda_command_complete = 0;
      do
      {
        ossaStallThread(agRoot, WAIT_INCREMENT);
        hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_SEQID_MASK ) >> SHIFT16) == seq_id;
      } while (!hda_command_complete && (max_wait_count -= WAIT_INCREMENT));
      SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x STEP 2 took %d\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

      smTrace(hpDBG_VERY_LOUD,"2Y",(max_wait_time -  max_wait_count) );
      /* TP:2Y STEP 2 took */


      if(! hda_command_complete)
      {
        SA_DBG1(("siHDAMode_V:2SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
        SA_DBG1(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
        SA_DBG1(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
        SA_DBG1(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
        SA_DBG1(("siHDAMode_V:hda_command_complete failed Step 2\n" ));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2W");
        return returnVal;
      }


      SA_DBG2(("siHDAMode_V:2SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
      SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
      SA_DBG2(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
      SA_DBG2(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));

    }

    SA_DBG1(("siHDAMode_V: ILA DMA done\n" ));
  } /* end ila   */

  if(HDA_STEP_3)
  {

    SA_DBG1(("siHDAMode_V:                                                                   STEP 3\n"));
    /*
      Step 3
      The host polls the HDA response field RSP_CODE for HDAR_IDLE (0x8002) via MEMBASE-I. The polling timeout
      should be no more than 1 second. The response status, HDAR_IDLE with its status equal to 0x10,
      indicates a DMA success response from the boot ROM. Response states that indicate a failure are:
      HDAR_BAD_CMD HDAR_BAD_IMG HDAR_IDLE with its status equal to 0x11

    */

    max_wait_time = (2000 * 1000); /* wait 2 seconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    hda_command_complete = 0;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_SEQID_MASK ) >> SHIFT16) == seq_id;
    } while (!hda_command_complete && (max_wait_count -= WAIT_INCREMENT));

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x STEP 3 took %d\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));
    smTrace(hpDBG_VERY_LOUD,"2Z",(max_wait_time -  max_wait_count) );
    /* TP:2Z STEP 3 took */

    if(! hda_command_complete)
    {

      SA_DBG1(("siHDAMode_V: Response 0 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+0),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+4),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+8),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+12),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+16),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+20),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+24),
                          ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) ));


      SA_DBG1(("siHDAMode_V:3SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
      SA_DBG1(("siHDAMode_V:hda_command_complete failed Step 3\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "2W");
      return returnVal;
    }


    hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_SEQID_MASK ) >> SHIFT16) == seq_id;
    hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_RSPCODE_MASK );

    SA_DBG2(("siHDAMode_V:ILA is ready hda_status %X hda_command_complete %d\n",hda_status ,hda_command_complete));

    /* Tell FW ILA is ready */
    SA_DBG2(("siHDAMode_V: Response 0 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+0),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+4),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+8),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+12),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+16),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+20),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+24),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) ));

    SA_DBG2(("siHDAMode_V:3SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));

    SA_DBG2(("siHDAMode_V: Step 3 MSGU_HOST_SCRATCH_PAD_3 write %X\n",HDA_ISTR_DONE));
    ossaHwRegWriteExt(agRoot, PCIBAR0,MSGU_HOST_SCRATCH_PAD_3 ,HDA_ISTR_DONE );

    SA_DBG2(("siHDAMode_V:3SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));

  }

  if(HDA_STEP_4)
  {

    SA_DBG2(("siHDAMode_V: Exec ILA\n"));
    si_memset(&hdacmd,0,sizeof(spcv_hda_cmd_t));
    si_memset(&hdarsp,0,sizeof(spcv_hda_rsp_t));

    hdacmd.cmdparm_0 = 0x200000; /* length  SPC_V_HDAC_EXEC*/;
    hdacmd.cmdparm_1 = userFwImg->ilaLen ; /* length  SPC_V_HDAC_EXEC*/;
    seq_id++;

    hdacmd.C_PA_SEQ_ID_CMD_CODE = ( SPC_V_HDAC_PA << SHIFT24 ) | ( seq_id << SHIFT16 )| SPC_V_HDAC_EXEC;

    SA_DBG1(("siHDAMode_V:                                                                   STEP 4\n"));

    /*
    Step 4
    The host writes the HDAC_EXEC command (0x0002) via MEMBASE-I for the boot ROM to authenticate
    and execute the HDA-ILA image. The host sets parameter 0 and parameter 1 for the HDA-ILA image
    appropriately:
    Parameter 0: Entry offset this value must be 0x20_0000.
    Parameter 1: the HDA-ILA image length.
    */

    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+0 ,hdacmd.cmdparm_0);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+4 ,hdacmd.cmdparm_1);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+8 ,hdacmd.cmdparm_2);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+12,hdacmd.cmdparm_3);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+16,hdacmd.cmdparm_4);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+20,hdacmd.cmdparm_5);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+24,hdacmd.cmdparm_6);
    ossaHwRegWriteExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+28,hdacmd.C_PA_SEQ_ID_CMD_CODE);

    SA_DBG1(("siHDAMode_V: Exec ILA\n" ));

    SA_DBG2(("siHDAMode_V:  Command 0 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+0),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+4),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+8),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+12),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+16),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+20),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+24),
                        ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_COMMAND_OFFSET+28) ));

    SA_DBG2(("siHDAMode_V: command %X\n",hdacmd.C_PA_SEQ_ID_CMD_CODE ));

    SA_DBG2(("siHDAMode_V:4SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
  } // End Step 4
  if(HDA_STEP_5)
  {
    SA_DBG1(("siHDAMode_V:                                             start wait            STEP 5\n"));

    /*
      Step 5
      The host continues polling for the HDA-ILA status via MEMBASE-I. The polling timeout should
      be no more than 1 second. The response status HDAR_EXEC indicates a good response from the
      boot ROM. Response states that indicate a failure are:
      HDAR_BAD_CMD
      HDAR_BAD_IMG
    */

    max_wait_time = (2000 * 1000); /* wait 2 seconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    hda_command_complete = 0;
    hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_RSPCODE_MASK );
    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x hda_status 0x%x Begin STEP 5\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),hda_status));
    hda_status = 0;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_RSPCODE_MASK );
      hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) & SPC_V_HDAR_SEQID_MASK ) >> SHIFT16) == seq_id;
    } while (hda_status != SPC_V_HDAR_EXEC && (max_wait_count -= WAIT_INCREMENT));

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x hda_status 0x%x hda_command_complete 0x%x STEP 5 wait for seq_id took %d\n",
               ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),
               hda_status,
               hda_command_complete,
               (max_wait_time -  max_wait_count)));

    smTrace(hpDBG_VERY_LOUD,"2Z",(max_wait_time -  max_wait_count) );
    /* TP:2Z STEP 5 took */

    if(! hda_command_complete)
    {
        SA_DBG1(("siHDAMode_V: Response 0 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+0),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+4),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+8),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+12),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+16),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+20),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+24),
                            ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) ));

      SA_DBG1(("siHDAMode_V:5SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
      SA_DBG1(("siHDAMode_V:hda_command_complete failed Step 5\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "2W");
      return returnVal;
    }

    if (hda_status != SPC_V_HDAR_EXEC)
    {
      SA_DBG1(("siHDAMode_V:ILA_EXEC_ERROR hda_status %X hda_command_complete %d\n",hda_status ,hda_command_complete));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "2W");
      goto bootrom_err;
    }
    SA_DBG1(("siHDAMode_V:           end    seq_id updated                                   STEP 5\n"));
  } // End Step 5

  if(HDA_STEP_6)
  {
    SA_DBG1(("siHDAMode_V:  start                                                            STEP 6\n"));

    /*
      Step 6
      The host polls the upper 8 bits [31:24] 5 of the Scratchpad 0 Register
      (page 609) for the ILAHDA_RAAE_IMG_GET (0x11) state. Polling timeout
      should be no more than 2 seconds. If a polling timeout occurs, the host
      should check for a fatal error as described in Section 12.2.
      If successful, the Host Scratchpad 4 Register (page 620) and Host
      Scratchpad 5 Register (page 621) are set as follows: Host Scratchpad 4
      Register (page 620) holds the lower 32-bit host address of
      the RAAE image. Host Scratchpad 5 Register (page 621)
      holds the upper 32-bit host address of the RAAE image.
      Then the host writes the command ILAHDAC_RAAE_IMG_DONE(0x81) to the upper
      8 bits [31:24] of the Host Scratchpad 3 Register (page 619) and writes the
      sizeof the RAAE image to the lower 24 bits [23:0].
    */

    max_wait_time = (2000 * 1000); /* wait 2 seconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    hda_command_complete = 0;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register) & 0xff000000 ) >> SHIFT24 ) == ILAHDA_RAAE_IMG_GET;
    } while (!hda_command_complete && (max_wait_count -= WAIT_INCREMENT));

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD0 = 0x%x STEP 6 wait for ILAHDA_RAAE_IMG_GET took %d\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register),(max_wait_time -  max_wait_count)));
    smTrace(hpDBG_VERY_LOUD,"2b",(max_wait_time -  max_wait_count) );
    /* TP:2b STEP 6 took */
    if(! hda_command_complete)
    {
      SA_DBG1(("siHDAMode_V:hda_command_complete failed Step 6\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "2W");
      goto fw_err;
    }

    si_memset(pbase, 0, biggest);

    if( userFwImg->aap1Len < biggest)
    {
      si_memcpy(pbase,userFwImg->aap1Img, userFwImg->aap1Len );
    }
    else
    {
      SA_DBG1(("siHDAMode_V:  userFwImg->aap1Len 0x%x < biggest 0x%x\n",userFwImg->aap1Len,biggest));
    }
    /*
    */
    /* upper */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_5, base_Hi );
    SA_DBG3(("siHDAMode_V: MSGU_HOST_SCRATCH_PAD_5 0x%X\n", base_Hi));
    /* lower */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_4, base_Lo );
    SA_DBG3(("siHDAMode_V: MSGU_HOST_SCRATCH_PAD_4 0x%X\n",base_Lo));
    /* len */
    ossaHwRegWriteExt(agRoot, PCIBAR0,MSGU_HOST_SCRATCH_PAD_3 ,(ILAHDAC_RAAE_IMG_DONE << SHIFT24) | userFwImg->aap1Len );
    SA_DBG1(("siHDAMode_V: write ILAHDAC_RAAE_IMG_DONE to MSGU_HOST_SCRATCH_PAD_3 0x%X\n",(ILAHDAC_RAAE_IMG_DONE << SHIFT24) | userFwImg->aap1Len));
    //    ossaHwRegWriteExt(agRoot, PCIBAR0,MSGU_HOST_SCRATCH_PAD_4 , userFwImg->DmaBase_l);

    ossaStallThread(agRoot, gWaitmSec * 1000);
    if(1) /* step in question */
    {
      max_wait_time = (2000 * 1000); /* wait 2 seconds */
      max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
      hda_command_complete = 0;
      do
      {
        ossaStallThread(agRoot, WAIT_INCREMENT);
        hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register) & 0xff000000 ) >> SHIFT24 ) == ILAHDA_IOP_IMG_GET;
      } while (!hda_command_complete && (max_wait_count -= WAIT_INCREMENT));

      SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x STEP 7 wait for ILAHDA_IOP_IMG_GET took %d\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));
      smTrace(hpDBG_VERY_LOUD,"2c",(max_wait_time -  max_wait_count) );
      /* TP:2c STEP 6a ILAHDA_IOP_IMG_GET took */
      smTrace(hpDBG_VERY_LOUD,"2y",hda_command_complete );
      /* TP:2y hda_command_complete */

      if(! hda_command_complete)
      {
        SA_DBG1(("siHDAMode_V:hda_command_complete failed Step 7\n" ));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "2W");
        goto fw_err;
      }
    }
    SA_DBG1(("siHDAMode_V:  End                  V_Scratchpad_0_Register 0x%08X          STEP 6\n",ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register)));
  }

  if(HDA_STEP_7)
  {
    SA_DBG1(("siHDAMode_V:                                                                   STEP 7\n"));
    /*
      Step 7
      The host polls (reads) the upper 8 bits 7 [31:24] of the Scratchpad 0 Register (page 609)
      for ILAHDA_IOP_IMG_GET (0x10) state. The polling timeout should be no more than 2 seconds.
      If a polling timeout occurs, the host should check for a fatal error as described in
      Section 12.2. If successful, the Host Scratchpad 4 Register (page 620) and Host
      Scratchpad 5 Register (page 621) are set as follows:
      Host Scratchpad 4 Register (page 620) holds the lower host address of the IOP image.
      Host Scratchpad 5 Register (page 621) holds the upper host address of the IOP image.
      Then host writes the command ILAHDAC_IOP_IMG_DONE(0x80) to the upper 8 bits [31:24] of the
      Host Scratchpad 3 Register  (page 614)and writes the sizeof the IOP image to the lower 24
      bits [23:0].

    */

    si_memset(pbase, 0, biggest);

    if( userFwImg->iopLen < biggest)
    {
      si_memcpy(pbase,userFwImg->iopImg, userFwImg->iopLen );
    }
    else
    {
      SA_DBG1(("siHDAMode_V:  userFwImg->iopImg 0x%x < biggest 0x%x\n",userFwImg->iopLen,biggest));
    }

    /* upper */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_5, base_Hi );
    SA_DBG3(("siHDAMode_V: MSGU_HOST_SCRATCH_PAD_5 0x%X\n", base_Hi));
    /* lower */
    ossaHwRegWrite(agRoot, MSGU_HOST_SCRATCH_PAD_4, base_Lo );
    SA_DBG3(("siHDAMode_V: MSGU_HOST_SCRATCH_PAD_4 0x%X\n",base_Lo));
    SA_DBG2(("siHDAMode_V: MSGU_HOST_SCRATCH_PAD_4\n"));
    /* len */
    ossaHwRegWriteExt(agRoot, PCIBAR0,MSGU_HOST_SCRATCH_PAD_3 ,(ILAHDAC_IOP_IMG_DONE << SHIFT24) | userFwImg->iopLen );
    SA_DBG2(("siHDAMode_V: MSGU_HOST_SCRATCH_PAD_3 0x%X\n",(ILAHDAC_IOP_IMG_DONE << SHIFT24) | userFwImg->iopLen));


    if(saRoot->swConfig.hostDirectAccessMode & 2 )
    {
  /* Hda AES DIF offload */
    ossaHwRegWrite(agRoot, V_Scratchpad_Rsvd_0_Register, HDA_AES_DIF_FUNC);
    SA_DBG1(("siHDAMode_V: V_Scratchpad_Rsvd_0_Register, HDA_AES_DIF_FUNC 0x%X\n",HDA_AES_DIF_FUNC));
  /* Hda AES DIF offload */
    }

    SA_DBG2(("siHDAMode_V: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));


    max_wait_time = (2000 * 1000); /* wait 2 seconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    hda_command_complete = 0;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      hda_command_complete = ((ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register) & 0xff000000 ) >> SHIFT24 ) == ILAHDA_IOP_IMG_GET;
    } while (!hda_command_complete && (max_wait_count -= WAIT_INCREMENT));

    smTrace(hpDBG_VERY_LOUD,"2d",(max_wait_time -  max_wait_count) );
    /* TP:2d STEP 7 ILAHDA_IOP_IMG_GET took */
    smTrace(hpDBG_VERY_LOUD,"2z",hda_command_complete );
    /* TP:2z hda_command_complete */

    SA_DBG2(("siHDAMode_V:SCRATCH_PAD0 = 0x%x STEP 7 wait for ILAHDA_IOP_IMG_GET took %d\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register),(max_wait_time -  max_wait_count)));

    if(! hda_command_complete)
    {
      SA_DBG1(("siHDAMode_V:7SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
      SA_DBG1(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
      SA_DBG1(("siHDAMode_V:hda_command_complete failed Step 7\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "2W");
      return returnVal;
    }


    SA_DBG2(("siHDAMode_V:7SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
    SA_DBG2(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
    SA_DBG1(("siHDAMode_V:  End                    STEP 7\n"));
  }


  if(HDA_STEP_8)
  {
    bit32  SCRATCH_PAD1;

    SA_DBG1(("siHDAMode_V:     Check fw ready                                                Step 8\n"));

    /*
    Step 8
    IOP0/1 start-up sequence. The host polls the Scratchpad 1 Register (page 610)
    bits [1:0] for RAAE_STATE, bits [13:12] for IOP1_STATE, and
    bits [11:10] for IOP0_STATE to go to 11b (Ready state).
    The polling timeout should be no more than 1 second. If a polling timeout occurs,
    the host should check for a fatal error in Section 12.2.
    */

    returnVal = AGSA_RC_SUCCESS;

    max_wait_time = (1000 * 1000); /* wait 1000 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while ((SCRATCH_PAD1  == 0xFFFFFFFF  ) && (max_wait_count -= WAIT_INCREMENT));
    smTrace(hpDBG_VERY_LOUD,"HZ",(max_wait_time -  max_wait_count) );
    /* TP:2f Step 8 PCI took */

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x (0x%x) Step 8 PCIe took %d\n", SCRATCH_PAD1,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));
    /* ILA */
    max_wait_time = (1000 * 1000); /* wait 1000 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_ILA_MASK) != SCRATCH_PAD1_V_ILA_MASK) && (max_wait_count -= WAIT_INCREMENT));

    smTrace(hpDBG_VERY_LOUD,"2g",(max_wait_time -  max_wait_count) );
    /* TP:2g Step 8 ILA took */

    SA_DBG2(("siHDAMode_V:SCRATCH_PAD1 = 0x%x SCRATCH_PAD1_V_ILA_MASK (0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_ILA_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

    if (!max_wait_count)
    {
      // Ignore for now returnVal = AGSA_RC_FAILURE;
      SA_DBG1(("siHDAMode_V:Timeout SCRATCH_PAD1_V_ILA_MASK (0x%x)  not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_ILA_MASK, SCRATCH_PAD1));
    }

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x SCRATCH_PAD1_V_ILA_MASK (0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_ILA_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));

    /* RAAE */
    max_wait_time = (1800 * 1000); /* wait 1800 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_RAAE_MASK) != SCRATCH_PAD1_V_RAAE_MASK) && (max_wait_count -= WAIT_INCREMENT));

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x SCRATCH_PAD1_V_RAAE_MASK (0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_RAAE_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));
    smTrace(hpDBG_VERY_LOUD,"2h",(max_wait_time -  max_wait_count) );
    /* TP:2h Step 8 RAAE took */

    if (!max_wait_count)
    {
      SA_DBG1(("siHDAMode_V:Timeout SCRATCH_PAD1_V_RAAE_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_RAAE_MASK, SCRATCH_PAD1));

    }
    /* IOP0 */
    max_wait_time = (600 * 1000); /* wait 600 milliseconds */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
    } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP0_MASK) != SCRATCH_PAD1_V_IOP0_MASK) && (max_wait_count -= WAIT_INCREMENT));

    SA_DBG1(("siHDAMode_V:SCRATCH_PAD1 = 0x%x  SCRATCH_PAD1_V_IOP0_MASK(0x%x)(0x%x) took %d\n", SCRATCH_PAD1,SCRATCH_PAD1_V_IOP0_MASK,ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1),(max_wait_time -  max_wait_count)));
    smTrace(hpDBG_VERY_LOUD,"2i",(max_wait_time -  max_wait_count) );
    /* TP:2i Step 8 IOP took */

    if (!max_wait_count)
    {
      returnVal = AGSA_RC_FAILURE;
      SA_DBG1(("siHDAMode_V:Timeout SCRATCH_PAD1_V_IOP0_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_IOP0_MASK ,SCRATCH_PAD1));

    }


  SA_DBG1(("siHDAMode_V: Step 8 0x%X ERROR_STATE 0x%X\n",ossaHwRegReadExt(agRoot,PCIBAR0 ,V_SoftResetRegister ),
  SCRATCH_PAD1_V_ERROR_STATE( ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1) ) ));
  if (SCRATCH_PAD1_V_ERROR_STATE( ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1) )) 
  {
      if(smIS_ENCRYPT(agRoot))
      {
        SA_DBG1(("siHDAMode_V: Encryption and HDA mode not supported - failed Step 8\n" ));
      }
      else
      {
         SA_DBG1(("siHDAMode_V: ERROR_STATE failed Step 8\n" ));
      }
      returnVal = AGSA_RC_FAILURE;
      smTraceFuncExit(hpDBG_VERY_LOUD, 'l', "2W");
      goto fw_err;
  }

  }
  SA_DBG1(("siHDAMode_V:                      returnVal  0x%X                               Step 8\n",returnVal));
/*
Step 10
The host continues with the normal SPCv Configuration Table initialization sequence
as described in Section 6.2.8.1.
*/
  if(saRoot->swConfig.hostDirectAccessMode & 2 )
  {
    /* Hda AES DIF offload */
    SA_DBG1(("siHDAMode_V: AES/DIF 0x%08X offload enabled %s\n",ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3 ),
                           ((ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3 ) & (1 << SHIFT15)) ? "yes" :"no") ));
    /* Hda AES DIF offload */
    /* ossaHwRegWrite(agRoot, V_Scratchpad_Rsvd_0_Register, 0); */
    /* Hda AES DIF offload */
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'm', "2W");
  return returnVal;

bootrom_err:
  SA_DBG2(("siHDAMode_V: Response 0 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+0),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+4),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+8),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+12),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+16),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+20),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+24),
      ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28) ));

fw_err:
  SA_DBG2(("siHDAMode_V: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_0_Register)));
  SA_DBG2(("siHDAMode_V: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_1_Register)));
  SA_DBG2(("siHDAMode_V: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_2_Register)));
  SA_DBG2(("siHDAMode_V: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, V_Scratchpad_3_Register)));
  return returnVal;
}

#endif /* SA_ENABLE_HDA_FUNCTIONS */




/******************************************************************************/
/*! \brief Function to check FW is ready for soft reset
 *
 *  The siSpcSoftResetRDYChk() function is called to check status of FW
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *
 *  \return success or fail
 */
/*******************************************************************************/
LOCAL bit32 siSpcSoftResetRDYChk(agsaRoot_t *agRoot)
{
  bit32 regVal;
  bit32 Scratchpad1;
  bit32 Scratchpad2;
  bit32 spad2notready = 0;
#if defined(SALLSDK_DEBUG)
  bit32 regVal1;
  bit32 regVal2;
#endif /* SALLSDK_DEBUG */

  /* read the scratch pad 2 register bit 2 */
  regVal = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2) & SCRATCH_PAD2_FWRDY_RST;
  Scratchpad1 =  ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
  if (regVal == SCRATCH_PAD2_FWRDY_RST)
  {
      /* FW assert happened, it is ready for soft reset */
      /* Do nothing */
  }
  else
  {
    /* read bootloader response state */
    regVal = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;
    if (regVal == BOOTTLOADERHDA_IDLE)
    {
     /* For customers wants to do soft reset even the chip is already in HDA mode */
     /* Do not need to trigger RB6 twice */
     ;
    }
    else
    {
      /* Trigger NMI twice via RB6 */
      if (AGSA_RC_FAILURE == siBar4Shift(agRoot, RB6_ACCESS_REG))
      {
        SA_DBG1(("siSpcSoftReset:Shift Bar4 to 0x%x failed\n", RB6_ACCESS_REG));
        return AGSA_RC_FAILURE;
      }

      if(Scratchpad1  != (SCRATCH_PAD1_FW_INIT_ERR | SCRATCH_PAD1_AAP_ERROR_STATE))
      {
        ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_RB6_OFFSET , RB6_MAGIC_NUMBER_RST);

        ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_RB6_OFFSET , RB6_MAGIC_NUMBER_RST);
      }
      else
      {
        SA_DBG1(("siSoftReset: ILA load fail SKIP RB6 access 0x%x\n",Scratchpad1 ));
      }
      SPAD2_NOT_READY:
      /* wait for 100 ms */
      ossaStallThread(agRoot, ONE_HUNDRED_MILLISECS  );
      Scratchpad2 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);
      regVal = Scratchpad2 & SCRATCH_PAD2_FWRDY_RST;
      if (regVal != SCRATCH_PAD2_FWRDY_RST)
      {
        if (spad2notready > WAIT_SECONDS(12) / ONE_HUNDRED_MILLISECS ) /**/
        {
#if defined(SALLSDK_DEBUG)
          regVal1 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
          regVal2 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);
          SA_DBG1(("siSpcSoftResetRDYChk: TIMEOUT:MSGU_SCRATCH_PAD1=0x%x, MSGU_SCRATCH_PAD2=0x%x\n", regVal1, regVal2));
          SA_DBG1(("siSpcSoftResetRDYChk: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0)));
          SA_DBG1(("siSpcSoftResetRDYChk: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3)));
#endif /* SALLSDK_DEBUG */
          return AGSA_RC_SUCCESS; /* Timeout Ok reset anyway */
        }

        spad2notready++;
        goto SPAD2_NOT_READY;
      }
    }
  }

  return AGSA_RC_SUCCESS;
}


agsaBarOffset_t SPCTable[] =
{

  { GEN_MSGU_IBDB_SET,                 PCIBAR0, MSGU_IBDB_SET,                   SIZE_DW }, /* 0x00  */
  { GEN_MSGU_ODR,                      PCIBAR0, MSGU_ODR,                        SIZE_DW }, /* 0x01  */
  { GEN_MSGU_ODCR,                     PCIBAR0, MSGU_ODCR,                       SIZE_DW }, /* 0x02  */
  { GEN_MSGU_SCRATCH_PAD_0,            PCIBAR0, MSGU_SCRATCH_PAD_0,              SIZE_DW }, /* 0x03  */
  { GEN_MSGU_SCRATCH_PAD_1,            PCIBAR0, MSGU_SCRATCH_PAD_1,              SIZE_DW }, /* 0x04  */
  { GEN_MSGU_SCRATCH_PAD_2,            PCIBAR0, MSGU_SCRATCH_PAD_2,              SIZE_DW }, /* 0x05  */
  { GEN_MSGU_SCRATCH_PAD_3,            PCIBAR0, MSGU_SCRATCH_PAD_3,              SIZE_DW }, /* 0x06  */
  { GEN_MSGU_HOST_SCRATCH_PAD_0,       PCIBAR0, MSGU_HOST_SCRATCH_PAD_0,         SIZE_DW }, /* 0x07  */
  { GEN_MSGU_HOST_SCRATCH_PAD_1,       PCIBAR0, MSGU_HOST_SCRATCH_PAD_1,         SIZE_DW }, /* 0x08  */
  { GEN_MSGU_HOST_SCRATCH_PAD_2,       PCIBAR0, MSGU_HOST_SCRATCH_PAD_2,         SIZE_DW }, /* 0x09  */
  { GEN_MSGU_HOST_SCRATCH_PAD_3,       PCIBAR0, MSGU_HOST_SCRATCH_PAD_3,         SIZE_DW }, /* 0x0a  */
  { GEN_MSGU_ODMR,                     PCIBAR0, MSGU_ODMR,                       SIZE_DW }, /* 0x0b  */
  { GEN_PCIE_TRIGGER,                  PCIBAR0, PCIE_TRIGGER_ON_REGISTER_READ,   SIZE_DW }, /* 0x0c  */
  { GEN_SPC_REG_RESET,                 PCIBAR2, SPC_REG_RESET,                   SIZE_DW }, /* 0x0d  */
};

agsaBarOffset_t SPC_V_Table[] =
{

  { GEN_MSGU_IBDB_SET,                 PCIBAR0, V_Inbound_Doorbell_Set_Register,       SIZE_DW }, /* 0x00  */
  { GEN_MSGU_ODR,                      PCIBAR0, V_Outbound_Doorbell_Set_Register,      SIZE_DW }, /* 0x01  */
  { GEN_MSGU_ODCR,                     PCIBAR0, V_Outbound_Doorbell_Clear_Register,    SIZE_DW }, /* 0x02  */
  { GEN_MSGU_SCRATCH_PAD_0,            PCIBAR0, V_Scratchpad_0_Register,               SIZE_DW }, /* 0x03  */
  { GEN_MSGU_SCRATCH_PAD_1,            PCIBAR0, V_Scratchpad_1_Register,               SIZE_DW }, /* 0x04  */
  { GEN_MSGU_SCRATCH_PAD_2,            PCIBAR0, V_Scratchpad_2_Register,               SIZE_DW }, /* 0x05  */
  { GEN_MSGU_SCRATCH_PAD_3,            PCIBAR0, V_Scratchpad_3_Register,               SIZE_DW }, /* 0x06  */
  { GEN_MSGU_HOST_SCRATCH_PAD_0,       PCIBAR0, V_Host_Scratchpad_0_Register,          SIZE_DW }, /* 0x07  */
  { GEN_MSGU_HOST_SCRATCH_PAD_1,       PCIBAR0, V_Host_Scratchpad_1_Register,          SIZE_DW }, /* 0x08  */
  { GEN_MSGU_HOST_SCRATCH_PAD_2,       PCIBAR0, V_Host_Scratchpad_2_Register,          SIZE_DW }, /* 0x09  */
  { GEN_MSGU_HOST_SCRATCH_PAD_3,       PCIBAR0, V_Host_Scratchpad_3_Register,          SIZE_DW }, /* 0x0a  */
  { GEN_MSGU_ODMR,                     PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register, SIZE_DW }, /* 0x0b  */
  { GEN_PCIE_TRIGGER,                  PCIBAR0, PCIE_TRIGGER_ON_REGISTER_READ,         SIZE_DW }, /* 0x0c  */
  { GEN_SPC_REG_RESET,                 PCIBAR0, V_SoftResetRegister,                   SIZE_DW }, /* 0x0d  */
};


/*******************************************************************************/
/**
 *
 *  \brief
 *  \param agsaRoot         Pointer to a data structure containing both application
 *                          and LL layer context handles
 *  \param Spc_type         Device  Id of hardware
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void siUpdateBarOffsetTable(agsaRoot_t     *agRoot,
                                   bit32         Spc_Type
 )
{

  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  bit32 x;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"mf");

  smTrace(hpDBG_VERY_LOUD,"9A",Spc_Type);
  /* TP:9A Spc_Type */

  if(Spc_Type == VEN_DEV_SPC)
  {
    si_memcpy(&saRoot->SpcBarOffset, SPCTable, sizeof(SPCTable));
    SA_DBG5(("siUpdateBarOffsetTable:sizeof(SPCTable) sizeof(agsaBarOffset_t)sizeof(SPCTable) / sizeof(agsaBarOffset_t) %X %X %X\n",
        (unsigned int)sizeof(SPCTable), (unsigned int)sizeof(agsaBarOffset_t),
        (unsigned int)(sizeof(SPCTable) / sizeof(agsaBarOffset_t))
      ));
  }
  else /* VEN_DEV_SPCV */
  {
    si_memcpy(&saRoot->SpcBarOffset, SPC_V_Table, sizeof(SPC_V_Table));
    SA_DBG5(("siUpdateBarOffsetTable:sizeof(SPC_V_Table) sizeof(agsaBarOffset_t)sizeof(SPC_V_Table) / sizeof(agsaBarOffset_t) %X %X %X\n",
        (unsigned int)sizeof(SPC_V_Table),
        (unsigned int)sizeof(agsaBarOffset_t),
        (unsigned int)(sizeof(SPC_V_Table) / sizeof(agsaBarOffset_t))
      ));
  }

  for(x=0;x < sizeof(SPCTable) / sizeof(agsaBarOffset_t);x++)
  {

    SA_DBG4(("%8X: %8X %8X %8X\n",saRoot->SpcBarOffset[x].Generic,
                                  saRoot->SpcBarOffset[x].Bar,
                                  saRoot->SpcBarOffset[x].Offset,
                                  saRoot->SpcBarOffset[x].Length
                                         ));
    if(saRoot->SpcBarOffset[x].Generic != x)
    {
      SA_DBG1(("siUpdateBarOffsetTable:  saRoot->SpcBarOffset[%x].Generic %X != %X\n",x, saRoot->SpcBarOffset[x].Generic, x));
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "mf");
}



GLOBAL bit32 siHalRegReadExt( agsaRoot_t  *agRoot,
                             bit32       generic,
                             bit32       regOffset
                             )
{

  agsaBarOffset_t * Table = agNULL;
  bit32 retVal;

  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "agRoot");
  Table = WHATTABLE(agRoot);
  SA_ASSERT( (agNULL != Table), "Table");

/*
  if(Table[generic].Offset != regOffset)
  {

    SA_DBG1(("siHalRegReadExt: Table[%x].Offset %x != regOffset %x\n",generic,
                                        Table[generic].Offset,
                                        regOffset ));
  }
*/

  if(Table[generic].Bar)
  {
    retVal  = ossaHwRegReadExt(agRoot,
                Table[generic].Bar,
                Table[generic].Offset);
  }
  else
  {
    retVal  = ossaHwRegRead(agRoot,
                Table[generic].Offset);
  }

  return(retVal);
}


GLOBAL void siHalRegWriteExt(
                             agsaRoot_t  *agRoot,
                             bit32       generic,
                             bit32       regOffset,
                             bit32       regValue
                             )
{
  agsaBarOffset_t * Table = agNULL;

  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "agRoot");

  Table = WHATTABLE(agRoot);
  SA_ASSERT( (agNULL != Table), "Table");


/*
    if(Table[generic].Offset != regOffset)
    {

      SA_DBG1(("siHalRegWriteExt: Table[%x].Offset %x != regOffset %x\n",generic,
                                          Table[generic].Offset,
                                          regOffset ));
    }
*/

    SA_DBG6(("siHalRegWriteExt: Bar %x Offset %8X Wrote %8X\n",
                                        Table[generic].Bar,
                                        Table[generic].Offset,
                                        regValue ));


  if(Table[generic].Bar)
  {
    ossaHwRegWriteExt(agRoot,
              Table[generic].Bar,
              Table[generic].Offset,
              regValue  );
  }else
  {
    ossaHwRegWrite(agRoot,
            Table[generic].Offset,
            regValue  );
  }
}




GLOBAL void siPCITriger(agsaRoot_t *agRoot)
{

  SA_DBG1(("siPCITriger: Read PCIe Bar zero plus 0x%x\n", PCIE_TRIGGER_ON_REGISTER_READ));
  ossaHwRegReadExt(agRoot,PCIBAR0 ,PCIE_TRIGGER_ON_REGISTER_READ );
}


GLOBAL bit32 siGetPciBar(
              agsaRoot_t *agRoot
              )
{
  bit32 MSGUCfgTblBase = 0;
  bit32 pcibar = 0;
  MSGUCfgTblBase = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
  pcibar = (MSGUCfgTblBase & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* get pci Bar index */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, pcibar);

  return(pcibar);
}

GLOBAL bit32 siGetTableOffset(
              agsaRoot_t *agRoot,
              bit32  TableOffsetInTable
              )
{
  bit32 TableOffset;
  bit32 MSGUCfgTblBase;
  /* read scratch pad0 to get PCI BAR and offset of configuration table */
  MSGUCfgTblBase = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  MSGUCfgTblBase &= SCRATCH_PAD0_OFFSET_MASK;

  TableOffset = ossaHwRegReadExt(agRoot,siGetPciBar(agRoot) ,MSGUCfgTblBase +TableOffsetInTable  );
  SA_DBG4(("GetTableOffset:TableOffset with size 0x%x\n", TableOffset));

  /* Mask off size */
  TableOffset &= 0xFFFFFF;
  TableOffset +=MSGUCfgTblBase;
  return(TableOffset);

}


GLOBAL void siCheckQs(
              agsaRoot_t *agRoot
              )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);

  mpiOCQueue_t         *circularOQ;
  mpiICQueue_t         *circularIQ;
  int i;

  for ( i = 0; i < saRoot->QueueConfig.numInboundQueues; i++ )
  {
    circularIQ = &saRoot->inboundQueue[i];

    OSSA_READ_LE_32(circularIQ->agRoot, &circularIQ->consumerIdx, circularIQ->ciPointer, 0);
    if(circularIQ->producerIdx != circularIQ->consumerIdx)
    {
      SA_DBG1(("siCheckQs: In  Q %d  PI 0x%03x CI 0x%03x (%d) \n",i,
      circularIQ->producerIdx,
      circularIQ->consumerIdx,
      (circularIQ->producerIdx > circularIQ->consumerIdx ? (circularIQ->producerIdx - circularIQ->consumerIdx) :   (circularIQ->numElements -  circularIQ->consumerIdx ) + circularIQ->producerIdx)));
    }
  }

  for ( i = 0; i < saRoot->QueueConfig.numOutboundQueues; i++ )
  {
    circularOQ = &saRoot->outboundQueue[i];
    OSSA_READ_LE_32(circularOQ->agRoot, &circularOQ->producerIdx, circularOQ->piPointer, 0);
    if(circularOQ->producerIdx != circularOQ->consumerIdx)
    {
        SA_DBG1(("siCheckQs: Out Q %d  PI 0x%03x CI 0x%03x (%d) \n",i,
        circularOQ->producerIdx,
        circularOQ->consumerIdx,
        (circularOQ->producerIdx > circularOQ->consumerIdx ? (circularOQ->producerIdx - circularOQ->consumerIdx) :   (circularOQ->numElements -  circularOQ->consumerIdx ) + circularOQ->producerIdx)));

    }
  }

}
GLOBAL void siPciCpyMem(agsaRoot_t *agRoot,
                       bit32 soffset,
                       const void *dst,
                       bit32 DWcount,
                       bit32 busBaseNumber
                       )
{
  bit32 i, val,offset;
  bit32 *dst1;

  dst1 = (bit32 *)dst;

  SA_DBG1(("siPciCpyMem:copy DWcount %d from offset 0x%x to %p\n",DWcount,soffset,dst));

  for (i= 0; i < DWcount; i+=4,dst1++)
  {
    offset = (soffset + i / 4);
    SA_ASSERT( (offset < (64 * 1024)), "siPciCpyMem offset too large");
    if(offset < (64 * 1024))
    {
      val = ossaHwRegReadExt(agRoot, busBaseNumber, offset);
      *dst1 =  BIT32_TO_LEBIT32(val);
    }
  }

  return;
}
