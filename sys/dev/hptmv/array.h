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

#ifndef _ARRAY_H_
#define _ARRAY_H_

/*
 * time represented in DWORD format
 */
#pragma pack(1)	
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
#pragma pack()

/***************************************************************************
 * Description: Virtual Device Table
 ***************************************************************************/

typedef struct _RaidArray 
{
    /*
     * basic information
     */
    UCHAR   bArnMember;        /* the number of members in array */
    UCHAR   bArRealnMember;    /* real member count */
    UCHAR   bArBlockSizeShift; /* the number of shift bit for a block */
    UCHAR   reserve1;

    ULONG   dArStamp;          /* array ID. all disks in a array has same ID */
	ULONG   failedStamps[4];   /* stamp for failed members */
    USHORT  bStripeWitch;      /* = (1 << BlockSizeShift) */

	USHORT	rf_broken: 1;
	USHORT	rf_need_rebuild: 1;			/* one member's data are incorrect.
	                                       for R5, if CriticalMembers==0, it means 
										   parity needs to be constructed */
	USHORT	rf_need_sync: 1;			/* need write array info to disk */
	/* ioctl flags */
	USHORT  rf_auto_rebuild: 1;
	USHORT  rf_newly_created: 1;
	USHORT  rf_rebuilding: 1;
	USHORT  rf_verifying: 1;
	USHORT  rf_initializing: 1;
	USHORT  rf_abort_rebuild: 1;
	USHORT  rf_duplicate_and_create: 1;
	USHORT  rf_duplicate_and_created: 1;
	USHORT  rf_duplicate_must_done: 1;
	USHORT  rf_raid15: 1;

	USHORT  CriticalMembers;   /* tell which member is critial */
	UCHAR   last_read;       /* for RAID 1 load banlancing */
	UCHAR   alreadyBroken;

	LBA_T   RebuildSectors;  /* how many sectors is OK (LBA on member disk) */

    PVDevice pMember[MAX_MEMBERS];
    /*
     * utility working data
     */
    UCHAR   ArrayName[MAX_ARRAY_NAME];  /* The Name of the array */
	TIME_RECORD CreateTime;				/* when created it */
	UCHAR	Description[64];		/* array description */
	UCHAR	CreateManager[16];		/* who created it */
} RaidArray;

/***************************************************************************
 *            Array Description on disk
 ***************************************************************************/
#pragma pack(1)	
typedef struct _ArrayDescript
{
	ULONG   Signature;      	/* This block is vaild array info block */
    ULONG   dArStamp;          	/* array ID. all disks in a array has same ID */

	UCHAR   bCheckSum;          /* check sum of ArrayDescript_3_0_size bytes */

#ifdef __BIG_ENDIAN_BITFIELD
	UCHAR   df_reservedbits: 6; /* put more flags here */
    UCHAR   df_user_mode_set: 1;/* user select device mode */
    UCHAR   df_bootmark:1;      /* user set boot mark on the disk */
#else 
    UCHAR   df_bootmark:1;      /* user set boot mark on the disk */
    UCHAR   df_user_mode_set: 1;/* user select device mode */
	UCHAR   df_reservedbits: 6; /* put more flags here */
#endif

    UCHAR   bUserDeviceMode;	/* see device.h */
    UCHAR   ArrayLevel;			/* how many level[] is valid */

	struct {
	    ULONG   Capacity;         	/* capacity for the array */

		UCHAR   VDeviceType;  		/* see above & arrayType in array.h */
	    UCHAR   bMemberCount;		/* all disk in the array */
	    UCHAR   bSerialNumber;  	/* Serial Number	*/	
	    UCHAR   bArBlockSizeShift; 	/* the number of shift bit for a block */

#ifdef __BIG_ENDIAN_BITFIELD
		USHORT  rf_reserved: 14;
		USHORT  rf_raid15: 1;       /* don't remove even you don't use it */
		USHORT  rf_need_rebuild:1;  /* array is critical */
#else 
		USHORT  rf_need_rebuild:1;  /* array is critical */
		USHORT  rf_raid15: 1;       /* don't remove even you don't use it */
		USHORT  rf_reserved: 14;
#endif
		USHORT  CriticalMembers;    /* record critical members */
		ULONG   RebuildSectors;  /* how many sectors is OK (LBA on member disk) */
	} level[2];

    UCHAR   ArrayName[MAX_ARRAY_NAME];  /* The Name of the array */
	TIME_RECORD CreateTime;				/* when created it */
	UCHAR	Description[64];		/* array description */
	UCHAR	CreateManager[16];		/* who created it */

#define ArrayDescript_3_0_size ((unsigned)(ULONG_PTR)&((struct _ArrayDescript *)0)->bCheckSum31)
#define ArrayDescript_3_1_size 512

	UCHAR   bCheckSum31;        /* new check sum */
	UCHAR   PrivateFlag1;       /* private */ 
	UCHAR   alreadyBroken;      /* last stamp has been saved to failedStamps */
	
#ifdef __BIG_ENDIAN_BITFIELD
    UCHAR   df_read_ahead: 1;   /* enable read ahead */
	UCHAR   df_read_ahead_set: 1;
    UCHAR   df_write_cache: 1;  /* enable write cache */
	UCHAR   df_write_cache_set: 1;
    UCHAR   df_ncq: 1;          /* enable NCQ */
	UCHAR   df_ncq_set: 1;
    UCHAR   df_tcq: 1;          /* enable TCQ */
	UCHAR   df_tcq_set: 1;
#else 
	UCHAR   df_tcq_set: 1;
    UCHAR   df_tcq: 1;          /* enable TCQ */
	UCHAR   df_ncq_set: 1;
    UCHAR   df_ncq: 1;          /* enable NCQ */
	UCHAR   df_write_cache_set: 1;
    UCHAR   df_write_cache: 1;  /* enable write cache */
	UCHAR   df_read_ahead_set: 1;
    UCHAR   df_read_ahead: 1;   /* enable read ahead */
#endif
    
    struct {
    	ULONG CapacityHi32;
    	ULONG RebuildSectorsHi32;
    }
    levelex[2];

	ULONG failedStamps[4]; /* failed memebrs's stamps */

} ArrayDescript;

