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
#ifndef __DMTYPES_H__
#define __DMTYPES_H__

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>

#include <dev/pms/RefTisa/discovery/dm/dmlist.h>
#ifdef TBD
#include <dev/pms/RefTisa/tisa/api/tiscsi.h>
#endif


/* for SMP only */
typedef void (*dmSMPCompleted_t) (
                                    agsaRoot_t            *,
                                    agsaIORequest_t       *,
                                    bit32                 ,
                                    bit32                 ,
                                    agsaFrameHandle_t     
                                    );


/* timer functions ; both I and T */
typedef void (*dmTimerCBFunc_t)(dmRoot_t *dmRoot, void *timerData1, void *timerData2, void *timerData3);


/** \brief data structure for timer request
 *  Timer requests are enqueued and dequeued using dmList_t
 *  and have a callback function
 */
typedef struct dmTimerRequest_s {
  /* the number of ticks */
  bit32             timeout;
  void              *timerData1;
  void              *timerData2;
  void              *timerData3;
  dmTimerCBFunc_t   timerCBFunc;
  dmList_t          timerLink;
  bit32             timerRunning;
}  dmTimerRequest_t;

typedef struct dmRootOsData_s {
  dmRoot_t  *dmRoot;            /**< Pointer back to dmRoot                 */
  void      *dmAllShared;       /**< Pointer to dmContext_t               */
  void      *dmIni;             /**< Pointer to SAS/SATA initiator               */
}  dmRootOsData_t;

typedef struct DMSASAddressID_s 
{
  bit32   sasAddressLo;     /**< HOST SAS address lower part */
  bit32   sasAddressHi;     /**< HOST SAS address higher part */
  bit8    phyIdentifier;    /**< PHY IDENTIFIER of the PHY */
} DMSASAddressID_t;

struct dmExpander_s;

typedef struct dmDiscovery_s 
{
  dmList_t                   discoveringExpanderList;
  dmList_t                   UpdiscoveringExpanderList;
  //  tdList_t                   freeExpanderList;
  bit32                   status;
  DMSASAddressID_t        sasAddressIDDiscoverError;
  agsaSATAIdentifyData_t  *pSataIdentifyData;
  struct dmExpander_s     *RootExp; /* Root expander of discovery */
  bit32                   NumOfUpExp;
  bit32                   type; /* discovery type: TDSA_DISCOVERY_OPTION_FULL_START 
                                   or TDSA_DISCOVERY_OPTION_INCREMENTAL_START*/
  bit32                   retries;                                   
  bit32                   configureRouteRetries; 
  bit32                   deviceRetistrationRetries; 
  dmTimerRequest_t        discoveryTimer;
  dmTimerRequest_t        configureRouteTimer;
  dmTimerRequest_t        deviceRegistrationTimer;
  dmTimerRequest_t        BCTimer; /* Broadcast Change timer for ResetTriggerred */
  smpRespDiscover_t       SMPDiscoverResp;
  smpRespDiscover2_t      SMPDiscover2Resp;
  bit32                   pendingSMP; /* the number of pending SMP for this discovery */
  bit32                   SeenBC; /* received Broadcast change */
  bit32                   forcedOK; /* report DiscOK when chance is missed */ 
  dmTimerRequest_t        SMPBusyTimer; /* SMP retry timer for saSMPStart busy */
  bit32                   SMPRetries; /* number of SMP retries when LL returns busy for saSMPStart*/
  bit32                   ResetTriggerred; /* Hard/Link reset triggerred by discovery */
  dmTimerRequest_t        DiscoverySMPTimer; /* discovery-related SMP application Timer */
  /* For SAS 2 */
  bit32                   DeferredError; /* Deferred Error for SAS 2 */
  bit32                   ConfiguresOthers; /* exp configures others; no routing configuration */
} dmDiscovery_t;

typedef struct dmSASSubID_s
{
  bit32        sasAddressHi;
  bit32        sasAddressLo;
  bit8         initiator_ssp_stp_smp;
  bit8         target_ssp_stp_smp;

} dmSASSubID_t;

