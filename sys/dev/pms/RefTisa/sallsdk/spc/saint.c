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
/*! \file saint.c
 *  \brief The file implements the functions to handle/enable/disable interrupt
 *
 */
/*******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#define SA_CLEAR_ODCR_IN_INTERRUPT

//#define SA_TEST_FW_SPURIOUS_INT

#ifdef SA_TEST_FW_SPURIOUS_INT
bit32 gOurIntCount = 0;
bit32 gSpuriousIntCount = 0;
bit32 gSpuriousInt[64]=
{
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0
};
bit32 gSpuriousInt1[64]=
{
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0
};
#endif /* SA_TEST_FW_SPURIOUS_INT */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif /* siTraceFileID */
#define siTraceFileID 'G'
#endif /* SA_ENABLE_TRACE_FUNCTIONS */

LOCAL FORCEINLINE bit32 siProcessOBMsg(
                           agsaRoot_t  *agRoot,
                           bit32        count,
                           bit32        queueNum
                           );

LOCAL bit32 siFatalInterruptHandler(
  agsaRoot_t  *agRoot,
  bit32       interruptVectorIndex
  )
{
  agsaLLRoot_t         *saRoot = agNULL;
  agsaFatalErrorInfo_t fatal_error;
  bit32                value;
  bit32                ret = AGSA_RC_FAILURE;
  bit32                Sendfatal = agTRUE;
  
  SA_ASSERT((agNULL != agRoot), "");
  if (agRoot == agNULL)
  {
    SA_DBG1(("siFatalInterruptHandler: agRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
  if (saRoot == agNULL)
  {
    SA_DBG1(("siFatalInterruptHandler: saRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }

  value  = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
  if (saRoot->ResetFailed)
  {
    SA_DBG1(("siFatalInterruptHandler: ResetFailed\n"));
    ossaDisableInterrupts(agRoot, interruptVectorIndex);
    return AGSA_RC_FAILURE;
  }

  if(SCRATCH_PAD1_V_ERROR_STATE( value ) )
  {
    si_memset(&fatal_error, 0, sizeof(agsaFatalErrorInfo_t));
    /* read detail fatal errors */
    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0, MSGU_SCRATCH_PAD_0);
    fatal_error.errorInfo0 = value;
    SA_DBG1(("siFatalInterruptHandler: ScratchPad0 AAP error 0x%x code 0x%x\n",SCRATCH_PAD1_V_ERROR_STATE( value ), value));

    value  = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
    fatal_error.errorInfo1 = value;
    /* AAP error state */
    SA_DBG1(("siFatalInterruptHandler: AAP error state and error code 0x%x\n", value));
    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2);
    fatal_error.errorInfo2 = value;
    SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2 0x%08x\n", fatal_error.errorInfo2 ));

#if defined(SALLSDK_DEBUG)
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_ILA_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler:SCRATCH_PAD1_V_ERROR_STATE SCRATCH_PAD2_FW_ILA_ERR 0x%08x\n", SCRATCH_PAD2_FW_ILA_ERR));
    }
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_FLM_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_FLM_ERR 0x%08x\n", SCRATCH_PAD2_FW_FLM_ERR));
    }
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_FW_ASRT_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_FW_ASRT_ERR 0x%08x\n", SCRATCH_PAD2_FW_FW_ASRT_ERR));
    }
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_WDG_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_HW_WDG_ERR 0x%08x\n", SCRATCH_PAD2_FW_HW_WDG_ERR));
    }
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_GEN_EXCEPTION_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_GEN_EXCEPTION_ERR 0x%08x\n", SCRATCH_PAD2_FW_GEN_EXCEPTION_ERR));
    }
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_UNDTMN_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_UNDTMN_ERR 0x%08x\n",SCRATCH_PAD2_FW_UNDTMN_ERR ));
    }
    if(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_FATAL_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_HW_FATAL_ERR 0x%08x\n", SCRATCH_PAD2_FW_HW_FATAL_ERR));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_PCS_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_PCS_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_GSM_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_GSM_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP0_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP0_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) ==SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP1_ERR  )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP1_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP2_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP2_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_ERAAE_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_ERAAE_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_SDS_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_SDS_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_CORE_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_CORE_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_AL_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_AL_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_MSGU_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_MSGU_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_SPBC_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_SPBC_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_BDMA_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_BDMA_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) ==  SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSL2B_ERR)
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSL2B_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSDC_ERR )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSDC_ERR 0x%08x\n", value));
    }
    if((fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_MASK) == SCRATCH_PAD2_HW_ERROR_INT_INDX_UNDETERMINED_ERROR_OCCURRED )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_HW_ERROR_INT_INDX_UNDETERMINED_ERROR_OCCURRED 0x%08x\n", value));
    }
#endif /* SALLSDK_DEBUG */

    if( fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_NON_FATAL_ERR   &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_ILA_ERR)           &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_FLM_ERR)           &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_FW_ASRT_ERR)       &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_WDG_ERR)        &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_GEN_EXCEPTION_ERR) &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_UNDTMN_ERR)        &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_PCS_ERR)       &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_GSM_ERR)       &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP0_ERR)     &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_OSSP2_ERR)     &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_ERAAE_ERR)     &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_SDS_ERR)       &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_CORE_ERR) &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_PCIE_AL_ERR)   &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_MSGU_ERR)      &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_SPBC_ERR)      &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_BDMA_ERR)      &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSL2B_ERR)   &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_MCPSDC_ERR)    &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_HW_ERROR_INT_INDX_UNDETERMINED_ERROR_OCCURRED) &&
      !(fatal_error.errorInfo2 & SCRATCH_PAD2_FW_HW_FATAL_ERR) )
    {
      SA_DBG1(("siFatalInterruptHandler: SCRATCH_PAD2_FW_HW_NON_FATAL_ERR 0x%08x\n", value));
      Sendfatal = agFALSE;
    }

    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3, MSGU_SCRATCH_PAD_3);
    SA_DBG1(("siFatalInterruptHandler: ScratchPad3 IOP error code 0x%08x\n", value));
    fatal_error.errorInfo3 = value;

    if (agNULL != saRoot)
    {
      fatal_error.regDumpBusBaseNum0 = saRoot->mainConfigTable.regDumpPCIBAR;
      fatal_error.regDumpOffset0 = saRoot->mainConfigTable.FatalErrorDumpOffset0;
      fatal_error.regDumpLen0 = saRoot->mainConfigTable.FatalErrorDumpLength0;
      fatal_error.regDumpBusBaseNum1 = saRoot->mainConfigTable.regDumpPCIBAR;
      fatal_error.regDumpOffset1 = saRoot->mainConfigTable.FatalErrorDumpOffset1;
      fatal_error.regDumpLen1 = saRoot->mainConfigTable.FatalErrorDumpLength1;
    }
    else
    {
      fatal_error.regDumpBusBaseNum0 = 0;
      fatal_error.regDumpOffset0 = 0;
      fatal_error.regDumpLen0 = 0;
      fatal_error.regDumpBusBaseNum1 = 0;
      fatal_error.regDumpOffset1 = 0;
      fatal_error.regDumpLen1 = 0;
    }
    /* Call Back with error */
    SA_DBG1(("siFatalInterruptHandler: Sendfatal %x HostR0 0x%x\n",Sendfatal ,ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_Rsvd_0_Register ) ));
    SA_DBG1(("siFatalInterruptHandler:  ScratchPad2 0x%x ScratchPad3 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Host_Scratchpad_2_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Host_Scratchpad_3_Register) ));

    ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_MALFUNCTION, Sendfatal, (void *)&fatal_error, agNULL);
    ret = AGSA_RC_SUCCESS;
  }
  else
  {
    bit32 host_reg0;
    host_reg0 = ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_Rsvd_0_Register );
    if( host_reg0 == 0x2)
    {
      Sendfatal = agFALSE;

      SA_DBG1(("siFatalInterruptHandler: Non fatal ScratchPad1 0x%x HostR0 0x%x\n", value,host_reg0));
      SA_DBG1(("siFatalInterruptHandler:  ScratchPad0 0x%x ScratchPad1 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_1_Register) ));
      SA_DBG1(("siFatalInterruptHandler:  ScratchPad2 0x%x ScratchPad3 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_2_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_3_Register) ));

      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_MALFUNCTION, Sendfatal, (void *)&fatal_error, agNULL);
      ret = AGSA_RC_SUCCESS;
    }
    else if( host_reg0 == HDA_AES_DIF_FUNC)
    {
      SA_DBG1(("siFatalInterruptHandler: HDA_AES_DIF_FUNC 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_Rsvd_0_Register)));
      Sendfatal = agFALSE;
      ret = AGSA_RC_SUCCESS;
    }
    else
    {
      SA_DBG1(("siFatalInterruptHandler: No error detected ScratchPad1 0x%x HostR0 0x%x\n", value,host_reg0));
      SA_DBG1(("siFatalInterruptHandler:  ScratchPad0 0x%x ScratchPad1 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_1_Register) ));
      SA_DBG1(("siFatalInterruptHandler:  ScratchPad2 0x%x ScratchPad3 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_2_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_3_Register) ));

      SA_DBG1(("siFatalInterruptHandler: Doorbell_Set  %08X U %08X\n",
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
      SA_DBG1(("siFatalInterruptHandler: Doorbell_Mask %08X U %08X\n",
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register ),
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU )));

      ret = AGSA_RC_FAILURE;
    }
  }
  return ret;

}

GLOBAL bit32 saFatalInterruptHandler(
  agsaRoot_t  *agRoot,
  bit32       interruptVectorIndex
  )
{
  agsaLLRoot_t         *saRoot = agNULL;
  bit32                ret = AGSA_RC_FAILURE;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  if (saRoot->ResetFailed)
  {
    SA_DBG1(("saFatalInterruptHandler: ResetFailed\n"));
    ossaDisableInterrupts(agRoot, interruptVectorIndex);
    return AGSA_RC_FAILURE;
  }
  if (saRoot->swConfig.fatalErrorInterruptEnable != 1)
  {
    SA_DBG1(("saFatalInterruptHandler: fatalErrorInterrtupt is NOT enabled\n"));
    ossaDisableInterrupts(agRoot, interruptVectorIndex);
    return AGSA_RC_FAILURE;
  }

  if (saRoot->swConfig.fatalErrorInterruptVector != interruptVectorIndex)
  {
    SA_DBG1(("saFatalInterruptHandler: interruptVectorIndex does not match 0x%x 0x%x\n",
             saRoot->swConfig.fatalErrorInterruptVector, interruptVectorIndex));
    SA_DBG1(("saFatalInterruptHandler:  ScratchPad0 0x%x ScratchPad1 0x%x\n",
                              ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register),
                              ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_1_Register) ));
    SA_DBG1(("saFatalInterruptHandler:  ScratchPad2 0x%x ScratchPad3 0x%x\n",
                              ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_2_Register),
                              ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_3_Register) ));
    ossaDisableInterrupts(agRoot, interruptVectorIndex);
    return AGSA_RC_FAILURE;
  }

  ret = siFatalInterruptHandler(agRoot,interruptVectorIndex);


  ossaDisableInterrupts(agRoot, interruptVectorIndex);

  return ret;
}
/******************************************************************************/
/*! \brief Function to process the interrupts
 *
 *  The saInterruptHandler() function is called after an interrupts has
 *  been received
 *  This function disables interrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex message that caused MSI message
 *
 *  \return TRUE if we caused interrupt
 *
 */
/*******************************************************************************/
FORCEINLINE bit32
saInterruptHandler(
  agsaRoot_t  *agRoot,
  bit32       interruptVectorIndex
  )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32 ToBeProcessedCount = 0;
  bit32 our_int = 0;
#ifdef SA_TEST_FW_SPURIOUS_INT
  bit8         i;
#endif/* SA_TEST_FW_SPURIOUS_INT */

  if( agNULL == saRoot )
  {
    /* Can be called before initialize is completed in a shared
       interrupt environment like windows 2003
    */
    return(ToBeProcessedCount);
  }

  if( (our_int = saRoot->OurInterrupt(agRoot,interruptVectorIndex)) == FALSE )
  {
#ifdef SA_TEST_FW_SPURIOUS_INT
    gSpuriousIntCount++;
    smTrace(hpDBG_REGISTERS,"S1",gSpuriousIntCount);
    /* TP:S1 gSpuriousIntCount */
#endif /* SA_TEST_FW_SPURIOUS_INT */
    return(ToBeProcessedCount);
  }

  smTraceFuncEnter(hpDBG_TICK_INT, "5q");

  smTrace(hpDBG_TICK_INT,"VI",interruptVectorIndex);
  /* TP:Vi interrupt VectorIndex */

  if ( agFALSE == saRoot->sysIntsActive )
  {
    // SA_ASSERT(0, "saInterruptHandler sysIntsActive not set");

#ifdef SA_PRINTOUT_IN_WINDBG
#ifndef DBG
        DbgPrint("saInterruptHandler: sysIntsActive not set Doorbell_Mask_Set  %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU) );
#endif /* DBG  */
#endif /* SA_PRINTOUT_IN_WINDBG  */


    SA_DBG1(("saInterruptHandler: Doorbell_Mask_Set  %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU)));
    ossaDisableInterrupts(agRoot, interruptVectorIndex);
    return(ToBeProcessedCount);

  }

  /* Allow replacement of disable interrupt */
  ossaDisableInterrupts(agRoot, interruptVectorIndex);


#ifdef SA_TEST_FW_SPURIOUS_INT

  /* count for my interrupt */
  gOurIntCount++;

  smTrace(hpDBG_REGISTERS,"S4",gOurIntCount);
  /* TP:S4 gOurIntCount */
#endif /* SA_TEST_FW_SPURIOUS_INT */

  smTraceFuncExit(hpDBG_TICK_INT, 'a', "5q");
  return(TRUE);

}

/******************************************************************************/
/*! \brief Function to disable MSIX interrupts
 *
 *  siDisableMSIXInterrupts disables interrupts
 *  called thru macro ossaDisableInterrupts
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex - vector index for message
 *
 */
/*******************************************************************************/
GLOBAL void siDisableMSIXInterrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  bit32 msi_index;
#ifndef SA_CLEAR_ODCR_IN_INTERRUPT
  bit32 value;
#endif /* SA_CLEAR_ODCR_IN_INTERRUPT */
  msi_index = interruptVectorIndex * MSIX_TABLE_ELEMENT_SIZE;
  msi_index += MSIX_TABLE_BASE;
  ossaHwRegWrite(agRoot,msi_index , MSIX_INTERRUPT_DISABLE);
  ossaHwRegRead(agRoot, msi_index); /* Dummy read */
#ifndef SA_CLEAR_ODCR_IN_INTERRUPT
  value  = (1 << interruptVectorIndex);
  ossaHwRegWrite(agRoot, MSGU_ODCR, value);
#endif /* SA_CLEAR_ODCR_IN_INTERRUPT */
}

/******************************************************************************/
/*! \brief Function to disable MSIX V interrupts
 *
 *  siDisableMSIXInterrupts disables interrupts
 *  called thru macro ossaDisableInterrupts
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex - vector index for message
 *
 */
/*******************************************************************************/
void siDisableMSIX_V_Interrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  bit64 mask;
  agsabit32bit64 u64;
  mask =( (bit64)1 << interruptVectorIndex);
  u64.B64 = mask;
  if(smIS64bInt(agRoot))
  {
    SA_DBG4(("siDisableMSIX_V_Interrupts: VI %d U 0x%08X L 0x%08X\n",interruptVectorIndex,u64.S32[1],u64.S32[0]));
    ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_RegisterU,u64.S32[1]);
  }
  ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_Register, u64.S32[0]);

}
/******************************************************************************/
/*! \brief Function to disable MSI interrupts
 *
 *  siDisableMSIInterrupts disables interrupts
 *  called thru macro ossaDisableInterrupts
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex - vector index for message
 *
 */
/*******************************************************************************/
GLOBAL void siDisableMSIInterrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  bit32 ODMRValue;
  bit32 mask;
  mask = 1 << interruptVectorIndex;

  /*Must be protected for interuption */
  ODMRValue = ossaHwRegRead(agRoot, MSGU_ODMR);
  ODMRValue |= mask;

  ossaHwRegWrite(agRoot, MSGU_ODMR, ODMRValue);
  ossaHwRegWrite(agRoot, MSGU_ODCR, mask);
}