/* report an error if ArrayDescript size exceed 512 */
typedef char ArrayDescript_size_should_not_exceed_512[512-sizeof(ArrayDescript)];

#pragma pack()

/* Signature */
#define HPT_ARRAY_V3          0x5a7816f3
#ifdef ARRAY_V2_ONLY
#define SAVE_FOR_RAID_INFO    0
#else 
#define SAVE_FOR_RAID_INFO    10
#endif

/***************************************************************************
 *  Function protocol for array layer
 ***************************************************************************/

/*
 * array.c
 */
ULONG FASTCALL GetStamp(void);
void HPTLIBAPI SyncArrayInfo(PVDevice pVDev);
void HPTLIBAPI fDeleteArray(_VBUS_ARG PVDevice pVArray, BOOLEAN del_block0);

/*
 * iArray.c
 */
void HPTLIBAPI fCheckArray(PDevice pDevice);
void HPTLIBAPI CheckArrayCritical(_VBUS_ARG0);
PVDevice HPTLIBAPI GetSpareDisk(_VBUS_ARG PVDevice pArray);
#ifdef SUPPORT_OLD_ARRAY
void HPTLIBAPI fFixRAID01Stripe(_VBUS_ARG PVDevice pStripe);
#endif

/***************************************************************************
 *  Macro defination
 ***************************************************************************/
#ifndef MAX_ARRAY_PER_VBUS
#define MAX_ARRAY_PER_VBUS (MAX_VDEVICE_PER_VBUS*2) /* worst case */
#endif


#if defined(MAX_ARRAY_DEVICE)
#if MAX_ARRAY_DEVICE!=MAX_ARRAY_PER_VBUS
#error "remove MAX_ARRAY_DEVICE and use MAX_ARRAY_PER_VBUS instead"
#endif
#endif

#define _SET_ARRAY_BUS_(pArray) pArray->pVBus = _vbus_p;

#ifdef ARRAY_V2_ONLY
#define _SET_ARRAY_VER_(pArray) pArray->vf_format_v2 = 1;
#else 
#define _SET_ARRAY_VER_(pArray)
#endif

#define mArGetArrayTable(pVArray) \
	if((pVArray = _vbus_(pFreeArrayLink)) != 0) { \
    	_vbus_(pFreeArrayLink) = (PVDevice)_vbus_(pFreeArrayLink)->pVBus; \
    	ZeroMemory(pVArray, ARRAY_VDEV_SIZE); \
		_SET_ARRAY_BUS_(pVArray) \
		_SET_ARRAY_VER_(pVArray) \
    } else

#define mArFreeArrayTable(pVArray) \
	do { \
		pVArray->pVBus = (PVBus)_vbus_(pFreeArrayLink);\
    	_vbus_(pFreeArrayLink) = pVArray; \
    	pVArray->u.array.dArStamp = 0; \
    } while(0)

UCHAR CheckSum(UCHAR *p, int size);

void HPTLIBAPI fRAID0SendCommand(_VBUS_ARG PCommand pCmd);
void HPTLIBAPI fRAID1SendCommand(_VBUS_ARG PCommand pCmd);
void HPTLIBAPI fJBODSendCommand(_VBUS_ARG PCommand pCmd);
void HPTLIBAPI fRAID0MemberFailed(_VBUS_ARG PVDevice pVDev);
void HPTLIBAPI fRAID1MemberFailed(_VBUS_ARG PVDevice pVDev);
void HPTLIBAPI fJBODMemberFailed(_VBUS_ARG PVDevice pVDev);
#if SUPPORT_RAID5
void HPTLIBAPI fRAID5SendCommand(_VBUS_ARG PCommand pCmd);
void HPTLIBAPI fRAID5MemberFailed(_VBUS_ARG PVDevice pVDev);
#else 
#define fRAID5SendCommand 0
#define fRAID5MemberFailed 0
#endif

#endif
