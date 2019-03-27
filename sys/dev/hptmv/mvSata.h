/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 MARVELL SEMICONDUCTOR ISRAEL, LTD.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __INCmvSatah
#define __INCmvSatah

#ifndef SUPPORT_MV_SATA_GEN_1
#define SUPPORT_MV_SATA_GEN_1 1
#endif

#ifndef SUPPORT_MV_SATA_GEN_2
#define SUPPORT_MV_SATA_GEN_2 0
#endif

#ifndef SUPPORT_MV_SATA_GEN_2E
#define SUPPORT_MV_SATA_GEN_2E 0
#endif

#if (SUPPORT_MV_SATA_GEN_1 + SUPPORT_MV_SATA_GEN_2 + SUPPORT_MV_SATA_GEN_2E) > 1

#define MV_SATA_GEN_1(x) ((x)->sataAdapterGeneration==1)
#define MV_SATA_GEN_2(x) ((x)->sataAdapterGeneration>=2)
#define MV_SATA_GEN_2E(x) ((x)->sataAdapterGeneration==3)

#elif SUPPORT_MV_SATA_GEN_1==1

#define MV_SATA_GEN_1(x) 1
#define MV_SATA_GEN_2(x) 0
#define MV_SATA_GEN_2E(x) 0

#elif SUPPORT_MV_SATA_GEN_2==1

#define MV_SATA_GEN_1(x) 0
#define MV_SATA_GEN_2(x) 1
#define MV_SATA_GEN_2E(x) 0

#elif SUPPORT_MV_SATA_GEN_2E==1

#define MV_SATA_GEN_1(x)  0
#define MV_SATA_GEN_2(x)  1 /* gen2E impiles gen2 */
#define MV_SATA_GEN_2E(x) 1

#else 
#error "Which IC do you support?"
#endif

/* Definitions */
/* MV88SX50XX specific defines */
#define MV_SATA_VENDOR_ID		   				0x11AB
#define MV_SATA_DEVICE_ID_5080			   		0x5080
#define MV_SATA_DEVICE_ID_5081			   		0x5081
#define MV_SATA_DEVICE_ID_6080			   		0x6080
#define MV_SATA_DEVICE_ID_6081			   		0x6081

#if defined(RR2310) || defined(RR1740) || defined(RR2210) || defined (RR2522)
#define MV_SATA_CHANNELS_NUM					4
#define MV_SATA_UNITS_NUM						1
#else 
#define MV_SATA_CHANNELS_NUM					8
#define MV_SATA_UNITS_NUM						2
#endif

#define MV_SATA_PCI_BAR0_SPACE_SIZE				(1<<18) /* 256 Kb*/

#define CHANNEL_QUEUE_LENGTH					32
#define CHANNEL_QUEUE_MASK					    0x1F

#define MV_EDMA_QUEUE_LENGTH					32	/* Up to 32 outstanding	 */
                        							/* commands per SATA channel*/
#define MV_EDMA_QUEUE_MASK                      0x1F
#define MV_EDMA_REQUEST_QUEUE_SIZE				1024 /* 32*32 = 1KBytes */
#define MV_EDMA_RESPONSE_QUEUE_SIZE				256  /* 32*8 = 256 Bytes */

#define MV_EDMA_REQUEST_ENTRY_SIZE				32
#define MV_EDMA_RESPONSE_ENTRY_SIZE				8

#define MV_EDMA_PRD_ENTRY_SIZE					16		/* 16Bytes*/
#define MV_EDMA_PRD_NO_SNOOP_FLAG				0x00000001 /* MV_BIT0 */
#define MV_EDMA_PRD_EOT_FLAG					0x00008000 /* MV_BIT15 */

#define MV_ATA_IDENTIFY_DEV_DATA_LENGTH  		256	/* number of words(2 byte)*/
#define MV_ATA_MODEL_NUMBER_LEN					40
#define ATA_SECTOR_SIZE							512
/* Log messages level defines */
#define MV_DEBUG								0x1
#define MV_DEBUG_INIT							0x2
#define MV_DEBUG_INTERRUPTS						0x4
#define MV_DEBUG_SATA_LINK						0x8
#define MV_DEBUG_UDMA_COMMAND					0x10
#define MV_DEBUG_NON_UDMA_COMMAND				0x20
#define MV_DEBUG_ERROR							0x40


