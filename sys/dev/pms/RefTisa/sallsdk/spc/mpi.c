/*******************************************************************************
**
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
/*! \file mpi.c
 *  \brief The file is a MPI Libraries to implement the MPI functions
 *
 * The file implements the MPI Library functions.
 *
 */
/*******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'A'
#endif

#ifdef LOOPBACK_MPI
extern int loopback;
#endif
/*******************************************************************************/

/*******************************************************************************/
/*******************************************************************************/
/* FUNCTIONS                                                                   */
/*******************************************************************************/
/*******************************************************************************/
/** \fn void mpiRequirementsGet(mpiConfig_t* config, mpiMemReq_t* memoryRequirement)
 *  \brief Retrieves the MPI layer resource requirements
 *  \param config            MPI configuration for the Host MPI Message Unit
 *  \param memoryRequirement Returned data structure as defined by mpiMemReq_t
 *                           that holds the different chunks of memory that are required
 *
 * The mpiRequirementsGet() function is used to determine the resource requirements
 * for the SPC device interface
 *
 * Return: None
 */
/*******************************************************************************/
void mpiRequirementsGet(mpiConfig_t* config, mpiMemReq_t* memoryRequirement)
{
  bit32 qIdx, numq;
  mpiMemReq_t* memoryMap;
  SA_DBG2(("Entering function:mpiRequirementsGet\n"));
  SA_ASSERT((NULL != config), "config argument cannot be null");

  memoryMap = memoryRequirement;
  memoryMap->count = 0;

  /* MPI Memory region 0 for MSGU(AAP1) Event Log for fw */
  memoryMap->region[memoryMap->count].numElements = 1;
  memoryMap->region[memoryMap->count].elementSize = sizeof(bit8) * config->mainConfig.eventLogSize;
  memoryMap->region[memoryMap->count].totalLength = sizeof(bit8) * config->mainConfig.eventLogSize;
  memoryMap->region[memoryMap->count].alignment = 32;
  memoryMap->region[memoryMap->count].type = AGSA_DMA_MEM;
  SA_DBG2(("mpiRequirementsGet:eventLogSize region[%d] 0x%X\n",memoryMap->count,memoryMap->region[memoryMap->count].totalLength ));
  memoryMap->count++;

  SA_DBG2(("mpiRequirementsGet:eventLogSize region[%d] 0x%X\n",memoryMap->count,memoryMap->region[memoryMap->count].totalLength ));
  /* MPI Memory region 1 for IOP Event Log for fw */
  memoryMap->region[memoryMap->count].numElements = 1;
  memoryMap->region[memoryMap->count].elementSize = sizeof(bit8) * config->mainConfig.IOPeventLogSize;
  memoryMap->region[memoryMap->count].totalLength = sizeof(bit8) * config->mainConfig.IOPeventLogSize;
  memoryMap->region[memoryMap->count].alignment = 32;
  memoryMap->region[memoryMap->count].type = AGSA_DMA_MEM;
  SA_DBG2(("mpiRequirementsGet:IOPeventLogSize region[%d] 0x%X\n",memoryMap->count,memoryMap->region[memoryMap->count].totalLength ));
  memoryMap->count++;

  /* MPI Memory region 2 for consumer Index of inbound queues */
  memoryMap->region[memoryMap->count].numElements = 1;
  memoryMap->region[memoryMap->count].elementSize = sizeof(bit32) * config->numInboundQueues;
  memoryMap->region[memoryMap->count].totalLength = sizeof(bit32) * config->numInboundQueues;
  memoryMap->region[memoryMap->count].alignment = 4;
  memoryMap->region[memoryMap->count].type = AGSA_DMA_MEM;
  SA_DBG2(("mpiRequirementsGet:numInboundQueues region[%d] 0x%X\n",memoryMap->count,memoryMap->region[memoryMap->count].totalLength ));
  memoryMap->count++;

  /* MPI Memory region 3 for producer Index of outbound queues */
  memoryMap->region[memoryMap->count].numElements = 1;
  memoryMap->region[memoryMap->count].elementSize = sizeof(bit32) * config->numOutboundQueues;
  memoryMap->region[memoryMap->count].totalLength = sizeof(bit32) * config->numOutboundQueues;
  memoryMap->region[memoryMap->count].alignment = 4;
  memoryMap->region[memoryMap->count].type = AGSA_DMA_MEM;
  SA_DBG2(("mpiRequirementsGet:numOutboundQueues region[%d] 0x%X\n",memoryMap->count,memoryMap->region[memoryMap->count].totalLength ));
  memoryMap->count++;

  /* MPI Memory regions 4, ... for the inbound queues - depends on configuration */
  numq = 0;
  for(qIdx = 0; qIdx < config->numInboundQueues; qIdx++)
  {
    if(0 != config->inboundQueues[qIdx].numElements)
    {
        bit32 memSize = config->inboundQueues[qIdx].numElements * config->inboundQueues[qIdx].elementSize;
        bit32 remainder = memSize & 127;

        /* Calculate the size of this queue padded to 128 bytes */
        if (remainder > 0)
        {
            memSize += (128 - remainder);
        }

        if (numq == 0)
        {
            memoryMap->region[memoryMap->count].numElements = 1;
            memoryMap->region[memoryMap->count].elementSize = memSize;
            memoryMap->region[memoryMap->count].totalLength = memSize;
            memoryMap->region[memoryMap->count].alignment = 128;
            memoryMap->region[memoryMap->count].type = AGSA_CACHED_DMA_MEM;
        }
        else
        {
            memoryMap->region[memoryMap->count].elementSize += memSize;
            memoryMap->region[memoryMap->count].totalLength += memSize;
        }

        numq++;

        if ((0 == ((qIdx + 1) % MAX_QUEUE_EACH_MEM)) ||
            (qIdx == (bit32)(config->numInboundQueues - 1)))
        {
            SA_DBG2(("mpiRequirementsGet: (inboundQueues) memoryMap->region[%d].elementSize = %d\n",
                     memoryMap->count, memoryMap->region[memoryMap->count].elementSize));
            SA_DBG2(("mpiRequirementsGet: (inboundQueues) memoryMap->region[%d].numElements = %d\n",
                     memoryMap->count, memoryMap->region[memoryMap->count].numElements));

            memoryMap->count++;
            numq = 0;
        }
    }
  }

  /* MPI Memory regions for the outbound queues - depends on configuration */
  numq = 0;
  for(qIdx = 0; qIdx < config->numOutboundQueues; qIdx++)
  {
    if(0 != config->outboundQueues[qIdx].numElements)
    {
        bit32 memSize = config->outboundQueues[qIdx].numElements * config->outboundQueues[qIdx].elementSize;
        bit32 remainder = memSize & 127;

        /* Calculate the size of this queue padded to 128 bytes */
        if (remainder > 0)
        {
            memSize += (128 - remainder);
        }

        if (numq == 0)
        {
            memoryMap->region[memoryMap->count].numElements = 1;
            memoryMap->region[memoryMap->count].elementSize = memSize;
            memoryMap->region[memoryMap->count].totalLength = memSize;
            memoryMap->region[memoryMap->count].alignment = 128;
            memoryMap->region[memoryMap->count].type = AGSA_CACHED_DMA_MEM;
        }
        else
        {
            memoryMap->region[memoryMap->count].elementSize += memSize;
            memoryMap->region[memoryMap->count].totalLength += memSize;
        }

        numq++;

        if ((0 == ((qIdx + 1) % MAX_QUEUE_EACH_MEM)) ||
            (qIdx ==  (bit32)(config->numOutboundQueues - 1)))
        {
            SA_DBG2(("mpiRequirementsGet: (outboundQueues) memoryMap->region[%d].elementSize = %d\n",
                     memoryMap->count, memoryMap->region[memoryMap->count].elementSize));
            SA_DBG2(("mpiRequirementsGet: (outboundQueues) memoryMap->region[%d].numElements = %d\n",
                     memoryMap->count, memoryMap->region[memoryMap->count].numElements));


            memoryMap->count++;
            numq = 0;
        }
    }
  }

}

