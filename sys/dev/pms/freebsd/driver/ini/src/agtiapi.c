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
*******************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#define MAJOR_REVISION	    1
#define MINOR_REVISION	    3
#define BUILD_REVISION	    10800

#include <sys/param.h>      // defines used in kernel.h
#include <sys/ioccom.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>     // types used in module initialization
#include <sys/conf.h>       // cdevsw struct
#include <sys/uio.h>        // uio struct
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/bus.h>        // structs, prototypes for pci bus stuff
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <vm/vm.h>          // 1. for vtophys
#include <vm/pmap.h>        // 2. for vtophys
#include <dev/pci/pcivar.h> // For pci_get macros
#include <dev/pci/pcireg.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sema.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <machine/atomic.h>
#include <sys/libkern.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h> //
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/freebsd/driver/ini/src/agtiapi.h>
#include <dev/pms/freebsd/driver/ini/src/agtiproto.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/freebsd/driver/common/lxencrypt.h> 

MALLOC_DEFINE( M_PMC_MCCB, "CCB List", "CCB List for PMCS driver" );

MALLOC_DEFINE( M_PMC_MSTL, "STLock malloc",
               "allocated in agtiapi_attach as memory for lock use" );
MALLOC_DEFINE( M_PMC_MDVT, "ag_device_t malloc",
               "allocated in agtiapi_attach as mem for ag_device_t pDevList" );
MALLOC_DEFINE( M_PMC_MPRT, "ag_portal_data_t malloc",
               "allocated in agtiapi_attach as mem for *pPortalData" );
MALLOC_DEFINE( M_PMC_MDEV, "tiDeviceHandle_t * malloc",
               "allocated in agtiapi_GetDevHandle as local mem for **agDev" );
MALLOC_DEFINE( M_PMC_MFLG, "lDevFlags * malloc",
               "allocated in agtiapi_GetDevHandle as local mem for * flags" );
#ifdef LINUX_PERBI_SUPPORT
MALLOC_DEFINE( M_PMC_MSLR, "ag_slr_map_t malloc",
               "mem allocated in agtiapi_attach for pSLRList" );
MALLOC_DEFINE( M_PMC_MTGT, "ag_tgt_map_t malloc",
               "mem allocated in agtiapi_attach for pWWNList" );
#endif
MALLOC_DEFINE(TEMP,"tempbuff","buffer for payload");
MALLOC_DEFINE(TEMP2, "tempbuff", "buffer for agtiapi_getdevlist");
STATIC U32  agtiapi_intx_mode    = 0;
STATIC U08  ag_Perbi             = 0;
STATIC U32  agtiapi_polling_mode = 0;
STATIC U32  ag_card_good         = 0;   // * total card initialized
STATIC U32  ag_option_flag       = 0;   // * adjustable parameter flag
STATIC U32  agtiapi_1st_time     = 1;
STATIC U32  ag_timeout_secs      = 10;  //Made timeout equivalent to linux

U32         gTiDebugLevel        = 1;
S32	        ag_encryption_enable = 0;
atomic_t    outstanding_encrypted_io_count;

#define cache_line_size() CACHE_LINE_SIZE

#define PMCoffsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define CPU_TO_LE32(dst, src)                  \
    dst.lower = htole32(LOW_32_BITS(src)); \
    dst.upper = htole32(HIGH_32_BITS(src))

#define CMND_TO_CHANNEL( ccb )     ( ccb->ccb_h.path_id )
#define CMND_TO_TARGET(  ccb )     ( ccb->ccb_h.target_id )
#define CMND_TO_LUN(     ccb )     ( ccb->ccb_h.target_lun )

STATIC U08 agtiapi_AddrModes[AGTIAPI_MAX_CHANNEL_NUM + 1] = 
      { AGTIAPI_PERIPHERAL };

#ifdef LINUX_PERBI_SUPPORT
// Holding area for target-WWN mapping assignments on the boot line
static ag_mapping_t *agMappingList = NULL;  // modified by agtiapi_Setup()
#endif

// * For Debugging Purpose 
#ifdef AGTIAPI_DEBUG
#define AGTIAPI_WWN(name, len)   wwnprintk(name, len)
#else
#define AGTIAPI_WWN(name, len)
#endif


#define AGTIAPI_WWNPRINTK(name, len, format, a...)     \
          AGTIAPI_PRINTK(format "name ", a);           \
          AGTIAPI_WWN((unsigned char*)name, len);

#define AGTIAPI_ERR_WWNPRINTK(name, len, format, a...) \
          printk(KERN_DEBUG format "name ", ## a);     \
          wwnprintk((unsigned char*)name, len);
#define AGTIAPI_CPY_DEV_INFO(root, dev, pDev)            \
          tiINIGetDeviceInfo(root, dev, &pDev->devInfo); \
          wwncpy(pDev);

#ifdef AGTIAPI_LOCAL_LOCK

#define AG_CARD_LOCAL_LOCK(lock)     ,(lock)
#define AG_SPIN_LOCK_IRQ(lock, flags)
#define AG_SPIN_UNLOCK_IRQ(lock, flags)
#define AG_SPIN_LOCK(lock)
#define AG_SPIN_UNLOCK(lock)
#define AG_GLOBAL_ARG(arg)
#define AG_PERF_SPINLOCK(lock)
#define AG_PERF_SPINLOCK_IRQ(lock, flags)


#define AG_LOCAL_LOCK(lock)     if (lock) \
                                         mtx_lock(lock)
#define AG_LOCAL_UNLOCK(lock)   if (lock) \
                                         mtx_unlock(lock)
#define AG_LOCAL_FLAGS(_flags)         unsigned long _flags = 0
#endif


#define AG_GET_DONE_PCCB(pccb, pmcsc)            \
  {                                              \
    AG_LOCAL_LOCK(&pmcsc->doneLock);             \
    pccb = pmcsc->ccbDoneHead;                   \
    if (pccb != NULL)                            \
    {                                            \
      pmcsc->ccbDoneHead = NULL;                 \
      pmcsc->ccbDoneTail = NULL;                 \
      AG_LOCAL_UNLOCK(&pmcsc->doneLock);         \
      agtiapi_Done(pmcsc, pccb);                 \
    }                                            \
    else                                         \
      AG_LOCAL_UNLOCK(&pmcsc->doneLock);         \
  }

#define AG_GET_DONE_SMP_PCCB(pccb, pmcsc)	\
  {                                              \
    AG_LOCAL_LOCK(&pmcsc->doneSMPLock);          \
    pccb = pmcsc->smpDoneHead;                   \
    if (pccb != NULL)                            \
    {                                            \
      pmcsc->smpDoneHead = NULL;                 \
      pmcsc->smpDoneTail = NULL;                 \
      AG_LOCAL_UNLOCK(&pmcsc->doneSMPLock);      \
      agtiapi_SMPDone(pmcsc, pccb);              \
    }                                            \
    else                                         \
      AG_LOCAL_UNLOCK(&pmcsc->doneSMPLock);      \
  }

#ifdef AGTIAPI_DUMP_IO_DEBUG
#define AG_IO_DUMPCCB(pccb)    agtiapi_DumpCCB(pccb)
#else
#define AG_IO_DUMPCCB(pccb)
#endif

#define SCHED_DELAY_JIFFIES 4 /* in seconds */

#ifdef HOTPLUG_SUPPORT
#define AG_HOTPLUG_LOCK_INIT(lock)   mxt_init(lock)
#define AG_LIST_LOCK(lock)           mtx_lock(lock)
#define AG_LIST_UNLOCK(lock)         mtx_unlock(lock)
#else
#define AG_HOTPLUG_LOCK_INIT(lock)
#define AG_LIST_LOCK(lock)
#define AG_LIST_UNLOCK(lock)
#endif

STATIC void agtiapi_CheckIOTimeout(void *data);



static ag_card_info_t agCardInfoList[ AGTIAPI_MAX_CARDS ]; // card info list
static void agtiapi_cam_action( struct cam_sim *, union ccb * );
static void agtiapi_cam_poll( struct cam_sim * );

// Function prototypes
static d_open_t  agtiapi_open;
static d_close_t agtiapi_close;
static d_read_t  agtiapi_read;
static d_write_t agtiapi_write;
static d_ioctl_t agtiapi_CharIoctl;
static void agtiapi_async(void *callback_arg, u_int32_t code,
              struct cam_path *path, void *arg);
void agtiapi_adjust_queue_depth(struct cam_path *path, bit32 QueueDepth);

// Character device entry points
static struct cdevsw agtiapi_cdevsw = {
  .d_version = D_VERSION,
  .d_open    = agtiapi_open,
  .d_close   = agtiapi_close,
  .d_read    = agtiapi_read,
  .d_write   = agtiapi_write,
  .d_ioctl   = agtiapi_CharIoctl,
  .d_name    = "pmspcv",
};

U32 maxTargets = 0;
U32 ag_portal_count = 0;

// In the cdevsw routines, we find our softc by using the si_drv1 member
// of struct cdev. We set this variable to point to our softc in our
// attach routine when we create the /dev entry.

int agtiapi_open( struct cdev *dev, int oflags, int devtype, struct thread *td )
{
  struct agtiapi_softc *sc;
  /* Look up our softc. */
  sc = dev->si_drv1;
  AGTIAPI_PRINTK("agtiapi_open\n");
  AGTIAPI_PRINTK("Opened successfully. sc->my_dev %p\n", sc->my_dev);
  return( 0 );
}

int agtiapi_close( struct cdev *dev, int fflag, int devtype, struct thread *td )
{
  struct agtiapi_softc *sc;
  // Look up our softc
  sc = dev->si_drv1;
  AGTIAPI_PRINTK("agtiapi_close\n");
  AGTIAPI_PRINTK("Closed. sc->my_dev %p\n", sc->my_dev);
  return( 0 );
}

int agtiapi_read( struct cdev *dev, struct uio *uio, int ioflag )
{
  struct agtiapi_softc *sc;
  // Look up our softc
  sc = dev->si_drv1;
  AGTIAPI_PRINTK( "agtiapi_read\n" );
  AGTIAPI_PRINTK( "Asked to read %lu bytes. sc->my_dev %p\n",
                  uio->uio_resid, sc->my_dev );
  return( 0 );
}

int agtiapi_write( struct cdev *dev, struct uio *uio, int ioflag )
{
  struct agtiapi_softc *sc;
  // Look up our softc
  sc = dev->si_drv1;
  AGTIAPI_PRINTK( "agtiapi_write\n" );
  AGTIAPI_PRINTK( "Asked to write %lu bytes. sc->my_dev %p\n",
                  uio->uio_resid, sc->my_dev );
  return( 0 );
}

int agtiapi_getdevlist( struct agtiapi_softc *pCard,
                        tiIOCTLPayload_t *agIOCTLPayload )
{
  tdDeviceListPayload_t *pIoctlPayload =
    (tdDeviceListPayload_t *) agIOCTLPayload->FunctionSpecificArea;
  tdDeviceInfoIOCTL_t *pDeviceInfo = NULL;
  bit8		   *pDeviceInfoOrg;
  tdsaDeviceData_t *pDeviceData = NULL;
  tiDeviceHandle_t **devList = NULL;
  tiDeviceHandle_t **devHandleArray = NULL;
  tiDeviceHandle_t *pDeviceHandle = NULL;
  bit32 x, memNeeded1;
  bit32 count, total;
  bit32 MaxDeviceCount;
  bit32 ret_val=IOCTL_CALL_INVALID_CODE;
  ag_portal_data_t *pPortalData;
  bit8 *pDeviceHandleList = NULL;
  AGTIAPI_PRINTK( "agtiapi_getdevlist: Enter\n" );
  
  pDeviceInfoOrg = pIoctlPayload -> pDeviceInfo;
  MaxDeviceCount = pCard->devDiscover;
  if (MaxDeviceCount > pIoctlPayload->deviceLength )
  {   
    AGTIAPI_PRINTK( "agtiapi_getdevlist: MaxDeviceCount: %d > Requested device length: %d\n", MaxDeviceCount, pIoctlPayload->deviceLength );
    MaxDeviceCount = pIoctlPayload->deviceLength;
    ret_val = IOCTL_CALL_FAIL;
  }
  AGTIAPI_PRINTK( "agtiapi_getdevlist: MaxDeviceCount: %d > Requested device length: %d\n", MaxDeviceCount, pIoctlPayload->deviceLength );
  memNeeded1 = AG_ALIGNSIZE( MaxDeviceCount * sizeof(tiDeviceHandle_t *),
                             sizeof(void *) );
  AGTIAPI_PRINTK("agtiapi_getdevlist: portCount %d\n", pCard->portCount);
  devList = malloc(memNeeded1, TEMP2, M_WAITOK); 
  if (devList == NULL)
  {
    AGTIAPI_PRINTK("agtiapi_getdevlist: failed to allocate memory\n");
    ret_val = IOCTL_CALL_FAIL;
    agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
    return ret_val;
  }
  osti_memset(devList, 0,  memNeeded1);
  pPortalData = &pCard->pPortalData[0];
  pDeviceHandleList = (bit8*)devList;
  for (total = x = 0; x < pCard->portCount; x++, pPortalData++)
  {
    count = tiINIGetDeviceHandlesForWinIOCTL(&pCard->tiRoot,
                    &pPortalData->portalInfo.tiPortalContext,
		    ( tiDeviceHandle_t **)pDeviceHandleList ,MaxDeviceCount );
    if (count == DISCOVERY_IN_PROGRESS)
    {
      AGTIAPI_PRINTK( "agtiapi_getdevlist: DISCOVERY_IN_PROGRESS on "
                      "portal %d\n", x );
      free(devList, TEMP2);
      ret_val = IOCTL_CALL_FAIL;
      agIOCTLPayload->Status = IOCTL_ERR_STATUS_INTERNAL_ERROR;
      return ret_val;
    }
    total += count;
    pDeviceHandleList+= count*sizeof(tiDeviceHandle_t *);
    MaxDeviceCount-= count;
  }
  if (total > pIoctlPayload->deviceLength)
  {
    total = pIoctlPayload->deviceLength;
  }
  // dump device information from device handle list
  count = 0;
  
  devHandleArray = devList;
  for (x = 0; x < pCard->devDiscover; x++)
  {
     pDeviceHandle = (tiDeviceHandle_t*)devHandleArray[x];
    if (devList[x] != agNULL)
    {
      pDeviceData = devList [x]->tdData;
    
	pDeviceInfo = (tdDeviceInfoIOCTL_t*)(pDeviceInfoOrg + sizeof(tdDeviceInfoIOCTL_t) * count);
      if (pDeviceData != agNULL && pDeviceInfo != agNULL)
      {
        osti_memcpy( &pDeviceInfo->sasAddressHi,
                     pDeviceData->agDeviceInfo.sasAddressHi,
                     sizeof(bit32) );
        osti_memcpy( &pDeviceInfo->sasAddressLo,
                     pDeviceData->agDeviceInfo.sasAddressLo,
                     sizeof(bit32) );
#if 0
        pDeviceInfo->sasAddressHi =
          DMA_BEBIT32_TO_BIT32( pDeviceInfo->sasAddressHi );
        pDeviceInfo->sasAddressLo =
          DMA_BEBIT32_TO_BIT32( pDeviceInfo->sasAddressLo );
#endif

        pDeviceInfo->deviceType =
          ( pDeviceData->agDeviceInfo.devType_S_Rate & 0x30 ) >> 4;
        pDeviceInfo->linkRate   =
          pDeviceData->agDeviceInfo.devType_S_Rate & 0x0F;
        pDeviceInfo->phyId      =  pDeviceData->phyID;
 	pDeviceInfo->ishost	=  pDeviceData->target_ssp_stp_smp;
	pDeviceInfo->DeviceHandle= (unsigned long)pDeviceHandle;
	if(pDeviceInfo->deviceType == 0x02)
	{
	   bit8 *sasAddressHi;
	   bit8 *sasAddressLo;
	   tiIniGetDirectSataSasAddr(&pCard->tiRoot, pDeviceData->phyID, &sasAddressHi, &sasAddressLo);
	   pDeviceInfo->sasAddressHi = DMA_BEBIT32_TO_BIT32(*(bit32*)sasAddressHi);
	   pDeviceInfo->sasAddressLo = DMA_BEBIT32_TO_BIT32(*(bit32*)sasAddressLo) + pDeviceData->phyID + 16;
	}
	else
	{
        pDeviceInfo->sasAddressHi =
          DMA_BEBIT32_TO_BIT32( pDeviceInfo->sasAddressHi );
        pDeviceInfo->sasAddressLo =
          DMA_BEBIT32_TO_BIT32( pDeviceInfo->sasAddressLo );
 	}

        AGTIAPI_PRINTK( "agtiapi_getdevlist: devicetype %x\n",
                        pDeviceInfo->deviceType );
        AGTIAPI_PRINTK( "agtiapi_getdevlist: linkrate %x\n",
                        pDeviceInfo->linkRate );
        AGTIAPI_PRINTK( "agtiapi_getdevlist: phyID %x\n",
                        pDeviceInfo->phyId );
        AGTIAPI_PRINTK( "agtiapi_getdevlist: addresshi %x\n",
                        pDeviceInfo->sasAddressHi );
        AGTIAPI_PRINTK( "agtiapi_getdevlist: addresslo %x\n",
                        pDeviceInfo->sasAddressHi );
      }
      else
      {
        AGTIAPI_PRINTK( "agtiapi_getdevlist: pDeviceData %p or pDeviceInfo "
                        "%p is NULL %d\n", pDeviceData, pDeviceInfo, x );
      }
      count++;
    }
  }
  pIoctlPayload->realDeviceCount = count;
  AGTIAPI_PRINTK( "agtiapi_getdevlist: Exit RealDeviceCount = %d\n", count );
  if (devList)
  {
    free(devList, TEMP2);
  }
  if(ret_val != IOCTL_CALL_FAIL)
  {
    ret_val = IOCTL_CALL_SUCCESS;
  }
  agIOCTLPayload->Status = IOCTL_ERR_STATUS_OK;
  return  ret_val;
}

/******************************************************************************
agtiapi_getCardInfo()

Purpose:
  This function retrives the Card information
Parameters: 
  
Return:
  A number - error  
  0        - HBA has been detected
Note:    
******************************************************************************/
int agtiapi_getCardInfo ( struct agtiapi_softc *pCard,
                          U32_64                size,
                          void                 *buffer )
{
  CardInfo_t       *pCardInfo;

  pCardInfo = (CardInfo_t *)buffer;

  pCardInfo->deviceId = pci_get_device(pCard->my_dev);
  pCardInfo->vendorId =pci_get_vendor(pCard->my_dev) ;
  memcpy( pCardInfo->pciMemBaseSpc,
          pCard->pCardInfo->pciMemBaseSpc,
          ((sizeof(U32_64))*PCI_NUMBER_BARS) );
  pCardInfo->deviceNum = pci_get_slot(pCard->my_dev);
  pCardInfo->pciMemBase = pCard->pCardInfo->pciMemBase;
  pCardInfo->pciIOAddrLow = pCard->pCardInfo->pciIOAddrLow;
  pCardInfo->pciIOAddrUp = pCard->pCardInfo->pciIOAddrUp;
  pCardInfo->busNum =pci_get_bus(pCard->my_dev);
  return 0;
}

void agtiapi_adjust_queue_depth(struct cam_path *path, bit32 QueueDepth)
{
  struct ccb_relsim crs;
  xpt_setup_ccb(&crs.ccb_h, path, 5);
  crs.ccb_h.func_code = XPT_REL_SIMQ;
  crs.ccb_h.flags = CAM_DEV_QFREEZE;
  crs.release_flags = RELSIM_ADJUST_OPENINGS;
  crs.openings = QueueDepth;
  xpt_action((union ccb *)&crs);
  if(crs.ccb_h.status != CAM_REQ_CMP) {
                 printf("XPT_REL_SIMQ failed\n");
  }
}
static void
agtiapi_async(void *callback_arg, u_int32_t code,
	       struct cam_path *path, void *arg)
{
	struct agtiapi_softc *pmsc;
	U32        TID;
	ag_device_t *targ;	
	pmsc = (struct agtiapi_softc*)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
	    struct ccb_getdev *cgd;
	    cgd = (struct ccb_getdev *)arg;
	    if (cgd == NULL) {
		break;
	    }
	    TID = cgd->ccb_h.target_id;
	    if (TID >= 0 && TID < maxTargets){
                if (pmsc != NULL){
                    TID = INDEX(pmsc, TID);
                    targ   = &pmsc->pDevList[TID];
	            agtiapi_adjust_queue_depth(path, targ->qdepth);
                }
	    }
	    break;
        }
	default:
		break;
	}
}
/******************************************************************************
agtiapi_CharIoctl()

Purpose:
  This function handles the ioctl from application layer
Parameters: 
 
Return:
  A number - error  
  0        - HBA has been detected
Note:    
******************************************************************************/
static int agtiapi_CharIoctl( struct cdev   *dev,
                              u_long         cmd,
                              caddr_t        data,
                              int            fflag,
                              struct thread *td )
{
  struct sema           mx;
  datatosend           *load; // structure defined in lxcommon.h
  tiIOCTLPayload_t     *pIoctlPayload;
  struct agtiapi_softc *pCard;
  pCard=dev->si_drv1;
  U32   status = 0;
  U32   retValue;
  int   err    = 0;
  int   error  = 0;
  tdDeviceListPayload_t *pDeviceList = NULL;
  unsigned long flags;

  switch (cmd)
  {
  case AGTIAPI_IOCTL:
    load=(datatosend*)data;
    pIoctlPayload = malloc(load->datasize,TEMP,M_WAITOK);
    AGTIAPI_PRINTK( "agtiapi_CharIoctl: old load->datasize = %d\n", load->datasize );
    //Copy payload to kernel buffer, on success it returns 0
    err = copyin(load->data,pIoctlPayload,load->datasize);
    if (err)
    {
      status = IOCTL_CALL_FAIL;
      return status;
    }
    sema_init(&mx,0,"sem");
    pCard->pIoctlSem  =&mx; 
    pCard->up_count = pCard->down_count = 0;
    if ( pIoctlPayload->MajorFunction == IOCTL_MJ_GET_DEVICE_LIST )
    {
      retValue = agtiapi_getdevlist(pCard, pIoctlPayload);
      if (retValue == 0)
      {
        pIoctlPayload->Status = IOCTL_CALL_SUCCESS;
        status = IOCTL_CALL_SUCCESS;
      }
      else
      {
        pIoctlPayload->Status = IOCTL_CALL_FAIL;
        status = IOCTL_CALL_FAIL;
      }
      //update new device length
      pDeviceList = (tdDeviceListPayload_t*)pIoctlPayload->FunctionSpecificArea;
      load->datasize =load->datasize - sizeof(tdDeviceInfoIOCTL_t) * (pDeviceList->deviceLength - pDeviceList->realDeviceCount);
      AGTIAPI_PRINTK( "agtiapi_CharIoctl: new load->datasize = %d\n", load->datasize );

    }
    else if (pIoctlPayload->MajorFunction == IOCTL_MN_GET_CARD_INFO)
    {
      retValue = agtiapi_getCardInfo( pCard,
                                      pIoctlPayload->Length,
                                      (pIoctlPayload->FunctionSpecificArea) );
      if (retValue == 0)
      {
        pIoctlPayload->Status = IOCTL_CALL_SUCCESS;
        status = IOCTL_CALL_SUCCESS;
      }
      else
      {
        pIoctlPayload->Status = IOCTL_CALL_FAIL;
        status = IOCTL_CALL_FAIL;
      }
    }
    else if ( pIoctlPayload->MajorFunction == IOCTL_MJ_CHECK_DPMC_EVENT )
    {
      if ( pCard->flags & AGTIAPI_PORT_PANIC )
      {
        strcpy ( pIoctlPayload->FunctionSpecificArea, "DPMC LEAN\n" );
      }
      else
      {
        strcpy ( pIoctlPayload->FunctionSpecificArea, "do not dpmc lean\n" );
      }
      pIoctlPayload->Status = IOCTL_CALL_SUCCESS;
      status = IOCTL_CALL_SUCCESS;
    }
    else if (pIoctlPayload->MajorFunction == IOCTL_MJ_CHECK_FATAL_ERROR )
    {
      AGTIAPI_PRINTK("agtiapi_CharIoctl: IOCTL_MJ_CHECK_FATAL_ERROR call received for card %d\n", pCard->cardNo);
      //read port status to see if there is a fatal event
      if(pCard->flags & AGTIAPI_PORT_PANIC)
      {
        printf("agtiapi_CharIoctl: Port Panic Status For Card %d is True\n",pCard->cardNo);
        pIoctlPayload->Status = IOCTL_MJ_FATAL_ERR_CHK_SEND_TRUE;
      }
      else
      {
        AGTIAPI_PRINTK("agtiapi_CharIoctl: Port Panic Status For Card %d is False\n",pCard->cardNo);
        pIoctlPayload->Status = IOCTL_MJ_FATAL_ERR_CHK_SEND_FALSE;
      }
      status = IOCTL_CALL_SUCCESS;
    }
    else if (pIoctlPayload->MajorFunction == IOCTL_MJ_FATAL_ERROR_DUMP_COMPLETE)
    {
      AGTIAPI_PRINTK("agtiapi_CharIoctl: IOCTL_MJ_FATAL_ERROR_DUMP_COMPLETE call received for card %d\n", pCard->cardNo);
      //set flags bit status to be a soft reset
      pCard->flags |= AGTIAPI_SOFT_RESET;
      //trigger soft reset for the card
      retValue = agtiapi_ResetCard (pCard, &flags);
    
      if(retValue == AGTIAPI_SUCCESS)
      {
        //clear port panic status
        pCard->flags &= ~AGTIAPI_PORT_PANIC;
        pIoctlPayload->Status = IOCTL_MJ_FATAL_ERROR_SOFT_RESET_TRIG;
        status = IOCTL_CALL_SUCCESS;
      }
      else
      {
        pIoctlPayload->Status = IOCTL_CALL_FAIL;
        status = IOCTL_CALL_FAIL;
      }
    }
    else
    {
      status = tiCOMMgntIOCTL( &pCard->tiRoot,
                               pIoctlPayload,
                               pCard,
                               NULL,
                               NULL );
      if (status == IOCTL_CALL_PENDING)
      {
        ostiIOCTLWaitForSignal(&pCard->tiRoot,NULL, NULL, NULL);
        status = IOCTL_CALL_SUCCESS;  
      }
    }
    pCard->pIoctlSem = NULL;
    err = 0;

    //copy kernel buffer to userland buffer
    err=copyout(pIoctlPayload,load->data,load->datasize);
    if (err)
    {
      status = IOCTL_CALL_FAIL;
      return status;
    }
    free(pIoctlPayload,TEMP);
    pIoctlPayload=NULL;
    break;
  default:
    error = ENOTTY;
    break;
  }
  return(status);
}

/******************************************************************************
agtiapi_probe()

Purpose:
  This function initialize and registere all detected HBAs.
  The first function being called in driver after agtiapi_probe()
Parameters: 
  device_t dev (IN)  - device pointer
Return:
  A number - error  
  0        - HBA has been detected
Note:    
******************************************************************************/
static int agtiapi_probe( device_t dev )
{
  int retVal;
  int thisCard;
  ag_card_info_t *thisCardInst;

  thisCard = device_get_unit( dev );
  if ( thisCard >= AGTIAPI_MAX_CARDS ) 
  {
    device_printf( dev, "Too many PMC-Sierra cards detected ERROR!\n" );
    return (ENXIO); // maybe change to different return value?
  }
  thisCardInst = &agCardInfoList[ thisCard ];
  retVal = agtiapi_ProbeCard( dev, thisCardInst, thisCard );
  if ( retVal )
    return (ENXIO); // maybe change to different return value?
  return( BUS_PROBE_DEFAULT );  // successful probe
}