struct dmDeviceData_s;

typedef struct dmIntPortContext_s
{
  /**< current number of devices in this PortContext */
  bit32                         Count;
  bit32                   DiscoveryState;   
  bit32                   DiscoveryAbortInProgress;   
  /* passed by tiINIDiscoverTargets()
     eg) discovery or rediscovery ....
  */
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
  dmList_t                      FreeLink; /**< free portcontext list */
  dmList_t                      MainLink; /**< in-use portcontext list */
  /**< SAS address of the remote device */
  bit32                         sasRemoteAddressHi; /**< SAS address high part */
  bit32                         sasRemoteAddressLo; /**< SAS address low part */
  /**< SAS ID frame of the remote device */
  agsaSASIdentify_t             sasIDframe;
  
  /**< SAS address of the local device*/
  bit32                         sasLocalAddressHi; /**< SAS address high part */
  bit32                         sasLocalAddressLo; /**< SAS address low part */
#ifdef TBD
  /**< the list of PhyID belonging to this port */
  bit8                          PhyIDList[DM_MAX_NUM_PHYS];
#endif  
  dmPortContext_t               *dmPortContext;
  dmRoot_t                      *dmRoot;
  
#ifdef TBD  
  /* used in tiINIDiscoverTarget() */
  agsaRoot_t                    *agRoot;
  agsaPortContext_t             *agPortContext;
  /* maybe needs timers for saPhyStart() */

  bit8                  nativeSATAMode; /* boolean flag: whether the port is in Native SATA mode */
  bit8                remoteSignature[8]; /* the remote signature of the port is the port is in native SATA mode */
#endif  
  bit8                 directAttatchedSAS; /* boolean flag: whether the port connected directly to SAS end device*/
  /* SAS/SATA discovery information such as discoveringExpanderList */
  dmDiscovery_t              discovery;
  bit32                      valid;
  bit8                       LinkRate;
  bit32                      RegisteredDevNums; /* registered number of devices */
  bit32                      eventPhyID; /* used for saHwEventAck() */
  bit32                      Transient; /* transient period between link up and link down/port recovery */
  bit32                      RegFailed; /* Registration of expander belonging to this port failure */ 
  
}  dmIntPortContext_t;

typedef struct dmDeviceData_s  {

  dmList_t                FreeLink; /* free dev list */
  dmList_t                MainLink; /* main(in use) dev list */
  dmList_t                IncDisLink; /* Used for incremental Discovery only */
  bit32                   id; /* for debugging only */
  bit8                    DeviceType;
  /* used in tiINIIOStart() */
  dmRoot_t                *dmRoot;
//  agsaDevHandle_t         *agDevHandle;
  
  /* for SAS; remote device */
  //  agsaSASDeviceInfo_t     agSASDeviceInfo;
  /* device's sas address */
  DMSASAddressID_t        SASAddressID;
  bit8                    initiator_ssp_stp_smp;
  bit8                    target_ssp_stp_smp;
  bit8                    numOfPhys;

  /* SATA specific data */
  bit8                    satSignature[8];          /* SATA device Signature*/

  /**< pointer to tdsaPortcontext which the device belongs to */
  struct dmIntPortContext_s *dmPortContext;
  /* validity of device */
  bit8                    valid;
  bit8                    valid2;
  bit8                    processed; /* used in TD discovery */
#ifdef AGTIAPI_CTL
  bit8                    discovered;
#endif
  agsaDeviceInfo_t        agDeviceInfo;
  dmDeviceInfo_t          dmDeviceInfo;
  agsaContext_t           agContext; /* used in saRegisterNewDevice()*/
  /**< pointer to dmExpander if Device is expander */
  struct dmExpander_s     *dmExpander;
  struct dmDeviceData_s   *ExpDevice; /* Expander device which this device is attached to */
  
  bit8                    phyID;      /* PhyID this device is attached to SPC or expander */
  agsaSASIdentify_t     sasIdentify; /* used only in TD discovery */
  bit8                  connectionRate;
//  bit8                  registered;
  bit8                  directlyAttached;
  bit8                  SASSpecDeviceType; /* 0 - 3; SAS_NO_DEVICE - SAS_FANOUT_EXPANDER_DEVICE */
  bit32                 IOStart;
  bit32                 IOResponse;
  agsaContext_t         agDeviceResetContext; /* used in saLocalPhyControl() */
  bit32                 TRflag; /* transport recovery flag; used only for tiINITransportRecovery */
  bit32                 ResetCnt; /* number of reset to the device */  
  bit32                 registered; /* registered to LL */
  bit32                 reported; /* reproted to TDM */  
  bit32                 MCN; /* MCN; initialized to 0; current value in discovery */  
  bit32                 MCNDone; /* done in updating MCN */  
  bit32                 PrevMCN; /* MCN; initialized to 0; previous value in discovery */  

}  dmDeviceData_t;


