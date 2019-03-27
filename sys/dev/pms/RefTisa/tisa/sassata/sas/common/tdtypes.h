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
/** \file
 *
 * The file defines data structures for SAS/SATA TD layer
 *
 */
#ifndef __TDTYPES_H__
#define __TDTYPES_H__

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/src/smtypes.h>
#endif

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/common/tddefs.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdlist.h>
#include <dev/pms/RefTisa/tisa/api/tiscsi.h>


/* function definitions */
typedef void (*tdssSSPReqReceived_t) (
                                      agsaRoot_t *,
                                      agsaDevHandle_t *,
                                      agsaFrameHandle_t,
                                      bit32,
                                      bit32,
                                      bit32
                                      );
typedef void (*tdssSMPReqReceived_t) (
                                      agsaRoot_t            *,
                                      agsaDevHandle_t       *,
                                      agsaSMPFrameHeader_t  *,
                                      agsaFrameHandle_t,
                                      bit32,
                                      bit32
                                      );
                                      
typedef bit32 (*tdssGetSGLChunk_t) (agsaRoot_t      *agRoot,
                                    agsaIORequest_t *agIORequest,
                                    bit32           agChunkOffset,
                                    bit32           *agChunkUpper32,
                                    bit32           *agChunkLower32,
                                    bit32           *agChunkLen);
/* for SSP only */
typedef void (*tdssIOCompleted_t) (agsaRoot_t *,
                                   agsaIORequest_t *,
                                   bit32,
                                   bit32,
                                   agsaFrameHandle_t,
                                   bit32);
/* for SMP only */
typedef void (*tdssSMPCompleted_t) (
                                    agsaRoot_t            *,
                                    agsaIORequest_t       *,
                                    bit32                 ,
                                    bit32                 ,
                                    agsaFrameHandle_t     
                                    );


/** \brief data structure for callback function jumptableESLG page
 *
 * This data structure defines callback fucntions for SSP, SMP and SATA
 * This is used for jump table used for instance specific function callback jump
 *
 */
typedef struct tdsaJumpTable_s {
  /**< function that called to process received SSP frame */
  tdssSSPReqReceived_t pSSPReqReceived;
  /**< function that called to process received SSP frame */
  tdssSMPReqReceived_t pSMPReqReceived;
  /**< SSP IO completion callback function eg) ossaSSPcompleted() */
  tdssIOCompleted_t         pSSPIOCompleted;
  /**< SMP IO completion callback function eg) ossaSMPcompleted() */
  tdssSMPCompleted_t        pSMPCompleted;
  /* callback function for LL getSGL. Simple place holder for now */
  tdssGetSGLChunk_t         pGetSGLChunk;
}  tdsaJumpTable_t;

/* timer functions ; both I and T */
typedef void (*tdsaTimerCBFunc_t)(tiRoot_t *tiRoot, void *timerData1, void *timerData2, void *timerData3);

/** \brief data structure for timer request
 *  Timer requests are enqueued and dequeued using tdList_t
 *  and have a callback function
 */
typedef struct tdsaTimerRequest_s {
  /* the number of ticks */
  bit32             timeout;
  void              *timerData1;
  void              *timerData2;
  void              *timerData3;
  tdsaTimerCBFunc_t timerCBFunc;
  tdList_t          timerLink;
  bit32             timerRunning;
}  tdsaTimerRequest_t;


/** \brief data structure for IO request data
 *  used at target only in ttdtxchg_t structure
 *  just a place holder for now
 */
typedef struct tdssIORequestData_s {
  /* jump table has to be the first */
  tdsaJumpTable_t *pJumpTable;    /* this is just a pointer */
}  tdssIORequestData_t;



/** \brief data structure OS root from the view of lower layer.
 * TD Layer interrupt/non-interrupt context support structure for agsaRoot_t.
 * The osData part of agsaRoot points to this tdsaRootOsData_t structure.
 * In other words, agsaRoot_t->osData points to this structure and used for
 * both SAS and SATA
 */
typedef struct tdsaRootOsData_s {
  tiRoot_t  *tiRoot;            /**< Pointer back to tiRoot                 */
  void      *tdsaAllShared;     /**< Pointer to tdsaContext_t               */
  void      *itdsaIni;           /**< Pointer to SAS/SATA initiator               */
  void      *ttdsaTgt;           /**< Pointer to SAS/SATA target                  */
  /* for sata */
  void      *tdstHost;          /**< Pointer to SATA Host                   */
  void      *tdstDevice;        /**< Pointer to SATA Device                 */
  agBOOLEAN IntContext;         /**< Interrupt context                      */
}  tdsaRootOsData_t;