/******************************************************************************
agtiapi_attach()

Purpose:
  This function initialize and registere all detected HBAs.
  The first function being called in driver after agtiapi_probe()
Parameters: 
  device_t dev (IN)  - device pointer
Return:
  A number - error  
  0        - HBA has been detected
Note:    
******************************************************************************/
static int agtiapi_attach( device_t devx )
{
  // keeping get_unit call to once
  int                   thisCard = device_get_unit( devx );
  struct agtiapi_softc *pmsc;
  ag_card_info_t       *thisCardInst = &agCardInfoList[ thisCard ];
  ag_resource_info_t   *pRscInfo;
  int                   idx;
  int			        lenRecv;
  char			        buffer [256], *pLastUsedChar;
  union ccb *ccb;
  int bus, tid, lun;
  struct ccb_setasync csa;

  AGTIAPI_PRINTK("agtiapi_attach: start dev %p thisCard %d\n", devx, thisCard);
  // AGTIAPI_PRINTK( "agtiapi_attach: entry pointer values  A %p / %p\n",
  //        thisCardInst->pPCIDev, thisCardInst );
  AGTIAPI_PRINTK( "agtiapi_attach: deviceID: 0x%x\n", pci_get_devid( devx ) );

  TUNABLE_INT_FETCH( "DPMC_TIMEOUT_SECS",  &ag_timeout_secs );
  TUNABLE_INT_FETCH( "DPMC_TIDEBUG_LEVEL", &gTiDebugLevel   );
  // printf( "agtiapi_attach: debugLevel %d, timeout %d\n",
  //         gTiDebugLevel, ag_timeout_secs );
  if ( ag_timeout_secs < 1 )
  {
    ag_timeout_secs = 1; // set minimum timeout value of 1 second
  }
  ag_timeout_secs = (ag_timeout_secs * 1000); // convert to millisecond notation

  // Look up our softc and initialize its fields.
  pmsc = device_get_softc( devx );
  pmsc->my_dev = devx;

  /* Get NumberOfPortals */ 
  if ((ostiGetTransportParam(
                             &pmsc->tiRoot, 
                             "Global",
                             "CardDefault",
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "NumberOfPortals",
                             buffer, 
                             255, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      ag_portal_count = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      ag_portal_count = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    if (ag_portal_count > AGTIAPI_MAX_PORTALS)
      ag_portal_count = AGTIAPI_MAX_PORTALS;
  }
  else
  {
    ag_portal_count = AGTIAPI_MAX_PORTALS;
  }
  AGTIAPI_PRINTK( "agtiapi_attach: ag_portal_count=%d\n", ag_portal_count );
  // initialize hostdata structure
  pmsc->flags    |= AGTIAPI_INIT_TIME | AGTIAPI_SCSI_REGISTERED |
      AGTIAPI_INITIATOR;
  pmsc->cardNo    = thisCard;  
  pmsc->ccbTotal  = 0;
  pmsc->portCount = ag_portal_count;
  pmsc->pCardInfo = thisCardInst;
  pmsc->tiRoot.osData = pmsc;
  pmsc->pCardInfo->pCard  = (void *)pmsc;
  pmsc->VidDid    = ( pci_get_vendor(devx) << 16 ) | pci_get_device( devx );
  pmsc->SimQFrozen = agFALSE;
  pmsc->devq_flag  = agFALSE;
  pRscInfo = &thisCardInst->tiRscInfo;

  osti_memset(buffer, 0, 256); 
  lenRecv = 0;

  /* Get MaxTargets */ 
  if ((ostiGetTransportParam(
                             &pmsc->tiRoot, 
                             "Global",
                             "InitiatorParms",
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxTargets",
                             buffer, 
                             sizeof(buffer), 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      maxTargets = osti_strtoul (buffer, &pLastUsedChar, 0);
      AGTIAPI_PRINTK( "agtiapi_attach:  maxTargets = osti_strtoul  0 \n" );
    }
    else
    {
      maxTargets = osti_strtoul (buffer, &pLastUsedChar, 10);
      AGTIAPI_PRINTK( "agtiapi_attach:  maxTargets = osti_strtoul 10\n"   );
    }
  }
  else

  {
    if(Is_ADP8H(pmsc))
       maxTargets = AGTIAPI_MAX_DEVICE_8H;
    else if(Is_ADP7H(pmsc))
       maxTargets = AGTIAPI_MAX_DEVICE_7H;
    else
       maxTargets = AGTIAPI_MAX_DEVICE;
  }

  if (maxTargets > AGTIAPI_HW_LIMIT_DEVICE)
  {
    AGTIAPI_PRINTK( "agtiapi_attach: maxTargets: %d > AGTIAPI_HW_LIMIT_DEVICE: %d\n",  maxTargets, AGTIAPI_HW_LIMIT_DEVICE );
    AGTIAPI_PRINTK( "agtiapi_attach: change maxTargets = AGTIAPI_HW_LIMIT_DEVICE\n" );
    maxTargets = AGTIAPI_HW_LIMIT_DEVICE;
  }
  pmsc->devDiscover    = maxTargets ; 

 #ifdef HIALEAH_ENCRYPTION
   ag_encryption_enable   =  1;
   if(ag_encryption_enable && pci_get_device(pmsc->pCardInfo->pPCIDev) == 
                                  PCI_DEVICE_ID_HIALEAH_HBA_SPCVE)
   {
	pmsc->encrypt = 1;
	pRscInfo->tiLoLevelResource.loLevelOption.encryption = agTRUE;
	printf("agtiapi_attach: Encryption Enabled\n" );
   }
#endif
  // ## for now, skip calls to ostiGetTransportParam(...)
  // ## for now, skip references to DIF & EDC

  // Create a /dev entry for this device. The kernel will assign us
  // a major number automatically. We use the unit number of this
  // device as the minor number and name the character device
  // "agtiapi<unit>".
  pmsc->my_cdev = make_dev( &agtiapi_cdevsw, thisCard, UID_ROOT, GID_WHEEL,
			    0600, "spcv%u", thisCard );
  pmsc->my_cdev->si_drv1 = pmsc;

  mtx_init( &thisCardInst->pmIOLock, "pmc SAS I/O lock",
	    NULL, MTX_DEF|MTX_RECURSE );

  struct cam_devq *devq;  

  /* set the maximum number of pending IOs */
  devq = cam_simq_alloc( AGTIAPI_MAX_CAM_Q_DEPTH );
  if (devq == NULL)
  {
    AGTIAPI_PRINTK("agtiapi_attach: cam_simq_alloc is NULL\n" );
    return( EIO );
  }

  struct cam_sim *lsim;
  lsim = cam_sim_alloc( agtiapi_cam_action,
                        agtiapi_cam_poll,
                        "pmspcbsd",
                        pmsc,
                        thisCard,
                        &thisCardInst->pmIOLock,
                        1,                       // queued per target
                        AGTIAPI_MAX_CAM_Q_DEPTH, // max tag depth
                        devq );
  if ( lsim == NULL ) {
    cam_simq_free( devq );
    AGTIAPI_PRINTK("agtiapi_attach: cam_sim_alloc is NULL\n" );
    return( EIO );
  }

  pmsc->dev_scan = agFALSE;
  //one cam sim per scsi bus
  mtx_lock( &thisCardInst->pmIOLock );
  if ( xpt_bus_register( lsim, devx, 0 ) != CAM_SUCCESS ) 
  { // bus 0
    cam_sim_free( lsim, TRUE );
    mtx_unlock( &thisCardInst->pmIOLock );
    AGTIAPI_PRINTK("agtiapi_attach: xpt_bus_register fails\n" );
    return( EIO );
  }

  pmsc->sim  = lsim;
  bus = cam_sim_path(pmsc->sim);
  tid = CAM_TARGET_WILDCARD;
  lun = CAM_LUN_WILDCARD;
  ccb = xpt_alloc_ccb_nowait();
  if (ccb == agNULL)
  {
	mtx_unlock( &thisCardInst->pmIOLock );
    cam_sim_free( lsim, TRUE );
    cam_simq_free( devq );
    return ( EIO );
  }
  if (xpt_create_path(&ccb->ccb_h.path, agNULL, bus, tid,
		      CAM_LUN_WILDCARD) != CAM_REQ_CMP) 
  { 
	mtx_unlock( &thisCardInst->pmIOLock );
	cam_sim_free( lsim, TRUE );
    cam_simq_free( devq );
    xpt_free_ccb(ccb);
    return( EIO );
  }
  pmsc->path = ccb->ccb_h.path;
  xpt_setup_ccb(&csa.ccb_h, pmsc->path, 5);
  csa.ccb_h.func_code = XPT_SASYNC_CB;
  csa.event_enable = AC_FOUND_DEVICE;
  csa.callback = agtiapi_async;
  csa.callback_arg = pmsc;
  xpt_action((union ccb *)&csa);
  if (csa.ccb_h.status != CAM_REQ_CMP) {
	  AGTIAPI_PRINTK("agtiapi_attach: Unable to register AC_FOUND_DEVICE\n" );
  }
  lsim->devq = devq;
  mtx_unlock( &thisCardInst->pmIOLock );



  
  // get TD and lower layer memory requirements
  tiCOMGetResource( &pmsc->tiRoot,
                    &pRscInfo->tiLoLevelResource,
                    &pRscInfo->tiInitiatorResource,
                    NULL,
                    &pRscInfo->tiSharedMem );

  agtiapi_ScopeDMARes( thisCardInst );
  AGTIAPI_PRINTK( "agtiapi_attach: size from the call agtiapi_ScopeDMARes"
                  " 0x%x \n", pmsc->typhn );

  // initialize card information and get resource ready
  if( agtiapi_InitResource( thisCardInst ) == AGTIAPI_FAIL ) {
    AGTIAPI_PRINTK( "agtiapi_attach: Card %d initialize resource ERROR\n",
            thisCard );
  }

  // begin: allocate and initialize card portal info resource
  ag_portal_data_t   *pPortalData;
  if (pmsc->portCount == 0)
  {
    pmsc->pPortalData = NULL;
  }
  else 
  {
    pmsc->pPortalData = (ag_portal_data_t *)
                        malloc( sizeof(ag_portal_data_t) * pmsc->portCount,
                                M_PMC_MPRT, M_ZERO | M_WAITOK );
    if (pmsc->pPortalData == NULL)
    {
      AGTIAPI_PRINTK( "agtiapi_attach: Portal memory allocation ERROR\n" );
    }
  }

  pPortalData = pmsc->pPortalData;
  for( idx = 0; idx < pmsc->portCount; idx++ ) {
    pPortalData->pCard = pmsc;
    pPortalData->portalInfo.portID = idx;
    pPortalData->portalInfo.tiPortalContext.osData = (void *)pPortalData;
    pPortalData++;
  }
  // end: allocate and initialize card portal info resource

  // begin: enable msix

  // setup msix
  // map to interrupt handler
  int error = 0;
  int mesgs = MAX_MSIX_NUM_VECTOR;
  int i, cnt;

  void (*intrHandler[MAX_MSIX_NUM_ISR])(void *arg) =
    {
      agtiapi_IntrHandler0,
      agtiapi_IntrHandler1,
      agtiapi_IntrHandler2,
      agtiapi_IntrHandler3,
      agtiapi_IntrHandler4,
      agtiapi_IntrHandler5,
      agtiapi_IntrHandler6,
      agtiapi_IntrHandler7,
      agtiapi_IntrHandler8,
      agtiapi_IntrHandler9,
      agtiapi_IntrHandler10,
      agtiapi_IntrHandler11,
      agtiapi_IntrHandler12,
      agtiapi_IntrHandler13,
      agtiapi_IntrHandler14,
      agtiapi_IntrHandler15
      
    };

  cnt = pci_msix_count(devx);
  AGTIAPI_PRINTK("supported MSIX %d\n", cnt); //this should be 64
  mesgs = MIN(mesgs, cnt);
  error = pci_alloc_msix(devx, &mesgs);
  if (error != 0) {
    printf( "pci_alloc_msix error %d\n", error );
    AGTIAPI_PRINTK("error %d\n", error);
    return( EIO );
  }

  for(i=0; i < mesgs; i++) {
    pmsc->rscID[i] = i + 1;
    pmsc->irq[i] = bus_alloc_resource_any( devx,
                                           SYS_RES_IRQ,
                                           &pmsc->rscID[i],
                                           RF_ACTIVE );
    if( pmsc->irq[i] == NULL ) {
      printf( "RES_IRQ went terribly bad at %d\n", i );
      return( EIO );
    }

    if ( (error = bus_setup_intr( devx, pmsc->irq[i],
                                  INTR_TYPE_CAM | INTR_MPSAFE,
                                  NULL,
                                  intrHandler[i],
                                  pmsc,
                                  &pmsc->intrcookie[i] )
           ) != 0 ) {
      device_printf( devx, "Failed to register handler" );
      return( EIO );
    }
  }
  pmsc->flags |= AGTIAPI_IRQ_REQUESTED;
  pmsc->pCardInfo->maxInterruptVectors = MAX_MSIX_NUM_VECTOR;
  // end: enable msix
  
  int ret = 0;
  ret = agtiapi_InitCardSW(pmsc);
  if (ret == AGTIAPI_FAIL || ret == AGTIAPI_UNKNOWN)
  {
    AGTIAPI_PRINTK( "agtiapi_attach: agtiapi_InitCardSW failure %d\n",
                    ret );
    return( EIO );
  }    

  pmsc->ccbFreeList = NULL;
  pmsc->ccbChainList = NULL;
  pmsc->ccbAllocList = NULL;

  pmsc->flags |= ( AGTIAPI_INSTALLED );

  ret = agtiapi_alloc_requests( pmsc );
  if( ret != 0 ) {
    AGTIAPI_PRINTK( "agtiapi_attach: agtiapi_alloc_requests failure %d\n",
                    ret );
    return( EIO );
  }

  ret = agtiapi_alloc_ostimem( pmsc );
  if (ret != AGTIAPI_SUCCESS)
  {
    AGTIAPI_PRINTK( "agtiapi_attach: agtiapi_alloc_ostimem failure %d\n",
                    ret );
    return( EIO );
  }

  ret = agtiapi_InitCardHW( pmsc );
  if (ret != 0)
  {
    AGTIAPI_PRINTK( "agtiapi_attach: agtiapi_InitCardHW failure %d\n",
                    ret );
    return( EIO );
  }

#ifdef HIALEAH_ENCRYPTION
  if(pmsc->encrypt)
  {
	if((agtiapi_SetupEncryption(pmsc)) < 0)
		AGTIAPI_PRINTK("SetupEncryption returned less than 0\n");
  }
#endif

  pmsc->flags &= ~AGTIAPI_INIT_TIME;
  return( 0 );
}

/******************************************************************************
agtiapi_InitCardSW()

Purpose:
  Host Bus Adapter Initialization
Parameters: 
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
Note:    
  TBD, need chip register information
******************************************************************************/
STATIC agBOOLEAN agtiapi_InitCardSW( struct agtiapi_softc *pmsc ) 
{
  ag_card_info_t *thisCardInst = pmsc->pCardInfo;
  ag_resource_info_t *pRscInfo = &thisCardInst->tiRscInfo;
  int initSWIdx;

  // begin: agtiapi_InitCardSW()
  // now init some essential locks  n agtiapi_InitCardSW
  mtx_init( &pmsc->sendLock,     "local q send lock",   NULL, MTX_DEF );
  mtx_init( &pmsc->doneLock,     "local q done lock",   NULL, MTX_DEF );
  mtx_init( &pmsc->sendSMPLock,  "local q send lock",   NULL, MTX_DEF );
  mtx_init( &pmsc->doneSMPLock,  "local q done lock",   NULL, MTX_DEF );
  mtx_init( &pmsc->ccbLock,      "ccb list lock",       NULL, MTX_DEF );
  mtx_init( &pmsc->devListLock,  "hotP devListLock",    NULL, MTX_DEF );
  mtx_init( &pmsc->memLock,      "dynamic memory lock", NULL, MTX_DEF );
  mtx_init( &pmsc->freezeLock,   "sim freeze lock",     NULL, MTX_DEF | MTX_RECURSE);

  // initialize lower layer resources
  //## if (pCard->flags & AGTIAPI_INIT_TIME) {
#ifdef HIALEAH_ENCRYPTION
    /* Enable encryption if chip supports it */
    if (pci_get_device(pmsc->pCardInfo->pPCIDev) == 
                     PCI_DEVICE_ID_HIALEAH_HBA_SPCVE)
        pmsc->encrypt = 1;

    if (pmsc->encrypt)
        pRscInfo->tiLoLevelResource.loLevelOption.encryption = agTRUE;
#endif
  pmsc->flags &= ~(AGTIAPI_PORT_INITIALIZED | AGTIAPI_SYS_INTR_ON);


  // For now, up to 16 MSIX vectors are supported
  thisCardInst->tiRscInfo.tiLoLevelResource.loLevelOption.
    maxInterruptVectors = pmsc->pCardInfo->maxInterruptVectors;
  AGTIAPI_PRINTK( "agtiapi_InitCardSW: maxInterruptVectors set to %d",
                  pmsc->pCardInfo->maxInterruptVectors );
  thisCardInst->tiRscInfo.tiLoLevelResource.loLevelOption.max_MSI_InterruptVectors = 0;
  thisCardInst->tiRscInfo.tiLoLevelResource.loLevelOption.flag = 0;
  pRscInfo->tiLoLevelResource.loLevelOption.maxNumOSLocks = 0;

  AGTIAPI_PRINTK( "agtiapi_InitCardSW: tiCOMInit root %p, dev %p, pmsc %p\n",
                  &pmsc->tiRoot, pmsc->my_dev, pmsc );
  if( tiCOMInit( &pmsc->tiRoot,
                 &thisCardInst->tiRscInfo.tiLoLevelResource,
                 &thisCardInst->tiRscInfo.tiInitiatorResource,
                 NULL,
                 &thisCardInst->tiRscInfo.tiSharedMem ) != tiSuccess ) {
    AGTIAPI_PRINTK( "agtiapi_InitCardSW: tiCOMInit ERROR\n" );
    return AGTIAPI_FAIL;
  }
  int maxLocks;
  maxLocks = pRscInfo->tiLoLevelResource.loLevelOption.numOfQueuesPerPort;
  pmsc->STLock = malloc( ( maxLocks * sizeof(struct mtx) ), M_PMC_MSTL,
			              M_ZERO | M_WAITOK );

  for( initSWIdx = 0; initSWIdx < maxLocks; initSWIdx++ )
  {
    // init all indexes
    mtx_init( &pmsc->STLock[initSWIdx], "LL & TD lock", NULL, MTX_DEF );
  }

  if( tiCOMPortInit( &pmsc->tiRoot, agFALSE ) != tiSuccess ) {
    printf( "agtiapi_InitCardSW: tiCOMPortInit ERROR -- AGTIAPI_FAIL\n" );
    return AGTIAPI_FAIL;
  }
  AGTIAPI_PRINTK( "agtiapi_InitCardSW: tiCOMPortInit"
                  " root %p, dev %p, pmsc %p\n", 
                  &pmsc->tiRoot, pmsc->my_dev, pmsc );

  pmsc->flags |= AGTIAPI_PORT_INITIALIZED;
  pmsc->freezeSim = agFALSE;

#ifdef HIALEAH_ENCRYPTION
  atomic_set(&outstanding_encrypted_io_count, 0);
  /*fix below*/
  /*if(pmsc->encrypt && (pmsc->flags & AGTIAPI_INIT_TIME))
	   if((agtiapi_SetupEncryptionPools(pmsc)) != 0)
	     printf("SetupEncryptionPools failed\n"); */
#endif
  return AGTIAPI_SUCCESS;
  // end: agtiapi_InitCardSW()
}

/******************************************************************************
agtiapi_InitCardHW()

Purpose:
  Host Bus Adapter Initialization
Parameters: 
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
Note:    
  TBD, need chip register information
******************************************************************************/
STATIC agBOOLEAN agtiapi_InitCardHW( struct agtiapi_softc *pmsc ) 
{
  U32 numVal;
  U32 count;
  U32 loop;
  // begin: agtiapi_InitCardHW()

  ag_portal_info_t *pPortalInfo = NULL;
  ag_portal_data_t *pPortalData;

  // ISR is registered, enable chip interrupt.
  tiCOMSystemInterruptsActive( &pmsc->tiRoot, agTRUE );
  pmsc->flags |= AGTIAPI_SYS_INTR_ON;

  numVal = sizeof(ag_device_t) * pmsc->devDiscover;
  pmsc->pDevList =
    (ag_device_t *)malloc( numVal, M_PMC_MDVT, M_ZERO | M_WAITOK );
  if( !pmsc->pDevList ) {
    AGTIAPI_PRINTK( "agtiapi_InitCardHW: kmalloc %d DevList ERROR\n", numVal );
    panic( "agtiapi_InitCardHW\n" );
    return AGTIAPI_FAIL;
  }

#ifdef LINUX_PERBI_SUPPORT
  numVal = sizeof(ag_slr_map_t) * pmsc->devDiscover;
  pmsc->pSLRList =
    (ag_slr_map_t *)malloc( numVal, M_PMC_MSLR, M_ZERO | M_WAITOK );
  if( !pmsc->pSLRList ) {
    AGTIAPI_PRINTK( "agtiapi_InitCardHW: kmalloc %d SLRList ERROR\n", numVal );
    panic( "agtiapi_InitCardHW SLRL\n" );
    return AGTIAPI_FAIL;
  }

  numVal = sizeof(ag_tgt_map_t) * pmsc->devDiscover;
  pmsc->pWWNList =
    (ag_tgt_map_t *)malloc( numVal, M_PMC_MTGT, M_ZERO | M_WAITOK );
  if( !pmsc->pWWNList ) {
    AGTIAPI_PRINTK( "agtiapi_InitCardHW: kmalloc %d WWNList ERROR\n", numVal );
    panic( "agtiapi_InitCardHW WWNL\n" );
    return AGTIAPI_FAIL;
  }

  // Get the WWN_to_target_ID mappings from the
  // holding area which contains the input of the
  // system configuration file.
  if( ag_Perbi )
    agtiapi_GetWWNMappings( pmsc, agMappingList );
  else {
    agtiapi_GetWWNMappings( pmsc, 0 );
    if( agMappingList )
      printf( "agtiapi_InitCardHW: WWN PERBI disabled WARN\n" );
  }
#endif

  //agtiapi_DelaySec(5);
  DELAY( 500000 );

  pmsc->tgtCount = 0;

  pmsc->flags &= ~AGTIAPI_CB_DONE;
  pPortalData = pmsc->pPortalData;

  //start port

  for (count = 0; count < pmsc->portCount; count++)
  {
    AG_SPIN_LOCK_IRQ( agtiapi_host_lock, flags );

    pPortalInfo = &pPortalData->portalInfo;
    pPortalInfo->portStatus &= ~( AGTIAPI_PORT_START      | 
                                  AGTIAPI_PORT_DISC_READY |
                                  AGTIAPI_DISC_DONE       |
                                  AGTIAPI_DISC_COMPLETE );

    for (loop = 0; loop < AGTIAPI_LOOP_MAX; loop++)
    {
      AGTIAPI_PRINTK( "tiCOMPortStart entry data %p / %d / %p\n",
                      &pmsc->tiRoot,
                      pPortalInfo->portID,
                      &pPortalInfo->tiPortalContext );

      if( tiCOMPortStart( &pmsc->tiRoot, 
                          pPortalInfo->portID, 
                          &pPortalInfo->tiPortalContext,
                          0 )
          != tiSuccess ) {
        AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, flags );
        agtiapi_DelayMSec( AGTIAPI_EXTRA_DELAY );
        AG_SPIN_LOCK_IRQ(agtiapi_host_lock, flags);
        AGTIAPI_PRINTK( "tiCOMPortStart failed -- no loop, portalData %p\n",
                        pPortalData );
      }
      else {
        AGTIAPI_PRINTK( "tiCOMPortStart success no loop, portalData %p\n", 
                        pPortalData );
        break;
      }
    } // end of for loop
    /* release lock */
    AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, flags );

    if( loop >= AGTIAPI_LOOP_MAX ) {
      return AGTIAPI_FAIL;
    }
    tiCOMGetPortInfo( &pmsc->tiRoot,
                      &pPortalInfo->tiPortalContext,
                      &pPortalInfo->tiPortInfo );
    pPortalData++;
  }

  /* discover target device */
#ifndef HOTPLUG_SUPPORT
  agtiapi_DiscoverTgt( pCard );
#endif


  pmsc->flags |= AGTIAPI_INSTALLED;
  
  if( pmsc->flags & AGTIAPI_INIT_TIME ) {
    agtiapi_TITimer( (void *)pmsc );
    pmsc->flags |= AGTIAPI_TIMER_ON;
  }

  return 0;
}



/******************************************************************************
agtiapi_IntrHandlerx_()

Purpose:
  Interrupt service routine.
Parameters:
  void arg (IN)              Pointer to the HBA data structure
  bit32 idx (IN)             Vector index
******************************************************************************/
void  agtiapi_IntrHandlerx_( void *arg, int index )
{
  
  struct agtiapi_softc *pCard;
  int rv;

  pCard = (struct agtiapi_softc *)arg;

#ifndef AGTIAPI_DPC
  ccb_t     *pccb;
#endif

  AG_LOCAL_LOCK(&(pCard->pCardInfo->pmIOLock));
  AG_PERF_SPINLOCK(agtiapi_host_lock);
  if (pCard->flags & AGTIAPI_SHUT_DOWN)
    goto ext;

  rv = tiCOMInterruptHandler(&pCard->tiRoot, index);
  if (rv == agFALSE)
  {
    /* not our irq */
    AG_SPIN_UNLOCK(agtiapi_host_lock);
    AG_LOCAL_UNLOCK(&(pCard->pCardInfo->pmIOLock));    
    return;
  }


#ifdef AGTIAPI_DPC
  tasklet_hi_schedule(&pCard->tasklet_dpc[idx]);
#else
  /* consume all completed entries, 100 is random number to be big enough */
  tiCOMDelayedInterruptHandler(&pCard->tiRoot, index, 100, tiInterruptContext);
  AG_GET_DONE_PCCB(pccb, pCard);
  AG_GET_DONE_SMP_PCCB(pccb, pCard);
#endif

ext:
  AG_SPIN_UNLOCK(agtiapi_host_lock);
  AG_LOCAL_UNLOCK(&(pCard->pCardInfo->pmIOLock));  
  return;

}

/******************************************************************************
agtiapi_IntrHandler0()
Purpose:     Interrupt service routine for interrupt vector index 0.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler0( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 0 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler1()
Purpose:     Interrupt service routine for interrupt vector index 1.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler1( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 1 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler2()
Purpose:     Interrupt service routine for interrupt vector index 2.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler2( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 2 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler3()
Purpose:     Interrupt service routine for interrupt vector index 3.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler3( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 3 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler4()
Purpose:     Interrupt service routine for interrupt vector index 4.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler4( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 4 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler5()
Purpose:     Interrupt service routine for interrupt vector index 5.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler5( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 5 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler6()
Purpose:     Interrupt service routine for interrupt vector index 6.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler6( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 6 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler7()
Purpose:     Interrupt service routine for interrupt vector index 7.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler7( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 7 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler8()
Purpose:     Interrupt service routine for interrupt vector index 8.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler8( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 8 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler9()
Purpose:     Interrupt service routine for interrupt vector index 9.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler9( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 9 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler10()
Purpose:     Interrupt service routine for interrupt vector index 10.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler10( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 10 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler11()
Purpose:     Interrupt service routine for interrupt vector index 11.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler11( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 11 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler12()
Purpose:     Interrupt service routine for interrupt vector index 12.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler12( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 12 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler13()
Purpose:     Interrupt service routine for interrupt vector index 13.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler13( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 13 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler14()
Purpose:     Interrupt service routine for interrupt vector index 14.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler14( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 14 );
  return;
}

/******************************************************************************
agtiapi_IntrHandler15()
Purpose:     Interrupt service routine for interrupt vector index 15.
Parameters:  void arg (IN)       Pointer to the HBA data structure
******************************************************************************/
void agtiapi_IntrHandler15( void *arg )
{
  agtiapi_IntrHandlerx_( arg, 15 );
  return;
}

static void agtiapi_SglMemoryCB( void *arg,
                                 bus_dma_segment_t *dm_segs,
                                 int nseg,
                                 int error )
{
  bus_addr_t *addr;
  AGTIAPI_PRINTK("agtiapi_SglMemoryCB: start\n");
  if (error != 0)
  {
    AGTIAPI_PRINTK("agtiapi_SglMemoryCB: error %d\n", error);
    panic("agtiapi_SglMemoryCB: error %d\n", error);
    return;  
  } 
  addr = arg;
  *addr = dm_segs[0].ds_addr;
  return;
}

static void agtiapi_MemoryCB( void *arg,
                              bus_dma_segment_t *dm_segs,
                              int nseg,
                              int error )
{
  bus_addr_t *addr;
  AGTIAPI_PRINTK("agtiapi_MemoryCB: start\n");
  if (error != 0)
  {
    AGTIAPI_PRINTK("agtiapi_MemoryCB: error %d\n", error);
    panic("agtiapi_MemoryCB: error %d\n", error);
    return;  
  } 
  addr = arg;
  *addr = dm_segs[0].ds_addr;
  return;
}

/******************************************************************************
agtiapi_alloc_requests()

Purpose:
  Allocates resources such as dma tag and timer
Parameters: 
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
Note:    
******************************************************************************/
int agtiapi_alloc_requests( struct agtiapi_softc *pmcsc )
{
  
  int rsize, nsegs;
  U32 next_tick;

  nsegs = AGTIAPI_NSEGS;
  rsize = AGTIAPI_MAX_DMA_SEGS;   // 128
  AGTIAPI_PRINTK( "agtiapi_alloc_requests: MAXPHYS 0x%x PAGE_SIZE 0x%x \n",
                  MAXPHYS, PAGE_SIZE );
  AGTIAPI_PRINTK( "agtiapi_alloc_requests: nsegs %d rsize %d \n",
                  nsegs, rsize ); // 32, 128
  // This is for csio->data_ptr
  if( bus_dma_tag_create( agNULL,                      // parent
                          1,                           // alignment
                          0,                           // boundary
                          BUS_SPACE_MAXADDR,           // lowaddr
                          BUS_SPACE_MAXADDR,           // highaddr
                          NULL,                        // filter
                          NULL,                        // filterarg
                          BUS_SPACE_MAXSIZE_32BIT,     // maxsize
                          nsegs,                       // nsegments
                          BUS_SPACE_MAXSIZE_32BIT,     // maxsegsize
                          BUS_DMA_ALLOCNOW,            // flags
                          busdma_lock_mutex,           // lockfunc
                          &pmcsc->pCardInfo->pmIOLock, // lockarg
                          &pmcsc->buffer_dmat ) ) {
    AGTIAPI_PRINTK( "agtiapi_alloc_requests: Cannot alloc request DMA tag\n" );
    return( ENOMEM );
  }

  // This is for tiSgl_t of pccb in agtiapi_PrepCCBs()
  rsize =
    (sizeof(tiSgl_t) * AGTIAPI_NSEGS) *
    AGTIAPI_CCB_PER_DEVICE * maxTargets;
  AGTIAPI_PRINTK( "agtiapi_alloc_requests: rsize %d \n", rsize ); // 32, 128
  if( bus_dma_tag_create( agNULL,                  // parent
                          32,                      // alignment
                          0,	                     // boundary
                          BUS_SPACE_MAXADDR_32BIT, // lowaddr
                          BUS_SPACE_MAXADDR,	     // highaddr
                          NULL,                    // filter
                          NULL,	                   // filterarg
                          rsize,                   // maxsize
                          1,                       // nsegments
                          rsize,                   // maxsegsize
                          BUS_DMA_ALLOCNOW,        // flags
                          NULL,                    // lockfunc
                          NULL,                    // lockarg
                          &pmcsc->tisgl_dmat ) ) {
    AGTIAPI_PRINTK( "agtiapi_alloc_requests: Cannot alloc request DMA tag\n" );
    return( ENOMEM );
  }

  if( bus_dmamem_alloc( pmcsc->tisgl_dmat,
                        (void **)&pmcsc->tisgl_mem,
                        BUS_DMA_NOWAIT,
                        &pmcsc->tisgl_map ) ) {
    AGTIAPI_PRINTK( "agtiapi_alloc_requests: Cannot allocate SGL memory\n" );
    return( ENOMEM );
  }

  bzero( pmcsc->tisgl_mem, rsize );
  bus_dmamap_load( pmcsc->tisgl_dmat,
                   pmcsc->tisgl_map,
                   pmcsc->tisgl_mem,
                   rsize,
                   agtiapi_SglMemoryCB,
                   &pmcsc->tisgl_busaddr,
                   BUS_DMA_NOWAIT /* 0 */ );

  mtx_init( &pmcsc->OS_timer_lock,  "OS timer lock",      NULL, MTX_DEF );
  mtx_init( &pmcsc->IO_timer_lock,  "IO timer lock",      NULL, MTX_DEF );
  mtx_init( &pmcsc->devRmTimerLock, "targ rm timer lock", NULL, MTX_DEF );
  callout_init_mtx( &pmcsc->OS_timer, &pmcsc->OS_timer_lock, 0 );
  callout_init_mtx( &pmcsc->IO_timer, &pmcsc->IO_timer_lock, 0 );
  callout_init_mtx( &pmcsc->devRmTimer,
		    &pmcsc->devRmTimerLock, 0);

  next_tick = pmcsc->pCardInfo->tiRscInfo.tiLoLevelResource.
              loLevelOption.usecsPerTick / USEC_PER_TICK;
  AGTIAPI_PRINTK( "agtiapi_alloc_requests: before callout_reset, "
                  "next_tick 0x%x\n", next_tick );
  callout_reset( &pmcsc->OS_timer, next_tick, agtiapi_TITimer, pmcsc );
  return 0;
}

/******************************************************************************
agtiapi_alloc_ostimem()

Purpose:
  Allocates memory used later in ostiAllocMemory
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Pointer to the HBA data structure
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
Note:
  This is a pre-allocation for ostiAllocMemory() "non-cacheable" function calls
******************************************************************************/
int  agtiapi_alloc_ostimem( struct agtiapi_softc *pmcsc ) {
  int rsize, nomsize;

  nomsize = 4096;
  rsize = AGTIAPI_DYNAMIC_MAX * nomsize; // 8M
  AGTIAPI_PRINTK("agtiapi_alloc_ostimem: rsize %d \n", rsize);
 
  if( bus_dma_tag_create( agNULL,                      // parent
                          32,                          // alignment
                          0,                           // boundary
                          BUS_SPACE_MAXADDR,           // lowaddr
                          BUS_SPACE_MAXADDR,           // highaddr
                          NULL,                        // filter
                          NULL,                        // filterarg
                          rsize,                       // maxsize (size)
                          1,                           // number of segments
                          rsize,                       // maxsegsize
                          0,                           // flags
                          NULL,                        // lockfunc
                          NULL,                        // lockarg
                          &pmcsc->osti_dmat ) ) {
    AGTIAPI_PRINTK( "agtiapi_alloc_ostimem: Can't create no-cache mem tag\n" );
    return AGTIAPI_FAIL;
  }


  if( bus_dmamem_alloc( pmcsc->osti_dmat,
                        &pmcsc->osti_mem,
                        BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_NOCACHE,
                        &pmcsc->osti_mapp ) ) {
    AGTIAPI_PRINTK( "agtiapi_alloc_ostimem: Cannot allocate cache mem %d\n",
                    rsize );
    return AGTIAPI_FAIL;
  }


  bus_dmamap_load( pmcsc->osti_dmat,
                   pmcsc->osti_mapp,
                   pmcsc->osti_mem,
                   rsize,
                   agtiapi_MemoryCB, // try reuse of CB for same goal
                   &pmcsc->osti_busaddr,
                   BUS_DMA_NOWAIT );

  // populate all the ag_dma_addr_t osti_busaddr/mem fields with addresses for
  //  handy reference when driver is in motion 
  int idx;
  ag_card_info_t *pCardInfo = pmcsc->pCardInfo;
  ag_dma_addr_t  *pMem;

  for( idx = 0; idx < AGTIAPI_DYNAMIC_MAX; idx++ ) {
    pMem = &pCardInfo->dynamicMem[idx];
    pMem->nocache_busaddr = pmcsc->osti_busaddr + ( idx * nomsize );
    pMem->nocache_mem     = (void*)((U64)pmcsc->osti_mem + ( idx * nomsize ));
    pCardInfo->freeDynamicMem[idx] = &pCardInfo->dynamicMem[idx];
  }

  pCardInfo->topOfFreeDynamicMem = AGTIAPI_DYNAMIC_MAX;

  return AGTIAPI_SUCCESS;
}


/******************************************************************************
agtiapi_cam_action()

Purpose:
  Parses CAM frames and triggers a corresponding action
Parameters: 
  struct cam_sim *sim (IN)  Pointer to SIM data structure
  union ccb * ccb (IN)      Pointer to CAM ccb data structure
Return:
Note:    
******************************************************************************/
static void agtiapi_cam_action( struct cam_sim *sim, union ccb * ccb )
{
  struct agtiapi_softc *pmcsc;
  tiDeviceHandle_t *pDevHandle = NULL;	// acts as flag as well
  tiDeviceInfo_t devInfo;
  int pathID, targetID, lunID;
  int lRetVal;
  U32 TID;
  U32 speed = 150000;

  pmcsc = cam_sim_softc( sim );
  AGTIAPI_IO( "agtiapi_cam_action: start pmcs %p\n", pmcsc );

  if (pmcsc == agNULL)
  {
    AGTIAPI_PRINTK( "agtiapi_cam_action: start pmcs is NULL\n" );
    return;
  }
  mtx_assert( &(pmcsc->pCardInfo->pmIOLock), MA_OWNED );

  AGTIAPI_IO( "agtiapi_cam_action: cardNO %d func_code 0x%x\n", pmcsc->cardNo, ccb->ccb_h.func_code );

  pathID   = xpt_path_path_id( ccb->ccb_h.path );
  targetID = xpt_path_target_id( ccb->ccb_h.path );
  lunID    = xpt_path_lun_id( ccb->ccb_h.path );

  AGTIAPI_IO( "agtiapi_cam_action: P 0x%x T 0x%x L 0x%x\n",
              pathID, targetID, lunID );

  switch (ccb->ccb_h.func_code) 
  {
  case XPT_PATH_INQ:
  {
    struct ccb_pathinq *cpi;

    /* See architecure book p180*/
    cpi = &ccb->cpi;
    cpi->version_num = 1;
    cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE | PI_WIDE_16;
    cpi->target_sprt = 0;
    cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
    cpi->hba_eng_cnt = 0;
    cpi->max_target = maxTargets - 1;
    cpi->max_lun = AGTIAPI_MAX_LUN;
    cpi->maxio = 1024 *1024; /* Max supported I/O size, in bytes. */
    cpi->initiator_id = 255;
    strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
    strlcpy(cpi->hba_vid, "PMC", HBA_IDLEN);
    strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
    cpi->unit_number = cam_sim_unit(sim);
    cpi->bus_id = cam_sim_bus(sim);
    // rate is set when XPT_GET_TRAN_SETTINGS is processed
    cpi->base_transfer_speed = 150000;
    cpi->transport = XPORT_SAS;
    cpi->transport_version = 0;
    cpi->protocol = PROTO_SCSI;
    cpi->protocol_version = SCSI_REV_SPC3;
    cpi->ccb_h.status = CAM_REQ_CMP;
    break;
  }
  case XPT_GET_TRAN_SETTINGS:
  {
    struct ccb_trans_settings	*cts;
    struct ccb_trans_settings_sas *sas;
    struct ccb_trans_settings_scsi	*scsi;

    if ( pmcsc->flags & AGTIAPI_SHUT_DOWN )
    {
      return;
    }

    cts = &ccb->cts;
    sas = &ccb->cts.xport_specific.sas;
    scsi = &cts->proto_specific.scsi;

    cts->protocol = PROTO_SCSI;
    cts->protocol_version = SCSI_REV_SPC3;
    cts->transport = XPORT_SAS;
    cts->transport_version = 0;

    sas->valid = CTS_SAS_VALID_SPEED;

    /* this sets the "MB/s transfers" */ 
    if (pmcsc != NULL && targetID >= 0 && targetID < maxTargets)
    {
      if (pmcsc->pWWNList != NULL)
      {
        TID = INDEX(pmcsc, targetID);
        if (TID < maxTargets)
        {
          pDevHandle = pmcsc->pDevList[TID].pDevHandle;
        }
      }
    }
    if (pDevHandle)
    {
      tiINIGetDeviceInfo( &pmcsc->tiRoot, pDevHandle, &devInfo );
      switch (devInfo.info.devType_S_Rate & 0xF)
      {
        case 0x8: speed = 150000;
          break;
        case 0x9: speed = 300000;
          break;
        case 0xA: speed = 600000;
          break;
        case 0xB: speed = 1200000;
          break;
        default:  speed = 150000;
          break;
      }
    }
    sas->bitrate      = speed;
    scsi->valid       = CTS_SCSI_VALID_TQ;
    scsi->flags       = CTS_SCSI_FLAGS_TAG_ENB;
    ccb->ccb_h.status = CAM_REQ_CMP;
    break;
  }  
  case XPT_RESET_BUS:
  {
    lRetVal = agtiapi_eh_HostReset( pmcsc, ccb ); // usually works first time
    if ( SUCCESS == lRetVal )
    {
      AGTIAPI_PRINTK( "agtiapi_cam_action: bus reset success.\n" );
    }
    else
    {
      AGTIAPI_PRINTK( "agtiapi_cam_action: bus reset failed.\n" );
    }
    ccb->ccb_h.status = CAM_REQ_CMP;
    break;
  }
  case XPT_RESET_DEV:
  {
    ccb->ccb_h.status = CAM_REQ_CMP;
    break;
  }
  case XPT_ABORT:
  {
    ccb->ccb_h.status = CAM_REQ_CMP;
    break;
  }
#if __FreeBSD_version >= 900026
  case XPT_SMP_IO:
  {
    agtiapi_QueueSMP( pmcsc, ccb );
    return;
  }
#endif /* __FreeBSD_version >= 900026 */
  case XPT_SCSI_IO:
  {
    if(pmcsc->dev_scan == agFALSE)
    {
       ccb->ccb_h.status = CAM_SEL_TIMEOUT;  
       break;
    }
    if (pmcsc->flags & AGTIAPI_SHUT_DOWN)
    {
      AGTIAPI_PRINTK( "agtiapi_cam_action: shutdown, XPT_SCSI_IO 0x%x\n",
                      XPT_SCSI_IO );
      ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
      break;
    }
    else
    {
      AGTIAPI_IO( "agtiapi_cam_action: Zero XPT_SCSI_IO 0x%x, doing IOs\n",
                  XPT_SCSI_IO );
      agtiapi_QueueCmnd_( pmcsc, ccb );
      return;
    }
  }

  case XPT_CALC_GEOMETRY:
  {
	  cam_calc_geometry(&ccb->ccg, 1);
	  ccb->ccb_h.status = CAM_REQ_CMP;
	  break;
  }	  
  default:
  {
    /*
      XPT_SET_TRAN_SETTINGS	
    */
    AGTIAPI_IO( "agtiapi_cam_action: default function code 0x%x\n",
                ccb->ccb_h.func_code );
    ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
    break;
  }
  } /* switch */
  xpt_done(ccb);
}


/******************************************************************************
agtiapi_GetCCB()

Purpose:
  Get a ccb from free list or allocate a new one
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Pointer to HBA structure
Return:
  Pointer to a ccb structure, or NULL if not available
Note:
******************************************************************************/
STATIC pccb_t agtiapi_GetCCB( struct agtiapi_softc *pmcsc )
{
  pccb_t pccb;

  AGTIAPI_IO( "agtiapi_GetCCB: start\n" );

  AG_LOCAL_LOCK( &pmcsc->ccbLock );

  /* get the ccb from the head of the free list */
  if ((pccb = (pccb_t)pmcsc->ccbFreeList) != NULL)
  {
    pmcsc->ccbFreeList = (caddr_t *)pccb->pccbNext;
    pccb->pccbNext = NULL;
    pccb->flags = ACTIVE;
    pccb->startTime = 0;
    pmcsc->activeCCB++;
    AGTIAPI_IO( "agtiapi_GetCCB: re-allocated ccb %p\n", pccb );
  }
  else
  {
    AGTIAPI_PRINTK( "agtiapi_GetCCB: kmalloc ERROR - no ccb allocated\n" );
  }

  AG_LOCAL_UNLOCK( &pmcsc->ccbLock );
  return pccb;
}

/******************************************************************************
agtiapi_QueueCmnd_()

Purpose:
  Calls for sending CCB and excuting on HBA.
Parameters:
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
  union ccb * ccb (IN)      Pointer to CAM ccb data structure
Return:
  0 - Command is pending to execute
  1 - Command returned without further process
Note:
******************************************************************************/
int agtiapi_QueueCmnd_(struct agtiapi_softc *pmcsc, union ccb * ccb)
{
  struct ccb_scsiio *csio = &ccb->csio;
  pccb_t     pccb = agNULL; // call dequeue
  int        status = tiSuccess;
  U32        Channel = CMND_TO_CHANNEL(ccb);
  U32        TID     = CMND_TO_TARGET(ccb);
  U32        LUN     = CMND_TO_LUN(ccb);

  AGTIAPI_IO( "agtiapi_QueueCmnd_: start\n" );

  /* no support for CBD > 16 */
  if (csio->cdb_len > 16)
  {
    AGTIAPI_PRINTK( "agtiapi_QueueCmnd_: unsupported CDB length %d\n",
                    csio->cdb_len );
    ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
    ccb->ccb_h.status &= ~CAM_STATUS_MASK;
    ccb->ccb_h.status |= CAM_REQ_INVALID;//CAM_REQ_CMP;
    xpt_done(ccb);
    return tiError;
  }
  if (TID < 0 || TID >= maxTargets)
  {
    AGTIAPI_PRINTK("agtiapi_QueueCmnd_: INVALID TID ERROR\n");
    ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
    ccb->ccb_h.status &= ~CAM_STATUS_MASK;
    ccb->ccb_h.status |= CAM_DEV_NOT_THERE;//CAM_REQ_CMP;
    xpt_done(ccb);
    return tiError;
  }
  /* get a ccb */
  if ((pccb = agtiapi_GetCCB(pmcsc)) == NULL)
  {
    AGTIAPI_PRINTK("agtiapi_QueueCmnd_: GetCCB ERROR\n");
    if (pmcsc != NULL)
    {
      ag_device_t *targ;
      TID = INDEX(pmcsc, TID);
      targ   = &pmcsc->pDevList[TID];
      agtiapi_adjust_queue_depth(ccb->ccb_h.path,targ->qdepth);
    }
    ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
    ccb->ccb_h.status &= ~CAM_STATUS_MASK;
    ccb->ccb_h.status |= CAM_REQUEUE_REQ;
    xpt_done(ccb);
    return tiBusy;
  }
  pccb->pmcsc = pmcsc;
  /* initialize Command Control Block (CCB) */
  pccb->targetId   = TID;
  pccb->lun        = LUN;
  pccb->channel    = Channel;
  pccb->ccb        = ccb; /* for struct scsi_cmnd */
  pccb->senseLen   = csio->sense_len;
  pccb->startTime  = ticks;
  pccb->pSenseData = (caddr_t) &csio->sense_data;
  pccb->tiSuperScsiRequest.flags = 0;

  /* each channel is reserved for different addr modes */
  pccb->addrMode = agtiapi_AddrModes[Channel];

  status = agtiapi_PrepareSGList(pmcsc, pccb);
  if (status != tiSuccess)
  {
    AGTIAPI_PRINTK("agtiapi_QueueCmnd_: agtiapi_PrepareSGList failure\n");
    agtiapi_FreeCCB(pmcsc, pccb);
    if (status == tiReject)
    {
      ccb->ccb_h.status = CAM_REQ_INVALID;
    }
    else
    {
      ccb->ccb_h.status = CAM_REQ_CMP;
    }
    xpt_done( ccb );
    return tiError;
  }
  return status;
}

/******************************************************************************
agtiapi_DumpCDB()

Purpose:
  Prints out CDB
Parameters:
  const char *ptitle (IN)  A string to be printed
  ccb_t *pccb (IN)         A pointer to the driver's own CCB, not CAM's CCB
Return:
Note:
******************************************************************************/
STATIC void agtiapi_DumpCDB(const char *ptitle, ccb_t *pccb)
{
  union ccb *ccb;
  struct ccb_scsiio *csio;
  bit8  cdb[64];
  int len;

  if (pccb == NULL)
  {
    printf( "agtiapi_DumpCDB: no pccb here \n" );
    panic("agtiapi_DumpCDB: pccb is NULL. called from %s\n", ptitle);
    return;
  }
  ccb = pccb->ccb;
  if (ccb == NULL)
  {
    printf( "agtiapi_DumpCDB: no ccb here \n" );
    panic( "agtiapi_DumpCDB: pccb %p ccb %p flags %d ccb NULL! "
           "called from %s\n",
           pccb, pccb->ccb, pccb->flags, ptitle );
    return;
  }
  csio = &ccb->csio;
  if (csio == NULL)
  {
    printf( "agtiapi_DumpCDB: no csio here \n" );
    panic( "agtiapi_DumpCDB: pccb%p ccb%p flags%d csio NULL! called from %s\n",
           pccb, pccb->ccb, pccb->flags, ptitle );
    return;
  }
  len = MIN(64, csio->cdb_len);
  if (csio->ccb_h.flags & CAM_CDB_POINTER)
  {
    bcopy(csio->cdb_io.cdb_ptr, &cdb[0], len);
  }
  else
  {
    bcopy(csio->cdb_io.cdb_bytes, &cdb[0], len);
  }

  AGTIAPI_IO( "agtiapi_DumpCDB: pccb%p CDB0x%x csio->cdb_len %d"
              " len %d from %s\n",
              pccb, cdb[0],
              csio->cdb_len,
              len,
              ptitle );
  return;
}

/******************************************************************************
agtiapi_DoSoftReset()

Purpose:
  Do card reset
Parameters:
  *data (IN)               point to pmcsc (struct agtiapi_softc *)
Return:
Note:
******************************************************************************/
int agtiapi_DoSoftReset (struct agtiapi_softc *pmcsc)
{
  int  ret;
  unsigned long flags;

  pmcsc->flags |=  AGTIAPI_SOFT_RESET;
  AG_SPIN_LOCK_IRQ( agtiapi_host_lock, flags );
  ret = agtiapi_ResetCard( pmcsc, &flags );
  AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, flags );
                 
  if( ret != AGTIAPI_SUCCESS )
    return tiError;
                
  return SUCCESS;
}