/******************************************************************************/
/*! \brief Function to disable MSI V interrupts
 *
 *  siDisableMSIInterrupts disables interrupts
 *  called thru macro ossaDisableInterrupts
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex - vector index for message
 *
 */
/*******************************************************************************/
GLOBAL void siDisableMSI_V_Interrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  SA_ASSERT(0, "Should not be called");
  SA_DBG4(("siDisableMSI_V_Interrupts:\n"));
}

/******************************************************************************/
/*! \brief Function to process Legacy interrupts
 *
 *  siDisableLegacyInterrupts disables interrupts
 *  called thru macro ossaDisableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex not used in legacy case
 *
 */
/*******************************************************************************/
GLOBAL void siDisableLegacyInterrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  ossaHwRegWrite(agRoot, MSGU_ODMR, ODMR_MASK_ALL);
#ifndef SA_CLEAR_ODCR_IN_INTERRUPT
  ossaHwRegWrite(agRoot, MSGU_ODCR, ODCR_CLEAR_ALL);
#endif /* SA_CLEAR_ODCR_IN_INTERRUPT */
}

/******************************************************************************/
/*! \brief Function to process Legacy V interrupts
 *
 *  siDisableLegacyInterrupts disables interrupts
 *  called thru macro ossaDisableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex not used in legacy case
 *
 */
/*******************************************************************************/
GLOBAL void siDisableLegacy_V_Interrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{

  bit64 mask;
  agsabit32bit64 u64;
  mask =( (bit64)1 << interruptVectorIndex);
  u64.B64 = mask;

  SA_DBG4(("siDisableLegacy_V_Interrupts:IN MSGU_READ_ODR  %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODR,  V_Outbound_Doorbell_Set_Register)));
  SA_DBG4(("siDisableLegacy_V_Interrupts:IN MSGU_READ_ODMR %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODMR, V_Outbound_Doorbell_Mask_Set_Register )));
  if(smIS64bInt(agRoot))
  {
    SA_DBG4(("siDisableLegacy_V_Interrupts: VI %d U 0x%08X L 0x%08X\n",interruptVectorIndex,u64.S32[1],u64.S32[0]));
    ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_Register,u64.S32[1] );
  }
  ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_RegisterU,u64.S32[0]);

}
/******************************************************************************/
/*! \brief Function to process MSIX interrupts
 *
 *  siOurMSIXInterrupt checks if we generated interrupt
 *  called thru function pointer saRoot->OurInterrupt
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \return always true
 */
/*******************************************************************************/
GLOBAL bit32 siOurMSIXInterrupt(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  return(TRUE);
}

/******************************************************************************/
/*! \brief Function to process MSIX V interrupts
 *
 *  siOurMSIXInterrupt checks if we generated interrupt
 *  called thru function pointer saRoot->OurInterrupt
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \return always true
 */
/*******************************************************************************/
GLOBAL bit32 siOurMSIX_V_Interrupt(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  return(TRUE);
}
/******************************************************************************/
/*! \brief Function to process MSI interrupts
 *
 *  siOurMSIInterrupt checks if we generated interrupt
 *  called thru function pointer saRoot->OurInterrupt
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \return always true
 */
/*******************************************************************************/
bit32 siOurMSIInterrupt(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  return(TRUE);
}

/******************************************************************************/
/*! \brief Function to process MSI V interrupts
 *
 *  siOurMSIInterrupt checks if we generated interrupt
 *  called thru function pointer saRoot->OurInterrupt
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \return always true
 */
/*******************************************************************************/
bit32 siOurMSI_V_Interrupt(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  SA_DBG4((":siOurMSI_V_Interrupt\n"));
  return(TRUE);
}

/******************************************************************************/
/*! \brief Function to process Legacy interrupts
 *
 *  siOurLegacyInterrupt checks if we generated interrupt
 *  called thru function pointer saRoot->OurInterrupt
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \return true if we claim interrupt
 */
/*******************************************************************************/
bit32 siOurLegacyInterrupt(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  bit32 Int_masked;
  bit32 Int_active;
  Int_masked = MSGU_READ_ODMR;
  Int_active = MSGU_READ_ODR;

  if(Int_masked & 1 )
  {
    return(FALSE);
  }
  if(Int_active & 1 )
  {

    return(TRUE);
  }
  return(FALSE);
}

/******************************************************************************/
/*! \brief Function to process Legacy V interrupts
 *
 *  siOurLegacyInterrupt checks if we generated interrupt
 *  called thru function pointer saRoot->OurInterrupt
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \return true if we claim interrupt
 */
/*******************************************************************************/
bit32 siOurLegacy_V_Interrupt(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  bit32 Int_active;
  Int_active = siHalRegReadExt(agRoot, GEN_MSGU_ODR, V_Outbound_Doorbell_Set_Register  );

  return(Int_active ? TRUE : FALSE);
}


/******************************************************************************/
/*! \brief Function to process the cause of interrupt
 *
 *  The saDelayedInterruptHandler() function is called after an interrupt messages has
 *  been received it may be called by a deferred procedure call
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex  - vector index for message
 *  \param count Number of completion queue entries to consume
 *
 *  \return number of messages processed
 *
 */
/*******************************************************************************/
FORCEINLINE bit32
saDelayedInterruptHandler(
  agsaRoot_t  *agRoot,
  bit32       interruptVectorIndex,
  bit32       count
  )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32         processedMsgCount = 0;
  bit32         pad1 = 0;
  bit32         host_reg0 = 0;
#if defined(SALLSDK_DEBUG)
  bit32 host_reg1 = 0;
#endif
  bit8         i = 0;

  OSSA_OUT_ENTER(agRoot);

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5p");

  smTrace(hpDBG_VERY_LOUD,"Vd",interruptVectorIndex);
  /* TP:Vd delayed VectorIndex */
  smTrace(hpDBG_VERY_LOUD,"Vc",count);
  /* TP:Vc IOMB count*/

  if( saRoot->swConfig.fatalErrorInterruptEnable &&
      saRoot->swConfig.fatalErrorInterruptVector == interruptVectorIndex )
  {
    pad1 = siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1);
    host_reg0 = ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_Rsvd_0_Register );


    if(saRoot->swConfig.hostDirectAccessMode & 2 )
    {
      if( host_reg0 == HDA_AES_DIF_FUNC)
      { 
        host_reg0 = 0;
      }
    }


#if defined(SALLSDK_DEBUG)
    host_reg1 = ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_Rsvd_1_Register );