/** \brief data structure for port/phy related flags
 *  Some fields are just place holders and not used yet
 */
typedef struct tdssPortFlags_s {
  /**< port started flag */
  agBOOLEAN             portStarted;

  /**< port initialized flag */
  agBOOLEAN             portInitialized;

  agBOOLEAN             portReadyForDiscoverySent;

  /**< port stopped by oslayer */
  agBOOLEAN             portStoppedByOSLayer;

  /**< fail portinit/start */
  agBOOLEAN             failPortInit;
  
  agBOOLEAN             pseudoPortInitDone;
  agBOOLEAN             pseudoPortStartDone;  
}  tdssPortFlags_t;

/** \brief data structure for both SAS/SATA related flags
 *  Some fields are just place holders and not used yet
 * 
 */
typedef struct tdsaComMemFlags_s {
  /**< current interrupt setting */
  agBOOLEAN             sysIntsActive;      

  /**< reset in progress */
  agBOOLEAN             resetInProgress;

  /**< reset status */
  agBOOLEAN             resetFailed;

}  tdsaComMemFlags_t;


/* 
 * SAT related structure 
 */
typedef struct satInternalIo_s
{
  tdList_t                    satIntIoLink;
  tiIORequest_t               satIntTiIORequest;
  void                        *satIntRequestBody;
  tiScsiInitiatorRequest_t   satIntTiScsiXchg;
  tiMem_t                     satIntDmaMem;
  tiMem_t                     satIntReqBodyMem;
  bit32                       satIntFlag;
  tiIORequest_t               *satOrgTiIORequest;
  bit32                       id;
} satInternalIo_t;



/*
 * SAT specific structure per SATA drive 
 */
#define SAT_NONNCQ_MAX  1
#define SAT_NCQ_MAX     32
#define SAT_MAX_INT_IO  16

typedef struct TDSASAddressID_s 
{
  bit32   sasAddressLo;     /**< HOST SAS address lower part */
  bit32   sasAddressHi;     /**< HOST SAS address higher part */
  bit8    phyIdentifier;    /**< PHY IDENTIFIER of the PHY */
} TDSASAddressID_t;


struct tdsaExpander_s;


typedef struct tdsaDiscovery_s 
{
  tdList_t                   discoveringExpanderList;
  tdList_t                   UpdiscoveringExpanderList;
  //  tdList_t                   freeExpanderList;
  bit32                   status;
  TDSASAddressID_t        sasAddressIDDiscoverError;
  agsaSATAIdentifyData_t  *pSataIdentifyData;
  struct tdsaExpander_s   *RootExp; /* Root expander of discovery */
  bit32                   NumOfUpExp;
  bit32                   type; /* discovery type: TDSA_DISCOVERY_OPTION_FULL_START 
                                   or TDSA_DISCOVERY_OPTION_INCREMENTAL_START*/
  bit32                   retries;                                   
  bit32                   configureRouteRetries; 
  bit32                   deviceRetistrationRetries; 
  tdsaTimerRequest_t      discoveryTimer;
  tdsaTimerRequest_t      configureRouteTimer;
  tdsaTimerRequest_t      deviceRegistrationTimer;
  tdsaTimerRequest_t      BCTimer; /* Broadcast Change timer for ResetTriggerred */
  smpRespDiscover_t       SMPDiscoverResp;
  bit32                   pendingSMP; /* the number of pending SMP for this discovery */
  bit32                   SeenBC; /* received Broadcast change */
  bit32                   forcedOK; /* report DiscOK when chance is missed */ 
  tdsaTimerRequest_t      SMPBusyTimer; /* SMP retry timer for saSMPStart busy */
  bit32                   SMPRetries; /* number of SMP retries when LL returns busy for saSMPStart*/
  bit32                   ResetTriggerred; /* Hard/Link reset triggerred by discovery */
  tdsaTimerRequest_t      DiscoverySMPTimer; /* discovery-related SMP application Timer */
} tdsaDiscovery_t;