/******************************************************************************
agtiapi_CheckIOTimeout()

Purpose:
  Timeout function for SCSI IO or TM 
Parameters:
  *data (IN)               point to pCard (ag_card_t *)
Return:
Note:
******************************************************************************/
STATIC void agtiapi_CheckIOTimeout(void *data)
{
  U32       status = AGTIAPI_SUCCESS;
  ccb_t *pccb;
  struct agtiapi_softc *pmcsc;
  pccb_t pccb_curr;
  pccb_t pccb_next;
  pmcsc = (struct agtiapi_softc *)data;

  //AGTIAPI_PRINTK("agtiapi_CheckIOTimeout: Enter\n");

  //AGTIAPI_PRINTK("agtiapi_CheckIOTimeout: Active CCB %d\n", pmcsc->activeCCB);

  pccb = (pccb_t)pmcsc->ccbChainList;

  /* if link is down, do nothing */
  if ((pccb == NULL) || (pmcsc->activeCCB == 0))
  {
  //AGTIAPI_PRINTK("agtiapi_CheckIOTimeout: goto restart_timer\n");
    goto restart_timer;
  }

  AG_SPIN_LOCK_IRQ(agtiapi_host_lock, flags);
  if (pmcsc->flags & AGTIAPI_SHUT_DOWN)
    goto ext;

  pccb_curr = pccb;

  /* Walk thorugh the IO Chain linked list to find the pending io */
  /* Set the TM flag based on the pccb type, i.e SCSI IO or TM cmd */
  while (pccb_curr != NULL)
  {
    /* start from 1st ccb in the chain */
    pccb_next = pccb_curr->pccbChainNext;
    if( (pccb_curr->flags == 0) || (pccb_curr->tiIORequest.tdData == NULL) ||
        (pccb_curr->startTime == 0) /* && (pccb->startTime == 0) */)
    {
      //AGTIAPI_PRINTK("agtiapi_CheckIOTimeout: move to next element\n");
    }
    else if ( ( (ticks-pccb_curr->startTime) >= ag_timeout_secs ) &&
              !(pccb_curr->flags & TIMEDOUT) )
    {
      AGTIAPI_PRINTK( "agtiapi_CheckIOTimeout: pccb %p timed out, call TM "
		      "function -- flags=%x startTime=%ld tdData = %p\n",
		      pccb_curr, pccb_curr->flags, pccb->startTime,
		      pccb_curr->tiIORequest.tdData );
      pccb_curr->flags |= TIMEDOUT;
      status = agtiapi_StartTM(pmcsc, pccb_curr);
      if (status == AGTIAPI_SUCCESS)
      {
        AGTIAPI_PRINTK( "agtiapi_CheckIOTimeout: TM Request sent with "
                        "success\n" );
        goto restart_timer;
      }
      else
      {
#ifdef AGTIAPI_LOCAL_RESET
        /* abort request did not go through */
        AGTIAPI_PRINTK("agtiapi_CheckIOTimeout: Abort request failed\n");
        /* TODO: call Soft reset here */
        AGTIAPI_PRINTK( "agtiapi_CheckIOTimeout:in agtiapi_CheckIOTimeout() "
                        "abort request did not go thru ==> soft reset#7, then "
                        "restart timer\n" );
        agtiapi_DoSoftReset (pmcsc);
        goto restart_timer;
#endif
      }
    }
    pccb_curr = pccb_next;
  }
restart_timer:
  callout_reset(&pmcsc->IO_timer, 1*hz, agtiapi_CheckIOTimeout, pmcsc);

ext:
  AG_SPIN_UNLOCK_IRQ(agtiapi_host_lock, flags);
  return;
}

/******************************************************************************
agtiapi_StartTM()

Purpose:
  DDI calls for aborting outstanding IO command 
Parameters: 
  struct scsi_cmnd *pccb (IN) Pointer to the command to be aborted  
  unsigned long flags (IN/out) spinlock flags used in locking from 
                              calling layers
Return:
  AGTIAPI_SUCCESS  - success
  AGTIAPI_FAIL     - fail
******************************************************************************/
int
agtiapi_StartTM(struct agtiapi_softc *pCard, ccb_t *pccb)
{
  ccb_t     *pTMccb = NULL;
  U32       status = AGTIAPI_SUCCESS;
  ag_device_t      *pDevice = NULL;
  U32       TMstatus = tiSuccess;
  AGTIAPI_PRINTK( "agtiapi_StartTM: pccb %p, pccb->flags %x\n",
                  pccb, pccb->flags );
  if (pccb == NULL)
  {
    AGTIAPI_PRINTK("agtiapi_StartTM: %p not found\n",pccb);
    status = AGTIAPI_SUCCESS;
    goto ext;
  }
  if (!pccb->tiIORequest.tdData)
  {
    /* should not be the case */
    AGTIAPI_PRINTK("agtiapi_StartTM: ccb %p flag 0x%x tid %d no tdData "
                   "ERROR\n", pccb, pccb->flags, pccb->targetId);
    status = AGTIAPI_FAIL;
  }
  else
  {
    /* If timedout CCB is TM_ABORT_TASK command, issue LocalAbort first to
       clear pending TM_ABORT_TASK */
    /* Else Device State will not be put back to Operational, (refer FW) */
    if (pccb->flags & TASK_MANAGEMENT)
    {
      if (tiINIIOAbort(&pCard->tiRoot, &pccb->tiIORequest) != tiSuccess)
      {
        AGTIAPI_PRINTK( "agtiapi_StartTM: LocalAbort Request for Abort_TASK "
                        "TM failed\n" );
        /* TODO: call Soft reset here */
        AGTIAPI_PRINTK( "agtiapi_StartTM: in agtiapi_StartTM() abort "
			"tiINIIOAbort() failed ==> soft reset#8\n" );
        agtiapi_DoSoftReset( pCard );
      }
      else
      {
        AGTIAPI_PRINTK( "agtiapi_StartTM: LocalAbort for Abort_TASK TM "
                        "Request sent\n" );
        status = AGTIAPI_SUCCESS; 
      }
    }
    else
    {
      /* get a ccb */
      if ((pTMccb = agtiapi_GetCCB(pCard)) == NULL)
      {
        AGTIAPI_PRINTK("agtiapi_StartTM: TM resource unavailable!\n");
        status = AGTIAPI_FAIL;
        goto ext;
      }
      pTMccb->pmcsc = pCard;
      pTMccb->targetId = pccb->targetId;
      pTMccb->devHandle = pccb->devHandle;
      if (pTMccb->targetId >= pCard->devDiscover)
      {
        AGTIAPI_PRINTK("agtiapi_StartTM: Incorrect dev Id in TM!\n");
        status = AGTIAPI_FAIL;
        goto ext;
      }
      if (pTMccb->targetId < 0 || pTMccb->targetId >= maxTargets)
      {
        return AGTIAPI_FAIL;
      }
      if (INDEX(pCard, pTMccb->targetId) >= maxTargets)
      {
        return AGTIAPI_FAIL;
      }
      pDevice = &pCard->pDevList[INDEX(pCard, pTMccb->targetId)];
      if ((pDevice == NULL) || !(pDevice->flags & ACTIVE))
      {
        return AGTIAPI_FAIL;
      }

      /* save pending io to issue local abort at Task mgmt CB */
      pTMccb->pccbIO = pccb;
      AGTIAPI_PRINTK( "agtiapi_StartTM: pTMccb %p flag %x tid %d via TM "
                      "request !\n",
                      pTMccb, pTMccb->flags, pTMccb->targetId );
      pTMccb->flags &= ~(TASK_SUCCESS | ACTIVE);
      pTMccb->flags |= TASK_MANAGEMENT;
      TMstatus = tiINITaskManagement(&pCard->tiRoot, 
                              pccb->devHandle,
                              AG_ABORT_TASK,
                              &pccb->tiSuperScsiRequest.scsiCmnd.lun,
                              &pccb->tiIORequest, 
                              &pTMccb->tiIORequest); 
      if (TMstatus == tiSuccess)
      {
        AGTIAPI_PRINTK( "agtiapi_StartTM: TM_ABORT_TASK request success ccb "
                        "%p, pTMccb %p\n", 
                        pccb, pTMccb );
        pTMccb->startTime = ticks;
        status = AGTIAPI_SUCCESS; 
      }
      else if (TMstatus == tiIONoDevice)
      {
        AGTIAPI_PRINTK( "agtiapi_StartTM: TM_ABORT_TASK request tiIONoDevice ccb "
                        "%p, pTMccb %p\n", 
                        pccb, pTMccb );
        status = AGTIAPI_SUCCESS; 
      }
      else
      {
        AGTIAPI_PRINTK( "agtiapi_StartTM: TM_ABORT_TASK request failed ccb %p, "
                        "pTMccb %p\n", 
                        pccb, pTMccb );
        status = AGTIAPI_FAIL;
        agtiapi_FreeTMCCB(pCard, pTMccb);
        /* TODO */
        /* call TM_TARGET_RESET */
      }
    }
  }
  ext:
  AGTIAPI_PRINTK("agtiapi_StartTM: return %d flgs %x\n", status, 
                 (pccb) ? pccb->flags : -1);
  return status;
} /* agtiapi_StartTM */

#if __FreeBSD_version > 901000
/******************************************************************************
agtiapi_PrepareSGList()

Purpose:
  This function prepares scatter-gather list for the given ccb
Parameters:
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
  ccb_t *pccb (IN)      A pointer to the driver's own CCB, not CAM's CCB
Return:
  0 - success
  1 - failure

Note:
******************************************************************************/
static int agtiapi_PrepareSGList(struct agtiapi_softc *pmcsc, ccb_t *pccb)
{
  union ccb *ccb = pccb->ccb;
  struct ccb_scsiio *csio = &ccb->csio;
  struct ccb_hdr *ccbh = &ccb->ccb_h;
  AGTIAPI_IO( "agtiapi_PrepareSGList: start\n" );

//  agtiapi_DumpCDB("agtiapi_PrepareSGList", pccb);
  AGTIAPI_IO( "agtiapi_PrepareSGList: dxfer_len %d\n", csio->dxfer_len );

  if ((ccbh->flags & CAM_DIR_MASK) != CAM_DIR_NONE) 
  {
	switch((ccbh->flags & CAM_DATA_MASK))
    	{
          int error;
          struct bus_dma_segment seg;
	  case CAM_DATA_VADDR:
        /* Virtual address that needs to translated into one or more physical address ranges. */
          //  int error;
            //  AG_LOCAL_LOCK(&(pmcsc->pCardInfo->pmIOLock));
            AGTIAPI_IO( "agtiapi_PrepareSGList: virtual address\n" );
            error = bus_dmamap_load( pmcsc->buffer_dmat,
                                 pccb->CCB_dmamap,
                                 csio->data_ptr,
                                 csio->dxfer_len,
                                 agtiapi_PrepareSGListCB,
                                 pccb,
                                 BUS_DMA_NOWAIT/* 0 */ );
            //  AG_LOCAL_UNLOCK( &(pmcsc->pCardInfo->pmIOLock) );

	    if (error == EINPROGRESS) 
	    {
          /* So as to maintain ordering, freeze the controller queue until our mapping is returned. */
          AGTIAPI_PRINTK("agtiapi_PrepareSGList: EINPROGRESS\n");
          xpt_freeze_simq(pmcsc->sim, 1);
          pmcsc->SimQFrozen = agTRUE;	  
          ccbh->status |= CAM_RELEASE_SIMQ;
        }
	break;
	case CAM_DATA_PADDR:
	    /* We have been given a pointer to single physical buffer. */
	    /* pccb->tiSuperScsiRequest.sglVirtualAddr = seg.ds_addr; */
          //struct bus_dma_segment seg;
          AGTIAPI_PRINTK("agtiapi_PrepareSGList: physical address\n");
          seg.ds_addr =
            (bus_addr_t)(vm_offset_t)csio->data_ptr;
             seg.ds_len = csio->dxfer_len;
             // * 0xFF to be defined
             agtiapi_PrepareSGListCB(pccb, &seg, 1, 0xAABBCCDD);
	     break;
	default:
           AGTIAPI_PRINTK("agtiapi_PrepareSGList: unexpected case\n");
           return tiReject;
    }
  }
  else 
  {
    agtiapi_PrepareSGListCB(pccb, NULL, 0, 0xAAAAAAAA);
  }
  return tiSuccess;
}
#else
/******************************************************************************
agtiapi_PrepareSGList()

Purpose:
  This function prepares scatter-gather list for the given ccb
Parameters:
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
  ccb_t *pccb (IN)      A pointer to the driver's own CCB, not CAM's CCB
Return:
  0 - success
  1 - failure

Note:
******************************************************************************/
static int agtiapi_PrepareSGList(struct agtiapi_softc *pmcsc, ccb_t *pccb)
{
  union ccb *ccb = pccb->ccb;
  struct ccb_scsiio *csio = &ccb->csio;
  struct ccb_hdr *ccbh = &ccb->ccb_h;
  AGTIAPI_IO( "agtiapi_PrepareSGList: start\n" );
//  agtiapi_DumpCDB("agtiapi_PrepareSGList", pccb);
  AGTIAPI_IO( "agtiapi_PrepareSGList: dxfer_len %d\n", csio->dxfer_len );

  if ((ccbh->flags & CAM_DIR_MASK) != CAM_DIR_NONE)
  {
    if ((ccbh->flags & CAM_SCATTER_VALID) == 0)
    {
      /* We've been given a pointer to a single buffer. */
      if ((ccbh->flags & CAM_DATA_PHYS) == 0) 
      {
        /* Virtual address that needs to translated into one or more physical address ranges. */
        int error;
      //  AG_LOCAL_LOCK(&(pmcsc->pCardInfo->pmIOLock));
        AGTIAPI_IO( "agtiapi_PrepareSGList: virtual address\n" );
        error = bus_dmamap_load( pmcsc->buffer_dmat,
                                 pccb->CCB_dmamap,
                                 csio->data_ptr,
                                 csio->dxfer_len,
                                 agtiapi_PrepareSGListCB,
                                 pccb,
                                 BUS_DMA_NOWAIT/* 0 */ );
      //  AG_LOCAL_UNLOCK( &(pmcsc->pCardInfo->pmIOLock) );

	    if (error == EINPROGRESS) 
	    {
          /* So as to maintain ordering, freeze the controller queue until our mapping is returned. */
          AGTIAPI_PRINTK("agtiapi_PrepareSGList: EINPROGRESS\n");
          xpt_freeze_simq(pmcsc->sim, 1);
          pmcsc->SimQFrozen = agTRUE;	  
          ccbh->status |= CAM_RELEASE_SIMQ;
        }
      }
      else
      {
	    /* We have been given a pointer to single physical buffer. */
	    /* pccb->tiSuperScsiRequest.sglVirtualAddr = seg.ds_addr; */
        struct bus_dma_segment seg;
        AGTIAPI_PRINTK("agtiapi_PrepareSGList: physical address\n");
        seg.ds_addr =
          (bus_addr_t)(vm_offset_t)csio->data_ptr;
        seg.ds_len = csio->dxfer_len;
        // * 0xFF to be defined
        agtiapi_PrepareSGListCB(pccb, &seg, 1, 0xAABBCCDD);
      }
    }
    else
    {
      
      AGTIAPI_PRINTK("agtiapi_PrepareSGList: unexpected case\n");
      return tiReject;
    }
  }
  else 
  {
    agtiapi_PrepareSGListCB(pccb, NULL, 0, 0xAAAAAAAA);
  }
  return tiSuccess;
}

