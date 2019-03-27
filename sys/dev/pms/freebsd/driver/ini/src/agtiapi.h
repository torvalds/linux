/*******************************************************************************
**
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
*
*INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
*ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
*SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
*OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
*WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
**
* $FreeBSD$
**
*******************************************************************************/

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>
#include <dev/pms/freebsd/driver/ini/src/agdef.h>
#include <dev/pms/freebsd/driver/common/lxcommon.h>
#ifdef AGTIAPI_ISCSI
#include "cmtypes.h"
#include "bktypes.h"
#endif
#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#endif
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <vm/uma.h>

typedef u_int32_t atomic_t;

#define atomic_set(p,v)		(*(p) = (v))
#define atomic_read(p)		(*(p))
#define atomic_inc(p)		atomic_add_int(p,1)
#define atomic_dec(p)		atomic_subtract_int(p,1)
#define atomic_add(n,p)		atomic_add_int(p,n)
#define atomic_sub(n,p)		atomic_subtract_int(p,n)

#define AGSCSI_INIT_XCHG_LEN  sizeof(tiScsiInitiatorRequest_t)
#define AGSMP_INIT_XCHG_LEN   sizeof(tiSMPFrame_t)  
#define CMND_DMA_UNMAP( pCard, cmnd )


// define PMC lean flags used for bit operations to track dev listing state
#define DPMC_LEANFLAG_NOAGDEVYT     2  // agDev handle not present yet
#define DPMC_LEANFLAG_NOWWNLIST     4  // WWNList entry not present
#define DPMC_LEANFLAG_AGDEVUSED     8  // agDev handle used
#define DPMC_LEANFLAG_PDEVSUSED    16  // pDevice slot used

typedef bus_dmamap_t dma_addr_t; // ##

#define timer_list callout

typedef struct ccb_hdr_s {
  void *next;
} ccb_hdr_t;


typedef struct _CCB {
  U32               targetId;
  U32               lun;
  U32               channel;
  U16               ccbStatus;
  U16               scsiStatus;
  U32               dataLen;
  U08               senseLen;
  U08               addrMode;
  U08               retryCount;
  U16               numSgElements;
  U32               flags;
  U32_64            dmaHandle;
  caddr_t           pSenseData;   // auto request sense data
  tiSgl_t          *sgList;           // [AGTIAPI_MAX_DMA_SEGS]
  bus_addr_t        tisgl_busaddr;
  //  dma_addr_t        sglDmaHandle;      // ## dmaHandle for sgList
  tiDeviceHandle_t *devHandle;
  struct _CCB      *pccbNext;
  struct _CCB      *pccbChainNext;    // forward link pointers
  struct scsi_cmnd *cmd;              // call back owner pointer
  struct _CCB      *pccbIO;           // for TM TARGET_RESET
  U32_64            startTime;
  tiIORequest_t     tiIORequest;
  tdIORequestBody_t tdIOReqBody;
  tiSuperScsiInitiatorRequest_t tiSuperScsiRequest;
  tiSMPFrame_t 		  tiSMPFrame;
#ifdef CCBUILD_TEST_EPL
  caddr_t           epl_ptr;
  dma_addr_t        epl_dma_ptr;
#endif

#ifdef CCBUILD_TEST_DPL
  caddr_t           dplPtr;
  dma_addr_t        dplDma;
#endif

#if defined (PERF_COUNT)
  u64               startCmnd;         // temp var to hold cmnd arrival
#endif
#ifdef ENABLE_NONSTANDARD_SECTORS
  caddr_t           metaPtr;
  dma_addr_t        dmaHandleMeta;
#endif
#ifdef ENABLE_SATA_DIF
  caddr_t           holePtr;
  dma_addr_t        dmaHandleHole;
  int               scaling_done;
#endif

#ifdef SUPER_FAST_IO_TEST 
  agsaIORequest_t      IoContext;
  agsaSASRequestBody_t sasRequestBody;
  u32                  reqType;
  u32                  queueId;
  agsaSgl_t           *sgl; // Used for esgl
#endif
  //new
  bus_dmamap_t	        CCB_dmamap;
  union ccb           *ccb; /* replacement of struct scsi_cmnd */
  struct agtiapi_softc *pmcsc;
 
} ccb_t, *pccb_t;


#define AGTIAPI_CCB_SIZE  sizeof(struct _CCB)

/*
typedef struct _ag_portal_data
{
  ag_portal_info_t    portalInfo;
  void                *pCard;
} ag_portal_data_t;
*/

typedef enum {
	DEK_TABLE_0 = 0,
	DEK_TABLE_1 = 1,
	DEK_TABLE_INVALID = DEK_MAX_TABLES,
     } dek_table_e;

typedef struct ag_encrypt_map_s {
	unsigned long long lbaMin;
	unsigned long long lbaMax;
	dek_table_e        dekTable;
	bit32	     	   dekIndex;
	bit32		   kekIndex;
	bit32		   kekTagCheck;
	bit32		   kekTag[2];
	struct list_head   *list;
    } ag_encrypt_map_t;