typedef struct 
{
  tdList_t                satIoLinkList;            /* Normal I/O from TISA         */
  tdList_t                satFreeIntIoLinkList;     /* SAT internal I/O free list   */
  tdList_t                satActiveIntIoLinkList;   /* SAT internal I/O active list */
  satInternalIo_t         satIntIo[SAT_MAX_INT_IO]; /* Internal I/O resource        */
  agsaSATAIdentifyData_t  satIdentifyData;          /* Copy of SATA Id Dev data     */
  bit8                    SN_id_limit[25];          /* temporary serial number id info */
  bit32                   satNCQ;                   /* Flag for NCQ support         */ 
  bit32                   sat48BitSupport;          /* Flag for 48-bit addressing   */
  bit32                   satSMARTSelfTest;         /* Flag for SMART self test     */
  bit32                   satSMARTFeatureSet;       /* Flag for SMART feature set   */
  bit32                   satSMARTEnabled;          /* Flag for SMART enabled       */
  bit32                   satRemovableMedia;        /* Flag for Removable Media     */
  bit32                   satRemovableMediaEnabled; /* Flag for Removable Media Enabled */
  bit32                   satDMASupport;            /* Flag for DMA Support         */
  bit32                   satDMAEnabled;            /* Flag for DMA Enabled         */
  bit32                   satDMADIRSupport;         /* Flag in PACKET command for DMA transfer */
  bit32                   satWWNSupport;            /* Flag for DMA Enabled         */
  bit32                   satPendingIO;             /* Number of pending I/O        */
  bit32                   satPendingNCQIO;          /* Number of pending NCQ I/O    */
  bit32                   satPendingNONNCQIO;       /* Number of pending NON NCW I/O*/
  bit32                   satNCQMaxIO;              /* Max NCQ I/O in SAT or drive  */
  bit32                   satDriveState;            /* State of SAT/drive           */
  bit32                   satAbortAfterReset;       /* Flag: abort after SATA reset */
  bit32                   satAbortCalled;           /* Flag: abort called indication*/
  bit32                   satVerifyState;           /* Flag: Read Vrf state for diag*/
  bit32                   satMaxUserAddrSectors;    /* max user addressable setctors*/
  bit32                   satWriteCacheEnabled;     /* Flag for write cache enabled */
  bit32                   satLookAheadEnabled;      /* Flag for look ahead enabled  */
  bit32                   satDeviceFaultState;      /* State of DF                  */
  bit32                   satStopState;             /* State of Start and Stop      */
  bit32                   satFormatState;           /* State of format              */
  bit32                   satPMField;               /* PM field, first 4 bits       */
  bit8                    satSignature[8];          /* Signature                    */
  bit32                   satDeviceType;            /* ATA device type              */
  bit32                   satSectorDone;            /* Number of Sector done by Cmnd*/
  bit32                   freeSATAFDMATagBitmap;    /* SATA NCQ tag bit map         */
  bit32                   IDDeviceValid;            /* ID DeviceData valid bit      */
  bit8                    satMaxLBA[8];             /* MAXLBA is from read capacity */
  bit32                   satBGPendingDiag;         /* Pending Diagnostic in backgound */
  bit32                   NumOfFCA;                 /* number of SMP HARD RESET on this device */   
  bit32                   NumOfIDRetries;           /* number of SMP HARD RESET after ID retries */   
  tiIORequest_t           *satTmTaskTag;            /* TM Task Tag                  */
  void                    *satSaDeviceData;         /* Pointer back to sa dev data  */
  bit32                   ID_Retries;               /* identify device data retries */
  bit32                   IDPending;                /* number of pending identify device data */
} satDeviceData_t;


/** \brief data structure for SAS device list
 *  This structure maintains the device as a list and information about
 *  the device such as the device type and ID address frame.
 *  agsaDeviceHandle_t->osData points to this structure.
 */