#endif
/******************************************************************************
agtiapi_PrepareSGListCB()

Purpose:
  Callback function for bus_dmamap_load()
  This fuctions sends IO to LL layer.
Parameters:
  void *arg (IN)                Pointer to the HBA data structure
  bus_dma_segment_t *segs (IN)  Pointer to dma segment
  int nsegs (IN)                number of dma segment
  int error (IN)                error
Return:
Note:
******************************************************************************/
static void agtiapi_PrepareSGListCB( void *arg,
                                     bus_dma_segment_t *segs,
                                     int nsegs,
                                     int error )
{
  pccb_t     pccb = arg;
  union ccb *ccb = pccb->ccb;
  struct ccb_scsiio *csio = &ccb->csio;

  struct agtiapi_softc *pmcsc;
  tiIniScsiCmnd_t *pScsiCmnd;
  bit32 i;
  bus_dmasync_op_t op;
  U32_64     phys_addr;
  U08        *CDB; 
  int        io_is_encryptable = 0;
  unsigned long long start_lba = 0;
  ag_device_t *pDev;
  U32        TID     = CMND_TO_TARGET(ccb);

  AGTIAPI_IO( "agtiapi_PrepareSGListCB: start, nsegs %d error 0x%x\n",
              nsegs, error );
  pmcsc = pccb->pmcsc;
 
  if (error != tiSuccess)
  {
    if (error == 0xAABBCCDD || error == 0xAAAAAAAA)
    {
      // do nothing
    }
    else
    {
      AGTIAPI_PRINTK("agtiapi_PrepareSGListCB: error status 0x%x\n", error);
      bus_dmamap_unload(pmcsc->buffer_dmat, pccb->CCB_dmamap);
      bus_dmamap_destroy(pmcsc->buffer_dmat, pccb->CCB_dmamap);
      agtiapi_FreeCCB(pmcsc, pccb);
      ccb->ccb_h.status = CAM_REQ_CMP;
      xpt_done(ccb);
      return;
    }
  }

  if (nsegs > AGTIAPI_MAX_DMA_SEGS)
  {
    AGTIAPI_PRINTK( "agtiapi_PrepareSGListCB: over the limit. nsegs %d"
                    " AGTIAPI_MAX_DMA_SEGS %d\n", 
                    nsegs, AGTIAPI_MAX_DMA_SEGS );
    bus_dmamap_unload(pmcsc->buffer_dmat, pccb->CCB_dmamap);
    bus_dmamap_destroy(pmcsc->buffer_dmat, pccb->CCB_dmamap);
    agtiapi_FreeCCB(pmcsc, pccb);
    ccb->ccb_h.status = CAM_REQ_CMP;
    xpt_done(ccb);   
    return;
  }


  /* fill in IO information */
  pccb->dataLen = csio->dxfer_len;

  /* start fill in sgl structure */
  if (nsegs == 1 && error == 0xAABBCCDD)
  {
    /* to be tested */
    /* A single physical buffer */
    AGTIAPI_PRINTK("agtiapi_PrepareSGListCB: nsegs is 1\n");
    CPU_TO_LE32(pccb->tiSuperScsiRequest.agSgl1, segs[0].ds_addr);
    pccb->tiSuperScsiRequest.agSgl1.len   = htole32(pccb->dataLen);
    pccb->tiSuperScsiRequest.agSgl1.type  = htole32(tiSgl);
    pccb->tiSuperScsiRequest.sglVirtualAddr = (void *)segs->ds_addr;
    pccb->numSgElements = 1;
  }
  else if (nsegs == 0 && error == 0xAAAAAAAA)
  {
    /* no data transfer */
    AGTIAPI_IO( "agtiapi_PrepareSGListCB: no data transfer\n" );
    pccb->tiSuperScsiRequest.agSgl1.len = 0;
    pccb->dataLen = 0; 
    pccb->numSgElements = 0;  
  }
  else
  {
    /* virtual/logical buffer */
    if (nsegs == 1)
    {
      pccb->dataLen = segs[0].ds_len;

      CPU_TO_LE32(pccb->tiSuperScsiRequest.agSgl1, segs[0].ds_addr);     
      pccb->tiSuperScsiRequest.agSgl1.type = htole32(tiSgl);
      pccb->tiSuperScsiRequest.agSgl1.len = htole32(segs[0].ds_len);
      pccb->tiSuperScsiRequest.sglVirtualAddr = (void *)csio->data_ptr;
      pccb->numSgElements = nsegs;	      
			                
    }
    else
    {    
      pccb->dataLen = 0;
      /* loop */
      for (i = 0; i < nsegs; i++)
      {
        pccb->sgList[i].len = htole32(segs[i].ds_len);
        CPU_TO_LE32(pccb->sgList[i], segs[i].ds_addr);     
        pccb->sgList[i].type = htole32(tiSgl);
        pccb->dataLen += segs[i].ds_len;

      } /* for */
      pccb->numSgElements = nsegs;
      /* set up sgl buffer address */      
      CPU_TO_LE32(pccb->tiSuperScsiRequest.agSgl1,  pccb->tisgl_busaddr);
      pccb->tiSuperScsiRequest.agSgl1.type = htole32(tiSglList);
      pccb->tiSuperScsiRequest.agSgl1.len = htole32(pccb->dataLen);
      pccb->tiSuperScsiRequest.sglVirtualAddr = (void *)csio->data_ptr;
      pccb->numSgElements = nsegs;  
    } /* else */
  }

  /* set data transfer direction */
  if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) 
  {
    op = BUS_DMASYNC_PREWRITE;
    pccb->tiSuperScsiRequest.dataDirection = tiDirectionOut;
  }
  else 
  {
    op = BUS_DMASYNC_PREREAD;
    pccb->tiSuperScsiRequest.dataDirection = tiDirectionIn;
  }

  pScsiCmnd = &pccb->tiSuperScsiRequest.scsiCmnd;

  pScsiCmnd->expDataLength = pccb->dataLen;

  if (csio->ccb_h.flags & CAM_CDB_POINTER)
  {
    bcopy(csio->cdb_io.cdb_ptr, &pScsiCmnd->cdb[0], csio->cdb_len);
  }
  else
  {
    bcopy(csio->cdb_io.cdb_bytes, &pScsiCmnd->cdb[0],csio->cdb_len);
  }

  CDB = &pScsiCmnd->cdb[0];

  switch (CDB[0])
  {
  case REQUEST_SENSE:  /* requires different buffer */
    /* This code should not be excercised because SAS support auto sense 
       For the completeness, vtophys() is still used here.
     */
    AGTIAPI_PRINTK("agtiapi_PrepareSGListCB: QueueCmnd - REQUEST SENSE new\n");
    pccb->tiSuperScsiRequest.agSgl1.len = htole32(pccb->senseLen);
    phys_addr = vtophys(&csio->sense_data);
    CPU_TO_LE32(pccb->tiSuperScsiRequest.agSgl1, phys_addr);
    pccb->tiSuperScsiRequest.agSgl1.type  = htole32(tiSgl);
    pccb->dataLen = pccb->senseLen;
    pccb->numSgElements = 1;
    break;
  case INQUIRY:
    /* only using lun 0 for device type detection */
    pccb->flags |= AGTIAPI_INQUIRY;
    break;
  case TEST_UNIT_READY:
  case RESERVE:
  case RELEASE:
  case START_STOP:
  	pccb->tiSuperScsiRequest.agSgl1.len = 0;
    pccb->dataLen = 0;
    break;
  case READ_6:
  case WRITE_6:
    /* Extract LBA */
    start_lba = ((CDB[1] & 0x1f) << 16) |
                 (CDB[2] << 8)          |
                 (CDB[3]);
#ifdef HIALEAH_ENCRYPTION
    io_is_encryptable = 1;
#endif
    break;
  case READ_10:
  case WRITE_10:
  case READ_12:
  case WRITE_12:
    /* Extract LBA */
    start_lba = (CDB[2] << 24) |
                (CDB[3] << 16) |
                (CDB[4] << 8)  |
                (CDB[5]);
#ifdef HIALEAH_ENCRYPTION
    io_is_encryptable = 1;
#endif
    break;
  case READ_16:
  case WRITE_16:
    /* Extract LBA */
    start_lba = (CDB[2] << 24) |
                (CDB[3] << 16) |
                (CDB[4] << 8)  |
                (CDB[5]);
    start_lba <<= 32;
    start_lba |= ((CDB[6] << 24) |
                  (CDB[7] << 16) |
                  (CDB[8] << 8)  |
                  (CDB[9]));
#ifdef HIALEAH_ENCRYPTION
    io_is_encryptable = 1;
#endif
    break;
  default:
    break;
  }

  /* fill device lun based one address mode */
  agtiapi_SetLunField(pccb);

  if (pccb->targetId < 0 || pccb->targetId >= maxTargets)
  {
    pccb->ccbStatus   = tiIOFailed;
    pccb->scsiStatus  = tiDetailNoLogin;
    agtiapi_FreeCCB(pmcsc, pccb);
    ccb->ccb_h.status = CAM_DEV_NOT_THERE; // ## v. CAM_FUNC_NOTAVAIL
    xpt_done(ccb);
    pccb->ccb         = NULL;
    return;
  }
  if (INDEX(pmcsc, pccb->targetId) >= maxTargets)
  {
    pccb->ccbStatus   = tiIOFailed;
    pccb->scsiStatus  = tiDetailNoLogin;
    agtiapi_FreeCCB(pmcsc, pccb);
    ccb->ccb_h.status = CAM_DEV_NOT_THERE; // ## v. CAM_FUNC_NOTAVAIL
    xpt_done(ccb);
    pccb->ccb         = NULL;
    return;
  }
  pDev = &pmcsc->pDevList[INDEX(pmcsc, pccb->targetId)];

#if 1 
  if ((pmcsc->flags & EDC_DATA) &&
      (pDev->flags & EDC_DATA))
  {
    /*
     * EDC support:
     *
     * Possible command supported -
     * READ_6, READ_10, READ_12, READ_16, READ_LONG, READ_BUFFER,
     * READ_DEFECT_DATA, etc.
     * WRITE_6, WRITE_10, WRITE_12, WRITE_16, WRITE_LONG, WRITE_LONG2, 
     * WRITE_BUFFER, WRITE_VERIFY, WRITE_VERIFY_12, etc.
     *
     * Do some data length adjustment and set chip operation instruction.
     */
    switch (CDB[0])
    {
      case READ_6:
      case READ_10:
      case READ_12:
      case READ_16:
        //  BUG_ON(pccb->tiSuperScsiRequest.flags & TI_SCSI_INITIATOR_ENCRYPT);
#ifdef AGTIAPI_TEST_DIF
        pccb->tiSuperScsiRequest.flags |= TI_SCSI_INITIATOR_DIF;
#endif
        pccb->flags |= EDC_DATA;

#ifdef TEST_VERIFY_AND_FORWARD
        pccb->tiSuperScsiRequest.Dif.flags =
          DIF_VERIFY_FORWARD | DIF_UDT_REF_BLOCK_COUNT;
        if(pDev->sector_size == 520) {
            pScsiCmnd->expDataLength += (pccb->dataLen / 512) * 8;
        } else if(pDev->sector_size == 4104) {
            pScsiCmnd->expDataLength += (pccb->dataLen / 4096) * 8;
        }
#else
#ifdef AGTIAPI_TEST_DIF
        pccb->tiSuperScsiRequest.Dif.flags =
          DIF_VERIFY_DELETE | DIF_UDT_REF_BLOCK_COUNT;
#endif
#endif
#ifdef AGTIAPI_TEST_DIF
        switch(pDev->sector_size) {
            case 528:
                pccb->tiSuperScsiRequest.Dif.flags |=
                  ( DIF_BLOCK_SIZE_520 << 16 );
                break;
            case 4104:
                pccb->tiSuperScsiRequest.Dif.flags |=
                  ( DIF_BLOCK_SIZE_4096 << 16 );
                break;
            case 4168:
                pccb->tiSuperScsiRequest.Dif.flags |=
                  ( DIF_BLOCK_SIZE_4160 << 16 );
                break;
        }

        if(pCard->flags & EDC_DATA_CRC)
            pccb->tiSuperScsiRequest.Dif.flags |= DIF_CRC_VERIFICATION;

        /* Turn on upper 4 bits of UVM */
        pccb->tiSuperScsiRequest.Dif.flags |= 0x03c00000;

#endif
#ifdef AGTIAPI_TEST_DPL
        if(agtiapi_SetupDifPerLA(pCard, pccb, start_lba) < 0) {
            printk(KERN_ERR "SetupDifPerLA Failed.\n");
            cmnd->result = SCSI_HOST(DID_ERROR);
            goto err;
        }
        pccb->tiSuperScsiRequest.Dif.enableDIFPerLA = TRUE;        
#endif
#ifdef AGTIAPI_TEST_DIF
        /* Set App Tag */
        pccb->tiSuperScsiRequest.Dif.udtArray[0] = 0xaa;
        pccb->tiSuperScsiRequest.Dif.udtArray[1] = 0xbb;

        /* Set LBA in UDT array */
        if(CDB[0] == READ_6) {
            pccb->tiSuperScsiRequest.Dif.udtArray[2] = CDB[3];
            pccb->tiSuperScsiRequest.Dif.udtArray[3] = CDB[2];
            pccb->tiSuperScsiRequest.Dif.udtArray[4] = CDB[1] & 0x1f;
            pccb->tiSuperScsiRequest.Dif.udtArray[5] = 0;
        } else if(CDB[0] == READ_10 || CDB[0] == READ_12) {
            pccb->tiSuperScsiRequest.Dif.udtArray[2] = CDB[5];
            pccb->tiSuperScsiRequest.Dif.udtArray[3] = CDB[4];
            pccb->tiSuperScsiRequest.Dif.udtArray[4] = CDB[3];
            pccb->tiSuperScsiRequest.Dif.udtArray[5] = CDB[2];
        } else if(CDB[0] == READ_16) {
            pccb->tiSuperScsiRequest.Dif.udtArray[2] = CDB[9];
            pccb->tiSuperScsiRequest.Dif.udtArray[3] = CDB[8];
            pccb->tiSuperScsiRequest.Dif.udtArray[4] = CDB[7];
            pccb->tiSuperScsiRequest.Dif.udtArray[5] = CDB[6];
            /* Note: 32 bits lost */
        }
#endif

        break;
      case WRITE_6:
      case WRITE_10:
      case WRITE_12:
      case WRITE_16:
        //   BUG_ON(pccb->tiSuperScsiRequest.flags & TI_SCSI_INITIATOR_ENCRYPT);
        pccb->flags |= EDC_DATA;
#ifdef AGTIAPI_TEST_DIF
        pccb->tiSuperScsiRequest.flags |= TI_SCSI_INITIATOR_DIF;
        pccb->tiSuperScsiRequest.Dif.flags =
          DIF_INSERT | DIF_UDT_REF_BLOCK_COUNT;
        switch(pDev->sector_size) {
            case 528:
                pccb->tiSuperScsiRequest.Dif.flags |=
                  (DIF_BLOCK_SIZE_520 << 16);
                break;
            case 4104:
                pccb->tiSuperScsiRequest.Dif.flags |=
                  ( DIF_BLOCK_SIZE_4096 << 16 );
                break;
            case 4168:
                pccb->tiSuperScsiRequest.Dif.flags |=
                  ( DIF_BLOCK_SIZE_4160 << 16 );
                break;
        }

        /* Turn on upper 4 bits of UUM */
        pccb->tiSuperScsiRequest.Dif.flags |= 0xf0000000;
#endif
#ifdef AGTIAPI_TEST_DPL
        if(agtiapi_SetupDifPerLA(pCard, pccb, start_lba) < 0) {
            printk(KERN_ERR "SetupDifPerLA Failed.\n");
            cmnd->result = SCSI_HOST(DID_ERROR);
            goto err;
        }
        pccb->tiSuperScsiRequest.Dif.enableDIFPerLA = TRUE;
#endif
#ifdef AGTIAPI_TEST_DIF
        /* Set App Tag */
        pccb->tiSuperScsiRequest.Dif.udtArray[0] = 0xaa;
        pccb->tiSuperScsiRequest.Dif.udtArray[1] = 0xbb;

        /* Set LBA in UDT array */
        if(CDB[0] == WRITE_6) {
            pccb->tiSuperScsiRequest.Dif.udtArray[2] = CDB[3];
            pccb->tiSuperScsiRequest.Dif.udtArray[3] = CDB[2];
            pccb->tiSuperScsiRequest.Dif.udtArray[4] = CDB[1] & 0x1f;
        } else if(CDB[0] == WRITE_10 || CDB[0] == WRITE_12) {
            pccb->tiSuperScsiRequest.Dif.udtArray[2] = CDB[5];
            pccb->tiSuperScsiRequest.Dif.udtArray[3] = CDB[4];
            pccb->tiSuperScsiRequest.Dif.udtArray[4] = CDB[3];
            pccb->tiSuperScsiRequest.Dif.udtArray[5] = CDB[2];
        } else if(CDB[0] == WRITE_16) {
            pccb->tiSuperScsiRequest.Dif.udtArray[2] = CDB[5];
            pccb->tiSuperScsiRequest.Dif.udtArray[3] = CDB[4];
            pccb->tiSuperScsiRequest.Dif.udtArray[4] = CDB[3];
            pccb->tiSuperScsiRequest.Dif.udtArray[5] = CDB[2];
            /* Note: 32 bits lost */
        }
#endif
        break;
    }
  }
#endif /* end of DIF */

  if ((ccb->ccb_h.flags & CAM_TAG_ACTION_VALID) != 0)
  {
    switch(csio->tag_action)
    {
    case MSG_HEAD_OF_Q_TAG:
      pScsiCmnd->taskAttribute = TASK_HEAD_OF_QUEUE;
      break;
    case MSG_ACA_TASK:
      pScsiCmnd->taskAttribute = TASK_ACA;
      break;
    case MSG_ORDERED_Q_TAG:
      pScsiCmnd->taskAttribute = TASK_ORDERED;
      break;
    case MSG_SIMPLE_Q_TAG: /* fall through */
    default:
      pScsiCmnd->taskAttribute = TASK_SIMPLE;
      break;
    }
  }

  if (pccb->tiSuperScsiRequest.agSgl1.len != 0 && pccb->dataLen != 0)
  {
    /* should be just before start IO */
    bus_dmamap_sync(pmcsc->buffer_dmat, pccb->CCB_dmamap, op);
  }

  /*
   * If assigned pDevHandle is not available
   * then there is no need to send it to StartIO()
   */
  if (pccb->targetId < 0 || pccb->targetId >= maxTargets)
  {
    pccb->ccbStatus   = tiIOFailed;
    pccb->scsiStatus  = tiDetailNoLogin;
    agtiapi_FreeCCB(pmcsc, pccb);
    ccb->ccb_h.status = CAM_DEV_NOT_THERE; // ## v. CAM_FUNC_NOTAVAIL
    xpt_done(ccb);
    pccb->ccb         = NULL;
    return;
  }
  TID = INDEX(pmcsc, pccb->targetId);
  if ((TID >= pmcsc->devDiscover) ||
      !(pccb->devHandle = pmcsc->pDevList[TID].pDevHandle))
  {
    /*
    AGTIAPI_PRINTK( "agtiapi_PrepareSGListCB: not sending ccb devH %p,"
                    " target %d tid %d/%d card %p ERROR pccb %p\n",
                    pccb->devHandle, pccb->targetId, TID,
                    pmcsc->devDiscover, pmcsc, pccb );
    */
    pccb->ccbStatus   = tiIOFailed;
    pccb->scsiStatus  = tiDetailNoLogin;
    agtiapi_FreeCCB(pmcsc, pccb);
    ccb->ccb_h.status = CAM_DEV_NOT_THERE; // ## v. CAM_FUNC_NOTAVAIL
    xpt_done(ccb);
    pccb->ccb         = NULL;
    return;
  }
  AGTIAPI_IO( "agtiapi_PrepareSGListCB: send ccb pccb->devHandle %p, "
                  "pccb->targetId %d TID %d pmcsc->devDiscover %d card %p\n",
                  pccb->devHandle, pccb->targetId, TID, pmcsc->devDiscover,
                  pmcsc );
#ifdef HIALEAH_ENCRYPTION
  if(pmcsc->encrypt && io_is_encryptable) {
    agtiapi_SetupEncryptedIO(pmcsc, pccb, start_lba);
  } else{
	io_is_encryptable = 0;
	pccb->tiSuperScsiRequest.flags = 0;
  }
#endif
  // put the request in send queue
  agtiapi_QueueCCB( pmcsc, &pmcsc->ccbSendHead, &pmcsc->ccbSendTail
                    AG_CARD_LOCAL_LOCK(&pmcsc->sendLock), pccb );
  agtiapi_StartIO(pmcsc);
  return;
}

/******************************************************************************
agtiapi_StartIO()

Purpose:
  Send IO request down for processing.
Parameters:
  (struct agtiapi_softc *pmcsc (IN)  Pointer to HBA data structure
Return:
Note:
******************************************************************************/
STATIC void agtiapi_StartIO( struct agtiapi_softc *pmcsc )
{
  ccb_t *pccb;
  int TID;			
  ag_device_t *targ;	

  AGTIAPI_IO( "agtiapi_StartIO: start\n" );

  AG_LOCAL_LOCK( &pmcsc->sendLock );
  pccb = pmcsc->ccbSendHead;

  /* if link is down, do nothing */
  if ((pccb == NULL) || pmcsc->flags & AGTIAPI_RESET)
  {
    AG_LOCAL_UNLOCK( &pmcsc->sendLock );
    AGTIAPI_PRINTK( "agtiapi_StartIO: goto ext\n" );
    goto ext;
  }


 if (pmcsc != NULL && pccb->targetId >= 0 && pccb->targetId < maxTargets)
  {
      TID = INDEX(pmcsc, pccb->targetId);
      targ   = &pmcsc->pDevList[TID];
  }


  /* clear send queue */
  pmcsc->ccbSendHead = NULL;
  pmcsc->ccbSendTail = NULL;
  AG_LOCAL_UNLOCK( &pmcsc->sendLock );

  /* send all ccbs down */
  while (pccb) 
  {
    pccb_t pccb_next;
    U32    status;

    pccb_next = pccb->pccbNext;
    pccb->pccbNext = NULL;

    if (!pccb->ccb)
    {
      AGTIAPI_PRINTK( "agtiapi_StartIO: pccb->ccb is NULL ERROR!\n" );
      pccb = pccb_next;
      continue;
    }
    AG_IO_DUMPCCB( pccb );

    if (!pccb->devHandle)
    {
      agtiapi_DumpCCB( pccb );
      AGTIAPI_PRINTK( "agtiapi_StartIO: ccb NULL device ERROR!\n" );
      pccb = pccb_next;
      continue;
    }
    AGTIAPI_IO( "agtiapi_StartIO: ccb %p retry %d\n", pccb, pccb->retryCount );

#ifndef ABORT_TEST
    if( !pccb->devHandle || !pccb->devHandle->osData || /* in rmmod case */
        !(((ag_device_t *)(pccb->devHandle->osData))->flags & ACTIVE))
    {
      AGTIAPI_PRINTK( "agtiapi_StartIO: device %p not active! ERROR\n", 
                      pccb->devHandle );
      if( pccb->devHandle ) {
        AGTIAPI_PRINTK( "agtiapi_StartIO: device not active detail"
                        " -- osData:%p\n",
                        pccb->devHandle->osData );
        if( pccb->devHandle->osData ) {
          AGTIAPI_PRINTK( "agtiapi_StartIO: more device not active detail"
                          " -- active flag:%d\n",
                          ( (ag_device_t *)
                            (pccb->devHandle->osData))->flags & ACTIVE );
        }
      }
      pccb->ccbStatus  = tiIOFailed;
      pccb->scsiStatus = tiDetailNoLogin;
      agtiapi_Done( pmcsc, pccb );
      pccb = pccb_next;
      continue;
    }
#endif

#ifdef FAST_IO_TEST
    status = agtiapi_FastIOTest( pmcsc, pccb );
#else
    status = tiINISuperIOStart( &pmcsc->tiRoot, 
                                &pccb->tiIORequest,
                                pccb->devHandle, 
                                &pccb->tiSuperScsiRequest,
                                (void *)&pccb->tdIOReqBody,
                                tiInterruptContext );
#endif
    switch( status )
    {
      case tiSuccess:
        /*
        static int squelchCount = 0;
        if ( 200000 == squelchCount++ ) // squelch prints
        {
          AGTIAPI_PRINTK( "agtiapi_StartIO: tiINIIOStart stat tiSuccess %p\n",
                          pccb );
          squelchCount = 0; // reset count
        }
        */

 
        break;   
      case tiDeviceBusy:
        AGTIAPI_PRINTK( "agtiapi_StartIO: tiINIIOStart status tiDeviceBusy %p\n",
                        pccb->ccb );
#ifdef LOGEVENT 
        agtiapi_LogEvent( pmcsc,
                          IOCTL_EVT_SEV_INFORMATIONAL,
                          0,
                          agNULL,
                          0,
                          "tiINIIOStart tiDeviceBusy " );
#endif
        pccb->ccbStatus = tiIOFailed;
        pccb->scsiStatus = tiDeviceBusy;        
        agtiapi_Done(pmcsc, pccb);
        break;
      case tiBusy:
        
        AGTIAPI_PRINTK( "agtiapi_StartIO: tiINIIOStart status tiBusy %p\n",
                        pccb->ccb );
#ifdef LOGEVENT 
        agtiapi_LogEvent( pmcsc,
                          IOCTL_EVT_SEV_INFORMATIONAL,
                          0,
                          agNULL,
                          0,
                          "tiINIIOStart tiBusy " );
#endif

        pccb->ccbStatus = tiIOFailed;
        pccb->scsiStatus = tiBusy;        
        agtiapi_Done(pmcsc, pccb);

        break;
      case tiIONoDevice:
        AGTIAPI_PRINTK( "agtiapi_StartIO: tiINIIOStart status tiNoDevice %p "
                        "ERROR\n", pccb->ccb );
#ifdef LOGEVENT
        agtiapi_LogEvent( pmcsc,
                          IOCTL_EVT_SEV_INFORMATIONAL,
                          0,
                          agNULL,
                          0,
                          "tiINIIOStart invalid device handle " );
#endif
#ifndef ABORT_TEST
        /* return command back to OS due to no device available */
        ((ag_device_t *)(pccb->devHandle->osData))->flags &= ~ACTIVE;
        pccb->ccbStatus  = tiIOFailed;
        pccb->scsiStatus = tiDetailNoLogin;
        agtiapi_Done(pmcsc, pccb);
#else
        /* for short cable pull, we want IO retried - 3-18-2005 */
        agtiapi_QueueCCB(pmcsc, &pmcsc->ccbSendHead, &pmcsc->ccbSendTail
                         AG_CARD_LOCAL_LOCK(&pmcsc->sendLock), pccb); 
#endif
        break;
      case tiError:
        AGTIAPI_PRINTK("agtiapi_StartIO: tiINIIOStart status tiError %p\n",
                       pccb->ccb);
#ifdef LOGEVENT
        agtiapi_LogEvent(pmcsc, 
                         IOCTL_EVT_SEV_INFORMATIONAL, 
                         0, 
                         agNULL, 
                         0, 
                         "tiINIIOStart tiError ");
#endif
        pccb->ccbStatus  = tiIOFailed;
        pccb->scsiStatus = tiDetailOtherError;
        agtiapi_Done(pmcsc, pccb);
        break;
      default:
        AGTIAPI_PRINTK("agtiapi_StartIO: tiINIIOStart status default %x %p\n",
                       status, pccb->ccb);
#ifdef LOGEVENT
        agtiapi_LogEvent(pmcsc, 
                         IOCTL_EVT_SEV_ERROR, 
                         0, 
                         agNULL, 
                         0, 
                         "tiINIIOStart unexpected status ");
#endif
        pccb->ccbStatus  = tiIOFailed;
        pccb->scsiStatus = tiDetailOtherError;
        agtiapi_Done(pmcsc, pccb);
    }
    
    pccb = pccb_next;
  }
ext:
  /* some IO requests might have been completed */
  AG_GET_DONE_PCCB(pccb, pmcsc);
  return;
}