#endif
    if( (SCRATCH_PAD1_V_ERROR_STATE( pad1 ) != 0 ) && host_reg0 )
    {

      SA_DBG1(("saDelayedInterruptHandler: vi %d  Error %08X\n",interruptVectorIndex,  SCRATCH_PAD1_V_ERROR_STATE( pad1 )));
      SA_DBG1(("saDelayedInterruptHandler: Sp 1 %08X Hr0 %08X Hr1 %08X\n",pad1,host_reg0,host_reg1 ));
      SA_DBG1(("saDelayedInterruptHandler: SCRATCH_PAD1_V_ERROR_STATE      %08X\n", SCRATCH_PAD1_V_ERROR_STATE( pad1 )));
      SA_DBG1(("saDelayedInterruptHandler: SCRATCH_PAD1_V_ILA_ERROR_STATE  %08X\n", SCRATCH_PAD1_V_ILA_ERROR_STATE( pad1 )));
      SA_DBG1(("saDelayedInterruptHandler: SCRATCH_PAD1_V_RAAE_ERROR_STATE %08X\n", SCRATCH_PAD1_V_RAAE_ERROR_STATE( pad1 )));
      SA_DBG1(("saDelayedInterruptHandler: SCRATCH_PAD1_V_IOP0_ERROR_STATE %08X\n", SCRATCH_PAD1_V_IOP0_ERROR_STATE( pad1 )));
      SA_DBG1(("saDelayedInterruptHandler: SCRATCH_PAD1_V_IOP1_ERROR_STATE %08X\n", SCRATCH_PAD1_V_IOP1_ERROR_STATE( pad1 )));

      siFatalInterruptHandler( agRoot, interruptVectorIndex  );
      ossaDisableInterrupts(agRoot, interruptVectorIndex);

    }
    else
    {
      SA_DBG2(("saDelayedInterruptHandler: Fatal Check VI %d SCRATCH_PAD1 %08X host_reg0 %08X host_reg1 %08X\n",interruptVectorIndex, pad1,host_reg0,host_reg1));
      SA_DBG2(("saDelayedInterruptHandler:  ScratchPad0 0x%x ScratchPad1 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_0_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_1_Register) ));
      SA_DBG2(("saDelayedInterruptHandler:  ScratchPad2 0x%x ScratchPad3 0x%x\n",
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_2_Register),
                                ossaHwRegReadExt(agRoot, PCIBAR0,V_Scratchpad_3_Register) ));

      SA_DBG2(("saDelayedInterruptHandler: Doorbell_Set  %08X U %08X\n",
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
      SA_DBG2(("saDelayedInterruptHandler: Doorbell_Mask %08X U %08X\n",
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register ),
                               ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU )));
    }

  }


#ifdef SA_LNX_PERF_MODE
  return siProcessOBMsg(agRoot, count, interruptVectorIndex);
#endif

  /* check all the configuration outbound queues within a vector bitmap */
  SA_ASSERT((saRoot->QueueConfig.numOutboundQueues < 65), "numOutboundQueue");

  for ( i = 0; i < saRoot->QueueConfig.numOutboundQueues; i++ )
  {
    /* process IOMB in the outbound queue 0 to 31 if bit set in the vector bitmap */
    if (i < OQ_NUM_32)
    {
      if (saRoot->interruptVecIndexBitMap[interruptVectorIndex] & (1 << i))
      {
        processedMsgCount += siProcessOBMsg(agRoot, count, i);
      }
      else if (saRoot->QueueConfig.outboundQueues[i].interruptEnable == 0)
      {
        /* polling mode - interruptVectorIndex = 0 only and no bit set */
        processedMsgCount += siProcessOBMsg(agRoot, count, i);
      }
#ifdef SA_FW_TEST_INTERRUPT_REASSERT
      else if (saRoot->CheckAll)
      {
        /* polling mode - interruptVectorIndex = 0 only and no bit set */
        processedMsgCount += siProcessOBMsg(agRoot, count, i);
      }
#endif /* SA_FW_TEST_INTERRUPT_REASSERT */

    }
    else
    {
      /* process IOMB in the outbound queue 32 to 63 if bit set in the vector bitmap */
      if (saRoot->interruptVecIndexBitMap1[interruptVectorIndex] & (1 << (i - OQ_NUM_32)))
      {
        processedMsgCount += siProcessOBMsg(agRoot, count, i);
      }
      /* check interruptEnable bit for polling mode of OQ */
      /* the following code can be removed, we do not care about the bit */
      else if (saRoot->QueueConfig.outboundQueues[i].interruptEnable == 0)
      {
        /* polling mode - interruptVectorIndex = 0 only and no bit set */
        processedMsgCount += siProcessOBMsg(agRoot, count, i);
      }
#ifdef SA_FW_TEST_INTERRUPT_REASSERT
      else if (saRoot->CheckAll)
      {
        /* polling mode - interruptVectorIndex = 0 only and no bit set */
        processedMsgCount += siProcessOBMsg(agRoot, count, i);
      }
#endif /* SA_FW_TEST_INTERRUPT_REASSERT */
    }
  }

#ifdef SA_FW_TEST_INTERRUPT_REASSERT
  saRoot->CheckAll = 0;
#endif /* SA_FW_TEST_INTERRUPT_REASSERT */

#ifndef SA_RENABLE_IN_OSLAYER
  if ( agTRUE == saRoot->sysIntsActive )
  {
    /* Allow replacement of enable interrupt */
    ossaReenableInterrupts(agRoot, interruptVectorIndex);
  }
#endif /* SA_RENABLE_IN_OSLAYER */

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5p");

  OSSA_OUT_LEAVE(agRoot);
  return processedMsgCount;
}

/******************************************************************************/
/*! \brief Function to reenable MSIX interrupts
 *
 *  siReenableMSIXInterrupts  reenableinterrupts
 *  called thru macro ossaReenableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex  - vector index for message
 *
 */
/*******************************************************************************/
void siReenableMSIXInterrupts(
   agsaRoot_t *agRoot,
   bit32 interruptVectorIndex
  )
{
  bit32 msi_index;
#ifdef SA_CLEAR_ODCR_IN_INTERRUPT
  bit32 value;
#endif /* SA_CLEAR_ODCR_IN_INTERRUPT */
  msi_index = interruptVectorIndex * MSIX_TABLE_ELEMENT_SIZE;
  msi_index += MSIX_TABLE_BASE;
  ossaHwRegWriteExt(agRoot, PCIBAR0,msi_index, MSIX_INTERRUPT_ENABLE);

  SA_DBG4(("siReenableMSIXInterrupts:interruptVectorIndex %d\n",interruptVectorIndex));

#ifdef SA_CLEAR_ODCR_IN_INTERRUPT
  value  = (1 << interruptVectorIndex);
  siHalRegWriteExt(agRoot, GEN_MSGU_ODCR, MSGU_ODCR, value);
#endif /* SA_CLEAR_ODCR_IN_INTERRUPT */
}
/******************************************************************************/
/*! \brief Function to reenable MSIX interrupts
 *
 *  siReenableMSIXInterrupts  reenableinterrupts
 *  called thru macro ossaReenableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex  - vector index for message
 *
 */
/*******************************************************************************/
void siReenableMSIX_V_Interrupts(
    agsaRoot_t *agRoot,
    bit32 interruptVectorIndex
    )
{
  agsaLLRoot_t         *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit64 mask;
  agsabit32bit64 u64;
  mask =( (bit64)1 << interruptVectorIndex);
  u64.B64 = mask;

  SA_DBG4(("siReenableMSIX_V_Interrupts:\n"));

  if(saRoot->sysIntsActive)
  {
    if(smIS64bInt(agRoot))
    {
      SA_DBG4(("siReenableMSIX_V_Interrupts: VI %d U 0x%08X L 0x%08X\n",interruptVectorIndex,u64.S32[1],u64.S32[0]));
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Clear_RegisterU,u64.S32[1] );
    }
    ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Clear_Register,u64.S32[0]);
  }
  else
  {
      SA_DBG1(("siReenableMSIX_V_Interrupts: VI %d sysIntsActive off\n",interruptVectorIndex));
  }

}

/******************************************************************************/
/*! \brief Function to reenable MSI interrupts
 *
 *  siReenableMSIXInterrupts  reenableinterrupts
 *  called thru macro ossaReenableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex  - vector index for message
 *
 */
/*******************************************************************************/
GLOBAL void siReenableMSIInterrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  bit32 ODMRValue;

  ODMRValue = siHalRegReadExt(agRoot, GEN_MSGU_ODMR, MSGU_ODMR);
  ODMRValue &= ~(1 << interruptVectorIndex);

  siHalRegWriteExt(agRoot, GEN_MSGU_ODMR, MSGU_ODMR, ODMRValue);
}

/******************************************************************************/
/*! \brief Function to reenable MSI V interrupts
 *
 *  siReenableMSIXInterrupts  reenableinterrupts
 *  called thru macro ossaReenableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex  - vector index for message
 *
 */
/*******************************************************************************/
GLOBAL void siReenableMSI_V_Interrupts(
   agsaRoot_t *agRoot,
   bit32 interruptVectorIndex
   )
{
  SA_ASSERT(0, "Should not be called");

  SA_DBG4(("siReenableMSI_V_Interrupts:\n"));

}
/******************************************************************************/
/*! \brief Function to reenable Legacy interrupts
 *
 *  siReenableLegacyInterrupts reenableinterrupts
 *  called thru macro ossaReenableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex always zero
 *
 */
/*******************************************************************************/
GLOBAL void siReenableLegacyInterrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{
  siHalRegWriteExt(agRoot, GEN_MSGU_ODMR, MSGU_ODMR, ODMR_CLEAR_ALL);

#ifdef SA_CLEAR_ODCR_IN_INTERRUPT
  siHalRegWriteExt(agRoot, GEN_MSGU_ODCR, MSGU_ODCR, ODCR_CLEAR_ALL);
#endif /* SA_CLEAR_ODCR_IN_INTERRUPT */
}

/******************************************************************************/
/*! \brief Function to reenable Legacy V interrupts
 *
 *  siReenableLegacyInterrupts reenableinterrupts
 *  called thru macro ossaReenableInterrupts
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex always zero
 *
 */
