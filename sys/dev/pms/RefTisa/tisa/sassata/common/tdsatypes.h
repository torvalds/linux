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
 *
 * The file defines data structures for SAS/SATA TD layer
 *
 */

#ifndef __TDSATYPES_H
#define __TDSATYPES_H

#define ESGL_PAGES_SIZE sizeof(agsaEsgl_t) /** the esgl page size */
#define NUM_ESGL_PAGES 0 /* old value 512 */  /**< the default number of esgl pages */


/**< target device type */
#define TD_DEFAULT_DEVICE 0
#define TD_SAS_DEVICE 1
#define TD_SATA_DEVICE 2

#include <dev/pms/RefTisa/tisa/sassata/common/tdioctl.h>


/** \brief data structure for SATA Host
 *
 * to be developed
 *
 */
typedef struct itdstHost_s
{
  int i;
} itdstHost_t;

/** \brief data structure for SATA Device
 *
 * to be developed
 *
 */
typedef struct ttdstDevice_s
{
  int i;
} ttdstDevice_t;

#ifdef INITIATOR_DRIVER
typedef struct itdsaIni_s {
  /**< point to the shared structure bothe SAS and SATA */
  struct tdsaContext_s           *tdsaAllShared;
  itdssOperatingOption_t         OperatingOption;
  tdSCSIStatusCount_t            ScsiStatusCounts;
  tdSenseKeyCount_t              SenseKeyCounter;
  bit32                          NumIOsActive;
  /* the list of initiator timer; upon expiration timer CB fn is called
     eg) itdProcessTimers()
   */
  tdList_t                       timerlist;
} itdsaIni_t;
#endif

struct ttdsaXchgAllocFreeInfoList_s;

#ifdef TARGET_DRIVER
typedef struct ttdsaTgt_s {
  /* point to the shared across SAS and SATA */
  struct tdsaContext_s              *tdsaAllShared;
  ttdssOperatingOption_t            OperatingOption;
  tiTargetOption_t                  tiOperatingOption;
  ttdsaXchgData_t                   ttdsaXchgData;
#ifdef PASSTHROUGH
  /* registered passthrough CB */
  ostiProcessPassthroughCmnd_t      PasthroughCB;
#endif
} ttdsaTgt_t;
#endif


/** \brief data structure for SATA Device
 *
 * not in use yet. just a place holderto be develped
 *
 */
typedef struct tdHardwareInfo_s {
  bit16         DeviceID;
  bit16         VendorID;
  bit8          ChipRev;
  bit32         PciFunctionNumber;
  bit32         FlashRomPresent;
} tdHardwareInfo_t;


/** \brief data structure for ESLG page
 *
 * This data structure describes the ESGL page maintained in TD layer.
 * One important field is agEsgl which is pointer to assaEsgl_t data structure,
 * which describes ESGL used in lower layer.
 * Memory for this data structure is allocated using tiTdSharedMem_t data
 * structure. However, Memory for agsaEsgl_t data structure is allocated using
 * tiLoLevelResource_t.
 *
 */
typedef struct tdsaEsglPageInfo_s {
  tdList_t   tdlist;            /**< pointers to next and previous pages */
  bit32      physAddressUpper;  /**< upper physical address of the page */
  bit32      physAddressLower;  /**< lower physical address of the page */
  bit32      len;
  agsaEsgl_t *agEsgl;
  bit32      id;                /**< for debugging only */
} tdsaEsglPageInfo_t;

/** \brief data structure for ESLG page pool
 *
 * This data structure describes the pool of esgl pages
 *
 */
typedef struct tdsaEsglPagePool_s {
  tdsaEsglPageInfo_t EsglPages[1];  /**< variable size array */
} tdsaEsglPagePool_t;


/** \brief data structure for ESGL pool information
 *
 * This data structure maintains information about ESGL pool. For example, this
 * data structure maintains the number of free and total ESGL pages and uses
 * tdList_t data structure for listing of ESGL pages.
 *
 */
typedef struct tdsaEsglAllInfo_s {
  /*
     used in tdGetEsglPages()
   */
  tdList_t             mainlist; /* not used */
  tdList_t             freelist;
  bit32                NumEsglPages;
  bit32                NumFreeEsglPages;
  bit32                EsglPageSize;
  bit32                physAddrUpper;
  bit32                physAddrLower;
  void                 *virtPtr;
  tdsaEsglPagePool_t   *EsglPagePool;
} tdsaEsglAllInfo_t;

typedef struct smp_pass_through_req
{
		bit8 exp_sas_addr[8];			//Storing the 16 digit expander SAS-address
		bit32 smp_req_len;				//Length of the request frame
		bit32 smp_resp_len; 			//Length of the response frame
		bit8 smp_req_resp[1]; 				//Pointer to the request-response frame
}smp_pass_through_req_t;