/******************************************************************************
agtiapi_StartSMP()

Purpose:
  Send SMP request down for processing.
Parameters:
  (struct agtiapi_softc *pmcsc (IN)  Pointer to HBA data structure
Return:
Note:
******************************************************************************/
STATIC void agtiapi_StartSMP(struct agtiapi_softc *pmcsc)
{
  ccb_t *pccb;

  AGTIAPI_PRINTK("agtiapi_StartSMP: start\n");

  AG_LOCAL_LOCK(&pmcsc->sendSMPLock);
  pccb = pmcsc->smpSendHead;

  /* if link is down, do nothing */
  if ((pccb == NULL) || pmcsc->flags & AGTIAPI_RESET)
  {
    AG_LOCAL_UNLOCK(&pmcsc->sendSMPLock);
    AGTIAPI_PRINTK("agtiapi_StartSMP: goto ext\n");
    goto ext;
  }

  /* clear send queue */
  pmcsc->smpSendHead = NULL;
  pmcsc->smpSendTail = NULL;
  AG_LOCAL_UNLOCK(&pmcsc->sendSMPLock);

  /* send all ccbs down */
  while (pccb)
  {
    pccb_t pccb_next;
    U32    status;

    pccb_next = pccb->pccbNext;
    pccb->pccbNext = NULL;

    if (!pccb->ccb)
    {
      AGTIAPI_PRINTK("agtiapi_StartSMP: pccb->ccb is NULL ERROR!\n");
      pccb = pccb_next;
      continue;
    }

    if (!pccb->devHandle)
    {
      AGTIAPI_PRINTK("agtiapi_StartSMP: ccb NULL device ERROR!\n");
      pccb = pccb_next;
      continue;
    }
    pccb->flags |= TAG_SMP; // mark as SMP for later tracking
    AGTIAPI_PRINTK( "agtiapi_StartSMP: ccb %p retry %d\n",
                    pccb, pccb->retryCount );
    status = tiINISMPStart( &pmcsc->tiRoot, 
                            &pccb->tiIORequest,
                            pccb->devHandle, 
                            &pccb->tiSMPFrame,
                            (void *)&pccb->tdIOReqBody,
                            tiInterruptContext);

    switch (status)
    {
    case tiSuccess:
      break;
    case tiBusy:
      AGTIAPI_PRINTK("agtiapi_StartSMP: tiINISMPStart status tiBusy %p\n",
                     pccb->ccb);
      /* pending ccb back to send queue */
      agtiapi_QueueCCB(pmcsc, &pmcsc->smpSendHead, &pmcsc->smpSendTail
                       AG_CARD_LOCAL_LOCK(&pmcsc->sendSMPLock), pccb);
      break;
    case tiError:
      AGTIAPI_PRINTK("agtiapi_StartIO: tiINIIOStart status tiError %p\n",
                     pccb->ccb);
      pccb->ccbStatus = tiSMPFailed;
      agtiapi_SMPDone(pmcsc, pccb);
      break;
    default:
      AGTIAPI_PRINTK("agtiapi_StartIO: tiINIIOStart status default %x %p\n",
                     status, pccb->ccb);
      pccb->ccbStatus = tiSMPFailed;
      agtiapi_SMPDone(pmcsc, pccb);
    }

    pccb = pccb_next;
  }
  ext:
  /* some SMP requests might have been completed */
  AG_GET_DONE_SMP_PCCB(pccb, pmcsc);

  return;
}

#if __FreeBSD_version > 901000
/******************************************************************************
agtiapi_PrepareSMPSGList()

Purpose:
  This function prepares scatter-gather list for the given ccb
Parameters:
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
  ccb_t *pccb (IN)      A pointer to the driver's own CCB, not CAM's CCB
Return:
  0 - success
  1 - failure

Note:
******************************************************************************/
static int agtiapi_PrepareSMPSGList( struct agtiapi_softc *pmcsc, ccb_t *pccb )
{
  /* Pointer to CAM's ccb */
  union ccb *ccb = pccb->ccb;
  struct ccb_smpio *csmpio = &ccb->smpio;
  struct ccb_hdr *ccbh = &ccb->ccb_h;

  AGTIAPI_PRINTK("agtiapi_PrepareSMPSGList: start\n");
  switch((ccbh->flags & CAM_DATA_MASK))
  {
    case CAM_DATA_PADDR:
    case CAM_DATA_SG_PADDR:
      AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGList: Physical Address not supported\n");
      ccb->ccb_h.status = CAM_REQ_INVALID;
      xpt_done(ccb);
      return tiReject;
    case CAM_DATA_SG:

    /* 
     * Currently we do not support Multiple SG list 
     * return error for now 
     */
      if ( (csmpio->smp_request_sglist_cnt > 1)
           || (csmpio->smp_response_sglist_cnt > 1) )
      {
        AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGList: Multiple SG list not supported\n");
        ccb->ccb_h.status = CAM_REQ_INVALID;
        xpt_done(ccb);
        return tiReject;
      }
    }
    if ( csmpio->smp_request_sglist_cnt != 0 )
    {
      /* 
       * Virtual address that needs to translated into
       * one or more physical address ranges.
       */
      int error;
      //AG_LOCAL_LOCK(&(pmcsc->pCardInfo->pmIOLock));  
      AGTIAPI_PRINTK("agtiapi_PrepareSGList: virtual address\n");
      error = bus_dmamap_load( pmcsc->buffer_dmat,
                               pccb->CCB_dmamap, 
                               csmpio->smp_request, 
                               csmpio->smp_request_len,
                               agtiapi_PrepareSMPSGListCB, 
                               pccb, 
                               BUS_DMA_NOWAIT /* 0 */ );
      
      //AG_LOCAL_UNLOCK(&(pmcsc->pCardInfo->pmIOLock));  

      if (error == EINPROGRESS)
      {
        /*
         * So as to maintain ordering,
         * freeze the controller queue
         * until our mapping is
         * returned.
         */
        AGTIAPI_PRINTK( "agtiapi_PrepareSGList: EINPROGRESS\n" );
        xpt_freeze_simq( pmcsc->sim, 1 );
        pmcsc->SimQFrozen = agTRUE;	
        ccbh->status |= CAM_RELEASE_SIMQ;
      }
    }
    if( csmpio->smp_response_sglist_cnt != 0 )
    {
      /*
       * Virtual address that needs to translated into
       * one or more physical address ranges.
       */
      int error;
      //AG_LOCAL_LOCK( &(pmcsc->pCardInfo->pmIOLock) );  
      AGTIAPI_PRINTK( "agtiapi_PrepareSGList: virtual address\n" );
      error = bus_dmamap_load( pmcsc->buffer_dmat,
                               pccb->CCB_dmamap, 
                               csmpio->smp_response, 
                               csmpio->smp_response_len,
                               agtiapi_PrepareSMPSGListCB, 
                               pccb, 
                               BUS_DMA_NOWAIT /* 0 */ );
      
      //AG_LOCAL_UNLOCK( &(pmcsc->pCardInfo->pmIOLock) );

      if ( error == EINPROGRESS )
      {
        /*
         * So as to maintain ordering,
         * freeze the controller queue
         * until our mapping is
         * returned.
         */
        AGTIAPI_PRINTK( "agtiapi_PrepareSGList: EINPROGRESS\n" );
        xpt_freeze_simq( pmcsc->sim, 1 );
        pmcsc->SimQFrozen = agTRUE;	
        ccbh->status |= CAM_RELEASE_SIMQ;
      }
    }
 
  else
  {
    if ( (csmpio->smp_request_sglist_cnt == 0) &&
         (csmpio->smp_response_sglist_cnt == 0) )
    {
      AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGList: physical address\n" );
      pccb->tiSMPFrame.outFrameBuf = (void *)csmpio->smp_request;
      pccb->tiSMPFrame.outFrameLen = csmpio->smp_request_len;
      pccb->tiSMPFrame.expectedRespLen = csmpio->smp_response_len;

      // 0xFF to be defined
      agtiapi_PrepareSMPSGListCB( pccb, NULL, 0, 0xAABBCCDD );
    }
    pccb->tiSMPFrame.flag = 0;
  }

  return tiSuccess;
}
#else

/******************************************************************************
agtiapi_PrepareSMPSGList()

Purpose:
  This function prepares scatter-gather list for the given ccb
Parameters:
  struct agtiapi_softc *pmsc (IN)  Pointer to the HBA data structure
  ccb_t *pccb (IN)      A pointer to the driver's own CCB, not CAM's CCB
Return:
  0 - success
  1 - failure

Note:
******************************************************************************/
static int agtiapi_PrepareSMPSGList( struct agtiapi_softc *pmcsc, ccb_t *pccb )
{
  /* Pointer to CAM's ccb */
  union ccb *ccb = pccb->ccb;
  struct ccb_smpio *csmpio = &ccb->smpio;
  struct ccb_hdr *ccbh = &ccb->ccb_h;

  AGTIAPI_PRINTK("agtiapi_PrepareSMPSGList: start\n");

  if (ccbh->flags & (CAM_DATA_PHYS|CAM_SG_LIST_PHYS)) 
  {
    AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGList: Physical Address "
                    "not supported\n" );
    ccb->ccb_h.status = CAM_REQ_INVALID;
    xpt_done(ccb);
    return tiReject;;
  }

  if (ccbh->flags & CAM_SCATTER_VALID)
  {
    /* 
     * Currently we do not support Multiple SG list 
     * return error for now 
     */
    if ( (csmpio->smp_request_sglist_cnt > 1)
         || (csmpio->smp_response_sglist_cnt > 1) )
    {
      AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGList: Multiple SG list "
                      "not supported\n" );
      ccb->ccb_h.status = CAM_REQ_INVALID;
      xpt_done(ccb);
      return tiReject;;
    }
    if ( csmpio->smp_request_sglist_cnt != 0 )
    {
      /* 
       * Virtual address that needs to translated into
       * one or more physical address ranges.
       */
      int error;
      //AG_LOCAL_LOCK(&(pmcsc->pCardInfo->pmIOLock));  
      AGTIAPI_PRINTK("agtiapi_PrepareSGList: virtual address\n");
      error = bus_dmamap_load( pmcsc->buffer_dmat,
                               pccb->CCB_dmamap, 
                               csmpio->smp_request, 
                               csmpio->smp_request_len,
                               agtiapi_PrepareSMPSGListCB, 
                               pccb, 
                               BUS_DMA_NOWAIT /* 0 */ );
      
      //AG_LOCAL_UNLOCK(&(pmcsc->pCardInfo->pmIOLock));  

      if (error == EINPROGRESS)
      {
        /*
         * So as to maintain ordering,
         * freeze the controller queue
         * until our mapping is
         * returned.
         */
        AGTIAPI_PRINTK( "agtiapi_PrepareSGList: EINPROGRESS\n" );
        xpt_freeze_simq( pmcsc->sim, 1 );
        pmcsc->SimQFrozen = agTRUE;	
        ccbh->status |= CAM_RELEASE_SIMQ;
      }
    }
    if( csmpio->smp_response_sglist_cnt != 0 )
    {
      /*
       * Virtual address that needs to translated into
       * one or more physical address ranges.
       */
      int error;
      //AG_LOCAL_LOCK( &(pmcsc->pCardInfo->pmIOLock) );  
      AGTIAPI_PRINTK( "agtiapi_PrepareSGList: virtual address\n" );
      error = bus_dmamap_load( pmcsc->buffer_dmat,
                               pccb->CCB_dmamap, 
                               csmpio->smp_response, 
                               csmpio->smp_response_len,
                               agtiapi_PrepareSMPSGListCB, 
                               pccb, 
                               BUS_DMA_NOWAIT /* 0 */ );
      
      //AG_LOCAL_UNLOCK( &(pmcsc->pCardInfo->pmIOLock) );

      if ( error == EINPROGRESS )
      {
        /*
         * So as to maintain ordering,
         * freeze the controller queue
         * until our mapping is
         * returned.
         */
        AGTIAPI_PRINTK( "agtiapi_PrepareSGList: EINPROGRESS\n" );
        xpt_freeze_simq( pmcsc->sim, 1 );
        pmcsc->SimQFrozen = agTRUE;	
        ccbh->status |= CAM_RELEASE_SIMQ;
      }
    }
  }
  else
  {
    if ( (csmpio->smp_request_sglist_cnt == 0) &&
         (csmpio->smp_response_sglist_cnt == 0) )
    {
      AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGList: physical address\n" );
      pccb->tiSMPFrame.outFrameBuf = (void *)csmpio->smp_request;
      pccb->tiSMPFrame.outFrameLen = csmpio->smp_request_len;
      pccb->tiSMPFrame.expectedRespLen = csmpio->smp_response_len;

      // 0xFF to be defined
      agtiapi_PrepareSMPSGListCB( pccb, NULL, 0, 0xAABBCCDD );
    }
    pccb->tiSMPFrame.flag = 0;
  }

  return tiSuccess;
}

#endif
/******************************************************************************
agtiapi_PrepareSMPSGListCB()

Purpose:
  Callback function for bus_dmamap_load()
  This fuctions sends IO to LL layer.
Parameters:
  void *arg (IN)                Pointer to the HBA data structure
  bus_dma_segment_t *segs (IN)  Pointer to dma segment
  int nsegs (IN)                number of dma segment
  int error (IN)                error
Return:
Note:
******************************************************************************/
static void agtiapi_PrepareSMPSGListCB( void *arg,
                                        bus_dma_segment_t *segs,
                                        int nsegs,
                                        int error )
{
  pccb_t                pccb = arg;
  union ccb            *ccb  = pccb->ccb;
  struct agtiapi_softc *pmcsc;
  U32        TID     = CMND_TO_TARGET(ccb);
  int status;
  tiDeviceHandle_t     *tiExpDevHandle;
  tiPortalContext_t    *tiExpPortalContext;
  ag_portal_info_t     *tiExpPortalInfo;

  AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGListCB: start, nsegs %d error 0x%x\n",
                  nsegs, error );
  pmcsc = pccb->pmcsc;

  if ( error != tiSuccess )
  {
    if (error == 0xAABBCCDD)
    {
      // do nothing
    }
    else
    {
      AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGListCB: error status 0x%x\n",
                      error );
      bus_dmamap_unload( pmcsc->buffer_dmat, pccb->CCB_dmamap );
      bus_dmamap_destroy( pmcsc->buffer_dmat, pccb->CCB_dmamap );
      agtiapi_FreeCCB( pmcsc, pccb );
      ccb->ccb_h.status = CAM_REQ_CMP;
      xpt_done( ccb );
      return;
    }
  }

  if ( nsegs > AGTIAPI_MAX_DMA_SEGS )
  {
    AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGListCB: over the limit. nsegs %d "
                    "AGTIAPI_MAX_DMA_SEGS %d\n",
                    nsegs, AGTIAPI_MAX_DMA_SEGS );
    bus_dmamap_unload( pmcsc->buffer_dmat, pccb->CCB_dmamap );
    bus_dmamap_destroy( pmcsc->buffer_dmat, pccb->CCB_dmamap );
    agtiapi_FreeCCB( pmcsc, pccb );
    ccb->ccb_h.status = CAM_REQ_CMP;
    xpt_done( ccb );
    return;
  }

  /*
   * If assigned pDevHandle is not available
   * then there is no need to send it to StartIO()
   */
  /* TODO: Add check for deviceType */
  if ( pccb->targetId < 0 || pccb->targetId >= maxTargets )
  {
    agtiapi_FreeCCB( pmcsc, pccb );
    ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
    xpt_done(ccb);
    pccb->ccb        = NULL; 
    return;
  }
  TID = INDEX( pmcsc, pccb->targetId );
  if ( (TID >= pmcsc->devDiscover) ||
       !(pccb->devHandle = pmcsc->pDevList[TID].pDevHandle) )
  {
    AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGListCB: not sending ccb devH %p, "
                    "target %d tid %d/%d "
                    "card %p ERROR pccb %p\n",
                    pccb->devHandle,
                    pccb->targetId,
                    TID, 
                    pmcsc->devDiscover,
                    pmcsc,
                    pccb );
    agtiapi_FreeCCB( pmcsc, pccb );
    ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
    xpt_done( ccb );
    pccb->ccb        = NULL; 
    return;
  }
  /* TODO: add indirect handling */
  /* set the flag correctly based on Indiret SMP request and response */

  AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGListCB: send ccb pccb->devHandle %p, "
                  "pccb->targetId %d TID %d pmcsc->devDiscover %d card %p\n",
                  pccb->devHandle,
                  pccb->targetId, TID,
                  pmcsc->devDiscover,
                  pmcsc );
  tiExpDevHandle = pccb->devHandle;
  tiExpPortalInfo = pmcsc->pDevList[TID].pPortalInfo;
  tiExpPortalContext = &tiExpPortalInfo->tiPortalContext;
  /* Look for the expander associated with the ses device */
  status = tiINIGetExpander( &pmcsc->tiRoot, 
                             tiExpPortalContext,
                             pccb->devHandle, 
                             &tiExpDevHandle );

  if ( status != tiSuccess )
  {
    AGTIAPI_PRINTK( "agtiapi_PrepareSMPSGListCB: Error getting Expander "
                    "device\n" );
    agtiapi_FreeCCB( pmcsc, pccb );
    ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
    xpt_done( ccb );
    pccb->ccb        = NULL; 
    return;
  }
	
  /* this is expander device */
  pccb->devHandle = tiExpDevHandle;
  /* put the request in send queue */
  agtiapi_QueueCCB( pmcsc, &pmcsc->smpSendHead, &pmcsc->smpSendTail
                    AG_CARD_LOCAL_LOCK(&pmcsc->sendSMPLock), pccb );

  agtiapi_StartSMP( pmcsc );

  return;
}


/******************************************************************************
agtiapi_Done()

Purpose:
  Processing completed ccbs
Parameters:
  struct agtiapi_softc *pmcsc (IN)   Pointer to HBA data structure
  ccb_t *pccb (IN)     A pointer to the driver's own CCB, not CAM's CCB
Return:
Note:
******************************************************************************/
STATIC void agtiapi_Done(struct agtiapi_softc *pmcsc, ccb_t *pccb)
{
  pccb_t pccb_curr = pccb;
  pccb_t pccb_next;

  tiIniScsiCmnd_t *cmnd;
  union ccb * ccb;

  AGTIAPI_IO("agtiapi_Done: start\n");
  while (pccb_curr)
  {
    /* start from 1st ccb in the chain */
    pccb_next = pccb_curr->pccbNext;

    if (agtiapi_CheckError(pmcsc, pccb_curr) != 0)
    {
      /* send command back and release the ccb */
      cmnd = &pccb_curr->tiSuperScsiRequest.scsiCmnd;

      if (cmnd->cdb[0] == RECEIVE_DIAGNOSTIC)
      {
        AGTIAPI_PRINTK("agtiapi_Done: RECEIVE_DIAG pg %d id %d cmnd %p pccb "
                       "%p\n", cmnd->cdb[2], pccb_curr->targetId, cmnd,
                       pccb_curr);
      }

      CMND_DMA_UNMAP(pmcsc, ccb);

      /* send the request back to the CAM */
      ccb = pccb_curr->ccb;
      agtiapi_FreeCCB(pmcsc, pccb_curr);
      xpt_done(ccb);
	}
    pccb_curr = pccb_next;
  }
  return;
}

/******************************************************************************
agtiapi_SMPDone()

Purpose:
  Processing completed ccbs
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Ponter to HBA data structure
  ccb_t *pccb (IN)                  A pointer to the driver's own CCB, not
                                    CAM's CCB
Return:
Note:
******************************************************************************/
STATIC void agtiapi_SMPDone(struct agtiapi_softc *pmcsc, ccb_t *pccb)
{
  pccb_t pccb_curr = pccb;
  pccb_t pccb_next;

  union ccb * ccb;

  AGTIAPI_PRINTK("agtiapi_SMPDone: start\n");

  while (pccb_curr)
  {
    /* start from 1st ccb in the chain */
    pccb_next = pccb_curr->pccbNext;

    if (agtiapi_CheckSMPError(pmcsc, pccb_curr) != 0)
    {
      CMND_DMA_UNMAP(pmcsc, ccb);

      /* send the request back to the CAM */
      ccb = pccb_curr->ccb;
      agtiapi_FreeSMPCCB(pmcsc, pccb_curr);
      xpt_done(ccb);

    }
    pccb_curr = pccb_next;
  }

  AGTIAPI_PRINTK("agtiapi_SMPDone: Done\n");
  return;
}

/******************************************************************************
agtiapi_hexdump()

Purpose:
  Utility function for dumping in hex
Parameters:
  const char *ptitle (IN)  A string to be printed
  bit8 *pbuf (IN)          A pointer to a buffer to be printed. 
  int len (IN)             The lengther of the buffer
Return:
Note:
******************************************************************************/
void agtiapi_hexdump(const char *ptitle, bit8 *pbuf, int len)
{
  int i;
  AGTIAPI_PRINTK("%s - hexdump(len=%d):\n", ptitle, (int)len);
  if (!pbuf)
  {
    AGTIAPI_PRINTK("pbuf is NULL\n");
    return;
  }
  for (i = 0; i < len; )
  {
    if (len - i > 4)
    {
      AGTIAPI_PRINTK( " 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n", pbuf[i], pbuf[i+1],
                      pbuf[i+2], pbuf[i+3] );
      i += 4;
    }
    else
    {
      AGTIAPI_PRINTK(" 0x%02x,", pbuf[i]);
      i++;
    }
  }
  AGTIAPI_PRINTK("\n");
}


/******************************************************************************
agtiapi_CheckError()

Purpose:
  Processes status pertaining to the ccb -- whether it was
  completed successfully, aborted, or error encountered.
Parameters: 
  ag_card_t *pCard (IN)  Pointer to HBA data structure
  ccb_t *pccd (IN)       A pointer to the driver's own CCB, not CAM's CCB
Return:
  0 - the command retry is required
  1 - the command process is completed
Note:    

******************************************************************************/
STATIC U32 agtiapi_CheckError(struct agtiapi_softc *pmcsc, ccb_t *pccb)
{
  ag_device_t      *pDevice;
  // union ccb * ccb = pccb->ccb;
  union ccb * ccb;
  int is_error, TID;

  if (pccb == NULL) {
    return 0;
  }
  ccb = pccb->ccb;
  AGTIAPI_IO("agtiapi_CheckError: start\n");
  if (ccb == NULL)
  {
    /* shouldn't be here but just in case we do */
    AGTIAPI_PRINTK("agtiapi_CheckError: CCB orphan = %p ERROR\n", pccb);
    agtiapi_FreeCCB(pmcsc, pccb);
    return 0;
  }

  is_error = 1;
  pDevice = NULL;
  if (pmcsc != NULL && pccb->targetId >= 0 && pccb->targetId < maxTargets)
  {
    if (pmcsc->pWWNList != NULL)
    {
      TID = INDEX(pmcsc, pccb->targetId);
      if (TID < maxTargets)
      {
        pDevice = &pmcsc->pDevList[TID];
        if (pDevice != NULL)
        {
          is_error = 0;
        }
      }
    }
  }
  if (is_error)
  {
    AGTIAPI_PRINTK("agtiapi_CheckError: pDevice == NULL\n");
    agtiapi_FreeCCB(pmcsc, pccb);
    return 0;
  }

  /* SCSI status */
  ccb->csio.scsi_status = pccb->scsiStatus;

   if(pDevice->CCBCount > 0){
    atomic_subtract_int(&pDevice->CCBCount,1);
}
  AG_LOCAL_LOCK(&pmcsc->freezeLock);
  if(pmcsc->freezeSim == agTRUE)
  { 
    pmcsc->freezeSim = agFALSE;
    xpt_release_simq(pmcsc->sim, 1); 
  }
  AG_LOCAL_UNLOCK(&pmcsc->freezeLock);

  switch (pccb->ccbStatus)
  {
  case tiIOSuccess:
    AGTIAPI_IO("agtiapi_CheckError: tiIOSuccess pccb %p\n", pccb);
    /* CAM status */
    if (pccb->scsiStatus == SCSI_STATUS_OK)
    {
      ccb->ccb_h.status = CAM_REQ_CMP;
    }
    else
      if (pccb->scsiStatus == SCSI_TASK_ABORTED)
    {
      ccb->ccb_h.status = CAM_REQ_ABORTED;
    }
    else
    {
      ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
    }
    if (ccb->csio.scsi_status == SCSI_CHECK_CONDITION)
    {
      ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
    }
 
    break;

  case tiIOOverRun:
    AGTIAPI_PRINTK("agtiapi_CheckError: tiIOOverRun pccb %p\n", pccb);
    /* resid is ignored for this condition */
    ccb->csio.resid = 0;
    ccb->ccb_h.status = CAM_DATA_RUN_ERR;
    break;
  case tiIOUnderRun:
    AGTIAPI_PRINTK("agtiapi_CheckError: tiIOUnderRun pccb %p\n", pccb);
    ccb->csio.resid = pccb->scsiStatus;
    ccb->ccb_h.status = CAM_REQ_CMP;
    ccb->csio.scsi_status = SCSI_STATUS_OK;
    break;

  case tiIOFailed:
    AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed %d id %d ERROR\n",
                    pccb, pccb->scsiStatus, pccb->targetId );
    if (pccb->scsiStatus == tiDeviceBusy)
    {
      AGTIAPI_IO( "agtiapi_CheckError: pccb %p tiIOFailed - tiDetailBusy\n",
                  pccb );
      ccb->ccb_h.status &= ~CAM_STATUS_MASK;
      ccb->ccb_h.status |= CAM_REQUEUE_REQ;
      if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) 
      {
        ccb->ccb_h.status |= CAM_DEV_QFRZN;
        xpt_freeze_devq(ccb->ccb_h.path, /*count*/1);
      }
    }
    else if(pccb->scsiStatus == tiBusy)
    {
      AG_LOCAL_LOCK(&pmcsc->freezeLock);
      if(pmcsc->freezeSim == agFALSE)
      {
        pmcsc->freezeSim = agTRUE;
        xpt_freeze_simq(pmcsc->sim, 1);
      }
      AG_LOCAL_UNLOCK(&pmcsc->freezeLock);
      ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
      ccb->ccb_h.status |= CAM_REQUEUE_REQ;
    }
    else if (pccb->scsiStatus == tiDetailNoLogin)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailNoLogin ERROR\n", pccb );
      ccb->ccb_h.status = CAM_DEV_NOT_THERE;
    }
    else if (pccb->scsiStatus == tiDetailNotValid)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailNotValid ERROR\n", pccb );
      ccb->ccb_h.status = CAM_REQ_INVALID;
    }
    else if (pccb->scsiStatus == tiDetailAbortLogin)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailAbortLogin ERROR\n", pccb );
      ccb->ccb_h.status = CAM_REQ_ABORTED;
    }
    else if (pccb->scsiStatus == tiDetailAbortReset)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailAbortReset ERROR\n", pccb );
      ccb->ccb_h.status = CAM_REQ_ABORTED;
    }
    else if (pccb->scsiStatus == tiDetailAborted)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailAborted ERROR\n", pccb );
      ccb->ccb_h.status = CAM_REQ_ABORTED;
    }
    else if (pccb->scsiStatus == tiDetailOtherError)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailOtherError ERROR\n", pccb );
      ccb->ccb_h.status = CAM_REQ_ABORTED;
    }
    break;
  case tiIODifError:
    AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed %d id %d ERROR\n",
                    pccb, pccb->scsiStatus, pccb->targetId );
    if (pccb->scsiStatus == tiDetailDifAppTagMismatch)
    {
      AGTIAPI_IO( "agtiapi_CheckError: pccb %p tiIOFailed - "
                  "tiDetailDifAppTagMismatch\n", pccb );
      ccb->ccb_h.status = CAM_REQ_CMP_ERR;
    }
    else if (pccb->scsiStatus == tiDetailDifRefTagMismatch)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailDifRefTagMismatch\n", pccb );
      ccb->ccb_h.status = CAM_REQ_CMP_ERR;
    }
    else if (pccb->scsiStatus == tiDetailDifCrcMismatch)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed - "
                      "tiDetailDifCrcMismatch\n", pccb );
      ccb->ccb_h.status = CAM_REQ_CMP_ERR;
    }
    break;
#ifdef HIALEAH_ENCRYPTION
  case tiIOEncryptError:
    AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOFailed %d id %d ERROR\n",
                    pccb, pccb->scsiStatus, pccb->targetId );
    if (pccb->scsiStatus == tiDetailDekKeyCacheMiss) 
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: %s: pccb %p tiIOFailed - "
                      "tiDetailDekKeyCacheMiss ERROR\n",
                      __FUNCTION__, pccb );
      ccb->ccb_h.status = CAM_REQ_ABORTED;
      agtiapi_HandleEncryptedIOFailure(pDevice, pccb);
    }
    else if (pccb->scsiStatus == tiDetailDekIVMismatch)
    {
      AGTIAPI_PRINTK( "agtiapi_CheckError: %s: pccb %p tiIOFailed - "
                      "tiDetailDekIVMismatch ERROR\n", __FUNCTION__, pccb );
      ccb->ccb_h.status = CAM_REQ_ABORTED;
      agtiapi_HandleEncryptedIOFailure(pDevice, pccb);
    }
    break;
#endif
  default:
    AGTIAPI_PRINTK( "agtiapi_CheckError: pccb %p tiIOdefault %d id %d ERROR\n",
                    pccb, pccb->ccbStatus, pccb->targetId );
    ccb->ccb_h.status = CAM_REQ_CMP_ERR;
    break;
  }

  return 1;
}


/******************************************************************************
agtiapi_SMPCheckError()

Purpose:
  Processes status pertaining to the ccb -- whether it was
  completed successfully, aborted, or error encountered.
Parameters: 
  ag_card_t *pCard (IN)  Pointer to HBA data structure
  ccb_t *pccd (IN)       A pointer to the driver's own CCB, not CAM's CCB
Return:
  0 - the command retry is required
  1 - the command process is completed
Note:    

******************************************************************************/
STATIC U32 agtiapi_CheckSMPError( struct agtiapi_softc *pmcsc, ccb_t *pccb )
{
	union ccb * ccb = pccb->ccb;

	AGTIAPI_PRINTK("agtiapi_CheckSMPError: start\n");

	if (!ccb)
	{
		/* shouldn't be here but just in case we do */
		AGTIAPI_PRINTK( "agtiapi_CheckSMPError: CCB orphan = %p ERROR\n",
                              pccb );
		agtiapi_FreeSMPCCB(pmcsc, pccb);
		return 0;
	}

	switch (pccb->ccbStatus)
	{
	case tiSMPSuccess:
		AGTIAPI_PRINTK( "agtiapi_CheckSMPError: tiSMPSuccess pccb %p\n",
                              pccb );
		/* CAM status */
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
  case tiSMPFailed:
		AGTIAPI_PRINTK( "agtiapi_CheckSMPError: tiSMPFailed pccb %p\n",
                              pccb );
		/* CAM status */
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
		break;
  default:
		AGTIAPI_PRINTK( "agtiapi_CheckSMPError: pccb %p tiSMPdefault %d "
                              "id %d ERROR\n",
                              pccb, 
                              pccb->ccbStatus,
                              pccb->targetId );
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
		break;
	}


  return 1;

}

