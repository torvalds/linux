/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 HighPoint Technologies, Inc.
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

#ifndef _VDEVICE_H_
#define _VDEVICE_H_

/***************************************************************************
 * Description:  virtual device header
 ***************************************************************************/

typedef  struct _VDevice
{
	UCHAR        VDeviceType;
	UCHAR        vf_bootmark: 1; 	/* is boot device? */
	UCHAR		 vf_bootable: 1;    /* has active partition */
	UCHAR        vf_online: 1; 		/* is usable? */
	UCHAR        vf_cache_disk: 1;  /* Cache enabled */
	UCHAR        vf_format_v2: 1;   /* old array block */
	UCHAR        vf_freed: 1;       /* memory free */
	UCHAR        reserve1;
	UCHAR        bSerialNumber; 	/* valid if pParent!=0 */

	PVDevice	 pParent;			/* parent array */
	PVBus        pVBus;				/* vbus this device located. Must not be NULL. */

	LBA_T        VDeviceCapacity;   /* number of blocks */

	LBA_T        LockedLba;
	USHORT       LockedSectors;
	USHORT       ActiveRequests;
	PCommand     LockWaitList;
	void (* HPTLIBAPI QuiesceAction)(_VBUS_ARG void *arg);
	void  *QuiesceArg;
	void (* HPTLIBAPI flush_callback)(_VBUS_ARG void *arg);
	void *flush_callback_arg;


#if defined(_RAID5N_)
	struct stripe **CacheEntry;
	struct range_lock *range_lock;
#endif

	void (* HPTLIBAPI pfnSendCommand)(_VBUS_ARG PCommand pCmd);   /* call this to send a command to a VDevice */
	void (* HPTLIBAPI pfnDeviceFailed)(_VBUS_ARG PVDevice pVDev); /* call this when a VDevice failed */

	union {
#ifdef SUPPORT_ARRAY
		RaidArray array;
#endif
		Device disk;
	} u;

} VDevice;

#define ARRAY_VDEV_SIZE (offsetof(VDevice, u) + sizeof(RaidArray))
#define DISK_VDEV_SIZE  (offsetof(VDevice, u) + sizeof(Device))

#define Map2pVDevice(pDev) ((PVDevice)((UINT_PTR)pDev - (UINT)(UINT_PTR)&((PVDevice)0)->u.disk))

/*
 * bUserDeviceMode
 */
#define MEMBER_NOT_SET_MODE  0x5F

/*
 * arrayType
 */
#define VD_SPARE             0
#define VD_REMOVABLE         1
#define VD_ATAPI             2
#define VD_SINGLE_DISK       3

#define VD_JBOD              4 /* JBOD */
#define VD_RAID_0            5 /* RAID 0 stripe */
#define VD_RAID_1            6 /* RAID 1 mirror */
#define VD_RAID_3            7 /* RAID 3 */
#define VD_RAID_5            8 /* RAID 5 */
#define VD_MAX_TYPE 8

#ifdef SUPPORT_ARRAY
#define mIsArray(pVDev) (pVDev->VDeviceType>VD_SINGLE_DISK)
#else 
#define mIsArray(pVDev) 0
#endif

extern void (* HPTLIBAPI pfnSendCommand[])(_VBUS_ARG PCommand pCmd);
extern void (* HPTLIBAPI pfnDeviceFailed[])(_VBUS_ARG PVDevice pVDev);
void HPTLIBAPI fOsDiskFailed(_VBUS_ARG PVDevice pVDev);
void HPTLIBAPI fDeviceSendCommand(_VBUS_ARG PCommand pCmd);
void HPTLIBAPI fSingleDiskFailed(_VBUS_ARG PVDevice pVDev);

/***************************************************************************
 * Description:  RAID Adapter
 ***************************************************************************/

typedef struct _VBus  {
	/* pVDevice[] may be non-continuous */
	PVDevice      pVDevice[MAX_VDEVICE_PER_VBUS];

	UINT          nInstances;
	PChipInstance pChipInstance[MAX_CHIP_IN_VBUS];

	void *        OsExt; /* for OS private use */

	
	int serial_mode;
	int next_active;
	int working_devs;



	PCommand pFreeCommands;
	DPC_ROUTINE PendingRoutines[MAX_PENDING_ROUTINES];
	int PendingRoutinesFirst, PendingRoutinesLast;
	DPC_ROUTINE IdleRoutines[MAX_IDLE_ROUTINES];
	int IdleRoutinesFirst, IdleRoutinesLast;

#ifdef SUPPORT_ARRAY
	PVDevice pFreeArrayLink;
	BYTE    _ArrayTables[MAX_ARRAY_PER_VBUS * ARRAY_VDEV_SIZE];
#endif


#ifdef _RAID5N_
	struct r5_global_data r5;
#endif

} VBus;

/*
 * Array members must be on same VBus.
 * The platform dependent part shall select one of the following strategy.
 */
#ifdef SET_VBUS_FOR_EACH_IRQ
#define CHIP_ON_SAME_VBUS(pChip1, pChip2) ((pChip1)->bChipIntrNum==(pChip2)->bChipIntrNum)
#elif defined(SET_VBUS_FOR_EACH_CONTROLLER)
#define CHIP_ON_SAME_VBUS(pChip1, pChip2) \
	((pChip1)->pci_bus==(pChip2)->pci_bus && (pChip1)->pci_dev==(pChip2)->pci_dev)
