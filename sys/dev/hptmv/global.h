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
#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include <dev/hptmv/mvOs.h>
#include <dev/hptmv/mvSata.h>
#include <dev/hptmv/mvStorageDev.h>

#define COMPANY      "HighPoint Technologies, Inc."
#define COPYRIGHT    "(c) 2000-2007. HighPoint Technologies, Inc."
#define DRIVER_NAME		"RocketRAID 18xx SATA Controller driver"
#define CONTROLLER_NAME	"RocketRAID 18xx SATA Controller"
#define PROC_DIR_NAME hptmv

#define HPT_INTERFACE_VERSION 0x01010000
#define SUPPORT_48BIT_LBA
#define SUPPORT_ARRAY
#define SUPPORT_RAID5 1
#define _RAID5N_
#define MAX_QUEUE_COMM 32
#define MAX_SG_DESCRIPTORS 17
#define MAX_VBUS 2    /*one vbus per adapter in mv linux driver, 
                        MAX_VBUS is defined for share code and can not be 1*/

#define SET_VBUS_FOR_EACH_CONTROLLER
#define MAX_MEMBERS 8
#define MAX_ARRAY_NAME 16
#define MAX_VDEVICE_PER_VBUS 8
#define MAX_ARRAY_DEVICE MAX_ARRAY_PER_VBUS
#define MAX_CHIP_IN_VBUS 1

#define SUPPORT_IOCTL
#define SUPPORT_FAIL_LED

typedef void * PChipInstance;
typedef void * PChannel;
typedef struct _VDevice *PVDevice;
typedef struct _VBus *PVBus;
typedef struct _ArrayDescript *PArrayDescript;
typedef struct _ArrayDescriptV2 *PArrayDescriptV2;
typedef struct _Command *PCommand;

typedef struct _Device {
	UCHAR df_on_line;
	UCHAR df_atapi;
	UCHAR df_removable_drive;
	UCHAR busyCount;

	UCHAR df_tcq_set: 1;
    UCHAR df_tcq: 1;          /* enable TCQ */
	UCHAR df_ncq_set: 1;
    UCHAR df_ncq: 1;          /* enable NCQ */
	UCHAR df_write_cache_set: 1;
    UCHAR df_write_cache: 1;  /* enable write cache */
	UCHAR df_read_ahead_set: 1;
    UCHAR df_read_ahead: 1;   /* enable read ahead */
		
	UCHAR retryCount;
	UCHAR resetCount;
	UCHAR pad1;
		
	UCHAR df_user_mode_set;
    UCHAR bDeModeSetting;    /* Current Data Transfer mode: 0-4 PIO 0-4 */
    UCHAR bDeUsable_Mode;       /* actual maximum data transfer mode */
	UCHAR bDeUserSelectMode;
	
	PVBus pVBus;
	ULONG dDeRealCapacity;
	ULONG dDeHiddenLba;
	ULONG HeadPosition;
	ULONG QueueLength;
	MV_SATA_CHANNEL *mv;
}
Device, *PDevice;

typedef struct _SCAT_GATH
{
    ULONG_PTR     dSgAddress;
    USHORT        wSgSize;
    USHORT        wSgFlag;
} SCAT_GATH, FAR *FPSCAT_GATH;

#define OS_VDEV_EXT
typedef struct _VDevice_Ext
{
	UCHAR gui_locked; /* the device is locked by GUI */
	UCHAR reserve[3];
} VDevice_Ext, *PVDevice_Ext;    


#define SG_FLAG_SKIP        0x4000
#define SG_FLAG_EOT         0x8000

#define _VBUS_ARG0 PVBus _vbus_p
#define _VBUS_ARG PVBus _vbus_p,
#define _VBUS_P _vbus_p,
#define _VBUS_P0 _vbus_p
#define _VBUS_INST(x) PVBus _vbus_p = x;
#define _vbus_(x) (_vbus_p->x)

/*************************************************************************
 * arithmetic functions 
 *************************************************************************/
#define LongRShift(x, y) 	(x >> y)
#define LongLShift(x, y)   	(x << y)
#define LongDiv(x, y)      	(x / (UINT)(y))
#define LongRem(x, y)		(x % (UINT)(y))
#define LongMul(x, y)      	(x * y)

/*************************************************************************
 * C library
 *************************************************************************/
int HPTLIBAPI os_memcmp(const void *cs, const void *ct, unsigned len);
void HPTLIBAPI os_memcpy(void *to, const void *from, unsigned len);
void HPTLIBAPI os_memset(void *s, char c, unsigned len);
unsigned HPTLIBAPI os_strlen(const char *s);

#ifdef NO_LIBC
#define memcmp os_memcmp
#define memcpy os_memcpy
#define memset os_memset
#define strlen os_strlen
#endif
#define ZeroMemory(a, b)  	memset((char *)a, 0, b)
#define MemoryCopy(a,b,c) 	memcpy((char *)(a), (char *)(b), (UINT)(c))
#define farMemoryCopy(a,b,c) memcpy((char *)(a), (char *)(b), (UINT)c)
#define StrLen            	strlen

/* 
 * we don't want whole hptintf.h in shared code...
 * some constants must match that in hptintf.h!
 */
enum _driver_events_t
{	
	ET_DEVICE=0,
    ET_DEVICE_REMOVED,
    ET_DEVICE_PLUGGED,
    ET_DEVICE_ERROR,
    ET_REBUILD_STARTED,
    ET_REBUILD_ABORTED,
    ET_REBUILD_FINISHED,
    ET_SPARE_TOOK_OVER,
    ET_REBUILD_FAILED,
	ET_VERIFY_STARTED,   
	ET_VERIFY_ABORTED,   
	ET_VERIFY_FAILED,    
	ET_VERIFY_FINISHED,  
	ET_INITIALIZE_STARTED,   
	ET_INITIALIZE_ABORTED,   
	ET_INITIALIZE_FAILED,    
	ET_INITIALIZE_FINISHED,  
	ET_VERIFY_DATA_ERROR,    
};

#define StallExec(x) mvMicroSecondsDelay(x)
extern void HPTLIBAPI ioctl_ReportEvent(UCHAR event, PVOID param);
#define fNotifyGUI(WhatHappen, pVDevice) ioctl_ReportEvent(WhatHappen, pVDevice)
#define DECLARE_BUFFER(type, ptr) UCHAR ptr##__buf[512]; type ptr=(type)ptr##__buf

int HPTLIBAPI fDeReadWrite(PDevice pDev, ULONG Lba, UCHAR Cmd, void *tmpBuffer);
void HPTLIBAPI fDeSelectMode(PDevice pDev, UCHAR NewMode);
int HPTLIBAPI fDeSetTCQ(PDevice pDev, int enable, int depth);
int HPTLIBAPI fDeSetNCQ(PDevice pDev, int enable, int depth);
int HPTLIBAPI fDeSetWriteCache(PDevice pDev, int enable);
int HPTLIBAPI fDeSetReadAhead(PDevice pDev, int enable);

#include <dev/hptmv/atapi.h>
#include <dev/hptmv/command.h>
#include <dev/hptmv/array.h>
#include <dev/hptmv/raid5n.h>
#include <dev/hptmv/vdevice.h>

#if defined(__FreeBSD__) && defined(HPTLIBAPI)
#undef HPTLIBAPI
#define HPTLIBAPI 
#endif

#ifdef SUPPORT_ARRAY
#define ArrayTables(i) ((PVDevice)&_vbus_(_ArrayTables)[i*ARRAY_VDEV_SIZE])
#endif

#endif