/*******************************************************************************/
GLOBAL void siReenableLegacy_V_Interrupts(
  agsaRoot_t *agRoot,
  bit32 interruptVectorIndex
  )
{

  bit32 mask;
  mask = 1 << interruptVectorIndex;

  SA_DBG5(("siReenableLegacy_V_Interrupts:IN MSGU_READ_ODR  %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODR, V_Outbound_Doorbell_Set_Register)));
  SA_DBG5(("siReenableLegacy_V_Interrupts:IN MSGU_READ_ODMR %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODMR, V_Outbound_Doorbell_Mask_Set_Register )));

  ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Clear_Register, mask);
  ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Clear_Register, mask );


  SA_DBG5(("siReenableLegacy_V_Interrupts:OUT MSGU_READ_ODMR %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODMR, V_Outbound_Doorbell_Mask_Set_Register )));

}

/******************************************************************************/
/*! \brief Function to enable a single interrupt vector
 *
 *
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex Interrupt vector to enable
 *
 */
/*******************************************************************************/
/******************************************************************************/
/*! \brief  saSystemInterruptsEnable
 *   Function to enable a single interrupt vector
 *
 *  \param agRoot OS Layer-specific and LL Layer-specific context handles for this
 *                instance of SAS/SATA hardware
 *  \param interruptVectorIndex Interrupt vector to enable
 *
 */
/*******************************************************************************/
GLOBAL FORCEINLINE
void saSystemInterruptsEnable(
                              agsaRoot_t  *agRoot,
                              bit32       interruptVectorIndex
                              )
{
  ossaReenableInterrupts(agRoot, interruptVectorIndex);
}
/******************************************************************************/
/*! \brief Routine to handle Outbound Message
 *
 *  The handle for outbound message
 *
 *  \param agRoot   handles for this instance of SAS/SATA hardware
 *  \param count    interrupt message count
 *  \param queueNum outbound queue
 *
 *  \return
 */
/*******************************************************************************/
LOCAL FORCEINLINE bit32
siProcessOBMsg(
  agsaRoot_t  *agRoot,
  bit32       count,
  bit32       queueNum
  )
{
  agsaLLRoot_t         *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  mpiOCQueue_t         *circularQ = agNULL;
  void                 *pMsg1     = agNULL;
  bit32                ret, processedMsgCount = 0;
  bit32                ParseOBIombStatus = 0;
#ifdef SA_ENABLE_TRACE_FUNCTIONS
  bit32                i = 0;
#endif
  bit16                opcode  = 0;
  mpiMsgCategory_t     category;
  bit8                 bc      = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5r");


  SA_DBG3(("siProcessOBMsg: queueNum 0x%x\n", queueNum));

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_OBQ_LOCK + queueNum);

  circularQ = &saRoot->outboundQueue[queueNum];
  OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);

  if (circularQ->producerIdx == circularQ->consumerIdx)
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_OBQ_LOCK + queueNum);
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5r");
    return processedMsgCount;
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_OBQ_LOCK + queueNum);

  do
  {
    /* ossaSingleThreadedEnter(agRoot, LL_IOREQ_OBQ_LOCK + queueNum); */
    ret = mpiMsgConsume(circularQ, &pMsg1, &category, &opcode, &bc);
    /* ossaSingleThreadedLeave(agRoot, LL_IOREQ_OBQ_LOCK + queueNum); */

    if (AGSA_RC_SUCCESS == ret)
    {
      smTrace(hpDBG_IOMB,"M0",queueNum);
      /* TP:M0 queueNum */
      smTrace(hpDBG_VERY_LOUD,"MA",opcode);
      /* TP:MA opcode */
      smTrace(hpDBG_IOMB,"MB",category);
      /* TP:MB category */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
      for (i=0; i<((bit32)bc*(circularQ->elementSize/4)); i++)
      {
          /* The -sizeof(mpiMsgHeader_t) is to account for mpiMsgConsume incrementing the pointer past the header*/
          smTrace(hpDBG_IOMB,"MC",*( ((bit32*)((bit8 *)pMsg1 - sizeof(mpiMsgHeader_t))) + i));
          /* TP:MC Outbound IOMB Dword */
      }
#endif

      MPI_DEBUG_TRACE( circularQ->qNumber,((circularQ->producerIdx << 16 ) | circularQ->consumerIdx),MPI_DEBUG_TRACE_OBQ, (void *)(((bit8*)pMsg1) - sizeof(mpiMsgHeader_t)), circularQ->elementSize);

      ossaLogIomb(circularQ->agRoot,
                  circularQ->qNumber,
                  FALSE,
                  (void *)(((bit8*)pMsg1) - sizeof(mpiMsgHeader_t)),
                  bc*circularQ->elementSize);

      ossaQueueProcessed(agRoot, queueNum, circularQ->producerIdx, circularQ->consumerIdx);
      /* process the outbound message */
      ParseOBIombStatus = mpiParseOBIomb(agRoot, (bit32 *)pMsg1, category, opcode);
      if (ParseOBIombStatus == AGSA_RC_FAILURE)
      {
        SA_DBG1(("siProcessOBMsg, Failed Q %2d PI 0x%03x CI 0x%03x\n", queueNum, circularQ->producerIdx, circularQ->consumerIdx));
#if defined(SALLSDK_DEBUG)
        /* free the message for debug: this is a hang! */

        mpiMsgFreeSet(circularQ, pMsg1, bc);
        processedMsgCount ++;
#endif /**/
        break;
      }

      /* free the message from the outbound circular buffer */
      mpiMsgFreeSet(circularQ, pMsg1, bc);
      processedMsgCount ++;
    }
    else
    //if (AGSA_RC_BUSY == ret) // always (circularQ->producerIdx == circularQ->consumerIdx)
    // || (AGSA_RC_FAILURE == ret)
    {
        break;
    }
  }
  /* end of message processing if hit the count */
  while(count > processedMsgCount);

/* #define SALLSDK_FATAL_ERROR_DETECT 1 */
/*
   this comments are to be removed
   fill in 0x1D 0x1e 0x1f 0x20 in MPI table for
  bit32   regDumpBusBaseNum0;
  bit32   regDumpOffset0;
  bit32   regDumpLen0;
  bit32   regDumpBusBaseNum1;
  bit32   regDumpOffset1;
  bit32   regDumpLen1;
  in agsaFatalErrorInfo_t

  ??? regDumpBusBaseNum0 and regDumpBusBaseNum1
    saRoot->mainConfigTable.regDumpPCIBAR = pcibar;
    saRoot->mainConfigTable.FatalErrorDumpOffset0 = config->FatalErrorDumpOffset0;
    saRoot->mainConfigTable.FatalErrorDumpLength0 = config->FatalErrorDumpLength0;
    saRoot->mainConfigTable.FatalErrorDumpOffset1 = config->FatalErrorDumpOffset1;
    saRoot->mainConfigTable.FatalErrorDumpLength1 = config->FatalErrorDumpLength1;



*/
#if defined(SALLSDK_FATAL_ERROR_DETECT)

  if( smIS_SPC(agRoot) ) /* SPC only */
  {

  /* any fatal error happened */
  /* executing this code impacts performance by 1% when no error is detected */
  {
    agsaFatalErrorInfo_t fatal_error;
    bit32                value;
    bit32                value1;

    value  = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
    value1 = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2);

    if( (value & SA_FATAL_ERROR_SP1_AAP1_ERR_MASK) == SA_FATAL_ERROR_FATAL_ERROR ||
        (value1 & SA_FATAL_ERROR_SP2_IOP_ERR_MASK) == SA_FATAL_ERROR_FATAL_ERROR    )
    {
      si_memset(&fatal_error, 0, sizeof(agsaFatalErrorInfo_t));
      /* read detail fatal errors */
      value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0, MSGU_SCRATCH_PAD_0);
      fatal_error.errorInfo0 = value;
      SA_DBG1(("siProcessOBMsg: ScratchPad0 AAP error code 0x%x\n", value));

      value  = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
      fatal_error.errorInfo1 = value;
      /* AAP error state */
      SA_DBG1(("siProcessOBMsg: AAP error state and error code 0x%x\n", value));
      value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2);
      fatal_error.errorInfo2 = value;
      /* IOP error state */
      SA_DBG1(("siProcessOBMsg: IOP error state and error code 0x%x\n", value));
      value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3, MSGU_SCRATCH_PAD_3);
      SA_DBG1(("siProcessOBMsg: ScratchPad3 IOP error code 0x%x\n", value));
      fatal_error.errorInfo3 = value;

      if (agNULL != saRoot)
      {
        fatal_error.regDumpBusBaseNum0 = saRoot->mainConfigTable.regDumpPCIBAR;
        fatal_error.regDumpOffset0 = saRoot->mainConfigTable.FatalErrorDumpOffset0;
        fatal_error.regDumpLen0 = saRoot->mainConfigTable.FatalErrorDumpLength0;
        fatal_error.regDumpBusBaseNum1 = saRoot->mainConfigTable.regDumpPCIBAR;
        fatal_error.regDumpOffset1 = saRoot->mainConfigTable.FatalErrorDumpOffset1;
        fatal_error.regDumpLen1 = saRoot->mainConfigTable.FatalErrorDumpLength1;
      }
      else
      {
        fatal_error.regDumpBusBaseNum0 = 0;
        fatal_error.regDumpOffset0 = 0;
        fatal_error.regDumpLen0 = 0;
        fatal_error.regDumpBusBaseNum1 = 0;
        fatal_error.regDumpOffset1 = 0;
        fatal_error.regDumpLen1 = 0;
      }
      /* Call Back with error */
      SA_DBG1(("siProcessOBMsg: SALLSDK_FATAL_ERROR_DETECT \n"));
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_MALFUNCTION, 0, (void *)&fatal_error, agNULL);
    }
  }
  }
#endif /* SALLSDK_FATAL_ERROR_DETECT */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5r");
  return processedMsgCount;
}

/******************************************************************************/
/*! \brief Function to enable/disable interrupts
 *
 *  The saSystemInterruptsActive() function is called to indicate to the LL Layer
 *  whether interrupts are available. The parameter sysIntsActive indicates whether
 *  interrupts are available at this time.
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param sysIntsActive flag for enable/disable interrupt
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void saSystemInterruptsActive(
  agsaRoot_t  *agRoot,
  agBOOLEAN   sysIntsActive
  )
{
  bit32 x;
  agsaLLRoot_t  *saRoot;

  SA_ASSERT((agNULL != agRoot), "");
  if (agRoot == agNULL)
  {
    SA_DBG1(("saSystemInterruptsActive: agRoot == agNULL\n"));
    return;
  }
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
  if (saRoot == agNULL)
  {
    SA_DBG1(("saSystemInterruptsActive: saRoot == agNULL\n"));
    return;
  }

  smTraceFuncEnter(hpDBG_TICK_INT,"5s");
  SA_DBG1(("saSystemInterruptsActive: now 0x%X new 0x%x\n",saRoot->sysIntsActive,sysIntsActive));
  SA_DBG3(("saSystemInterruptsActive: Doorbell_Set  %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
  SA_DBG3(("saSystemInterruptsActive: Doorbell_Mask %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register ),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU )));

  if( saRoot->sysIntsActive && sysIntsActive )
  {
    SA_DBG1(("saSystemInterruptsActive: Already active 0x%X new 0x%x\n",saRoot->sysIntsActive,sysIntsActive));
    smTraceFuncExit(hpDBG_TICK_INT, 'a', "5s");
    return;
  }

  if( !saRoot->sysIntsActive && !sysIntsActive )
  {
    if(smIS_SPC(agRoot))
    {
      siHalRegWriteExt(agRoot, GEN_MSGU_ODMR, MSGU_ODMR,AGSA_INTERRUPT_HANDLE_ALL_CHANNELS );
    }
    else
    {
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_Register, AGSA_INTERRUPT_HANDLE_ALL_CHANNELS);
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_RegisterU, AGSA_INTERRUPT_HANDLE_ALL_CHANNELS);
    }
    SA_DBG1(("saSystemInterruptsActive: Already disabled 0x%X new 0x%x\n",saRoot->sysIntsActive,sysIntsActive));
    smTraceFuncExit(hpDBG_TICK_INT, 'b', "5s");
    return;
  }

  /* Set the flag is sdkData */
  saRoot->sysIntsActive = (bit8)sysIntsActive;


  smTrace(hpDBG_TICK_INT,"Vq",sysIntsActive);
  /* TP:Vq sysIntsActive */
  /* If sysIntsActive is true */
  if ( agTRUE == sysIntsActive )
  {

    SA_DBG1(("saSystemInterruptsActive: Doorbell_Set  %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
    SA_DBG1(("saSystemInterruptsActive: Doorbell_Mask_Set  %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU)));
    if(smIS_SPCV(agRoot))
    {
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Clear_Register, 0xFFFFFFFF);
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Clear_RegisterU, 0xFFFFFFFF);
    }
    /* enable interrupt */
    for(x=0; x < saRoot->numInterruptVectors; x++)
    {
      ossaReenableInterrupts(agRoot,x );
    }

    if(saRoot->swConfig.fatalErrorInterruptEnable)
    {
      ossaReenableInterrupts(agRoot,saRoot->swConfig.fatalErrorInterruptVector );
    }

    siHalRegWriteExt(agRoot, GEN_MSGU_ODMR, MSGU_ODMR, 0);
  }
  /* If sysIntsActive is false */
  else
  {
    /* disable interrupt */
    if(smIS_SPC(agRoot))
    {
      siHalRegWriteExt(agRoot, GEN_MSGU_ODMR, MSGU_ODMR,AGSA_INTERRUPT_HANDLE_ALL_CHANNELS );
    }
    else
    {
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_Register, AGSA_INTERRUPT_HANDLE_ALL_CHANNELS);
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Mask_Set_RegisterU, AGSA_INTERRUPT_HANDLE_ALL_CHANNELS);
    }
  }

  SA_DBG3(("saSystemInterruptsActive: Doorbell_Set  %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
  SA_DBG3(("saSystemInterruptsActive: Doorbell_Mask %08X U %08X\n",
                           ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register ),
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU )));


  smTraceFuncExit(hpDBG_TICK_INT, 'c', "5s");
}