typedef struct tdsaDeviceData_s  {
  /* in tdtypes.h */
  tdsaJumpTable_t        *pJumpTable; /**< a pointer to callback function jumptable */
  tiDeviceHandle_t       tiDeviceHandle; 

  tdList_t                FreeLink; /* free dev list */
  tdList_t                MainLink; /* main(in use) dev list */
  tdList_t                IncDisLink; /* Used for incremental Discovery only */
  bit32                   id; /* for debugging only */
  bit32                   InQID; /* Inbound queue ID */
  bit32                   OutQID; /* Outbound queue ID */
  bit8                    DeviceType;
  /* used in tiINIIOStart() */
  agsaRoot_t              *agRoot;
  agsaDevHandle_t         *agDevHandle;
  
  /* for SAS; remote device */
  //  agsaSASDeviceInfo_t     agSASDeviceInfo;
  /* device's sas address */
  TDSASAddressID_t        SASAddressID;
  bit8                    initiator_ssp_stp_smp;
  bit8                    target_ssp_stp_smp;
  bit8                    numOfPhys;
  /* SATA specific data */
  satDeviceData_t         satDevData;

  /**< pointer to tdsaPortcontext which the device belongs to */
  struct tdsaPortContext_s *tdPortContext;
  /* validity of device */
  bit8                    valid;
  bit8                    valid2;
  bit8                    processed; /* used in TD discovery */
#ifdef AGTIAPI_CTL
  bit8                    discovered;
#endif
  agsaDeviceInfo_t        agDeviceInfo;
  agsaContext_t           agContext; /* used in saRegisterNewDevice()*/
  /**< pointer to tdsaExpander if Device is expander */
  struct tdsaExpander_s   *tdExpander;
  struct tdsaDeviceData_s *ExpDevice; /* Expander device which this device is attached to */
  
  bit8                    phyID;      /* PhyID this device is attached to SPC or expander */
  agsaSASIdentify_t     sasIdentify; /* used only in TD discovery */
  bit8                  connectionRate;
  bit8                  registered;
  bit8                  directlyAttached;
  bit8                  SASSpecDeviceType; /* 0 - 3; SAS_NO_DEVICE - SAS_FANOUT_EXPANDER_DEVICE */
  bit32                 IOStart;
  bit32                 IOResponse;
  agsaContext_t         agDeviceResetContext; /* used in saLocalPhyControl() */
  tiIORequest_t         TransportRecoveryIO;
  bit32                 TRflag; /* transport recovery flag; used only for tiINITransportRecovery */
  bit32                 ResetCnt; /* number of reset to the device */  
  tdsaTimerRequest_t    SATAIDDeviceTimer; /* ID Device Data timer for SATA device */
  bit32                 OSAbortAll;
#ifdef FDS_DM
  bit32                 devMCN; /* MCN reported by DM */
  bit32                 finalMCN; /* final MCN using devMCN and local MCN */
#endif
#ifdef FDS_SM
  smDeviceHandle_t      smDeviceHandle; /* for SATM */
  bit32                 SMNumOfFCA;
  bit32                 SMNumOfID;
  tdsaTimerRequest_t    tdIDTimer; /* ID Device Data timer for SATA device */
#endif 
}  tdsaDeviceData_t;

/*
  this field is used to add or remove SAS device from sharedcontext
*/
typedef struct tdsaSASSubID_s
{
  bit32        sasAddressHi;
  bit32        sasAddressLo;
  bit8         initiator_ssp_stp_smp;
  bit8         target_ssp_stp_smp;

} tdsaSASSubID_t;


struct tdsaDeviceData_s;
//struct itdssDiscoveryData_s;

/** \brief data structure for TD port context
 *  This structure maintains information about the port such as ID address frame
 *  and the discovery status and the list of devices discovered by this port.
 *  itdsaIni_t->PortContext[] points to this structure.
 *  agsaPortContext->osData points to this structure, too.
 */