/*******************************************************************************/
/** \fn mpiMsgFreeGet(mpiICQueue_t *circularQ, bit16 messageSize, void** messagePtr)
 *  \brief Retrieves a free message buffer from an inbound queue
 *  \param circularQ    Pointer to an inbound circular queue
 *  \param messageSize  Requested message size in bytes - only support 64 bytes/element
 *  \param messagePtr   Pointer to the free message buffer payload (not including message header) or NULL if no free message buffers are available
 *
 * This function is used to retrieve a free message buffer for the given inbound queue of at least
 * messageSize bytes.
 * The caller can use the returned buffer to construct the message and then call mpiMsgProduce()
 * to deliver the message to the device message unit or mpiMsgInvalidate() if the message buffer
 * is not going to be used
 *
 * Return:
 *         AGSA_RC_SUCCESS if messagePtr contains a valid message buffer pointer
 *         AGSA_RC_FAILURE if messageSize larger than the elementSize of queue
 *         AGSA_RC_BUSY    if there are not free message buffers (Queue full)
 */
/*******************************************************************************/
GLOBAL FORCEINLINE
bit32
mpiMsgFreeGet(
  mpiICQueue_t *circularQ,
  bit16 messageSize,
  void** messagePtr
  )
{
  bit32 offset;
  agsaRoot_t          *agRoot=circularQ->agRoot;
  mpiMsgHeader_t *msgHeader;
  bit8 bcCount = 1; /* only support single buffer */

  SA_DBG4(("Entering function:mpiMsgFreeGet\n"));
  SA_ASSERT(NULL != circularQ, "circularQ cannot be null");
  SA_ASSERT(NULL != messagePtr, "messagePtr argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue is 0");

  /* Checks is the requested message size can be allocated in this queue */
  if(messageSize > circularQ->elementSize)
  {
    SA_DBG1(("mpiMsgFreeGet: Message Size (%d) is larger than Q element size (%d)\n",messageSize,circularQ->elementSize));
    return AGSA_RC_FAILURE;
  }

  /* Stores the new consumer index */
  OSSA_READ_LE_32(circularQ->agRoot, &circularQ->consumerIdx, circularQ->ciPointer, 0);
  /* if inbound queue is full, return busy */
  /* This queue full logic may only works for bc == 1 ( == ) */
  /* ( pi + bc ) % size > ci not fully works for bc > 1 */
  /* To do - support bc > 1 case and wrap around case */
  if (((circularQ->producerIdx + bcCount) % circularQ->numElements) == circularQ->consumerIdx)
  {
    *messagePtr = NULL;
    smTrace(hpDBG_VERY_LOUD,"Za", (((circularQ->producerIdx & 0xFFF) << 16) |  (circularQ->consumerIdx & 0xFFF) ));
    /* TP:Za IQ PI CI */
    ossaHwRegRead(agRoot, MSGU_HOST_SCRATCH_PAD_0);
    SA_DBG1(("mpiMsgFreeGet: %d + %d == %d AGSA_RC_BUSY\n",circularQ->producerIdx,bcCount,circularQ->consumerIdx));

    return AGSA_RC_BUSY;
  }

  smTrace(hpDBG_VERY_LOUD,"Zb", (((circularQ->producerIdx & 0xFFF) << 16) |  (circularQ->consumerIdx & 0xFFF) ));
  /* TP:Zb IQ PI CI */


  /* get memory IOMB buffer address */
  offset = circularQ->producerIdx * circularQ->elementSize;
  /* increment to next bcCount element */
  circularQ->producerIdx = (circularQ->producerIdx + bcCount) % circularQ->numElements;

  /* Adds that distance to the base of the region virtual address plus the message header size*/
  msgHeader = (mpiMsgHeader_t*) (((bit8 *)(circularQ->memoryRegion.virtPtr)) + offset);

  SA_DBG3(("mpiMsgFreeGet: msgHeader = %p Offset = 0x%x\n", (void *)msgHeader, offset));

  /* Sets the message buffer in "allocated" state */
  /* bc always is 1 for inbound queue */
  /* temporarily store it in the native endian format, when the rest of the */
  /* header is filled, this would be converted to Little Endian */
  msgHeader->Header = (1<<24);
  *messagePtr = ((bit8*)msgHeader) + sizeof(mpiMsgHeader_t);

  return AGSA_RC_SUCCESS;
}

