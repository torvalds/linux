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
/*! \file sainit.c
 *  \brief The file implements the functions to initialize the LL layer
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
#define siTraceFileID 'F'
#endif

bit32 gLLDebugLevel         = 3;

#if defined(SALLSDK_DEBUG)
bit32 gLLDebugLevelSet      = 0; // block reinitialize from updating
bit32 gLLLogFuncDebugLevel  = 0;
bit32 gLLSoftResetCounter   = 0;
#endif

bit32 gPollForMissingInt;

#ifdef FW_EVT_LOG_TST
void  *eventLogAddress = NULL;
#endif

extern bit32 gWait_3;
extern bit32 gWait_2;
bit32 gFPGA_TEST = 0; // If set unblock fpga functions

/******************************************************************************/
/*! \brief Get the memory and lock requirement from LL layer
 *
 *  Get the memory and lock requirement from LL layer
 *
 *  \param agRoot             Handles for this instance of SAS/SATA hardware
 *  \param swConfig           Pointer to the software configuration
 *  \param memoryRequirement  Point to the data structure that holds the different
 *                                       chunks of memory that are required
 *  \param usecsPerTick       micro-seconds per tick for the LL layer
 *  \param maxNumLocks        maximum number of locks for the LL layer
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void saGetRequirements(
  agsaRoot_t              *agRoot,
  agsaSwConfig_t          *swConfig,
  agsaMemoryRequirement_t *memoryRequirement,
  bit32                   *usecsPerTick,
  bit32                   *maxNumLocks
  )
{
  bit32               memoryReqCount = 0;
  bit32               i;
  static mpiConfig_t  mpiConfig;
  static mpiMemReq_t  mpiMemoryRequirement;


  /* sanity check */
  SA_ASSERT((agNULL != swConfig), "");
  SA_ASSERT((agNULL != memoryRequirement), "");
  SA_ASSERT((agNULL != usecsPerTick), "");
  SA_ASSERT((agNULL != maxNumLocks), "");

  si_memset(&mpiMemoryRequirement, 0, sizeof(mpiMemReq_t));
  si_memset(&mpiConfig, 0, sizeof(mpiConfig_t));

  SA_DBG1(("saGetRequirements:agRoot %p swConfig %p memoryRequirement %p usecsPerTick %p maxNumLocks %p\n",agRoot, swConfig,memoryRequirement,usecsPerTick,maxNumLocks));
  SA_DBG1(("saGetRequirements: usecsPerTick 0x%x (%d)\n",*usecsPerTick,*usecsPerTick));

  /* Get Resource Requirements for SPC MPI */
  /* Set the default/specified requirements swConfig from TD layer */
  siConfiguration(agRoot, &mpiConfig, agNULL, swConfig);
  mpiRequirementsGet(&mpiConfig, &mpiMemoryRequirement);

  /* memory requirement for saRoot, CACHE memory */
  memoryRequirement->agMemory[LLROOT_MEM_INDEX].singleElementLength = sizeof(agsaLLRoot_t);
  memoryRequirement->agMemory[LLROOT_MEM_INDEX].numElements = 1;
  memoryRequirement->agMemory[LLROOT_MEM_INDEX].totalLength = sizeof(agsaLLRoot_t);
  memoryRequirement->agMemory[LLROOT_MEM_INDEX].alignment = sizeof(void *);
  memoryRequirement->agMemory[LLROOT_MEM_INDEX].type = AGSA_CACHED_MEM;
  memoryReqCount ++;

  SA_DBG1(("saGetRequirements: agMemory[LLROOT_MEM_INDEX] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[LLROOT_MEM_INDEX].singleElementLength,
           memoryRequirement->agMemory[LLROOT_MEM_INDEX].totalLength,
           memoryRequirement->agMemory[LLROOT_MEM_INDEX].alignment,
           memoryRequirement->agMemory[LLROOT_MEM_INDEX].type ));

  /* memory requirement for Device Links, CACHE memory */
  memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].singleElementLength = sizeof(agsaDeviceDesc_t);
  memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].numElements = swConfig->numDevHandles;
  memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].totalLength = sizeof(agsaDeviceDesc_t)
                                                                * swConfig->numDevHandles;
  memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].alignment = sizeof(void *);
  memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].type = AGSA_CACHED_MEM;
  memoryReqCount ++;
  SA_DBG1(("saGetRequirements: agMemory[DEVICELINK_MEM_INDEX] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].singleElementLength,
           memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].totalLength,
           memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].alignment,
           memoryRequirement->agMemory[DEVICELINK_MEM_INDEX].type ));

  /* memory requirement for IORequest Links, CACHE memory */
  memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].singleElementLength = sizeof(agsaIORequestDesc_t);
  /*
  Add SA_RESERVED_REQUEST_COUNT to guarantee quality of service
  */
  memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].numElements = swConfig->maxActiveIOs + SA_RESERVED_REQUEST_COUNT;
  memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].totalLength = sizeof(agsaIORequestDesc_t) *
                memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].numElements;
  memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].alignment = sizeof(void *);
  memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].type = AGSA_CACHED_MEM;
  memoryReqCount ++;

  SA_DBG1(("saGetRequirements: agMemory[IOREQLINK_MEM_INDEX] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].singleElementLength,
           memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].totalLength,
           memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].alignment,
           memoryRequirement->agMemory[IOREQLINK_MEM_INDEX].type ));

  /* memory requirement for Timer Links, CACHE memory */
  memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].singleElementLength = sizeof(agsaTimerDesc_t);
  memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].numElements = NUM_TIMERS;
  memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].totalLength = sizeof(agsaTimerDesc_t) * NUM_TIMERS;
  memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].alignment = sizeof(void *);
  memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].type = AGSA_CACHED_MEM;
  memoryReqCount ++;
  SA_DBG1(("saGetRequirements: agMemory[TIMERLINK_MEM_INDEX] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].singleElementLength,
           memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].totalLength,
           memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].alignment,
           memoryRequirement->agMemory[TIMERLINK_MEM_INDEX].type ));

#ifdef SA_ENABLE_TRACE_FUNCTIONS

  /* memory requirement for LL trace memory */
  memoryRequirement->agMemory[LL_FUNCTION_TRACE].singleElementLength = 1;
  memoryRequirement->agMemory[LL_FUNCTION_TRACE].numElements = swConfig->TraceBufferSize;
  memoryRequirement->agMemory[LL_FUNCTION_TRACE].totalLength = swConfig->TraceBufferSize;
  memoryRequirement->agMemory[LL_FUNCTION_TRACE].alignment = sizeof(void *);
  memoryRequirement->agMemory[LL_FUNCTION_TRACE].type = AGSA_CACHED_MEM;
  memoryReqCount ++;

  SA_DBG1(("saGetRequirements: agMemory[LL_FUNCTION_TRACE] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[LL_FUNCTION_TRACE].singleElementLength,
           memoryRequirement->agMemory[LL_FUNCTION_TRACE].totalLength,
           memoryRequirement->agMemory[LL_FUNCTION_TRACE].alignment,
           memoryRequirement->agMemory[LL_FUNCTION_TRACE].type ));

#endif /* END SA_ENABLE_TRACE_FUNCTIONS */

#ifdef FAST_IO_TEST
  {
  agsaMem_t *agMemory = memoryRequirement->agMemory;

  /* memory requirement for Super IO CACHE memory */
  agMemory[LL_FAST_IO].singleElementLength = sizeof(saFastRequest_t);
  agMemory[LL_FAST_IO].numElements = LL_FAST_IO_SIZE;
  agMemory[LL_FAST_IO].totalLength = LL_FAST_IO_SIZE *
                                     agMemory[LL_FAST_IO].singleElementLength;
  agMemory[LL_FAST_IO].alignment = sizeof(void*);
  agMemory[LL_FAST_IO].type = AGSA_CACHED_MEM;
  memoryReqCount ++;

  SA_DBG1(("saGetRequirements: agMemory[LL_FAST_IO] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[LL_FAST_IO].singleElementLength,
           memoryRequirement->agMemory[LL_FAST_IO].totalLength,
           memoryRequirement->agMemory[LL_FAST_IO].alignment,
           memoryRequirement->agMemory[LL_FAST_IO].type ));

  }
#endif

#ifdef SA_ENABLE_HDA_FUNCTIONS
  {
  agsaMem_t *agMemory = memoryRequirement->agMemory;

  /* memory requirement for HDA FW image */
  agMemory[HDA_DMA_BUFFER].singleElementLength = (1024 * 1024); /* must be greater than size of aap1 fw image */
  agMemory[HDA_DMA_BUFFER].numElements = 1;
  agMemory[HDA_DMA_BUFFER].totalLength = agMemory[HDA_DMA_BUFFER].numElements *
                                     agMemory[HDA_DMA_BUFFER].singleElementLength;
  agMemory[HDA_DMA_BUFFER].alignment = 32;
  agMemory[HDA_DMA_BUFFER].type = AGSA_DMA_MEM;
  memoryReqCount ++;
  SA_DBG1(("saGetRequirements: agMemory[HDA_DMA_BUFFER] singleElementLength = 0x%x totalLength = 0x%x align = 0x%x type %x\n",
           memoryRequirement->agMemory[HDA_DMA_BUFFER].singleElementLength,
           memoryRequirement->agMemory[HDA_DMA_BUFFER].totalLength,
           memoryRequirement->agMemory[HDA_DMA_BUFFER].alignment,
           memoryRequirement->agMemory[HDA_DMA_BUFFER].type ));
  }
#endif /* SA_ENABLE_HDA_FUNCTIONS */

  /* memory requirement for MPI MSGU layer, DMA memory */
  for ( i = 0; i < mpiMemoryRequirement.count; i ++ )
  {
    memoryRequirement->agMemory[memoryReqCount].singleElementLength = mpiMemoryRequirement.region[i].elementSize;
    memoryRequirement->agMemory[memoryReqCount].numElements         = mpiMemoryRequirement.region[i].numElements;
    memoryRequirement->agMemory[memoryReqCount].totalLength         = mpiMemoryRequirement.region[i].totalLength;
    memoryRequirement->agMemory[memoryReqCount].alignment           = mpiMemoryRequirement.region[i].alignment;
    memoryRequirement->agMemory[memoryReqCount].type                = mpiMemoryRequirement.region[i].type;
    SA_DBG1(("saGetRequirements:MPI agMemory[%d] singleElementLength = 0x%x  totalLength = 0x%x align = 0x%x type %x\n",
          memoryReqCount,
          memoryRequirement->agMemory[memoryReqCount].singleElementLength,
          memoryRequirement->agMemory[memoryReqCount].totalLength,
          memoryRequirement->agMemory[memoryReqCount].alignment,
          memoryRequirement->agMemory[memoryReqCount].type ));
    memoryReqCount ++;
  }


  /* requirement for locks */
  if (swConfig->param3 == agNULL)
  {
    *maxNumLocks = (LL_IOREQ_IBQ_LOCK + AGSA_MAX_INBOUND_Q );
    SA_DBG1(("saGetRequirements: param3 == agNULL maxNumLocks   %d\n", *maxNumLocks ));
  }
  else
  {
    agsaQueueConfig_t *queueConfig;
    queueConfig = (agsaQueueConfig_t *)swConfig->param3;
    *maxNumLocks = (LL_IOREQ_IBQ_LOCK_PARM + queueConfig->numInboundQueues );
    SA_DBG1(("saGetRequirements: maxNumLocks   %d\n", *maxNumLocks ));
  }


  /* setup the time tick */
  *usecsPerTick = SA_USECS_PER_TICK;

  SA_ASSERT(memoryReqCount < AGSA_NUM_MEM_CHUNKS, "saGetRequirements: Exceed max number of memory place holder");

  /* set up memory requirement count */
  memoryRequirement->count = memoryReqCount;

  swConfig->legacyInt_X = 1;
  swConfig->max_MSI_InterruptVectors = 32;
  swConfig->max_MSIX_InterruptVectors = 64;//16;

  SA_DBG1(("saGetRequirements:  swConfig->stallUsec  %d\n",swConfig->stallUsec  ));

#ifdef SA_CONFIG_MDFD_REGISTRY
  SA_DBG1(("saGetRequirements:  swConfig->disableMDF %d\n",swConfig->disableMDF));
#endif /*SA_CONFIG_MDFD_REGISTRY*/
  /*SA_DBG1(("saGetRequirements:  swConfig->enableDIF  %d\n",swConfig->enableDIF  ));*/
  /*SA_DBG1(("saGetRequirements:  swConfig->enableEncryption  %d\n",swConfig->enableEncryption  ));*/
#ifdef SA_ENABLE_HDA_FUNCTIONS
  swConfig->hostDirectAccessSupport = 1;
  swConfig->hostDirectAccessMode = 0;
#else
  swConfig->hostDirectAccessSupport = 0;
  swConfig->hostDirectAccessMode = 0;
#endif

}

/******************************************************************************/
/*! \brief Initialize the Hardware
 *
 *  Initialize the Hardware
 *
 *  \param agRoot             Handles for this instance of SAS/SATA hardware
 *  \param memoryAllocated    Point to the data structure that holds the different
                                        chunks of memory that are required
 *  \param hwConfig           Pointer to the hardware configuration
 *  \param swConfig           Pointer to the software configuration
 *  \param usecsPerTick       micro-seconds per tick for the LL layer
 *
 *  \return If initialization is successful
 *          - \e AGSA_RC_SUCCESS initialization is successful
 *          - \e AGSA_RC_FAILURE initialization is not successful
 */
/*******************************************************************************/
GLOBAL bit32 saInitialize(
  agsaRoot_t              *agRoot,
  agsaMemoryRequirement_t *memoryAllocated,
  agsaHwConfig_t          *hwConfig,
  agsaSwConfig_t          *swConfig,
  bit32                   usecsPerTick
  )
{
  agsaLLRoot_t          *saRoot;
  agsaDeviceDesc_t      *pDeviceDesc;
  agsaIORequestDesc_t   *pRequestDesc;
  agsaTimerDesc_t       *pTimerDesc;
  agsaPort_t            *pPort;
  agsaPortMap_t         *pPortMap;
  agsaDeviceMap_t       *pDeviceMap;
  agsaIOMap_t           *pIOMap;
  bit32                 maxNumIODevices;
  bit32                 i, j;
  static mpiMemReq_t    mpiMemoryAllocated;
  bit32                 Tried_NO_HDA = agFALSE;
  bit32                 Double_Reset_HDA = agFALSE;
  bit32                 ret = AGSA_RC_SUCCESS;
#ifdef FAST_IO_TEST
  void   *fr; /* saFastRequest_t */
  bit32  size;
  bit32  alignment;
#endif

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != memoryAllocated), "");
  SA_ASSERT((agNULL != hwConfig), "");
  SA_ASSERT((agNULL != swConfig), "");
  SA_ASSERT((LLROOT_MEM_INDEX < memoryAllocated->count), "");
  SA_ASSERT((DEVICELINK_MEM_INDEX < memoryAllocated->count), "");
  SA_ASSERT((IOREQLINK_MEM_INDEX < memoryAllocated->count), "");
  SA_ASSERT((TIMERLINK_MEM_INDEX < memoryAllocated->count), "");

  si_memset(&mpiMemoryAllocated, 0, sizeof(mpiMemReq_t));

  si_macro_check(agRoot);

  SA_DBG1(("saInitialize: WAIT_INCREMENT %d\n", WAIT_INCREMENT ));
  SA_DBG1(("saInitialize: usecsPerTick %d\n", usecsPerTick ));
  if(! smIS_SPC(agRoot))
  {
    if(! smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: ossaHwRegReadConfig32 ID  reads as %08X\n", ossaHwRegReadConfig32(agRoot,0 ) ));
      SA_DBG1(("saInitialize: expect %08X or %08X or\n",  VEN_DEV_SPCV, VEN_DEV_SPCVE));
      SA_DBG1(("saInitialize: expect %08X or %08X or\n",  VEN_DEV_SPCVP, VEN_DEV_SPCVEP));
      SA_DBG1(("saInitialize: expect %08X or %08X\n",     VEN_DEV_ADAPVEP, VEN_DEV_ADAPVP));
      return AGSA_RC_FAILURE;
    }
  }

  if(  smIS_SPC(agRoot) && smIS_SPCV(agRoot))
  {
      SA_DBG1(("saInitialize: Macro error !smIS_SPC %d smIS_SPCv %d smIS_SFC %d\n",smIS_SPC(agRoot),smIS_SPCV(agRoot), smIS_SFC(agRoot) ));
      return AGSA_RC_FAILURE;
  }

  /* Check the memory allocated */
  for ( i = 0; i < memoryAllocated->count; i ++ )
  {
    /* If memory allocation failed  */
    if (memoryAllocated->agMemory[i].singleElementLength &&
        memoryAllocated->agMemory[i].numElements)
    {
      if ( (0 != memoryAllocated->agMemory[i].numElements)
          && (0 == memoryAllocated->agMemory[i].totalLength) )
      {
        /* return failure */
        SA_DBG1(("saInitialize:AGSA_RC_FAILURE Memory[%d]  singleElementLength = 0x%x  numElements = 0x%x NOT allocated\n",
          i,
          memoryAllocated->agMemory[i].singleElementLength,
          memoryAllocated->agMemory[i].numElements));
        ret = AGSA_RC_FAILURE;
        return ret;
      }
      else
      {
        SA_DBG1(("saInitialize: Memory[%d] singleElementLength = 0x%x  numElements = 0x%x allocated %p\n",
          i,
          memoryAllocated->agMemory[i].singleElementLength,
          memoryAllocated->agMemory[i].numElements,
          memoryAllocated->agMemory[i].virtPtr));
      }
    }
  }

  /* Get the saRoot memory address */
  saRoot = (agsaLLRoot_t *) (memoryAllocated->agMemory[LLROOT_MEM_INDEX].virtPtr);
  SA_ASSERT((agNULL != saRoot), "saRoot");
  if(agNULL == saRoot)
  {
    SA_DBG1(("saInitialize:AGSA_RC_FAILURE saRoot\n"));
    return AGSA_RC_FAILURE;
  }

  agRoot->sdkData = (void *) saRoot;

  SA_DBG1(("saInitialize: saRoot %p\n",saRoot));

  if ( (memoryAllocated != &saRoot->memoryAllocated) ||
       (hwConfig != &saRoot->hwConfig) ||
       (swConfig != &saRoot->swConfig) )
  {
    agsaMemoryRequirement_t *memA = &saRoot->memoryAllocated;
    agsaHwConfig_t          *hwC  = &saRoot->hwConfig;
    agsaSwConfig_t          *swC  = &saRoot->swConfig;

    /* Copy data here */

    *memA   = *memoryAllocated;
    *hwC    = *hwConfig;
    *swC    = *swConfig;
  }


#if defined(SALLSDK_DEBUG)
  if(gLLDebugLevelSet == 0)
  {
    gLLDebugLevelSet = 1;
    gLLDebugLevel = swConfig->sallDebugLevel & 0xF;
    SA_DBG1(("saInitialize:  gLLDebugLevel  %x\n",gLLDebugLevel));
  }
#endif /* SALLSDK_DEBUG */

#ifdef SA_ENABLE_TRACE_FUNCTIONS

  saRoot->TraceBufferLength = memoryAllocated->agMemory[LL_FUNCTION_TRACE].totalLength;
  saRoot->TraceBuffer = memoryAllocated->agMemory[LL_FUNCTION_TRACE].virtPtr;

  siEnableTracing ( agRoot );
/*
*/

#endif /* SA_ENABLE_TRACE_FUNCTIONS */

#ifdef FAST_IO_TEST
  {
  agsaMem_t *agMemory = memoryAllocated->agMemory;

  /* memory requirement for Super IO CACHE memory */
  size = sizeof(saRoot->freeFastReq) / sizeof(saRoot->freeFastReq[0]);

  SA_ASSERT(size == agMemory[LL_FAST_IO].numElements, "");
  SA_ASSERT(agMemory[LL_FAST_IO].virtPtr, "");
  SA_ASSERT((agMemory[LL_FAST_IO].singleElementLength ==
    sizeof(saFastRequest_t)) &&
    (agMemory[LL_FAST_IO].numElements == LL_FAST_IO_SIZE) &&
    (agMemory[LL_FAST_IO].totalLength == agMemory[LL_FAST_IO].numElements *
                                 agMemory[LL_FAST_IO].singleElementLength), "");

  for (i = 0, alignment = agMemory[LL_FAST_IO].alignment,
       fr = agMemory[LL_FAST_IO].virtPtr;
       i < size; i++,
       fr = (void*)((bitptr)fr + (bitptr)(((bit32)sizeof(saFastRequest_t) +
                    alignment - 1) & ~(alignment - 1))))
  {
    saRoot->freeFastReq[i] = fr;
  }
  saRoot->freeFastIdx = size;
  }
#endif /* FAST_IO_TEST*/

  smTraceFuncEnter(hpDBG_VERY_LOUD, "m1");

  SA_DBG1(("saInitialize: swConfig->PortRecoveryResetTimer    %x\n",swConfig->PortRecoveryResetTimer ));

  SA_DBG1(("saInitialize: hwDEVICE_ID_VENDID            0x%08x\n", ossaHwRegReadConfig32(agRoot,0)));
  SA_DBG1(("saInitialize: CFGSTAT CFGCMD                0x%08x\n", ossaHwRegReadConfig32(agRoot,4)));
  SA_DBG1(("saInitialize: CLSCODE REVID                 0x%08x\n", ossaHwRegReadConfig32(agRoot,8)));
  SA_DBG1(("saInitialize: BIST DT HDRTYPE LATTIM CLSIZE 0x%08x\n", ossaHwRegReadConfig32(agRoot,12)));
  SA_DBG1(("saInitialize: hwSVID                        0x%08x\n", ossaHwRegReadConfig32(agRoot,44)));


#ifdef SA_ENABLE_PCI_TRIGGER

   SA_DBG1(("saInitialize: SA_ENABLE_PCI_TRIGGER  a       0x%08x %p\n", saRoot->swConfig.PCI_trigger,&saRoot->swConfig.PCI_trigger));

  if( saRoot->swConfig.PCI_trigger & PCI_TRIGGER_INIT_TEST )
  {
    SA_DBG1(("saInitialize: SA_ENABLE_PCI_TRIGGER         0x%08x %p\n", saRoot->swConfig.PCI_trigger,&saRoot->swConfig.PCI_trigger));
    saRoot->swConfig.PCI_trigger &= ~PCI_TRIGGER_INIT_TEST;
    siPCITriger(agRoot);
  }
