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
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/*! \file satypes.h
 *  \brief The file defines the internal data structure types used by LL layer
 *
 */
/*******************************************************************************/

#ifndef  __SATYPES_H__

#define __SATYPES_H__

/** \brief the callback function of an timer
 *
 * the definition of the timer callback function
 */
typedef bit32 (* agsaCallback_t) (agsaRoot_t *agRoot,
                                  bit32      Event,
                                  void       *Parm);

/** \brief the data structure of a timer
 *
 * use to describe timer
 *
 */
typedef struct agsaTimerDesc_s
{
  SALINK          linkNode; /**< the link node data structure of the timer */
  bit32           valid;  /**< the valid bit of the timer descriptor */
  bit32           timeoutTick; /**< the timeout tick of the timer */
  agsaCallback_t  pfnTimeout; /**< the callback function fo the timer */
  bit32           Event; /**< the event paramter of the timer callback function */
  void *          pParm; /**< the point to the paramter passed to callback function */
} agsaTimerDesc_t;

/** \brief the port
 *
 * describe port data structure
 *
 */
typedef struct agsaPort_s
{
  SALINK              linkNode;     /**< the link node data structure of the port */
  agsaPortContext_t   portContext; /**< the port context of the port */
  SALINK_LIST         listSASATADevices; /**< SAS/SATA devices list of the port */
  bit32               phyMap[AGSA_MAX_VALID_PHYS]; /**< Boolean arrar: the Phys included in the port. */
  bit32               status;  /**< port state */
  bit32               tobedeleted;  /**< mark for deletetion after callback  */
  bit32               portId; /** Port Id from SPC */
  bit8                portIdx; /**< the Index of the port */
  bit8                reserved[3];
} agsaPort_t;

/** \brief the phy
 *
 * phy data structure
 *
 */
typedef struct agsaPhy_s
{
  agsaPort_t          *pPort; /**< pointer to the port includes the phy */
  agsaSASIdentify_t   sasIdentify; /**< the SAS identify of the phy */
  agsaContext_t       *agContext; /**< agContext for the Phy */
  bit32               status; /**< the status of the phy */
  bit8                phyId; /**< the Id of the phy */
  bit8                linkstatus; /**< the link status of the phy */
  bit8                reserved[2];
#if defined(SALLSDK_DEBUG)
  bit8                remoteSignature[8]; /* the remote signature of the phy is the phy is in native SATA mode */
#endif
} agsaPhy_t;

/** \brief the LL defined SAS/SATA device information
 *
 * LL defined SAS/SATA device information
 *
 */
typedef union agsaSASSATADevInfo_s
{
  agsaSASDeviceInfo_t   sasDeviceInfo;  /**< SAS device information of the device */
  agsaSATADeviceInfo_t  sataDeviceInfo; /**< SATA device information of the device */
} agsaSASSATADevInfo_t;

/** \brief the LL defined device descriptor
 *
 * LL defined device descriptor
 *
 */
typedef struct agsaDeviceDesc_s
{
  SALINK                linkNode; /**< the link node data structure of the device */
  agsaDevHandle_t       initiatorDevHandle; /**< the device handle of an initiator device */
  agsaDevHandle_t       targetDevHandle; /**< the device handle of a target device */
  SALINK_LIST           pendingIORequests; /**< the pending IO requests, for SSP or SATA */
  agsaPort_t            *pPort; /**< the port discovered the device */
  bit8                  deviceType; /**< the device type */
  bit8                  reserved[3];
  bit32                 option;
  bit32                 param;
  agsaSASSATADevInfo_t  devInfo; /**< SAS/SATA device information */
  bit32                 DeviceMapIndex;  /**< device index for device handle */
} agsaDeviceDesc_t;

/** \brief the LL defined IO request descriptor
 *
 * LL defined IO Request descriptor
 *
 */
typedef struct agsaIORequestDesc_s
{
  SALINK            linkNode;          /**< the link node data structure of the IO request */
  agsaIORequest_t   *pIORequestContext;/**< the IO request context */
  agsaDeviceDesc_t  *pDevice;          /**< the pointer to the device, to which the request is sent */
  agsaPort_t        *pPort;            /**< the pointer to the port - using by HW_EVENT_ACK with PHY_DOWN event */
  ossaSSPCompletedCB_t completionCB;   /**< completion callback to be called */
  bit32             requestType;       /**< the request type */
  bit16             HwAckType;         /**< Track HW_acks */
  bit16             SOP;               /**< SetPhyProfile page not returned in reply */
  bit32             startTick;         /**< start time for this IO */
  bit32             HTag;              /**< the host tag to index into the IORequest array */
  bit8              valid;             /**< boolean flag: the request is valid */
  bit8              IRmode;            /**< indirect smp response mode */
  bit8              modePageContext;   /**< request is for security mode change */
  bit8              DeviceInfoCmdOption;/**<  */
#ifdef FAST_IO_TEST
  SALINK            fastLink; /* Fast I/O's chain */
#endif
} agsaIORequestDesc_t;