/* Typedefs    */
typedef enum mvUdmaType  
{
	MV_UDMA_TYPE_READ, MV_UDMA_TYPE_WRITE
} MV_UDMA_TYPE;

typedef enum mvFlushType 
{
	MV_FLUSH_TYPE_CALLBACK, MV_FLUSH_TYPE_NONE 
} MV_FLUSH_TYPE;

typedef enum mvCompletionType  
{
	MV_COMPLETION_TYPE_NORMAL, MV_COMPLETION_TYPE_ERROR,
	MV_COMPLETION_TYPE_ABORT 
} MV_COMPLETION_TYPE;

typedef enum mvEventType 
{
	MV_EVENT_TYPE_ADAPTER_ERROR, MV_EVENT_TYPE_SATA_CABLE 
} MV_EVENT_TYPE;

typedef enum mvEdmaMode 
{
	MV_EDMA_MODE_QUEUED,
	MV_EDMA_MODE_NOT_QUEUED,
	MV_EDMA_MODE_NATIVE_QUEUING
} MV_EDMA_MODE;

typedef enum mvEdmaQueueResult
{
	MV_EDMA_QUEUE_RESULT_OK = 0,
	MV_EDMA_QUEUE_RESULT_EDMA_DISABLED,
	MV_EDMA_QUEUE_RESULT_FULL,
	MV_EDMA_QUEUE_RESULT_BAD_LBA_ADDRESS,
	MV_EDMA_QUEUE_RESULT_BAD_PARAMS
} MV_EDMA_QUEUE_RESULT;

typedef enum mvQueueCommandResult
{
	MV_QUEUE_COMMAND_RESULT_OK = 0,
	MV_QUEUE_COMMAND_RESULT_QUEUED_MODE_DISABLED,
	MV_QUEUE_COMMAND_RESULT_FULL,
	MV_QUEUE_COMMAND_RESULT_BAD_LBA_ADDRESS,
	MV_QUEUE_COMMAND_RESULT_BAD_PARAMS
} MV_QUEUE_COMMAND_RESULT;

typedef enum mvNonUdmaProtocol
{
    MV_NON_UDMA_PROTOCOL_NON_DATA,
    MV_NON_UDMA_PROTOCOL_PIO_DATA_IN,
    MV_NON_UDMA_PROTOCOL_PIO_DATA_OUT
} MV_NON_UDMA_PROTOCOL;


struct mvDmaRequestQueueEntry;
struct mvDmaResponseQueueEntry;
struct mvDmaCommandEntry;

struct mvSataAdapter;
struct mvStorageDevRegisters;

typedef MV_BOOLEAN (* HPTLIBAPI mvSataCommandCompletionCallBack_t)(struct mvSataAdapter *,
														 MV_U8,
                                                         MV_COMPLETION_TYPE,
														 MV_VOID_PTR, MV_U16,
														 MV_U32,
											    struct mvStorageDevRegisters SS_SEG*);

typedef enum mvQueuedCommandType 
{
	MV_QUEUED_COMMAND_TYPE_UDMA,
	MV_QUEUED_COMMAND_TYPE_NONE_UDMA
} MV_QUEUED_COMMAND_TYPE;

typedef struct mvUdmaCommandParams 
{
	MV_UDMA_TYPE readWrite;
	MV_BOOLEAN   isEXT;
	MV_U32       lowLBAAddress;
	MV_U16       highLBAAddress;
	MV_U16       numOfSectors;
	MV_U32       prdLowAddr;
	MV_U32       prdHighAddr;
	mvSataCommandCompletionCallBack_t callBack;
	MV_VOID_PTR  commandId;
} MV_UDMA_COMMAND_PARAMS;