#endif /* SA_ENABLE_PCI_TRIGGER */


  saRoot->ChipId = (ossaHwRegReadConfig32(agRoot,0) & 0xFFFF0000);

  SA_DBG1(("saInitialize: saRoot->ChipId                0x%08x\n", saRoot->ChipId));
  siUpdateBarOffsetTable(agRoot,saRoot->ChipId);

  if(saRoot->ChipId == VEN_DEV_SPC)
  {
    if(!  smIS_SPC(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPC macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m1");
      return AGSA_RC_FAILURE;
    }

    SA_DBG1(("saInitialize:  SPC \n" ));
  }
  else if(saRoot->ChipId == VEN_DEV_HIL )
  {
    SA_DBG1(("saInitialize:  SPC HIL\n" ));
    if(!  smIS_SPC(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPC macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPCV)
  {
    SA_DBG1(("saInitialize:  SPC V\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPCVE)
  {
    SA_DBG1(("saInitialize:  SPC VE\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPCVP)
  {
    SA_DBG1(("saInitialize:  SPC VP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPCVEP)
  {
    SA_DBG1(("saInitialize:  SPC VEP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_ADAPVP)
  {
    SA_DBG1(("saInitialize: Adaptec 8088\n" ));
  }
  else if(saRoot->ChipId == VEN_DEV_ADAPVEP)
  {
    SA_DBG1(("saInitialize: Adaptec 8089\n" ));
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12V)
  {
    SA_DBG1(("saInitialize:  SPC 12V\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12VE)
  {
    SA_DBG1(("saInitialize:  SPC 12VE\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12VP)
  {
    SA_DBG1(("saInitialize:  SPC 12VP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12VEP)
  {
    SA_DBG1(("saInitialize:  SPC 12VEP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12ADP)
  {
    SA_DBG1(("saInitialize:  SPC 12ADP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12ADPE)
  {
    SA_DBG1(("saInitialize:  SPC 12ADPE\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'l', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12ADPP)
  {
    SA_DBG1(("saInitialize:  SPC 12ADPP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'm', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12ADPEP)
  {
    SA_DBG1(("saInitialize:  SPC 12ADPEP\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'n', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SPC12SATA)
  {
    SA_DBG1(("saInitialize:  SPC12SATA\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'o', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId ==  VEN_DEV_9015)
  {
    SA_DBG1(("saInitialize:  SPC 12V FPGA\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'p', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId ==  VEN_DEV_9060)
  {
    SA_DBG1(("saInitialize:  SPC 12V FPGA B\n" ));
    if(!  smIS_SPCV(agRoot))
    {
      SA_DBG1(("saInitialize: smIS_SPCV macro fail !!!!\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'q', "m1");
      return AGSA_RC_FAILURE;
    }
  }
  else if(saRoot->ChipId == VEN_DEV_SFC)
  {
    SA_DBG1(("saInitialize: SFC \n" ));
  }
  else
  {
    SA_DBG1(("saInitialize saRoot->ChipId %8X expect %8X or %8X\n", saRoot->ChipId,VEN_DEV_SPC, VEN_DEV_SPCV));
    SA_ASSERT(0, "ChipId");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'r', "m1");
    return AGSA_RC_FAILURE;
  }

  if( smIS_SPC(agRoot))
  {
    SA_DBG1(("saInitialize: Rev is A %d B %d C %d\n",smIsCfgSpcREV_A(agRoot),smIsCfgSpcREV_B(agRoot),smIsCfgSpcREV_C(agRoot)));
  }
  else
  {
    SA_DBG1(("saInitialize: Rev is A %d B %d C %d\n",smIsCfgVREV_A(agRoot),smIsCfgVREV_B(agRoot),smIsCfgVREV_C(agRoot)));
  }

  if( smIS_SPC(agRoot))
  {
    SA_DBG1(("saInitialize: LINK_CTRL 0x%08x Speed 0x%X Lanes 0x%X \n", ossaHwRegReadConfig32(agRoot,128),
      ((ossaHwRegReadConfig32(agRoot,128) & 0x000F0000) >> 16),
      ((ossaHwRegReadConfig32(agRoot,128) & 0x0FF00000) >> 20) ));
  }
  else
  {
    SA_DBG1(("saInitialize: LINK_CTRL 0x%08x Speed 0x%X Lanes 0x%X \n", ossaHwRegReadConfig32(agRoot,208),
      ((ossaHwRegReadConfig32(agRoot,208) & 0x000F0000) >> 16),
      ((ossaHwRegReadConfig32(agRoot,208) & 0x0FF00000) >> 20) ));
  }

  SA_DBG1(("saInitialize: V_SoftResetRegister  %08X\n",  ossaHwRegReadExt(agRoot, PCIBAR0, V_SoftResetRegister )));

/*
  SA_DBG1(("saInitialize:TOP_BOOT_STRAP STRAP_BIT %X\n",  ossaHwRegReadExt(agRoot, PCIBAR1, 0) ));

  SA_DBG1(("SPC_REG_TOP_DEVICE_ID  %8X expect %08X\n",  ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_TOP_DEVICE_ID), SPC_TOP_DEVICE_ID));
  SA_DBG1(("SPC_REG_TOP_DEVICE_ID  %8X expect %08X\n",  siHalRegReadExt( agRoot, GEN_SPC_REG_TOP_DEVICE_ID,SPC_REG_TOP_DEVICE_ID ) , SPC_TOP_DEVICE_ID));

  SA_DBG1(("SPC_REG_TOP_BOOT_STRAP %8X expect %08X\n",  ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_TOP_BOOT_STRAP), SPC_TOP_BOOT_STRAP));

  SA_DBG1(("swConfig->numSASDevHandles =%d\n", swConfig->numDevHandles));
*/
  smTrace(hpDBG_VERY_LOUD,"29",swConfig->numDevHandles);
  /* TP:29 swConfig->numDevHandles */

  /* Setup Device link */
  /* Save the information of allocated device Link memory */
  saRoot->deviceLinkMem = memoryAllocated->agMemory[DEVICELINK_MEM_INDEX];
  if(agNULL == saRoot->deviceLinkMem.virtPtr)
  {
    SA_ASSERT(0, "deviceLinkMem");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'q', "m1");
    return AGSA_RC_FAILURE;
  }

  si_memset(saRoot->deviceLinkMem.virtPtr, 0, saRoot->deviceLinkMem.totalLength);
  SA_DBG2(("saInitialize: [%d] saRoot->deviceLinkMem VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n",
    DEVICELINK_MEM_INDEX,
    saRoot->deviceLinkMem.virtPtr,
    saRoot->deviceLinkMem.phyAddrLower,
    saRoot->deviceLinkMem.numElements,
    saRoot->deviceLinkMem.totalLength,
    saRoot->deviceLinkMem.type));

  maxNumIODevices = swConfig->numDevHandles;
  SA_DBG2(("saInitialize:  maxNumIODevices=%d, swConfig->numDevHandles=%d \n",
    maxNumIODevices,
    swConfig->numDevHandles));

#ifdef SA_ENABLE_PCI_TRIGGER
  SA_DBG1(("saInitialize:  swConfig->PCI_trigger= 0x%x\n", swConfig->PCI_trigger));
#endif /* SA_ENABLE_PCI_TRIGGER */

  /* Setup free IO Devices link list */
  saLlistInitialize(&(saRoot->freeDevicesList));
  for ( i = 0; i < (bit32) maxNumIODevices; i ++ )
  {
    /* get the pointer to the device descriptor */
    pDeviceDesc = (agsaDeviceDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->deviceLinkMem), i);
    /* Initialize device descriptor */
    saLlinkInitialize(&(pDeviceDesc->linkNode));

    pDeviceDesc->initiatorDevHandle.osData    = agNULL;
    pDeviceDesc->initiatorDevHandle.sdkData   = agNULL;
    pDeviceDesc->targetDevHandle.osData       = agNULL;
    pDeviceDesc->targetDevHandle.sdkData      = agNULL;
    pDeviceDesc->deviceType                   = SAS_SATA_UNKNOWN_DEVICE;
    pDeviceDesc->pPort                        = agNULL;
    pDeviceDesc->DeviceMapIndex               = 0;

    saLlistInitialize(&(pDeviceDesc->pendingIORequests));

    /* Add the device descriptor to the free IO device link list */
    saLlistAdd(&(saRoot->freeDevicesList), &(pDeviceDesc->linkNode));
  }

  /* Setup IO Request link */
  /* Save the information of allocated IO Request Link memory */
  saRoot->IORequestMem = memoryAllocated->agMemory[IOREQLINK_MEM_INDEX];
  si_memset(saRoot->IORequestMem.virtPtr, 0, saRoot->IORequestMem.totalLength);

  SA_DBG2(("saInitialize: [%d] saRoot->IORequestMem  VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n",
    IOREQLINK_MEM_INDEX,
    saRoot->IORequestMem.virtPtr,
    saRoot->IORequestMem.phyAddrLower,
    saRoot->IORequestMem.numElements,
    saRoot->IORequestMem.totalLength,
    saRoot->IORequestMem.type));

  /* Setup free IO  Request link list */
  saLlistIOInitialize(&(saRoot->freeIORequests));
  saLlistIOInitialize(&(saRoot->freeReservedRequests));
  for ( i = 0; i < swConfig->maxActiveIOs; i ++ )
  {
    /* get the pointer to the request descriptor */
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), i);
    /* Initialize request descriptor */
    saLlinkInitialize(&(pRequestDesc->linkNode));

    pRequestDesc->valid             = agFALSE;
    pRequestDesc->requestType       = AGSA_REQ_TYPE_UNKNOWN;
    pRequestDesc->pIORequestContext = agNULL;
    pRequestDesc->HTag              = i;
    pRequestDesc->pDevice           = agNULL;
    pRequestDesc->pPort             = agNULL;

    /* Add the request descriptor to the free Reserved Request link list */
  /* SMP request must get service so reserve one request when first SMP completes */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequestDesc->linkNode));
    }
    else
    {
    /* Add the request descriptor to the free IO Request link list */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequestDesc->linkNode));
    }

  }

  /* Setup timer link */
  /* Save the information of allocated timer Link memory */
  saRoot->timerLinkMem = memoryAllocated->agMemory[TIMERLINK_MEM_INDEX];
  si_memset(saRoot->timerLinkMem.virtPtr, 0, saRoot->timerLinkMem.totalLength);
  SA_DBG2(("saInitialize: [%d] saRoot->timerLinkMem  VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n",
    TIMERLINK_MEM_INDEX,
    saRoot->timerLinkMem.virtPtr,
    saRoot->timerLinkMem.phyAddrLower,
    saRoot->timerLinkMem.numElements,
    saRoot->timerLinkMem.totalLength,
    saRoot->timerLinkMem.type ));

  /* Setup free timer link list */
  saLlistInitialize(&(saRoot->freeTimers));
  for ( i = 0; i < NUM_TIMERS; i ++ )
  {
    /* get the pointer to the timer descriptor */
    pTimerDesc = (agsaTimerDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->timerLinkMem), i);
    /* Initialize timer descriptor */
    saLlinkInitialize(&(pTimerDesc->linkNode));

    pTimerDesc->valid         = agFALSE;
    pTimerDesc->timeoutTick   = 0;
    pTimerDesc->pfnTimeout    = agNULL;
    pTimerDesc->Event         = 0;
    pTimerDesc->pParm         = agNULL;

    /* Add the timer descriptor to the free timer link list */
    saLlistAdd(&(saRoot->freeTimers), &(pTimerDesc->linkNode));
  }
  /* Setup valid timer link list */
  saLlistInitialize(&(saRoot->validTimers));

  /* Setup Phys */
  /* Setup PhyCount */
  saRoot->phyCount = (bit8) hwConfig->phyCount;
  /* Init Phy data structure */
  for ( i = 0; i < saRoot->phyCount; i ++ )
  {
    saRoot->phys[i].pPort = agNULL;
    saRoot->phys[i].phyId = (bit8) i;

    /* setup phy status is PHY_STOPPED */
    PHY_STATUS_SET(&(saRoot->phys[i]), PHY_STOPPED);
  }

  /* Setup Ports */
  /* Setup PortCount */
  saRoot->portCount = saRoot->phyCount;
  /* Setup free port link list */
  saLlistInitialize(&(saRoot->freePorts));
  for ( i = 0; i < saRoot->portCount; i ++ )
  {
    /* get the pointer to the port */
    pPort = &(saRoot->ports[i]);
    /* Initialize port */
    saLlinkInitialize(&(pPort->linkNode));

    pPort->portContext.osData   = agNULL;
    pPort->portContext.sdkData  = pPort;
    pPort->portId         = 0;
    pPort->portIdx        = (bit8) i;
    pPort->status         = PORT_NORMAL;

    for ( j = 0; j < saRoot->phyCount; j ++ )
    {
      pPort->phyMap[j] = agFALSE;
    }

    saLlistInitialize(&(pPort->listSASATADevices));

    /* Add the port to the free port link list */
    saLlistAdd(&(saRoot->freePorts), &(pPort->linkNode));
  }
  /* Setup valid port link list */
  saLlistInitialize(&(saRoot->validPorts));

  /* Init sysIntsActive - default is interrupt enable */
  saRoot->sysIntsActive = agFALSE;

  /* setup timer tick granunarity */
  saRoot->usecsPerTick = usecsPerTick;

  /* setup smallest timer increment for stall */
  saRoot->minStallusecs = swConfig->stallUsec;

  SA_DBG1(("saInitialize: WAIT_INCREMENT %d\n" ,WAIT_INCREMENT ));
  if (0 == WAIT_INCREMENT)
  {
    saRoot->minStallusecs = WAIT_INCREMENT_DEFAULT;
  }

  /* initialize LL timer tick */
  saRoot->timeTick = 0;

  /* initialize device (de)registration callback fns */
  saRoot->DeviceRegistrationCB = agNULL;
  saRoot->DeviceDeregistrationCB = agNULL;

  /* Initialize the PortMap for port context */
  for ( i = 0; i < saRoot->portCount; i ++ )
  {
    pPortMap = &(saRoot->PortMap[i]);

    pPortMap->PortContext   = agNULL;
    pPortMap->PortID        = PORT_MARK_OFF;
    pPortMap->PortStatus    = PORT_NORMAL;
    saRoot->autoDeregDeviceflag[i] = 0;
  }

  /* Initialize the DeviceMap for device handle */
  for ( i = 0; i < MAX_IO_DEVICE_ENTRIES; i ++ )
  {
    pDeviceMap = &(saRoot->DeviceMap[i]);

    pDeviceMap->DeviceHandle  = agNULL;
    pDeviceMap->DeviceIdFromFW   =  i;
  }

  /* Initialize the IOMap for IOrequest */
  for ( i = 0; i < MAX_ACTIVE_IO_REQUESTS; i ++ )
  {
    pIOMap = &(saRoot->IOMap[i]);

    pIOMap->IORequest   = agNULL;
    pIOMap->Tag         = MARK_OFF;
  }

  /* setup mpi configuration */
  if (!swConfig->param3)
  {
    /* default configuration */
    siConfiguration(agRoot, &saRoot->mpiConfig, hwConfig, swConfig);
  }
  else
  {
    /* get from TD layer and save it */
    agsaQueueConfig_t *dCFG = &saRoot->QueueConfig;
    agsaQueueConfig_t *sCFG = (agsaQueueConfig_t *)swConfig->param3;

    if (dCFG != sCFG)
    {
      *dCFG = *sCFG;

      if ((hwConfig->hwInterruptCoalescingTimer) || (hwConfig->hwInterruptCoalescingControl))
      {
        for ( i = 0; i < sCFG->numOutboundQueues; i ++ )
        {
          /* disable FW assisted coalescing */
          sCFG->outboundQueues[i].interruptDelay = 0;
          sCFG->outboundQueues[i].interruptCount = 0;
        }

        if(smIS_SPC(agRoot))
        {
          if (hwConfig->hwInterruptCoalescingTimer == 0)
          {
            hwConfig->hwInterruptCoalescingTimer = 1;
            SA_DBG1(("saInitialize:InterruptCoalescingTimer should not be zero. Force to 1\n"));
          }
        }
      }
      ret = siConfiguration(agRoot, &saRoot->mpiConfig, hwConfig, swConfig);
      if (AGSA_RC_FAILURE == ret)
      {
        SA_DBG1(("saInitialize failure queue number=%d\n", saRoot->QueueConfig.numInboundQueues));
        agRoot->sdkData = agNULL;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'r', "m1");
        return ret;
      }
    }
  }


  saRoot->swConfig.param3 = &saRoot->QueueConfig;

  mpiMemoryAllocated.count = memoryAllocated->count - MPI_MEM_INDEX;
  for ( i = 0; i < mpiMemoryAllocated.count; i ++ )
  {
    mpiMemoryAllocated.region[i].virtPtr        = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].virtPtr;
    mpiMemoryAllocated.region[i].appHandle      = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].osHandle;
    mpiMemoryAllocated.region[i].physAddrUpper  = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].phyAddrUpper;
    mpiMemoryAllocated.region[i].physAddrLower  = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].phyAddrLower;
    mpiMemoryAllocated.region[i].totalLength    = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].totalLength;
    mpiMemoryAllocated.region[i].numElements    = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].numElements;
    mpiMemoryAllocated.region[i].elementSize    = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].singleElementLength;
    mpiMemoryAllocated.region[i].alignment      = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].alignment;
    mpiMemoryAllocated.region[i].type           = memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].type;
    SA_DBG2(("saInitialize: memoryAllocated->agMemory[%d] VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n",
      (MPI_IBQ_OBQ_INDEX + i),
      memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].virtPtr,
      memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].phyAddrLower,
      memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].numElements,
      memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].totalLength,
      memoryAllocated->agMemory[MPI_IBQ_OBQ_INDEX + i].type));

    /* set to zeros */
    SA_DBG1(("saInitialize: Zero memory region %d virt %p allocated %d\n",
            i,mpiMemoryAllocated.region[i].virtPtr,    mpiMemoryAllocated.region[i].totalLength));
    si_memset(mpiMemoryAllocated.region[i].virtPtr , 0,mpiMemoryAllocated.region[i].totalLength);

  }

  if ((!swConfig->max_MSI_InterruptVectors) &&
      (!swConfig->max_MSIX_InterruptVectors) &&
      (!swConfig->legacyInt_X))
  {
    /* polling mode */
    SA_DBG1(("saInitialize: configured as polling mode\n"));
  }
  else
  {

    SA_DBG1(("saInitialize: swConfig->max_MSI_InterruptVectors %d\n",swConfig->max_MSI_InterruptVectors));
    SA_DBG1(("saInitialize: swConfig->max_MSIX_InterruptVectors %d\n",swConfig->max_MSIX_InterruptVectors));

    if ((swConfig->legacyInt_X > 1) || (swConfig->max_MSI_InterruptVectors > 32) ||
      (swConfig->max_MSIX_InterruptVectors > 64))
    {
      /* error */
      agRoot->sdkData = agNULL;
      SA_DBG1(("saInitialize:AGSA_RC_FAILURE InterruptVectors A\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 's', "m1");
      return AGSA_RC_FAILURE;
    }
    if ((swConfig->legacyInt_X) && (swConfig->max_MSI_InterruptVectors))
    {
      /* error */
      agRoot->sdkData = agNULL;
      SA_DBG1(("saInitialize:AGSA_RC_FAILURE InterruptVectors B\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 't', "m1");
      return AGSA_RC_FAILURE;
    }
    else if ((swConfig->legacyInt_X) && (swConfig->max_MSIX_InterruptVectors))
    {
      /* error */
      agRoot->sdkData = agNULL;
      SA_DBG1(("saInitialize:AGSA_RC_FAILURE InterruptVectors C\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'u', "m1");
      return AGSA_RC_FAILURE;
    }
    else if ((swConfig->max_MSI_InterruptVectors) && (swConfig->max_MSIX_InterruptVectors))
    {
      /* error */
      agRoot->sdkData = agNULL;
      SA_DBG1(("saInitialize:AGSA_RC_FAILURE InterruptVectors D\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'v', "m1");
      return AGSA_RC_FAILURE;
    }
  }

  /* This section sets common interrupt for Legacy(IRQ) and MSI and MSIX types */
  if(smIS_SPC(agRoot))
  {
    SA_DBG1(("saInitialize:  SPC  interrupts\n" ));

    if (swConfig->legacyInt_X)
    {
      saRoot->OurInterrupt       = siOurLegacyInterrupt;      /* Called in ISR*/
      saRoot->DisableInterrupts  = siDisableLegacyInterrupts; /* Called in ISR*/
      saRoot->ReEnableInterrupts = siReenableLegacyInterrupts;/* Called in Delayed Int handler*/
    }
    else if (swConfig->max_MSIX_InterruptVectors)
    {
      saRoot->OurInterrupt       = siOurMSIXInterrupt;
      saRoot->DisableInterrupts  = siDisableMSIXInterrupts;
      saRoot->ReEnableInterrupts = siReenableMSIXInterrupts;
    }
    else if (swConfig->max_MSI_InterruptVectors)
    {
      saRoot->OurInterrupt       = siOurMSIInterrupt;
      saRoot->DisableInterrupts  = siDisableMSIInterrupts;
      saRoot->ReEnableInterrupts = siReenableMSIInterrupts;
    }
    else
    {
      /* polling mode */
      saRoot->OurInterrupt       = siOurLegacyInterrupt;      /* Called in ISR*/
      saRoot->DisableInterrupts  = siDisableLegacyInterrupts; /* Called in ISR*/
      saRoot->ReEnableInterrupts = siReenableLegacyInterrupts;/* Called in Delayed Int handler*/
    }
  }
  else
  {
    SA_DBG1(("saInitialize:  SPC V interrupts\n" ));
    if (swConfig->legacyInt_X )
    {
      SA_DBG1(("saInitialize:  SPC V legacyInt_X\n" ));
      saRoot->OurInterrupt       = siOurLegacy_V_Interrupt;      /* Called in ISR*/
      saRoot->DisableInterrupts  = siDisableLegacy_V_Interrupts; /* Called in ISR*/
      saRoot->ReEnableInterrupts = siReenableLegacy_V_Interrupts;/* Called in Delayed Int handler*/
    }
    else if (swConfig->max_MSIX_InterruptVectors)
    {
      SA_DBG1(("saInitialize:  SPC V max_MSIX_InterruptVectors %X\n", swConfig->max_MSIX_InterruptVectors));
      saRoot->OurInterrupt       = siOurMSIX_V_Interrupt;       /* */
      saRoot->DisableInterrupts  = siDisableMSIX_V_Interrupts;
      saRoot->ReEnableInterrupts = siReenableMSIX_V_Interrupts;
    }
    else if (swConfig->max_MSI_InterruptVectors)
    {
      SA_DBG1(("saInitialize:  SPC V max_MSI_InterruptVectors\n" ));
      saRoot->OurInterrupt       = siOurMSIX_V_Interrupt;        /* */
      saRoot->DisableInterrupts  = siDisableMSIX_V_Interrupts;
      saRoot->ReEnableInterrupts = siReenableMSIX_V_Interrupts;
    }
    else
    {
      /* polling mode */
      SA_DBG1(("saInitialize:  SPC V polling mode\n" ));
      saRoot->OurInterrupt       = siOurLegacy_V_Interrupt;      /* Called in ISR*/
      saRoot->DisableInterrupts  = siDisableLegacy_V_Interrupts; /* Called in ISR*/
      saRoot->ReEnableInterrupts = siReenableLegacy_V_Interrupts;/* Called in Delayed Int handler*/
    }
    SA_DBG1(("saInitialize:  SPC V\n" ));
  }

  saRoot->Use64bit =  (saRoot->QueueConfig.numOutboundQueues > 32 ) ? 1 : 0;
  if( smIS64bInt(agRoot))
  {
    SA_DBG1(("saInitialize: Use 64 bits for interrupts %d %d\n" ,saRoot->Use64bit, saRoot->QueueConfig.numOutboundQueues ));
  }
  else
  {
    SA_DBG1(("saInitialize: Use 32 bits for interrupts %d %d\n",saRoot->Use64bit , saRoot->QueueConfig.numOutboundQueues  ));
  }

#ifdef SA_LL_IBQ_PROTECT
  SA_DBG1(("saInitialize: Inbound locking defined since LL_IOREQ_IBQ0_LOCK %d\n",LL_IOREQ_IBQ0_LOCK));
#endif /* SA_LL_IBQ_PROTECT */

  /* Disable interrupt */
  saRoot->DisableInterrupts(agRoot, 0);
  SA_DBG1(("saInitialize: DisableInterrupts sysIntsActive %X\n" ,saRoot->sysIntsActive));

#ifdef SA_FW_TEST_BUNCH_STARTS
  saRoot->BunchStarts_Enable        = FALSE;
  saRoot->BunchStarts_Threshold     = 5;
  saRoot->BunchStarts_Pending       = 0;
  saRoot->BunchStarts_TimeoutTicks  = 10;  // N x 100 ms 
#endif /* SA_FW_TEST_BUNCH_STARTS */

  /* clear the interrupt vector bitmap */
  for ( i = 0; i < MAX_NUM_VECTOR; i ++ )
  {
    saRoot->interruptVecIndexBitMap[i] = 0;
    saRoot->interruptVecIndexBitMap1[i] = 0;
  }

#if defined(SALLSDK_DEBUG)
  smTrace(hpDBG_VERY_LOUD,"2Y",0);
  /* TP:2Y SCRATCH_PAD */

  SA_DBG1(("saInitialize: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_0)));
  SA_DBG1(("saInitialize: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)));
  SA_DBG1(("saInitialize: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_2)));
  SA_DBG1(("saInitialize: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_3)));
#endif /* SALLSDK_DEBUG */

  if(smIS_SPCV(agRoot))
  {
    bit32 ScratchPad1 =0;
    bit32 ScratchPad3 =0;

    ScratchPad1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register);
    ScratchPad3 = ossaHwRegRead(agRoot,V_Scratchpad_3_Register);
    if((ScratchPad1 & SCRATCH_PAD1_V_RAAE_MASK) ==  SCRATCH_PAD1_V_RAAE_MASK)
    {
      if(((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK ) == SCRATCH_PAD3_V_ENC_DIS_ERR ) ||
         ((ScratchPad3 & SCRATCH_PAD3_V_ENC_MASK ) == SCRATCH_PAD3_V_ENC_ENA_ERR )    )
      {
        SA_DBG1(("saInitialize:Warning Encryption Issue SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_3)));
      }
    }
  }

  if( smIS_SPC(agRoot))
  {
#ifdef SA_ENABLE_HDA_FUNCTIONS
    TryWithHDA_ON:
    Double_Reset_HDA = TRUE;

    if (swConfig->hostDirectAccessSupport)
    {
      if (AGSA_RC_FAILURE == siHDAMode(agRoot, swConfig->hostDirectAccessMode, (agsaFwImg_t *)swConfig->param4))
      {
        SA_DBG1(("saInitialize:AGSA_RC_FAILURE siHDAMode\n"));
        agRoot->sdkData = agNULL;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'w', "m1");
        return AGSA_RC_FAILURE;
      }
      else
      {
        SA_DBG1(("saInitialize:1 Going to HDA mode HDA 0x%X \n",ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET)));
        if(Double_Reset_HDA == agFALSE)
        {
          siSpcSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
          SA_DBG1(("saInitialize: Double_Reset_HDA HDA 0x%X \n",ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET)));
          Double_Reset_HDA = TRUE;
          goto TryWithHDA_ON;
        }
      }
    }
    else
    {
      /* check FW is running */
      if (BOOTTLOADERHDA_IDLE == (ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS))
      {
        /* HDA mode */
        SA_DBG1(("saInitialize: No HDA mode enable and FW is not running.\n"));
        if(Tried_NO_HDA != agTRUE )
        {

          Tried_NO_HDA = TRUE;
          swConfig->hostDirectAccessSupport = 1;
          swConfig->hostDirectAccessMode = 1;
          siSpcSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
          SA_DBG1(("saInitialize: 2 Going to HDA mode HDA %X \n",ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET)));
          goto TryWithHDA_ON;
        }
        else
        {
          SA_DBG1(("saInitialize: could not start HDA mode HDA %X \n",ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET)));
          smTraceFuncExit(hpDBG_VERY_LOUD, 'x', "m1");

          return AGSA_RC_FAILURE;
        }
        smTraceFuncExit(hpDBG_VERY_LOUD, 'y', "m1");
        return AGSA_RC_FAILURE;
      }
    }
#else /* SA_ENABLE_HDA_FUNCTIONS */
    /* check FW is running */
    if (BOOTTLOADERHDA_IDLE == (ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS) )
    {
      /* HDA mode */
      SA_DBG1(("saInitialize: No HDA mode enable and FW is not running.\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'z', "m1");
      return AGSA_RC_FAILURE;
    }
#endif /* SA_ENABLE_HDA_FUNCTIONS */
  }
  else
  {
    SA_DBG1(("saInitialize: SPCv swConfig->hostDirectAccessMode %d swConfig->hostDirectAccessSupport %d\n",swConfig->hostDirectAccessMode,swConfig->hostDirectAccessSupport));
    if (swConfig->hostDirectAccessSupport)
    {
      bit32 hda_status;
      bit32 soft_reset_status = AGSA_RC_SUCCESS;

      SA_DBG1(("saInitialize: SPCv load HDA\n"));

      hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28));

      SA_DBG1(("saInitialize: hda_status 0x%x\n",hda_status));

      siScratchDump(agRoot);

      if( swConfig->hostDirectAccessMode == 0)
      {
        soft_reset_status = siSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
        if(soft_reset_status !=  AGSA_RC_SUCCESS)
        {
          agRoot->sdkData = agNULL;
          SA_DBG1(("saInitialize:AGSA_RC_FAILURE soft_reset_status\n"));

          smTraceFuncExit(hpDBG_VERY_LOUD, 'A', "m1");
          return AGSA_RC_FAILURE;
        }
      }

      if((hda_status  & SPC_V_HDAR_RSPCODE_MASK)  != SPC_V_HDAR_IDLE)
      {
        SA_DBG1(("saInitialize: hda_status not SPC_V_HDAR_IDLE 0x%08x\n", hda_status));
        soft_reset_status = siSoftReset(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
        hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28));
        if((hda_status  & SPC_V_HDAR_RSPCODE_MASK)  != SPC_V_HDAR_IDLE)
        {
          SA_DBG1(("saInitialize: 2 reset hda_status not SPC_V_HDAR_IDLE 0x%08x\n", hda_status));
        }
      }
      if(soft_reset_status !=  AGSA_RC_SUCCESS)
      {
        agRoot->sdkData = agNULL;
        SA_DBG1(("saInitialize:AGSA_RC_FAILURE soft_reset_status A\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'B', "m1");
        return AGSA_RC_FAILURE;
      }

#ifdef SA_ENABLE_HDA_FUNCTIONS
      if (AGSA_RC_FAILURE == siHDAMode_V(agRoot, swConfig->hostDirectAccessMode, (agsaFwImg_t *)swConfig->param4))
      {
        SA_DBG1(("saInitialize:AGSA_RC_FAILURE siHDAMode_V\n"));

        siChipResetV(agRoot, SPC_HDASOFT_RESET_SIGNATURE);
        agRoot->sdkData = agNULL;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'C', "m1");
        return AGSA_RC_FAILURE;
      }
#endif /* SA_ENABLE_HDA_FUNCTIONS */

    }
    else
    {
      SA_DBG1(("saInitialize: SPCv normal\n"));
    }

  }

  /* copy the table to the LL layer */
  si_memcpy(&saRoot->mpiConfig.phyAnalogConfig, &hwConfig->phyAnalogConfig, sizeof(agsaPhyAnalogSetupTable_t));

#ifdef SALL_API_TEST
  /* Initialize the LL IO counter */
  si_memset(&saRoot->LLCounters, 0, sizeof(agsaIOCountInfo_t));
#endif

  si_memset(&saRoot->IoErrorCount, 0, sizeof(agsaIOErrorEventStats_t));
  si_memset(&saRoot->IoEventCount, 0, sizeof(agsaIOErrorEventStats_t));
  if(smIS_SPC(agRoot))
  {
	  if( smIS_spc8081(agRoot))
	  {
		if (AGSA_RC_FAILURE == siBar4Shift(agRoot, MBIC_GSM_SM_BASE))
		{
		  SA_DBG1(("saInitialize: siBar4Shift FAILED ******************************************\n"));
		}
	  }
	siSpcSoftReset(agRoot, SPC_SOFT_RESET_SIGNATURE);
  }
  if(smIS_SPCV(agRoot))
  {
	SA_DBG1(("saInitialize: saRoot->ChipId == VEN_DEV_SPCV\n"));
	siChipResetV(agRoot, SPC_SOFT_RESET_SIGNATURE);
  }	

  /* MPI Initialization */
  ret = mpiInitialize(agRoot, &mpiMemoryAllocated, &saRoot->mpiConfig);
  SA_DBG1(("saInitialize: MaxOutstandingIO 0x%x swConfig->maxActiveIOs 0x%x\n", saRoot->ControllerInfo.maxPendingIO,saRoot->swConfig.maxActiveIOs ));

#ifdef SA_ENABLE_HDA_FUNCTIONS
  if( ret  == AGSA_RC_FAILURE && Tried_NO_HDA == agFALSE && smIS_SPC(agRoot))
  { /* FW not flashed  */
    Tried_NO_HDA=agTRUE;
    swConfig->hostDirectAccessSupport = 1;
    swConfig->hostDirectAccessMode = 1;
    siSoftReset(agRoot, SPC_SOFT_RESET_SIGNATURE);
    SA_DBG1(("saInitialize: 3 Going to HDA mode HDA %X \n",ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET)));
    goto TryWithHDA_ON;
  }

#endif /* SA_ENABLE_HDA_FUNCTIONS */

  if( ret  == AGSA_RC_FAILURE)
  {
    SA_DBG1(("saInitialize:  AGSA_RC_FAILURE mpiInitialize\n"));
    SA_DBG1(("saInitialize: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_0_Register)));
    SA_DBG1(("saInitialize: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_1_Register)));
    SA_DBG1(("saInitialize: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_2_Register)));
    SA_DBG1(("saInitialize: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_3_Register)));

    if(saRoot->swConfig.fatalErrorInterruptEnable)
    {
      ossaDisableInterrupts(agRoot,saRoot->swConfig.fatalErrorInterruptVector );
    }

    agRoot->sdkData = agNULL;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'D', "m1");
    return ret;
  }

  /* setup hardware interrupt coalescing control and timer registers */
  if(smIS_SPCV(agRoot))
  {
      SA_DBG1(("saInitialize: SPC_V Not set hwInterruptCoalescingTimer\n" ));
      SA_DBG1(("saInitialize: SPC_V Not set hwInterruptCoalescingControl\n" ));
  }
  else
  {
      ossaHwRegWriteExt(agRoot, PCIBAR1, SPC_ICTIMER,hwConfig->hwInterruptCoalescingTimer );
      ossaHwRegWriteExt(agRoot, PCIBAR1, SPC_ICCONTROL, hwConfig->hwInterruptCoalescingControl);
  }


  SA_DBG1(("saInitialize: swConfig->fatalErrorInterruptEnable  %X\n",swConfig->fatalErrorInterruptEnable));

  SA_DBG1(("saInitialize: saRoot->swConfig.fatalErrorInterruptVector  %X\n",saRoot->swConfig.fatalErrorInterruptVector));
  SA_DBG1(("saInitialize: swConfig->max_MSI_InterruptVectors   %X\n",swConfig->max_MSI_InterruptVectors));
  SA_DBG1(("saInitialize: swConfig->max_MSIX_InterruptVectors  %X\n",swConfig->max_MSIX_InterruptVectors));
  SA_DBG1(("saInitialize: swConfig->legacyInt_X                %X\n",swConfig->legacyInt_X));
  SA_DBG1(("saInitialize: swConfig->hostDirectAccessSupport    %X\n",swConfig->hostDirectAccessSupport));
  SA_DBG1(("saInitialize: swConfig->hostDirectAccessMode       %X\n",swConfig->hostDirectAccessMode));

#ifdef SA_CONFIG_MDFD_REGISTRY
  SA_DBG1(("saInitialize: swConfig->disableMDF                 %X\n",swConfig->disableMDF));
#endif /*SA_CONFIG_MDFD_REGISTRY*/
  /*SA_DBG1(("saInitialize: swConfig->enableDIF                  %X\n",swConfig->enableDIF));*/
  /*SA_DBG1(("saInitialize: swConfig->enableEncryption           %X\n",swConfig->enableEncryption));*/


  /* log message if failure */
  if (AGSA_RC_FAILURE == ret)
  {
    SA_DBG1(("saInitialize:AGSA_RC_FAILURE mpiInitialize\n"));
    /* Assign chip status */
    saRoot->chipStatus = CHIP_FATAL_ERROR;
  }
  else
  {
    /* Assign chip status */
    saRoot->chipStatus = CHIP_NORMAL;
#ifdef SA_FW_TIMER_READS_STATUS
    siTimerAdd(agRoot,SA_FW_TIMER_READS_STATUS_INTERVAL, siReadControllerStatus,0,agNULL  );
#endif /* SA_FW_TIMER_READS_STATUS */
  }


  if( ret == AGSA_RC_SUCCESS || ret == AGSA_RC_VERSION_UNTESTED)
  {
    if(gPollForMissingInt)
    {
      mpiOCQueue_t         *circularQ;
      SA_DBG1(("saInitialize:  saRoot->sysIntsActive %X\n",saRoot->sysIntsActive));

      circularQ = &saRoot->outboundQueue[0];
      OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
      SA_DBG1(("saInitialize: PI 0x%03x CI 0x%03x\n",circularQ->producerIdx, circularQ->consumerIdx));
    }
  }

  /* If fatal error interrupt enable we need checking it during the interrupt */
  SA_DBG1(("saInitialize: swConfig.fatalErrorInterruptEnable %d\n",saRoot->swConfig.fatalErrorInterruptEnable));
  SA_DBG1(("saInitialize: swConfig.fatalErrorInterruptVector %d\n",saRoot->swConfig.fatalErrorInterruptVector));
  SA_DBG1(("saInitialize: swConfig->max_MSIX_InterruptVectors  %X\n",swConfig->max_MSIX_InterruptVectors));

  if(saRoot->swConfig.fatalErrorInterruptEnable)
  {

    SA_DBG1(("saInitialize: Doorbell_Set  %08X U %08X\n",
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
    SA_DBG1(("saInitialize: Doorbell_Mask %08X U %08X\n",
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register ),
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU )));

    ossaReenableInterrupts(agRoot,saRoot->swConfig.fatalErrorInterruptVector );

    SA_DBG1(("saInitialize: Doorbell_Set  %08X U %08X\n",
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_Register),
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Set_RegisterU)));
    SA_DBG1(("saInitialize: Doorbell_Mask %08X U %08X\n",
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_Register ),
                             ossaHwRegReadExt(agRoot, PCIBAR0, V_Outbound_Doorbell_Mask_Set_RegisterU )));
  }


  SA_DBG1(("saInitialize: siDumpActiveIORequests\n"));
  siDumpActiveIORequests(agRoot, saRoot->swConfig.maxActiveIOs);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'E', "m1");
  /* return */
  return ret;
}



#ifdef SA_FW_TIMER_READS_STATUS

bit32 siReadControllerStatus(
                                  agsaRoot_t      *agRoot,
                                  bit32           Event,
                                  void *          pParm
                                  )
{
  bit32 to_ret =0;
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  mpiReadGSTable(agRoot,  &saRoot->mpiGSTable);

  if(smIS_SPCV_2_IOP(agRoot))
  {
    if(saRoot->Iop1Tcnt_last  == saRoot->mpiGSTable.Iop1Tcnt )
    SA_DBG2(("siReadControllerStatus: Iop1 %d STUCK\n", saRoot->mpiGSTable.Iop1Tcnt));
  }

  if( saRoot->MsguTcnt_last == saRoot->mpiGSTable.MsguTcnt || saRoot->IopTcnt_last  == saRoot->mpiGSTable.IopTcnt )
  {
    SA_DBG1(("siReadControllerStatus: Msgu %d Iop %d\n",saRoot->mpiGSTable.MsguTcnt, saRoot->mpiGSTable.IopTcnt));
    saFatalInterruptHandler(agRoot,  saRoot->swConfig.fatalErrorInterruptVector  );
  }
  SA_DBG2(("siReadControllerStatus: Msgu %d Iop %d\n",saRoot->mpiGSTable.MsguTcnt, saRoot->mpiGSTable.IopTcnt));

  saRoot->MsguTcnt_last = saRoot->mpiGSTable.MsguTcnt;
  saRoot->IopTcnt_last  = saRoot->mpiGSTable.IopTcnt;
  saRoot->Iop1Tcnt_last = saRoot->mpiGSTable.Iop1Tcnt;


  if(gPollForMissingInt)
  {
    mpiOCQueue_t         *circularQ;
    SA_DBG4(("siReadControllerStatus:  saRoot->sysIntsActive %X\n",saRoot->sysIntsActive));

    circularQ = &saRoot->outboundQueue[0];
    OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
    if(circularQ->producerIdx != circularQ->consumerIdx)
    {
      SA_DBG1(("siReadControllerStatus:  saRoot->sysIntsActive %X\n",saRoot->sysIntsActive));
      SA_DBG1(("siReadControllerStatus: PI 0x%03x CI 0x%03x\n",circularQ->producerIdx, circularQ->consumerIdx));

      SA_DBG1(("siReadControllerStatus:IN MSGU_READ_ODMR %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODMR, V_Outbound_Doorbell_Mask_Set_Register )));
      SA_DBG1(("siReadControllerStatus:MSGU_READ_ODR  %08X\n",siHalRegReadExt(agRoot, GEN_MSGU_ODR, V_Outbound_Doorbell_Set_Register)));
      ossaHwRegWriteExt(agRoot, PCIBAR0,V_Outbound_Doorbell_Clear_Register, 0xFFFFFFFF );

    }
  }

  siTimerAdd(agRoot,SA_FW_TIMER_READS_STATUS_INTERVAL, siReadControllerStatus,Event,pParm  );

  return(to_ret);
}

#endif /* SA_FW_TIMER_READS_STATUS */

/******************************************************************************/
/*! \brief Routine to do SPC configuration with default or specified values
 *
 *  Set up configuration table in LL Layer
 *
 *  \param agRoot    handles for this instance of SAS/SATA hardware
 *  \param mpiConfig MPI Configuration
 *  \param swConfig  Pointer to the software configuration
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL bit32 siConfiguration(
  agsaRoot_t      *agRoot,
  mpiConfig_t     *mpiConfig,
  agsaHwConfig_t  *hwConfig,
  agsaSwConfig_t  *swConfig
  )
{
  agsaQueueConfig_t *queueConfig;
  bit32             intOption, enable64 = 0;
  bit8              i;


  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "");

  smTraceFuncEnter(hpDBG_VERY_LOUD,"m2");

  si_memset(mpiConfig, 0, sizeof(mpiConfig_t));
  SA_DBG1(("siConfiguration: si_memset mpiConfig\n"));

#if defined(SALLSDK_DEBUG)
  sidump_swConfig(swConfig);
#endif
  mpiConfig->mainConfig.custset                      = swConfig->FWConfig;

  SA_DBG1(("siConfiguration:custset              %8X  %8X\n",mpiConfig->mainConfig.custset,swConfig->FWConfig));

  if (swConfig->param3 == agNULL)
  {
    SA_DBG1(("siConfiguration: swConfig->param3 == agNULL\n"));
    /* initialize the mpiConfig */
    /* We configure the Host main part of configuration table */
    mpiConfig->mainConfig.iQNPPD_HPPD_GEvent          = 0;
    mpiConfig->mainConfig.outboundHWEventPID0_3       = 0;
    mpiConfig->mainConfig.outboundHWEventPID4_7       = 0;
    mpiConfig->mainConfig.outboundNCQEventPID0_3      = 0;
    mpiConfig->mainConfig.outboundNCQEventPID4_7      = 0;
    mpiConfig->mainConfig.outboundTargetITNexusEventPID0_3 = 0;
    mpiConfig->mainConfig.outboundTargetITNexusEventPID4_7 = 0;
    mpiConfig->mainConfig.outboundTargetSSPEventPID0_3 = 0;
    mpiConfig->mainConfig.outboundTargetSSPEventPID4_7 = 0;

    mpiConfig->mainConfig.ioAbortDelay                    = 0;

    mpiConfig->mainConfig.upperEventLogAddress        = 0;
    mpiConfig->mainConfig.lowerEventLogAddress        = 0;
    mpiConfig->mainConfig.eventLogSize                = MPI_LOGSIZE;
    mpiConfig->mainConfig.eventLogOption              = 0;
    mpiConfig->mainConfig.upperIOPeventLogAddress     = 0;
    mpiConfig->mainConfig.lowerIOPeventLogAddress     = 0;
    mpiConfig->mainConfig.IOPeventLogSize             = MPI_LOGSIZE;
    mpiConfig->mainConfig.IOPeventLogOption           = 0;
    mpiConfig->mainConfig.FatalErrorInterrupt         = 0;

    /* save the default value */
    mpiConfig->numInboundQueues = AGSA_MAX_INBOUND_Q;
    mpiConfig->numOutboundQueues = AGSA_MAX_OUTBOUND_Q;
    mpiConfig->maxNumInboundQueues = AGSA_MAX_INBOUND_Q;
    mpiConfig->maxNumOutboundQueues = AGSA_MAX_OUTBOUND_Q;

    /* configure inbound queues */
    for ( i = 0; i < AGSA_MAX_INBOUND_Q; i ++ )
    {
      mpiConfig->inboundQueues[i].numElements   = INBOUND_DEPTH_SIZE;
      mpiConfig->inboundQueues[i].elementSize   = IOMB_SIZE64;
      mpiConfig->inboundQueues[i].priority      = MPI_QUEUE_NORMAL;
    }

    /* configure outbound queues */
    for ( i = 0; i < AGSA_MAX_OUTBOUND_Q; i ++ )
    {
      mpiConfig->outboundQueues[i].numElements        = OUTBOUND_DEPTH_SIZE;
      mpiConfig->outboundQueues[i].elementSize        = IOMB_SIZE64;
      mpiConfig->outboundQueues[i].interruptVector    = 0;
      mpiConfig->outboundQueues[i].interruptDelay     = 0;
      mpiConfig->outboundQueues[i].interruptThreshold = 0;
      /* always enable OQ interrupt */
      mpiConfig->outboundQueues[i].interruptEnable    = 1;
    }
  }
  else
  { /* Parm3 is not null  */
    queueConfig = (agsaQueueConfig_t *)swConfig->param3;

#if defined(SALLSDK_DEBUG)
    sidump_Q_config( queueConfig );
#endif

    SA_DBG1(("siConfiguration: swConfig->param3 == %p\n",queueConfig));

    if ((queueConfig->numInboundQueues > AGSA_MAX_INBOUND_Q) ||
      (queueConfig->numOutboundQueues > AGSA_MAX_OUTBOUND_Q))
    {
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m2");
      SA_DBG1(("siConfiguration:AGSA_RC_FAILURE MAX_Q\n"));

      return AGSA_RC_FAILURE;
    }

    if ((queueConfig->numInboundQueues  == 0 ||
         queueConfig->numOutboundQueues == 0    ))
    {
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "m2");
      SA_DBG1(("siConfiguration:AGSA_RC_FAILURE NO_Q\n"));
      return AGSA_RC_FAILURE;
    }
    mpiConfig->mainConfig.eventLogSize = swConfig->sizefEventLog1 * KBYTES;
    mpiConfig->mainConfig.eventLogOption  = swConfig->eventLog1Option;
    mpiConfig->mainConfig.IOPeventLogSize = swConfig->sizefEventLog2 * KBYTES;
    mpiConfig->mainConfig.IOPeventLogOption = swConfig->eventLog2Option;

    if ((queueConfig->numInboundQueues > IQ_NUM_32) || (queueConfig->numOutboundQueues > OQ_NUM_32))
    {
      enable64 = 1;
    }

    if (agNULL == hwConfig)
    {
      intOption = 0;
    }
    else
    {

#if defined(SALLSDK_DEBUG)
      sidump_hwConfig(hwConfig);
#endif


      if(smIS_SPCV(agRoot))
      {
        intOption = 0;
      }
      else
      {
        intOption = hwConfig->intReassertionOption & INT_OPTION;
      }

    }

    /* Enable SGPIO */
    swConfig->sgpioSupportEnable = 1;
	
    /* set bit for normal priority or high priority path */
    /* set fatal error interrupt enable and vector */
    /* set Interrupt Reassertion enable and 64 IQ/OQ enable */
    mpiConfig->mainConfig.FatalErrorInterrupt =
                                 (swConfig->fatalErrorInterruptEnable)                       /* bit 0*/     |
         (hwConfig == agNULL ? 0: (hwConfig->hwOption & HW_CFG_PICI_EFFECTIVE_ADDRESS ? (0x1 << SHIFT1): 0))|
                                     (swConfig->sgpioSupportEnable                    ? (0x1 << SHIFT2): 0) |
    /* compile option SA_ENABLE_POISION_TLP */(SA_PTNFE_POISION_TLP                          << SHIFT3)     |
#ifdef SA_CONFIG_MDFD_REGISTRY
                                            (swConfig->disableMDF                     ? (0x1 << SHIFT4): 0) |
#else
    /* compile option SA_DISABLE_MDFD       */   (SA_MDFD_MULTI_DATA_FETCH                      << SHIFT4)  |
#endif /*SA_CONFIG_MDFD_REGISTRY*/
    /* compile option SA_DISABLE_OB_COAL    */(SA_OUTBOUND_COALESCE                          << SHIFT5)     |
    /* compile option SA_ENABLE_ARBTE       */(SA_ARBTE                                      << SHIFT6)     |
                               ((swConfig->fatalErrorInterruptVector & FATAL_ERROR_INT_BITS) << SHIFT8)     |
                                              (enable64                                      << SHIFT16)    |
                                              (intOption                                     << SHIFT17);


    SA_DBG1(("siConfiguration: swConfig->fatalErrorInterruptEnable  %X\n",swConfig->fatalErrorInterruptEnable));
    SA_DBG1(("siConfiguration: swConfig->fatalErrorInterruptVector  %X\n",swConfig->fatalErrorInterruptVector));



    /* initialize the mpiConfig */
    /* We configure the Host main part of configuration table */
    mpiConfig->mainConfig.outboundTargetITNexusEventPID0_3 = 0;
    mpiConfig->mainConfig.outboundTargetITNexusEventPID4_7 = 0;
    mpiConfig->mainConfig.outboundTargetSSPEventPID0_3 = 0;
    mpiConfig->mainConfig.outboundTargetSSPEventPID4_7 = 0;
    mpiConfig->mainConfig.ioAbortDelay = 0;
    mpiConfig->mainConfig.PortRecoveryTimerPortResetTimer = swConfig->PortRecoveryResetTimer;

    /* get parameter from queueConfig */
    mpiConfig->mainConfig.iQNPPD_HPPD_GEvent          = queueConfig->iqNormalPriorityProcessingDepth |
                                                        (queueConfig->iqHighPriorityProcessingDepth << SHIFT8) |
                                                        (queueConfig->generalEventQueue << SHIFT16) |
                                                        (queueConfig->tgtDeviceRemovedEventQueue << SHIFT24);

    mpiConfig->mainConfig.outboundHWEventPID0_3       = queueConfig->sasHwEventQueue[0] |
                                                        (queueConfig->sasHwEventQueue[1] << SHIFT8)  |
                                                        (queueConfig->sasHwEventQueue[2] << SHIFT16) |
                                                        (queueConfig->sasHwEventQueue[3] << SHIFT24);
    mpiConfig->mainConfig.outboundHWEventPID4_7       = queueConfig->sasHwEventQueue[4] |
                                                        (queueConfig->sasHwEventQueue[5] << SHIFT8)  |
                                                        (queueConfig->sasHwEventQueue[6] << SHIFT16) |
                                                        (queueConfig->sasHwEventQueue[7] << SHIFT24);
    mpiConfig->mainConfig.outboundNCQEventPID0_3      = queueConfig->sataNCQErrorEventQueue[0] |
                                                        (queueConfig->sataNCQErrorEventQueue[1] << SHIFT8)  |
                                                        (queueConfig->sataNCQErrorEventQueue[2] << SHIFT16) |
                                                        (queueConfig->sataNCQErrorEventQueue[3] << SHIFT24);
    mpiConfig->mainConfig.outboundNCQEventPID4_7      = queueConfig->sataNCQErrorEventQueue[4] |
                                                        (queueConfig->sataNCQErrorEventQueue[5] << SHIFT8)  |
                                                        (queueConfig->sataNCQErrorEventQueue[6] << SHIFT16) |
                                                        (queueConfig->sataNCQErrorEventQueue[7] << SHIFT24);
    /* save it */
    mpiConfig->numInboundQueues = queueConfig->numInboundQueues;
    mpiConfig->numOutboundQueues = queueConfig->numOutboundQueues;
    mpiConfig->queueOption = queueConfig->queueOption;

    SA_DBG2(("siConfiguration: numInboundQueues=%d numOutboundQueues=%d\n",
    queueConfig->numInboundQueues,
    queueConfig->numOutboundQueues));

    /* configure inbound queues */
    /* We configure the size of queue based on swConfig */
    for( i = 0; i < queueConfig->numInboundQueues; i ++ )
    {
      mpiConfig->inboundQueues[i].numElements   = (bit16)queueConfig->inboundQueues[i].elementCount;
      mpiConfig->inboundQueues[i].elementSize   = (bit16)queueConfig->inboundQueues[i].elementSize;;
      mpiConfig->inboundQueues[i].priority      = queueConfig->inboundQueues[i].priority;

      SA_DBG2(("siConfiguration: IBQ%d:elementCount=%d elementSize=%d priority=%d Total Size 0x%X\n",
      i,
      queueConfig->inboundQueues[i].elementCount,
      queueConfig->inboundQueues[i].elementSize,
      queueConfig->inboundQueues[i].priority,
      queueConfig->inboundQueues[i].elementCount * queueConfig->inboundQueues[i].elementSize ));
    }

    /* configura outbound queues */
    /* We configure the size of queue based on swConfig */
    for( i = 0; i < queueConfig->numOutboundQueues; i ++ )
    {
      mpiConfig->outboundQueues[i].numElements        = (bit16)queueConfig->outboundQueues[i].elementCount;
      mpiConfig->outboundQueues[i].elementSize        = (bit16)queueConfig->outboundQueues[i].elementSize;
      mpiConfig->outboundQueues[i].interruptVector    = (bit8)queueConfig->outboundQueues[i].interruptVectorIndex;
      mpiConfig->outboundQueues[i].interruptDelay     = (bit16)queueConfig->outboundQueues[i].interruptDelay;
      mpiConfig->outboundQueues[i].interruptThreshold = (bit8)queueConfig->outboundQueues[i].interruptCount;
      mpiConfig->outboundQueues[i].interruptEnable    = (bit32)queueConfig->outboundQueues[i].interruptEnable;

      SA_DBG2(("siConfiguration: OBQ%d:elementCount=%d elementSize=%d interruptCount=%d interruptEnable=%d\n",
      i,
      queueConfig->outboundQueues[i].elementCount,
      queueConfig->outboundQueues[i].elementSize,
      queueConfig->outboundQueues[i].interruptCount,
      queueConfig->outboundQueues[i].interruptEnable));
    }
  }

  SA_DBG1(("siConfiguration:mpiConfig->mainConfig.FatalErrorInterrupt 0x%X\n",mpiConfig->mainConfig.FatalErrorInterrupt));
  SA_DBG1(("siConfiguration:swConfig->fatalErrorInterruptVector       0x%X\n",swConfig->fatalErrorInterruptVector));
  SA_DBG1(("siConfiguration:enable64                                  0x%X\n",enable64));
  SA_DBG1(("siConfiguration:PortRecoveryResetTimer                    0x%X\n",swConfig->PortRecoveryResetTimer));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "m2");

  /* return */
  return AGSA_RC_SUCCESS;
}

#ifdef FW_EVT_LOG_TST
void saLogDump(agsaRoot_t *agRoot,
               U32    *eventLogSize,
               U32   **eventLogAddress_)
{
  agsaLLRoot_t *saRoot =  (agsaLLRoot_t *)(agRoot->sdkData);
  //mpiConfig_t  *mpiConfig = &saRoot->mpiConfig;
  mpiHostLLConfigDescriptor_t *mpiConfig = &saRoot->mainConfigTable;

  *eventLogAddress_ = (U32*)eventLogAddress;
  *eventLogSize = (U32)mpiConfig->eventLogSize;
}
#endif

/*******************************************************************************/
/** \fn mpiInitialize(agsaRoot *agRoot, mpiMemReq_t* memoryAllocated, mpiConfig_t* config)
 *  \brief Initializes the MPI Message Unit
 *  \param agRoot           Pointer to a data structure containing LL layer context handles
 *  \param memoryAllocated  Data structure that holds the different chunks of memory that are allocated
 *  \param config           MPI configuration
 *
 * This function is called to initialize SPC_HOST_MPI internal data structures and the SPC hardware.
 * This function is competed synch->ronously (there is no callback)
 *
 * Return:
 *         AGSA_RC_SUCCESS if initialization succeeded.
 *         AGSA_RC_FAILURE if initialization failed.
 */
/*******************************************************************************/
GLOBAL bit32 mpiInitialize(agsaRoot_t *agRoot,
                           mpiMemReq_t* memoryAllocated,
                           mpiConfig_t* config)
{
  static spc_configMainDescriptor_t mainCfg;              /* main part of MPI configuration */
  static spc_inboundQueueDescriptor_t inQueueCfg;         /* Inbound queue HW configuration structure */
  static spc_outboundQueueDescriptor_t outQueueCfg;       /* Outbound queue HW configuration structure */
  bit16 qIdx, i, indexoffset;                      /* Queue index */
  bit16 mIdx = 0;                                  /* Memory region index */
  bit32 MSGUCfgTblDWIdx, GSTLenMPIS;
  bit32 MSGUCfgTblBase, ret = AGSA_RC_SUCCESS;
  bit32 value, togglevalue;
  bit32 saveOffset;
  bit32 inboundoffset, outboundoffset;
  bit8  pcibar;
  bit16 maxinbound = AGSA_MAX_INBOUND_Q;
  bit16 maxoutbound = AGSA_MAX_OUTBOUND_Q;
  bit32 OB_CIPCIBar;
  bit32 IB_PIPCIBar;
  bit32 max_wait_time;
  bit32 max_wait_count;
  bit32 memOffset;
  agsaLLRoot_t *saRoot;
  mpiICQueue_t *circularIQ = agNULL;
  mpiOCQueue_t *circularOQ;

  bit32 mpiUnInitFailed = 0;
  bit32 mpiStartToggleFailed = 0;


#if defined(SALLSDK_DEBUG)
 bit8 phycount = AGSA_MAX_VALID_PHYS;
#endif /* SALLSDK_DEBUG */

  SA_DBG1(("mpiInitialize: Entering\n"));
  SA_ASSERT(NULL != agRoot, "agRoot argument cannot be null");
  SA_ASSERT(NULL != memoryAllocated, "memoryAllocated argument cannot be null");
  SA_ASSERT(NULL != config, "config argument cannot be null");
  SA_ASSERT(0 == (sizeof(spc_inboundQueueDescriptor_t) % 4), "spc_inboundQueueDescriptor_t type size has to be divisible by 4");

  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  si_memset(&mainCfg,0,sizeof(spc_configMainDescriptor_t));
  si_memset(&inQueueCfg,0,sizeof(spc_inboundQueueDescriptor_t));
  si_memset(&outQueueCfg,0,sizeof(spc_outboundQueueDescriptor_t));

  SA_ASSERT((agNULL !=saRoot ), "");
  if(saRoot == agNULL)
  {
    SA_DBG1(("mpiInitialize: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  smTraceFuncEnter(hpDBG_VERY_LOUD,"m3");

  /*Shift BAR 4 for SPC HAILEAH*/
  if(smIS_SPC(agRoot))
  {
    if( smIS_HIL(agRoot))
    {
      if (AGSA_RC_FAILURE == siBar4Shift(agRoot, MBIC_GSM_SM_BASE))
      {
        SA_DBG1(("mpiInitialize: siBar4Shift FAILED ******************************************\n"));
        return AGSA_RC_FAILURE;
      }
    }
  }

  /* Wait for the SPC Configuration Table to be ready */
  ret = mpiWaitForConfigTable(agRoot, &mainCfg);
  if (AGSA_RC_FAILURE == ret)
  {
    /* return error if MPI Configuration Table not ready */
    SA_DBG1(("mpiInitialize: mpiWaitForConfigTable FAILED ******************************************\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m3");
    return ret;
  }

  /* read scratch pad0 to get PCI BAR and offset of configuration table */
  MSGUCfgTblBase = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
  /* get PCI BAR */
  MSGUCfgTblBase = (MSGUCfgTblBase & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* get pci Bar index */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, MSGUCfgTblBase);

  SA_DBG1(("mpiInitialize: MSGUCfgTblBase = 0x%x\n", MSGUCfgTblBase));
#if defined(SALLSDK_DEBUG)
  /* get Phy count from configuration table */
  phycount = (bit8)((mainCfg.ContrlCapFlag & PHY_COUNT_BITS) >> SHIFT19);

  SA_DBG1(("mpiInitialize: Number of PHYs = 0x%x\n", phycount));

  smTrace(hpDBG_VERY_LOUD,"70",phycount);
  /* TP:70 phycount */
#endif /* SALLSDK_DEBUG */

  /* get High Priority IQ support flag */
  if (mainCfg.ContrlCapFlag & HP_SUPPORT_BIT)
  {
    SA_DBG1(("mpiInitialize: High Priority IQ support from SPC\n"));
  }
  /* get Interrupt Coalescing Support flag */
  if (mainCfg.ContrlCapFlag & INT_COL_BIT)
  {
    SA_DBG1(("mpiInitialize: Interrupt Coalescing support from SPC\n"));
  }

  /* get configured the number of inbound/outbound queues */
  if (memoryAllocated->count == TOTAL_MPI_MEM_CHUNKS)
  {
    config->maxNumInboundQueues  = AGSA_MAX_INBOUND_Q;
    config->maxNumOutboundQueues = AGSA_MAX_OUTBOUND_Q;
  }
  else
  {
    config->maxNumInboundQueues  = config->numInboundQueues;
    config->maxNumOutboundQueues = config->numOutboundQueues;
    maxinbound  = config->numInboundQueues;
    maxoutbound = config->numOutboundQueues;
  }

  SA_DBG1(("mpiInitialize: Number of IQ %d\n", maxinbound));
  SA_DBG1(("mpiInitialize: Number of OQ %d\n", maxoutbound));

  /* get inbound queue offset */
  inboundoffset = mainCfg.inboundQueueOffset;
  /* get outbound queue offset */
  outboundoffset = mainCfg.outboundQueueOffset;

  if(smIS_SPCV(agRoot))
  {
    SA_DBG2(("mpiInitialize: Offset of IQ %d\n", (inboundoffset & 0xFF000000) >> 24));
    SA_DBG2(("mpiInitialize: Offset of OQ %d\n", (outboundoffset & 0xFF000000) >> 24));
    inboundoffset &= 0x00FFFFFF;
    outboundoffset &= 0x00FFFFFF;
  }
  /* get offset of the configuration table */
  MSGUCfgTblDWIdx = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
  MSGUCfgTblDWIdx = MSGUCfgTblDWIdx & SCRATCH_PAD0_OFFSET_MASK;

  saveOffset = MSGUCfgTblDWIdx;

  /* Checks if the configuration memory region size is the same as the mpiConfigMain */
  if(memoryAllocated->region[mIdx].totalLength != sizeof(bit8) * config->mainConfig.eventLogSize)
  {
    SA_DBG1(("ERROR: The memory region [%d] 0x%X != 0x%X does not have the size of the MSGU event log ******************************************\n",
      mIdx,memoryAllocated->region[mIdx].totalLength,config->mainConfig.eventLogSize));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "m3");
    return AGSA_RC_FAILURE;
  }

  mainCfg.iQNPPD_HPPD_GEvent               = config->mainConfig.iQNPPD_HPPD_GEvent;

  if(smIS_SPCV(agRoot))
  {
    mainCfg.outboundHWEventPID0_3            = 0;
    mainCfg.outboundHWEventPID4_7            = 0;
    mainCfg.outboundNCQEventPID0_3           = 0;
    mainCfg.outboundNCQEventPID4_7           = 0;
    mainCfg.outboundTargetITNexusEventPID0_3 = 0;
    mainCfg.outboundTargetITNexusEventPID4_7 = 0;
    mainCfg.outboundTargetSSPEventPID0_3     = 0;
    mainCfg.outboundTargetSSPEventPID4_7     = 0;
    mainCfg.ioAbortDelay                     = 0;  /* SPCV reserved */
    mainCfg.custset                          = 0;
    mainCfg.portRecoveryResetTimer           = config->mainConfig.PortRecoveryTimerPortResetTimer;
    SA_DBG1(("mpiInitialize:custset V                %8X\n",mainCfg.custset));
    SA_DBG1(("mpiInitialize:portRecoveryResetTimer V %8X\n",mainCfg.portRecoveryResetTimer));

    mainCfg.interruptReassertionDelay        = saRoot->hwConfig.intReassertionOption;
    SA_DBG1(("mpiInitialize:interruptReassertionDelay V %8X\n", mainCfg.interruptReassertionDelay));


  }
  else
  {
    mainCfg.outboundHWEventPID0_3            = config->mainConfig.outboundHWEventPID0_3;
    mainCfg.outboundHWEventPID4_7            = config->mainConfig.outboundHWEventPID4_7;
    mainCfg.outboundNCQEventPID0_3           = config->mainConfig.outboundNCQEventPID0_3;
    mainCfg.outboundNCQEventPID4_7           = config->mainConfig.outboundNCQEventPID4_7;
    mainCfg.outboundTargetITNexusEventPID0_3 = config->mainConfig.outboundTargetITNexusEventPID0_3;
    mainCfg.outboundTargetITNexusEventPID4_7 = config->mainConfig.outboundTargetITNexusEventPID4_7;
    mainCfg.outboundTargetSSPEventPID0_3     = config->mainConfig.outboundTargetSSPEventPID0_3;
    mainCfg.outboundTargetSSPEventPID4_7     = config->mainConfig.outboundTargetSSPEventPID4_7;
    mainCfg.ioAbortDelay                     = config->mainConfig.ioAbortDelay;
    mainCfg.custset                          = config->mainConfig.custset;

    SA_DBG1(("mpiInitialize:custset spc     %8X\n",mainCfg.custset));

  }
#ifdef FW_EVT_LOG_TST
  eventLogAddress = memoryAllocated->region[mIdx].virtPtr;
#endif
  mainCfg.upperEventLogAddress             = memoryAllocated->region[mIdx].physAddrUpper;
  mainCfg.lowerEventLogAddress             = memoryAllocated->region[mIdx].physAddrLower;
  mainCfg.eventLogSize                     = config->mainConfig.eventLogSize;
  mainCfg.eventLogOption                   = config->mainConfig.eventLogOption;

  mIdx++;

  /* Checks if the configuration memory region size is the same as the mpiConfigMain */
  if(memoryAllocated->region[mIdx].totalLength != sizeof(bit8) * config->mainConfig.IOPeventLogSize)
  {
    SA_DBG1(("ERROR: The memory region does not have the size of the IOP event log\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "m3");
    return AGSA_RC_FAILURE;
  }

  mainCfg.upperIOPeventLogAddress     = memoryAllocated->region[mIdx].physAddrUpper;
  mainCfg.lowerIOPeventLogAddress     = memoryAllocated->region[mIdx].physAddrLower;
  mainCfg.IOPeventLogSize             = config->mainConfig.IOPeventLogSize;
  mainCfg.IOPeventLogOption           = config->mainConfig.IOPeventLogOption;
  mainCfg.FatalErrorInterrupt         = config->mainConfig.FatalErrorInterrupt;

  SA_DBG1(("mpiInitialize: iQNPPD_HPPD_GEvent 0x%x\n", mainCfg.iQNPPD_HPPD_GEvent));
  if(smIS_SPCV(agRoot))
  {
  }
  else
  {
    SA_DBG3(("mpiInitialize: outboundHWEventPID0_3 0x%x\n", mainCfg.outboundHWEventPID0_3));
    SA_DBG3(("mpiInitialize: outboundHWEventPID4_7 0x%x\n", mainCfg.outboundHWEventPID4_7));
    SA_DBG3(("mpiInitialize: outboundNCQEventPID0_3 0x%x\n", mainCfg.outboundNCQEventPID0_3));
    SA_DBG3(("mpiInitialize: outboundNCQEventPID4_7 0x%x\n", mainCfg.outboundNCQEventPID4_7));
    SA_DBG3(("mpiInitialize: outboundTargetITNexusEventPID0_3 0x%x\n", mainCfg.outboundTargetITNexusEventPID0_3));
    SA_DBG3(("mpiInitialize: outboundTargetITNexusEventPID4_7 0x%x\n", mainCfg.outboundTargetITNexusEventPID4_7));
    SA_DBG3(("mpiInitialize: outboundTargetSSPEventPID0_3 0x%x\n", mainCfg.outboundTargetSSPEventPID0_3));
    SA_DBG3(("mpiInitialize: outboundTargetSSPEventPID4_7 0x%x\n", mainCfg.outboundTargetSSPEventPID4_7));
  }

  SA_DBG3(("mpiInitialize: upperEventLogAddress 0x%x\n", mainCfg.upperEventLogAddress));
  SA_DBG3(("mpiInitialize: lowerEventLogAddress 0x%x\n", mainCfg.lowerEventLogAddress));
  SA_DBG3(("mpiInitialize: eventLogSize 0x%x\n", mainCfg.eventLogSize));
  SA_DBG3(("mpiInitialize: eventLogOption 0x%x\n", mainCfg.eventLogOption));
#ifdef FW_EVT_LOG_TST
  SA_DBG3(("mpiInitialize: eventLogAddress 0x%p\n", eventLogAddress));
#endif
  SA_DBG3(("mpiInitialize: upperIOPLogAddress 0x%x\n", mainCfg.upperIOPeventLogAddress));
  SA_DBG3(("mpiInitialize: lowerIOPLogAddress 0x%x\n", mainCfg.lowerIOPeventLogAddress));
  SA_DBG3(("mpiInitialize: IOPeventLogSize 0x%x\n", mainCfg.IOPeventLogSize));
  SA_DBG3(("mpiInitialize: IOPeventLogOption 0x%x\n", mainCfg.IOPeventLogOption));
  SA_DBG3(("mpiInitialize: FatalErrorInterrupt 0x%x\n", mainCfg.FatalErrorInterrupt));
  SA_DBG3(("mpiInitialize: HDAModeFlags 0x%x\n", mainCfg.HDAModeFlags));
  SA_DBG3(("mpiInitialize: analogSetupTblOffset 0x%08x\n", mainCfg.analogSetupTblOffset));

  saRoot->mainConfigTable.iQNPPD_HPPD_GEvent               = mainCfg.iQNPPD_HPPD_GEvent;

  if(smIS_SPCV(agRoot))
  {
  /* SPCV - reserved fields */
    saRoot->mainConfigTable.outboundHWEventPID0_3            = 0;
    saRoot->mainConfigTable.outboundHWEventPID4_7            = 0;
    saRoot->mainConfigTable.outboundNCQEventPID0_3           = 0;
    saRoot->mainConfigTable.outboundNCQEventPID4_7           = 0;
    saRoot->mainConfigTable.outboundTargetITNexusEventPID0_3 = 0;
    saRoot->mainConfigTable.outboundTargetITNexusEventPID4_7 = 0;
    saRoot->mainConfigTable.outboundTargetSSPEventPID0_3     = 0;
    saRoot->mainConfigTable.outboundTargetSSPEventPID4_7     = 0;
    saRoot->mainConfigTable.ioAbortDelay                     = 0;
    saRoot->mainConfigTable.custset                          = 0;

  }
  else
  {
    saRoot->mainConfigTable.outboundHWEventPID0_3            = mainCfg.outboundHWEventPID0_3;
    saRoot->mainConfigTable.outboundHWEventPID4_7            = mainCfg.outboundHWEventPID4_7;
    saRoot->mainConfigTable.outboundNCQEventPID0_3           = mainCfg.outboundNCQEventPID0_3;
    saRoot->mainConfigTable.outboundNCQEventPID4_7           = mainCfg.outboundNCQEventPID4_7;
    saRoot->mainConfigTable.outboundTargetITNexusEventPID0_3 = mainCfg.outboundTargetITNexusEventPID0_3;
    saRoot->mainConfigTable.outboundTargetITNexusEventPID4_7 = mainCfg.outboundTargetITNexusEventPID4_7;
    saRoot->mainConfigTable.outboundTargetSSPEventPID0_3     = mainCfg.outboundTargetSSPEventPID0_3;
    saRoot->mainConfigTable.outboundTargetSSPEventPID4_7     = mainCfg.outboundTargetSSPEventPID4_7;
    saRoot->mainConfigTable.ioAbortDelay                     = mainCfg.ioAbortDelay;
    saRoot->mainConfigTable.custset                          = mainCfg.custset;

  }

  saRoot->mainConfigTable.upperEventLogAddress             = mainCfg.upperEventLogAddress;
  saRoot->mainConfigTable.lowerEventLogAddress             = mainCfg.lowerEventLogAddress;
  saRoot->mainConfigTable.eventLogSize                     = mainCfg.eventLogSize;
  saRoot->mainConfigTable.eventLogOption                   = mainCfg.eventLogOption;
  saRoot->mainConfigTable.upperIOPeventLogAddress          = mainCfg.upperIOPeventLogAddress;
  saRoot->mainConfigTable.lowerIOPeventLogAddress          = mainCfg.lowerIOPeventLogAddress;
  saRoot->mainConfigTable.IOPeventLogSize                  = mainCfg.IOPeventLogSize;
  saRoot->mainConfigTable.IOPeventLogOption                = mainCfg.IOPeventLogOption;
  saRoot->mainConfigTable.FatalErrorInterrupt              = mainCfg.FatalErrorInterrupt;


  if(smIS_SPCV(agRoot))
  {
    ;/* SPCV - reserved fields */
  }
  else
  {
    saRoot->mainConfigTable.HDAModeFlags                     = mainCfg.HDAModeFlags;
  }

  saRoot->mainConfigTable.analogSetupTblOffset             = mainCfg.analogSetupTblOffset;

  smTrace(hpDBG_VERY_LOUD,"71",mIdx);
  /* TP:71 71 mIdx  */



  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IQNPPD_HPPD_OFFSET),
                     mainCfg.iQNPPD_HPPD_GEvent);

  SA_DBG3(("mpiInitialize: Offset 0x%08x mainCfg.iQNPPD_HPPD_GEvent 0x%x\n", (bit32)(MSGUCfgTblDWIdx + MAIN_IQNPPD_HPPD_OFFSET), mainCfg.iQNPPD_HPPD_GEvent));

  if(smIS_SPC6V(agRoot))
  {
    if(smIsCfgVREV_B(agRoot))
    {
      ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IO_ABORT_DELAY),
                     MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE);

      SA_DBG1(("mpiInitialize:SPCV - MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE\n" ));
    }
    if(smIsCfgVREV_C(agRoot))
    {
      SA_DBG1(("mpiInitialize:SPCV - END_TO_END_CRC On\n" ));
    }
    SA_DBG3(("mpiInitialize:SPCV - rest reserved field  \n" ));
    ;/* SPCV - reserved field */
  }
  else if(smIS_SPC(agRoot))
  {
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_OB_HW_EVENT_PID03_OFFSET),
                       mainCfg.outboundHWEventPID0_3);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_OB_HW_EVENT_PID47_OFFSET),
                       mainCfg.outboundHWEventPID4_7);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_OB_NCQ_EVENT_PID03_OFFSET),
                       mainCfg.outboundNCQEventPID0_3);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_OB_NCQ_EVENT_PID47_OFFSET),
                       mainCfg.outboundNCQEventPID4_7);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_TITNX_EVENT_PID03_OFFSET),
                       mainCfg.outboundTargetITNexusEventPID0_3);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_TITNX_EVENT_PID47_OFFSET),
                       mainCfg.outboundTargetITNexusEventPID4_7);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_OB_SSP_EVENT_PID03_OFFSET),
                       mainCfg.outboundTargetSSPEventPID0_3);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_OB_SSP_EVENT_PID47_OFFSET),
                       mainCfg.outboundTargetSSPEventPID4_7);
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_CUSTOMER_SETTING),
                       mainCfg.custset);
  }else
  {
    if(smIsCfgVREV_A(agRoot))
    {
       ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IO_ABORT_DELAY),
                     MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE);  /* */
       SA_DBG1(("mpiInitialize:SPCV12G - offset MAIN_IO_ABORT_DELAY 0x%x value MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE 0x%x\n",MAIN_IO_ABORT_DELAY ,MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE));
       SA_DBG1(("mpiInitialize:SPCV12G - END_TO_END_CRC OFF for rev A %d\n",smIsCfgVREV_A(agRoot) ));
    }
    else if(smIsCfgVREV_B(agRoot))
    {
       SA_DBG1(("mpiInitialize:SPCV12G - END_TO_END_CRC ON rev B %d ****************************\n",smIsCfgVREV_B(agRoot) ));
       /*ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IO_ABORT_DELAY),
                     MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE); 
       */
    }
    else if(smIsCfgVREV_C(agRoot))
    {
       SA_DBG1(("mpiInitialize:SPCV12G - END_TO_END_CRC on rev C %d\n",smIsCfgVREV_C(agRoot) ));
    }
    else
    {
       ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IO_ABORT_DELAY),
                     MAIN_IO_ABORT_DELAY_END_TO_END_CRC_DISABLE);
       SA_DBG1(("mpiInitialize:SPCV12G - END_TO_END_CRC Off unknown rev 0x%x\n", ossaHwRegReadConfig32((agRoot), 8 )));
    }
  }

  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_ADDR_HI),       mainCfg.upperEventLogAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_ADDR_LO),       mainCfg.lowerEventLogAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_BUFF_SIZE),     mainCfg.eventLogSize);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_OPTION),        mainCfg.eventLogOption);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_ADDR_HI),   mainCfg.upperIOPeventLogAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_ADDR_LO),   mainCfg.lowerIOPeventLogAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_BUFF_SIZE), mainCfg.IOPeventLogSize);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_OPTION),    mainCfg.IOPeventLogOption);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_INTERRUPT),   mainCfg.FatalErrorInterrupt);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_PRECTD_PRESETD),          mainCfg.portRecoveryResetTimer);

  SA_DBG3(("mpiInitialize: Offset 0x%08x upperEventLogAddress    0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_ADDR_HI), mainCfg.upperEventLogAddress ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x lowerEventLogAddress    0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_ADDR_LO), mainCfg.lowerEventLogAddress ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x eventLogSize            0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_BUFF_SIZE), mainCfg.eventLogSize ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x eventLogOption          0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_OPTION), mainCfg.eventLogOption ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x upperIOPeventLogAddress 0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_ADDR_HI), mainCfg.upperIOPeventLogAddress ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x lowerIOPeventLogAddress 0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_ADDR_LO), mainCfg.lowerIOPeventLogAddress ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x IOPeventLogSize         0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_BUFF_SIZE), mainCfg.IOPeventLogSize ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x IOPeventLogOption       0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_OPTION), mainCfg.IOPeventLogOption ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x FatalErrorInterrupt     0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_INTERRUPT), mainCfg.FatalErrorInterrupt ));
  SA_DBG3(("mpiInitialize: Offset 0x%08x PortRecoveryResetTimer  0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_PRECTD_PRESETD), mainCfg.portRecoveryResetTimer ));

  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IRAD_RESERVED),           mainCfg.interruptReassertionDelay);
  SA_DBG3(("mpiInitialize: Offset 0x%08x InterruptReassertionDelay 0x%x\n",(bit32)(MSGUCfgTblDWIdx + MAIN_IRAD_RESERVED), mainCfg.interruptReassertionDelay ));

  mIdx++;

  /* skip the ci and pi memory region */
  mIdx++;
  mIdx++;

  smTrace(hpDBG_VERY_LOUD,"72",mIdx);
  /* TP:72 mIdx  */
  smTrace(hpDBG_VERY_LOUD,"Bc",maxinbound);
  /* TP:Bc  maxinbound  */
  smTrace(hpDBG_VERY_LOUD,"Bd",pcibar);
  /* TP:Bd pcibar   */

  /* index offset */
  indexoffset = 0;
  memOffset   = 0;

  /* Memory regions for the inbound queues */
  for(qIdx = 0; qIdx < maxinbound; qIdx++)
  {
    /* point back to the begin then plus offset to next queue */
    smTrace(hpDBG_VERY_LOUD,"Bd",pcibar);
    /* TP:Bd pcibar   */
    MSGUCfgTblDWIdx = saveOffset;
    MSGUCfgTblDWIdx += inboundoffset;
    MSGUCfgTblDWIdx += (sizeof(spc_inboundQueueDescriptor_t) * qIdx);
    SA_DBG1(("mpiInitialize: A saveOffset 0x%x MSGUCfgTblDWIdx 0x%x\n",saveOffset ,MSGUCfgTblDWIdx));

    /* if the MPI configuration says that this queue is disabled ... */
    if(0 == config->inboundQueues[qIdx].numElements)
    {
      /* ... Clears the configuration table for this queue */

      inQueueCfg.elementPriSizeCount= 0;
      inQueueCfg.upperBaseAddress = 0;
      inQueueCfg.lowerBaseAddress = 0;
      inQueueCfg.ciUpperBaseAddress = 0;
      inQueueCfg.ciLowerBaseAddress = 0;
      /* skip inQueueCfg.PIPCIBar (PM8000 write access) */
      /* skip inQueueCfg.PIOffset (PM8000 write access) */

      /* Update the inbound configuration table in SPC GSM */
      mpiUpdateIBQueueCfgTable(agRoot, &inQueueCfg, MSGUCfgTblDWIdx, pcibar);
    }

    /* If the queue is enabled, then ... */
    else
    {
      bit32 memSize = config->inboundQueues[qIdx].numElements * config->inboundQueues[qIdx].elementSize;
      bit32 remainder = memSize & 127;

      /* Calculate the size of this queue padded to 128 bytes */
      if (remainder > 0)
      {
        memSize += (128 - remainder);
      }

      /* ... first checks that the memory region has the right size */
      if( (memoryAllocated->region[mIdx].totalLength - memOffset < memSize) ||
          (NULL == memoryAllocated->region[mIdx].virtPtr) ||
          (0 == memoryAllocated->region[mIdx].totalLength))
      {
        SA_DBG1(("mpiInitialize: ERROR The memory region does not have the right size for this inbound queue"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "m3");
        return AGSA_RC_FAILURE;
      }
      else
      {
        /* Then, using the MPI configuration argument, initializes the corresponding element on the saRoot */
        saRoot->inboundQueue[qIdx].numElements  = config->inboundQueues[qIdx].numElements;
        saRoot->inboundQueue[qIdx].elementSize  = config->inboundQueues[qIdx].elementSize;
        saRoot->inboundQueue[qIdx].priority     = config->inboundQueues[qIdx].priority;
        si_memcpy(&saRoot->inboundQueue[qIdx].memoryRegion, &memoryAllocated->region[mIdx], sizeof(mpiMem_t));
        saRoot->inboundQueue[qIdx].memoryRegion.virtPtr =
          (bit8 *)saRoot->inboundQueue[qIdx].memoryRegion.virtPtr + memOffset;
        saRoot->inboundQueue[qIdx].memoryRegion.physAddrLower += memOffset;
        saRoot->inboundQueue[qIdx].memoryRegion.elementSize = memSize;
        saRoot->inboundQueue[qIdx].memoryRegion.totalLength = memSize;
        saRoot->inboundQueue[qIdx].memoryRegion.numElements = 1;

        /* Initialize the local copy of PIs, CIs */
        SA_DBG1(("mpiInitialize: queue %d PI CI zero\n",qIdx));
        saRoot->inboundQueue[qIdx].producerIdx = 0;
        saRoot->inboundQueue[qIdx].consumerIdx = 0;
        saRoot->inboundQueue[qIdx].agRoot = agRoot;

        /* MPI memory region for inbound CIs are 2 */
        saRoot->inboundQueue[qIdx].ciPointer = (((bit8 *)(memoryAllocated->region[MPI_CI_INDEX].virtPtr)) + qIdx * 4);
        /* ... and in the local structure we will use to copy to the HW configuration table */

        /* CI base address */
        inQueueCfg.elementPriSizeCount= config->inboundQueues[qIdx].numElements |
                                        (config->inboundQueues[qIdx].elementSize << SHIFT16) |
                                        (config->inboundQueues[qIdx].priority << SHIFT30);
        inQueueCfg.upperBaseAddress   = saRoot->inboundQueue[qIdx].memoryRegion.physAddrUpper;
        inQueueCfg.lowerBaseAddress   = saRoot->inboundQueue[qIdx].memoryRegion.physAddrLower;
        inQueueCfg.ciUpperBaseAddress = memoryAllocated->region[MPI_CI_INDEX].physAddrUpper;
        inQueueCfg.ciLowerBaseAddress = memoryAllocated->region[MPI_CI_INDEX].physAddrLower + qIdx * 4;

        /* write the configured data of inbound queue to SPC GSM */
        mpiUpdateIBQueueCfgTable(agRoot, &inQueueCfg, MSGUCfgTblDWIdx, pcibar);
        /* get inbound PI PCI Bar and Offset */
        /* get the PI PCI Bar offset and convert it to logical BAR */
        IB_PIPCIBar = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + IB_PIPCI_BAR));
        saRoot->inboundQueue[qIdx].PIPCIBar     = mpiGetPCIBarIndex(agRoot, IB_PIPCIBar);
        saRoot->inboundQueue[qIdx].PIPCIOffset  = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + IB_PIPCI_BAR_OFFSET));
        saRoot->inboundQueue[qIdx].qNumber      = qIdx;

        memOffset += memSize;

        if ((0 == ((qIdx + 1) % MAX_QUEUE_EACH_MEM)) ||
            (qIdx == (maxinbound - 1)))
        {
          mIdx++;
          indexoffset += MAX_QUEUE_EACH_MEM;
          memOffset = 0;
        }

      } /* else for memeory ok */
    } /* queue enable */
  } /* loop for inbound queue */

  smTrace(hpDBG_VERY_LOUD,"73",0);
  /* TP:73  outbound queues  */

  /* index offset */
  indexoffset = 0;
  memOffset = 0;
  /* Let's process the memory regions for the outbound queues */
  for(qIdx = 0; qIdx < maxoutbound; qIdx++)
  {
    /* point back to the begin then plus offset to next queue */
    MSGUCfgTblDWIdx  = saveOffset;
    MSGUCfgTblDWIdx += outboundoffset;
    MSGUCfgTblDWIdx += (sizeof(spc_outboundQueueDescriptor_t) * qIdx);

    /* if the MPI configuration says that this queue is disabled ... */
    if(0 == config->outboundQueues[qIdx].numElements)
    {
      /* ... Clears the configuration table for this queue */
      outQueueCfg.upperBaseAddress   = 0;
      outQueueCfg.lowerBaseAddress   = 0;
      outQueueCfg.piUpperBaseAddress = 0;
      outQueueCfg.piLowerBaseAddress = 0;
      /* skip outQueueCfg.CIPCIBar = 0; read access only */
      /* skip outQueueCfg.CIOffset = 0; read access only */
      outQueueCfg.elementSizeCount     = 0;
      outQueueCfg.interruptVecCntDelay = 0;

      /* Updated the configuration table in SPC GSM */
      mpiUpdateOBQueueCfgTable(agRoot, &outQueueCfg, MSGUCfgTblDWIdx, pcibar);
    }

    /* If the outbound queue is enabled, then ... */
    else
    {
      bit32 memSize = config->outboundQueues[qIdx].numElements * config->outboundQueues[qIdx].elementSize;
      bit32 remainder = memSize & 127;

      /* Calculate the size of this queue padded to 128 bytes */
      if (remainder > 0)
      {
          memSize += (128 - remainder);
      }

      /* ... first checks that the memory region has the right size */
      if((memoryAllocated->region[mIdx].totalLength - memOffset < memSize) ||
         (NULL == memoryAllocated->region[mIdx].virtPtr) ||
         (0 == memoryAllocated->region[mIdx].totalLength))
      {
        SA_DBG1(("ERROR: The memory region does not have the right size for this outbound queue"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "m3");
        return AGSA_RC_FAILURE;
      }
      else
      {
        /* Then, using the MPI configuration argument, initializes the corresponding element on the MPI context ... */
        saRoot->outboundQueue[qIdx].numElements  = config->outboundQueues[qIdx].numElements;
        saRoot->outboundQueue[qIdx].elementSize  = config->outboundQueues[qIdx].elementSize;
        si_memcpy(&saRoot->outboundQueue[qIdx].memoryRegion, &memoryAllocated->region[mIdx], sizeof(mpiMem_t));
        saRoot->outboundQueue[qIdx].memoryRegion.virtPtr =
            (bit8 *)saRoot->outboundQueue[qIdx].memoryRegion.virtPtr + memOffset;
        saRoot->outboundQueue[qIdx].memoryRegion.physAddrLower += memOffset;
        saRoot->outboundQueue[qIdx].memoryRegion.elementSize = memSize;
        saRoot->outboundQueue[qIdx].memoryRegion.totalLength = memSize;
        saRoot->outboundQueue[qIdx].memoryRegion.numElements = 1;
        saRoot->outboundQueue[qIdx].producerIdx = 0;
        saRoot->outboundQueue[qIdx].consumerIdx = 0;
        saRoot->outboundQueue[qIdx].agRoot = agRoot;

        /* MPI memory region for outbound PIs are 3 */
        saRoot->outboundQueue[qIdx].piPointer = (((bit8 *)(memoryAllocated->region[MPI_CI_INDEX + 1].virtPtr))+ qIdx * 4);
        /* ... and in the local structure we will use to copy to the HW configuration table */
        outQueueCfg.upperBaseAddress = saRoot->outboundQueue[qIdx].memoryRegion.physAddrUpper;
        outQueueCfg.lowerBaseAddress = saRoot->outboundQueue[qIdx].memoryRegion.physAddrLower;

        /* PI base address */
        outQueueCfg.piUpperBaseAddress = memoryAllocated->region[MPI_CI_INDEX + 1].physAddrUpper;
        outQueueCfg.piLowerBaseAddress = memoryAllocated->region[MPI_CI_INDEX + 1].physAddrLower + qIdx * 4;
        outQueueCfg.elementSizeCount = config->outboundQueues[qIdx].numElements |
                                       (config->outboundQueues[qIdx].elementSize << SHIFT16);

        /* enable/disable interrupt - use saSystemInterruptsActive() API */
        /* instead of ossaHwRegWrite(agRoot, MSGU_ODMR, 0); */
        /* Outbound Doorbell Auto disable */
        /* LL does not use ossaHwRegWriteExt(agRoot, PCIBAR1, SPC_ODAR, 0xffffffff); */
        if (config->outboundQueues[qIdx].interruptEnable)
        {
          /* enable interrupt flag bit30 of outbound table */
          outQueueCfg.elementSizeCount |= OB_PROPERTY_INT_ENABLE;
        }
        if(smIS_SPCV(agRoot))
        {
          outQueueCfg.interruptVecCntDelay = ((config->outboundQueues[qIdx].interruptVector    & INT_VEC_BITS  ) << SHIFT24);
        }
        else
        {
          outQueueCfg.interruptVecCntDelay =  (config->outboundQueues[qIdx].interruptDelay     & INT_DELAY_BITS)             |
                                             ((config->outboundQueues[qIdx].interruptThreshold & INT_THR_BITS  ) << SHIFT16) |
                                             ((config->outboundQueues[qIdx].interruptVector    & INT_VEC_BITS  ) << SHIFT24);
        }

        /* create a VectorIndex Bit Map */
        if (qIdx < OQ_NUM_32)
        {
          saRoot->interruptVecIndexBitMap[config->outboundQueues[qIdx].interruptVector] |= (1 << qIdx);
          SA_DBG2(("mpiInitialize:below 32 saRoot->interruptVecIndexBitMap[config->outboundQueues[qIdx].interruptVector] 0x%08x\n",saRoot->interruptVecIndexBitMap[config->outboundQueues[qIdx].interruptVector]));
        }
        else
        {
          saRoot->interruptVecIndexBitMap1[config->outboundQueues[qIdx].interruptVector] |= (1 << (qIdx - OQ_NUM_32));
          SA_DBG2(("mpiInitialize:Above 32 saRoot->interruptVecIndexBitMap1[config->outboundQueues[qIdx].interruptVector] 0x%08x\n",saRoot->interruptVecIndexBitMap1[config->outboundQueues[qIdx].interruptVector]));
        }
        /* Update the outbound configuration table */
        mpiUpdateOBQueueCfgTable(agRoot, &outQueueCfg, MSGUCfgTblDWIdx, pcibar);

        /* read the CI PCIBar offset and convert it to logical bar */
        OB_CIPCIBar = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + OB_CIPCI_BAR));
        saRoot->outboundQueue[qIdx].CIPCIBar    = mpiGetPCIBarIndex(agRoot, OB_CIPCIBar);
        saRoot->outboundQueue[qIdx].CIPCIOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + OB_CIPCI_BAR_OFFSET));
        saRoot->outboundQueue[qIdx].DIntTOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + OB_DYNAMIC_COALES_OFFSET));
        saRoot->outboundQueue[qIdx].qNumber      = qIdx;

        memOffset += memSize;

        if ((0 == ((qIdx + 1) % MAX_QUEUE_EACH_MEM)) ||
            (qIdx == (maxoutbound - 1)))
        {
          mIdx++;
          indexoffset += MAX_QUEUE_EACH_MEM;
          memOffset =0;
        }
      }
    }
  }

  /* calculate number of vectors */
  saRoot->numInterruptVectors = 0;
  for (qIdx = 0; qIdx < MAX_NUM_VECTOR; qIdx++)
  {
    if ((saRoot->interruptVecIndexBitMap[qIdx]) || (saRoot->interruptVecIndexBitMap1[qIdx]))
    {
      (saRoot->numInterruptVectors)++;
    }
  }

  SA_DBG2(("mpiInitialize:(saRoot->numInterruptVectors) 0x%x\n",(saRoot->numInterruptVectors)));

  if(smIS_SPCV(agRoot))
  {
    /* setup interrupt vector table  */
    mpiWrIntVecTable(agRoot,config);
  }

  if(smIS_SPCV(agRoot))
  {
    mpiWrAnalogSetupTable(agRoot,config);
  }

  /* setup phy analog registers */
  mpiWriteCALAll(agRoot, &config->phyAnalogConfig);

  {
    bit32 pcibar = 0;
    bit32 TableOffset;
    pcibar = siGetPciBar(agRoot);
    TableOffset = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
    TableOffset &= SCRATCH_PAD0_OFFSET_MASK;
    SA_DBG1(("mpiInitialize: mpiContextTable TableOffset 0x%08X contains 0x%08X\n",TableOffset,ossaHwRegReadExt(agRoot, pcibar, TableOffset )));

    SA_ASSERT( (ossaHwRegReadExt(agRoot, pcibar, TableOffset ) == 0x53434D50), "Config table signiture");

    SA_DBG1(("mpiInitialize: AGSA_MPI_MAIN_CONFIGURATION_TABLE           0x%08X\n", 0));
    SA_DBG1(("mpiInitialize: AGSA_MPI_GENERAL_STATUS_TABLE               0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_GST_OFFSET) & 0xFFFF )));
    SA_DBG1(("mpiInitialize: AGSA_MPI_INBOUND_QUEUE_CONFIGURATION_TABLE  0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_IBQ_OFFSET)  & 0xFFFF)));
    SA_DBG1(("mpiInitialize: AGSA_MPI_OUTBOUND_QUEUE_CONFIGURATION_TABLE 0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_OBQ_OFFSET)  & 0xFFFF)));
    SA_DBG1(("mpiInitialize: AGSA_MPI_SAS_PHY_ANALOG_SETUP_TABLE         0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_ANALOG_SETUP_OFFSET) & 0xFFFF )));
    SA_DBG1(("mpiInitialize: AGSA_MPI_INTERRUPT_VECTOR_TABLE             0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_INT_VEC_TABLE_OFFSET) & 0xFFFF)));
    SA_DBG1(("mpiInitialize: AGSA_MPI_PER_SAS_PHY_ATTRIBUTE_TABLE        0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_PHY_ATTRIBUTE_OFFSET) & 0xFFFF)));
    SA_DBG1(("mpiInitialize: AGSA_MPI_OUTBOUND_QUEUE_FAILOVER_TABLE      0x%08X\n", (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_MOQFOT_MOQFOES) & 0xFFFF)));

  }

  if(agNULL !=  saRoot->swConfig.mpiContextTable )
  {
    agsaMPIContext_t * context = (agsaMPIContext_t * )saRoot->swConfig.mpiContextTable;
    bit32 length = saRoot->swConfig.mpiContextTablelen;
    bit32 pcibar = 0;
    bit32 TableOffset;
    pcibar = siGetPciBar(agRoot);
    TableOffset = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
    TableOffset &= SCRATCH_PAD0_OFFSET_MASK;
    SA_DBG1(("mpiInitialize: mpiContextTable TableOffset 0x%08X contains 0x%08X\n",TableOffset,ossaHwRegReadExt(agRoot, pcibar, TableOffset )));

    SA_ASSERT( (ossaHwRegReadExt(agRoot, pcibar, TableOffset ) == 0x53434D50), "Config table signiture");
    if ( (ossaHwRegReadExt(agRoot, pcibar, TableOffset ) != 0x53434D50))
    {
      SA_DBG1(("mpiInitialize: TableOffset 0x%x reads 0x%x expect 0x%x \n",TableOffset,ossaHwRegReadExt(agRoot, pcibar, TableOffset ),0x53434D50));
    }

    if(context )
    {
      SA_DBG1(("mpiInitialize: MPITableType 0x%x context->offset 0x%x context->value 0x%x\n",context->MPITableType,context->offset,context->value));
      while( length != 0)
      {
        switch(context->MPITableType)
        {

        bit32 OffsetInMain;
        case AGSA_MPI_MAIN_CONFIGURATION_TABLE:
          SA_DBG1(("mpiInitialize:  AGSA_MPI_MAIN_CONFIGURATION_TABLE %d 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset, context->offset, context->value));
          OffsetInMain = TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4) , context->value);
          break;
        case AGSA_MPI_GENERAL_STATUS_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_GENERAL_STATUS_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType ,TableOffset+MAIN_GST_OFFSET, context->offset, context->value  ));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_GST_OFFSET ) & 0xFFFF) + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        case AGSA_MPI_INBOUND_QUEUE_CONFIGURATION_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_INBOUND_QUEUE_CONFIGURATION_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset+MAIN_IBQ_OFFSET, context->offset, context->value));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_IBQ_OFFSET ) & 0xFFFF)  + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        case AGSA_MPI_OUTBOUND_QUEUE_CONFIGURATION_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_OUTBOUND_QUEUE_CONFIGURATION_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset+MAIN_OBQ_OFFSET, context->offset, context->value));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_OBQ_OFFSET ) & 0xFFFF)  + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        case AGSA_MPI_SAS_PHY_ANALOG_SETUP_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_SAS_PHY_ANALOG_SETUP_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset+MAIN_ANALOG_SETUP_OFFSET, context->offset, context->value));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+ MAIN_ANALOG_SETUP_OFFSET) & 0xFFFF)  + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        case AGSA_MPI_INTERRUPT_VECTOR_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_INTERRUPT_VECTOR_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset+MAIN_INT_VEC_TABLE_OFFSET, context->offset, context->value));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+ MAIN_INT_VEC_TABLE_OFFSET) & 0xFFFF)  + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        case AGSA_MPI_PER_SAS_PHY_ATTRIBUTE_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_PER_SAS_PHY_ATTRIBUTE_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset+MAIN_PHY_ATTRIBUTE_OFFSET, context->offset, context->value));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_PHY_ATTRIBUTE_OFFSET ) & 0xFFFF)  + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        case AGSA_MPI_OUTBOUND_QUEUE_FAILOVER_TABLE:
          SA_DBG1(("mpiInitialize: AGSA_MPI_OUTBOUND_QUEUE_FAILOVER_TABLE %d offset 0x%x + 0x%x = 0x%x\n",context->MPITableType,TableOffset+MAIN_MOQFOT_MOQFOES, context->offset, context->value));
          OffsetInMain = (ossaHwRegReadExt(agRoot, pcibar, TableOffset+MAIN_MOQFOT_MOQFOES ) & 0xFFFF)  + TableOffset;
          ossaHwRegWriteExt(agRoot, pcibar, OffsetInMain + (context->offset * 4), context->value);
          break;
        default:
          SA_DBG1(("mpiInitialize: error MPITableType unknown %d offset 0x%x value 0x%x\n",context->MPITableType, context->offset, context->value));
          break;
        }
        if(smIS_SPC12V(agRoot))
        {
          if (saRoot->ControllerInfo.fwInterfaceRev > 0x301 )
          {
            SA_DBG1(("mpiInitialize: MAIN_AWT_MIDRANGE 0x%08X\n",
                    ossaHwRegReadExt(agRoot, pcibar, TableOffset + MAIN_AWT_MIDRANGE)
                     ));
          }
        }
        if(length >= sizeof(agsaMPIContext_t))
        {
          length -= sizeof(agsaMPIContext_t);
          context++;

        }
        else
        {
          length = 0;
        }
      }

    }

    SA_DBG1(("mpiInitialize:  context %p saRoot->swConfig.mpiContextTable %p %d\n",context,saRoot->swConfig.mpiContextTable,context == saRoot->swConfig.mpiContextTable ? 1 : 0));

    if ( (ossaHwRegReadExt(agRoot, pcibar, TableOffset ) != 0x53434D50))
    {
      SA_DBG1(("mpiInitialize:TableOffset 0x%x reads 0x%x expect 0x%x \n",TableOffset,ossaHwRegReadExt(agRoot, pcibar, TableOffset ),0x53434D50));
    }

    SA_ASSERT( (ossaHwRegReadExt(agRoot, pcibar, TableOffset ) == 0x53434D50), "Config table signiture After");
  }
  /* At this point the Message Unit configuration table is set up. Now we need to ring the doorbell */
  togglevalue = 0;

  smTrace(hpDBG_VERY_LOUD,"74",  siHalRegReadExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET ));
  /* TP:74 Doorbell */

  /* Write bit0=1 to Inbound DoorBell Register to tell the SPC FW the table is updated */
  siHalRegWriteExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET, SPC_MSGU_CFG_TABLE_UPDATE);

  if(siHalRegReadExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET ) & SPC_MSGU_CFG_TABLE_UPDATE)
  {
    SA_DBG1(("mpiInitialize: SPC_MSGU_CFG_TABLE_UPDATE (0x%X) \n",  siHalRegReadExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET)));
  }
  else
  {
    SA_DBG1(("mpiInitialize: SPC_MSGU_CFG_TABLE_UPDATE not set (0x%X)\n",  siHalRegReadExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET)));
    ossaStallThread(agRoot, WAIT_INCREMENT);
  }

  smTrace(hpDBG_VERY_LOUD,"A5",  siHalRegReadExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET ));
  /* TP:A5 Doorbell */