#ifdef LOOPBACK_MPI
GLOBAL bit32 mpiMsgFreeGetOQ(mpiOCQueue_t *circularQ, bit16 messageSize, void** messagePtr)
{
  bit32 offset;
  mpiMsgHeader_t *msgHeader;
  bit8 bcCount = 1; /* only support single buffer */

  SA_DBG4(("Entering function:mpiMsgFreeGet\n"));
  SA_ASSERT(NULL != circularQ, "circularQ cannot be null");
  SA_ASSERT(NULL != messagePtr, "messagePtr argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue is 0");

  /* Checks is the requested message size can be allocated in this queue */
  if(messageSize > circularQ->elementSize)
  {
    SA_DBG1(("mpiMsgFreeGet: Message Size is not fit in\n"));
    return AGSA_RC_FAILURE;
  }

  /* Stores the new consumer index */
  //OSSA_READ_LE_32(circularQ->agRoot, &circularQ->consumerIdx, circularQ->ciPointer, 0);
  /* if inbound queue is full, return busy */
  /* This queue full logic may only works for bc == 1 ( == ) */
  /* ( pi + bc ) % size > ci not fully works for bc > 1 */
  /* To do - support bc > 1 case and wrap around case */
  if (((circularQ->producerIdx + bcCount) % circularQ->numElements) == circularQ->consumerIdx)
  {
    *messagePtr = NULL;
    return AGSA_RC_BUSY;
  }

  /* get memory IOMB buffer address */
  offset = circularQ->producerIdx * circularQ->elementSize;
  /* increment to next bcCount element */
  circularQ->producerIdx = (circularQ->producerIdx + bcCount) % circularQ->numElements;

  /* Adds that distance to the base of the region virtual address plus the message header size*/
  msgHeader = (mpiMsgHeader_t*) (((bit8 *)(circularQ->memoryRegion.virtPtr)) + offset);

  SA_DBG3(("mpiMsgFreeGet: msgHeader = %p Offset = 0x%x\n", (void *)msgHeader, offset));

  /* Sets the message buffer in "allocated" state */
  /* bc always is 1 for inbound queue */
  /* temporarily store it in the native endian format, when the rest of the */
  /* header is filled, this would be converted to Little Endian */
  msgHeader->Header = (1<<24);
  *messagePtr = ((bit8*)msgHeader) + sizeof(mpiMsgHeader_t);

  return AGSA_RC_SUCCESS;
}
#endif

/*******************************************************************************/
/** \fn mpiMsgProduce(mpiICQueue_t *circularQ, void *messagePtr, mpiMsgCategory_t category, bit16 opCode, bit8 responseQueue)
 *  \brief Add a header of IOMB then send to a inbound queue and update the Producer index
 *  \param circularQ     Pointer to an inbound queue
 *  \param messagePtr    Pointer to the message buffer payload (not including message header))
 *  \param category      Message category (ETHERNET, FC, SAS-SATA, SCSI)
 *  \param opCode        Message operation code
 *  \param responseQueue If the message requires response, this paramater indicates the outbound queue for the response
 *
 * This function is used to sumit a message buffer, previously obtained from  mpiMsgFreeGet()
 * function call, to the given Inbound queue
 *
 * Return:
 *         AGSA_RC_SUCCESS if the message has been posted succesfully
 */