typedef struct tdsaPortContext_s
{
  /**< current number of devices in this PortContext */
  bit32                         Count;
 
  bit32                   DiscoveryState;   
  
  bit32                   discoveryOptions;
  /* Discovery ready is given? */ 
  bit32                   DiscoveryRdyGiven; 
  /* Port has received link up */
  bit32                   SeenLinkUp;
  /* statistics */
  bit32                   numAvailableTargets;
  /* flag: indicates that discovery is trigggered by tiINIDiscoverTargets */
  bit32                   osInitiatedDiscovery;
  
  bit32                         id; /* for debugging only */
  tdList_t                      FreeLink; /**< free portcontext list */
  tdList_t                      MainLink; /**< in-use portcontext list */
  /**< SAS address of the remote device */
  bit32                         sasRemoteAddressHi; /**< SAS address high part */
  bit32                         sasRemoteAddressLo; /**< SAS address low part */
  /**< SAS ID frame of the remote device */
  agsaSASIdentify_t             sasIDframe;
  
  /**< SAS address of the local device*/
  bit32                         sasLocalAddressHi; /**< SAS address high part */
  bit32                         sasLocalAddressLo; /**< SAS address low part */

  /**< the list of PhyID belonging to this port */
  bit8                          PhyIDList[TD_MAX_NUM_PHYS];
  tiPortalContext_t             *tiPortalContext;
  /* used in tiINIDiscoverTarget() */
  agsaRoot_t                    *agRoot;
  agsaPortContext_t             *agPortContext;
  /* maybe needs timers for saPhyStart() */

  bit8                  nativeSATAMode; /* boolean flag: whether the port is in Native SATA mode */
  bit8                remoteSignature[8]; /* the remote signature of the port is the port is in native SATA mode */
  bit8                 directAttatchedSAS; /* boolean flag: whether the port connected directly to SAS end device*/
  /* SAS/SATA discovery information such as discoveringExpanderList */
  tdsaDiscovery_t            discovery;
  bit32                      valid;
  bit8                       LinkRate;
  bit32                      RegisteredDevNums; /* registered number of devices */
  bit32                      eventPhyID; /* used for saHwEventAck() */
  bit32                      Transient; /* transient period between link up and link down/port recovery */
  agsaContext_t              agContext; /* used in tiCOMPortStop()*/
  bit32                      PortRecoverPhyID; /* used to remember PhyID in Port_Recover event; used in ossaDeviceRegistrationCB() */
  bit32                      DiscFailNSeenBC; /* used to remember broadcast change after discovery failure */
  bit8                       remoteName[68];
#ifdef FDS_DM
  dmPortContext_t            dmPortContext;
  bit32                      DMDiscoveryState; /* DM discovery state returned by tddmDiscoverCB or tddmQueryDiscoveryCB */
  bit32                      UseDM; /* set only when the directly attached target is SMP target(expander) */
  bit32                      UpdateMCN; /* flag for inidicating update MCN */    
#endif  
}  tdsaPortContext_t;

/** \brief data structure for TD port information
 *  This structure contains information in order to start the port
 *  The most of fields are filled in by OS layer and there can be up to
 *  8 of these structures
 *  tiPortalContext_t->tdData points to this structure.
 */
typedef struct tdsaPortStartInfo_s {
  tiPortalContext_t  *tiPortalContext; 
  tdsaPortContext_t  *portContext; /* tdsaportcontext */
  agsaSASIdentify_t  SASID;        /* SAS ID of the local */
  tdssPortFlags_t    flags;
  agsaPhyConfig_t    agPhyConfig;
}  tdsaPortStartInfo_t;
/*
  expander data structure
*/

#define REPORT_LUN_LEN             16
#define REPORT_LUN_OPCODE          0xa0
typedef struct tdDeviceLUNInfo_s
{
  unsigned long 	    tiDeviceHandle; 
  bit32                  numOfLun; 
}tdDeviceLUNInfoIOCTL_t;

typedef struct tdsaExpander_s 
{
  tdList_t                  linkNode; /**< the link node data structure of the expander */
  tdList_t                  upNode; /**< the link node data structure of the expander */
  tdsaDeviceData_t          *tdDevice; /**< the pointer to the device */
  struct tdsaExpander_s     *tdUpStreamExpander; /**< the pointer to the upstream expander device */
  bit8                      hasUpStreamDevice;
  bit8                      discoveringPhyId;
  bit16                     routingIndex; /* maximum routing table index reported by expander */
  bit16                     currentIndex[TD_MAX_EXPANDER_PHYS]; /* routing table index in use */
  tdsaDeviceData_t          *tdDeviceToProcess;    /* on some callbacks, this is a link to the device of interest */
  bit32                     configSASAddressHi;
  bit32                     configSASAddressLo;
  struct tdsaExpander_s     *tdCurrentDownStreamExpander; 
  bit8                      upStreamPhys[TD_MAX_EXPANDER_PHYS];
  bit16                     numOfUpStreamPhys;
  bit16                     currentUpStreamPhyIndex;
  bit32                     upStreamSASAddressHi; 
  bit32                     upStreamSASAddressLo;  
  bit32                     underDiscovering;
  bit32                     configRouteTable: 1;
  bit32                     configuring: 1;
  bit32                     configReserved: 30;
  bit32                     id; /* for debugging */
  struct tdsaExpander_s     *tdReturnginExpander;
  bit8                      downStreamPhys[TD_MAX_EXPANDER_PHYS];
  bit16                     numOfDownStreamPhys;
  bit16                     currentDownStreamPhyIndex;
  bit32                     discoverSMPAllowed; /* used only for configurable routers */
  bit8                      routingAttribute[TD_MAX_EXPANDER_PHYS];
  bit32                     configSASAddressHiTable[DEFAULT_MAX_DEV];
  bit32                     configSASAddressLoTable[DEFAULT_MAX_DEV];
  bit32                     configSASAddrTableIndex;  
  
} tdsaExpander_t;