/*
//  ossaHwRegWrite(agRoot, MSGU_IBDB_SET, SPC_MSGU_CFG_TABLE_UPDATE);
  MSGU_WRITE_IDR(SPC_MSGU_CFG_TABLE_UPDATE);
*/


  /* wait until Inbound DoorBell Clear Register toggled */
WaitLonger:
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    value = MSGU_READ_IDR;
    value &= SPC_MSGU_CFG_TABLE_UPDATE;
  } while ((value != togglevalue) && (max_wait_count -= WAIT_INCREMENT));

  smTrace(hpDBG_VERY_LOUD,"80", max_wait_count);
  /* TP:80 TP max_wait_count */
  if (!max_wait_count &&  mpiStartToggleFailed < 5 )
  {
     SA_DBG1(("mpiInitialize: mpiStartToggleFailed  count %d\n", mpiStartToggleFailed));
     mpiStartToggleFailed++;
    goto WaitLonger;
  }

  if (!max_wait_count )
  {

    SA_DBG1(("mpiInitialize: TIMEOUT:IBDB value/toggle = 0x%x 0x%x\n", value, togglevalue));
    MSGUCfgTblDWIdx = saveOffset;
    GSTLenMPIS = ossaHwRegReadExt(agRoot, pcibar, (bit32)MSGUCfgTblDWIdx + (bit32)(mainCfg.GSTOffset + GST_GSTLEN_MPIS_OFFSET));
    SA_DBG1(("mpiInitialize: MPI State = 0x%x\n", GSTLenMPIS));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "m3");
    return AGSA_RC_FAILURE;
  }
  smTrace(hpDBG_VERY_LOUD,"81", mpiStartToggleFailed );
  /* TP:81 TP */

  /* check the MPI-State for initialization */
  MSGUCfgTblDWIdx = saveOffset;
  GSTLenMPIS = ossaHwRegReadExt(agRoot, pcibar, (bit32)MSGUCfgTblDWIdx + (bit32)(mainCfg.GSTOffset + GST_GSTLEN_MPIS_OFFSET));
  if ( (GST_MPI_STATE_UNINIT == (GSTLenMPIS & GST_MPI_STATE_MASK)) && ( mpiUnInitFailed < 5 ) )
  {
    SA_DBG1(("mpiInitialize: MPI State = 0x%x mpiUnInitFailed count %d\n", GSTLenMPIS & GST_MPI_STATE_MASK,mpiUnInitFailed));
    ossaStallThread(agRoot, (20 * 1000));

    mpiUnInitFailed++;
    goto WaitLonger;
  }

  if (GST_MPI_STATE_INIT != (GSTLenMPIS & GST_MPI_STATE_MASK))
  {
    SA_DBG1(("mpiInitialize: Error Not GST_MPI_STATE_INIT MPI State = 0x%x\n", GSTLenMPIS & GST_MPI_STATE_MASK));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "m3");
    return AGSA_RC_FAILURE;
  }
  smTrace(hpDBG_VERY_LOUD,"82", 0);
  /* TP:82 TP */

  /* check MPI Initialization error */
  GSTLenMPIS = GSTLenMPIS >> SHIFT16;
  if (0x0000 != GSTLenMPIS)
  {
    SA_DBG1(("mpiInitialize: MPI Error = 0x%x\n", GSTLenMPIS));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "m3");
    return AGSA_RC_FAILURE;
  }
  smTrace(hpDBG_VERY_LOUD,"83", 0);
  /* TP:83 TP */

  /* reread IQ PI offset from SPC if IQ/OQ > 32 */
  if ((maxinbound > IQ_NUM_32) || (maxoutbound > OQ_NUM_32))
  {
    for(qIdx = 0; qIdx < maxinbound; qIdx++)
    {
      /* point back to the begin then plus offset to next queue */
      MSGUCfgTblDWIdx = saveOffset;
      MSGUCfgTblDWIdx += inboundoffset;
      MSGUCfgTblDWIdx += (sizeof(spc_inboundQueueDescriptor_t) * qIdx);
      saRoot->inboundQueue[qIdx].PIPCIOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + IB_PIPCI_BAR_OFFSET));
    }
  }
  smTrace(hpDBG_VERY_LOUD,"84", 0);
  /* TP:84 TP */

  /* at least one inbound queue and one outbound queue enabled */
  if ((0 == config->inboundQueues[0].numElements) || (0 == config->outboundQueues[0].numElements))
  {
    SA_DBG1(("mpiInitialize: Error,IQ0 or OQ0 have to enable\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "m3");
    return AGSA_RC_FAILURE;
  }
  smTrace(hpDBG_VERY_LOUD,"85", 0);
  /* TP:85 TP */

  /* clean the inbound queues */
  for (i = 0; i < config->numInboundQueues; i ++)
  {
    if(0 != config->inboundQueues[i].numElements)
    {
      circularIQ = &saRoot->inboundQueue[i];
      si_memset(circularIQ->memoryRegion.virtPtr, 0, circularIQ->memoryRegion.totalLength);
      si_memset(saRoot->inboundQueue[i].ciPointer, 0, sizeof(bit32));

      if(smIS_SPCV(agRoot))
      {
        ossaHwRegWriteExt(circularIQ->agRoot, circularIQ->PIPCIBar, circularIQ->PIPCIOffset, 0);
        SA_DBG1(("mpiInitialize:  SPC V writes IQ %2d offset 0x%x\n",i ,circularIQ->PIPCIOffset));
      }
    }
  }
  smTrace(hpDBG_VERY_LOUD,"86", 0);
  /* TP:86 TP */

  /* clean the outbound queues */
  for (i = 0; i < config->numOutboundQueues; i ++)
  {
    if(0 != config->outboundQueues[i].numElements)
    {
      circularOQ = &saRoot->outboundQueue[i];
      si_memset(circularOQ->memoryRegion.virtPtr, 0, circularOQ->memoryRegion.totalLength);
      si_memset(saRoot->outboundQueue[i].piPointer, 0, sizeof(bit32));
      if(smIS_SPCV(agRoot))
      {
        ossaHwRegWriteExt(circularOQ->agRoot, circularOQ->CIPCIBar, circularOQ->CIPCIOffset, 0);
        SA_DBG2(("mpiInitialize:  SPC V writes OQ %2d offset 0x%x\n",i ,circularOQ->CIPCIOffset));
      }

    }
  }


  smTrace(hpDBG_VERY_LOUD,"75",0);
  /* TP:75 AAP1 IOP */

  /* read back AAP1 and IOP event log address and size */
  MSGUCfgTblDWIdx = saveOffset;
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_ADDR_HI));
  saRoot->mainConfigTable.upperEventLogAddress = value;
  SA_DBG1(("mpiInitialize: upperEventLogAddress 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_ADDR_LO));
  saRoot->mainConfigTable.lowerEventLogAddress = value;
  SA_DBG1(("mpiInitialize: lowerEventLogAddress 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_BUFF_SIZE));
  saRoot->mainConfigTable.eventLogSize = value;
  SA_DBG1(("mpiInitialize: eventLogSize 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_EVENT_LOG_OPTION));
  saRoot->mainConfigTable.eventLogOption = value;
  SA_DBG1(("mpiInitialize: eventLogOption 0x%x\n", value));
  SA_DBG1(("mpiInitialize: EventLog dd /p %08X`%08X L %x\n",saRoot->mainConfigTable.upperEventLogAddress,saRoot->mainConfigTable.lowerEventLogAddress,saRoot->mainConfigTable.eventLogSize/4 ));

  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_ADDR_HI));
  saRoot->mainConfigTable.upperIOPeventLogAddress = value;
  SA_DBG1(("mpiInitialize: upperIOPLogAddress 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_ADDR_LO));
  saRoot->mainConfigTable.lowerIOPeventLogAddress = value;
  SA_DBG1(("mpiInitialize: lowerIOPLogAddress 0x%x\n", value));
  SA_DBG1(("mpiInitialize: IOPLog   dd /p %08X`%08X L %x\n",saRoot->mainConfigTable.upperIOPeventLogAddress,saRoot->mainConfigTable.lowerIOPeventLogAddress,saRoot->mainConfigTable.IOPeventLogSize/4 ));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_BUFF_SIZE));
  saRoot->mainConfigTable.IOPeventLogSize = value;
  SA_DBG1(("mpiInitialize: IOPeventLogSize 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IOP_EVENT_LOG_OPTION));
  saRoot->mainConfigTable.IOPeventLogOption = value;
  SA_DBG1(("mpiInitialize: IOPeventLogOption 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_INTERRUPT));

#ifdef SA_PRINTOUT_IN_WINDBG
#ifndef DBG
  DbgPrint("mpiInitialize: EventLog (%d) dd /p %08X`%08X L %x\n",
          saRoot->mainConfigTable.eventLogOption,
          saRoot->mainConfigTable.upperEventLogAddress,
          saRoot->mainConfigTable.lowerEventLogAddress,
          saRoot->mainConfigTable.eventLogSize/4 );
  DbgPrint("mpiInitialize: IOPLog   (%d) dd /p %08X`%08X L %x\n",
          saRoot->mainConfigTable.IOPeventLogOption,
          saRoot->mainConfigTable.upperIOPeventLogAddress,
          saRoot->mainConfigTable.lowerIOPeventLogAddress,
          saRoot->mainConfigTable.IOPeventLogSize/4 );
#endif /* DBG  */
#endif /* SA_PRINTOUT_IN_WINDBG  */

  saRoot->mainConfigTable.FatalErrorInterrupt = value;
  smTrace(hpDBG_VERY_LOUD,"76",value);
  /* TP:76 FatalErrorInterrupt */

  SA_DBG1(("mpiInitialize: hwConfig->hwOption %X\n", saRoot->hwConfig.hwOption  ));

  SA_DBG1(("mpiInitialize: FatalErrorInterrupt 0x%x\n", value));

  /* read back Register Dump offset and length */
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP0_OFFSET));
  saRoot->mainConfigTable.FatalErrorDumpOffset0 = value;
  SA_DBG1(("mpiInitialize: FatalErrorDumpOffset0 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP0_LENGTH));
  saRoot->mainConfigTable.FatalErrorDumpLength0 = value;
  SA_DBG1(("mpiInitialize: FatalErrorDumpLength0 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP1_OFFSET));
  saRoot->mainConfigTable.FatalErrorDumpOffset1 = value;
  SA_DBG1(("mpiInitialize: FatalErrorDumpOffset1 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP1_LENGTH));
  saRoot->mainConfigTable.FatalErrorDumpLength1 = value;
  SA_DBG1(("mpiInitialize: FatalErrorDumpLength1 0x%x\n", value));

  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_PRECTD_PRESETD));
  saRoot->mainConfigTable.PortRecoveryTimerPortResetTimer = value;

  SA_DBG1(("mpiInitialize: PortRecoveryTimerPortResetTimer 0x%x\n", value));
  value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(MSGUCfgTblDWIdx + MAIN_IRAD_RESERVED));
  saRoot->mainConfigTable.InterruptReassertionDelay = value;

  SA_DBG1(("mpiInitialize: InterruptReassertionDelay 0x%x\n", value));


  if(smIS_SPCV(agRoot))
  {
    bit32 sp1;
    sp1= ossaHwRegRead(agRoot,V_Scratchpad_1_Register );
    if(SCRATCH_PAD1_V_ERROR_STATE(sp1))
    {
      SA_DBG1(("mpiInitialize: SCRATCH_PAD1_V_ERROR_STAT 0x%x\n",sp1 ));
      ret = AGSA_RC_FAILURE;
    }

  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "m3");
  return ret;
}

/*******************************************************************************/
/** \fn mpiWaitForConfigTable(agsaRoot_t *agRoot, spc_configMainDescriptor_t *config)
 *  \brief Reading and Writing the Configuration Table
 *  \param agsaRoot Pointer to a data structure containing LL layer context handles
 *  \param config   Pointer to Configuration Table
 *
 * Return:
 *         AGSA_RC_SUCCESS if read the configuration table from SPC sucessful
 *         AGSA_RC_FAILURE if read the configuration table from SPC failed
 */
/*******************************************************************************/
GLOBAL bit32 mpiWaitForConfigTable(agsaRoot_t                 *agRoot,
                                   spc_configMainDescriptor_t *config)
{
  agsaLLRoot_t  *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32    MSGUCfgTblBase, ret = AGSA_RC_SUCCESS;
  bit32    CfgTblDWIdx;
  bit32    value, value1;
  bit32    max_wait_time;
  bit32    max_wait_count;
  bit32    Signature, ExpSignature;
  bit8     pcibar;

  SA_DBG2(("mpiWaitForConfigTable: Entering\n"));
  SA_ASSERT(NULL != agRoot, "agRoot argument cannot be null");

  smTraceFuncEnter(hpDBG_VERY_LOUD,"m4");


  /* check error state */
  value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1);
  value1 = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_2);

  if( smIS_SPC(agRoot) )
  {
    SA_DBG1(("mpiWaitForConfigTable: Waiting for SPC FW becoming ready.P1 0x%X P2 0x%X\n",value,value1));

  /* check AAP error */
  if (SCRATCH_PAD1_ERR == (value & SCRATCH_PAD_STATE_MASK))
  {
    /* error state */
    SA_DBG1(("mpiWaitForConfigTable: AAP error state and code 0x%x, ScratchPad2=0x%x\n", value, value1));
#if defined(SALLSDK_DEBUG)
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3,MSGU_SCRATCH_PAD_3)));
#endif
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m4");
    return AGSA_RC_FAILURE;
  }

  /* check IOP error */
  if (SCRATCH_PAD2_ERR == (value1 & SCRATCH_PAD_STATE_MASK))
  {
    /* error state */
    SA_DBG1(("mpiWaitForConfigTable: IOP error state and code 0x%x, ScratchPad1=0x%x\n", value1, value));
#if defined(SALLSDK_DEBUG)
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3,MSGU_SCRATCH_PAD_3)));
#endif
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "m4");
    return AGSA_RC_FAILURE;
  }

  /* bit 4-31 of scratch pad1 should be zeros if it is not in error state */
#ifdef DONT_DO /*                                                                        */
  if (value & SCRATCH_PAD1_STATE_MASK)
  {
    /* error case */
    SA_DBG1(("mpiWaitForConfigTable: wrong state failure, scratchPad1 0x%x\n", value));
    SA_DBG1(("mpiWaitForConfigTable: ScratchPad0 AAP error code 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0)));
#if defined(SALLSDK_DEBUG)
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD2 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3,MSGU_SCRATCH_PAD_3)));
#endif
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "m4");
    return AGSA_RC_FAILURE;
  }

  /* bit 4-31 of scratch pad2 should be zeros if it is not in error state */
  if (value1 & SCRATCH_PAD2_STATE_MASK)
  {
    /* error case */
    SA_DBG1(("mpiWaitForConfigTable: wrong state failure, scratchPad2 0x%x\n", value1));
    SA_DBG1(("mpiWaitForConfigTable: ScratchPad3 IOP error code 0x%x\n",siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3,MSGU_SCRATCH_PAD_3) ));