/*******************************************************************************/
#ifdef FAST_IO_TEST
GLOBAL bit32 mpiMsgPrepare(
                       mpiICQueue_t *circularQ,
                       void         *messagePtr,
                       mpiMsgCategory_t category,
                       bit16        opCode,
                       bit8         responseQueue,
                       bit8         hiPriority
                       )
{
  mpiMsgHeader_t *msgHeader;
  bit32          bc;
  bit32          Header = 0;
  bit32          hpriority = 0;

  SA_DBG4(("Entering function:mpiMsgProduce\n"));
  SA_ASSERT(NULL != circularQ, "circularQ argument cannot be null");
  SA_ASSERT(NULL != messagePtr, "messagePtr argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue"
            " is 0");
  SA_ASSERT(MPI_MAX_OUTBOUND_QUEUES > responseQueue, "oQueue ID is wrong");

  /* Obtains the address of the entire message buffer, including the header */
  msgHeader = (mpiMsgHeader_t*)(((bit8*)messagePtr) - sizeof(mpiMsgHeader_t));
  /* Read the BC from header, its stored in native endian format when message
     was allocated */
  /* intially */
  bc = (((msgHeader->Header) >> SHIFT24) & BC_MASK);
  SA_DBG6(("mpiMsgProduce: msgHeader bc %d\n", bc));
  if (circularQ->priority)
    hpriority = 1;

  /* Checks the message is in "allocated" state */
  SA_ASSERT(0 != bc, "The message buffer is not in \"allocated\" state "
                     "(bc == 0)");

  Header = ((V_BIT << SHIFT31) | (hpriority << SHIFT30)  |
            ((bc & BC_MASK) << SHIFT24) |
            ((responseQueue & OBID_MASK) << SHIFT16) |
            ((category  & CAT_MASK) << SHIFT12 ) | (opCode & OPCODE_MASK));

  /* pre flush the IOMB cache line */
  ossaCachePreFlush(circularQ->agRoot,
                    (void *)circularQ->memoryRegion.appHandle,
                    (void *)msgHeader, circularQ->elementSize * bc);
  OSSA_WRITE_LE_32(circularQ->agRoot, msgHeader, OSSA_OFFSET_OF(mpiMsgHeader_t,
                   Header), Header);
  /* flush the IOMB cache line */
  ossaCacheFlush(circularQ->agRoot, (void *)circularQ->memoryRegion.appHandle,
                 (void *)msgHeader, circularQ->elementSize * bc);

  MPI_DEBUG_TRACE( circularQ->qNumber,
                  ((circularQ->producerIdx << 16 ) | circularQ->consumerIdx),
                   MPI_DEBUG_TRACE_IBQ,
                  (void *)msgHeader,
                  circularQ->elementSize);

  ossaLogIomb(circularQ->agRoot,
              circularQ->qNumber,
              TRUE,
              (void *)msgHeader,
              circularQ->elementSize);

  return AGSA_RC_SUCCESS;
} /* mpiMsgPrepare */

GLOBAL bit32 mpiMsgProduce(
                       mpiICQueue_t *circularQ,
                       void         *messagePtr,
                       mpiMsgCategory_t category,
                       bit16        opCode,
                       bit8         responseQueue,
                       bit8         hiPriority
                       )
{
  bit32 ret;

  ret = mpiMsgPrepare(circularQ, messagePtr, category, opCode, responseQueue,
                      hiPriority);
  if (ret == AGSA_RC_SUCCESS)
  {
    /* update PI of inbound queue */
    ossaHwRegWriteExt(circularQ->agRoot,
                      circularQ->PIPCIBar,
                      circularQ->PIPCIOffset,
                      circularQ->producerIdx);
  }
  return ret;
}

GLOBAL void mpiIBQMsgSend(mpiICQueue_t *circularQ)
{
  ossaHwRegWriteExt(circularQ->agRoot,
                    circularQ->PIPCIBar,
                    circularQ->PIPCIOffset,
                    circularQ->producerIdx);
}
#else  /* FAST_IO_TEST */