/******************************************************************************/
/*! \brief Routine to handle for received SAS with data payload event
 *
 *  The handle for received SAS with data payload event
 *
 *  \param agRoot   handles for this instance of SAS/SATA hardware
 *  \param pRequest handles for the IOrequest
 *  \param pRespIU  the pointer to the Response IU
 *  \param param    Payload Length
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siEventSSPResponseWtDataRcvd(
  agsaRoot_t                *agRoot,
  agsaIORequestDesc_t       *pRequest,
  agsaSSPResponseInfoUnit_t *pRespIU,
  bit32                     param,
  bit32                     sspTag
  )
{
  agsaLLRoot_t  *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t        *pDevice;
  bit32                   count = 0;
  bit32                   padCount;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5g");

  /* get frame handle */

  /* If the request is still valid */
  if ( agTRUE == pRequest->valid )
  {
    /* get device */
    pDevice = pRequest->pDevice;

    /* Delete the request from the pendingIORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    if (sspTag & SSP_RESCV_BIT)
    {
        /* get the pad count, bit 17 and 18 of sspTag */
      padCount = (sspTag >> SSP_RESCV_PAD_SHIFT) & 0x3;
      /* get Residual Count */
      count = *(bit32 *)((bit8 *)pRespIU + param + padCount);
    }

    (*(ossaSSPCompletedCB_t)(pRequest->completionCB))(agRoot,
                             pRequest->pIORequestContext,
                             OSSA_IO_SUCCESS,
                             param,
                             (void *)pRespIU,
                             (bit16)(sspTag & SSPTAG_BITS),
                             count);

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("siEventSSPResponseWtDataRcvd: saving pRequest (%p) for later use\n", pRequest));
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
    SA_DBG1(("siEventSSPResponseWtDataRcvd: pRequest->Valid not TRUE\n"));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5g");

  return;
}

/******************************************************************************/
/*! \brief Routine to handle successfully completed IO event
 *
 *  Handle successfully completed IO
 *
 *  \param agRoot   handles for this instance of SAS/SATA hardware
 *  \param pRequest Pointer of IO request of the IO
 *  \param status   status of the IO
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL FORCEINLINE void siIODone(
  agsaRoot_t                *agRoot,
  agsaIORequestDesc_t       *pRequest,
  bit32                     status,
  bit32                     sspTag
  )
{
  agsaLLRoot_t     *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t *pDevice = agNULL;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5h");

  SA_ASSERT(NULL != pRequest, "pRequest cannot be null");

  /* If the request is still valid */
  if ( agTRUE == pRequest->valid )
  {
    /* get device */
    pDevice = pRequest->pDevice;

    /* process different request type */
    switch (pRequest->requestType & AGSA_REQTYPE_MASK)
    {
      case AGSA_SSP_REQTYPE:
      {
        SA_ASSERT(pRequest->valid, "pRequest not valid");
        pRequest->completionCB(agRoot,
                               pRequest->pIORequestContext,
                               OSSA_IO_SUCCESS,
                               0,
                               agNULL,
                               (bit16)(sspTag & SSPTAG_BITS),
                               0);
        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* Delete the request from the pendingIORequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
        /* return the request to free pool */
        pRequest->valid = agFALSE;
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);


        break;
      }
      case AGSA_SATA_REQTYPE:
      {
        SA_DBG5(("siIODone: SATA complete\n"));

        if ( agNULL != pRequest->pIORequestContext )
        {
          SA_DBG5(("siIODone: Complete Request\n"));

          (*(ossaSATACompletedCB_t)(pRequest->completionCB))(agRoot,
                                                             pRequest->pIORequestContext,
                                                             OSSA_IO_SUCCESS,
                                                             agNULL,
                                                             0,
                                                             agNULL);
        }
        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* Delete the request from the pendingIORequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

        pRequest->valid = agFALSE;

        break;
      }
      case AGSA_SMP_REQTYPE:
      {
        if ( agNULL != pRequest->pIORequestContext )
        {
          (*(ossaSMPCompletedCB_t)(pRequest->completionCB))(agRoot,
                                                            pRequest->pIORequestContext,
                                                            OSSA_IO_SUCCESS,
                                                            0,
                                                            agNULL);
        }

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* Delete the request from the pendingSMPRequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
        /* return the request to free pool */
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("siIODone: saving pRequest (%p) for later use\n", pRequest));
          saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
        }
        else
        {
          saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
        }
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

        pRequest->valid = agFALSE;

        break;
      }
      default:
      {
        SA_DBG1(("siIODone: unknown request type (%x) is completed. HTag=0x%x\n", pRequest->requestType, pRequest->HTag));
        break;
      }
    }
  }
  else
  {
    SA_DBG1(("siIODone: The request is not valid any more. HTag=0x%x requestType=0x%x\n", pRequest->HTag, pRequest->requestType));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5h");

}

/******************************************************************************/
/*! \brief Routine to handle abnormal completed IO/SMP event
 *
 *  Handle abnormal completed IO/SMP
 *
 *  \param agRoot   handles for this instance of SAS/SATA hardware
 *  \param pRequest Pointer of IO request of the IO
 *  \param status   status of the IO
 *  \param param    Length
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siAbnormal(
  agsaRoot_t                *agRoot,
  agsaIORequestDesc_t       *pRequest,
  bit32                     status,
  bit32                     param,
  bit32                     sspTag
  )
{
  agsaLLRoot_t     *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t *pDevice;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5i");

  if (agNULL == pRequest)
  {
    SA_DBG1(("siAbnormal: pRequest is NULL.\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5i");
    return;
  }

  /* If the request is still valid */
  if ( agTRUE == pRequest->valid )
  {
    /* get device */

    SA_ASSERT((pRequest->pIORequestContext->osData != pRequest->pIORequestContext->sdkData), "pIORequestContext");

    pDevice = pRequest->pDevice;

    /* remove the IO request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;

    smTrace(hpDBG_VERY_LOUD,"P6",status );
     /* TP:P6 siAbnormal status */
    smTrace(hpDBG_VERY_LOUD,"P7",param );
     /* TP:P7 siAbnormal param */
    /* process different request type */
    switch (pRequest->requestType & AGSA_REQTYPE_MASK)
    {
      case AGSA_SSP_REQTYPE:
      {
        (*(ossaSSPCompletedCB_t)(pRequest->completionCB))(agRoot,
                                                          pRequest->pIORequestContext,
                                                          status,
                                                          param,
                                                          agNULL,
                                                          (bit16)(sspTag & SSPTAG_BITS),
                                                          ((sspTag & SSP_AGR_S_BIT)? (1 << 0) : 0));

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* Delete the request from the pendingIORequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
        pRequest->valid = agFALSE;
        /* return the request to free pool */
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("siAbnormal: saving pRequest (%p) for later use\n", pRequest));
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
      case AGSA_SATA_REQTYPE:
      {
        SA_DBG5(("siAbnormal: SATA \n"));

        if ( agNULL != pRequest->pIORequestContext )
        {
          SA_DBG5(("siAbnormal: Calling SATACompletedCB\n"));

          (*(ossaSATACompletedCB_t)(pRequest->completionCB))(agRoot,
                                                             pRequest->pIORequestContext,
                                                             status,
                                                             agNULL,
                                                             param,
                                                             agNULL);
        }

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* Delete the request from the pendingIORequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
        /* return the request to free pool */
        pRequest->valid = agFALSE;
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("siAbnormal: saving pRequest (%p) for later use\n", pRequest));
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
      case AGSA_SMP_REQTYPE:
      {
        if ( agNULL != pRequest->pIORequestContext )
        {
          (*(ossaSMPCompletedCB_t)(pRequest->completionCB))(agRoot,
                                                            pRequest->pIORequestContext,
                                                            status,
                                                            param,
                                                            agNULL);
        }

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* Delete the request from the pendingSMPRequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
        /* return the request to free pool */
        pRequest->valid = agFALSE;
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("siAbnormal: saving pRequest (%p) for later use\n", pRequest));
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
      default:
      {
        SA_DBG1(("siAbnormal: unknown request type (%x) is completed. Tag=0x%x\n", pRequest->requestType, pRequest->HTag));
        break;
      }
    }

  }
  else
  {
    SA_DBG1(("siAbnormal: The request is not valid any more. Tag=0x%x\n", pRequest->HTag));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5i");

  return;
}


/******************************************************************************/
/*! \brief Routine to handle abnormal DIF completed IO/SMP event
 *
 *  Handle abnormal completed IO/SMP
 *
 *  \param agRoot   handles for this instance of SAS/SATA hardware
 *  \param pRequest Pointer of IO request of the IO
 *  \param status   status of the IO
 *  \param param    Length
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siDifAbnormal(
  agsaRoot_t          *agRoot,
  agsaIORequestDesc_t *pRequest,
  bit32               status,
  bit32               param,
  bit32               sspTag,
  bit32               *pMsg1
  )
{
  agsaLLRoot_t     *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t *pDevice;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2S");

  if (agNULL == pRequest)
  {
    SA_DBG1(("siDifAbnormal: pRequest is NULL.\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2S");
    return;
  }

  /* If the request is still valid */
  if ( agTRUE == pRequest->valid )
  {
    /* get device */
    pDevice = pRequest->pDevice;

    /* remove the IO request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;

    smTrace(hpDBG_VERY_LOUD,"P6",status );
     /* TP:P6 siDifAbnormal status */
    /* process different request type */
    switch (pRequest->requestType & AGSA_REQTYPE_MASK)
    {
      case AGSA_SSP_REQTYPE:
      {
        agsaDifDetails_t          agDifDetails;
        agsaSSPCompletionDifRsp_t    *pIomb;
        pIomb = (agsaSSPCompletionDifRsp_t *)pMsg1;
        si_memset(&agDifDetails, 0, sizeof(agDifDetails));

        OSSA_READ_LE_32(agRoot, &agDifDetails.UpperLBA,           pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,UpperLBA ));
        OSSA_READ_LE_32(agRoot, &agDifDetails.LowerLBA,           pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,LowerLBA ));
        OSSA_READ_LE_32(agRoot, &agDifDetails.sasAddressHi,       pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,sasAddressHi ));
        OSSA_READ_LE_32(agRoot, &agDifDetails.sasAddressLo,       pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,sasAddressLo));
        OSSA_READ_LE_32(agRoot, &agDifDetails.ExpectedCRCUDT01,   pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,ExpectedCRCUDT01 ));
        OSSA_READ_LE_32(agRoot, &agDifDetails.ExpectedUDT2345,    pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,ExpectedUDT2345));
        OSSA_READ_LE_32(agRoot, &agDifDetails.ActualCRCUDT01,     pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,ActualCRCUDT01 ));
        OSSA_READ_LE_32(agRoot, &agDifDetails.ActualUDT2345,      pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,ActualUDT2345));
        OSSA_READ_LE_32(agRoot, &agDifDetails.DIFErrDevID,        pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,DIFErrDevID ));
        OSSA_READ_LE_32(agRoot, &agDifDetails.ErrBoffsetEDataLen, pIomb, OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t,ErrBoffsetEDataLen ));
        agDifDetails.frame = (void *)(bit8*)(pIomb+ OSSA_OFFSET_OF(agsaSSPCompletionDifRsp_t, EDATA_FRM));

        (*(ossaSSPCompletedCB_t)(pRequest->completionCB))(agRoot,
                                                          pRequest->pIORequestContext,
                                                          status,
                                                          param,
                                                          &agDifDetails,
                                                          (bit16)(sspTag & SSPTAG_BITS),
                    ((sspTag & SSP_AGR_S_BIT)? (1 << 0) : 0));

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        pRequest->valid = agFALSE;
        /* Delete the request from the pendingIORequests */
        saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));

        /* return the request to free pool */
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("siDifAbnormal: saving pRequest (%p) for later use\n", pRequest));
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
      default:
      {
        SA_DBG1(("siDifAbnormal: unknown request type (%x) is completed. Tag=0x%x\n", pRequest->requestType, pRequest->HTag));
        break;
      }
    }

  }
  else
  {
    SA_DBG1(("siDifAbnormal: The request is not valid any more. Tag=0x%x\n", pRequest->HTag));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2S");

  return;
}