#if defined(SALLSDK_DEBUG)
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD1 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1)));
#endif
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "m4");

    return AGSA_RC_FAILURE;
  }
#endif /* DONT_DO */

  /* checking the fw and IOP in ready state */
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec timeout */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  /* wait until scratch pad 1 and 2 registers in ready state  */
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    value =siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1) & SCRATCH_PAD1_RDY;
    value1 =siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_2)  & SCRATCH_PAD2_RDY;
    if(smIS_SPCV(agRoot))
    {
      SA_DBG1(("mpiWaitForConfigTable:VEN_DEV_SPCV force  SCRATCH_PAD2 RDY 1 %08X 2 %08X\n" ,value,value1));
      value1 =3;
    }

    if ((max_wait_count -= WAIT_INCREMENT) == 0)
    {
      SA_DBG1(("mpiWaitForConfigTable: Timeout!! SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));
      break;
    }
  } while ((value != SCRATCH_PAD1_RDY) || (value1 != SCRATCH_PAD2_RDY));

  if (!max_wait_count)
  {
    SA_DBG1(("mpiWaitForConfigTable: timeout failure\n"));
#if defined(SALLSDK_DEBUG)
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiWaitForConfigTable: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3,MSGU_SCRATCH_PAD_3)));
#endif
    smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "m4");
    return AGSA_RC_FAILURE;
  }

  }else
  {

    if(((value & SCRATCH_PAD1_V_BOOTSTATE_HDA_SEEPROM ) == SCRATCH_PAD1_V_BOOTSTATE_HDA_SEEPROM))
    {
      SA_DBG1(("mpiWaitForConfigTable: HDA mode set in SEEPROM SP1 0x%X\n",value));
    }
    if(((value & SCRATCH_PAD1_V_READY) != SCRATCH_PAD1_V_READY) ||
       (value == 0xffffffff))
    {
      SA_DBG1(("mpiWaitForConfigTable: Waiting for _V_ FW becoming ready.P1 0x%X P2 0x%X\n",value,value1));

      /* checking the fw and IOP in ready state */
      max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec timeout */
      max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
      /* wait until scratch pad 1 and 2 registers in ready state  */
      do
      {
        ossaStallThread(agRoot, WAIT_INCREMENT);
        value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1);
        value1 = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_2);

        if ((max_wait_count -= WAIT_INCREMENT) == 0)
        {
          SA_DBG1(("mpiWaitForConfigTable: Timeout!! SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));
          return AGSA_RC_FAILURE;
        }
      } while (((value & SCRATCH_PAD1_V_READY) != SCRATCH_PAD1_V_READY) ||
               (value == 0xffffffff));
    }
  }


  SA_DBG1(("mpiWaitForConfigTable: FW Ready, SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));

  /* read scratch pad0 to get PCI BAR and offset of configuration table */
  MSGUCfgTblBase = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
  /* get offset */
  CfgTblDWIdx = MSGUCfgTblBase & SCRATCH_PAD0_OFFSET_MASK;
  /* get PCI BAR */
  MSGUCfgTblBase = (MSGUCfgTblBase & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;

  if(smIS_SPC(agRoot))
  {
    if( smIS_spc8081(agRoot))
    {
      if (BAR4 != MSGUCfgTblBase)
      {
        SA_DBG1(("mpiWaitForConfigTable: smIS_spc8081 PCI BAR is not BAR4, bar=0x%x - failure\n", MSGUCfgTblBase));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "m4");
        return AGSA_RC_FAILURE;
      }
    }
    else
    {
      if (BAR5 != MSGUCfgTblBase)
      {
        SA_DBG1(("mpiWaitForConfigTable: PCI BAR is not BAR5, bar=0x%x - failure\n", MSGUCfgTblBase));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "m4");
        return AGSA_RC_FAILURE;
      }
    }
  }

  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, MSGUCfgTblBase);

  /* read signature from the configuration table */
  Signature = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx);

  /* Error return if the signature is not "PMCS" */
  ExpSignature = ('P') | ('M' << SHIFT8) | ('C' << SHIFT16) | ('S' << SHIFT24);

  if (Signature != ExpSignature)
  {
    SA_DBG1(("mpiWaitForConfigTable: Signature value = 0x%x\n", Signature));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "m4");
    return AGSA_RC_FAILURE;
  }

  /* save Signature */
  si_memcpy(&config->Signature, &Signature, sizeof(Signature));

  /* read Interface Revsion from the configuration table */
  config->InterfaceRev = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_INTERFACE_REVISION);

  /* read FW Revsion from the configuration table */
  config->FWRevision = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_FW_REVISION);

  /* read Max Outstanding IO from the configuration table */
  config->MaxOutstandingIO = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_MAX_OUTSTANDING_IO_OFFSET);

  /* read Max SGL and Max Devices from the configuration table */
  config->MDevMaxSGL = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_MAX_SGL_OFFSET);

  /* read Controller Cap Flags from the configuration table */
  config->ContrlCapFlag = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_CNTRL_CAP_OFFSET);

  /* read GST Table Offset from the configuration table */
  config->GSTOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_GST_OFFSET);

  /* read Inbound Queue Offset from the configuration table */
  config->inboundQueueOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_IBQ_OFFSET);

  /* read Outbound Queue Offset from the configuration table */
  config->outboundQueueOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_OBQ_OFFSET);


  if(smIS_SPCV(agRoot))
  {
    ;/* SPCV - reserved field */
  }
  else
  {
    /* read HDA Flags from the configuration table */
    config->HDAModeFlags = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_HDA_FLAGS_OFFSET);
  }

  /* read analog Setting offset from the configuration table */
  config->analogSetupTblOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_ANALOG_SETUP_OFFSET);

  if(smIS_SPCV(agRoot))
  {
    ;/* SPCV - reserved field */
    /* read interrupt vector table offset */
    config->InterruptVecTblOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_INT_VEC_TABLE_OFFSET);
    /* read phy attribute table offset */
    config->phyAttributeTblOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_PHY_ATTRIBUTE_OFFSET);
    SA_DBG1(("mpiWaitForConfigTable: INT Vector Tble Offset = 0x%x\n", config->InterruptVecTblOffset));
    SA_DBG1(("mpiWaitForConfigTable: Phy Attribute Tble Offset = 0x%x\n", config->phyAttributeTblOffset));
  }
  else
  {
    ;/* SPC - Not used */
  }

  /* read Error Dump Offset and Length */
  config->FatalErrorDumpOffset0 = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP0_OFFSET);
  config->FatalErrorDumpLength0 = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP0_LENGTH);
  config->FatalErrorDumpOffset1 = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP1_OFFSET);
  config->FatalErrorDumpLength1 = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_FATAL_ERROR_RDUMP1_LENGTH);

  SA_DBG1(("mpiWaitForConfigTable: Interface Revision value = 0x%08x\n", config->InterfaceRev));
  SA_DBG1(("mpiWaitForConfigTable: FW Revision value = 0x%08x\n", config->FWRevision));
  
  if(smIS_SPC(agRoot))
  {
    SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%08x\n", STSDK_LL_SPC_VERSION));
  }
  if(smIS_SPC6V(agRoot))
  {
    SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%08x\n",STSDK_LL_VERSION ));
  }
  if(smIS_SPC12V(agRoot))
  {
    SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%08x\n",STSDK_LL_12G_VERSION ));
  }

  SA_DBG1(("mpiWaitForConfigTable: MaxOutstandingIO value = 0x%08x\n", config->MaxOutstandingIO));
  SA_DBG1(("mpiWaitForConfigTable: MDevMaxSGL value = 0x%08x\n", config->MDevMaxSGL));
  SA_DBG1(("mpiWaitForConfigTable: ContrlCapFlag value = 0x%08x\n", config->ContrlCapFlag));
  SA_DBG1(("mpiWaitForConfigTable: GSTOffset value = 0x%08x\n", config->GSTOffset));
  SA_DBG1(("mpiWaitForConfigTable: inboundQueueOffset value = 0x%08x\n", config->inboundQueueOffset));
  SA_DBG1(("mpiWaitForConfigTable: outboundQueueOffset value = 0x%08x\n", config->outboundQueueOffset));
  SA_DBG1(("mpiWaitForConfigTable: FatalErrorDumpOffset0 value = 0x%08x\n", config->FatalErrorDumpOffset0));
  SA_DBG1(("mpiWaitForConfigTable: FatalErrorDumpLength0 value = 0x%08x\n", config->FatalErrorDumpLength0));
  SA_DBG1(("mpiWaitForConfigTable: FatalErrorDumpOffset1 value = 0x%08x\n", config->FatalErrorDumpOffset1));
  SA_DBG1(("mpiWaitForConfigTable: FatalErrorDumpLength1 value = 0x%08x\n", config->FatalErrorDumpLength1));


  SA_DBG1(("mpiWaitForConfigTable: HDAModeFlags value = 0x%08x\n", config->HDAModeFlags));
  SA_DBG1(("mpiWaitForConfigTable: analogSetupTblOffset value = 0x%08x\n", config->analogSetupTblOffset));

  /* check interface version */

  if(smIS_SPC6V(agRoot))
  {
    if (config->InterfaceRev != STSDK_LL_INTERFACE_VERSION)
    {
      SA_DBG1(("mpiWaitForConfigTable: V sTSDK interface ver. 0x%x does not match InterfaceRev 0x%x warning!\n", STSDK_LL_INTERFACE_VERSION, config->InterfaceRev));
      ret = AGSA_RC_VERSION_UNTESTED;
      if ((config->InterfaceRev & STSDK_LL_INTERFACE_VERSION_IGNORE_MASK) != (STSDK_LL_INTERFACE_VERSION & STSDK_LL_INTERFACE_VERSION_IGNORE_MASK))
      {
        SA_DBG1(("mpiWaitForConfigTable: V sTSDK interface ver. 0x%x incompatible with InterfaceRev 0x%x warning!\n", STSDK_LL_INTERFACE_VERSION, config->InterfaceRev));
        ret = AGSA_RC_VERSION_INCOMPATIBLE;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "m4");
        return ret;
      }
    }
  }
  else if(smIS_SPC12V(agRoot))
  {
    if (config->InterfaceRev != STSDK_LL_12G_INTERFACE_VERSION)
    {
      SA_DBG1(("mpiWaitForConfigTable: 12g V sTSDK interface ver. 0x%x does not match InterfaceRev 0x%x warning!\n", STSDK_LL_12G_INTERFACE_VERSION, config->InterfaceRev));
      ret = AGSA_RC_VERSION_UNTESTED;
      if ((config->InterfaceRev & STSDK_LL_INTERFACE_VERSION_IGNORE_MASK) != (STSDK_LL_12G_INTERFACE_VERSION & STSDK_LL_INTERFACE_VERSION_IGNORE_MASK))
      {
        SA_DBG1(("mpiWaitForConfigTable: V sTSDK interface ver. 0x%x incompatible with InterfaceRev 0x%x warning!\n", STSDK_LL_12G_INTERFACE_VERSION, config->InterfaceRev));
        ret = AGSA_RC_VERSION_INCOMPATIBLE;
        ret = AGSA_RC_VERSION_UNTESTED;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "m4");
        return ret;
      }
    }
  }
  else
  {
    if (config->InterfaceRev != STSDK_LL_OLD_INTERFACE_VERSION)
    {
      SA_DBG1(("mpiWaitForConfigTable: SPC sTSDK interface ver. 0x%08x not compatible with InterfaceRev 0x%x warning!\n", STSDK_LL_INTERFACE_VERSION, config->InterfaceRev));
      ret = AGSA_RC_VERSION_INCOMPATIBLE;
      smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "m4");
      return ret;
    }

  }


  /* Check FW versions */
  if(smIS_SPC6V(agRoot))
  {
    SA_DBG1(("mpiWaitForConfigTable:6 sTSDK ver. sa.h 0x%08x config 0x%08x\n", STSDK_LL_VERSION, config->FWRevision));
    /* check FW and LL sTSDK version */
    if (config->FWRevision !=  MATCHING_V_FW_VERSION )
    {
      if (config->FWRevision >  MATCHING_V_FW_VERSION)
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x hadn't tested with FW ver. 0x%08x warning!\n", STSDK_LL_VERSION, config->FWRevision));
        ret = AGSA_RC_VERSION_UNTESTED;
      }

      else if (config->FWRevision <  MIN_FW_SPCVE_VERSION_SUPPORTED)
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x not compatible with FW ver. 0x%08x warning!\n", STSDK_LL_VERSION, config->FWRevision));
        ret = AGSA_RC_VERSION_INCOMPATIBLE;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'l', "m4");
        return ret;
      }
      else
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x mismatch with FW ver. 0x%08x warning!\n",STSDK_LL_VERSION , config->FWRevision));
        ret = AGSA_RC_VERSION_UNTESTED;
      }
    }
  }else if(smIS_SPC12V(agRoot))
  {
    SA_DBG1(("mpiWaitForConfigTable:12 sTSDK ver. sa.h 0x%08x config 0x%08x\n", STSDK_LL_12G_VERSION, config->FWRevision));
    /* check FW and LL sTSDK version */
    if (config->FWRevision !=  MATCHING_12G_V_FW_VERSION )
    {
      if (config->FWRevision >  MATCHING_12G_V_FW_VERSION)
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x hadn't tested with FW ver. 0x%08x warning!\n", STSDK_LL_12G_VERSION, config->FWRevision));
        ret = AGSA_RC_VERSION_UNTESTED;
      }

      else if (config->FWRevision <  MIN_FW_12G_SPCVE_VERSION_SUPPORTED)
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x not compatible with FW ver. 0x%08x warning!\n", STSDK_LL_12G_VERSION, config->FWRevision));
        ret = AGSA_RC_VERSION_INCOMPATIBLE;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'm', "m4");
        return ret;
      }
      else
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x mismatch with FW ver. 0x%08x warning!\n",STSDK_LL_12G_VERSION , config->FWRevision));
        ret = AGSA_RC_VERSION_UNTESTED;
      }
    }
  }
  else
  {
    if (config->FWRevision != MATCHING_SPC_FW_VERSION )
    {
      if (config->FWRevision >  MATCHING_SPC_FW_VERSION)
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x hadn't tested with FW ver. 0x%08x warning!\n", STSDK_LL_SPC_VERSION, config->FWRevision));
        ret = AGSA_RC_VERSION_UNTESTED;
      }
      else if (config->FWRevision <  MIN_FW_SPC_VERSION_SUPPORTED)
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x not compatible with FW ver. 0x%08x warning!\n", STSDK_LL_SPC_VERSION, config->FWRevision));
        ret = AGSA_RC_VERSION_INCOMPATIBLE;
        smTraceFuncExit(hpDBG_VERY_LOUD, 'n', "m4");
        return ret;
      }
      else
      {
        SA_DBG1(("mpiWaitForConfigTable: sTSDK ver. 0x%x mismatch with FW ver. 0x%08x warning!\n",STSDK_LL_SPC_VERSION , config->FWRevision));
        ret = AGSA_RC_VERSION_UNTESTED;
      }
    }
  }
  SA_DBG1(("mpiWaitForConfigTable: ILA version 0x%08X\n",  ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_ILAT_ILAV_ILASMRN_ILAMRN_ILAMJN) ));


  if(smIS_SPC12V(agRoot))
  {
    if (config->InterfaceRev > 0x301 )
    {
      SA_DBG1(("mpiWaitForConfigTable: MAIN_INACTIVE_ILA_REVSION 0x%08X\n",  ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_INACTIVE_ILA_REVSION) ));
      SA_DBG1(("mpiWaitForConfigTable: MAIN_SEEPROM_REVSION 0x%08X\n",  ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_SEEPROM_REVSION) ));
    }
  }

  if(smIS_SPC12V(agRoot))
  {
    if (config->InterfaceRev > 0x301 )
    {
      SA_DBG1(("mpiWaitForConfigTable: MAIN_AWT_MIDRANGE 0x%08X\n",  ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_AWT_MIDRANGE) ));
    }
  }


  if(smIS_SFC(agRoot))
  {
    /* always success for SFC*/
    ret = AGSA_RC_SUCCESS;
  }

  if (agNULL != saRoot)
  {
    /* save the information */
    saRoot->ControllerInfo.signature = Signature;
    saRoot->ControllerInfo.fwInterfaceRev = config->InterfaceRev;

    if(smIS_SPCV(agRoot))
    {
      saRoot->ControllerInfo.hwRevision = (ossaHwRegReadConfig32(agRoot,8) & 0xFF);
      SA_DBG1(("mpiWaitForConfigTable: hwRevision 0x%x\n",saRoot->ControllerInfo.hwRevision  ));
    }
    else
    {
      saRoot->ControllerInfo.hwRevision = SPC_READ_DEV_REV;
    }

    saRoot->ControllerInfo.fwRevision = config->FWRevision;
    saRoot->ControllerInfo.ilaRevision  = config->ilaRevision;
    saRoot->ControllerInfo.maxPendingIO = config->MaxOutstandingIO;
    saRoot->ControllerInfo.maxSgElements = config->MDevMaxSGL & 0xFFFF;
    saRoot->ControllerInfo.maxDevices = (config->MDevMaxSGL & MAX_DEV_BITS) >> SHIFT16;
    saRoot->ControllerInfo.queueSupport = config->ContrlCapFlag & Q_SUPPORT_BITS;
    saRoot->ControllerInfo.phyCount = (bit8)((config->ContrlCapFlag & PHY_COUNT_BITS) >> SHIFT19);
    saRoot->ControllerInfo.sasSpecsSupport = (config->ContrlCapFlag & SAS_SPEC_BITS) >> SHIFT25;
    SA_DBG1(("mpiWaitForConfigTable: MaxOutstandingIO 0x%x swConfig->maxActiveIOs 0x%x\n", config->MaxOutstandingIO,saRoot->swConfig.maxActiveIOs ));

    if(smIS_SPCV(agRoot))
    {
      ;/* SPCV - reserved field */
    }
    else
    {
      saRoot->ControllerInfo.controllerSetting = (bit8)config->HDAModeFlags;
    }

    saRoot->ControllerInfo.sdkInterfaceRev = STSDK_LL_INTERFACE_VERSION;
    saRoot->ControllerInfo.sdkRevision = STSDK_LL_VERSION;
    saRoot->mainConfigTable.regDumpPCIBAR = pcibar;
    saRoot->mainConfigTable.FatalErrorDumpOffset0 = config->FatalErrorDumpOffset0;
    saRoot->mainConfigTable.FatalErrorDumpLength0 = config->FatalErrorDumpLength0;
    saRoot->mainConfigTable.FatalErrorDumpOffset1 = config->FatalErrorDumpOffset1;
    saRoot->mainConfigTable.FatalErrorDumpLength1 = config->FatalErrorDumpLength1;

    if(smIS_SPCV(agRoot))
    {
      ;/* SPCV - reserved field */
    }
    else
    {
      saRoot->mainConfigTable.HDAModeFlags = config->HDAModeFlags;
    }

    saRoot->mainConfigTable.analogSetupTblOffset = config->analogSetupTblOffset;

    if(smIS_SPCV(agRoot))
    {
      saRoot->mainConfigTable.InterruptVecTblOffset = config->InterruptVecTblOffset;
      saRoot->mainConfigTable.phyAttributeTblOffset = config->phyAttributeTblOffset;
      saRoot->mainConfigTable.PortRecoveryTimerPortResetTimer = config->portRecoveryResetTimer;
    }

    SA_DBG1(("mpiWaitForConfigTable: Signature = 0x%x\n", Signature));
    SA_DBG1(("mpiWaitForConfigTable: hwRevision = 0x%x\n", saRoot->ControllerInfo.hwRevision));
    SA_DBG1(("mpiWaitForConfigTable: FW Revision = 0x%x\n", config->FWRevision));
    SA_DBG1(("mpiWaitForConfigTable: Max Sgl = 0x%x\n", saRoot->ControllerInfo.maxSgElements));
    SA_DBG1(("mpiWaitForConfigTable: Max Device = 0x%x\n", saRoot->ControllerInfo.maxDevices));
    SA_DBG1(("mpiWaitForConfigTable: Queue Support = 0x%x\n", saRoot->ControllerInfo.queueSupport));
    SA_DBG1(("mpiWaitForConfigTable: Phy Count = 0x%x\n", saRoot->ControllerInfo.phyCount));
    SA_DBG1(("mpiWaitForConfigTable: sas Specs Support = 0x%x\n", saRoot->ControllerInfo.sasSpecsSupport));

  }


  if(ret != AGSA_RC_SUCCESS )
  {
    SA_DBG1(("mpiWaitForConfigTable: return 0x%x not AGSA_RC_SUCCESS warning!\n", ret));
  }


  smTraceFuncExit(hpDBG_VERY_LOUD, 'o', "m4");
  return ret;
}