typedef struct mvNoneUdmaCommandParams 
{
  	MV_NON_UDMA_PROTOCOL protocolType;
	MV_BOOLEAN  isEXT;
	MV_U16_PTR	bufPtr;
	MV_U32		count;
	MV_U16		features;
	MV_U16		sectorCount;
	MV_U16		lbaLow;
	MV_U16		lbaMid;
	MV_U16		lbaHigh;
	MV_U8		device;
	MV_U8		command;
    mvSataCommandCompletionCallBack_t callBack;
	MV_VOID_PTR  commandId;
} MV_NONE_UDMA_COMMAND_PARAMS;

typedef struct mvQueueCommandInfo
{
	MV_QUEUED_COMMAND_TYPE	type;
	union
	{
		MV_UDMA_COMMAND_PARAMS		udmaCommand;
		MV_NONE_UDMA_COMMAND_PARAMS	NoneUdmaCommand;
    } commandParams;
} MV_QUEUE_COMMAND_INFO;

/* The following structure is for the Core Driver internal usage */
typedef struct mvQueuedCommandEntry 
{
    MV_BOOLEAN   isFreeEntry;
    MV_U8        commandTag;
	struct mvQueuedCommandEntry	*next;
	struct mvQueuedCommandEntry	*prev;
	MV_QUEUE_COMMAND_INFO	commandInfo;
} MV_QUEUED_COMMAND_ENTRY;

/* The following structures are part of the Core Driver API */
typedef struct mvSataChannel 
{
	/* Fields set by Intermediate Application Layer */
	MV_U8                       channelNumber;
	MV_BOOLEAN                  waitingForInterrupt;
	MV_BOOLEAN                  lba48Address; 
	MV_BOOLEAN                  maxReadTransfer;
	struct mvDmaRequestQueueEntry SS_SEG *requestQueue;
	struct mvDmaResponseQueueEntry SS_SEG *responseQueue;
	MV_U32                      requestQueuePciHiAddress;
	MV_U32                      requestQueuePciLowAddress;
	MV_U32                      responseQueuePciHiAddress;
	MV_U32                      responseQueuePciLowAddress;
	/* Fields set by CORE driver */
	struct mvSataAdapter        *mvSataAdapter;
	MV_OS_SEMAPHORE             semaphore;
	MV_U32                      eDmaRegsOffset;
	MV_U16                      identifyDevice[MV_ATA_IDENTIFY_DEV_DATA_LENGTH];
	MV_BOOLEAN                  EdmaActive;
	MV_EDMA_MODE                queuedDMA;
	MV_U8                       outstandingCommands;
	MV_BOOLEAN					workAroundDone;
	struct mvQueuedCommandEntry	commandsQueue[CHANNEL_QUEUE_LENGTH];
	struct mvQueuedCommandEntry	*commandsQueueHead;
	struct mvQueuedCommandEntry	*commandsQueueTail;
	MV_BOOLEAN					queueCommandsEnabled;
	MV_U8                       noneUdmaOutstandingCommands;
	MV_U8                       EdmaQueuedCommands;
    MV_U32                      freeIDsStack[CHANNEL_QUEUE_LENGTH];
	MV_U32                      freeIDsNum;
	MV_U32                      reqInPtr;
	MV_U32                      rspOutPtr;
} MV_SATA_CHANNEL;