/* 
 * SATA SAT specific function pointer for SATA completion for SAT commands.
 */
typedef void (*satCompleteCbPtr_t  )(
                          agsaRoot_t        *agRoot,
                          agsaIORequest_t   *agIORequest,
                          bit32             agIOStatus,
                          agsaFisHeader_t   *agFirstDword,
                          bit32             agIOInfoLen,
                          agsaFrameHandle_t agFrameHandle,
                          void              *satIOContext
                       );

/* 
 * SATA SAT specific function for I/O context
 */
typedef struct satIOContext_s 
{
  tdList_t                    satIoContextLink;
  satDeviceData_t             *pSatDevData;
  agsaFisRegHostToDevice_t    *pFis;
  tiIniScsiCmnd_t             *pScsiCmnd;
  scsiRspSense_t              *pSense;
  tiSenseData_t               *pTiSenseData;
  void                        *tiRequestBody;
  void                        *tiScsiXchg; /* for writesame10() */
  bit32                       reqType;
  bit32                       interruptContext;
  satCompleteCbPtr_t          satCompleteCB;
  satInternalIo_t             *satIntIoContext;
  tiDeviceHandle_t            *ptiDeviceHandle;
  bit8                        sataTag;
  bit8                        superIOFlag;/* Flag indicating type for tiScsiXchg */
  bit8                        reserved1;  /* Padding for allignment */
  bit8                        reserved2;  /* Padding for allignment */
  bit32                       currentLBA; /* current LBA for read and write */
  bit32                       ATACmd;     /* ATA command */
  bit32                       OrgTL;      /* original tranfer length(tl) */
  bit32                       LoopNum;    /* denominator tl */
  bit32                       LoopNum2;    /* denominator tl */
  bit8                        LBA[8];     /* for reassign blocks; current LBA */
  bit32                       ParmIndex;  /* for reassign blocks;current idx in defective LBA LIST */
  bit32                       ParmLen;    /* for reassign blocks; defective LBA list length */
  bit32                       NotifyOS;   /* only for task management */
  bit32                       TMF;        /* task management function */
  struct satIOContext_s       *satToBeAbortedIOContext; 
  struct satIOContext_s       *satOrgIOContext;
  bit32                       pid;        /* port id; used to protect double completion */
} satIOContext_t;


/** \brief data structure for SAS SSP IO reuqest body
 *  This structure contains IO related fields.
 *  agsaIORequest->osData points to this 
 */
typedef struct tdIORequestBody_s {
  tdssIOCompleted_t              IOCompletionFunc; 
  tiDeviceHandle_t               *tiDevHandle;
  tiIORequest_t                  *tiIORequest; /* for ini */
  agsaIORequest_t                agIORequest; /* for command and task and tm response and response */
  tiIORequest_t                  *tiIOToBeAbortedRequest; /* IO to be aborted */
  agsaContext_t                  agContext;  
#ifdef FDS_SM
  smIORequestBody_t              smIORequestBody;    /*SATA IO request body*/
  smIORequest_t                  smIORequest; /* for SATM */
  void                           *osMemHandle; /* for ID data */
  bit32                          pid;  /* port id for SATA completion */
  bit32                          superIOFlag; /* Super IO or not */ 
  union {
    smScsiInitiatorRequest_t       smSCSIRequest;
    smSuperScsiInitiatorRequest_t  smSuperSCSIRequest;      
  } SM;
#endif 
  union {
    struct {
      agsaSASRequestBody_t           agSASRequestBody;
      //      agsaSASRequestBody_t           agSASResponseBody;
      /* SSP response */
      //      agsaSSPResponseInfoUnit_t      agSSPRspIU;
    } SAS;
    struct {
      agsaSATAInitiatorRequest_t    agSATARequestBody;
      scsiRspSense_t                sensePayload;
      tiSenseData_t                 tiSenseData;
      satIOContext_t                satIOContext;
    } SATA;
  } transport;  
  bit32                          ioStarted;
  bit32                          ioCompleted;
  bit32                          reTries;
  /**< for ESGL */
  tdList_t                       EsglPageList;
  bit32                          agRequestType;
  union {
    struct {
      bit32                     expDataLength;
      tiSgl_t                   tiSgl1;
      tiSgl_t                   tiSgl2;
      void                      *sglVirtualAddr;
    } InitiatorRegIO;  /* regular IO */
    struct {
      void                      *osMemHandle;
      tiIORequest_t             *CurrentTaskTag;
      tiIORequest_t             *TaskTag;
    } InitiatorTMIO;  /* task management */

    struct {
      tiIORequest_t   tiIORequest; /* for target */
      
      union {
        struct {
          tiSgl_t         tiSgl1;
          void          * sglVirtualAddr;
        } RegIO;
        
        struct {
          tiSgl_t         tiSgl1;
          void          * sglVirtualAddr;
          tiSgl_t         tiSglMirror;
          void          * sglMirrorVirtualAddr;
          tdList_t        EsglMirrorPageList;
        } MirrorIO;
        
      } TargetIOType;
      
    } TargetIO;    /* target regular IO */
    
    
  } IOType;
}  tdIORequestBody_t;