/*******************************************************************************/
/** \fn mpiUnInitConfigTable(agsaRoot_t *agRoot, spc_configMainDescriptor_t *config)
 *  \brief UnInitialization Configuration Table
 *  \param agsaRoot Pointer to a data structure containing LL layer context handles
 *
 * Return:
 *         AGSA_RC_SUCCESS if Un-initialize the configuration table sucessful
 *         AGSA_RC_FAILURE if Un-initialize the configuration table failed
 */
/*******************************************************************************/
GLOBAL bit32 mpiUnInitConfigTable(agsaRoot_t *agRoot)
{
  bit32    MSGUCfgTblBase;
  bit32    CfgTblDWIdx, GSTOffset, GSTLenMPIS;
  bit32    value, togglevalue;
  bit32    max_wait_time;
  bit32    max_wait_count;
  bit8     pcibar;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"m7");
  SA_DBG1(("mpiUnInitConfigTable: agRoot %p\n",agRoot));
  SA_ASSERT(NULL != agRoot, "agRoot argument cannot be null");

  togglevalue = 0;

  /* read scratch pad0 to get PCI BAR and offset of configuration table */
  MSGUCfgTblBase =siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  if(MSGUCfgTblBase == 0xFFFFFFFF)
  {
    SA_DBG1(("mpiUnInitConfigTable: MSGUCfgTblBase = 0x%x AGSA_RC_FAILURE\n",MSGUCfgTblBase));
    return AGSA_RC_FAILURE;
  }

  /* get offset */
  CfgTblDWIdx = MSGUCfgTblBase & SCRATCH_PAD0_OFFSET_MASK;
  /* get PCI BAR */
  MSGUCfgTblBase = (MSGUCfgTblBase & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;

  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, MSGUCfgTblBase);

  /* Write bit 1 to Inbound DoorBell Register */
  siHalRegWriteExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET, SPC_MSGU_CFG_TABLE_RESET);

  /* wait until Inbound DoorBell Clear Register toggled */
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    value = MSGU_READ_IDR;
    value &= SPC_MSGU_CFG_TABLE_RESET;
  } while ((value != togglevalue) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("mpiUnInitConfigTable: TIMEOUT:IBDB value/toggle = 0x%x 0x%x\n", value, togglevalue));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m7");

    if(smIS_SPC(agRoot) )
    {
      return AGSA_RC_FAILURE;
    }

  }

  /* check the MPI-State for termination in progress */
  /* wait until Inbound DoorBell Clear Register toggled */
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  GSTOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_GST_OFFSET);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);

    if(GSTOffset == 0xFFFFFFFF)
    {
      SA_DBG1(("mpiUnInitConfigTable:AGSA_RC_FAILURE GSTOffset = 0x%x\n",GSTOffset));
      return AGSA_RC_FAILURE;
    }

    GSTLenMPIS = ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + (bit32)(GSTOffset + GST_GSTLEN_MPIS_OFFSET));
    if (GST_MPI_STATE_UNINIT == (GSTLenMPIS & GST_MPI_STATE_MASK))
    {
      break;
    }
  } while (max_wait_count -= WAIT_INCREMENT);

  if (!max_wait_count)
  {
    SA_DBG1(("mpiUnInitConfigTable: TIMEOUT, MPI State = 0x%x\n", GSTLenMPIS & GST_MPI_STATE_MASK));
#if defined(SALLSDK_DEBUG)

    SA_DBG1(("mpiUnInitConfigTable: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiUnInitConfigTable: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1)));
    SA_DBG1(("mpiUnInitConfigTable: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_2)));
    SA_DBG1(("mpiUnInitConfigTable: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_3)));
#endif

    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "m7");
    return AGSA_RC_FAILURE;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "m7");
  return AGSA_RC_SUCCESS;
}

/*******************************************************************************/
/** \fn void mpiUpdateIBQueueCfgTable(agsaRoot_t *agRoot, spc_inboundQueueDescriptor_t *outQueueCfg,
 *                               bit32 QueueTableOffset,bit8 pcibar)
 *  \brief Writing to the inbound queue of the Configuration Table
 *  \param agsaRoot Pointer to a data structure containing both application and LL layer context handles
 *  \param outQueueCfg      Pointer to inbuond configuration area
 *  \param QueueTableOffset Queue configuration table offset
 *  \param pcibar           PCI BAR
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiUpdateIBQueueCfgTable(agsaRoot_t             *agRoot,
                              spc_inboundQueueDescriptor_t  *inQueueCfg,
                              bit32                         QueueTableOffset,
                              bit8                          pcibar)
{
  smTraceFuncEnter(hpDBG_VERY_LOUD,"m5");

  smTrace(hpDBG_VERY_LOUD,"Ba",QueueTableOffset);
  /* TP:Ba QueueTableOffset */
  smTrace(hpDBG_VERY_LOUD,"Bb",pcibar);
  /* TP:Bb pcibar */

  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + IB_PROPERITY_OFFSET), inQueueCfg->elementPriSizeCount);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + IB_BASE_ADDR_HI_OFFSET), inQueueCfg->upperBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + IB_BASE_ADDR_LO_OFFSET), inQueueCfg->lowerBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + IB_CI_BASE_ADDR_HI_OFFSET), inQueueCfg->ciUpperBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + IB_CI_BASE_ADDR_LO_OFFSET), inQueueCfg->ciLowerBaseAddress);


  SA_DBG3(("mpiUpdateIBQueueCfgTable: Offset 0x%08x elementPriSizeCount 0x%x\n",(bit32)(QueueTableOffset + IB_PROPERITY_OFFSET), inQueueCfg->elementPriSizeCount));
  SA_DBG3(("mpiUpdateIBQueueCfgTable: Offset 0x%08x upperBaseAddress    0x%x\n",(bit32)(QueueTableOffset + IB_BASE_ADDR_HI_OFFSET), inQueueCfg->upperBaseAddress));
  SA_DBG3(("mpiUpdateIBQueueCfgTable: Offset 0x%08x lowerBaseAddress    0x%x\n",(bit32)(QueueTableOffset + IB_BASE_ADDR_LO_OFFSET), inQueueCfg->lowerBaseAddress));
  SA_DBG3(("mpiUpdateIBQueueCfgTable: Offset 0x%08x ciUpperBaseAddress  0x%x\n",(bit32)(QueueTableOffset + IB_CI_BASE_ADDR_HI_OFFSET), inQueueCfg->ciUpperBaseAddress));
  SA_DBG3(("mpiUpdateIBQueueCfgTable: Offset 0x%08x ciLowerBaseAddress  0x%x\n",(bit32)(QueueTableOffset + IB_CI_BASE_ADDR_LO_OFFSET), inQueueCfg->ciLowerBaseAddress));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m5");
}