#ifdef TD_INT_COALESCE
typedef struct tdsaIntCoalesceContext_s {
  tdList_t                  MainLink;   /* free */
  tdList_t                  FreeLink; /* in use */
  struct tdsaContext_s      *tdsaAllShared;
#ifdef OS_INT_COALESCE
  tiIntCoalesceContext_t    *tiIntCoalesceCxt;
#endif
  agsaIntCoalesceContext_t  agIntCoalCxt;
  /* for debug */
  bit32                     id;

} tdsaIntCoalesceContext_t;
#endif

typedef struct tdsaHwEventSource_s {
  bit32                 EventValid;
  agsaEventSource_t     Source;
} tdsaHwEventSource_t;

/** \brief data structure for SAS/SATA context at TD layer
 *
 * This data structure is used for both SAS and SATA.
 * In addition, this is the data structure used mainly to communicate with
 * lower layer.
 *
 */
typedef struct tdsaContext_s {
  bit32                 currentOperation;

  /**< agsaRoot_t->osData points to this */
  struct tdsaRootOsData_s      agRootOsDataForInt;     /* for interrupt */
  struct tdsaRootOsData_s      agRootOsDataForNonInt;  /* for non-interrupt */

  agsaRoot_t            agRootInt;          /* for interrupt */
  agsaRoot_t            agRootNonInt;       /* for non-interrupt */

  /* flags values commonly used for both SAS and SATA */
  struct tdsaComMemFlags_s       flags;


  /**< software-related initialization params used in saInitialize() */
  agsaSwConfig_t        SwConfig;

  /**< Queue-related initialization params used in saInitialize() */
  agsaQueueConfig_t     QueueConfig;

  /**< hardware-related initialization params used in saInitialize() */
  agsaHwConfig_t        HwConfig;


  /**< Copy of TI low level resoure */
  tiLoLevelResource_t   loResource;

  /* information of ESGL pages allocated
  tdsaEsglAllInfo_t          EsglAllInfo;
  */

  /*  hardware information; just place holder
  tdHardwareInfo_t      hwInfo;
  */

  bit32                 currentInterruptDelay;

  /**< timers used commonly in SAS/SATA */
  tdList_t                      timerlist;
  /***********************************************************************/
  /* used to be in tdssContext_t  tdssSASShared;*/
  struct itdsaIni_s          *itdsaIni; /* Initiator; */
  struct ttdsaTgt_s          *ttdsaTgt; /* Target */
  /**< pointer to PortContext memory;  */
  tdsaPortContext_t          *PortContextMem;
  /**< pointer to Device memory */
  tdsaDeviceData_t           *DeviceMem;

  tdList_t                   FreePortContextList;
  tdList_t                   MainPortContextList;
  tdList_t                   FreeDeviceList;
  tdList_t                   MainDeviceList;

  /**< actual storage for jump table */
  tdsaJumpTable_t            tdJumpTable;
  /**< Local SAS port start information such as ID addr */
  tdsaPortStartInfo_t        Ports[TD_MAX_NUM_PHYS];
  /***********************************************************************/
  /**< storage for FW download contents */
  tdFWControlEx_t              tdFWControlEx;
#ifdef SPC_ENABLE_PROFILE
  tdFWProfileEx_t              tdFWProfileEx;
#endif
#ifdef TD_INT_COALESCE
  tdsaIntCoalesceContext_t   *IntCoalesce;
#endif

  /* first time a card is processed set this true */
  bit32 first_process;

  /* expander list */
  tdsaExpander_t             *ExpanderHead;
  //  tdList_t                   discoveringExpanderList;
  tdList_t                   freeExpanderList;
    bit32                      phyCount;
  bit32                      IBQnumber;
  bit32                      OBQnumber;
  bit32                      InboundQueueSize[AGSA_MAX_OUTBOUND_Q];
  bit32                      InboundQueueEleSize[AGSA_MAX_OUTBOUND_Q];
  bit32                      OutboundQueueSize[AGSA_MAX_OUTBOUND_Q];
  bit32                      OutboundQueueEleSize[AGSA_MAX_OUTBOUND_Q];
  bit32                      OutboundQueueInterruptDelay[AGSA_MAX_OUTBOUND_Q];
  bit32                      OutboundQueueInterruptCount[AGSA_MAX_OUTBOUND_Q];
  bit32                      OutboundQueueInterruptEnable[AGSA_MAX_OUTBOUND_Q];
  bit32                      InboundQueuePriority[AGSA_MAX_INBOUND_Q];
  bit32                      QueueOption;
  bit32                      tdDeviceIdVendId;
  bit32                      tdSubVendorId;
  /* instance number */
  bit8                  CardIDString[TD_CARD_ID_LEN];
  bit32                 CardID;
#ifdef VPD_TESTING
  /* temp; for testing VPD indirect */
  bit32                 addrUpper;
  bit32                 addrLower;
#endif

  bit32                 resetCount;
  tdsaHwEventSource_t   eventSource[TD_MAX_NUM_PHYS];
  bit32                 portTMO; /* in 100ms */
  bit32                 phyCalibration; /* enables or disables phy calibration */
  bit32                 FCA; /* force to clear affiliation by sending SMP HARD RESET */
  bit32                 SMPQNum; /* first high priority queue number for SMP */
  bit32                 ResetInDiscovery; /* hard/link reset in discovery */
  bit32                 FWMaxPorts;
  bit32                 IDRetry; /* SATA ID failurs are retired */
  bit32                 RateAdjust; /* allow retry open with lower connection rate */
#ifdef AGTIAPI_CTL
  bit16                 SASConnectTimeLimit; /* used by tdsaCTLSet() */
#endif
  bit32                 MaxNumOSLocks; /* max number of OS layer locks */
  bit32                 MaxNumLLLocks; /* max num of LL locks */
  bit32                 MaxNumLocks;   /* max num of locks for layers and modules (LL, TDM, SATM, DM) */
#ifdef FDS_DM
  bit32                 MaxNumDMLocks; /* max num of DM locks */
  dmRoot_t              dmRoot; /* discovery root */
  dmSwConfig_t          dmSwConfig;
#endif
#ifdef FDS_SM
  bit32                 MaxNumSMLocks; /* max num of SM locks */
  smRoot_t              smRoot; /* SATM root */
  smSwConfig_t          smSwConfig;
#endif
  bit32                 MCN; /* temp; only for testing and to be set by registry or adj file */
  bit32                 sflag; /* Sflag bit */
#ifdef CCFLAGS_PHYCONTROL_COUNTS
  agsaPhyAnalogSetupRegisters_t analog[TD_MAX_NUM_PHYS];
#endif /* CCFLAGS_PHYCONTROL_COUNTS */
  bit32                 stp_idle_time; /* stp idle time for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 STP_MCT_TMO; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 SSP_MCT_TMO; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 MAX_OPEN_TIME; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 SMP_MAX_CONN_TIMER; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 STP_FRM_TMO; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 MFD; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 OPNRJT_RTRY_INTVL; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 DOPNRJT_RTRY_TMO; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 COPNRJT_RTRY_TMO; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 DOPNRJT_RTRY_THR; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 COPNRJT_RTRY_THR; /*  for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  bit32                 itNexusTimeout;   /*  set by registry */
  bit32                 MAX_AIP;   /* for agsaSASProtocolTimerConfigurationPage_t; set by registry or adj file */
  agsaMPIContext_t MpiOverride;
#ifdef TI_GETFOR_ONRESET
  bit8   FatalErrorData[(5 * (1024 * 1024))];
#endif /* TI_GETFOR_ONRESET */
  bit32	 sgpioResponseSet;    /*Used to sync between SGPIO Req and Resp */
  volatile  NvmdResponseSet;
}  tdsaContext_t;