/******************************************************************************
agtiapi_HandleEncryptedIOFailure():

Purpose:
Parameters:
Return:
Note: 
  Currently not used.
******************************************************************************/
void agtiapi_HandleEncryptedIOFailure(ag_device_t *pDev, ccb_t *pccb)
{
  
  AGTIAPI_PRINTK("agtiapi_HandleEncryptedIOFailure: start\n");
  return;
}

/******************************************************************************
agtiapi_Retry()

Purpose:
  Retry a ccb.
Parameters: 
  struct agtiapi_softc *pmcsc (IN)  Pointer to the HBA structure
  ccb_t *pccb (IN)            A pointer to the driver's own CCB, not CAM's CCB 
Return:
Note:
  Currently not used.    
******************************************************************************/
STATIC void agtiapi_Retry(struct agtiapi_softc *pmcsc, ccb_t *pccb)
{
  pccb->retryCount++;
  pccb->flags      = ACTIVE | AGTIAPI_RETRY;
  pccb->ccbStatus  = 0;
  pccb->scsiStatus = 0;
  pccb->startTime  = ticks;

  AGTIAPI_PRINTK( "agtiapi_Retry: start\n" );
  AGTIAPI_PRINTK( "agtiapi_Retry: ccb %p retry %d flgs x%x\n", pccb,
                  pccb->retryCount, pccb->flags );

  agtiapi_QueueCCB(pmcsc, &pmcsc->ccbSendHead, &pmcsc->ccbSendTail
                   AG_CARD_LOCAL_LOCK(&pmcsc->sendLock), pccb);
  return;
}


/******************************************************************************
agtiapi_DumpCCB()

Purpose:
  Dump CCB for debuging
Parameters:
  ccb_t *pccb (IN)  A pointer to the driver's own CCB, not CAM's CCB
Return:
Note:
******************************************************************************/
STATIC void agtiapi_DumpCCB(ccb_t *pccb)
{
  AGTIAPI_PRINTK("agtiapi_DumpCCB: pccb %p, devHandle %p, tid %d, lun %d\n", 
         pccb, 
         pccb->devHandle, 
         pccb->targetId, 
         pccb->lun);
  AGTIAPI_PRINTK("flag 0x%x, add_mode 0x%x, ccbStatus 0x%x, scsiStatus 0x%x\n", 
         pccb->flags,
         pccb->addrMode, 
         pccb->ccbStatus, 
         pccb->scsiStatus);
  AGTIAPI_PRINTK("scsi comand = 0x%x, numSgElements = %d\n", 
	 pccb->tiSuperScsiRequest.scsiCmnd.cdb[0],
         pccb->numSgElements);
  AGTIAPI_PRINTK("dataLen = 0x%x, sens_len = 0x%x\n",
         pccb->dataLen, 
         pccb->senseLen);
  AGTIAPI_PRINTK("tiSuperScsiRequest:\n");
  AGTIAPI_PRINTK("scsiCmnd: expDataLength 0x%x, taskAttribute 0x%x\n",
         pccb->tiSuperScsiRequest.scsiCmnd.expDataLength,
         pccb->tiSuperScsiRequest.scsiCmnd.taskAttribute);
  AGTIAPI_PRINTK("cdb[0] = 0x%x, cdb[1] = 0x%x, cdb[2] = 0x%x, cdb[3] = 0x%x\n",
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[0], 
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[1], 
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[2], 
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[3]); 
  AGTIAPI_PRINTK("cdb[4] = 0x%x, cdb[5] = 0x%x, cdb[6] = 0x%x, cdb[7] = 0x%x\n",
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[4], 
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[5], 
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[6], 
         pccb->tiSuperScsiRequest.scsiCmnd.cdb[7]);
  AGTIAPI_PRINTK( "cdb[8] = 0x%x, cdb[9] = 0x%x, cdb[10] = 0x%x, "
                  "cdb[11] = 0x%x\n",
                  pccb->tiSuperScsiRequest.scsiCmnd.cdb[8], 
                  pccb->tiSuperScsiRequest.scsiCmnd.cdb[9], 
                  pccb->tiSuperScsiRequest.scsiCmnd.cdb[10], 
                  pccb->tiSuperScsiRequest.scsiCmnd.cdb[11] );
  AGTIAPI_PRINTK("agSgl1: upper 0x%x, lower 0x%x, len 0x%x, type %d\n",
         pccb->tiSuperScsiRequest.agSgl1.upper, 
         pccb->tiSuperScsiRequest.agSgl1.lower, 
         pccb->tiSuperScsiRequest.agSgl1.len, 
         pccb->tiSuperScsiRequest.agSgl1.type); 
}

/******************************************************************************
agtiapi_eh_HostReset()

Purpose:
  A new error handler of Host Reset command.
Parameters:
  scsi_cmnd *cmnd (IN)  Pointer to a command to the HBA to be reset
Return:
  SUCCESS - success
  FAILED  - fail
Note:
******************************************************************************/
int agtiapi_eh_HostReset( struct agtiapi_softc *pmcsc, union ccb *cmnd )
{
  AGTIAPI_PRINTK( "agtiapi_eh_HostReset: ccb pointer %p\n",
                  cmnd );

  if( cmnd == NULL )
  {
    printf( "agtiapi_eh_HostReset: null command, skipping reset.\n" );
    return tiInvalidHandle;
  }

#ifdef LOGEVENT
  agtiapi_LogEvent( pmcsc,
                    IOCTL_EVT_SEV_INFORMATIONAL,
                    0,
                    agNULL,
                    0,
                    "agtiapi_eh_HostReset! " );
#endif

  return agtiapi_DoSoftReset( pmcsc );
}


/******************************************************************************
agtiapi_QueueCCB()

Purpose:
  Put ccb in ccb queue at the tail
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Pointer to HBA data structure
  pccb_t *phead (IN)                Double pointer to ccb queue head
  pccb_t *ptail (IN)                Double pointer to ccb queue tail
  ccb_t *pccb (IN)                  Poiner to a ccb to be queued
Return:
Note:
  Put the ccb to the tail of queue
******************************************************************************/
STATIC void agtiapi_QueueCCB( struct agtiapi_softc *pmcsc,
                              pccb_t *phead,
                              pccb_t *ptail, 
#ifdef AGTIAPI_LOCAL_LOCK
                              struct mtx *mutex,
#endif
                              ccb_t *pccb )
{
  AGTIAPI_IO( "agtiapi_QueueCCB: start\n" );
  AGTIAPI_IO( "agtiapi_QueueCCB: %p to %p\n", pccb, phead );
  if (phead == NULL || ptail == NULL)
  {
    panic( "agtiapi_QueueCCB: phead %p ptail %p", phead, ptail );
  }
  pccb->pccbNext = NULL;
  AG_LOCAL_LOCK( mutex );
  if (*phead == NULL)
  {
    //WARN_ON(*ptail != NULL); /* critical, just get more logs */
    *phead = pccb;
  }
  else
  {
    //WARN_ON(*ptail == NULL); /* critical, just get more logs */
    if (*ptail)
      (*ptail)->pccbNext = pccb;
  }
  *ptail = pccb;
  AG_LOCAL_UNLOCK( mutex );
  return;
}


/******************************************************************************
agtiapi_QueueCCB()

Purpose:
 
Parameters:
  
  
Return:
Note:
  
******************************************************************************/
static int agtiapi_QueueSMP(struct agtiapi_softc *pmcsc, union ccb * ccb)
{
  pccb_t pccb = agNULL; /* call dequeue */
  int        status = tiSuccess;
  int        targetID = xpt_path_target_id(ccb->ccb_h.path);

  AGTIAPI_PRINTK("agtiapi_QueueSMP: start\n");  

  /* get a ccb */
  if ((pccb = agtiapi_GetCCB(pmcsc)) == NULL)
  {
    AGTIAPI_PRINTK("agtiapi_QueueSMP: GetCCB ERROR\n");
    ccb->ccb_h.status = CAM_REQ_CMP;
    xpt_done(ccb);
    return tiBusy;
  }
  pccb->pmcsc = pmcsc;

  /* initialize Command Control Block (CCB) */
  pccb->targetId   = targetID;
  pccb->ccb        = ccb;	/* for struct scsi_cmnd */

  status = agtiapi_PrepareSMPSGList(pmcsc, pccb);

  if (status != tiSuccess)
  {
    AGTIAPI_PRINTK("agtiapi_QueueSMP: agtiapi_PrepareSMPSGList failure\n");
    agtiapi_FreeCCB(pmcsc, pccb);
    if (status == tiReject)
    {
      ccb->ccb_h.status = CAM_REQ_INVALID;
    }
    else
    {
      ccb->ccb_h.status = CAM_REQ_CMP;
    }
    xpt_done(ccb);
    return tiError;
  }

  return status;
}

/******************************************************************************
agtiapi_SetLunField()

Purpose:
  Set LUN field based on different address mode
Parameters:
  ccb_t *pccb (IN)  A pointer to the driver's own CCB, not CAM's CCB
Return:
Note:
******************************************************************************/
void agtiapi_SetLunField(ccb_t *pccb)
{
  U08 *pchar;

  pchar = (U08 *)&pccb->tiSuperScsiRequest.scsiCmnd.lun;

//  AGTIAPI_PRINTK("agtiapi_SetLunField: start\n");
  
  switch (pccb->addrMode)
  {
  case AGTIAPI_PERIPHERAL:
       *pchar++ = 0;
       *pchar   = (U08)pccb->lun;
       break;
  case AGTIAPI_VOLUME_SET:
       *pchar++ = (AGTIAPI_VOLUME_SET << AGTIAPI_ADDRMODE_SHIFT) | 
                  (U08)((pccb->lun >> 8) & 0x3F);
       *pchar   = (U08)pccb->lun;
       break;
  case AGTIAPI_LUN_ADDR:
       *pchar++ = (AGTIAPI_LUN_ADDR << AGTIAPI_ADDRMODE_SHIFT) | 
                  pccb->targetId;
       *pchar   = (U08)pccb->lun;
       break;
  }


}


/*****************************************************************************
agtiapi_FreeCCB()

Purpose:
  Free a ccb and put it back to ccbFreeList.
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Pointer to HBA data structure
  pccb_t pccb (IN)                  A pointer to the driver's own CCB, not
                                    CAM's CCB
Returns:
Note:
*****************************************************************************/
STATIC void agtiapi_FreeCCB(struct agtiapi_softc *pmcsc, pccb_t pccb)
{
  union ccb *ccb = pccb->ccb;
  bus_dmasync_op_t op;

  AG_LOCAL_LOCK(&pmcsc->ccbLock);
  AGTIAPI_IO( "agtiapi_FreeCCB: start %p\n", pccb );

#ifdef AGTIAPI_TEST_EPL
  tiEncrypt_t *encrypt;
#endif

  agtiapi_DumpCDB( "agtiapi_FreeCCB", pccb );

  if (pccb->sgList != agNULL)
  {
    AGTIAPI_IO( "agtiapi_FreeCCB: pccb->sgList is NOT null\n" );
  }
  else
  {
    AGTIAPI_PRINTK( "agtiapi_FreeCCB: pccb->sgList is null\n" );
  }

  /* set data transfer direction */
  if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) 
  {
    op = BUS_DMASYNC_POSTWRITE;
  }
  else 
  {
    op = BUS_DMASYNC_POSTREAD;
  }

  if (pccb->numSgElements == 0)
  {
    // do nothing
    AGTIAPI_IO( "agtiapi_FreeCCB: numSgElements zero\n" );
  }
  else if (pccb->numSgElements == 1)
  {
    AGTIAPI_IO( "agtiapi_FreeCCB: numSgElements is one\n" );
    //op is either BUS_DMASYNC_POSTWRITE or BUS_DMASYNC_POSTREAD
    bus_dmamap_sync(pmcsc->buffer_dmat, pccb->CCB_dmamap, op);
    bus_dmamap_unload(pmcsc->buffer_dmat, pccb->CCB_dmamap);
  }
  else
  {
    AGTIAPI_PRINTK( "agtiapi_FreeCCB: numSgElements 2 or higher \n" );
    //op is either BUS_DMASYNC_POSTWRITE or BUS_DMASYNC_POSTREAD
    bus_dmamap_sync(pmcsc->buffer_dmat, pccb->CCB_dmamap, op);
    bus_dmamap_unload(pmcsc->buffer_dmat, pccb->CCB_dmamap);
  }

#ifdef AGTIAPI_TEST_DPL
  if (pccb->tiSuperScsiRequest.Dif.enableDIFPerLA == TRUE) {
    if(pccb->dplPtr)
        memset( (char *) pccb->dplPtr,
                0,
                MAX_DPL_REGIONS * sizeof(dplaRegion_t) );
    pccb->tiSuperScsiRequest.Dif.enableDIFPerLA = FALSE;
    pccb->tiSuperScsiRequest.Dif.DIFPerLAAddrLo = 0;
    pccb->tiSuperScsiRequest.Dif.DIFPerLAAddrHi = 0;
  }
#endif

#ifdef AGTIAPI_TEST_EPL
  encrypt = &pccb->tiSuperScsiRequest.Encrypt;
  if (encrypt->enableEncryptionPerLA == TRUE) {
    encrypt->enableEncryptionPerLA = FALSE;
    encrypt->EncryptionPerLAAddrLo = 0;
    encrypt->EncryptionPerLAAddrHi = 0;
  }
#endif

#ifdef ENABLE_SATA_DIF
  if (pccb->holePtr && pccb->dmaHandleHole)
    pci_free_consistent( pmcsc->pCardInfo->pPCIDev,
                         512,
                         pccb->holePtr,
                         pccb->dmaHandleHole );
  pccb->holePtr    = 0;
  pccb->dmaHandleHole = 0;
#endif

  pccb->dataLen    = 0;
  pccb->retryCount = 0;
  pccb->ccbStatus  = 0;
  pccb->scsiStatus = 0;
  pccb->startTime  = 0;
  pccb->dmaHandle  = 0;
  pccb->numSgElements = 0;
  pccb->tiIORequest.tdData = 0;
  memset((void *)&pccb->tiSuperScsiRequest, 0, AGSCSI_INIT_XCHG_LEN);

#ifdef HIALEAH_ENCRYPTION
  if (pmcsc->encrypt)
    agtiapi_CleanupEncryptedIO(pmcsc, pccb);
#endif

  pccb->flags      = 0;
  pccb->ccb        = NULL;
  pccb->pccbIO = NULL;
  pccb->pccbNext     = (pccb_t)pmcsc->ccbFreeList;
  pmcsc->ccbFreeList = (caddr_t *)pccb;

  pmcsc->activeCCB--;

  AG_LOCAL_UNLOCK(&pmcsc->ccbLock);
  return;
}


/******************************************************************************
agtiapi_FlushCCBs()

Purpose:
  Flush all in processed ccbs.
Parameters:
  ag_card_t *pCard (IN)  Pointer to HBA data structure
  U32 flag (IN)            Flag to call back
Return:
Note:
******************************************************************************/
STATIC void agtiapi_FlushCCBs( struct agtiapi_softc *pCard, U32 flag )
{
  union ccb *ccb;
  ccb_t     *pccb;

  AGTIAPI_PRINTK( "agtiapi_FlushCCBs: enter \n" );
  for( pccb = (pccb_t)pCard->ccbChainList;
       pccb != NULL;
       pccb = pccb->pccbChainNext ) {
    if( pccb->flags == 0 )
    {
      // printf( "agtiapi_FlushCCBs: nothing, continue \n" );
      continue;
    }
    ccb = pccb->ccb;
    if ( pccb->flags & ( TASK_MANAGEMENT | DEV_RESET ) )
    {
      AGTIAPI_PRINTK( "agtiapi_FlushCCBs: agtiapi_FreeTMCCB \n" );
      agtiapi_FreeTMCCB( pCard, pccb );
    }
    else
    {
      if ( pccb->flags & TAG_SMP )
      {
        AGTIAPI_PRINTK( "agtiapi_FlushCCBs: agtiapi_FreeSMPCCB \n" );
        agtiapi_FreeSMPCCB( pCard, pccb );
      }
      else
      {
        AGTIAPI_PRINTK( "agtiapi_FlushCCBs: agtiapi_FreeCCB \n" );
        agtiapi_FreeCCB( pCard, pccb );
      }
      if( ccb ) {
        CMND_DMA_UNMAP( pCard, ccb );
        if( flag == AGTIAPI_CALLBACK ) {
          ccb->ccb_h.status = CAM_SCSI_BUS_RESET;
          xpt_done( ccb );
        }
      }
    }
  }
}

/*****************************************************************************
agtiapi_FreeSMPCCB()

Purpose:
  Free a ccb and put it back to ccbFreeList.
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Pointer to HBA data structure
  pccb_t pccb (IN)                  A pointer to the driver's own CCB, not
                                    CAM's CCB
Returns:
Note:
*****************************************************************************/
STATIC void agtiapi_FreeSMPCCB(struct agtiapi_softc *pmcsc, pccb_t pccb)
{
  union ccb *ccb = pccb->ccb;
  bus_dmasync_op_t op;

  AG_LOCAL_LOCK(&pmcsc->ccbLock);
  AGTIAPI_PRINTK("agtiapi_FreeSMPCCB: start %p\n", pccb);

  /* set data transfer direction */
  if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
  {
    op = BUS_DMASYNC_POSTWRITE;
  }
  else
  {
    op = BUS_DMASYNC_POSTREAD;
  }

  if (pccb->numSgElements == 0)
  {
    // do nothing
    AGTIAPI_PRINTK("agtiapi_FreeSMPCCB: numSgElements 0\n");
  }
  else if (pccb->numSgElements == 1)
  {
    AGTIAPI_PRINTK("agtiapi_FreeSMPCCB: numSgElements 1\n");
    //op is either BUS_DMASYNC_POSTWRITE or BUS_DMASYNC_POSTREAD
    bus_dmamap_sync(pmcsc->buffer_dmat, pccb->CCB_dmamap, op);
    bus_dmamap_unload(pmcsc->buffer_dmat, pccb->CCB_dmamap);
  }
  else
  {
    AGTIAPI_PRINTK("agtiapi_FreeSMPCCB: numSgElements 2 or higher \n");
    //op is either BUS_DMASYNC_POSTWRITE or BUS_DMASYNC_POSTREAD
    bus_dmamap_sync(pmcsc->buffer_dmat, pccb->CCB_dmamap, op);
    bus_dmamap_unload(pmcsc->buffer_dmat, pccb->CCB_dmamap);
  }

  /*dma api cleanning*/
  pccb->dataLen    = 0;
  pccb->retryCount = 0;
  pccb->ccbStatus  = 0;
  pccb->startTime  = 0;
  pccb->dmaHandle  = 0;
  pccb->numSgElements = 0;
  pccb->tiIORequest.tdData = 0;
  memset((void *)&pccb->tiSMPFrame, 0, AGSMP_INIT_XCHG_LEN);

  pccb->flags        = 0;
  pccb->ccb = NULL;
  pccb->pccbNext     = (pccb_t)pmcsc->ccbFreeList;
  pmcsc->ccbFreeList = (caddr_t *)pccb;

  pmcsc->activeCCB--;

  AG_LOCAL_UNLOCK(&pmcsc->ccbLock);
  return;

}

/*****************************************************************************
agtiapi_FreeTMCCB()

Purpose:
  Free a ccb and put it back to ccbFreeList.
Parameters:
  struct agtiapi_softc *pmcsc (IN)  Pointer to HBA data structure
  pccb_t pccb (IN)                  A pointer to the driver's own CCB, not
                                    CAM's CCB
Returns:
Note:
*****************************************************************************/
STATIC void agtiapi_FreeTMCCB(struct agtiapi_softc *pmcsc, pccb_t pccb)
{
  AG_LOCAL_LOCK(&pmcsc->ccbLock);
  AGTIAPI_PRINTK("agtiapi_FreeTMCCB: start %p\n", pccb);
  pccb->dataLen    = 0;
  pccb->retryCount = 0;
  pccb->ccbStatus  = 0;
  pccb->scsiStatus = 0;
  pccb->startTime  = 0;
  pccb->dmaHandle  = 0;
  pccb->numSgElements = 0;
  pccb->tiIORequest.tdData = 0;
  memset((void *)&pccb->tiSuperScsiRequest, 0, AGSCSI_INIT_XCHG_LEN);
  pccb->flags        = 0;
  pccb->ccb = NULL;
  pccb->pccbIO = NULL;
  pccb->pccbNext     = (pccb_t)pmcsc->ccbFreeList;
  pmcsc->ccbFreeList = (caddr_t *)pccb;
  pmcsc->activeCCB--;
  AG_LOCAL_UNLOCK(&pmcsc->ccbLock);
  return;
}
/******************************************************************************
agtiapi_CheckAllVectors():

Purpose:
Parameters:
Return:
Note:
  Currently, not used.
******************************************************************************/
void agtiapi_CheckAllVectors( struct agtiapi_softc *pCard, bit32 context )
{
#ifdef SPC_MSIX_INTR
  if (!agtiapi_intx_mode)
  {
    int i;

    for (i = 0; i < pCard->pCardInfo->maxInterruptVectors; i++)
      if (tiCOMInterruptHandler(&pCard->tiRoot, i) == agTRUE)
        tiCOMDelayedInterruptHandler(&pCard->tiRoot, i, 100, context);
  }
  else
  if (tiCOMInterruptHandler(&pCard->tiRoot, 0) == agTRUE)
    tiCOMDelayedInterruptHandler(&pCard->tiRoot, 0, 100, context);
#else
  if (tiCOMInterruptHandler(&pCard->tiRoot, 0) == agTRUE)
    tiCOMDelayedInterruptHandler(&pCard->tiRoot, 0, 100, context);
#endif

}


/******************************************************************************
agtiapi_CheckCB()

Purpose:
  Check call back function returned event for process completion
Parameters: 
  struct agtiapi_softc *pCard  Pointer to card data structure
  U32 milisec (IN)       Waiting time for expected event
  U32 flag (IN)          Flag of the event to check
  U32 *pStatus (IN)      Pointer to status of the card or port to check
Return:
  AGTIAPI_SUCCESS - event comes as expected
  AGTIAPI_FAIL    - event not coming
Note:
  Currently, not used    
******************************************************************************/
agBOOLEAN agtiapi_CheckCB( struct agtiapi_softc *pCard,
                           U32 milisec,
                           U32 flag,
                           volatile U32 *pStatus )
{
  U32    msecsPerTick = pCard->pCardInfo->tiRscInfo.tiInitiatorResource.
                        initiatorOption.usecsPerTick / 1000;
  S32    i = milisec/msecsPerTick;
  AG_GLOBAL_ARG( _flags );

  AGTIAPI_PRINTK( "agtiapi_CheckCB: start\n" );
  AGTIAPI_FLOW(   "agtiapi_CheckCB: start\n" );

  if( i <= 0 )
    i = 1;
  while (i > 0)
  {
    if (*pStatus & TASK_MANAGEMENT)
    {
      if (*pStatus & AGTIAPI_CB_DONE) 
      {
        if( flag == 0 || *pStatus & flag )
          return AGTIAPI_SUCCESS;
        else
          return AGTIAPI_FAIL;
      }
    }
    else if (pCard->flags & AGTIAPI_CB_DONE) 
    {
      if( flag == 0 || *pStatus & flag )
        return AGTIAPI_SUCCESS;
      else
        return AGTIAPI_FAIL;
    }

    agtiapi_DelayMSec( msecsPerTick );

    AG_SPIN_LOCK_IRQ( agtiapi_host_lock, _flags );
    tiCOMTimerTick( &pCard->tiRoot );

    agtiapi_CheckAllVectors( pCard, tiNonInterruptContext );
    AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, _flags );

    i--;
  }

  if( *pStatus & TASK_MANAGEMENT )
    *pStatus |= TASK_TIMEOUT;

  return AGTIAPI_FAIL;
}


/******************************************************************************
agtiapi_DiscoverTgt()

Purpose:
  Discover available devices
Parameters:
  struct agtiapi_softc *pCard (IN)  Pointer to the HBA data structure
Return:
Note:
******************************************************************************/
STATIC void agtiapi_DiscoverTgt(struct agtiapi_softc *pCard)
{

  ag_portal_data_t *pPortalData;
  U32              count;

  AGTIAPI_PRINTK("agtiapi_DiscoverTgt: start\n");
  AGTIAPI_FLOW("agtiapi_DiscoverTgt\n");
  AGTIAPI_INIT("agtiapi_DiscoverTgt\n");

  pPortalData = pCard->pPortalData;
  for (count = 0; count < pCard->portCount; count++, pPortalData++)
  {
    pCard->flags &= ~AGTIAPI_CB_DONE;
    if (!(PORTAL_STATUS(pPortalData) & AGTIAPI_PORT_DISC_READY))
    {
      if (pCard->flags & AGTIAPI_INIT_TIME)
      {
        if (agtiapi_CheckCB(pCard, 5000, AGTIAPI_PORT_DISC_READY, 
            &PORTAL_STATUS(pPortalData)) == AGTIAPI_FAIL)
        {
          AGTIAPI_PRINTK( "agtiapi_DiscoverTgt: Port %p / %d not ready for "
                          "discovery\n", 
                          pPortalData, count );
          /* 
           * There is no need to spend time on discovering device 
           * if port is not ready to do so.
           */
          continue;
        }
      }
      else
        continue;
    }

    AGTIAPI_FLOW( "agtiapi_DiscoverTgt: Portal %p DiscoverTargets starts\n",
                  pPortalData );
    AGTIAPI_INIT_DELAY(1000);

    pCard->flags &= ~AGTIAPI_CB_DONE;
    if (tiINIDiscoverTargets(&pCard->tiRoot, 
                             &pPortalData->portalInfo.tiPortalContext,
                             FORCE_PERSISTENT_ASSIGN_MASK)
        != tiSuccess)
      AGTIAPI_PRINTK("agtiapi_DiscoverTgt: tiINIDiscoverTargets ERROR\n");

    /*
     * Should wait till discovery completion to start
     * next portal. However, lower layer have issue on 
     * multi-portal case under Linux.
     */
  }

  pPortalData = pCard->pPortalData;
  for (count = 0; count < pCard->portCount; count++, pPortalData++)
  {
    if ((PORTAL_STATUS(pPortalData) & AGTIAPI_PORT_DISC_READY))
    {
      if (agtiapi_CheckCB(pCard, 20000, AGTIAPI_DISC_COMPLETE,
          &PORTAL_STATUS(pPortalData)) == AGTIAPI_FAIL)
      {
        if ((PORTAL_STATUS(pPortalData) & AGTIAPI_DISC_COMPLETE))
          AGTIAPI_PRINTK( "agtiapi_DiscoverTgt: Portal %p discover complete, "
                          "status 0x%x\n",
                          pPortalData,
                          PORTAL_STATUS(pPortalData) );
        else
          AGTIAPI_PRINTK( "agtiapi_DiscoverTgt: Portal %p discover is not "
                          "completed, status 0x%x\n",
                          pPortalData, PORTAL_STATUS(pPortalData) );
        continue;
      }
      AGTIAPI_PRINTK( "agtiapi_DiscoverTgt: Portal %d discover target "
                      "success\n",
                      count );
    }
  }

  /* 
   * Calling to get device handle should be done per portal based 
   * and better right after discovery is done. However, lower iscsi
   * layer may not returns discovery complete in correct sequence or we
   * ran out time. We get device handle for all portals together
   * after discovery is done or timed out.
   */
  pPortalData = pCard->pPortalData;
  for (count = 0; count < pCard->portCount; count++, pPortalData++)
  {
    /* 
     * We try to get device handle no matter 
     * if discovery is completed or not. 
     */
    if (PORTAL_STATUS(pPortalData) & AGTIAPI_PORT_DISC_READY)
    {
      U32 i;

      for (i = 0; i < AGTIAPI_GET_DEV_MAX; i++)
      {
        if (agtiapi_GetDevHandle(pCard, &pPortalData->portalInfo, 0, 0) != 0)
          break;
        agtiapi_DelayMSec(AGTIAPI_EXTRA_DELAY);
      }

      if ((PORTAL_STATUS(pPortalData) & AGTIAPI_DISC_COMPLETE) ||
          (pCard->tgtCount > 0))
        PORTAL_STATUS(pPortalData) |= ( AGTIAPI_DISC_DONE |
                                        AGTIAPI_PORT_LINK_UP );
    }
  }
  
  return;

}