/*******************************************************************************/
/** \fn void mpiUpdateOBQueueCfgTable(agsaRoot_t *agRoot, spc_outboundQueueDescriptor_t *outQueueCfg,
 *                               bit32 QueueTableOffset,bit8 pcibar)
 *  \brief Writing to the inbound queue of the Configuration Table
 *  \param agsaRoot         Pointer to a data structure containing both application
 *                          and LL layer context handles
 *  \param outQueueCfg      Pointer to outbuond configuration area
 *  \param QueueTableOffset Queue configuration table offset
 *  \param pcibar           PCI BAR
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiUpdateOBQueueCfgTable(agsaRoot_t             *agRoot,
                              spc_outboundQueueDescriptor_t *outQueueCfg,
                              bit32                         QueueTableOffset,
                              bit8                          pcibar)
{

  smTraceFuncEnter(hpDBG_VERY_LOUD,"m8");

  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + OB_PROPERITY_OFFSET), outQueueCfg->elementSizeCount);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + OB_BASE_ADDR_HI_OFFSET), outQueueCfg->upperBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + OB_BASE_ADDR_LO_OFFSET), outQueueCfg->lowerBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + OB_PI_BASE_ADDR_HI_OFFSET), outQueueCfg->piUpperBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + OB_PI_BASE_ADDR_LO_OFFSET), outQueueCfg->piLowerBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(QueueTableOffset + OB_INTERRUPT_COALES_OFFSET), outQueueCfg->interruptVecCntDelay);

  SA_DBG3(("mpiUpdateOBQueueCfgTable: Offset 0x%08x elementSizeCount     0x%x\n",(bit32)(QueueTableOffset + OB_PROPERITY_OFFSET), outQueueCfg->elementSizeCount));
  SA_DBG3(("mpiUpdateOBQueueCfgTable: Offset 0x%08x upperBaseAddress     0x%x\n",(bit32)(QueueTableOffset + OB_BASE_ADDR_HI_OFFSET), outQueueCfg->upperBaseAddress));
  SA_DBG3(("mpiUpdateOBQueueCfgTable: Offset 0x%08x lowerBaseAddress     0x%x\n",(bit32)(QueueTableOffset + OB_BASE_ADDR_LO_OFFSET), outQueueCfg->lowerBaseAddress));
  SA_DBG3(("mpiUpdateOBQueueCfgTable: Offset 0x%08x piUpperBaseAddress   0x%x\n",(bit32)(QueueTableOffset + OB_PI_BASE_ADDR_HI_OFFSET), outQueueCfg->piUpperBaseAddress));
  SA_DBG3(("mpiUpdateOBQueueCfgTable: Offset 0x%08x piLowerBaseAddress   0x%x\n",(bit32)(QueueTableOffset + OB_PI_BASE_ADDR_LO_OFFSET), outQueueCfg->piLowerBaseAddress));
  SA_DBG3(("mpiUpdateOBQueueCfgTable: Offset 0x%08x interruptVecCntDelay 0x%x\n",(bit32)(QueueTableOffset + OB_INTERRUPT_COALES_OFFSET), outQueueCfg->interruptVecCntDelay));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m8");
}



/*******************************************************************************/
/** \fn void mpiUpdateOBQueueCfgTable(agsaRoot_t *agRoot, spc_outboundQueueDescriptor_t *outQueueCfg,
 *                               bit32 QueueTableOffset,bit8 pcibar)
 *  \brief Writing to the inbound queue of the Configuration Table
 *  \param agsaRoot         Pointer to a data structure containing both application
 *                          and LL layer context handles
 *  \param outQueueCfg      Pointer to outbuond configuration area
 *  \param QueueTableOffset Queue configuration table offset
 *  \param pcibar           PCI BAR
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiUpdateFatalErrorTable(agsaRoot_t             *agRoot,
                              bit32                         FerrTableOffset,
                              bit32                         lowerBaseAddress,
                              bit32                         upperBaseAddress,
                              bit32                         length,
                              bit8                          pcibar)
{

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2U");

  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(FerrTableOffset + MPI_FATAL_EDUMP_TABLE_LO_OFFSET), lowerBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(FerrTableOffset + MPI_FATAL_EDUMP_TABLE_HI_OFFSET), upperBaseAddress);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(FerrTableOffset + MPI_FATAL_EDUMP_TABLE_LENGTH), length);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(FerrTableOffset + MPI_FATAL_EDUMP_TABLE_HANDSHAKE), 0);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(FerrTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS), 0);


  SA_DBG3(("mpiUpdateFatalErrorTable: Offset 0x%08x  MPI_FATAL_EDUMP_TABLE_LO_OFFSET 0x%x\n",FerrTableOffset + MPI_FATAL_EDUMP_TABLE_LO_OFFSET, lowerBaseAddress));
  SA_DBG3(("mpiUpdateFatalErrorTable: Offset 0x%08x  MPI_FATAL_EDUMP_TABLE_HI_OFFSET 0x%x\n",FerrTableOffset + MPI_FATAL_EDUMP_TABLE_HI_OFFSET,upperBaseAddress ));
  SA_DBG3(("mpiUpdateFatalErrorTable: Offset 0x%08x  MPI_FATAL_EDUMP_TABLE_LENGTH    0x%x\n",FerrTableOffset + MPI_FATAL_EDUMP_TABLE_LENGTH, length));
  SA_DBG3(("mpiUpdateFatalErrorTable: Offset 0x%08x  MPI_FATAL_EDUMP_TABLE_HANDSHAKE 0x%x\n",FerrTableOffset + MPI_FATAL_EDUMP_TABLE_HANDSHAKE,0 ));
  SA_DBG3(("mpiUpdateFatalErrorTable: Offset 0x%08x  MPI_FATAL_EDUMP_TABLE_STATUS    0x%x\n",FerrTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS,0 ));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2U");
}


/*******************************************************************************/
/** \fn bit32 mpiGetPCIBarIndex(agsaRoot_t *agRoot, pciBar)
 *  \brief Get PCI BAR Index from PCI BAR
 *  \param agsaRoot Pointer to a data structure containing both application and LL layer context handles
 *  \param pciBar - PCI BAR
 *
 * Return:
 *         PCI BAR Index
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetPCIBarIndex(agsaRoot_t *agRoot, bit32 pciBar)
{
  switch(pciBar)
  {
  case BAR0:
  case BAR1:
      pciBar = PCIBAR0;
      break;
  case BAR2:
  case BAR3:
      pciBar = PCIBAR1;
      break;
  case BAR4:
      pciBar = PCIBAR2;
      break;
  case BAR5:
      pciBar = PCIBAR3;
      break;
  default:
      pciBar = PCIBAR0;
      break;
  }

  return pciBar;
}

/*******************************************************************************/
/** \fn void mpiReadGSTTable(agsaRoot_t *agRoot, spc_GSTableDescriptor_t *mpiGSTable)
 *  \brief Reading the General Status Table
 *
 *  \param agsaRoot         Handles for this instance of SAS/SATA LLL
 *  \param mpiGSTable       Pointer of General Status Table
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiReadGSTable(agsaRoot_t             *agRoot,
                         spc_GSTableDescriptor_t  *mpiGSTable)
{
  bit32 CFGTableOffset, TableOffset;
  bit32 GSTableOffset;
  bit8  i, pcibar;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"m9");

  /* get offset of the configuration table */
  TableOffset = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  if(0xFFFFFFFF ==  TableOffset)
  {
    SA_ASSERT(0xFFFFFFFF ==  TableOffset, "Chip PCI dead");

    SA_DBG1(("mpiReadGSTable: Chip PCI dead  TableOffset 0x%x\n", TableOffset));
    return;
  }