/** \brief data structure for SAS SMP reuqest body
 *  This structure contains IO related fields.
 *  agsaIORequest->osData points to this
 *  
 */
typedef struct tdssSMPRequestBody_s {
  tdIORequestBody_t              IORequestBody;    /* for combo, must be the first */
  tdssSMPCompleted_t             SMPCompletionFunc;/* must be the second */
  
  tiDeviceHandle_t               *tiDevHandle;    /* not used for TD generated SMP */
  agsaIORequest_t                agIORequest;
  agsaSASRequestBody_t           agSASRequestBody;
  agsaSATAInitiatorRequest_t     agSATARequestBody; 
  void                           *osMemHandle;
  tdsaDeviceData_t               *tdDevice;
  tiIORequest_t                  *CurrentTaskTag; /* SMP is used for simulate target reset */
  tdsaPortContext_t              *tdPortContext; /* portcontext where SMP is sent from */
  bit8                           smpPayload[SMP_DIRECT_PAYLOAD_LIMIT]; /* for smp retries; 
                                                                          only for direct SMP */
  bit32                          retries; /* number of retries */
  bit32                          queueNumber; /* number of retries */
  /* for indirect SMP req/rsp */
  void                           *IndirectSMPReqosMemHandle;
  void                           *IndirectSMPReq;
  bit32                          IndirectSMPReqLen;
  void                           *IndirectSMPResposMemHandle;
  void                           *IndirectSMPResp;
  bit32                          IndirectSMPRespLen;
  
}  tdssSMPRequestBody_t;

#ifdef AGTIAPI_CTL
typedef struct tdIORequest_s
{
  tiIORequest_t             tiIORequest;
  tdIORequestBody_t         tdIORequestBody;
  void                      *osMemHandle;

  void                      *osMemHandle2;
  bit32                     physUpper32;
  bit32                     physLower32;
  void                      *virtAddr;

  tiIntrEventType_t         eventType;
  bit32                     eventStatus;
} tdIORequest_t;
#endif

#ifdef PASSTHROUGH
/* this is allocated by OS layer but used in TD layer just like tdIORequestBody */
typedef struct tdPassthroughCmndBody_s
{
  ostiPassthroughCmndEvent_t     EventCB;
  tiPassthroughRequest_t         *tiPassthroughRequest;
  tiDeviceHandle_t           *tiDevHandle;
  bit32                          tiPassthroughCmndType; /* used in local abort */
  union {
    struct {
#ifdef TO_DO      
      tiSMPFunction_t            SMPFn;
      tiSMPFunctionResult_t      SMPFnResult;  /* for SMP target only */
      bit32                      IT; /* 0: initiator 1: target */
      tiSMPFrameHeader_t         SMPHeader;
#endif      
      tdssSMPRequestBody_t       SMPBody;
    } SMP;
    struct {
      tiDataDirection_t          dataDirection;
    } RMC;
  } protocol;
} tdPassthroughCmndBody_t;

#endif

#endif                          /* __TDTYPES_H__ */