#elif defined(SET_VBUS_FOR_EACH_FUNCTION)
#define CHIP_ON_SAME_VBUS(pChip1, pChip2) \
	((pChip1)->pci_bus==(pChip2)->pci_bus && (pChip1)->pci_dev==(pChip2)->pci_dev && (pChip1)->pci_func==(pChip2)->pci_func)
#else 
#error You must set one vbus setting
#endif

#define FOR_EACH_CHANNEL_ON_VBUS(_pVBus, _pChan) \
		for (_pChan=pChanStart; _pChan<pChanEnd; _pChan++) \
			if (_pChan->pChipInstance->pVBus!=_pVBus) ; else

#define FOR_EACH_DEV_ON_VBUS(pVBus, pVDev, i) \
		for(i = 0; i < MAX_VDEVICE_PER_VBUS; i++) \
			if ((pVDev=pVBus->pVDevice[i])==0) continue; else


#define FOR_EACH_VBUS(pVBus) \
	for(pVBus = gVBus; pVBus < &gVBus[MAX_VBUS]; pVBus++) \

#define FOR_EACH_ARRAY_ON_ALL_VBUS(pVBus, pArray, i) \
	for(pVBus = gVBus; pVBus < &gVBus[MAX_VBUS]; pVBus++) \
		for(i = 0; i < MAX_ARRAY_PER_VBUS; i++) \
			if ((pArray=((PVDevice)&pVBus->_ArrayTables[i*ARRAY_VDEV_SIZE]))->u.array.dArStamp==0) continue; else

#define FOR_EACH_DEV_ON_ALL_VBUS(pVBus, pVDev, i) \
	FOR_EACH_VBUS(pVBus) \
		for(i = 0; i < MAX_VDEVICE_PER_VBUS; i++) \
			if ((pVDev=pVBus->pVDevice[i])==0) continue; else

/***************************************************************************
 * Description:  the functions called by IDE layer
 ***************************************************************************/
#ifdef SUPPORT_ARRAY
#define IdeRegisterDevice               fCheckArray
#else 
void HPTLIBAPI IdeRegisterDevice(PDevice pDev);
#endif

/***************************************************************************
 * Description:  the functions OS must provided
 ***************************************************************************/

void HPTLIBAPI OsSetDeviceTable(PDevice pDevice, PIDENTIFY_DATA pIdentify);

/*
 * allocate and free data structure
 */
PChannel fGetChannelTable(void);
PDevice  fGetDeviceTable(void);
#define  OsGetChannelTable(x, y)  fGetChannelTable()
#define  OsGetDeviceTable(x, y)   fGetDeviceTable()
void 	OsReturnTable(PDevice pDevice);
/***************************************************************************
 * Description:  the functions Prototype
 ***************************************************************************/
/*
 * vdevice.c
 */
int Initialize(void);
int InitializeAllChips(void);
void InitializeVBus(PVBus pVBus);
void fRegisterChip(PChipInstance pChip);
void __fRegisterVDevices(PVBus pVBus);
void fRegisterVDevices(void);
void HPTLIBAPI UnregisterVDevice(PVDevice);
void HPTLIBAPI fCheckBootable(PVDevice pVDev);
void HPTLIBAPI fFlushVDev(PVDevice pVDev);
void HPTLIBAPI fFlushVDevAsync(PVDevice pVDev, DPC_PROC done, void *arg);
void HPTLIBAPI fShutdownVDev(PVDevice pVDev);
void HPTLIBAPI fResetVBus(_VBUS_ARG0);
void HPTLIBAPI fCompleteAllCommandsSynchronously(PVBus _vbus_p);

#define RegisterVDevice(pVDev)
#define OsRegisterDevice(pVDev)
#define OsUnregisterDevice(pVDev)

#ifdef SUPPORT_VBUS_CONFIG
void VBus_Config(PVBus pVBus, char *str);
#else 
#define VBus_Config(pVBus, str)
#endif

#pragma pack(1)
struct fdisk_partition_table
{
	UCHAR 		bootid;   			/* bootable?  0=no, 128=yes  */
	UCHAR 		beghead;  			/* beginning head number */
	UCHAR 		begsect;  			/* beginning sector number */
	UCHAR		begcyl;   			/* 10 bit nmbr, with high 2 bits put in begsect */
	UCHAR		systid;   			/* Operating System type indicator code */
	UCHAR 		endhead;  			/* ending head number */
	UCHAR 		endsect;  			/* ending sector number */
	UCHAR 		endcyl;   			/* also a 10 bit nmbr, with same high 2 bit trick */
	ULONG   	relsect;            /* first sector relative to start of disk */
	ULONG 		numsect;            /* number of sectors in partition */
};

typedef struct _Master_Boot_Record
{
	UCHAR   bootinst[446];   		/* space to hold actual boot code */
	struct 	fdisk_partition_table parts[4];
	USHORT  signature;       		/* set to 0xAA55 to indicate PC MBR format */
}
Master_Boot_Record, *PMaster_Boot_Record;

#ifndef SUPPORT_ARRAY
/* TODO: move it later */
#ifdef __BIG_ENDIAN_BITFIELD
typedef DWORD TIME_RECORD;
#else 
typedef struct _TIME_RECORD {
   UINT        seconds:6;      /* 0 - 59 */
   UINT        minutes:6;      /* 0 - 59 */
   UINT        month:4;        /* 1 - 12 */
   UINT        hours:6;        /* 0 - 59 */
   UINT        day:5;          /* 1 - 31 */
   UINT        year:5;         /* 0=2000, 31=2031 */
} TIME_RECORD;
#endif
#endif

#pragma pack()
#endif