//  SA_DBG1(("mpiReadGSTable: TableOffset 0x%x\n", TableOffset));
  CFGTableOffset = TableOffset & SCRATCH_PAD0_OFFSET_MASK;

  /* get PCI BAR */
  TableOffset = (TableOffset & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, TableOffset);

  /* read GST Table Offset from the configuration table */
  GSTableOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CFGTableOffset + MAIN_GST_OFFSET);
//  SA_DBG1(("mpiReadGSTable: GSTableOffset 0x%x\n",GSTableOffset ));

  GSTableOffset = CFGTableOffset + GSTableOffset;

  mpiGSTable->GSTLenMPIS = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_GSTLEN_MPIS_OFFSET));
  mpiGSTable->IQFreezeState0 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_IQ_FREEZE_STATE0_OFFSET));
  mpiGSTable->IQFreezeState1 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_IQ_FREEZE_STATE1_OFFSET));
  mpiGSTable->MsguTcnt       = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_MSGUTCNT_OFFSET));
  mpiGSTable->IopTcnt        = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_IOPTCNT_OFFSET));
  mpiGSTable->Iop1Tcnt       = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_IOP1TCNT_OFFSET));

  SA_DBG4(("mpiReadGSTable: GSTLenMPIS     0x%x\n", mpiGSTable->GSTLenMPIS));
  SA_DBG4(("mpiReadGSTable: GSTLen         0x%x\n", (mpiGSTable->GSTLenMPIS & 0xfff8) >> SHIFT3));
  SA_DBG4(("mpiReadGSTable: IQFreezeState0 0x%x\n", mpiGSTable->IQFreezeState0));
  SA_DBG4(("mpiReadGSTable: IQFreezeState1 0x%x\n", mpiGSTable->IQFreezeState1));
  SA_DBG4(("mpiReadGSTable: MsguTcnt       0x%x\n", mpiGSTable->MsguTcnt));
  SA_DBG4(("mpiReadGSTable: IopTcnt        0x%x\n", mpiGSTable->IopTcnt));
  SA_DBG4(("mpiReadGSTable: Iop1Tcnt       0x%x\n", mpiGSTable->Iop1Tcnt));


  if(smIS_SPCV(agRoot))
  {
    /***** read Phy State from SAS Phy Attribute Table */
    TableOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CFGTableOffset + MAIN_PHY_ATTRIBUTE_OFFSET);
    TableOffset &= 0x00FFFFFF;
    TableOffset = TableOffset + CFGTableOffset;
    for (i = 0; i < 8; i++)
    {
      mpiGSTable->PhyState[i] = ossaHwRegReadExt(agRoot, pcibar, (bit32)(TableOffset + i * sizeof(phyAttrb_t)));
      SA_DBG4(("mpiReadGSTable: PhyState[0x%x] 0x%x\n", i, mpiGSTable->PhyState[i]));
    }
  }
  else
  {
    for (i = 0; i < 8; i++)
    {
      mpiGSTable->PhyState[i] = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_PHYSTATE_OFFSET + i * 4));
      SA_DBG4(("mpiReadGSTable: PhyState[0x%x] 0x%x\n", i, mpiGSTable->PhyState[i]));
    }
  }

  mpiGSTable->GPIOpins = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_GPIO_PINS_OFFSET));
  SA_DBG4(("mpiReadGSTable: GPIOpins 0x%x\n", mpiGSTable->GPIOpins));

  for (i = 0; i < 8; i++)
  {
    mpiGSTable->recoverErrInfo[i] = ossaHwRegReadExt(agRoot, pcibar, (bit32)(GSTableOffset + GST_RERRINFO_OFFSET));
    SA_DBG4(("mpiReadGSTable: recoverErrInfo[0x%x] 0x%x\n", i, mpiGSTable->recoverErrInfo[i]));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m9");

}

/*******************************************************************************/
/** \fn void siInitResources(agsaRoot_t *agRoot)
 *  Initialization of LL resources
 *
 *  \param agsaRoot         Handles for this instance of SAS/SATA LLL
 *  \param memoryAllocated  Point to the data structure that holds the different
 *                          chunks of memory that are required
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void siInitResources(agsaRoot_t              *agRoot,
                            agsaMemoryRequirement_t *memoryAllocated,
                            agsaHwConfig_t          *hwConfig,
                            agsaSwConfig_t          *swConfig,
                            bit32                   usecsPerTick)
{
  agsaLLRoot_t          *saRoot;
  agsaDeviceDesc_t      *pDeviceDesc;
  agsaIORequestDesc_t   *pRequestDesc;
  agsaTimerDesc_t       *pTimerDesc;
  agsaPort_t            *pPort;
  agsaPortMap_t         *pPortMap;
  agsaDeviceMap_t       *pDeviceMap;
  agsaIOMap_t           *pIOMap;
  bit32                 maxNumIODevices;
  bit32                 i, j;
  mpiICQueue_t          *circularIQ;
  mpiOCQueue_t          *circularOQ;

  if (agNULL == agRoot)
  {
    return;
  }

  /* Get the saRoot memory address */
  saRoot = (agsaLLRoot_t *) (memoryAllocated->agMemory[LLROOT_MEM_INDEX].virtPtr);
  agRoot->sdkData = (void *) saRoot;

  /* Setup Device link */
  /* Save the information of allocated device Link memory */
  saRoot->deviceLinkMem = memoryAllocated->agMemory[DEVICELINK_MEM_INDEX];
  si_memset(saRoot->deviceLinkMem.virtPtr, 0, saRoot->deviceLinkMem.totalLength);
  SA_DBG2(("siInitResources: [%d] saRoot->deviceLinkMem VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n" ,
    DEVICELINK_MEM_INDEX,
    saRoot->deviceLinkMem.virtPtr,
    saRoot->deviceLinkMem.phyAddrLower,
    saRoot->deviceLinkMem.numElements,
    saRoot->deviceLinkMem.totalLength,
    saRoot->deviceLinkMem.type));

  maxNumIODevices = swConfig->numDevHandles;
  SA_DBG2(("siInitResources:  maxNumIODevices=%d, swConfig->numDevHandles=%d \n",
    maxNumIODevices,
    swConfig->numDevHandles));

  /* Setup free IO Devices link list */
  saLlistInitialize(&(saRoot->freeDevicesList));
  for ( i = 0; i < (bit32) maxNumIODevices; i ++ )
  {
    /* get the pointer to the device descriptor */
    pDeviceDesc = (agsaDeviceDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->deviceLinkMem), i);
    /* Initialize device descriptor */
    saLlinkInitialize(&(pDeviceDesc->linkNode));

    pDeviceDesc->initiatorDevHandle.osData    = agNULL;
    pDeviceDesc->initiatorDevHandle.sdkData   = agNULL;
    pDeviceDesc->targetDevHandle.osData       = agNULL;
    pDeviceDesc->targetDevHandle.sdkData      = agNULL;
    pDeviceDesc->deviceType                   = SAS_SATA_UNKNOWN_DEVICE;
    pDeviceDesc->pPort                        = agNULL;
    pDeviceDesc->DeviceMapIndex               = 0;

    saLlistInitialize(&(pDeviceDesc->pendingIORequests));

    /* Add the device descriptor to the free IO device link list */
    saLlistAdd(&(saRoot->freeDevicesList), &(pDeviceDesc->linkNode));
  }

  /* Setup IO Request link */
  /* Save the information of allocated IO Request Link memory */
  saRoot->IORequestMem = memoryAllocated->agMemory[IOREQLINK_MEM_INDEX];
  si_memset(saRoot->IORequestMem.virtPtr, 0, saRoot->IORequestMem.totalLength);

  SA_DBG2(("siInitResources: [%d] saRoot->IORequestMem  VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n",
    IOREQLINK_MEM_INDEX,
    saRoot->IORequestMem.virtPtr,
    saRoot->IORequestMem.phyAddrLower,
    saRoot->IORequestMem.numElements,
    saRoot->IORequestMem.totalLength,
    saRoot->IORequestMem.type));

  /* Setup free IO  Request link list */
  saLlistIOInitialize(&(saRoot->freeIORequests));
  saLlistIOInitialize(&(saRoot->freeReservedRequests));
  for ( i = 0; i < swConfig->maxActiveIOs; i ++ )
  {
    /* get the pointer to the request descriptor */
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), i);
    /* Initialize request descriptor */
    saLlinkIOInitialize(&(pRequestDesc->linkNode));

    pRequestDesc->valid             = agFALSE;
    pRequestDesc->requestType       = AGSA_REQ_TYPE_UNKNOWN;
    pRequestDesc->pIORequestContext = agNULL;
    pRequestDesc->HTag              = i;
    pRequestDesc->pDevice           = agNULL;
    pRequestDesc->pPort             = agNULL;

    /* Add the request descriptor to the free IO Request link list */
    /* Add the request descriptor to the free Reserved Request link list */
  /* SMP request must get service so reserve one request when first SMP completes */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequestDesc->linkNode));
    }
    else
    {
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequestDesc->linkNode));
    }
  }


  /* Setup timer link */
  /* Save the information of allocated timer Link memory */
  saRoot->timerLinkMem = memoryAllocated->agMemory[TIMERLINK_MEM_INDEX];
  si_memset(saRoot->timerLinkMem.virtPtr, 0, saRoot->timerLinkMem.totalLength);
  SA_DBG2(("siInitResources: [%d] saRoot->timerLinkMem  VirtPtr=%p PhysicalLo=%x Count=%x Total=%x type %x\n",
    TIMERLINK_MEM_INDEX,
    saRoot->timerLinkMem.virtPtr,
    saRoot->timerLinkMem.phyAddrLower,
    saRoot->timerLinkMem.numElements,
    saRoot->timerLinkMem.totalLength,
    saRoot->timerLinkMem.type));

  /* Setup free timer link list */
  saLlistInitialize(&(saRoot->freeTimers));
  for ( i = 0; i < NUM_TIMERS; i ++ )
  {
    /* get the pointer to the timer descriptor */
    pTimerDesc = (agsaTimerDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->timerLinkMem), i);
    /* Initialize timer descriptor */
    saLlinkInitialize(&(pTimerDesc->linkNode));

    pTimerDesc->valid         = agFALSE;
    pTimerDesc->timeoutTick   = 0;
    pTimerDesc->pfnTimeout    = agNULL;
    pTimerDesc->Event         = 0;
    pTimerDesc->pParm         = agNULL;

    /* Add the timer descriptor to the free timer link list */
    saLlistAdd(&(saRoot->freeTimers), &(pTimerDesc->linkNode));
  }
  /* Setup valid timer link list */
  saLlistInitialize(&(saRoot->validTimers));

  /* Setup Phys */
  /* Setup PhyCount */
  saRoot->phyCount = (bit8) hwConfig->phyCount;
  /* Init Phy data structure */
  for ( i = 0; i < saRoot->phyCount; i ++ )
  {
    saRoot->phys[i].pPort = agNULL;
    saRoot->phys[i].phyId = (bit8) i;

    /* setup phy status is PHY_STOPPED */
    PHY_STATUS_SET(&(saRoot->phys[i]), PHY_STOPPED);
  }

  /* Setup Ports */
  /* Setup PortCount */
  saRoot->portCount = saRoot->phyCount;
  /* Setup free port link list */
  saLlistInitialize(&(saRoot->freePorts));
  for ( i = 0; i < saRoot->portCount; i ++ )
  {
    /* get the pointer to the port */
    pPort = &(saRoot->ports[i]);
    /* Initialize port */
    saLlinkInitialize(&(pPort->linkNode));

    pPort->portContext.osData   = agNULL;
    pPort->portContext.sdkData  = pPort;
    pPort->portId         = 0;
    pPort->portIdx        = (bit8) i;
    pPort->status         = PORT_NORMAL;

    for ( j = 0; j < saRoot->phyCount; j ++ )
    {
      pPort->phyMap[j] = agFALSE;
    }

    saLlistInitialize(&(pPort->listSASATADevices));

    /* Add the port to the free port link list */
    saLlistAdd(&(saRoot->freePorts), &(pPort->linkNode));
  }
  /* Setup valid port link list */
  saLlistInitialize(&(saRoot->validPorts));

  /* Init sysIntsActive */
  saRoot->sysIntsActive = agFALSE;

  /* setup timer tick granunarity */
  saRoot->usecsPerTick = usecsPerTick;

  /* initialize LL timer tick */
  saRoot->timeTick = 0;

  /* initialize device (de)registration callback fns */
  saRoot->DeviceRegistrationCB = agNULL;
  saRoot->DeviceDeregistrationCB = agNULL;

  /* Initialize the PortMap for port context */
  for ( i = 0; i < saRoot->portCount; i ++ )
  {
    pPortMap = &(saRoot->PortMap[i]);

    pPortMap->PortContext   = agNULL;
    pPortMap->PortID        = PORT_MARK_OFF;
    pPortMap->PortStatus    = PORT_NORMAL;
    saRoot->autoDeregDeviceflag[i] = 0;
  }

  /* Initialize the DeviceMap for device handle */
  for ( i = 0; i < MAX_IO_DEVICE_ENTRIES; i ++ )
  {
    pDeviceMap = &(saRoot->DeviceMap[i]);

    pDeviceMap->DeviceHandle  = agNULL;
    pDeviceMap->DeviceIdFromFW   =  i;
  }

  /* Initialize the IOMap for IOrequest */
  for ( i = 0; i < MAX_ACTIVE_IO_REQUESTS; i ++ )
  {
    pIOMap = &(saRoot->IOMap[i]);

    pIOMap->IORequest   = agNULL;
    pIOMap->Tag         = MARK_OFF;
  }

  /* clean the inbound queues */
  for (i = 0; i < saRoot->QueueConfig.numInboundQueues; i ++)
  {
    if(0 != saRoot->inboundQueue[i].numElements)
    {
      circularIQ = &saRoot->inboundQueue[i];
      si_memset(circularIQ->memoryRegion.virtPtr, 0, circularIQ->memoryRegion.totalLength);
      si_memset(saRoot->inboundQueue[i].ciPointer, 0, sizeof(bit32));
    }
  }
  /* clean the outbound queues */
  for (i = 0; i < saRoot->QueueConfig.numOutboundQueues; i ++)
  {
    if(0 != saRoot->outboundQueue[i].numElements)
    {
      circularOQ = &saRoot->outboundQueue[i];
      si_memset(circularOQ->memoryRegion.virtPtr, 0, circularOQ->memoryRegion.totalLength);
      si_memset(saRoot->outboundQueue[i].piPointer, 0, sizeof(bit32));
      circularOQ->producerIdx = 0;
      circularOQ->consumerIdx = 0;
      SA_DBG3(("siInitResource: Q %d  Clean PI 0x%03x CI 0x%03x\n", i,circularOQ->producerIdx, circularOQ->consumerIdx));
    }
  }

  return;
}