GLOBAL FORCEINLINE
bit32
mpiMsgProduce(
  mpiICQueue_t *circularQ,
  void *messagePtr,
  mpiMsgCategory_t category,
  bit16 opCode,
  bit8 responseQueue,
  bit8 hiPriority
  )
{
  mpiMsgHeader_t *msgHeader;
  bit32          bc;
  bit32          Header = 0;
  bit32          hpriority = 0;

#ifdef SA_FW_TEST_BUNCH_STARTS
#define Need_agRootDefined 1
#endif /* SA_FW_TEST_BUNCH_STARTS */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
  bit32             i;
#define Need_agRootDefined 1
#endif /* SA_ENABLE_TRACE_FUNCTIONS */

#ifdef MPI_DEBUG_TRACE_ENABLE
#define Need_agRootDefined 1
#endif /* MPI_DEBUG_TRACE_ENABLE */

#ifdef Need_agRootDefined
  agsaRoot_t   *agRoot=circularQ->agRoot;
#ifdef SA_FW_TEST_BUNCH_STARTS
   agsaLLRoot_t *saRoot = agNULL;
  saRoot = agRoot->sdkData;
#endif /* SA_FW_TEST_BUNCH_STARTS */

#undef Need_agRootDefined
#endif /* Need_agRootDefined */

  SA_DBG4(("Entering function:mpiMsgProduce\n"));
  SA_ASSERT(NULL != circularQ, "circularQ argument cannot be null");
  SA_ASSERT(NULL != messagePtr, "messagePtr argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue is 0");
  SA_ASSERT(MPI_MAX_OUTBOUND_QUEUES > responseQueue, "oQueue ID is wrong");

  /* REB Start extra trace */
  smTraceFuncEnter(hpDBG_VERY_LOUD,"22");
  /* REB End extra trace */

  /* Obtains the address of the entire message buffer, including the header */
  msgHeader = (mpiMsgHeader_t*)(((bit8*)messagePtr) - sizeof(mpiMsgHeader_t));
  /* Read the BC from header, its stored in native endian format when message was allocated */
  /* intially */
  bc = (((msgHeader->Header) >> SHIFT24) & BC_MASK);
  SA_DBG6(("mpiMsgProduce: msgHeader bc %d\n", bc));
  if (circularQ->priority)
  {
    hpriority = 1;
  }

  /* Checks the message is in "allocated" state */
  SA_ASSERT(0 != bc, "The message buffer is not in \"allocated\" state (bc == 0)");

  Header = ((V_BIT << SHIFT31) |
            (hpriority << SHIFT30)  |
            ((bc & BC_MASK) << SHIFT24) |
            ((responseQueue & OBID_MASK) << SHIFT16) |
            ((category  & CAT_MASK) << SHIFT12 ) |
            (opCode & OPCODE_MASK));

  /* pre flush the cache line */
  ossaCachePreFlush(circularQ->agRoot, (void *)circularQ->memoryRegion.appHandle, (void *)msgHeader, circularQ->elementSize * bc);
  OSSA_WRITE_LE_32(circularQ->agRoot, msgHeader, OSSA_OFFSET_OF(mpiMsgHeader_t, Header), Header);
  /* flush the cache line for IOMB */
  ossaCacheFlush(circularQ->agRoot, (void *)circularQ->memoryRegion.appHandle, (void *)msgHeader, circularQ->elementSize * bc);

  MPI_DEBUG_TRACE( circularQ->qNumber,
                  ((circularQ->producerIdx << 16 ) | circularQ->consumerIdx),
                  MPI_DEBUG_TRACE_IBQ,
                  (void *)msgHeader,
                  circularQ->elementSize);

  ossaLogIomb(circularQ->agRoot,
              circularQ->qNumber,
              TRUE,
              (void *)msgHeader,
              circularQ->elementSize);

#if defined(SALLSDK_DEBUG)
  MPI_IBQ_IOMB_LOG(circularQ->qNumber, (void *)msgHeader, circularQ->elementSize);
#endif  /* SALLSDK_DEBUG */
  /* REB Start extra trace */
#ifdef SA_ENABLE_TRACE_FUNCTIONS
  smTrace(hpDBG_IOMB,"M1",circularQ->qNumber);
 /* TP:M1 circularQ->qNumber */
  for (i=0; i<((bit32)bc*(circularQ->elementSize/4)); i++)
  {
      /* The -sizeof(mpiMsgHeader_t) is to account for mpiMsgProduce adding the header to the pMessage pointer */
      smTrace(hpDBG_IOMB,"MD",*( ((bit32 *)((bit8 *)messagePtr - sizeof(mpiMsgHeader_t))) + i));
      /* TP:MD Inbound IOMB Dword */
  }
#endif /* SA_ENABLE_TRACE_FUNCTIONS */

  /* update PI of inbound queue */

#ifdef SA_FW_TEST_BUNCH_STARTS
  if(saRoot->BunchStarts_Enable)
  {
      if (circularQ->BunchStarts_QPending == 0)
      {
          // store tick value for 1st deferred IO only 
          circularQ->BunchStarts_QPendingTick = saRoot->timeTick;
      }
      // update queue's pending count
      circularQ->BunchStarts_QPending++;

      // update global pending count
      saRoot->BunchStarts_Pending++;

      SA_DBG1(("mpiMsgProduce: BunchStarts - Global Pending %d\n", saRoot->BunchStarts_Pending));
      SA_DBG1(("mpiMsgProduce: BunchStarts - QPending %d, Q-%d\n", circularQ->BunchStarts_QPending, circularQ->qNumber));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "22");

      return AGSA_RC_SUCCESS;
  }

  saRoot->BunchStarts_Pending     = 0;
  circularQ->BunchStarts_QPending = 0;
#endif /* SA_FW_TEST_BUNCH_STARTS */
  ossaHwRegWriteExt(circularQ->agRoot,
                    circularQ->PIPCIBar,
                    circularQ->PIPCIOffset,
                    circularQ->producerIdx);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "22");

  return AGSA_RC_SUCCESS;
} /* mpiMsgProduce */
#endif /* FAST_IO_TEST */

#ifdef SA_FW_TEST_BUNCH_STARTS

void mpiMsgProduceBunch(  agsaLLRoot_t  *saRoot)
{
  mpiICQueue_t *circularQ;
  bit32 inq;

  for(inq=0; ((inq < saRoot->QueueConfig.numInboundQueues) && saRoot->BunchStarts_Pending); inq++)
  {
    circularQ= &saRoot->inboundQueue[inq];
    /* If any pending IOs present then either process if BunchStarts_Threshold
     * IO limit reached or if the timer has popped
     */
    if (circularQ->BunchStarts_QPending &&
        ((circularQ->BunchStarts_QPending >= saRoot->BunchStarts_Threshold) || 
         ((saRoot->timeTick - circularQ->BunchStarts_QPendingTick) >= saRoot->BunchStarts_TimeoutTicks))
       )
    {
      if(circularQ->qNumber != inq)
      {
        SA_DBG1(("mpiMsgProduceBunch:circularQ->qNumber(%d) != inq(%d)\n",circularQ->qNumber, inq));
      }

      SA_DBG1(("mpiMsgProduceBunch: IQ=%d, PI=%d\n", inq, circularQ->producerIdx));
      SA_DBG1(("mpiMsgProduceBunch: Qpending=%d, TotPending=%d\n", circularQ->BunchStarts_QPending, saRoot->BunchStarts_Pending));

      ossaHwRegWriteExt(circularQ->agRoot,
                     circularQ->PIPCIBar,
                     circularQ->PIPCIOffset,
                     circularQ->producerIdx);

      // update global pending count
      saRoot->BunchStarts_Pending -= circularQ->BunchStarts_QPending;

      // clear current queue's pending count after processing
      circularQ->BunchStarts_QPending = 0;
      circularQ->BunchStarts_QPendingTick = saRoot->timeTick;
    }
  }
}
#endif /* SA_FW_TEST_BUNCH_STARTS */

