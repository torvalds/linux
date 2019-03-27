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
#ifndef __SMTYPES_H__
#define __SMTYPES_H__

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>

#include <dev/pms/RefTisa/sat/src/smlist.h>

/*
 * SAT specific structure per SATA drive 
 */
#define SAT_NONNCQ_MAX  1
#define SAT_NCQ_MAX     32
#define SAT_MAX_INT_IO  16
#define SAT_APAPI_CMDQ_MAX 2

/* SMP direct payload size limit: IOMB direct payload size = 48 */
#define SMP_DIRECT_PAYLOAD_LIMIT 44

/* timer functions ; both I and T */
typedef void (*smTimerCBFunc_t)(smRoot_t *smRoot, void *timerData1, void *timerData2, void *timerData3);


/** \brief data structure for timer request
 *  Timer requests are enqueued and dequeued using smList_t
 *  and have a callback function
 */
typedef struct smTimerRequest_s {
  /* the number of ticks */
  bit32             timeout;
  void              *timerData1;
  void              *timerData2;
  void              *timerData3;
  smTimerCBFunc_t   timerCBFunc;
  smList_t          timerLink;
  bit32             timerRunning;
}  smTimerRequest_t;



typedef struct smSatInternalIo_s
{
  smList_t                    satIntIoLink;
  smIORequest_t               satIntSmIORequest; /* old satIntTiIORequest */
  void                        *satIntRequestBody; /* maps to smIOrequestBody */
  smScsiInitiatorRequest_t    satIntSmScsiXchg; /* old satIntTiScsiXchg*/
  smMem_t                     satIntDmaMem;
  smMem_t                     satIntReqBodyMem;
  bit32                       satIntFlag;
  smIORequest_t               *satOrgSmIORequest; /* old satOrgTiIORequest */
  bit32                       id;
} smSatInternalIo_t;



typedef struct smDeviceData_s  {
  smList_t                FreeLink; /* free dev list */
  smList_t                MainLink; /* main(in use) dev list */
  bit32                   id; /* for debugging only */
  smRoot_t                *smRoot;
  agsaDevHandle_t         *agDevHandle;
  bit32                   valid; /* valid or registered */
  smTimerRequest_t        SATAIDDeviceTimer; /* ID Device Data timer for SATA device */
  bit32                   SMAbortAll; /* flag for abortall case */ 
  smDeviceHandle_t        *smDevHandle;
  bit32                   directlyAttached;
  agsaDevHandle_t         *agExpDevHandle; /* expander a device is attached to if expander attached */
  bit32                   phyID;
  agsaContext_t           agDeviceResetContext; /* used in saLocalPhyControl() */
  bit32                   SMNumOfFCA;
    
  /* from satDeviceData_t */
  smList_t                satIoLinkList;            /* Normal I/O from TISA         */
  smList_t                satFreeIntIoLinkList;     /* SAT internal I/O free list   */
  smList_t                satActiveIntIoLinkList;   /* SAT internal I/O active list */
  smSatInternalIo_t       satIntIo[SAT_MAX_INT_IO]; /* Internal I/O resource        */
  agsaSATAIdentifyData_t  satIdentifyData;          /* Copy of SATA Id Dev data     */
  bit32                   satNCQ;                   /* Flag for NCQ support         */ 
  bit32                   sat48BitSupport;          /* Flag for 48-bit addressing   */
  bit32                   satSMARTSelfTest;         /* Flag for SMART self test     */
  bit32                   satSMARTFeatureSet;       /* Flag for SMART feature set   */
  bit32                   satSMARTEnabled;          /* Flag for SMART enabled       */
  bit32                   satRemovableMedia;        /* Flag for Removable Media     */
  bit32                   satRemovableMediaEnabled; /* Flag for Removable Media Enabled */
  bit32                   satDMASupport;            /* Flag for DMA Support         */
  bit32                   satDMAEnabled;            /* Flag for DMA Enabled         */
  bit32                   satUltraDMAMode;          /* Ultra DMA mode value        */
  bit32                   satDMADIRSupport;         /* Flag for DMA direction       */
  bit32                   satReadLookAheadSupport;  /* Flag for Read Look Ahead */
  bit32                   satVolatileWriteCacheSupport; /* Flag for Volatile Write Cache support*/
  bit32                   satWWNSupport;            /* Flag for DMA Enabled         */
  bit32                   satDMASetupAA;            /* Flag for DMA Setup Auto-Activate */
  bit32                   satNCQQMgntCmd;           /* Flag for NCQ Queue Management Command */
  bit32 volatile          satPendingIO;             /* Number of pending I/O        */
  bit32 volatile          satPendingNCQIO;          /* Number of pending NCQ I/O    */
  bit32 volatile          satPendingNONNCQIO;       /* Number of pending NON NCW I/O*/
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
  smIORequest_t           *satTmTaskTag;            /* TM Task Tag                  */
  void                    *satSaDeviceData;         /* Pointer back to sa dev data  */
  bit32                   ID_Retries;               /* identify device data retries */
  bit32                   OSAbortAll;               /* OS calls abort all           */
  bit32                   ReadCapacity;             /* Read Capacity Type; 10, 16   */
  bit32                   sasAddressLo;             /**< HOST SAS address lower part */
  bit32                   sasAddressHi;             /**< HOST SAS address higher part */

}  smDeviceData_t;