/******************************************************************************/
/*! \brief Routine to handle for received SMP response event
 *
 *  The handle for received SMP response event
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param pIomb       Pointer of payload of IOMB
 *  \param payloadSize size of the payload
 *  \param tag         the tag of the request SMP
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siSMPRespRcvd(
  agsaRoot_t              *agRoot,
  agsaSMPCompletionRsp_t  *pIomb,
  bit32                   payloadSize,
  bit32                   tag
  )
{
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaFrameHandle_t       frameHandle;
  agsaIORequestDesc_t     *pRequest;
  agsaDeviceDesc_t        *pDevice;
  agsaPort_t              *pPort;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5j");

  /* get the request */
  pRequest = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  SA_ASSERT(pRequest, "pRequest");

  /* get the port */
  pPort = pRequest->pPort;
  SA_ASSERT(pPort, "pPort");

  if (pRequest->IRmode == 0)
  {
    /* get frame handle - direct response mode */
    frameHandle = (agsaFrameHandle_t)(&(pIomb->SMPrsp[0]));
#if defined(SALLSDK_DEBUG)
    SA_DBG3(("saSMPRespRcvd(direct): smpRspPtr=0x%p - len=0x%x\n",
        frameHandle,
        payloadSize
        ));
#endif /* SALLSDK_DEBUG */
  }
  else
  {
    /* indirect response mode */
    frameHandle = agNULL;
  }

  /* If the request is still valid */
  if ( agTRUE == pRequest->valid )
  {
    /* get device */
    pDevice = pRequest->pDevice;
    SA_ASSERT(pDevice, "pDevice");

    /* Delete the request from the pendingSMPRequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* If the request is from OS layer */
    if ( agNULL != pRequest->pIORequestContext )
    {
      if (agNULL == frameHandle)
      {
        /* indirect mode */
        /* call back with success */
        (*(ossaSMPCompletedCB_t)(pRequest->completionCB))(agRoot, pRequest->pIORequestContext, OSSA_IO_SUCCESS, payloadSize, frameHandle);
      }
      else
      {
        /* direct mode */
        /* call back with success */
        (*(ossaSMPCompletedCB_t)(pRequest->completionCB))(agRoot, pRequest->pIORequestContext, OSSA_IO_SUCCESS, payloadSize, frameHandle);
      }
    }

    /* remove the IO request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;
    saRoot->IOMap[tag].agContext = agNULL;
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("siSMPRespRcvd: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5j");

  return;
}

/******************************************************************************/
/*! \brief Routine to handle for received Phy Up event
 *
 *  The handle for received Phy Up event
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param phyId for the Phy Up event happened
 *  \param agSASIdentify is the remote phy Identify
 *  \param portId is the port context index of the phy up event
 *  \param deviceId is the device context index
 *  \param linkRate link up rate from SPC
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siEventPhyUpRcvd(
  agsaRoot_t        *agRoot,
  bit32             phyId,
  agsaSASIdentify_t *agSASIdentify,
  bit32             portId,
  bit32             npipps,
  bit8              linkRate
  )
{
  agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaPhy_t         *pPhy = &(saRoot->phys[phyId]);
  agsaPort_t        *pPort;
  agsaSASIdentify_t remoteIdentify;
  agsaPortContext_t *agPortContext;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5k");

  /* Read remote SAS Identify from response message and save it */
  remoteIdentify = *agSASIdentify;

  /* get port context from portMap */
  SA_DBG2(("siEventPhyUpRcvd:PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[portId & PORTID_MASK].PortID,saRoot->PortMap[portId & PORTID_MASK].PortStatus,saRoot->PortMap[portId & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId].PortContext;

  SA_DBG2(("siEventPhyUpRcvd: portID %d PortContext %p linkRate 0x%X\n", portId, agPortContext,linkRate));
  if (smIS_SPCV8006(agRoot))
  {
    SA_DBG1(("siEventPhyUpRcvd: SAS_PHY_UP received for SATA Controller\n"));
    return;
  }

  if (agNULL != agPortContext)
  {
    /* existing port */
    pPort = (agsaPort_t *) (agPortContext->sdkData);
    pPort->portId = portId;

    /* include the phy to the port */
    pPort->phyMap[phyId] = agTRUE;
    /* Set the port for the phy */
    saRoot->phys[phyId].pPort = pPort;

    /* Update port state */
    if (OSSA_PORT_VALID == (npipps & PORT_STATE_MASK))
    {
      pPort->status &= ~PORT_INVALIDATING;
      saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
      SA_DBG1(("siEventPhyUpRcvd: portID %d PortContext %p, hitting workaround\n", portId, agPortContext));
    }
  }
  else
  {
    ossaSingleThreadedEnter(agRoot, LL_PORT_LOCK);
    /* new port */
    /* Allocate a free port */
    pPort = (agsaPort_t *) saLlistGetHead(&(saRoot->freePorts));
    if (agNULL != pPort)
    {
      /* Acquire port list lock */
      saLlistRemove(&(saRoot->freePorts), &(pPort->linkNode));

      /* setup the port data structure */
      pPort->portContext.osData = agNULL;
      pPort->portContext.sdkData = pPort;

      /* Add to valid port list */
      saLlistAdd(&(saRoot->validPorts), &(pPort->linkNode));
      /* Release port list lock */
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);

      /* include the phy to the port */
      pPort->phyMap[phyId] = agTRUE;
      /* Set the port for the phy */
      saRoot->phys[phyId].pPort = pPort;

      /* Setup portMap based on portId */
      saRoot->PortMap[portId].PortID = portId;
      saRoot->PortMap[portId].PortContext = &(pPort->portContext);
      pPort->portId = portId;

      SA_DBG3(("siEventPhyUpRcvd: NewPort portID %d PortContext %p\n", portId, saRoot->PortMap[portId].PortContext));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);
      /* pPort is agNULL*/
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5k");
      return;
    }

    if (OSSA_PORT_VALID == (npipps & PORT_STATE_MASK))
    {
      pPort->status &= ~PORT_INVALIDATING;
      saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
    }
    else
    {
      SA_DBG1(("siEventPhyUpRcvd: PortInvalid portID %d PortContext %p\n", portId, saRoot->PortMap[portId].PortContext));
    }
  }

  /* adjust the bit fields before callback */
  phyId = (linkRate << SHIFT8) | phyId;
  /* report PhyId, NPIP, PortState */
  phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
  ossaHwCB(agRoot, &(pPort->portContext), OSSA_HW_EVENT_SAS_PHY_UP, phyId, agNULL, &remoteIdentify);

  /* set PHY_UP status */
  PHY_STATUS_SET(pPhy, PHY_UP);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5k");

  /* return */
  return;
}

/******************************************************************************/
/*! \brief Routine to handle for received SATA signature event
 *
 *  The handle for received SATA signature event
 *
 *  \param agRoot   handles for this instance of SAS/SATA hardware
 *  \param phyId    the phy id of the phy received the frame
 *  \param pMsg     the pointer to the message payload
 *  \param portId   the port context index of the phy up event
 *  \param deviceId the device context index
 *  \param linkRate link up rate from SPC
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siEventSATASignatureRcvd(
  agsaRoot_t    *agRoot,
  bit32         phyId,
  void          *pMsg,
  bit32         portId,
  bit32         npipps,
  bit8          linkRate
  )
{
  agsaLLRoot_t                *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaPhy_t                   *pPhy = &(saRoot->phys[phyId]);
  agsaPort_t                  *pPort = agNULL;
  agsaPortContext_t           *agPortContext;
#if defined(SALLSDK_DEBUG)
  agsaFisRegDeviceToHost_t    *fisD2H;
  /* Read the D2H FIS */
  fisD2H = (agsaFisRegDeviceToHost_t *)pMsg;
#endif  /* SALLSDK_DEBUG */

  smTraceFuncEnter(hpDBG_VERY_LOUD,"5m");

  SA_DBG5(("siEventSATASignatureRcvd: About to read the signatureFIS data\n"));


  SA_DBG5(("agsaFisRegDeviceToHost_t:\n"));
  SA_DBG5(("  fisType         = %x\n", fisD2H->h.fisType));
  SA_DBG5(("  i_pmPort        = %x\n", fisD2H->h.i_pmPort));
  SA_DBG5(("  status          = %x\n", fisD2H->h.status));
  SA_DBG5(("  error           = %x\n", fisD2H->h.error));

  SA_DBG5(("  lbaLow          = %x\n", fisD2H->d.lbaLow));
  SA_DBG5(("  lbaMid          = %x\n", fisD2H->d.lbaMid));
  SA_DBG5(("  lbaHigh         = %x\n", fisD2H->d.lbaHigh));
  SA_DBG5(("  device          = %x\n", fisD2H->d.device));

  SA_DBG5(("  lbaLowExp       = %x\n", fisD2H->d.lbaLowExp));
  SA_DBG5(("  lbaMidExp       = %x\n", fisD2H->d.lbaMidExp));
  SA_DBG5(("  lbaHighExp      = %x\n", fisD2H->d.lbaHighExp));
  SA_DBG5(("  reserved4       = %x\n", fisD2H->d.reserved4));

  SA_DBG5(("  sectorCount     = %x\n", fisD2H->d.sectorCount));
  SA_DBG5(("  sectorCountExp  = %x\n", fisD2H->d.sectorCountExp));
  SA_DBG5(("  reserved5       = %x\n", fisD2H->d.reserved5));
  SA_DBG5(("  reserved6       = %x\n", fisD2H->d.reserved6));

  SA_DBG5(("  reserved7 (32)  = %08X\n", fisD2H->d.reserved7));

  SA_DBG5(("siEventSATASignatureRcvd: GOOD signatureFIS data\n"));

#if defined(SALLSDK_DEBUG)
  /* read signature */
  pPhy->remoteSignature[0] = (bit8) fisD2H->d.sectorCount;
  pPhy->remoteSignature[1] = (bit8) fisD2H->d.lbaLow;
  pPhy->remoteSignature[2] = (bit8) fisD2H->d.lbaMid;
  pPhy->remoteSignature[3] = (bit8) fisD2H->d.lbaHigh;
  pPhy->remoteSignature[4] = (bit8) fisD2H->d.device;