typedef struct dmExpander_s 
{
  /* start of dmDeviceData */
#ifdef TBD 
  dmList_t                FreeLink; /* free dev list */
  dmList_t                MainLink; /* main(in use) dev list */
#endif  
  bit32                   id; /* for debugging only */
  bit32                   InQID; /* Inbound queue ID */
  bit32                   OutQID; /* Outbound queue ID */
  bit8                    DeviceType;
  /* used in tiINIIOStart() */
  dmRoot_t                *dmRoot;
  agsaDevHandle_t         *agDevHandle;

  dmList_t                  linkNode; /**< the link node data structure of the expander */
  dmList_t                  upNode; /**< the link node data structure of the expander */
  dmDeviceData_t            *dmDevice; /**< the pointer to the device data */
  struct dmExpander_s       *dmUpStreamExpander; /**< the pointer to the upstream expander device */
  bit8                      hasUpStreamDevice;
  bit8                      discoveringPhyId;
  bit16                     routingIndex; /* maximum routing table index reported by expander */
  bit16                     currentIndex[DM_MAX_EXPANDER_PHYS]; /* routing table index in use */
  /*ReportPhySataSend in DM */ 
  dmDeviceData_t            *dmDeviceToProcess;    /* on some callbacks, this is a link to the device of interest */
  
  bit32                     configSASAddressHi;
  bit32                     configSASAddressLo;
  struct dmExpander_s       *dmCurrentDownStreamExpander; 
  bit8                      upStreamPhys[DM_MAX_EXPANDER_PHYS];
  bit16                     numOfUpStreamPhys;
  bit16                     currentUpStreamPhyIndex;
  bit32                     upStreamSASAddressHi; 
  bit32                     upStreamSASAddressLo;  
  bit32                     underDiscovering;
  bit32                     configRouteTable: 1;
  bit32                     configuring: 1;
  bit32                     configReserved: 30;
#ifdef TBD  
  bit32                   id; /* for debugging */
#endif
  struct dmExpander_s       *dmReturnginExpander;
  bit8                      downStreamPhys[DM_MAX_EXPANDER_PHYS];
  bit16                     numOfDownStreamPhys;
  bit8                      currentDownStreamPhyIndex;
  bit32                     discoverSMPAllowed; /* used only for configurable routers */
  bit8                      routingAttribute[DM_MAX_EXPANDER_PHYS];
  bit32                     configSASAddressHiTable[DM_MAX_DEV];
  bit32                     configSASAddressLoTable[DM_MAX_DEV];
  bit32                     configSASAddrTableIndex;  
  /* for SAS 2 */  
  bit32                     SAS2; /* supports SAS2 spec of not. The value of LONG RESPONSE 
                                     in report general response */
  bit32                     TTTSupported; /* Table to Table is supported */
  bit32                     UndoDueToTTTSupported; /* flag that indicates undo exp, device, route
                                                      configuration due to TTT */
  
} dmExpander_t;

typedef struct dmIndirectSMPRequestBody_s {
  dmList_t                     Link;
  bit32                        id;

}  dmIndirectSMPRequestBody_t;