typedef struct mvSataAdapter
{
	/* Fields set by Intermediate Application Layer */
	MV_U32            adapterId;
	MV_U8             pcbVersion;
    MV_U8             pciConfigRevisionId;
    MV_U16            pciConfigDeviceId;
	MV_VOID_PTR		  IALData;
	MV_BUS_ADDR_T     adapterIoBaseAddress;
	MV_U32            intCoalThre[MV_SATA_UNITS_NUM];
	MV_U32            intTimeThre[MV_SATA_UNITS_NUM];
	MV_BOOLEAN        (* HPTLIBAPI mvSataEventNotify)(struct mvSataAdapter *,
										   MV_EVENT_TYPE,
										   MV_U32, MV_U32); 
	MV_SATA_CHANNEL   *sataChannel[MV_SATA_CHANNELS_NUM];
	MV_U32            pciCommand; 
	MV_U32            pciSerrMask;
	MV_U32            pciInterruptMask;

	/* Fields set by CORE driver */
	MV_OS_SEMAPHORE   semaphore;
	MV_U32			  mainMask;	
	MV_OS_SEMAPHORE	  interruptsMaskSem;
    MV_BOOLEAN        implementA0Workarounds;
    MV_BOOLEAN        implement50XXB0Workarounds;
	MV_BOOLEAN        implement50XXB1Workarounds;
	MV_BOOLEAN        implement50XXB2Workarounds;
	MV_BOOLEAN        implement60X1A0Workarounds;
	MV_BOOLEAN        implement60X1A1Workarounds;
	MV_BOOLEAN        implement60X1B0Workarounds;
	MV_BOOLEAN		  implement7042A0Workarounds;
	MV_BOOLEAN		  implement7042A1Workarounds;
	MV_U8			  sataAdapterGeneration;
	MV_BOOLEAN		isPEX;
	MV_U8             failLEDMask;
    MV_U8			  signalAmps[MV_SATA_CHANNELS_NUM];
	MV_U8			  pre[MV_SATA_CHANNELS_NUM];
    MV_BOOLEAN        staggaredSpinup[MV_SATA_CHANNELS_NUM]; /* For 60x1 only */
} MV_SATA_ADAPTER;

typedef struct mvSataAdapterStatus
{
	/* Fields set by CORE driver */
	MV_BOOLEAN		channelConnected[MV_SATA_CHANNELS_NUM];
	MV_U32			pciDLLStatusAndControlRegister;
	MV_U32			pciCommandRegister;
	MV_U32			pciModeRegister;
	MV_U32			pciSERRMaskRegister;
	MV_U32			intCoalThre[MV_SATA_UNITS_NUM];
	MV_U32			intTimeThre[MV_SATA_UNITS_NUM];
	MV_U32			R00StatusBridgePortRegister[MV_SATA_CHANNELS_NUM];
}MV_SATA_ADAPTER_STATUS;


typedef struct mvSataChannelStatus
{
	/* Fields set by CORE driver */
	MV_BOOLEAN		isConnected;
	MV_U8			modelNumber[MV_ATA_MODEL_NUMBER_LEN];
	MV_BOOLEAN		DMAEnabled;
	MV_EDMA_MODE	queuedDMA;
	MV_U8			outstandingCommands;
	MV_U32			EdmaConfigurationRegister;
	MV_U32			EdmaRequestQueueBaseAddressHighRegister;
	MV_U32			EdmaRequestQueueInPointerRegister;
	MV_U32			EdmaRequestQueueOutPointerRegister;
	MV_U32			EdmaResponseQueueBaseAddressHighRegister;
	MV_U32			EdmaResponseQueueInPointerRegister;
	MV_U32			EdmaResponseQueueOutPointerRegister;
	MV_U32			EdmaCommandRegister;
	MV_U32			PHYModeRegister;
}MV_SATA_CHANNEL_STATUS;

/* this structure used by the IAL defines the PRD entries used by the EDMA HW */
typedef struct mvSataEdmaPRDEntry
{
	volatile MV_U32	lowBaseAddr;
	volatile MV_U16	byteCount;
	volatile MV_U16	flags;
	volatile MV_U32 highBaseAddr;
	volatile MV_U32 reserved;
}MV_SATA_EDMA_PRD_ENTRY;

/* API Functions */

/* CORE driver Adapter Management */
MV_BOOLEAN HPTLIBAPI mvSataInitAdapter(MV_SATA_ADAPTER *pAdapter);

MV_BOOLEAN HPTLIBAPI mvSataShutdownAdapter(MV_SATA_ADAPTER *pAdapter);

MV_BOOLEAN HPTLIBAPI mvSataGetAdapterStatus(MV_SATA_ADAPTER *pAdapter,
								  MV_SATA_ADAPTER_STATUS *pAdapterStatus);

MV_U32  HPTLIBAPI mvSataReadReg(MV_SATA_ADAPTER *pAdapter, MV_U32 regOffset);