/*******************************************************************************/
/** \fn void mpiReadCALTable(agsaRoot_t *agRoot,
 *                           spc_SPASTable_t *mpiCALTable, bit32 index)
 *  \brief Reading the Phy Analog Setup Register Table
 *  \param agsaRoot    Handles for this instance of SAS/SATA LLL
 *  \param mpiCALTable Pointer of Phy Calibration Table
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiReadCALTable(agsaRoot_t      *agRoot,
                            spc_SPASTable_t *mpiCALTable,
                            bit32           index)
{
  bit32 CFGTableOffset, TableOffset;
  bit32 CALTableOffset;
  bit8  pcibar;

  /* get offset of the configuration table */
  TableOffset = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  CFGTableOffset = TableOffset & SCRATCH_PAD0_OFFSET_MASK;

  /* get PCI BAR */
  TableOffset = (TableOffset & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, TableOffset);

  /* read Calibration Table Offset from the configuration table */
  CALTableOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CFGTableOffset + MAIN_ANALOG_SETUP_OFFSET);
  if(smIS_SPCV(agRoot))
  {
    CALTableOffset &= 0x00FFFFFF;
  }
  CALTableOffset = CFGTableOffset + CALTableOffset + (index * ANALOG_SETUP_ENTRY_SIZE * 4);

  mpiCALTable->spaReg0 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_PORT_CFG1_OFFSET));
  mpiCALTable->spaReg1 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_PORT_CFG2_OFFSET));
  mpiCALTable->spaReg2 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_PORT_CFG3_OFFSET));
  mpiCALTable->spaReg3 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_CFG_OFFSET));
  mpiCALTable->spaReg4 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_PORT_CFG1_OFFSET));
  mpiCALTable->spaReg5 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_PORT_CFG2_OFFSET));
  mpiCALTable->spaReg6 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_CFG1_OFFSET));
  mpiCALTable->spaReg7 = ossaHwRegReadExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_CFG2_OFFSET));

  SA_DBG3(("mpiReadCALTable: spaReg0 0x%x\n", mpiCALTable->spaReg0));
  SA_DBG3(("mpiReadCALTable: spaReg1 0x%x\n", mpiCALTable->spaReg1));
  SA_DBG3(("mpiReadCALTable: spaReg2 0x%x\n", mpiCALTable->spaReg2));
  SA_DBG3(("mpiReadCALTable: spaReg3 0x%x\n", mpiCALTable->spaReg3));
  SA_DBG3(("mpiReadCALTable: spaReg4 0x%x\n", mpiCALTable->spaReg4));
  SA_DBG3(("mpiReadCALTable: spaReg5 0x%x\n", mpiCALTable->spaReg5));
  SA_DBG3(("mpiReadCALTable: spaReg6 0x%x\n", mpiCALTable->spaReg6));
  SA_DBG3(("mpiReadCALTable: spaReg7 0x%x\n", mpiCALTable->spaReg7));
}

/*******************************************************************************/
/** \fn void mpiWriteCALTable(agsaRoot_t *agRoot,
 *                            spc_SPASTable_t *mpiCALTable, index)
 *  \brief Writing the Phy Analog Setup Register Table
 *  \param agsaRoot    Handles for this instance of SAS/SATA LLL
 *  \param mpiCALTable Pointer of Phy Calibration Table
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiWriteCALTable(agsaRoot_t     *agRoot,
                            spc_SPASTable_t *mpiCALTable,
                            bit32           index)
{
  bit32 CFGTableOffset, TableOffset;
  bit32 CALTableOffset;
  bit8  pcibar;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"m6");

  /* get offset of the configuration table */
  TableOffset = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  CFGTableOffset = TableOffset & SCRATCH_PAD0_OFFSET_MASK;

  /* get PCI BAR */
  TableOffset = (TableOffset & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, TableOffset);

  /* read Calibration Table Offset from the configuration table */
  CALTableOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CFGTableOffset + MAIN_ANALOG_SETUP_OFFSET);
  if(smIS_SPCV(agRoot))
  {
    CALTableOffset &= 0x00FFFFFF;
  }
  CALTableOffset = CFGTableOffset + CALTableOffset + (index * ANALOG_SETUP_ENTRY_SIZE * 4);

  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_PORT_CFG1_OFFSET), mpiCALTable->spaReg0);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_PORT_CFG2_OFFSET), mpiCALTable->spaReg1);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_PORT_CFG3_OFFSET), mpiCALTable->spaReg2);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + TX_CFG_OFFSET),       mpiCALTable->spaReg3);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_PORT_CFG1_OFFSET), mpiCALTable->spaReg4);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_PORT_CFG2_OFFSET), mpiCALTable->spaReg5);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_CFG1_OFFSET),      mpiCALTable->spaReg6);
  ossaHwRegWriteExt(agRoot, pcibar, (bit32)(CALTableOffset + RV_CFG2_OFFSET),      mpiCALTable->spaReg7);

  SA_DBG4(("mpiWriteCALTable: Offset 0x%08x  spaReg0 0x%x 0x%x 0x%x 0x%x\n",(bit32)(CALTableOffset + TX_PORT_CFG1_OFFSET), mpiCALTable->spaReg0, mpiCALTable->spaReg1, mpiCALTable->spaReg2, mpiCALTable->spaReg3));
  SA_DBG4(("mpiWriteCALTable: Offset 0x%08x  spaReg4 0x%x 0x%x 0x%x 0x%x\n",(bit32)(CALTableOffset + RV_PORT_CFG1_OFFSET), mpiCALTable->spaReg4, mpiCALTable->spaReg5, mpiCALTable->spaReg6, mpiCALTable->spaReg7));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "m6");
}

/*******************************************************************************/
/** \fn void mpiWriteCALAll(agsaRoot_t *agRoot,
 *                          agsaPhyAnalogSetupTable_t *mpiCALTable)
 *  \brief Writing the Phy Analog Setup Register Table
 *  \param agsaRoot    Handles for this instance of SAS/SATA LLL
 *  \param mpiCALTable Pointer of Phy Calibration Table
 *
 * Return:
 *         None
 */
/*******************************************************************************/
GLOBAL void mpiWriteCALAll(agsaRoot_t     *agRoot,
                           agsaPhyAnalogSetupTable_t *mpiCALTable)
{
  bit8 i;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"mz");

  if(smIS_SPCV(agRoot))
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "mz");
    return;
  }

  for (i = 0; i < MAX_INDEX; i++)
  {
    mpiWriteCALTable(agRoot, (spc_SPASTable_t *)&mpiCALTable->phyAnalogSetupRegisters[i], i);
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "mz");
}

GLOBAL void mpiWrAnalogSetupTable(agsaRoot_t *agRoot,
                                   mpiConfig_t      *config
                                 )
{

  bit32 AnalogTableBase,CFGTableOffset, value,phy;
  bit32 AnalogtableSize;
  bit8  pcibar;
  value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, value);

  CFGTableOffset = value & SCRATCH_PAD0_OFFSET_MASK;
  AnalogtableSize  = AnalogTableBase = ossaHwRegReadExt(agRoot,pcibar , (bit32)CFGTableOffset + MAIN_ANALOG_SETUP_OFFSET);
  AnalogtableSize &= 0xFF000000;
  AnalogtableSize >>= SHIFT24;
  AnalogTableBase &= 0x00FFFFFF;

  AnalogTableBase = CFGTableOffset + AnalogTableBase;

//   config->phyAnalogConfig.phyAnalogSetupRegisters[0].spaRegister0 = 0;
  SA_DBG1(("mpiWrAnalogSetupTable:Analogtable Base Offset %08X pcibar %d\n",AnalogTableBase, pcibar ));

  SA_DBG1(("mpiWrAnalogSetupTable:%d %d\n",(int)sizeof(agsaPhyAnalogSetupRegisters_t), AnalogtableSize));

  for(phy = 0; phy < 10; phy++) /* upto 10 phys See PM*/
  {
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 0 ),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister0 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 4 ),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister1 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 8 ),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister2 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 12),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister3 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 16),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister4 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 20),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister5 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 24),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister6 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 28),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister7 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 32),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister8 );
      ossaHwRegWriteExt(agRoot, pcibar,(AnalogTableBase + ( AnalogtableSize * phy)+ 36),config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister9 );

      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister0 0x%x 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) + 0,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister0 ,ossaHwRegReadExt(agRoot, pcibar,AnalogTableBase + ( AnalogtableSize * phy)+ 0 )));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister1 0x%x 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) + 4,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister1 ,ossaHwRegReadExt(agRoot, pcibar,AnalogTableBase + ( AnalogtableSize * phy)+ 4 )));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister2 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) + 8,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister2 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister3 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +12,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister3 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister4 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +16,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister4 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister5 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +20,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister5 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister6 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +24,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister6 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister7 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +28,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister7 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister8 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +32,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister8 ));
      SA_DBG4(("mpiWrAnalogSetupTable:phy %d Offset 0x%08x spaRegister9 0x%x\n",phy, (bit32) AnalogTableBase+ (AnalogtableSize * phy) +36,config->phyAnalogConfig.phyAnalogSetupRegisters[phy].spaRegister9 ));
  }

}


GLOBAL void mpiWrIntVecTable(agsaRoot_t *agRoot,
                            mpiConfig_t* config
                            )
{
  bit32 CFGTableOffset, value;
  bit32 INTVTableOffset;
  bit32 ValuetoWrite;
  bit8  pcibar, i,obq;

  /* get offset of the configuration table */
  value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  CFGTableOffset = value & SCRATCH_PAD0_OFFSET_MASK;

  /* get PCI BAR */
  value = (value & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, value);

  /* read Interrupt Table Offset from the main configuration table */
  INTVTableOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CFGTableOffset + MAIN_INT_VEC_TABLE_OFFSET);
  INTVTableOffset &= 0x00FFFFFF;
  INTVTableOffset = CFGTableOffset + INTVTableOffset;
  SA_DBG1(("mpiWrIntVecTable: Base Offset %08X\n",(bit32)(INTVTableOffset + INT_VT_Coal_CNT_TO ) ));

  for (i = 0; i < MAX_NUM_VECTOR; i ++)
  {
    bit32 found=0;
    for (obq = 0; obq < MAX_NUM_VECTOR; obq++)
    { /* find OBQ for  vector i */
      if( config->outboundQueues[obq].interruptVector == i )
      {
        found=1;
        break;
      }
    }

    if(!found )
    {
      continue;
    }

    ValuetoWrite = (( config->outboundQueues[obq].interruptDelay << SHIFT15) | config->outboundQueues[obq].interruptThreshold  );

    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(INTVTableOffset + INT_VT_Coal_CNT_TO + i * sizeof(InterruptVT_t)), ValuetoWrite );

    SA_DBG3(("mpiWrIntVecTable: Q %d interruptDelay 0x%X interruptThreshold 0x%X \n",i,
             config->outboundQueues[i].interruptDelay,  config->outboundQueues[i].interruptThreshold ));

    SA_DBG3(("mpiWrIntVecTable: %d INT_VT_Coal_CNT_TO Bar %d Offset %3X Writing 0x%08x\n",i,
            pcibar,
            (bit32)(INTVTableOffset + INT_VT_Coal_CNT_TO + i * sizeof(InterruptVT_t)),
            ValuetoWrite));

  }

  for (i = 0; i < MAX_NUM_VECTOR; i++)
  {
    /* read interrupt colescing control and timer  */
    value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(INTVTableOffset + INT_VT_Coal_CNT_TO + i * sizeof(InterruptVT_t)));
    SA_DBG4(("mpiWrIntVecTable: Offset 0x%08x Interrupt Colescing iccict[%02d] 0x%x\n", (bit32)(INTVTableOffset + INT_VT_Coal_CNT_TO + i * sizeof(InterruptVT_t)), i, value));
  }
}

GLOBAL void mpiWrPhyAttrbTable(agsaRoot_t *agRoot, sasPhyAttribute_t *phyAttrib)
{
  bit32 CFGTableOffset, value;
  bit32 PHYTableOffset;
  bit8  pcibar, i;

  /* get offset of the configuration table */
  value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0);

  CFGTableOffset = value & SCRATCH_PAD0_OFFSET_MASK;

  /* get PCI BAR */
  value = (value & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;
  /* convert the PCI BAR to logical bar number */
  pcibar = (bit8)mpiGetPCIBarIndex(agRoot, value);

  /* read Phy Attribute Table Offset from the configuration table */
  PHYTableOffset = ossaHwRegReadExt(agRoot, pcibar, (bit32)CFGTableOffset + MAIN_PHY_ATTRIBUTE_OFFSET);

  PHYTableOffset &=0x00FFFFFF;

  PHYTableOffset = CFGTableOffset + PHYTableOffset + PHY_EVENT_OQ;

  SA_DBG1(("mpiWrPhyAttrbTable: PHYTableOffset 0x%08x\n", PHYTableOffset));

  /* write OQ event per phy */
  for (i = 0; i < MAX_VALID_PHYS; i ++)
  {
    ossaHwRegWriteExt(agRoot, pcibar, (bit32)(PHYTableOffset + i * sizeof(phyAttrb_t)), phyAttrib->phyAttribute[i].phyEventOQ);

  SA_DBG3(("mpiWrPhyAttrbTable:%d Offset 0x%08x phyAttribute 0x%x\n",i,(bit32)(PHYTableOffset + i * sizeof(phyAttrb_t)), phyAttrib->phyAttribute[i].phyEventOQ ));


  }

  for (i = 0; i < MAX_VALID_PHYS; i ++)
  {
    value = ossaHwRegReadExt(agRoot, pcibar, (bit32)(PHYTableOffset + i * sizeof(phyAttrb_t)));
  SA_DBG1(("mpiWrPhyAttrbTable: OQ Event per phy[%x] 0x%x\n", i, value));
  }
}


#ifdef TEST /******************************************************************/
/*******************************************************************************/
/** \fn mpiFreezeInboundQueue(agsaRoot_t *agRoot)
 *  \brief Freeze the inbound queue
 *
 *  \param agRoot             Handles for this instance of SAS/SATA hardware
 *  \param bitMapQueueNum0    bit map for inbound queue number 0 - 31 to freeze
 *  \param bitMapQueueNum1    bit map for inbound queue number 32 - 63 to freeze
 *
 * Return:
 *         AGSA_RC_SUCCESS if Un-initialize the configuration table sucessful
 *         AGSA_RC_FAILURE if Un-initialize the configuration table failed
 */
/*******************************************************************************/
GLOBAL bit32 mpiFreezeInboundQueue(agsaRoot_t *agRoot, bit32 bitMapQueueNum0, bit32 bitMapQueueNum1)
{
  bit32    value, togglevalue;
  bit32    max_wait_time;
  bit32    max_wait_count;

  SA_DBG2(("Entering function:mpiFreezeInboundQueue\n"));
  SA_ASSERT(NULL != agRoot, "agRoot argument cannot be null");

  togglevalue = 0;

  if (bitMapQueueNum0)
  {
    /* update the inbound queue number to HOST_SCRATCH_PAD1 register for queue 0 to 31 */
    SA_DBG1(("mpiFreezeInboundQueue: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_0)));
    SA_DBG1(("mpiFreezeInboundQueue: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_3,MSGU_SCRATCH_PAD_3)));

    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_0,MSGU_SCRATCH_PAD_1);
    value |= bitMapQueueNum0;
    siHalRegWriteExt(agRoot, GEN_MSGU_HOST_SCRATCH_PAD_1, MSGU_HOST_SCRATCH_PAD_1, value);
  }

  if (bitMapQueueNum1)
  {
    /* update the inbound queue number to HOST_SCRATCH_PAD2 register for queue 32 to 63 */
    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_2);
    value |= bitMapQueueNum1;
    siHalRegWriteExt(agRoot, GEN_MSGU_HOST_SCRATCH_PAD_2, MSGU_HOST_SCRATCH_PAD_2, value);
  }

  /* Write bit 2 to Inbound DoorBell Register */
  siHalRegWriteExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET, IBDB_IBQ_FREEZE);

  /* wait until Inbound DoorBell Clear Register toggled */
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    /* Read Inbound DoorBell Register - for RevB */
//    value = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_IBDB_SET);
    value = MSGU_READ_IDR;
    value &= IBDB_IBQ_FREEZE;
  } while ((value != togglevalue) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("mpiFreezeInboundQueue: IBDB value/toggle = 0x%x 0x%x\n", value, togglevalue));
    return AGSA_RC_FAILURE;
  }

  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/** \fn mpiUnFreezeInboundQueue(agsaRoot_t *agRoot)
 *  \brief Freeze the inbound queue
 *
 *  \param agRoot             Handles for this instance of SAS/SATA hardware
 *  \param bitMapQueueNum0    bit map for inbound queue number 0 - 31 to freeze
 *  \param bitMapQueueNum1    bit map for inbound queue number 32 - 63 to freeze
 *
 * Return:
 *         AGSA_RC_SUCCESS if Un-initialize the configuration table sucessful
 *         AGSA_RC_FAILURE if Un-initialize the configuration table failed
 */
/******************************************************************************/
GLOBAL bit32 mpiUnFreezeInboundQueue(agsaRoot_t *agRoot, bit32 bitMapQueueNum0, bit32 bitMapQueueNum1)
{
  bit32    value, togglevalue;
  bit32    max_wait_time;
  bit32    max_wait_count;

  SA_DBG2(("Entering function:mpiUnFreezeInboundQueue\n"));
  SA_ASSERT(NULL != agRoot, "agRoot argument cannot be null");

  togglevalue = 0;

  if (bitMapQueueNum0)
  {
    /* update the inbound queue number to HOST_SCRATCH_PAD1 register - for queue 0 to 31 */
    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1);
    value |= bitMapQueueNum0;
    siHalRegWriteExt(agRoot, GEN_MSGU_HOST_SCRATCH_PAD_1, MSGU_HOST_SCRATCH_PAD_1, value);
  }

  if (bitMapQueueNum1)
  {
    /* update the inbound queue number to HOST_SCRATCH_PAD2 register - for queue 32 to 63 */
    value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_2);
    value |= bitMapQueueNum1;
    siHalRegWriteExt(agRoot, GEN_MSGU_HOST_SCRATCH_PAD_2, MSGU_HOST_SCRATCH_PAD_2, value);
  }

  /* Write bit 2 to Inbound DoorBell Register */
  siHalRegWriteExt(agRoot, GEN_MSGU_IBDB_SET, MSGU_IBDB_SET, IBDB_IBQ_UNFREEZE);

  /* wait until Inbound DoorBell Clear Register toggled */
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    /* Read Inbound DoorBell Register - for RevB */
    value = MSGU_READ_IDR;
    value &= IBDB_IBQ_UNFREEZE;
  } while ((value != togglevalue) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("mpiUnFreezeInboundQueue: IBDB value/toggle = 0x%x 0x%x\n", value, togglevalue));
    return AGSA_RC_FAILURE;
  }

  return AGSA_RC_SUCCESS;
}

#endif /* TEST ****************************************************************/

GLOBAL bit32 si_check_V_HDA(agsaRoot_t *agRoot)
{
  bit32 ret = AGSA_RC_SUCCESS;
  bit32 hda_status = 0;

  hda_status = (ossaHwRegReadExt(agRoot, PCIBAR0, SPC_V_HDA_RESPONSE_OFFSET+28));

  SA_DBG1(("si_check_V_HDA: hda_status 0x%08X\n",hda_status ));

  if((hda_status  & SPC_V_HDAR_RSPCODE_MASK)  == SPC_V_HDAR_IDLE)
  {
    /* HDA mode */
    SA_DBG1(("si_check_V_HDA: HDA mode, value = 0x%x\n", hda_status));
    ret = AGSA_RC_HDA_NO_FW_RUNNING;
  }


  return(ret);
}
GLOBAL bit32  si_check_V_Ready(agsaRoot_t *agRoot)
{
  bit32 ret = AGSA_RC_SUCCESS;
  bit32    SCRATCH_PAD1;
  bit32    max_wait_time;
  bit32    max_wait_count;
/* ILA */
  max_wait_time = (200 * 1000); /* wait 200 milliseconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
  } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_ILA_MASK) != SCRATCH_PAD1_V_ILA_MASK) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("si_check_V_Ready: SCRATCH_PAD1_V_ILA_MASK (0x%x)  not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_ILA_MASK, SCRATCH_PAD1));
    return( AGSA_RC_FAILURE);
  }
  /* RAAE */
  max_wait_time = (200 * 1000); /* wait 200 milliseconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
  } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_RAAE_MASK) != SCRATCH_PAD1_V_RAAE_MASK) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("si_check_V_Ready: SCRATCH_PAD1_V_RAAE_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_RAAE_MASK, SCRATCH_PAD1));
    return( AGSA_RC_FAILURE);

  }
  /* IOP0 */
  max_wait_time = (200 * 1000); /* wait 200 milliseconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
  } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP0_MASK) != SCRATCH_PAD1_V_IOP0_MASK) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("si_check_V_Ready: SCRATCH_PAD1_V_IOP0_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_IOP0_MASK ,SCRATCH_PAD1));
    return( AGSA_RC_FAILURE);

  }

  /* IOP1 */
  max_wait_time = (200 * 1000); /* wait 200 milliseconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
  } while (((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP1_MASK) != SCRATCH_PAD1_V_IOP1_MASK) && (max_wait_count -= WAIT_INCREMENT));

  if (!max_wait_count)
  {
    SA_DBG1(("si_check_V_Ready: SCRATCH_PAD1_V_IOP1_MASK (0x%x) not set SCRATCH_PAD1 = 0x%x\n",SCRATCH_PAD1_V_IOP1_MASK, SCRATCH_PAD1));
    // return( AGSA_RC_FAILURE);
  }

  return(ret);
}

GLOBAL bit32 siScratchDump(agsaRoot_t *agRoot)
{
  bit32 SCRATCH_PAD1;
  bit32 ret =0;
#ifdef SALLSDK_DEBUG
  bit32 SCRATCH_PAD2;
  bit32 SCRATCH_PAD3;
  bit32 SCRATCH_PAD0;

  SCRATCH_PAD0 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_0);
  SCRATCH_PAD2 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_2);
  SCRATCH_PAD3 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_3);
#endif  /* SALLSDK_DEBUG */
  SCRATCH_PAD1 = ossaHwRegReadExt(agRoot, PCIBAR0, MSGU_SCRATCH_PAD_1);
  SA_DBG1(("siScratchDump: SCRATCH_PAD 0 0x%08x 1 0x%08x 2 0x%08x 3 0x%08x\n",SCRATCH_PAD0,SCRATCH_PAD1,SCRATCH_PAD2,SCRATCH_PAD3 ));

  if((SCRATCH_PAD1 & SCRATCH_PAD1_V_RESERVED) == SCRATCH_PAD1_V_RESERVED  )
  {
    SA_DBG1(("siScratchDump: SCRATCH_PAD1 SCRATCH_PAD1_V_RESERVED 0x%08x\n", SCRATCH_PAD1_V_RESERVED));
  }
  else
  {
    if((SCRATCH_PAD1 & SCRATCH_PAD1_V_RAAE_MASK) == SCRATCH_PAD1_V_RAAE_MASK  )
    {
      SA_DBG1(("siScratchDump: SCRATCH_PAD1 valid 0x%08x\n",SCRATCH_PAD0 ));
      SA_DBG1(("siScratchDump: RAAE ready 0x%08x\n",SCRATCH_PAD1 & SCRATCH_PAD1_V_RAAE_MASK));
    }
    if((SCRATCH_PAD1 & SCRATCH_PAD1_V_ILA_MASK) == SCRATCH_PAD1_V_ILA_MASK)
    {
      SA_DBG1(("siScratchDump: ILA  ready 0x%08x\n", SCRATCH_PAD1 & SCRATCH_PAD1_V_ILA_MASK));
    }

    if(SCRATCH_PAD1 & SCRATCH_PAD1_V_BOOTSTATE_MASK)
    {
      SA_DBG1(("siScratchDump: BOOTSTATE not success 0x%08x\n",SCRATCH_PAD1 & SCRATCH_PAD1_V_BOOTSTATE_MASK));
    }

    if((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP0_MASK) == SCRATCH_PAD1_V_IOP0_MASK)
    {
      SA_DBG1(("siScratchDump: IOP0 ready 0x%08x\n",SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP0_MASK));
    }
    if((SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP1_MASK) == SCRATCH_PAD1_V_IOP1_MASK)
    {
      SA_DBG1(("siScratchDump: IOP1 ready 0x%08x\n",SCRATCH_PAD1 & SCRATCH_PAD1_V_IOP1_MASK ));
    }
    if((SCRATCH_PAD1 & SCRATCH_PAD1_V_READY) == SCRATCH_PAD1_V_READY)
    {
      SA_DBG1(("siScratchDump: SCRATCH_PAD1_V_READY  0x%08x\n",SCRATCH_PAD1 & SCRATCH_PAD1_V_READY ));
    }
    if((SCRATCH_PAD1 & SCRATCH_PAD1_V_BOOTSTATE_MASK) == SCRATCH_PAD1_V_BOOTSTATE_MASK)
    {
      SA_DBG1(("siScratchDump: SCRATCH_PAD1_V_BOOTSTATE_MASK  0x%08x\n",SCRATCH_PAD1 & SCRATCH_PAD1_V_BOOTSTATE_MASK ));
    }
  }
  return(ret);

}


void si_macro_check(agsaRoot_t *agRoot)
{

  SA_DBG1(("si_macro_check:smIS_SPC      %d\n",smIS_SPC(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_HIL      %d\n",smIS_HIL(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SFC      %d\n",smIS_SFC(agRoot)  ));

  SA_DBG1(("si_macro_check:smIS_spc8001  %d\n",smIS_spc8001(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_spc8081  %d\n",smIS_spc8081(agRoot)  ));

  SA_DBG1(("si_macro_check:smIS_SPCV8008 %d\n",smIS_SPCV8008(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8009 %d\n",smIS_SPCV8009(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8018 %d\n",smIS_SPCV8018(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8019 %d\n",smIS_SPCV8019(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_ADAP8088 %d\n",smIS_ADAP8088(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_ADAP8089 %d\n",smIS_ADAP8089(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8070 %d\n",smIS_SPCV8070(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8071 %d\n",smIS_SPCV8071(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8072 %d\n",smIS_SPCV8072(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8073 %d\n",smIS_SPCV8073(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8074 %d\n",smIS_SPCV8074(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8075 %d\n",smIS_SPCV8075(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8076 %d\n",smIS_SPCV8076(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV8077 %d\n",smIS_SPCV8077(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV9015 %d\n",smIS_SPCV9015(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV9060 %d\n",smIS_SPCV9060(agRoot)  ));
  SA_DBG1(("si_macro_check:smIS_SPCV     %d\n",smIS_SPCV(agRoot)      ));

  SA_DBG1(("si_macro_check:smIS64bInt    %d\n", smIS64bInt(agRoot)    ));

}