typedef struct ag_kek_table_s {
 #define KEK_TABLE_MAX_ENTRY  8
	bit32		   wrapperIndex;
	tiEncryptKekBlob_t kekBlob;
   } ag_kek_table_t;

typedef struct ag_dek_kek_map_s {
	bit32   	  kekIndex;
   } ag_dek_kek_map_t;

/*
** There is no LUN filed for the device structure.
** The reason is if the device is a single lun device, it
** will be lun 0.  If is a multi-lun device such as EMC 
** or Galaxi, only one device structure is associated with
** the device since only one device handler is provided.
*/
typedef struct _ag_device {
//#ifdef HOTPLUG_SUPPORT 
  /* used for hot-plug, temporarily either in new or removed devices list */
  LINK_NODE           devLink;  
//#endif
  U32                 targetId;
  U32                 flags;
  U16                 devType;
  U16                 resetCount;
  U32                 portalId;
  void               *pCard;
  U32                 sector_size;
  U32		      CCBCount;
#ifdef HOTPLUG_SUPPORT
  struct scsi_device *sdev;
#endif
  tiDeviceHandle_t   *pDevHandle;
  tiDeviceInfo_t      devInfo;
  ag_portal_info_t   *pPortalInfo;
  U08                 targetName[AGTIAPI_MAX_NAME];
  U16                 targetLen;
  U32		          qbusy;
  U32		          qdepth;
} ag_device_t;


/*      
** Use an array of these structures to map from assigned
** device target id (which is the index into the array) to
** the entry in the bd_devlist.
**
** Please note that an extra entry has been added to both
** the bd_devlist array and the bd_WWN_list.  This last
** entry is the "no mapping" entry -- used for initialization
** and to indicate an inactive entry.
*/
typedef struct _ag_tgt_map { 
  U16      devListIndex;
  U16      flags;
  U08      targetName[AGTIAPI_MAX_NAME];
  U16      targetLen;
  U08      portId;
  int      sasLrIdx; // Index into SAS Local/Remote list (part of extend-portID)
  uint32_t devRemoved; // when set, ghost target device is timing out
} ag_tgt_map_t;


// use an array of this struct to map local/remote dyads to ag_tgt_map_t
// entries
typedef struct _ag_slr_map {
  U08  localeName[AGTIAPI_MIN_NAME];
  U08  remoteName[AGTIAPI_MAX_NAME];
  int  localeNameLen;
  int  remoteNameLen;
} ag_slr_map_t;


#ifdef LINUX_PERBI_SUPPORT
// Use a list of these structures to hold target-WWN
// mapping assignments on the boot line during driver
// loading.
typedef struct _ag_mapping_s 
{
  struct _ag_mapping_s *next;
  U16                   targetId;
  U08                   cardNo;
  U08                   targetLen;
  U08                   targetName[AGTIAPI_MAX_NAME];
} ag_mapping_t;
#endif

typedef struct _ag_portal_data
{
  ag_portal_info_t    portalInfo;
  void               *pCard;
} ag_portal_data_t;


// The softc holds our per-instance data
struct agtiapi_softc {
  device_t            my_dev;
  struct cdev        *my_cdev;
  struct cam_sim     *sim;
  struct cam_path    *path;
  struct resource    *resirq;
  void               *intr_cookie;

  int                 rscID[MAX_MSIX_NUM_VECTOR];
  struct resource    *irq[MAX_MSIX_NUM_VECTOR];
  void               *intrcookie[MAX_MSIX_NUM_VECTOR];

  // timer stuff; mc lean
  bus_dma_tag_t       buffer_dmat;
  struct cam_devq    *devq;
  struct callout      OS_timer;
  struct mtx          OS_timer_lock;
  struct callout      IO_timer;
  struct mtx          IO_timer_lock;
  struct callout      devRmTimer;
  struct mtx          devRmTimerLock;
  uint16_t            rmChkCt;

  // for tiSgl_t memory
  tiSgl_t            *tisgl_mem;
  bus_addr_t          tisgl_busaddr;
  bus_dma_tag_t       tisgl_dmat;
  bus_dmamap_t        tisgl_map;

  // for ostiAllocMemory() pre allocation pool
  void               *osti_mem;
  bus_addr_t          osti_busaddr;
  bus_dma_tag_t       osti_dmat;
  bus_dmamap_t        osti_mapp;

  // pre-allocation pool
  U32                 typhn; // size needed
  void               *typh_mem;
  bus_addr_t          typh_busaddr;
  bus_dma_tag_t       typh_dmat;
  bus_dmamap_t        typh_mapp;
  U32                 typhIdx;
  U32                 tyPhsIx;