MV_VOID HPTLIBAPI mvSataWriteReg(MV_SATA_ADAPTER *pAdapter, MV_U32 regOffset,  
					   MV_U32 regValue);

MV_VOID HPTLIBAPI mvEnableAutoFlush(MV_VOID);
MV_VOID HPTLIBAPI mvDisableAutoFlush(MV_VOID);


/* CORE driver SATA Channel Management */
MV_BOOLEAN HPTLIBAPI mvSataConfigureChannel(MV_SATA_ADAPTER *pAdapter,
								  MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataRemoveChannel(MV_SATA_ADAPTER *pAdapter, MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataIsStorageDeviceConnected(MV_SATA_ADAPTER *pAdapter,
										  MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataChannelHardReset(MV_SATA_ADAPTER *pAdapter,
								  MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataConfigEdmaMode(MV_SATA_ADAPTER *pAdapter, MV_U8 channelIndex,
								MV_EDMA_MODE eDmaMode, MV_U8 maxQueueDepth);

MV_BOOLEAN HPTLIBAPI mvSataEnableChannelDma(MV_SATA_ADAPTER *pAdapter,
								  MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataDisableChannelDma(MV_SATA_ADAPTER *pAdapter,
								   MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataFlushDmaQueue(MV_SATA_ADAPTER *pAdapter, MV_U8 channelIndex, 
							   MV_FLUSH_TYPE flushType);

MV_U8 HPTLIBAPI mvSataNumOfDmaCommands(MV_SATA_ADAPTER *pAdapter, MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataSetIntCoalParams (MV_SATA_ADAPTER *pAdapter, MV_U8 sataUnit,
								   MV_U32 intCoalThre, MV_U32 intTimeThre);

MV_BOOLEAN HPTLIBAPI mvSataSetChannelPhyParams(MV_SATA_ADAPTER *pAdapter,
									 MV_U8 channelIndex,
									 MV_U8 signalAmps, MV_U8 pre);

MV_BOOLEAN HPTLIBAPI mvSataChannelPhyShutdown(MV_SATA_ADAPTER *pAdapter,
									MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataChannelPhyPowerOn(MV_SATA_ADAPTER *pAdapter,
									MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvSataChannelSetEdmaLoopBackMode(MV_SATA_ADAPTER *pAdapter,
											MV_U8 channelIndex,
											MV_BOOLEAN loopBackOn);

MV_BOOLEAN HPTLIBAPI mvSataGetChannelStatus(MV_SATA_ADAPTER *pAdapter, MV_U8 channelIndex,
								  MV_SATA_CHANNEL_STATUS *pChannelStatus);

MV_QUEUE_COMMAND_RESULT HPTLIBAPI mvSataQueueCommand(MV_SATA_ADAPTER *pAdapter,
										   MV_U8 channelIndex,
										   MV_QUEUE_COMMAND_INFO SS_SEG *pCommandParams);

/* Interrupt Service Routine */
MV_BOOLEAN HPTLIBAPI mvSataInterruptServiceRoutine(MV_SATA_ADAPTER *pAdapter);

MV_BOOLEAN HPTLIBAPI mvSataMaskAdapterInterrupt(MV_SATA_ADAPTER *pAdapter);

MV_BOOLEAN HPTLIBAPI mvSataUnmaskAdapterInterrupt(MV_SATA_ADAPTER *pAdapter);

/* Command Completion and Event Notification (user implemented) */
MV_BOOLEAN HPTLIBAPI mvSataEventNotify(MV_SATA_ADAPTER *, MV_EVENT_TYPE ,
							 MV_U32, MV_U32);

/*
 * Staggered spin-ip support and SATA interface speed control
 * (relevant for 60x1 adapters)
 */
MV_BOOLEAN HPTLIBAPI mvSataEnableStaggeredSpinUpAll (MV_SATA_ADAPTER *pAdapter);
MV_BOOLEAN HPTLIBAPI mvSataDisableStaggeredSpinUpAll (MV_SATA_ADAPTER *pAdapter);

#endif