typedef struct smAtaPassThroughHdr_s
{
  bit8 opc;
  bit8 mulCount : 3;
  bit8 proto : 4;
  bit8 extend : 1;
  bit8 offline : 2;
  bit8 ckCond : 1;
  bit8 tType : 1;
  bit8 tDir : 1;
  bit8 byteBlock : 1;
  bit8 tlength : 2;
  
}smAtaPassThroughHdr_t;

/*
 * SCSI Sense Data
 */
typedef struct 
{
  bit8       snsRespCode;
  bit8       snsSegment;
  bit8       senseKey;          /* sense key                                */
  bit8       info[4];
  bit8       addSenseLen;       /* 11 always                                */
  bit8       cmdSpecific[4];
  bit8       addSenseCode;      /* additional sense code                    */
  bit8       senseQual;         /* additional sense code qualifier          */
  bit8       fru;
  bit8       skeySpecific[3];
} smScsiRspSense_t;


/* 
 * SATA SAT specific function pointer for SATA completion for SAT commands.
 */
typedef void (*smSatCompleteCbPtr_t  )(
                          agsaRoot_t        *agRoot,
                          agsaIORequest_t   *agIORequest,
                          bit32             agIOStatus,
                          agsaFisHeader_t   *agFirstDword,
                          bit32             agIOInfoLen,
                          agsaFrameHandle_t agFrameHandle,
                          void              *satIOContext
                       );

/* for SMP only */
typedef void (*smSMPCompleted_t)(
                                  agsaRoot_t            *,
                                  agsaIORequest_t       *,
                                  bit32                 ,
                                  bit32                 ,
                                  agsaFrameHandle_t     
                                );


/* 
 * SATA SAT specific function for I/O context
 */
typedef struct smSatIOContext_s 
{
  smList_t                    satIoContextLink;
  smDeviceData_t              *pSatDevData;
  agsaFisRegHostToDevice_t    *pFis;
  smIniScsiCmnd_t             *pScsiCmnd;
  smScsiRspSense_t            *pSense;
  smSenseData_t               *pSmSenseData; /* old pTiSenseData */
  void                        *smRequestBody;  /* smIORequestBody_t; old tiRequestBody*/
  void                        *smScsiXchg; /* for writesame10(); old tiScsiXchg */
  bit32                       reqType;
  bit32                       interruptContext;
  smSatCompleteCbPtr_t        satCompleteCB;
  smSatInternalIo_t           *satIntIoContext; /* SATM generated IOs */
  smDeviceHandle_t            *psmDeviceHandle; /* old ptiDeviceHandle */
  bit8                        sataTag;
  bit8                        superIOFlag;/* Flag indicating type for smScsiXchg */
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
  struct smSatIOContext_s     *satToBeAbortedIOContext; 
  struct smSatIOContext_s     *satOrgIOContext;
  bit32                       UpperAddr;
  bit32                       LowerAddr;
  bit32                       SplitIdx;
  bit32                       AdjustBytes;
  bit32                       EsglLen;
  /* For the SAT Passthrough */
  bit8                        ck_cond;  
  bit8                        extend;
  bit8                        sectorCnt07;
  bit8                        LBAHigh07;
  bit8                        LBAMid07;
  bit8                        LBALow07;
  bit8                        Sector_Cnt_Upper_Nonzero;
  bit8                        LBA_Upper_Nonzero;  
  bit32                       pid;        /* port id; used to protect double completion */
  bit32                       id;         /* for debugging */
} smSatIOContext_t;