/*******************************************************************************/
/** \fn mpiMsgConsume(mpiOCQueue_t *circularQ, void *messagePtr1,
 *                mpiMsgCategory_t * pCategory, bit16 * pOpCode, bit8 * pBC)
 *  \brief Get a received message
 *  \param circularQ   Pointer to a outbound queue
 *  \param messagePtr1 Pointer to the returned message buffer or NULL if no valid message
 *  \param pCategory   Pointer to Message category (ETHERNET, FC, SAS-SATA, SCSI)
 *  \param pOpCode     Pointer to Message operation code
 *  \param pBC         Pointer to buffer count
 *
 * Consume a receive message in the specified outbound queue
 *
 * Return:
 *         AGSA_RC_SUCCESS if the message has been retrieved succesfully
 *         AGSA_RC_BUSY    if the circular is empty
 */
/*******************************************************************************/
GLOBAL FORCEINLINE
bit32
mpiMsgConsume(
  mpiOCQueue_t       *circularQ,
  void             ** messagePtr1,
  mpiMsgCategory_t   *pCategory,
  bit16              *pOpCode,
  bit8               *pBC
  )
{
  mpiMsgHeader_t *msgHeader;
  bit32          msgHeader_tmp;

  SA_ASSERT(NULL != circularQ, "circularQ argument cannot be null");
  SA_ASSERT(NULL != messagePtr1, "messagePtr1 argument cannot be null");
  SA_ASSERT(NULL != pCategory, "pCategory argument cannot be null");
  SA_ASSERT(NULL != pOpCode, "pOpCode argument cannot be null");
  SA_ASSERT(NULL != pBC, "pBC argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue is 0");

  do
  {
    /* If there are not-yet-delivered messages ... */
    if(circularQ->producerIdx != circularQ->consumerIdx)
    {
      /* Get the pointer to the circular queue buffer element */
      msgHeader = (mpiMsgHeader_t*) ((bit8 *)(circularQ->memoryRegion.virtPtr) + circularQ->consumerIdx * circularQ->elementSize);

#ifdef LOOPBACK_MPI
      if (!loopback)
#endif
      /* invalidate the cache line of IOMB */
      ossaCacheInvalidate(circularQ->agRoot, (void *)circularQ->memoryRegion.appHandle, (void *)msgHeader, circularQ->elementSize);


      /* read header */
      OSSA_READ_LE_32(circularQ->agRoot, &msgHeader_tmp, msgHeader, 0);

      SA_DBG4(("mpiMsgConsume: process an IOMB, header=0x%x\n", msgHeader_tmp));

      SA_ASSERT(0 != (msgHeader_tmp & HEADER_BC_MASK), "The bc field in the header is 0");
#ifdef TEST
      /* for debugging */
      if (0 == (msgHeader_tmp & HEADER_BC_MASK))
      {
        SA_DBG1(("mpiMsgConsume: CI=%d PI=%d msgHeader=%p\n", circularQ->consumerIdx, circularQ->producerIdx, (void *)msgHeader));
        circularQ->consumerIdx = (circularQ->consumerIdx + 1) % circularQ->numElements;
        /* update the CI of outbound queue - skip this blank IOMB, for test only */
        ossaHwRegWriteExt(circularQ->agRoot,
                          circularQ->CIPCIBar,
                          circularQ->CIPCIOffset,
                          circularQ->consumerIdx);
        return AGSA_RC_FAILURE;
      }
#endif
      /* get message pointer of valid entry */
      if (0 != (msgHeader_tmp & HEADER_V_MASK))
      {
        SA_ASSERT(circularQ->consumerIdx <= circularQ->numElements, "Multi-buffer messages cannot wrap around");

        if (OPC_OUB_SKIP_ENTRY != (msgHeader_tmp & OPCODE_MASK))
        {
          /* ... return the message payload */
          *messagePtr1 = ((bit8*)msgHeader) + sizeof(mpiMsgHeader_t);
          *pCategory   = (mpiMsgCategory_t)(msgHeader_tmp >> SHIFT12) & CAT_MASK;
          *pOpCode     = (bit16)(msgHeader_tmp & OPCODE_MASK);
          *pBC         = (bit8)((msgHeader_tmp >> SHIFT24) & BC_MASK);

          /* invalidate the cache line for IOMB */
#ifdef LOOPBACK_MPI
          if (!loopback)
#endif
            ossaCacheInvalidate(circularQ->agRoot, (void *)circularQ->memoryRegion.appHandle, (void *)msgHeader, (*pBC - 1) * circularQ->elementSize);

#if defined(SALLSDK_DEBUG)
          SA_DBG3(("mpiMsgConsume: CI=%d PI=%d msgHeader=%p\n", circularQ->consumerIdx, circularQ->producerIdx, (void *)msgHeader));
          MPI_OBQ_IOMB_LOG(circularQ->qNumber, (void *)msgHeader, circularQ->elementSize);
#endif
          return AGSA_RC_SUCCESS;
        }
        else
        {
          SA_DBG3(("mpiMsgConsume: SKIP_ENTRIES_IOMB BC=%d\n", (msgHeader_tmp >> SHIFT24) & BC_MASK));
          /* Updated comsumerIdx and skip it */
          circularQ->consumerIdx = (circularQ->consumerIdx + ((msgHeader_tmp >> SHIFT24) & BC_MASK)) % circularQ->numElements;
          /* clean header to 0 */
          msgHeader_tmp = 0;
          /*ossaSingleThreadedEnter(agRoot, LL_IOREQ_OBQ_LOCK);*/

          OSSA_WRITE_LE_32(circularQ->agRoot, msgHeader, OSSA_OFFSET_OF(mpiMsgHeader_t, Header), msgHeader_tmp);

          /* update the CI of outbound queue */
          ossaHwRegWriteExt(circularQ->agRoot,
                            circularQ->CIPCIBar,
                            circularQ->CIPCIOffset,
                            circularQ->consumerIdx);
          /* Update the producer index */
          OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
          /*ossaSingleThreadedLeave(agRoot, LL_IOREQ_OBQ_LOCK); */
        }
      }
      else
      {
        /* V bit is not set */
#if defined(SALLSDK_DEBUG)
        agsaRoot_t *agRoot=circularQ->agRoot;
        SA_DBG1(("mpiMsgConsume: V bit not set, PI=%d CI=%d msgHeader=%p\n",  circularQ->producerIdx, circularQ->consumerIdx,(void *)msgHeader));
        SA_DBG1(("mpiMsgConsume: V bit not set, 0x%08X Q=%d  \n", msgHeader_tmp, circularQ->qNumber));

        MPI_DEBUG_TRACE(MPI_DEBUG_TRACE_QNUM_ERROR + circularQ->qNumber,
                        ((circularQ->producerIdx << 16 ) | circularQ->consumerIdx),
                          MPI_DEBUG_TRACE_OBQ,
                         (void *)(((bit8*)msgHeader) - sizeof(mpiMsgHeader_t)),
                          circularQ->elementSize);

        circularQ->consumerIdx = circularQ->consumerIdx % circularQ->numElements;
        circularQ->consumerIdx ++;
        OSSA_WRITE_LE_32(circularQ->agRoot, msgHeader, OSSA_OFFSET_OF(mpiMsgHeader_t, Header), msgHeader_tmp);
        ossaHwRegWriteExt(agRoot,
                          circularQ->CIPCIBar,
                          circularQ->CIPCIOffset,
                          circularQ->consumerIdx);
        MPI_OBQ_IOMB_LOG(circularQ->qNumber, (void *)msgHeader, circularQ->elementSize);
#endif
        SA_DBG1(("mpiMsgConsume: V bit is not set!!!!! HW CI=%d\n", ossaHwRegReadExt(circularQ->agRoot, circularQ->CIPCIBar, circularQ->CIPCIOffset) ));
        SA_ASSERT(0, "V bit is not set");
        return AGSA_RC_FAILURE;
      }
    }
    else
    {
      /* Update the producer index from SPC */
      OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
    }
  } while(circularQ->producerIdx != circularQ->consumerIdx); /* while we don't have any more not-yet-delivered message */

#ifdef TEST
  SA_DBG4(("mpiMsgConsume: Outbound queue is empty.\n"));
#endif

  /* report empty */
  return AGSA_RC_BUSY;
}