#endif

  /* get port context from portMap */
  SA_DBG2(("siEventSATASignatureRcvd:PortID 0x%x PortStatus 0x%x PortContext %p\n",saRoot->PortMap[portId & PORTID_MASK].PortID,saRoot->PortMap[portId & PORTID_MASK].PortStatus,saRoot->PortMap[portId & PORTID_MASK].PortContext));
  agPortContext = (agsaPortContext_t *)saRoot->PortMap[portId].PortContext;

  SA_DBG2(("siEventSATASignatureRcvd: portID %d PortContext %p\n", portId, agPortContext));

  if (agNULL != agPortContext)
  {
    /* exist port */
    pPort = (agsaPort_t *) (agPortContext->sdkData);
    pPort->portId = portId;

    /* include the phy to the port */
    pPort->phyMap[phyId] = agTRUE;
    /* Set the port for the phy */
    saRoot->phys[phyId].pPort = pPort;
  }
  else
  {
    ossaSingleThreadedEnter(agRoot, LL_PORT_LOCK);
    /* new port */
    /* Allocate a free port */
    pPort = (agsaPort_t *) saLlistGetHead(&(saRoot->freePorts));
    if (agNULL != pPort)
    {
      /* Acquire port list lock */
      saLlistRemove(&(saRoot->freePorts), &(pPort->linkNode));

      /* setup the port data structure */
      pPort->portContext.osData = agNULL;
      pPort->portContext.sdkData = pPort;

      /* Add to valid port list */
      saLlistAdd(&(saRoot->validPorts), &(pPort->linkNode));
      /* Release port list lock */
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);

      /* include the phy to the port */
      pPort->phyMap[phyId] = agTRUE;
      /* Set the port for the phy */
      saRoot->phys[phyId].pPort = pPort;

      /* Setup portMap based on portId */
      saRoot->PortMap[portId].PortID = portId;
      saRoot->PortMap[portId].PortContext = &(pPort->portContext);
      pPort->portId = portId;
      SA_DBG3(("siEventSATASignatureRcvd: NewPort portID %d portContect %p\n", portId, saRoot->PortMap[portId].PortContext));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);
      /* pPort is agNULL*/
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5m");
      return;
    }

    if (OSSA_PORT_VALID == (npipps & PORT_STATE_MASK))
    {
      pPort->status &= ~PORT_INVALIDATING;
      saRoot->PortMap[portId].PortStatus  &= ~PORT_INVALIDATING;
    }
    else
    {
      SA_DBG1(("siEventSATASignatureRcvd: PortInvalid portID %d PortContext %p\n", portId, saRoot->PortMap[portId].PortContext));
    }
  }

  /* adjust the bit fields before callback */
  phyId = (linkRate << SHIFT8) | phyId;
  /* report PhyId, NPIP, PortState */
  phyId |= (npipps & PHY_IN_PORT_MASK) | ((npipps & PORT_STATE_MASK) << SHIFT16);
  ossaHwCB(agRoot, &(pPort->portContext), OSSA_HW_EVENT_SATA_PHY_UP, phyId, agNULL, pMsg);

  /* set PHY_UP status */
  PHY_STATUS_SET(pPhy, PHY_UP);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5m");

  /* return */
  return;
}


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
GLOBAL bit32 mpiParseOBIomb(
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
#ifdef SA_ENABLE_PCI_TRIGGER
          if( saRoot->swConfig.PCI_trigger & PCI_TRIGGER_COAL_IOMB_ERROR )
          {
            siPCITriger(agRoot);
          }
#endif /* SA_ENABLE_PCI_TRIGGER */
          return(AGSA_RC_FAILURE);
        }
        SA_ASSERT((pRequest->valid), "pRequest->valid");

#ifdef SA_ENABLE_PCI_TRIGGER
        if(!pRequest->valid)
        {
          if( saRoot->swConfig.PCI_trigger & PCI_TRIGGER_COAL_INVALID )
          {
            siPCITriger(agRoot);
          }
        }
#endif /* SA_ENABLE_PCI_TRIGGER */


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
    case OPC_OUB_GET_DEV_HANDLE:
    {
      SA_DBG3(("mpiParseOBIomb, GET_DEV_HANDLE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDevHandleRsp(agRoot, (agsaGetDevHandleRsp_t *)pMsg1);
      break;
    }
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
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_FLASH_OP_EXT_RSP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiFwExtFlashUpdateRsp(agRoot, (agsaFwFlashOpExtRsp_t *)pMsg1);
      break;
    }
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
    case OPC_OUB_GENERAL_EVENT:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_GENERAL_EVENT Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGeneralEventRsp(agRoot, (agsaGeneralEventRsp_t *)pMsg1);
      break;
    }
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
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_SAS_HW_EVENT_ACK Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSASHwEventAckRsp(agRoot, (agsaSASHwEventAckRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_PORT_CONTROL:
    {
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_PORT_CONTROL Response received IOMB=%p\n", pMsg1));
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
    case OPC_OUB_GET_DEVICE_STATE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_GET_DEVICE_STATE received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetDeviceStateRsp(agRoot, (agsaGetDeviceStateRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SET_DEV_INFO:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_DEV_INFO received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSetDevInfoRsp(agRoot, (agsaSetDeviceInfoRsp_t *)pMsg1);
      break;
    }
    case OPC_OUB_SAS_RE_INITIALIZE:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SAS_RE_INITIALIZE received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSasReInitializeRsp(agRoot, (agsaSasReInitializeRsp_t *)pMsg1);
      break;
    }

    case OPC_OUB_SGPIO_RESPONSE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SGPIO_RESPONSE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiSGpioRsp(agRoot, (agsaSGpioRsp_t *)pMsg1);
      break;
    }

    case OPC_OUB_PCIE_DIAG_EXECUTE:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_PCIE_DIAG_EXECUTE Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiPCIeDiagExecuteRsp(agRoot, (agsaPCIeDiagExecuteRsp_t *)pMsg1);
      break;
    }

    case OPC_OUB_GET_VIST_CAP_RSP:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_INB_GET_VIST_CAP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiGetVHistRsp(agRoot, (agsaGetVHistCapRsp_t *)pMsg1);
      break;
    }
    case 2104:
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
        /* process the response message */
        mpiGetVHistRsp(agRoot, (agsaGetVHistCapRsp_t *)pMsg1);
      }  
      else
      {
        SA_DBG1(("mpiParseOBIomb, 2104  Response received IOMB=%p\n", pMsg1));
        /* process the response message */
      }
      break;
    }
    case OPC_OUB_SET_CONTROLLER_CONFIG:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_SET_CONTROLLER_CONFIG Response received IOMB=%p\n", pMsg1));
      mpiSetControllerConfigRsp(agRoot, (agsaSetControllerConfigRsp_t *)pMsg1);
      break;
    }
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
    case OPC_OUB_DEK_MANAGEMENT:
    {
      SA_DBG3(("mpiParseOBIomb, OPC_OUB_DEK_MANAGEMENT Response received IOMB=%p\n", pMsg1));
      mpiDekManagementRsp(agRoot, (agsaDekManagementRsp_t *)pMsg1);
      break;
    }
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
    case OPC_OUB_DIF_ENC_OFFLOAD_RSP:
    {
      SA_ASSERT((smIS_SPCV(agRoot)), "smIS_SPCV");
      SA_DBG1(("mpiParseOBIomb, OPC_OUB_DIF_ENC_OFFLOAD_RSP Response received IOMB=%p\n", pMsg1));
      /* process the response message */
      mpiDifEncOffloadRsp(agRoot, (agsaDifEncOffloadRspV_t *)pMsg1);
      break;
    }
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