  // begin ag_card_t references (AKA pCard)
  struct Scsi_Host   *pHost;
  tiRoot_t            tiRoot;             // tiRoot for the card
  U32                 VidDid;
  U32                 SVID_SSID;
  U32                 flags;              // keep track of state
  U32                 freezeSim;
  U32                 up_count;
  U32                 down_count;
  U08                 hostNo;             // host number signed by OS
  U08                 cardNo;             // host no signed by driver
  U16                 tgtCount;           // total target devices
  U16                 badTgtCount;        // total bad target devices
  U16                 activeCCB;          // number of active CCB
  U32                 ccbTotal;           // total # of CCB allocated
  U32                 devDiscover;        // # of device to be discovered
  U32                 resetCount;
  U32                 timeoutTicks;
  U32                 portCount;          // portal count
  U32                 SimQFrozen;         // simq frozen state
  U32                 devq_flag;      //device busy flag 
  U32                 dev_scan;           //device ready 
  pccb_t              ccbSendHead;        // CCB send list head
  pccb_t              ccbSendTail;        // CCB send list tail
  pccb_t              ccbDoneHead;        // CCB done list head
  pccb_t              ccbDoneTail;        // CCB done list tail
  pccb_t              smpSendHead;        // CCB send list head
  pccb_t              smpSendTail;        // CCB send list tail
  pccb_t              smpDoneHead;        // CCB done list head
  pccb_t              smpDoneTail;        // CCB done list tail
  caddr_t            *ccbChainList;       // ccb chain list head
  caddr_t            *ccbFreeList;        // free ccb list head
  ccb_hdr_t          *ccbAllocList;       // ### ccb allocation chain list head
  struct pci_pool    *sglPool;            // for SGL pci_alloc_consistent
  struct timer_list   osTimerList;        // card timer list
#ifdef TD_TIMER
  struct timer_list   tdTimerList;        // tdlayer timer list
#endif
  struct timer_list   tiTimerList;        // tilayer timer list
  ag_portal_data_t   *pPortalData;        // wrapper
  ag_card_info_t     *pCardInfo;
  ag_device_t        *pDevList;

#define CIPHER_MODE_INVALID 0xffffffffUL
#define DEK_INDEX_INVALID   0xffffffffUL
#define KEK_INDEX_INVALID   0xffffffffUL
  int                 encrypt;            // enable/disable encryption flag
  bit32               dek_size;           // size of dek
  void               *ioctl_data;         // encryption ioctl data pointer

  struct list_head   *encrypt_map;        // encryption map
  ag_kek_table_t      kek_table[KEK_TABLE_MAX_ENTRY];
  // KEK table
  ag_dek_kek_map_t    dek_kek_map[DEK_MAX_TABLES][DEK_MAX_TABLE_ITEMS];
  // storage for dek index in tables (sysfs)
  int                 dek_index[2];
#define DEK_SIZE_PLAIN   72
#define DEK_SIZE_ENCRYPT 80
#define ENCRYPTION_MAP_MEMPOOL_SIZE 64
  char                map_cache_name[32]; // name of mapping memory pool
  struct kmem_cache  *map_cache;          // handle to mapping cache
  bit32               cipher_mode;        // storage of cipher mode
#define ENCRYPTION_IO_ERR_MEMPOOL_SIZE 256
  struct mtx          ioerr_queue_lock;
  char                ioerr_cache_name[32];
  struct kmem_cache  *ioerr_cache;        // handle to IO error cache

//#ifdef LINUX_PERBI_SUPPORT
  ag_tgt_map_t       *pWWNList;
  ag_slr_map_t       *pSLRList;           // SAS Local/Remote map list
  U32                 numTgtHardMapped;   // hard mapped target number
//#endif
  struct sema  		 *pIoctlSem;         // for ioctl sync.
  U32_64              osLockFlag;         // flag for oslayer spin lock TBU
#ifdef AGTIAPI_LOCAL_LOCK
  struct mtx          sendLock;           // local queue lock
  struct mtx          doneLock;           // local queue lock
  struct mtx          sendSMPLock;        // local queue lock
  struct mtx          doneSMPLock;        // local queue lock
  struct mtx          ccbLock;            // ccb list lock
  struct mtx         *STLock;             // Low Level & TD locks
  unsigned long      *STLockFlags;        // Low Level & TD locks flags
  struct mtx          memLock;            // dynamic memory allocation lock
  struct mtx          freezeLock;
#endif
#ifdef AGTIAPI_DPC                        // card deferred intr process tasklet
  struct callout      tasklet_dpc[MAX_MSIX_NUM_DPC];
#endif
//#ifdef HOTPLUG_SUPPORT
  struct mtx          devListLock;        // device lists lock
//#endif

};

int agtiapi_getdevlist( struct agtiapi_softc *pCard,
                        tiIOCTLPayload_t *agIOCTLPayload );
int agtiapi_getCardInfo ( struct agtiapi_softc      *pCard,
                          U32_64			size,
                          void			      *buffer );

#ifndef LINUX_PERBI_SUPPORT
#define INDEX(_pCard, _T)   (_T)
#else
#define INDEX(_pCard, _T)   (((_pCard)->pWWNList + (_T))->devListIndex)
#endif