/** \brief the LL defined SMP Response Frame header and payload
 *
 * LL defined SMP Response Frame header and payload
 *
 */
typedef struct agsaSMPRspFrame_s
{
  agsaSMPFrameHeader_t smpHeader;
  bit8                 smpPayload[1020];
} agsaSMPRspFrame_t;

/** \brief the agsaIOMap_t
 *
 * data storage for IO Request Mapping
 *
 */
typedef struct agsaIOMap_s
{
  bit32 Tag;
  agsaIORequestDesc_t *IORequest;
  agsaContext_t *agContext;
} agsaIOMap_t;

/** \brief the agsaPortMap_t
 *
 * data storage for Port Context Mapping
 *
 */
typedef struct agsaPortMap_s
{
  bit32 PortID;
  bit32 PortStatus;
  void  *PortContext;
} agsaPortMap_t;

/** \brief the agsaDeviceMap_t
 *
 * data storage for Device Handle Mapping
 *
 */
typedef struct agsaDeviceMap_s
{
  bit32 DeviceIdFromFW;
  void  *DeviceHandle;
} agsaDeviceMap_t;

#ifdef FAST_IO_TEST
/* interleaved Fast IO's are not allowed */
#define LL_FAST_IO_SIZE  1
#endif

/** \brief the LLRoot
 *
 * root data structure
 *
 */