/******************************************************************************
agtiapi_PrepCCBs()

Purpose:
  Prepares CCB including DMA map.
Parameters: 
  struct agtiapi_softc *pCard (IN)  Pointer to the HBA data structure
  ccb_hdr_t *hdr (IN)               Pointer to the CCB header
  U32 size (IN)                     size
  U32 max_ccb (IN)                  count
  
Return:
Note:    
******************************************************************************/
STATIC void agtiapi_PrepCCBs( struct agtiapi_softc *pCard,
                              ccb_hdr_t *hdr,
                              U32 size,
                              U32 max_ccb,
                              int tid )
{

  int i;
  U32 hdr_sz, ccb_sz;
  ccb_t *pccb = NULL;
  int offset = 0;
  int nsegs = 0;
  int sgl_sz = 0;

  AGTIAPI_PRINTK("agtiapi_PrepCCBs: start\n");
  offset = tid * AGTIAPI_CCB_PER_DEVICE;
  nsegs = AGTIAPI_NSEGS;
  sgl_sz = sizeof(tiSgl_t) * nsegs;
  AGTIAPI_PRINTK( "agtiapi_PrepCCBs: tid %d offset %d nsegs %d sizeof(tiSgl_t) "
                  "%lu, max_ccb %d\n",
                  tid,
                  offset,
                  nsegs,
                  sizeof(tiSgl_t),
                  max_ccb );

  ccb_sz = roundup2(AGTIAPI_CCB_SIZE, cache_line_size());
  hdr_sz = roundup2(sizeof(*hdr), cache_line_size());

  AGTIAPI_PRINTK("agtiapi_PrepCCBs: after cache line\n");

  memset((void *)hdr, 0, size);
  hdr->next = pCard->ccbAllocList;
  pCard->ccbAllocList = hdr;

  AGTIAPI_PRINTK("agtiapi_PrepCCBs: after memset\n");

  pccb = (ccb_t*) ((char*)hdr + hdr_sz);

  for (i = 0; i < max_ccb; i++, pccb = (ccb_t*)((char*)pccb + ccb_sz))
  {
    pccb->tiIORequest.osData = (void *)pccb;

    /*
     * Initially put all the ccbs on the free list
     * in addition to chainlist.
     * ccbChainList is a list of all available ccbs
     * (free/active everything)
     */
    pccb->pccbChainNext = (pccb_t)pCard->ccbChainList;
    pccb->pccbNext      = (pccb_t)pCard->ccbFreeList;

    pCard->ccbChainList = (caddr_t *)pccb;
    pCard->ccbFreeList  = (caddr_t *)pccb;
    pCard->ccbTotal++;

#ifdef AGTIAPI_ALIGN_CHECK
    if (&pccb & 0x63)
      AGTIAPI_PRINTK("pccb = %p\n", pccb);
    if (pccb->devHandle & 0x63)
      AGTIAPI_PRINTK("devHandle addr = %p\n", &pccb->devHandle);
    if (&pccb->lun & 0x63)
      AGTIAPI_PRINTK("lun addr = %p\n", &pccb->lun);
    if (&pccb->targetId & 0x63)
      AGTIAPI_PRINTK("tig addr = %p\n", &pccb->targetId);
    if (&pccb->ccbStatus & 0x63)
      AGTIAPI_PRINTK("ccbStatus addr = %p\n", &pccb->ccbStatus);
    if (&pccb->scsiStatus & 0x63)
      AGTIAPI_PRINTK("scsiStatus addr = %p\n", &pccb->scsiStatus);
    if (&pccb->dataLen & 0x63)
      AGTIAPI_PRINTK("dataLen addr = %p\n", &pccb->dataLen);
    if (&pccb->senseLen & 0x63)
      AGTIAPI_PRINTK("senseLen addr = %p\n", &pccb->senseLen);
    if (&pccb->numSgElements & 0x63)
      AGTIAPI_PRINTK("numSgElements addr = %p\n", &pccb->numSgElements);
    if (&pccb->retryCount & 0x63)
      AGTIAPI_PRINTK("retry cnt addr = %p\n", &pccb->retryCount);
    if (&pccb->flags & 0x63)
      AGTIAPI_PRINTK("flag addr = %p\n", &pccb->flags);
    if (&pccb->pSenseData & 0x63)
      AGTIAPI_PRINTK("senseData addr = %p\n", &pccb->pSenseData);
    if (&pccb->sgList[0] & 0x63)
      AGTIAPI_PRINTK("SgList 0 = %p\n", &pccb->sgList[0]);
    if (&pccb->pccbNext & 0x63)
      AGTIAPI_PRINTK("ccb next = %p\n", &pccb->pccbNext);
    if (&pccb->pccbChainNext & 0x63)
      AGTIAPI_PRINTK("ccbChainNext = %p\n", &pccb->pccbChainNext);
    if (&pccb->cmd & 0x63)
      AGTIAPI_PRINTK("command = %p\n", &pccb->cmd);
    if( &pccb->startTime & 0x63 )
      AGTIAPI_PRINTK( "startTime = %p\n", &pccb->startTime );
    if (&pccb->tiIORequest & 0x63)
      AGTIAPI_PRINTK("tiIOReq addr = %p\n", &pccb->tiIORequest);
    if (&pccb->tdIOReqBody & 0x63)
      AGTIAPI_PRINTK("tdIORequestBody addr = %p\n", &pccb->tdIOReqBody);
    if (&pccb->tiSuperScsiRequest & 0x63)
      AGTIAPI_PRINTK( "InitiatorExchange addr = %p\n",
                      &pccb->tiSuperScsiRequest );
#endif
    if ( bus_dmamap_create( pCard->buffer_dmat, 0, &pccb->CCB_dmamap ) !=
         tiSuccess)
    {
      AGTIAPI_PRINTK("agtiapi_PrepCCBs: can't create dma\n");
      return;
    }      
    /* assigns tiSgl_t memory to pccb */
    pccb->sgList = (void*)((U64)pCard->tisgl_mem + ((i + offset) * sgl_sz));
    pccb->tisgl_busaddr = pCard->tisgl_busaddr + ((i + offset) * sgl_sz);
    pccb->ccb = NULL;      
    pccb->pccbIO = NULL;      
    pccb->startTime = 0;
  }

#ifdef AGTIAPI_ALIGN_CHECK
  AGTIAPI_PRINTK("ccb size = %d / %d\n", sizeof(ccb_t), ccb_sz);
#endif
  return;
}

/******************************************************************************
agtiapi_InitCCBs()

Purpose:
  Create and initialize per card based CCB pool.
Parameters: 
  struct agtiapi_softc *pCard (IN)  Pointer to the HBA data structure
  int tgtCount (IN)                 Count
Return:
  Total number of ccb allocated
Note:    
******************************************************************************/
STATIC U32 agtiapi_InitCCBs(struct agtiapi_softc *pCard, int tgtCount, int tid)
{

  U32   max_ccb, size, ccb_sz, hdr_sz;
  int   no_allocs = 0, i;
  ccb_hdr_t  *hdr = NULL;

  AGTIAPI_PRINTK("agtiapi_InitCCBs: start\n");
  AGTIAPI_PRINTK("agtiapi_InitCCBs: tgtCount %d tid %d\n", tgtCount, tid);
  AGTIAPI_FLOW("agtiapi_InitCCBs: tgtCount %d tid %d\n", tgtCount, tid);

#ifndef HOTPLUG_SUPPORT
  if (pCard->tgtCount > AGSA_MAX_INBOUND_Q)
    return 1;
#else
  if (tgtCount > AGSA_MAX_INBOUND_Q)
    tgtCount = AGSA_MAX_INBOUND_Q;
#endif

  max_ccb = tgtCount * AGTIAPI_CCB_PER_DEVICE;//      / 4; // TBR
  ccb_sz = roundup2(AGTIAPI_CCB_SIZE, cache_line_size());
  hdr_sz = roundup2(sizeof(*hdr), cache_line_size());
  size = ccb_sz * max_ccb + hdr_sz;
  
  for (i = 0; i < (1 << no_allocs); i++) 
  {
    hdr = (ccb_hdr_t*)malloc( size, M_PMC_MCCB, M_NOWAIT );
    if( !hdr )
    {
      panic( "agtiapi_InitCCBs: bug!!!\n" );
    }
    else
    {
      agtiapi_PrepCCBs( pCard, hdr, size, max_ccb, tid );
    }
  }

  return 1;

}


#ifdef LINUX_PERBI_SUPPORT
/******************************************************************************
agtiapi_GetWWNMappings()

Purpose:
  Get the mappings from target IDs to WWNs, if any.
  Store them in the WWN_list array, indexed by target ID.
  Leave the devListIndex field blank; this will be filled-in later.
Parameters:
  ag_card_t *pCard (IN)        Pointer to HBA data structure
  ag_mapping_t *pMapList (IN)  Pointer to mapped device list
Return:
Note:  The boot command line parameters are used to load the
  mapping information, which is contained in the system
  configuration file.
******************************************************************************/
STATIC void agtiapi_GetWWNMappings( struct agtiapi_softc *pCard,
                                    ag_mapping_t         *pMapList )
{
  int           devDisc;
  int           lIdx = 0;
  ag_tgt_map_t *pWWNList;
  ag_slr_map_t *pSLRList;
  ag_device_t  *pDevList;

  if( !pCard )
    panic( "agtiapi_GetWWNMappings: no pCard \n" );

  AGTIAPI_PRINTK( "agtiapi_GetWWNMappings: start\n" );

  pWWNList = pCard->pWWNList;
  pSLRList = pCard->pSLRList;
  pDevList = pCard->pDevList;
  pCard->numTgtHardMapped = 0;
  devDisc = pCard->devDiscover;

  pWWNList[devDisc-1].devListIndex  = maxTargets;
  pSLRList[devDisc-1].localeNameLen = -2;
  pSLRList[devDisc-1].remoteNameLen = -2;
  pDevList[devDisc-1].targetId      = maxTargets;

  /*
   * Get the mappings from holding area which contains
   * the input of the system file and store them
   * in the WWN_list array, indexed by target ID.
   */
  for ( lIdx = 0; lIdx < devDisc - 1; lIdx++) {
    pWWNList[lIdx].flags = 0;
    pWWNList[lIdx].devListIndex  = maxTargets;
    pSLRList[lIdx].localeNameLen = -1;
    pSLRList[lIdx].remoteNameLen = -1;
  }

  //  this is where we would propagate values fed to pMapList

} /* agtiapi_GetWWNMappings */

#endif


/******************************************************************************
agtiapi_FindWWNListNext()
Purpose:
  finds first available new (unused) wwn list entry

Parameters:
  ag_tgt_map_t *pWWNList              Pointer to head of wwn list
  int lstMax                          Number of entries in WWNList
Return:
  index into WWNList indicating available entry space;
  if available entry space is not found, return negative value
******************************************************************************/
STATIC int agtiapi_FindWWNListNext( ag_tgt_map_t *pWWNList, int lstMax )
{
  int  lLstIdx;

  for ( lLstIdx = 0; lLstIdx < lstMax; lLstIdx++ )
  {
    if ( pWWNList[lLstIdx].devListIndex == lstMax &&
         pWWNList[lLstIdx].targetLen == 0 )
    {
      AGTIAPI_PRINTK( "agtiapi_FindWWNListNext: %d %d %d %d v. %d\n",
                      lLstIdx,
                      pWWNList[lLstIdx].devListIndex,
                      pWWNList[lLstIdx].targetLen,
                      pWWNList[lLstIdx].portId,
                      lstMax );
      return lLstIdx;
    }
  }
  return -1;
}


/******************************************************************************
agtiapi_GetDevHandle()

Purpose:
  Get device handle.  Handles will be placed in the
  devlist array with same order as TargetList provided and
  will be mapped to a scsi target id and registered to OS later.
Parameters:
  struct agtiapi_softc *pCard (IN)    Pointer to the HBA data structure
  ag_portal_info_t *pPortalInfo (IN)  Pointer to the portal data structure
  U32 eType (IN)                      Port event
  U32 eStatus (IN)                    Port event status
Return:
  Number of device handle slot present
Note:
  The sequence of device handle will match the sequence of taregt list
******************************************************************************/
STATIC U32 agtiapi_GetDevHandle( struct agtiapi_softc *pCard,
                                 ag_portal_info_t *pPortalInfo,
                                 U32 eType,
                                 U32 eStatus )
{
  ag_device_t       *pDevice;
  // tiDeviceHandle_t *agDev[pCard->devDiscover];
  tiDeviceHandle_t **agDev;
  int                devIdx, szdv, devTotal, cmpsetRtn;
  int                lDevIndex = 0, lRunScanFlag = FALSE;
  int               *lDevFlags;
  tiPortInfo_t       portInfT;
  ag_device_t        lTmpDevice;
  ag_tgt_map_t      *pWWNList;
  ag_slr_map_t      *pSLRList;
  bit32              lReadRm;
  bit16              lReadCt;


  AGTIAPI_PRINTK( "agtiapi_GetDevHandle: start\n" );
  AGTIAPI_PRINTK( "agtiapi_GetDevHandle: pCard->devDiscover %d / tgtCt %d\n",
                  pCard->devDiscover, pCard->tgtCount );
  AGTIAPI_FLOW( "agtiapi_GetDevHandle: portalInfo %p\n", pPortalInfo );
  AGTIAPI_INIT_DELAY( 1000 );

  agDev = (tiDeviceHandle_t **) malloc( sizeof(tiDeviceHandle_t *) * pCard->devDiscover,
                                        M_PMC_MDEV, M_ZERO | M_NOWAIT);
  if (agDev == NULL) 
  {
    AGTIAPI_PRINTK( "agtiapi_GetDevHandle: failed to alloc agDev[]\n" );
    return 0;
  }

  lDevFlags = (int *) malloc( sizeof(int) * pCard->devDiscover,
                              M_PMC_MFLG, M_ZERO | M_NOWAIT );
  if (lDevFlags == NULL)
  {
    free((caddr_t)agDev, M_PMC_MDEV);
    AGTIAPI_PRINTK( "agtiapi_GetDevHandle: failed to alloc lDevFlags[]\n" );
    return 0;
  }

  pWWNList = pCard->pWWNList;
  pSLRList = pCard->pSLRList;

  memset( (void *)agDev, 0, sizeof(void *) * pCard->devDiscover );
  memset( lDevFlags,     0, sizeof(int)    * pCard->devDiscover );

  // get device handles
  devTotal = tiINIGetDeviceHandles( &pCard->tiRoot,
                                    &pPortalInfo->tiPortalContext,
                                    (tiDeviceHandle_t **)agDev,
                                    pCard->devDiscover );

  AGTIAPI_PRINTK( "agtiapi_GetDevHandle: portalInfo %p port id %d event %u "
                  "status %u card %p pCard->devDiscover %d devTotal %d "
                  "pPortalInfo->devTotal %d pPortalInfo->devPrev %d "
                  "AGTIAPI_INIT_TIME %x\n",
                  pPortalInfo, pPortalInfo->portID, eType, eStatus, pCard,
                  pCard->devDiscover, devTotal, pPortalInfo->devTotal,
                  pPortalInfo->devPrev,
                  pCard->flags & AGTIAPI_INIT_TIME );

  // reset devTotal from any previous runs of this
  pPortalInfo->devPrev  = devTotal;
  pPortalInfo->devTotal = devTotal;

  AG_LIST_LOCK( &pCard->devListLock );

  if ( tiCOMGetPortInfo( &pCard->tiRoot,
                         &pPortalInfo->tiPortalContext,
                         &portInfT )
       != tiSuccess)
  {
    AGTIAPI_PRINTK( "agtiapi_GetDevHandle: tiCOMGetPortInfo did not succeed. \n" );
  }


  szdv = sizeof( pPortalInfo->pDevList ) / sizeof( pPortalInfo->pDevList[0] );
  if (szdv > pCard->devDiscover)
  {
    szdv = pCard->devDiscover;
  }

  // reconstructing dev list via comparison of wwn

  for ( devIdx = 0; devIdx < pCard->devDiscover; devIdx++ )
  {
    if ( agDev[devIdx] != NULL )
    {
      // AGTIAPI_PRINTK( "agtiapi_GetDevHandle: agDev %d not NULL %p\n",
      //                 devIdx, agDev[devIdx] );

      // pack temp device structure for tiINIGetDeviceInfo call
      pDevice                  = &lTmpDevice;
      pDevice->devType         = DIRECT_DEVICE;
      pDevice->pCard           = (void *)pCard;
      pDevice->flags           = ACTIVE;
      pDevice->pPortalInfo     = pPortalInfo;
      pDevice->pDevHandle      = agDev[devIdx];
      pDevice->qbusy           = agFALSE; 

      //AGTIAPI_PRINTK( "agtiapi_GetDevHandle: idx %d / %d : %p \n",
      //                devIdx, pCard->devDiscover, agDev[devIdx] );

      tiINIGetDeviceInfo( &pCard->tiRoot, agDev[devIdx],
                          &pDevice->devInfo );

      //AGTIAPI_PRINTK( "agtiapi_GetDevHandle: wwn sizes %ld %d/%d ",
      //                sizeof(pDevice->targetName),
      //                pDevice->devInfo.osAddress1,
      //                pDevice->devInfo.osAddress2 );

      wwncpy( pDevice );
      wwnprintk( (unsigned char*)pDevice->targetName, pDevice->targetLen );

      for ( lDevIndex = 0; lDevIndex < szdv; lDevIndex++ ) // match w/ wwn list
      {
        if ( (pCard->pDevList[lDevIndex].portalId == pPortalInfo->portID) &&
             pDevice->targetLen     > 0 &&
             portInfT.localNameLen  > 0 &&
             portInfT.remoteNameLen > 0 &&
             pSLRList[pWWNList[lDevIndex].sasLrIdx].localeNameLen > 0 &&
             pSLRList[pWWNList[lDevIndex].sasLrIdx].remoteNameLen > 0 &&
             ( portInfT.localNameLen ==
               pSLRList[pWWNList[lDevIndex].sasLrIdx].localeNameLen ) &&
             ( portInfT.remoteNameLen ==
               pSLRList[pWWNList[lDevIndex].sasLrIdx].remoteNameLen ) &&
             memcmp( pWWNList[lDevIndex].targetName, pDevice->targetName,
                     pDevice->targetLen )   == 0  &&
             memcmp( pSLRList[pWWNList[lDevIndex].sasLrIdx].localeName,
                     portInfT.localName,
                     portInfT.localNameLen )   == 0  &&
             memcmp( pSLRList[pWWNList[lDevIndex].sasLrIdx].remoteName,
                     portInfT.remoteName,
                     portInfT.remoteNameLen )   == 0  )
        {
          AGTIAPI_PRINTK( " pWWNList match @ %d/%d/%d \n",
                          lDevIndex, devIdx, pPortalInfo->portID );

          if ( (pCard->pDevList[lDevIndex].targetId == lDevIndex) &&
               ( pPortalInfo->pDevList[lDevIndex] ==
                 &pCard->pDevList[lDevIndex] )  ) // active
          {

            AGTIAPI_PRINTK( "agtiapi_GetDevHandle: dev in use %d of %d/%d\n",
                            lDevIndex, devTotal, pPortalInfo->portID );
            lDevFlags[devIdx]    |= DPMC_LEANFLAG_AGDEVUSED; // agDev handle
            lDevFlags[lDevIndex] |= DPMC_LEANFLAG_PDEVSUSED; // pDevice used
            lReadRm = atomic_readandclear_32( &pWWNList[lDevIndex].devRemoved );
            if ( lReadRm )   // cleared timeout, now remove count for timer
            {
              AGTIAPI_PRINTK( "agtiapi_GetDevHandle: clear timer count for"
                              " %d of %d\n",
                              lDevIndex, pPortalInfo->portID );
              atomic_subtract_16( &pCard->rmChkCt, 1 );
              lReadCt = atomic_load_acq_16( &pCard->rmChkCt );
              if ( 0 == lReadCt )
              {
                callout_stop( &pCard->devRmTimer );
              }
            }
            break;
          }

          AGTIAPI_PRINTK( "agtiapi_GetDevHandle: goin fresh on %d of %d/%d\n",
                          lDevIndex,  // reactivate now
                          devTotal, pPortalInfo->portID );

          // pDevice going fresh
          lRunScanFlag = TRUE; // scan and clear outstanding removals

          // pCard->tgtCount++; ##
          pDevice->targetId  = lDevIndex;
          pDevice->portalId  = pPortalInfo->portID;

          memcpy ( &pCard->pDevList[lDevIndex], pDevice, sizeof(lTmpDevice) );
          agDev[devIdx]->osData = (void *)&pCard->pDevList[lDevIndex];
          if ( agtiapi_InitCCBs( pCard, 1, pDevice->targetId ) == 0 )
          {
            AGTIAPI_PRINTK( "agtiapi_GetDevHandle: InitCCB "
                            "tgtCnt %d ERROR!\n", pCard->tgtCount );
            AG_LIST_UNLOCK( &pCard->devListLock );
            free((caddr_t)lDevFlags, M_PMC_MFLG);
            free((caddr_t)agDev, M_PMC_MDEV);
            return 0;
          }
          pPortalInfo->pDevList[lDevIndex] = &pCard->pDevList[lDevIndex];     // (ag_device_t *)
          if ( 0 == lDevFlags[devIdx] )
          {
            pPortalInfo->devTotal++;
            lDevFlags[devIdx]    |= DPMC_LEANFLAG_AGDEVUSED; // agDev used
            lDevFlags[lDevIndex] |= DPMC_LEANFLAG_PDEVSUSED; // pDevice used
          }
          else
          {
            AGTIAPI_PRINTK( "agtiapi_GetDevHandle: odd dev handle "
                            "status inspect %d %d %d\n",
                            lDevFlags[devIdx], devIdx, lDevIndex );
            pPortalInfo->devTotal++;
            lDevFlags[devIdx]    |= DPMC_LEANFLAG_AGDEVUSED; // agDev used
            lDevFlags[lDevIndex] |= DPMC_LEANFLAG_PDEVSUSED; // pDevice used

          }
          break;
        }
      }
      // end: match this wwn with previous wwn list

      // we have an agDev entry, but no pWWNList target for it
      if ( !(lDevFlags[devIdx] & DPMC_LEANFLAG_AGDEVUSED) )
      { // flag dev handle not accounted for yet
        lDevFlags[devIdx] |= DPMC_LEANFLAG_NOWWNLIST;
        // later, get an empty pDevice and map this agDev.
        // AGTIAPI_PRINTK( "agtiapi_GetDevHandle: devIdx %d flags 0x%x, %d\n",
        //                 devIdx, lDevFlags[devIdx], (lDevFlags[devIdx] & 8) );
      }
    }
    else
    {
      lDevFlags[devIdx] |= DPMC_LEANFLAG_NOAGDEVYT; // known empty agDev handle
    }
  }

  // AGTIAPI_PRINTK( "agtiapi_GetDevHandle: all WWN all the time, "
  //                 "devLstIdx/flags/(WWNL)portId ... \n" );
  // review device list for further action needed
  for ( devIdx = 0; devIdx < pCard->devDiscover; devIdx++ )
  {
    if ( lDevFlags[devIdx] & DPMC_LEANFLAG_NOWWNLIST ) // new target, register
    {
      int lNextDyad; // find next available dyad entry

      AGTIAPI_PRINTK( "agtiapi_GetDevHandle: register new target, "
                      "devIdx %d -- %d \n", devIdx, pCard->devDiscover );
      lRunScanFlag = TRUE; // scan and clear outstanding removals
      for ( lNextDyad = 0; lNextDyad < pCard->devDiscover; lNextDyad++ )
      {
        if ( pSLRList[lNextDyad].localeNameLen < 0 &&
             pSLRList[lNextDyad].remoteNameLen < 0    )
          break;
      }

      if ( lNextDyad == pCard->devDiscover )
      {
        printf( "agtiapi_GetDevHandle: failed to find available SAS LR\n" );
        AG_LIST_UNLOCK( &pCard->devListLock );
        free( (caddr_t)lDevFlags, M_PMC_MFLG );
        free( (caddr_t)agDev, M_PMC_MDEV );
        return 0;
      }
      // index of new entry
      lDevIndex = agtiapi_FindWWNListNext( pWWNList, pCard->devDiscover );
      AGTIAPI_PRINTK( "agtiapi_GetDevHandle: listIdx new target %d of %d/%d\n",
                      lDevIndex, devTotal, pPortalInfo->portID );
      if ( 0 > lDevIndex )
      {
        printf( "agtiapi_GetDevHandle: WARNING -- WWNList exhausted.\n" );
        continue;
      }

      pDevice = &pCard->pDevList[lDevIndex];

      tiINIGetDeviceInfo( &pCard->tiRoot, agDev[devIdx], &pDevice->devInfo );
      wwncpy( pDevice );
      agtiapi_InitCCBs( pCard, 1, lDevIndex );

      pDevice->pCard   = (void *)pCard;
      pDevice->devType = DIRECT_DEVICE;

      // begin to populate new WWNList entry
      memcpy( pWWNList[lDevIndex].targetName, pDevice->targetName, pDevice->targetLen );
      pWWNList[lDevIndex].targetLen = pDevice->targetLen;

      pWWNList[lDevIndex].flags         = SOFT_MAPPED;
      pWWNList[lDevIndex].portId        = pPortalInfo->portID;
      pWWNList[lDevIndex].devListIndex  = lDevIndex;
      pWWNList[lDevIndex].sasLrIdx      = lNextDyad;

      pSLRList[lNextDyad].localeNameLen = portInfT.localNameLen;
      pSLRList[lNextDyad].remoteNameLen = portInfT.remoteNameLen;
      memcpy( pSLRList[lNextDyad].localeName, portInfT.localName, portInfT.localNameLen );
      memcpy( pSLRList[lNextDyad].remoteName, portInfT.remoteName, portInfT.remoteNameLen );
      // end of populating new WWNList entry

      pDevice->targetId = lDevIndex;

      pDevice->flags = ACTIVE;
      pDevice->CCBCount = 0; 
      pDevice->pDevHandle = agDev[devIdx];
      agDev[devIdx]->osData = (void*)pDevice;

      pDevice->pPortalInfo = pPortalInfo;
      pDevice->portalId = pPortalInfo->portID;
      pPortalInfo->pDevList[lDevIndex] = (void*)pDevice;
      lDevFlags[lDevIndex] |= DPMC_LEANFLAG_PDEVSUSED; // mark pDevice slot used
    }

    if ( (pCard->pDevList[devIdx].portalId == pPortalInfo->portID) &&
         !(lDevFlags[devIdx] & DPMC_LEANFLAG_PDEVSUSED) ) // pDevice not used
    {
      pDevice = &pCard->pDevList[devIdx];
      //pDevice->flags &= ~ACTIVE;
      if ( ( pDevice->pDevHandle != NULL ||
             pPortalInfo->pDevList[devIdx] != NULL ) )
      {
        atomic_add_16( &pCard->rmChkCt, 1 );      // show count of lost device

        if (FALSE == lRunScanFlag)
        {

          AGTIAPI_PRINTK( "agtiapi_GetDevHandle: targ dropped out %d of %d/%d\n",
                          devIdx, devTotal, pPortalInfo->portID );
          // if ( 0 == pWWNList[devIdx].devRemoved ) '.devRemoved = 5;
          cmpsetRtn = atomic_cmpset_32( &pWWNList[devIdx].devRemoved, 0, 5 );
          if ( 0 == cmpsetRtn )
          {
            AGTIAPI_PRINTK( "agtiapi_GetDevHandle: target %d timer already set\n",
                    devIdx );
          }
          else
          {
            callout_reset( &pCard->devRmTimer, 1 * hz, agtiapi_devRmCheck, pCard );
          }
        }
        // else ... scan coming soon enough anyway, ignore timer for dropout
      }
    }
  } // end of for ( devIdx = 0; ...

  AG_LIST_UNLOCK( &pCard->devListLock );

  free((caddr_t)lDevFlags, M_PMC_MFLG);
  free((caddr_t)agDev, M_PMC_MDEV);

  if ( TRUE == lRunScanFlag )
    agtiapi_clrRmScan( pCard );

  return devTotal;
} // end  agtiapi_GetDevHandle

/******************************************************************************
agtiapi_scan()

Purpose:
  Triggers CAM's scan
Parameters: 
  struct agtiapi_softc *pCard (IN)    Pointer to the HBA data structure
Return:
Note:    
******************************************************************************/
static void agtiapi_scan(struct agtiapi_softc *pmcsc)
{
  union ccb *ccb;
  int bus, tid, lun;
 
  AGTIAPI_PRINTK("agtiapi_scan: start cardNO %d \n", pmcsc->cardNo);
    
  bus = cam_sim_path(pmcsc->sim);
 
  tid = CAM_TARGET_WILDCARD;
  lun = CAM_LUN_WILDCARD;

  mtx_lock(&(pmcsc->pCardInfo->pmIOLock)); 
  ccb = xpt_alloc_ccb_nowait();
  if (ccb == agNULL)
  {
    mtx_unlock(&(pmcsc->pCardInfo->pmIOLock)); 
    return;
  }
  if (xpt_create_path(&ccb->ccb_h.path, agNULL, bus, tid,
		      CAM_LUN_WILDCARD) != CAM_REQ_CMP) 
  { 
    mtx_unlock(&(pmcsc->pCardInfo->pmIOLock)); 
    xpt_free_ccb(ccb);
    return;
  }

  mtx_unlock(&(pmcsc->pCardInfo->pmIOLock)); 
  pmcsc->dev_scan = agTRUE;
  xpt_rescan(ccb);
  return;
}

/******************************************************************************
agtiapi_DeQueueCCB()

Purpose:
  Remove a ccb from a queue
Parameters: 
  struct agtiapi_softc *pCard (IN)  Pointer to the card structure
  pccb_t *phead (IN)     Pointer to a head of ccb queue
  ccb_t  *pccd  (IN)     Pointer to the ccb to be processed
Return:
  AGTIAPI_SUCCESS - the ccb is removed from queue
  AGTIAPI_FAIL    - the ccb is not found from queue
Note:    
******************************************************************************/
STATIC agBOOLEAN 
agtiapi_DeQueueCCB(struct agtiapi_softc *pCard, pccb_t *phead, pccb_t *ptail, 
#ifdef AGTIAPI_LOCAL_LOCK
                   struct mtx *lock,
#endif
                   ccb_t *pccb)
{
  ccb_t  *pccb_curr;
  U32     status = AGTIAPI_FAIL;

  AGTIAPI_PRINTK("agtiapi_DeQueueCCB: %p from %p\n", pccb, phead);

  if (pccb == NULL || *phead == NULL)
  {
    return AGTIAPI_FAIL;
  }

  AGTIAPI_PRINTK("agtiapi_DeQueueCCB: %p from %p\n", pccb, phead);
  AG_LOCAL_LOCK(lock);

  if (pccb == *phead)
  {
    *phead = (*phead)->pccbNext;
    if (pccb == *ptail)
    {
      *ptail = NULL;
    }
    else
      pccb->pccbNext = NULL;
    status = AGTIAPI_SUCCESS;
  }
  else
  {
    pccb_curr = *phead;
    while (pccb_curr->pccbNext != NULL)
    {
      if (pccb_curr->pccbNext == pccb)
      {
        pccb_curr->pccbNext = pccb->pccbNext;
        pccb->pccbNext = NULL;
        if (pccb == *ptail)
        {
          *ptail = pccb_curr;
        }
        else
          pccb->pccbNext = NULL;
        status = AGTIAPI_SUCCESS;
        break;
      }
      pccb_curr = pccb_curr->pccbNext;
    }
  }
  AG_LOCAL_UNLOCK(lock);

  return status;
}


STATIC void wwnprintk( unsigned char *name, int len )
{
  int i;

  for (i = 0; i < len; i++, name++)
    AGTIAPI_PRINTK("%02x", *name); 
  AGTIAPI_PRINTK("\n");
}
/* 
 * SAS and SATA behind expander has 8 byte long unique address. 
 * However, direct connect SATA device use 512 byte unique device id.
 * SPC uses remoteName to indicate length of ID and remoteAddress for the
 * address of memory that holding ID.
 */ 