/*
  should DM allocate a pool of SMP and manages it 
  or
  depend on ostiAllocMemory()
*/
typedef struct dmSMPRequestBody_s {
  dmList_t                     Link;
  dmSMPCompleted_t             SMPCompletionFunc;/* must be the second */

#ifdef TBD    
  tiDeviceHandle_t               *tiDevHandle;    /* not used for TD generated SMP */
#endif  
  agsaIORequest_t                agIORequest;
  agsaSASRequestBody_t           agSASRequestBody;
  agsaSATAInitiatorRequest_t     agSATARequestBody; 
  /**< SMP response */
  //agsaSMPFrame_t                 SMPRsp;
  dmDeviceData_t                 *dmDevice;
  
#ifdef TBD
  void                           *osMemHandle;
  // can this be simply dmExpander_t
  dmDeviceData_t                 *dmDevice;
  tiIORequest_t                  *CurrentTaskTag; /* SMP is used for simulate target reset */
#endif
  dmRoot_t                       *dmRoot;
//  dmExpander_t                   *dmExpander;
  dmIntPortContext_t             *dmPortContext; /* portcontext where SMP is sent from */
  bit8                           smpPayload[SMP_DIRECT_PAYLOAD_LIMIT]; /* for smp retries; 
                                                                          only for direct SMP */
  bit32                          retries; /* number of retries */
  /* for indirect SMP req/rsp */
  void                           *IndirectSMP;
  bit32                          IndirectSMPUpper32;
  bit32                          IndirectSMPLower32;
  /* used only when SMP is INDIRECT SMP request. On SMP completion, 
     this is used to free up INDIRECT SMP response 
  */
  void                           *IndirectSMPResponse; /* dmSMPRequestBody_t */   



#ifdef TBD  
  void                           *IndirectSMPReqosMemHandle;
  void                           *IndirectSMPReq;
  bit32                          IndirectSMPReqLen;
  bit32                          IndirectSMPReqUpper32;
  bit32                          IndirectSMPReqLower32;  
  void                           *IndirectSMPResposMemHandle;
  void                           *IndirectSMPResp;
  bit32                          IndirectSMPRespLen;
  bit32                          IndirectSMPRespUpper32;
  bit32                          IndirectSMPRespLower32;  
#endif  
  bit32                          id;
  agsaContext_t                  agContext;
}  dmSMPRequestBody_t;


typedef struct dmIntContext_s {
  /**< agsaRoot_t->osData points to this */
  struct dmRootOsData_s      dmRootOsData;
  
  bit32               usecsPerTick;
#ifdef TBD
  dmRoot_t            dmRootInt;          /* for interrupt */
  dmRoot_t            dmRootNonInt;       /* for non-interrupt */
#endif

  agsaRoot_t          *agRoot;
  
  /**< software-related initialization params used in saInitialize() */
  dmSwConfig_t        SwConfig;  

  /**< timers used commonly in SAS/SATA */
  dmList_t                      timerlist;
  /**< pointer to PortContext memory;  */
  dmIntPortContext_t          *PortContextMem; 
   
  dmList_t                   FreePortContextList;
  dmList_t                   MainPortContextList;
  
  /**< pointer to Device memory */
  dmDeviceData_t             *DeviceMem;  
  dmList_t                   FreeDeviceList;
  dmList_t                   MainDeviceList;
  
  /**< pointer to Expander memory */
  dmExpander_t               *ExpanderMem; 
  dmList_t                   freeExpanderList;
  dmList_t                   mainExpanderList;
  
  /**< pointer to SMP command memory */
  dmSMPRequestBody_t         *SMPMem; 
  dmList_t                   freeSMPList;
  
  /**< pointer to Indirect SMP request/repsonse memory */
  bit8                       *IndirectSMPMem; 
  bit32                      IndirectSMPUpper32;
  bit32                      IndirectSMPLower32;
  bit32                      itNexusTimeout;
  bit32                      MaxRetryDiscovery;
  bit32                      RateAdjust;
    
}  dmIntContext_t;

typedef struct dmIntRoot_s
{
  /**<< common data structure for SAS/SATA */
  dmIntContext_t          dmAllShared;
} dmIntRoot_t;

#endif                          /* __DMTYPES_H__ */