/******************************************************************************/
/*! \brief SPC MPI SATA Completion
 *
 *  This function handles the SATA completion.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb1       Pointer of Message1
 *  \param bc           buffer count
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL FORCEINLINE
bit32 mpiSATACompletion(
  agsaRoot_t    *agRoot,
  bit32         *pIomb1
  )
{
  bit32                     ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t              *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32                     status;
  bit32                     tag;
  bit32                     param;
  agsaIORequestDesc_t       *pRequest;
  bit32                     *agFirstDword;
  bit32                     *pResp;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2s");

  OSSA_READ_LE_32(AGROOT, &tag, pIomb1, OSSA_OFFSET_OF(agsaSATACompletionRsp_t, tag)) ;
  OSSA_READ_LE_32(AGROOT, &status, pIomb1, OSSA_OFFSET_OF(agsaSATACompletionRsp_t, status)) ;
  OSSA_READ_LE_32(AGROOT, &param, pIomb1, OSSA_OFFSET_OF(agsaSATACompletionRsp_t, param)) ;

  SA_DBG3(("mpiSATACompletion: start, HTAG=0x%x\n", tag));

  /* get IOrequest from IOMap */
  pRequest  = (agsaIORequestDesc_t*)saRoot->IOMap[tag].IORequest;
  SA_ASSERT((pRequest), "pRequest");

  if(agNULL == pRequest)
  {
    SA_DBG1(("mpiSATACompletion: agNULL == pRequest tag 0x%X status 0x%X\n",tag, status ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2s");
    return AGSA_RC_FAILURE;
  }

  SA_ASSERT((pRequest->valid), "pRequest->valid");
  if(!pRequest->valid)
  {
    SA_DBG1(("mpiSATACompletion: not valid IOMB tag=0x%x status=0x%x param=0x%x Device =0x%x\n", tag, status, param,
    pRequest->pDevice ? pRequest->pDevice->DeviceMapIndex : -1));
  }

  switch (status)
  {
    case OSSA_IO_SUCCESS:
    {
      SA_DBG3(("mpiSATACompletion: OSSA_IO_SUCCESS, param=0x%x\n", param));
      if (!param)
      {
        /* SATA request completion */
        siIODone( agRoot, pRequest, OSSA_IO_SUCCESS, 0);
      }
      else
      {
        /* param number bytes of SATA Rsp */
        agFirstDword  = &pIomb1[3];
        pResp         = &pIomb1[4];

        /* CB function to the up layer */
        /* Response Length not include firstDW */
        saRoot->IoErrorCount.agOSSA_IO_COMPLETED_ERROR_SCSI_STATUS++;
        SA_DBG2(("mpiSATACompletion: param 0x%x agFirstDwordResp 0x%x Resp 0x%x tag 0x%x\n",param,*agFirstDword,*pResp ,tag));
        siEventSATAResponseWtDataRcvd(agRoot, pRequest, agFirstDword, pResp, (param - 4));
      }

      break;
    }
    case OSSA_IO_ABORTED:
    {
      SA_DBG2(("mpiSATACompletion: OSSA_IO_ABORTED tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_ABORTED++;
      siAbnormal(agRoot, pRequest, status, param, 0);
      break;
    }
    case OSSA_IO_UNDERFLOW:
    {
      /* SATA Completion with error */
      SA_DBG1(("mpiSATACompletion, OSSA_IO_UNDERFLOW tag 0x%X\n", tag));
      /*underflow means underrun, treat it as success*/
      saRoot->IoErrorCount.agOSSA_IO_UNDERFLOW++;
      siAbnormal(agRoot, pRequest, status, param, 0);
      break;
    }
    case OSSA_IO_NO_DEVICE:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_NO_DEVICE tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_NO_DEVICE++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_ERROR_BREAK:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_ERROR_BREAK SPC tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_BREAK++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_ERROR_PHY_NOT_READY:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_ERROR_PHY_NOT_READY tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_PHY_NOT_READY++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_BREAK:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_BREAK SPC tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_BREAK++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_ERROR_NAK_RECEIVED:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_ERROR_NAK_RECEIVED tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_NAK_RECEIVED++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_ERROR_DMA:
    {
       SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_ERROR_DMA tag 0x%X\n", tag));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_DMA++;
       siAbnormal(agRoot, pRequest, status, 0, 0);
       break;
    }
    case OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_OPEN_RETRY_TIMEOUT:
    {
       SA_DBG1(("mpiSATACompletion, OSSA_IO_XFER_OPEN_RETRY_TIMEOUT tag 0x%X\n", tag));
       saRoot->IoErrorCount.agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT++;
       siAbnormal(agRoot, pRequest, status, 0, 0);
       break;
    }
    case OSSA_IO_PORT_IN_RESET:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_PORT_IN_RESET tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_PORT_IN_RESET++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_DS_NON_OPERATIONAL:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_DS_NON_OPERATIONAL tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_DS_NON_OPERATIONAL++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_DS_IN_RECOVERY:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_DS_IN_RECOVERY tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_DS_IN_RECOVERY++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_DS_IN_ERROR:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_DS_IN_ERROR tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_DS_IN_ERROR++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }

    case OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_ABORT_IN_PROGRESS:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_ABORT_IN_PROGRESS tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_ABORT_IN_PROGRESS++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_ABORT_DELAYED:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_ABORT_DELAYED tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_ABORT_DELAYED++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED HTAG = 0x%x\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO tag 0x%x\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO++;
      siAbnormal(agRoot, pRequest, status, 0, 0 );
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST tag 0x%x\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST++;
      siAbnormal(agRoot, pRequest, status, 0, 0 );
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE tag 0x%x\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE++;
      siAbnormal(agRoot, pRequest, status, 0, 0 );
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
    {
      SA_DBG1(("mpiSATACompletion, OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED tag 0x%x\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED++;
      siAbnormal(agRoot, pRequest, status, 0, 0 );
      break;
    }
    case OSSA_IO_DS_INVALID:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_DS_INVALID tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_DS_INVALID++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_MPI_IO_RQE_BUSY_FULL:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_MPI_IO_RQE_BUSY_FULL tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_MPI_IO_RQE_BUSY_FULL++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
#ifdef REMOVED
    case OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN tag 0x%x\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFER_ERR_EOB_DATA_OVERRUN++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
#endif
    case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
    {
      SA_DBG1(("mpiSATACompletion: OPC_OUB_SATA_COMP:OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE \n"));
      saRoot->IoErrorCount.agOSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_MPI_ERR_ATAPI_DEVICE_BUSY:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_MPI_ERR_ATAPI_DEVICE_BUSY tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_MPI_ERR_ATAPI_DEVICE_BUSY++;
      siAbnormal(agRoot, pRequest, status, param, 0 );
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_IV_MISMATCH++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }

    case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }
    case OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE:
    {
      SA_DBG1(("mpiSATACompletion: OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE tag 0x%X\n", tag));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS++;
      siAbnormal(agRoot, pRequest, status, 0, 0);
      break;
    }

    default:
    {
      SA_DBG1(("mpiSATACompletion: Unknown status  0x%x tag 0x%x\n", status, tag));
      saRoot->IoErrorCount.agOSSA_IO_UNKNOWN_ERROR++;
      siAbnormal(agRoot, pRequest, status, param, 0);
      break;
    }
  }

  /* The HTag should equal to the IOMB tag */
  if (pRequest->HTag != tag)
  {
    SA_DBG1(("mpiSATACompletion: Error Htag %d not equal IOMBtag %d\n", pRequest->HTag, tag));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2s");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SSP Completion
 *
 *  This function handles the SSP completion.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb1       Pointer of Message1
 *  \param bc           buffer count
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL FORCEINLINE
bit32 mpiSSPCompletion(
  agsaRoot_t    *agRoot,
  bit32         *pIomb1
  )
{
  agsaLLRoot_t              *saRoot   = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaSSPCompletionRsp_t    *pIomb    = (agsaSSPCompletionRsp_t *)pIomb1;
  agsaIORequestDesc_t       *pRequest = agNULL;
  agsaSSPResponseInfoUnit_t *pRespIU  = agNULL;
  bit32                      tag      = 0;
  bit32                      sspTag   = 0;
  bit32                      status, param = 0;
  bit32                      ret = AGSA_RC_SUCCESS;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "5A");

  /* get Tag */
  OSSA_READ_LE_32(agRoot, &tag, pIomb, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, tag));
  OSSA_READ_LE_32(agRoot, &status, pIomb, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, status));
  OSSA_READ_LE_32(agRoot, &param, pIomb, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, param));
  OSSA_READ_LE_32(agRoot, &sspTag, pIomb, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, SSPTag));
  /* get SSP_START IOrequest from IOMap */
  pRequest = (agsaIORequestDesc_t *)saRoot->IOMap[tag].IORequest;
  SA_ASSERT((pRequest), "pRequest");

  if(pRequest == agNULL)
  {
    SA_DBG1(("mpiSSPCompletion,AGSA_RC_FAILURE SSP Resp IOMB tag=0x%x, status=0x%x, param=0x%x, SSPTag=0x%x\n", tag, status, param, sspTag));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "5A");
    return(AGSA_RC_FAILURE);
  }
  SA_ASSERT((pRequest->valid), "pRequest->valid");

  if(!pRequest->valid)
  {
    SA_DBG1(("mpiSSPCompletion, SSP Resp IOMB tag=0x%x, status=0x%x, param=0x%x, SSPTag=0x%x Device =0x%x\n", tag, status, param, sspTag,
    pRequest->pDevice ? pRequest->pDevice->DeviceMapIndex : -1));
  }

  switch (status)
  {
    case OSSA_IO_SUCCESS:
    {
      if (!param)
      {
        /* Completion of SSP without Response Data */
        siIODone( agRoot, pRequest, OSSA_IO_SUCCESS, sspTag);
      }
      else
      {
        /* Get SSP Response with Response Data */
        pRespIU = (agsaSSPResponseInfoUnit_t *)&(pIomb->SSPrsp);
        if (pRespIU->status == 0x02 || pRespIU->status == 0x18 ||
            pRespIU->status == 0x30 || pRespIU->status == 0x40 )
        {
          /* SCSI status is CHECK_CONDITION, RESV_CONFLICT, ACA_ACTIVE, TASK_ABORTED */
          saRoot->IoErrorCount.agOSSA_IO_COMPLETED_ERROR_SCSI_STATUS++;
          SA_DBG2(("mpiSSPCompletion: pRespIU->status 0x%x tag 0x%x\n", pRespIU->status,tag));
        }
        siEventSSPResponseWtDataRcvd(agRoot, pRequest, pRespIU, param, sspTag);
      }

      break;
    }

    case OSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME:
    {
      SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
      saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME++;
      /* Get SSP Response with Response Data */
      pRespIU = (agsaSSPResponseInfoUnit_t *)&(pIomb->SSPrsp);
      if (pRespIU->status == 0x02 || pRespIU->status == 0x18 ||
          pRespIU->status == 0x30 || pRespIU->status == 0x40 )
      {
        /* SCSI status is CHECK_CONDITION, RESV_CONFLICT, ACA_ACTIVE, TASK_ABORTED */
        saRoot->IoErrorCount.agOSSA_IO_COMPLETED_ERROR_SCSI_STATUS++;
        SA_DBG2(("mpiSSPCompletion: pRespIU->status 0x%x tag 0x%x\n", pRespIU->status,tag));
      }
      siEventSSPResponseWtDataRcvd(agRoot, pRequest, pRespIU, param, sspTag);

      break;
     }

     case OSSA_IO_ABORTED:
     {
#ifdef SALL_API_TEST
       saRoot->LLCounters.IOCounter.numSSPAborted++;
       SA_DBG3(("mpiSSPCompletion, OSSA_IO_ABORTED Response received IOMB=%p %d\n",
              pIomb1, saRoot->LLCounters.IOCounter.numSSPAborted));
#endif
       SA_DBG2(("mpiSSPCompletion, OSSA_IO_ABORTED IOMB tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_ABORTED++;
       /* SSP Abort CB */
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_UNDERFLOW:
     {
       /* SSP Completion with error */
       SA_DBG2(("mpiSSPCompletion, OSSA_IO_UNDERFLOW tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       /*saRoot->IoErrorCount.agOSSA_IO_UNDERFLOW++;*/
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_NO_DEVICE:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_NO_DEVICE tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_NO_DEVICE++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_BREAK:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_BREAK tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_BREAK++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_PHY_NOT_READY:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_PHY_NOT_READY tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_PHY_NOT_READY++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
     {
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_BREAK:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_BREAK tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_BREAK++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_NAK_RECEIVED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_NAK_RECEIVED tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_NAK_RECEIVED++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_DMA:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_DMA tag 0x%x ssptag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_DMA++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_OPEN_RETRY_TIMEOUT:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_OPEN_RETRY_TIMEOUT tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_UNEXPECTED_PHASE++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_ERROR_OFFSET_MISMATCH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_OFFSET_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_OFFSET_MISMATCH++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_PORT_IN_RESET:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_PORT_IN_RESET tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_PORT_IN_RESET++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_DS_NON_OPERATIONAL:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_DS_NON_OPERATIONAL tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_DS_NON_OPERATIONAL++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_DS_IN_RECOVERY:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_DS_IN_RECOVERY tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_DS_IN_RECOVERY++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_TM_TAG_NOT_FOUND:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_TM_TAG_NOT_FOUND tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_TM_TAG_NOT_FOUND++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_XFER_PIO_SETUP_ERROR:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_PIO_SETUP_ERROR tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_PIO_SETUP_ERROR++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_SSP_IU_ZERO_LEN_ERROR tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_DS_IN_ERROR:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_DS_IN_ERROR tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_DS_IN_ERROR++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_ABORT_IN_PROGRESS:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_ABORT_IN_PROGRESS tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_ABORT_IN_PROGRESS++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_ABORT_DELAYED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_ABORT_DELAYED tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_ABORT_DELAYED++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_INVALID_LENGTH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_INVALID_LENGTH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_INVALID_LENGTH++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT++;
       /* not allowed case. Therefore, return failed status */
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED HTAG = 0x%x ssptag = 0x%x\n", tag, sspTag));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED++;
       siAbnormal(agRoot, pRequest, status, 0, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_DS_INVALID:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_DS_INVALID tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_DS_INVALID++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_MPI_IO_RQE_BUSY_FULL:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_MPI_IO_RQE_BUSY_FULL tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_MPI_IO_RQE_BUSY_FULL++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_IV_MISMATCH++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_INTERNAL_RAM:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_INTERNAL_RAM tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_INTERNAL_RAM++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
#ifdef SA_TESTBASE_EXTRA
     /* TestBase */
     case OSSA_IO_HOST_BST_INVALID:
     {
        SA_DBG1(("mpiParseOBIomb, OPC_OUB_SSP_COMP: OSSA_IO_HOST_BST_INVALID 0x%x\n", status));
        siAbnormal(agRoot, pRequest, status, param, sspTag);
        break;
     }
#endif /*  SA_TESTBASE_EXTRA */
     case OSSA_IO_XFR_ERROR_DIF_MISMATCH:
     {
        SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DIF_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
        saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DIF_MISMATCH++;
        siDifAbnormal(agRoot, pRequest, status, param, sspTag, pIomb1);
        break;
     }
     case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH++;
       siDifAbnormal(agRoot, pRequest, status, param, sspTag, pIomb1);
       break;
     }
     case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH++;
       siDifAbnormal(agRoot, pRequest, status, param, sspTag, pIomb1);
       break;
     }
     case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH++;
       siDifAbnormal(agRoot, pRequest, status, param, sspTag, pIomb1);
       break;
     }
     case OSSA_IO_XFER_ERROR_DIF_INTERNAL_ERROR:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERROR_DIF_INTERNAL_ERROR tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERROR_DIF_INTERNAL_ERROR++;
       siDifAbnormal(agRoot, pRequest, status, param, sspTag, pIomb1);
       break;
     }
     case OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_XFER_ERR_EOB_DATA_OVERRUN++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     case OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED:
     {
       SA_DBG1(("mpiSSPCompletion: OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       saRoot->IoErrorCount.agOSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED++;
       siAbnormal(agRoot, pRequest, status, param, sspTag);
       break;
     }
     default:
     {
       SA_DBG1(("mpiSSPCompletion: Unknown tag 0x%x sspTag 0x%x status 0x%x param 0x%x\n", tag,sspTag,status,param));
       /* not allowed case. Therefore, return failed status */
       saRoot->IoErrorCount.agOSSA_IO_UNKNOWN_ERROR++;
       siAbnormal(agRoot, pRequest, OSSA_IO_FAILED, param, sspTag);
       break;
     }
   }

   smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "5A");
   return ret;
}