STATIC int wwncpy( ag_device_t      *pDevice )
{
  int rc = 0;

  if (sizeof(pDevice->targetName) >= pDevice->devInfo.osAddress1 + 
                                     pDevice->devInfo.osAddress2) 
  {
    memcpy(pDevice->targetName, 
             pDevice->devInfo.remoteName, 
             pDevice->devInfo.osAddress1);
    memcpy(pDevice->targetName + pDevice->devInfo.osAddress1, 
             pDevice->devInfo.remoteAddress, 
             pDevice->devInfo.osAddress2);
    pDevice->targetLen = pDevice->devInfo.osAddress1 + 
                         pDevice->devInfo.osAddress2;
    rc = pDevice->targetLen;
  }
  else 
  {
    AGTIAPI_PRINTK("WWN wrong size: %d + %d ERROR\n", 
           pDevice->devInfo.osAddress1, pDevice->devInfo.osAddress2);
    rc = -1;
  }
  return rc;
}


/******************************************************************************
agtiapi_ReleaseCCBs()

Purpose:
  Free all allocated CCB memories for the Host Adapter.
Parameters:
  struct agtiapi_softc *pCard (IN)  Pointer to HBA data structure
Return:
Note:
******************************************************************************/
STATIC void agtiapi_ReleaseCCBs( struct agtiapi_softc *pCard )
{

  ccb_hdr_t *hdr;
  U32 hdr_sz;
  ccb_t *pccb = NULL;

  AGTIAPI_PRINTK( "agtiapi_ReleaseCCBs: start\n" );

#if ( defined AGTIAPI_TEST_DPL || defined AGTIAPI_TEST_EPL )
  ccb_t *pccb;
#endif

#ifdef AGTIAPI_TEST_DPL
  for (pccb = (pccb_t)pCard->ccbChainList; pccb != NULL;
       pccb = pccb->pccbChainNext)
  {
    if(pccb->dplPtr && pccb->dplDma)
      pci_pool_free(pCard->dpl_ctx_pool,   pccb->dplPtr, pccb->dplDma);
  }
#endif

#ifdef AGTIAPI_TEST_EPL
  for (pccb = (pccb_t)pCard->ccbChainList; pccb != NULL;
       pccb = pccb->pccbChainNext)
  {
    if(pccb->epl_ptr && pccb->epl_dma_ptr)
        pci_pool_free(
            pCard->epl_ctx_pool,
            pccb->epl_ptr, 
            pccb->epl_dma_ptr
        );
  }
#endif

  while ((hdr = pCard->ccbAllocList) != NULL)
  {
    pCard->ccbAllocList = hdr->next;
    hdr_sz = roundup2(sizeof(*hdr), cache_line_size());
    pccb = (ccb_t*) ((char*)hdr + hdr_sz);
    if (pCard->buffer_dmat != NULL && pccb->CCB_dmamap != NULL)
    {
      bus_dmamap_destroy(pCard->buffer_dmat, pccb->CCB_dmamap);
    }
    free(hdr, M_PMC_MCCB);
  }
  pCard->ccbAllocList = NULL;


  return;
}

/******************************************************************************
agtiapi_TITimer()

Purpose:
  Timer tick for tisa common layer
Parameters:
  void *data (IN)  Pointer to the HBA data structure
Return:
Note:
******************************************************************************/
STATIC void agtiapi_TITimer( void *data )
{

  U32                   next_tick;
  struct agtiapi_softc *pCard;

  pCard = (struct agtiapi_softc *)data;

//  AGTIAPI_PRINTK("agtiapi_TITimer: start\n");
  AG_GLOBAL_ARG( flags );

  next_tick = pCard->pCardInfo->tiRscInfo.tiLoLevelResource.
              loLevelOption.usecsPerTick / USEC_PER_TICK;

  if( next_tick == 0 )               /* no timer required */
    return;
  AG_SPIN_LOCK_IRQ( agtiapi_host_lock, flags );
  if( pCard->flags & AGTIAPI_SHUT_DOWN )
    goto ext;
  tiCOMTimerTick( &pCard->tiRoot );  /* tisa common layer timer tick */

  //add for polling mode
#ifdef PMC_SPC
  if( agtiapi_polling_mode )
    agtiapi_CheckAllVectors( pCard, tiNonInterruptContext );
#endif
  callout_reset( &pCard->OS_timer, next_tick, agtiapi_TITimer, pCard );
ext:
  AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, flags );
  return;
}

/******************************************************************************
agtiapi_clrRmScan()

Purpose:
  Clears device list entries scheduled for timeout and calls scan
Parameters:
  struct agtiapi_softc *pCard (IN)  Pointer to HBA data structure
******************************************************************************/
STATIC void agtiapi_clrRmScan( struct agtiapi_softc *pCard )
{
  ag_tgt_map_t         *pWWNList;
  ag_portal_info_t     *pPortalInfo;
  ag_portal_data_t     *pPortalData;
  int                   lIdx;
  bit32                 lReadRm;
  bit16                 lReadCt;

  pWWNList = pCard->pWWNList;

  AGTIAPI_PRINTK( "agtiapi_clrRmScan: start\n" );

  AG_LIST_LOCK( &pCard->devListLock );

  for ( lIdx = 0; lIdx < pCard->devDiscover; lIdx++ )
  {
    lReadCt = atomic_load_acq_16( &pCard->rmChkCt );
    if ( 0 == lReadCt )
    {
      break;  // trim to who cares
    }

    lReadRm = atomic_readandclear_32( &pWWNList[lIdx].devRemoved );
    if ( lReadRm > 0 )
    {
      pCard->pDevList[lIdx].flags &= ~ACTIVE;
      pCard->pDevList[lIdx].pDevHandle = NULL;

      pPortalData = &pCard->pPortalData[pWWNList[lIdx].portId];
      pPortalInfo = &pPortalData->portalInfo;
      pPortalInfo->pDevList[lIdx] = NULL;
      AGTIAPI_PRINTK( "agtiapi_clrRmScan: cleared dev %d at port %d\n",
                      lIdx, pWWNList[lIdx].portId );
      atomic_subtract_16( &pCard->rmChkCt, 1 );
    }
  }
  AG_LIST_UNLOCK( &pCard->devListLock );

  agtiapi_scan( pCard );
}


/******************************************************************************
agtiapi_devRmCheck()

Purpose:
  Timer tick to check for timeout on missing targets
  Removes device list entry when timeout is reached
Parameters:
  void *data (IN)  Pointer to the HBA data structure
******************************************************************************/
STATIC void agtiapi_devRmCheck( void *data )
{
  struct agtiapi_softc *pCard;
  ag_tgt_map_t         *pWWNList;
  int                   lIdx, cmpsetRtn, lRunScanFlag = FALSE;
  bit16                 lReadCt;
  bit32                 lReadRm;

  pCard = ( struct agtiapi_softc * )data;

  // routine overhead
  if ( callout_pending( &pCard->devRmTimer ) )  // callout was reset
  {
    return;
  }
  if ( !callout_active( &pCard->devRmTimer ) )  // callout was stopped
  {
    return;
  }
  callout_deactivate( &pCard->devRmTimer );

  if( pCard->flags & AGTIAPI_SHUT_DOWN )
  {
    return;  // implicit timer clear
  }

  pWWNList = pCard->pWWNList;

  AG_LIST_LOCK( &pCard->devListLock );
  lReadCt = atomic_load_acq_16( &pCard->rmChkCt );
  if ( lReadCt )
  {
    if ( callout_pending(&pCard->devRmTimer) == FALSE )
    {
      callout_reset( &pCard->devRmTimer, 1 * hz, agtiapi_devRmCheck, pCard );
    }
    else
    {
      AG_LIST_UNLOCK( &pCard->devListLock );
	  return;
    }

    for ( lIdx = 0; lIdx < pCard->devDiscover; lIdx++ )
    {
      lReadCt = atomic_load_acq_16( &pCard->rmChkCt );
      if ( 0 == lReadCt )
      {
        break;  // if handled somewhere else, get out
      }

      lReadRm = atomic_load_acq_32( &pWWNList[lIdx].devRemoved );
      if ( lReadRm > 0 )
      {
        if ( 1 == lReadRm ) // timed out
        { // no decrement of devRemoved as way to leave a clrRmScan marker
          lRunScanFlag = TRUE; // other devRemoved values are about to get wiped
          break; // ... so bail out
        }
        else
        {
          AGTIAPI_PRINTK( "agtiapi_devRmCheck: counting down dev %d @ %d; %d\n",
                          lIdx, lReadRm, lReadCt );
          cmpsetRtn = atomic_cmpset_32( &pWWNList[lIdx].devRemoved,
                                        lReadRm,
                                        lReadRm-1 );
          if ( 0 == cmpsetRtn )
          {
            printf( "agtiapi_devRmCheck: %d decrement already handled\n",
                    lIdx );
          }
        }
      }
    }
    AG_LIST_UNLOCK( &pCard->devListLock );

    if ( TRUE == lRunScanFlag )
      agtiapi_clrRmScan( pCard );
  }
  else
  {
    AG_LIST_UNLOCK( &pCard->devListLock );
  }

  return;
}


static void agtiapi_cam_poll( struct cam_sim *asim )
{
  return;
}

/*****************************************************************************
agtiapi_ResetCard()

Purpose:
  Hard or soft reset on the controller and resend any
  outstanding requests if needed.
Parameters:
  struct agtiapi_softc *pCard (IN)  Pointer to HBA data structure
  unsigned lomg flags (IN/OUT) Flags used in locking done from calling layers
Return:
  AGTIAPI_SUCCESS - reset successful
  AGTIAPI_FAIL    - reset failed
Note:
*****************************************************************************/
U32 agtiapi_ResetCard( struct agtiapi_softc *pCard, unsigned long *flags )
{
  ag_device_t      *pDevice;
  U32               lIdx = 0;
  U32               lFlagVal;
  agBOOLEAN         ret;
  ag_portal_info_t *pPortalInfo;
  ag_portal_data_t *pPortalData;
  U32               count, loop;
  int               szdv;

  if( pCard->flags & AGTIAPI_RESET ) {
    AGTIAPI_PRINTK( "agtiapi_ResetCard: reset card already in progress!\n" );
    return AGTIAPI_FAIL;
  }

  AGTIAPI_PRINTK( "agtiapi_ResetCard: Enter cnt %d\n",
                  pCard->resetCount );
#ifdef LOGEVENT
  agtiapi_LogEvent( pCard,
                    IOCTL_EVT_SEV_INFORMATIONAL,
                    0,
                    agNULL,
                    0,
                    "Reset initiator time = %d!",
                    pCard->resetCount + 1 );
#endif

  pCard->flags |= AGTIAPI_RESET;
  pCard->flags &= ~(AGTIAPI_CB_DONE | AGTIAPI_RESET_SUCCESS);
  tiCOMSystemInterruptsActive( &pCard->tiRoot, FALSE );
  pCard->flags &= ~AGTIAPI_SYS_INTR_ON;

  agtiapi_FlushCCBs( pCard, AGTIAPI_CALLBACK );

  for ( lIdx = 1; 3 >= lIdx; lIdx++ ) // we try reset up to 3 times
  {
    if( pCard->flags & AGTIAPI_SOFT_RESET )
    {
      AGTIAPI_PRINTK( "agtiapi_ResetCard: soft variant\n" );
      tiCOMReset( &pCard->tiRoot, tiSoftReset );
    }
    else
    {
      AGTIAPI_PRINTK( "agtiapi_ResetCard: no flag, no reset!\n" );
    }

    lFlagVal = AGTIAPI_RESET_SUCCESS;
    AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, *flags );
    ret = agtiapi_CheckCB( pCard, 50000, lFlagVal, &pCard->flags );
    AG_SPIN_LOCK_IRQ( agtiapi_host_lock, *flags );

    if( ret == AGTIAPI_FAIL )
    {
      AGTIAPI_PRINTK( "agtiapi_ResetCard: CheckCB indicates failed reset call, "
              "try again?\n" );
    }
    else
    {
      break;
    }
  }
  if ( 1 < lIdx )
  {
    if ( AGTIAPI_FAIL == ret )
    {
      AGTIAPI_PRINTK( "agtiapi_ResetCard: soft reset failed after try %d\n",
                      lIdx );
    }
    else
    {
      AGTIAPI_PRINTK( "agtiapi_ResetCard: soft reset success at try %d\n",
                      lIdx );
    }
  }
  if( AGTIAPI_FAIL == ret )
  {
    printf( "agtiapi_ResetCard: reset ERROR\n" );
    pCard->flags &= ~AGTIAPI_INSTALLED;
    return AGTIAPI_FAIL;
  }

  pCard->flags &= ~AGTIAPI_SOFT_RESET;

  // disable all devices
  pDevice = pCard->pDevList;
  for( lIdx = 0; lIdx < maxTargets; lIdx++, pDevice++ )
  {
    /* if ( pDevice->flags & ACTIVE )
    {
      printf( "agtiapi_ResetCard: before ... active device %d\n", lIdx );
    } */
    pDevice->flags &= ~ACTIVE;
  }

  AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, *flags );
  if( tiCOMPortInit( &pCard->tiRoot, agFALSE ) != tiSuccess )
    printf( "agtiapi_ResetCard: tiCOMPortInit FAILED \n" );
  else
    AGTIAPI_PRINTK( "agtiapi_ResetCard: tiCOMPortInit success\n" );

  if( !pCard->pDevList ) {  // try to get a little sanity here
    AGTIAPI_PRINTK( "agtiapi_ResetCard: no pDevList ERROR %p\n",
                    pCard->pDevList );
    return AGTIAPI_FAIL;
  }

  AGTIAPI_PRINTK( "agtiapi_ResetCard: pre target-count %d port-count %d\n",
                  pCard->tgtCount, pCard->portCount );
  pCard->tgtCount = 0;

  DELAY( 500000 );

  pCard->flags &= ~AGTIAPI_CB_DONE;

  pPortalData = pCard->pPortalData;

  for( count = 0; count < pCard->portCount; count++ ) {
    AG_SPIN_LOCK_IRQ( agtiapi_host_lock, flags );
    pPortalInfo = &pPortalData->portalInfo;
    pPortalInfo->portStatus = 0;
    pPortalInfo->portStatus &= ~( AGTIAPI_PORT_START      |
                                  AGTIAPI_PORT_DISC_READY |
                                  AGTIAPI_DISC_DONE       |
                                  AGTIAPI_DISC_COMPLETE );

    szdv =
      sizeof( pPortalInfo->pDevList ) / sizeof( pPortalInfo->pDevList[0] );
    if (szdv > pCard->devDiscover)
    {
      szdv = pCard->devDiscover;
    }
    
    for( lIdx = 0, loop = 0;
         lIdx < szdv  &&  loop < pPortalInfo->devTotal;
         lIdx++ )
    {
      pDevice = (ag_device_t*)pPortalInfo->pDevList[lIdx];
      if( pDevice )
      {
        loop++;
        pDevice->pDevHandle = 0; // mark for availability in pCard->pDevList[]
        // don't erase more as the device is scheduled for removal on DPC
      }
      AGTIAPI_PRINTK( "agtiapi_ResetCard: reset pDev %p pDevList %p idx %d\n",
                      pDevice, pPortalInfo->pDevList, lIdx );
      pPortalInfo->devTotal = pPortalInfo->devPrev = 0;
    }

    for( lIdx = 0; lIdx < maxTargets; lIdx++ )
    { // we reconstruct dev list later in get dev handle
      pPortalInfo->pDevList[lIdx] = NULL;
    }

    for( loop = 0; loop < AGTIAPI_LOOP_MAX; loop++ )
    {
      AGTIAPI_PRINTK( "agtiapi_ResetCard: tiCOMPortStart entry data "
                      "%p / %d / %p\n",
                      &pCard->tiRoot,
                      pPortalInfo->portID,
                      &pPortalInfo->tiPortalContext );

      if( tiCOMPortStart( &pCard->tiRoot,
                          pPortalInfo->portID,
                          &pPortalInfo->tiPortalContext,
                          0 )
          != tiSuccess )
      {
        printf( "agtiapi_ResetCard: tiCOMPortStart %d FAILED\n",
                pPortalInfo->portID );
      }
      else
      {
        AGTIAPI_PRINTK( "agtiapi_ResetCard: tiCOMPortStart %d success\n",
                        pPortalInfo->portID );
        break;
      }
    }
    AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, flags );
    tiCOMGetPortInfo( &pCard->tiRoot,
                      &pPortalInfo->tiPortalContext,
                      &pPortalInfo->tiPortInfo );
    pPortalData++;
  }
  // ## fail case:  pCard->flags &= ~AGTIAPI_INSTALLED;


  AG_SPIN_LOCK_IRQ(agtiapi_host_lock, *flags);

  if( !(pCard->flags & AGTIAPI_INSTALLED) ) // driver not installed !
  {
    printf( "agtiapi_ResetCard: error, driver not intstalled? "
            "!AGTIAPI_INSTALLED \n" );
    return AGTIAPI_FAIL;
  }

  AGTIAPI_PRINTK( "agtiapi_ResetCard: total device %d\n", pCard->tgtCount );

#ifdef LOGEVENT
  agtiapi_LogEvent( pCard,
                    IOCTL_EVT_SEV_INFORMATIONAL,
                    0,
                    agNULL,
                    0,
                    "Reset initiator total device = %d!",
                    pCard->tgtCount );
#endif
  pCard->resetCount++;

  AGTIAPI_PRINTK( "agtiapi_ResetCard: clear send and done queues\n" );
  // clear send & done queue
  AG_LOCAL_LOCK( &pCard->sendLock );
  pCard->ccbSendHead = NULL;
  pCard->ccbSendTail = NULL;
  AG_LOCAL_UNLOCK( &pCard->sendLock );

  AG_LOCAL_LOCK( &pCard->doneLock );
  pCard->ccbDoneHead = NULL;
  pCard->ccbDoneTail = NULL;
  AG_LOCAL_UNLOCK( &pCard->doneLock );

  // clear smp queues also
  AG_LOCAL_LOCK( &pCard->sendSMPLock );
  pCard->smpSendHead = NULL;
  pCard->smpSendTail = NULL;
  AG_LOCAL_UNLOCK( &pCard->sendSMPLock );

  AG_LOCAL_LOCK( &pCard->doneSMPLock );
  pCard->smpDoneHead = NULL;
  pCard->smpDoneTail = NULL;
  AG_LOCAL_UNLOCK( &pCard->doneSMPLock );

  // finished with all reset stuff, now start things back up
  tiCOMSystemInterruptsActive( &pCard->tiRoot, TRUE );
  pCard->flags |= AGTIAPI_SYS_INTR_ON;
  pCard->flags |= AGTIAPI_HAD_RESET;
  pCard->flags &= ~AGTIAPI_RESET;  // ##
  agtiapi_StartIO( pCard );
  AGTIAPI_PRINTK( "agtiapi_ResetCard: local return success\n" );
  return AGTIAPI_SUCCESS;
} // agtiapi_ResetCard


/******************************************************************************
agtiapi_ReleaseHBA()

Purpose:
  Releases all resources previously acquired to support 
  a specific Host Adapter, including the I/O Address range, 
  and unregisters the agtiapi Host Adapter.
Parameters: 
  device_t dev (IN)  - device pointer
Return:
  always return 0 - success
Note:    
******************************************************************************/
int agtiapi_ReleaseHBA( device_t dev )
{
  
  int thisCard = device_get_unit( dev ); // keeping get_unit call to once
  int i;
  ag_card_info_t *thisCardInst = &agCardInfoList[ thisCard ];
  struct ccb_setasync csa; 
  struct agtiapi_softc *pCard;
  pCard = device_get_softc( dev );
  ag_card_info_t *pCardInfo = pCard->pCardInfo;
  ag_resource_info_t *pRscInfo = &thisCardInst->tiRscInfo;
  
  AG_GLOBAL_ARG(flags);

  AGTIAPI_PRINTK( "agtiapi_ReleaseHBA: start\n" );
  
  if (thisCardInst != pCardInfo)
  {
    AGTIAPI_PRINTK( "agtiapi_ReleaseHBA: Wrong ag_card_info_t thisCardInst %p "
                    "pCardInfo %p\n",
                    thisCardInst,
                    pCardInfo );
    panic( "agtiapi_ReleaseHBA: Wrong ag_card_info_t thisCardInst %p pCardInfo "
           "%p\n",
           thisCardInst,
           pCardInfo );
    return( EIO );
  }


  AGTIAPI_PRINTK( "agtiapi_ReleaseHBA card %p\n", pCard );
  pCard->flags |= AGTIAPI_SHUT_DOWN;


  // remove timer
  if (pCard->flags & AGTIAPI_TIMER_ON)
  {
    AG_SPIN_LOCK_IRQ( agtiapi_host_lock, flags );
    callout_drain( &pCard->OS_timer );
    callout_drain( &pCard->devRmTimer );
    callout_drain(&pCard->IO_timer);
    AG_SPIN_UNLOCK_IRQ( agtiapi_host_lock, flags );
    AGTIAPI_PRINTK( "agtiapi_ReleaseHBA: timer released\n" );
  }

#ifdef HIALEAH_ENCRYPTION
//Release encryption table memory - Fix it
   //if(pCard->encrypt && (pCard->flags & AGTIAPI_INSTALLED))
	//agtiapi_CleanupEncryption(pCard);
#endif

  /*
   * Shutdown the channel so that chip gets frozen
   * and it does not do any more pci-bus accesses.
   */
  if (pCard->flags & AGTIAPI_SYS_INTR_ON)
  {
    tiCOMSystemInterruptsActive( &pCard->tiRoot, FALSE );
    pCard->flags &= ~AGTIAPI_SYS_INTR_ON;
    AGTIAPI_PRINTK( "agtiapi_ReleaseHBA: card interrupt off\n" );
  }
  if (pCard->flags & AGTIAPI_INSTALLED)
  {
    tiCOMShutDown( &pCard->tiRoot );
    AGTIAPI_PRINTK( "agtiapi_ReleaseHBA: low layers shutdown\n" );
  }
  
  /* 
   * first release IRQ, so that we do not get any more interrupts
   * from this host
   */
  if (pCard->flags & AGTIAPI_IRQ_REQUESTED)
  {
    if (!agtiapi_intx_mode)
    {
      int i;
      for (i = 0; i< MAX_MSIX_NUM_VECTOR; i++)
      {
        if (pCard->irq[i] != agNULL && pCard->rscID[i] != 0)
        {
          bus_teardown_intr(dev, pCard->irq[i], pCard->intrcookie[i]);
          bus_release_resource( dev,
                                SYS_RES_IRQ,
                                pCard->rscID[i],
                                pCard->irq[i] );
        }
      }
      pci_release_msi(dev);
    }    
    pCard->flags &= ~AGTIAPI_IRQ_REQUESTED;



#ifdef AGTIAPI_DPC
    for (i = 0; i < MAX_MSIX_NUM_DPC; i++)
      tasklet_kill(&pCard->tasklet_dpc[i]);
#endif
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: IRQ released\n");
  }

  // release memory vs. alloc in agtiapi_alloc_ostimem; used in ostiAllocMemory
  if( pCard->osti_busaddr != 0 ) {
    bus_dmamap_unload( pCard->osti_dmat, pCard->osti_mapp );
  }
  if( pCard->osti_mem != NULL )  {
    bus_dmamem_free( pCard->osti_dmat, pCard->osti_mem, pCard->osti_mapp );
  }    
  if( pCard->osti_dmat != NULL ) {
    bus_dma_tag_destroy( pCard->osti_dmat );
  }

  /* unmap the mapped PCI memory */ 
  /* calls bus_release_resource( ,SYS_RES_MEMORY, ..) */ 
  agtiapi_ReleasePCIMem(thisCardInst);

  /* release all ccbs */
  if (pCard->ccbTotal)
  {
    //calls bus_dmamap_destroy() for all pccbs
    agtiapi_ReleaseCCBs(pCard);
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: CCB released\n");
  }

#ifdef HIALEAH_ENCRYPTION
/*release encryption resources - Fix it*/
  if(pCard->encrypt)
  {
    /*Check that all IO's are completed */
    if(atomic_read (&outstanding_encrypted_io_count) > 0)
    {
       printf("%s: WARNING: %d outstanding encrypted IOs !\n", __FUNCTION__, atomic_read(&outstanding_encrypted_io_count));
    }
    //agtiapi_CleanupEncryptionPools(pCard);
  }
#endif


  /* release device list */
  if( pCard->pDevList ) {
    free((caddr_t)pCard->pDevList, M_PMC_MDVT);
    pCard->pDevList = NULL;
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: device list released\n");
  }
#ifdef LINUX_PERBI_SUPPORT // ## review use of PERBI
  AGTIAPI_PRINTK( "agtiapi_ReleaseHBA: WWN list %p \n", pCard->pWWNList );
  if( pCard->pWWNList ) {
    free( (caddr_t)pCard->pWWNList, M_PMC_MTGT );
    pCard->pWWNList = NULL;
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: WWN list released\n");
  }
  if( pCard->pSLRList ) {
    free( (caddr_t)pCard->pSLRList, M_PMC_MSLR );
    pCard->pSLRList = NULL;
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: SAS Local Remote list released\n");
  }

#endif
  if (pCard->pPortalData)
  {
    free((caddr_t)pCard->pPortalData, M_PMC_MPRT);
    pCard->pPortalData = NULL;
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: PortalData released\n");
  }
  //calls contigfree() or free()  
  agtiapi_MemFree(pCardInfo);
  AGTIAPI_PRINTK("agtiapi_ReleaseHBA: low level resource released\n");

#ifdef HOTPLUG_SUPPORT
  if (pCard->flags & AGTIAPI_PORT_INITIALIZED)
  {
    //    agtiapi_FreeDevWorkList(pCard);
    AGTIAPI_PRINTK("agtiapi_ReleaseHBA: (HP dev) work resources released\n");
  }
#endif

  /* 
   * TBD, scsi_unregister may release wrong host data structure
   * which cause NULL pointer shows up.  
   */
  if (pCard->flags & AGTIAPI_SCSI_REGISTERED)
  {
    pCard->flags &= ~AGTIAPI_SCSI_REGISTERED;


#ifdef AGTIAPI_LOCAL_LOCK
    if (pCard->STLock)
    {
      //destroy mtx
      int maxLocks;
      maxLocks = pRscInfo->tiLoLevelResource.loLevelOption.numOfQueuesPerPort;

      for( i = 0; i < maxLocks; i++ ) 
      { 
        mtx_destroy(&pCard->STLock[i]);
      }     
      free(pCard->STLock, M_PMC_MSTL);
      pCard->STLock = NULL;
    }
#endif

  }
  ag_card_good--;

  /* reset agtiapi_1st_time if this is the only card */
  if (!ag_card_good && !agtiapi_1st_time)
  {
    agtiapi_1st_time = 1;
  }

  /* for tiSgl_t memeory */
  if (pCard->tisgl_busaddr != 0)
  {
    bus_dmamap_unload(pCard->tisgl_dmat, pCard->tisgl_map);
  }    
  if (pCard->tisgl_mem != NULL)
  {  
    bus_dmamem_free(pCard->tisgl_dmat, pCard->tisgl_mem, pCard->tisgl_map);
  }    
  if (pCard->tisgl_dmat != NULL)
  {  
    bus_dma_tag_destroy(pCard->tisgl_dmat);
  }
      
  if (pCard->buffer_dmat != agNULL)
  {
    bus_dma_tag_destroy(pCard->buffer_dmat);
  }
  
  if (pCard->sim != NULL) 
  {
    mtx_lock(&thisCardInst->pmIOLock);
      xpt_setup_ccb(&csa.ccb_h, pCard->path, 5);
      csa.ccb_h.func_code = XPT_SASYNC_CB;
      csa.event_enable = 0;
      csa.callback = agtiapi_async;
      csa.callback_arg = pCard;
      xpt_action((union ccb *)&csa);
      xpt_free_path(pCard->path);
 //   if (pCard->ccbTotal == 0)
    if (pCard->ccbTotal <= thisCard)
    {
      /*
        no link up so that simq has not been released.
        In order to remove cam, we call this.
      */
      xpt_release_simq(pCard->sim, 1);
    }
    xpt_bus_deregister(cam_sim_path(pCard->sim));
    cam_sim_free(pCard->sim, FALSE);
    mtx_unlock(&thisCardInst->pmIOLock);
  }
  if (pCard->devq != NULL)
  {
    cam_simq_free(pCard->devq);
  }

  //destroy mtx
  mtx_destroy( &thisCardInst->pmIOLock );
  mtx_destroy( &pCard->sendLock );
  mtx_destroy( &pCard->doneLock );
  mtx_destroy( &pCard->sendSMPLock );
  mtx_destroy( &pCard->doneSMPLock );
  mtx_destroy( &pCard->ccbLock );
  mtx_destroy( &pCard->devListLock );
  mtx_destroy( &pCard->OS_timer_lock );
  mtx_destroy( &pCard->devRmTimerLock );
  mtx_destroy( &pCard->memLock );
  mtx_destroy( &pCard->freezeLock );

  destroy_dev( pCard->my_cdev );
  memset((void *)pCardInfo, 0, sizeof(ag_card_info_t));
  return 0;
}


// Called during system shutdown after sync
static int agtiapi_shutdown( device_t dev )
{
  AGTIAPI_PRINTK( "agtiapi_shutdown\n" );
  return( 0 );
}

static int agtiapi_suspend( device_t dev )  // Device suspend routine.
{
  AGTIAPI_PRINTK( "agtiapi_suspend\n" );
  return( 0 );
}

static int agtiapi_resume( device_t dev ) // Device resume routine.
{
  AGTIAPI_PRINTK( "agtiapi_resume\n" );
  return( 0 );
}

static device_method_t agtiapi_methods[] = {   // Device interface
  DEVMETHOD( device_probe,    agtiapi_probe      ),
  DEVMETHOD( device_attach,   agtiapi_attach     ),
  DEVMETHOD( device_detach,   agtiapi_ReleaseHBA ),
  DEVMETHOD( device_shutdown, agtiapi_shutdown   ),
  DEVMETHOD( device_suspend,  agtiapi_suspend    ),
  DEVMETHOD( device_resume,   agtiapi_resume     ),
  { 0, 0 }
};

static devclass_t pmspcv_devclass;

static driver_t pmspcv_driver = {
  "pmspcv",
  agtiapi_methods,
  sizeof( struct agtiapi_softc )
};

DRIVER_MODULE( pmspcv, pci, pmspcv_driver, pmspcv_devclass, 0, 0 );
MODULE_DEPEND( pmspcv, cam, 1, 1, 1 );
MODULE_DEPEND( pmspcv, pci, 1, 1, 1 );

#include <dev/pms/freebsd/driver/common/lxosapi.c>
#include <dev/pms/freebsd/driver/ini/src/osapi.c>
#include <dev/pms/freebsd/driver/common/lxutil.c>
#include <dev/pms/freebsd/driver/common/lxencrypt.c>