/*******************************************************************************/
/** \fn mpiMsgFreeSet(mpiOCQueue_t *circularQ, void *messagePtr)
 *  \brief Returns a received message to the outbound queue
 *  \param circularQ   Pointer to an outbound queue
 *  \param messagePtr1 Pointer to the returned message buffer to free
 *  \param messagePtr2 Pointer to the returned message buffer to free if bc > 1
 *
 * Returns consumed and processed message to the specified outbounf queue
 *
 * Return:
 *         AGSA_RC_SUCCESS if the message has been returned succesfully
 */
/*******************************************************************************/
GLOBAL FORCEINLINE
bit32
mpiMsgFreeSet(
  mpiOCQueue_t *circularQ,
  void *messagePtr1,
  bit8 bc
  )
{
  mpiMsgHeader_t     *msgHeader;

  SA_DBG4(("Entering function:mpiMsgFreeSet\n"));
  SA_ASSERT(NULL != circularQ, "circularQ argument cannot be null");
  SA_ASSERT(NULL != messagePtr1, "messagePtr1 argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue is 0");

  /* Obtains the address of the entire message buffer, including the header */
  msgHeader = (mpiMsgHeader_t*)(((bit8*)messagePtr1) - sizeof(mpiMsgHeader_t));

  if ( ((mpiMsgHeader_t*)((bit8*)circularQ->memoryRegion.virtPtr + circularQ->consumerIdx * circularQ->elementSize)) != msgHeader)
  {
    /* IOMB of CI points mismatch with Message Header - should never happened */
    SA_DBG1(("mpiMsgFreeSet: Wrong CI, Q %d ConsumeIdx = %d msgHeader 0x%08x\n",circularQ->qNumber, circularQ->consumerIdx ,msgHeader->Header));
    SA_DBG1(("mpiMsgFreeSet: msgHeader %p != %p\n", msgHeader,((mpiMsgHeader_t*)((bit8*)circularQ->memoryRegion.virtPtr + circularQ->consumerIdx * circularQ->elementSize))));

#ifdef LOOPBACK_MPI
    if (!loopback)
#endif
    /* Update the producer index from SPC */
    OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
#if defined(SALLSDK_DEBUG)
    SA_DBG3(("mpiMsgFreeSet: ProducerIdx = %d\n", circularQ->producerIdx));
#endif
    return AGSA_RC_SUCCESS;
  }

  /* ... free the circular queue buffer elements associated with the message ... */
  /*... by incrementing the consumer index (with wrap arround) */
  circularQ->consumerIdx = (circularQ->consumerIdx + bc) % circularQ->numElements;

  /* Invalidates this circular queue buffer element */

  msgHeader->Header &= ~HEADER_V_MASK; /* Clear Valid bit to indicate IOMB consumed by host */
  SA_ASSERT(circularQ->consumerIdx <= circularQ->numElements, "Multi-buffer messages cannot wrap arround");

  /* update the CI of outbound queue */
#ifdef LOOPBACK_MPI
  if (!loopback)
#endif
  {
  ossaHwRegWriteExt(circularQ->agRoot,
                    circularQ->CIPCIBar,
                    circularQ->CIPCIOffset,
                    circularQ->consumerIdx);

  /* Update the producer index from SPC */
  OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
  }
#if defined(SALLSDK_DEBUG)
  SA_DBG5(("mpiMsgFreeSet: CI=%d PI=%d\n", circularQ->consumerIdx, circularQ->producerIdx));
#endif
  return AGSA_RC_SUCCESS;
}