typedef struct agsaLLRoot_s
{
  agsaMem_t       deviceLinkMem; /**< Device Link System Memory */
  SALINK_LIST     freeDevicesList; /**< List of free IO device handles */

  agsaMem_t       IORequestMem; /**< IO Request Link System Memory */
  SALINK_LIST     freeIORequests; /**< List of free IORequests */
  SALINK_LIST     freeReservedRequests; /**< List of reserved IORequests not for normal IO! */

  agsaMem_t       timerLinkMem; /**< Timer Link System Memory */
  SALINK_LIST     freeTimers; /**< List of free timers */
  SALINK_LIST     validTimers; /**< List of valid timers */

  agsaPhy_t       phys[AGSA_MAX_VALID_PHYS]; /**< Phys */

  agsaPort_t      ports[AGSA_MAX_VALID_PORTS]; /**< Ports */
  SALINK_LIST     freePorts; /**< List of free ports */
  SALINK_LIST     validPorts; /**< List of valid ports */

  bit8            phyCount; /**< number of phys */
  bit8            portCount; /**< number of ports */
  bit8            sysIntsActive; /**< whether interrupt is enabled */
  bit8            reserved; /**< reserved */

  bit32           usecsPerTick; /**< timer tick unit */
  bit32           minStallusecs; /**< shorest available stall */
  bit32           timeTick; /**< the current timer tick */
  bit32           ResetStartTick; /* Reset StartTick */
  bit32           chipStatus; /**< chip status */

  bit32           interruptVecIndexBitMap[MAX_NUM_VECTOR]; /**< Interrupt Vector Index BitMap */
  bit32           interruptVecIndexBitMap1[MAX_NUM_VECTOR]; /**< Interrupt Vector Index BitMap1 */

  agsaBarOffset_t SpcBarOffset[60];
  bit32           ChipId; /* Subversion PCI ID */

  agsaPortMap_t   PortMap[AGSA_MAX_VALID_PORTS]; /**< Port Mapping for PortContext */
  agsaDeviceMap_t DeviceMap[MAX_IO_DEVICE_ENTRIES]; /**< Device Map for Device Handle */
  agsaIOMap_t     IOMap[MAX_ACTIVE_IO_REQUESTS]; /**< IO MAP for IO Request */
  agsaDevHandle_t *DeviceHandle[MAX_IO_DEVICE_ENTRIES]; /**< used for get device handles */
  agsaDevHandle_t *pDeviceHandle; /**< used for get device handles */

  agsaMemoryRequirement_t memoryAllocated; /**< SAS LL memory Allocation */
  agsaHwConfig_t          hwConfig; /**< copy of hwConfig */
  agsaSwConfig_t          swConfig; /**< copy of swConfig */
  agsaQueueConfig_t       QueueConfig; /* copy of MPI IBQ/OBQ configuration */

  mpiConfig_t     mpiConfig; /**< MPI Configuration */
  mpiMemReq_t     mpiMemoryAllocated; /**< MPI memory */
  mpiICQueue_t    inboundQueue[AGSA_MAX_INBOUND_Q];   /**< Outbound queue descriptor array */
  mpiOCQueue_t    outboundQueue[AGSA_MAX_OUTBOUND_Q]; /**< Outbound queue descriptor array */
  mpiHostLLConfigDescriptor_t    mainConfigTable; /**< LL main Configuration Table */

  ossaDeviceRegistrationCB_t     DeviceRegistrationCB; /**< Device Registration CB */
  ossaDeregisterDeviceHandleCB_t DeviceDeregistrationCB;/**< Device DeRegistration CB */

  bit32           numInterruptVectors; /**< Number of Interrupt Vectors configured from OS */
  bit32           Use64bit;            /**< Only write upper bits if needed */

  EnadDisabHandler_t DisableInterrupts;              /*Interrupt type dependant function pointer to disable interrupts */
  EnadDisabHandler_t ReEnableInterrupts;             /*Interrupt type dependant reenable  */
  InterruptOurs_t OurInterrupt;                      /*Interrupt type dependant check for our interrupt */

#ifdef SA_FW_TEST_BUNCH_STARTS
  /**
   * Following variables are needed to handle Bunch Starts (bulk update of PI)
   * - saRoot (agsaLLRoot_t): Global Flags, apply to all queues
   *   1. BunchStarts_Enable 
   *   2. BunchStarts_Threshold
   *   3. BunchStarts_Pending
   *   4. BunchStarts_TimeoutTicks
   *
   * - Circular Q (mpiICQueue_s): Queue specific flags
   *   1. BunchStarts_QPending
   *   2. BunchStarts_QPendingTick
   */
  bit32           BunchStarts_Enable;       // enables/disables whole feature
  bit32           BunchStarts_Threshold;    // global min number of IOs to bunch per queue.
  bit32           BunchStarts_Pending;      // global counter collects all Q->BunchStarts_QPending
  bit32           BunchStarts_TimeoutTicks; // global time out value beyond which bunched IOs will be started even below BunchStarts_Threshold.
#endif /* SA_FW_TEST_BUNCH_STARTS */

#ifdef SA_FW_TIMER_READS_STATUS
  spc_GSTableDescriptor_t  mpiGSTable;
  bit32    MsguTcnt_last;             /**< DW3 - MSGU Tick count */
  bit32    IopTcnt_last;              /**< DW4 - IOP Tick count */
  bit32    Iop1Tcnt_last;              /**< DW4 - IOP Tick count */

#endif /* SA_FW_TIMER_READS_STATUS */

  agsaControllerInfo_t ControllerInfo;
  agsaIOErrorEventStats_t   IoErrorCount;
  agsaIOErrorEventStats_t   IoEventCount;

  bit32           ResetFailed;
  //bit32           FatalDone;
  bit32           ForensicLastOffset;
  //bit32           FatalAccumLen;
  //bit32           NonFatalForensicLastOffset;
  //bit32           FatalCurrentLength;
  bit32           FatalForensicStep;
  bit32           FatalForensicShiftOffset;
  bit32           FatalBarLoc;

#ifdef HIALEAH_ENCRYPTION
  agsaEncryptGeneralPage_t EncGenPage;
#endif /* HIALEAH_ENCRYPTION */
#ifdef SA_ENABLE_TRACE_FUNCTIONS
  bit8  traceBuffLookup[16];

  bit32           TraceDestination;
  bit32           TraceMask;

  bit32           TraceBufferLength;
  bit32           CurrentTraceIndexWrapCount;
  bit32           CurrentTraceIndex;
  bit32           traceLineFeedCnt;
  bit8            *TraceBuffer;
  bit32           TraceBlockReInit;

#endif /*SA_ENABLE_TRACE_FUNCTIONS*/

  bit32           registerDump0[REGISTER_DUMP_BUFF_SIZE/4];  /**< register dump buffer 0 */
  bit32           registerDump1[REGISTER_DUMP_BUFF_SIZE/4];  /**< register dump buffer 1 */

  bit32           autoDeregDeviceflag[AGSA_MAX_VALID_PORTS];

#ifdef SA_FW_TEST_INTERRUPT_REASSERT
  bit32           CheckAll;
  bit32           OldPi[64];
  bit32           OldCi[64];
  bit32           OldFlag[64];
#endif /* SA_FW_TEST_INTERRUPT_REASSERT */


#ifdef SALL_API_TEST
  agsaLLCountInfo_t LLCounters;
#endif
#ifdef FAST_IO_TEST
  void            *freeFastReq[LL_FAST_IO_SIZE]; /* saFastRequest_t* */
  int             freeFastIdx;
#endif
} agsaLLRoot_t;

#ifdef FAST_IO_TEST
/*
  one struct per all prepared Fast IO's;
  freed after all IO's are posted to FW and interrupt is triggered;
  maintained for error rollback or cancel functionality
*/
typedef struct saFastRequest_s
{
  bit32       beforePI[AGSA_MAX_INBOUND_Q];
  bit32       inqList[AGSA_MAX_INBOUND_Q];
  bit32       inqMax;
  SALINK_LIST requests; /* List of all Fast IORequests */
  void        *agRoot;  /* agsaRoot_t * */
  bit8        valid;    /* to avoid usage when the struct is freed */
} saFastRequest_t;
#endif

#endif  /*__SATYPES_H__ */