typedef struct smIORequestBody_s {
  smList_t                    satIoBodyLink;
  smDeviceHandle_t            *smDevHandle;
  smIORequest_t               *smIORequest;
  agsaIORequest_t             agIORequest;
  smIORequest_t               *smIOToBeAbortedRequest; /* IO to be aborted; old tiIOToBeAbortedRequest */
  bit32                       id;
  bit32                       InUse;  
  union {
    struct {
      agsaSATAInitiatorRequest_t    agSATARequestBody;
      smScsiRspSense_t              sensePayload;
      smSenseData_t                 smSenseData; /* old tiSenseData */
      smSatIOContext_t              satIOContext;
    } SATA;
  } transport;  
  bit32                          ioStarted;
  bit32                          ioCompleted;
  bit32                          reTries;
  union {
    struct {
      bit32                     expDataLength;
      smSgl_t                   smSgl1; /* old tiSgl1 */
      smSgl_t                   smSgl2; /* old tiSgl2 */
      void                      *sglVirtualAddr;
    } InitiatorRegIO;  /* regular IO */
    struct {
      void                      *osMemHandle;
      smIORequest_t             *CurrentTaskTag;
      smIORequest_t             *TaskTag;
    } InitiatorTMIO;  /* task management */
  } IOType;
  
} smIORequestBody_t;

typedef struct smSMPRequestBody_s {
  smSMPCompleted_t               SMPCompletionFunc;/* must be the second */
  
  smDeviceHandle_t               *smDevHandle;    /* not used for SM generated SMP */
  agsaIORequest_t                agIORequest;
  agsaSASRequestBody_t           agSASRequestBody;
  void                           *osMemHandle;
  smDeviceData_t                 *smDeviceData;
  smIORequest_t                  *CurrentTaskTag; /* SMP is used for simulate target reset */
//  tdsaPortContext_t              *tdPortContext; /* portcontext where SMP is sent from */
  bit8                           smpPayload[SMP_DIRECT_PAYLOAD_LIMIT]; /* for smp retries; 
                                                                          only for direct SMP */
  bit32                          retries; /* number of retries */
 
}  smSMPRequestBody_t;


typedef struct smRootOsData_s {
  smRoot_t  *smRoot;            /**< Pointer back to smRoot                 */
  void      *smAllShared;       /**< Pointer to smIntContext_t               */
  void      *smIni;             /**< Pointer to SAS/SATA initiator               */
}  smRootOsData_t;

typedef struct smIntContext_s {
  /**< agsaRoot_t->osData points to this */
  struct smRootOsData_s      smRootOsData;
  
  bit32               usecsPerTick;
  agsaRoot_t          *agRoot;
  
  /**< software-related initialization params used in saInitialize() */
  smSwConfig_t        SwConfig;  
  
  /**< timers used commonly in SAS/SATA */
  smList_t                      timerlist;
  
  /**< pointer to Device memory */
  smDeviceData_t             *DeviceMem;  
  smList_t                   FreeDeviceList;
  smList_t                   MainDeviceList;

  /**< pointer to IO memory */
  smIORequestBody_t         *IOMem; 
  smList_t                   freeIOList;
  smList_t                   mainIOList;  
  bit32                      FCA;
}  smIntContext_t;

typedef struct smIntRoot_s
{
  /**<< common data structure for SAS/SATA */
  smIntContext_t          smAllShared;
} smIntRoot_t;


#endif                          /* __SMTYPES_H__ */