#ifdef TEST
GLOBAL bit32 mpiRotateQnumber(agsaRoot_t *agRoot)
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  bit32        denom;
  bit32        ret = 0;

  /* inbound queue number */
  saRoot->IBQnumber++;
  denom = saRoot->QueueConfig.numInboundQueues;
  if (saRoot->IBQnumber % denom == 0) /* % Qnumber*/
  {
    saRoot->IBQnumber = 0;
  }
  SA_DBG3(("mpiRotateQnumber: IBQnumber %d\n", saRoot->IBQnumber));

  /* outbound queue number */
  saRoot->OBQnumber++;
  denom = saRoot->QueueConfig.numOutboundQueues;
  if (saRoot->OBQnumber % denom == 0) /* % Qnumber*/
  {
    saRoot->OBQnumber = 0;
  }
  SA_DBG3(("mpiRotateQnumber: OBQnumber %d\n", saRoot->OBQnumber));

  ret = (saRoot->OBQnumber << SHIFT16) | saRoot->IBQnumber;
  return ret;
}
#endif

#ifdef LOOPBACK_MPI
GLOBAL bit32 mpiMsgProduceOQ(
                       mpiOCQueue_t *circularQ,
                       void         *messagePtr,
                       mpiMsgCategory_t category,
                       bit16        opCode,
                       bit8         responseQueue,
                       bit8         hiPriority
                       )
{
  mpiMsgHeader_t *msgHeader;
  bit32          bc;
  bit32          Header = 0;
  bit32          hpriority = 0;

  SA_DBG4(("Entering function:mpiMsgProduceOQ\n"));
  SA_ASSERT(NULL != circularQ, "circularQ argument cannot be null");
  SA_ASSERT(NULL != messagePtr, "messagePtr argument cannot be null");
  SA_ASSERT(0 != circularQ->numElements, "The number of elements in this queue"
            " is 0");
  SA_ASSERT(MPI_MAX_OUTBOUND_QUEUES > responseQueue, "oQueue ID is wrong");

  /* REB Start extra trace */
  smTraceFuncEnter(hpDBG_VERY_LOUD, "2I");
  /* REB End extra trace */

  /* Obtains the address of the entire message buffer, including the header */
  msgHeader = (mpiMsgHeader_t*)(((bit8*)messagePtr) - sizeof(mpiMsgHeader_t));
  /* Read the BC from header, its stored in native endian format when message
     was allocated */
  /* intially */
  SA_DBG4(("mpiMsgProduceOQ: msgHeader %p opcode %d pi/ci %d / %d\n", msgHeader, opCode, circularQ->producerIdx, circularQ->consumerIdx));
  bc = (((msgHeader->Header) >> SHIFT24) & BC_MASK);
  SA_DBG6(("mpiMsgProduceOQ: msgHeader bc %d\n", bc));
  if (circularQ->priority)
    hpriority = 1;

  /* Checks the message is in "allocated" state */
  SA_ASSERT(0 != bc, "The message buffer is not in \"allocated\" state "
                     "(bc == 0)");

  Header = ((V_BIT << SHIFT31) | (hpriority << SHIFT30)  |
            ((bc & BC_MASK) << SHIFT24) |
            ((responseQueue & OBID_MASK) << SHIFT16) |
            ((category  & CAT_MASK) << SHIFT12 ) | (opCode & OPCODE_MASK));
  /* pre flush the IOMB cache line */
  //ossaCachePreFlush(circularQ->agRoot,
  //                  (void *)circularQ->memoryRegion.appHandle,
  //                  (void *)msgHeader, circularQ->elementSize * bc);
  OSSA_WRITE_LE_32(circularQ->agRoot, msgHeader, OSSA_OFFSET_OF(mpiMsgHeader_t,
                   Header), Header);

  /* flush the IOMB cache line */
  //ossaCacheFlush(circularQ->agRoot, (void *)circularQ->memoryRegion.appHandle,
  //               (void *)msgHeader, circularQ->elementSize * bc);

  MPI_DEBUG_TRACE( circularQ->qNumber,
                 ((circularQ->producerIdx << 16 ) | circularQ->consumerIdx),
                  MPI_DEBUG_TRACE_OBQ,
                  (void *)msgHeader,
                  circularQ->elementSize);

  ossaLogIomb(circularQ->agRoot,
              circularQ->qNumber,
              TRUE,
              (void *)msgHeader,
              circularQ->elementSize);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2I");
  return AGSA_RC_SUCCESS;
} /* mpiMsgProduceOQ */
#endif