#ifdef FAST_IO_TEST
#define CMDS_PER_IO_IOPS  1
#define CMDS_PER_IO_DUP   1 //4
#endif

/** \brief the root data structure for TD layer
 *
 * This data structure is the main data structure used in communicating
 * with OS layer. For example, tiRoot_t->tdData points to this data structure
 * From this data structure, SATA host/Device and SAS initiator/target are found.
 *
 */
typedef struct tdsaRoot_s
{
  /**<< common data structure for SAS/SATA */
  tdsaContext_t          tdsaAllShared;
  bit32                  autoGoodRSP;
#ifdef INITIATOR_DRIVER
  itdsaIni_t             *itdsaIni; /**< SAS/SATA initiator */
#endif
#ifdef TARGET_DRIVER
  ttdsaTgt_t             *ttdsaTgt; /**< SAS/SATA target    */
#endif
}  tdsaRoot_t;

typedef struct tmf_pass_through_req
{
    bit8    pathId;
    bit8    targetId;
    bit8    lun;
}tmf_pass_through_req_t;

/* Context Field accessors */
#define TD_GET_TIROOT(sa_root)         (((tdsaRootOsData_t *)(sa_root)->osData)->tiRoot)
#define TD_GET_TDROOT(ti_root)         ((tdsaRoot_t *)(ti_root)->tdData)
#define TD_GET_TICONTEXT(ti_root)      ((tdsaContext_t *)&TD_GET_TDROOT(ti_root)->tdsaAllShared)
#define TD_GET_TIINI_CONTEXT(ti_root)  ((itdsaIni_t *)TD_GET_TICONTEXT(ti_root)->itdsaIni)
#define TD_GET_TITGT_CONTEXT(ti_root)  ((ttdsaTgt_t *)TD_GET_TICONTEXT(ti_root)->ttdsaTgt)
#endif /* __TDSATYPES_H */
