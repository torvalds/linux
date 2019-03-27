/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) HighPoint Technologies, Inc.
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
#include <dev/hptrr/hptrr_config.h>
/*
 * $Id: him.h,v 1.47 2007/12/17 08:55:49 gmm Exp $
 * Copyright (C) 2004-2005 HighPoint Technologies, Inc. All rights reserved.
 */
#ifndef _HPT_HIM_H_
#define _HPT_HIM_H_

#define VERMAGIC_HIM 46

#if defined(__cplusplus)
extern "C" {
#endif

#include <dev/hptrr/list.h>

#define SECTOR_TO_BYTE_SHIFT 9
#define SECTOR_TO_BYTE(x)       ((HPT_U32)(x) << SECTOR_TO_BYTE_SHIFT)
#define BYTE_TO_SECTOR(x)       ((x)>>SECTOR_TO_BYTE_SHIFT)

typedef struct _PCI_ID
{
	HPT_U16 vid;
	HPT_U16 did;
	HPT_U32 subsys;
	HPT_U8  rev;
	HPT_U8  nbase;
	HPT_U16 reserve;
}
PCI_ID;

typedef struct _PCI_ADDRESS
{
	HPT_U8 tree;
	HPT_U8 bus;
	HPT_U8 device;
	HPT_U8 function;
}
PCI_ADDRESS;

typedef struct _HIM_ADAPTER_CONFIG
{
	PCI_ADDRESS pci_addr;
	PCI_ID  pci_id;

	HPT_U8  max_devices;
	HPT_U8  reserve1;

	HPT_U8  bDevsPerBus;
	HPT_U8  first_on_slot;

	HPT_U8  bChipType;
	HPT_U8  bChipIntrNum;
	HPT_U8  bChipFlags;
	HPT_U8  bNumBuses;

	HPT_U8  szVendorID[36];
	HPT_U8  szProductID[36];
}
HIM_ADAPTER_CONFIG, *PHIM_ADAPTER_CONFIG;

typedef struct _HIM_CHANNEL_CONFIG
{
	HPT_U32 io_port;
	HPT_U32 ctl_port;
} HIM_CHANNEL_CONFIG, *PHIM_CHANNEL_CONFIG;

typedef struct _HIM_DEVICE_FLAGS
{
	HPT_UINT df_atapi               :1;   
	HPT_UINT df_removable_drive     :1;
	HPT_UINT df_on_line             :1;   
	HPT_UINT df_reduce_mode         :1;   
	HPT_UINT df_sata                :1;   
	HPT_UINT df_on_pm_port          :1;   
	HPT_UINT df_support_read_ahead  :1;
	HPT_UINT df_read_ahead_enabled  :1;
	HPT_UINT df_support_write_cache :1;
	HPT_UINT df_write_cache_enabled :1;
	HPT_UINT df_cdrom_device        :1;
	HPT_UINT df_tape_device         :1;
	HPT_UINT df_support_tcq         :1;
	HPT_UINT df_tcq_enabled         :1;
	HPT_UINT df_support_ncq         :1;
	HPT_UINT df_ncq_enabled         :1;
	HPT_UINT df_sas                 :1;
} DEVICE_FLAGS, *PDEVICE_FLAGS;

#pragma pack(1)
typedef struct _IDENTIFY_DATA2 {
	HPT_U16 GeneralConfiguration;
	HPT_U16 NumberOfCylinders;
	HPT_U16 Reserved1;
	HPT_U16 NumberOfHeads;
	HPT_U16 UnformattedBytesPerTrack;
	HPT_U16 UnformattedBytesPerSector;
	HPT_U16 SectorsPerTrack;
	HPT_U16 VendorUnique1[3];
	HPT_U16 SerialNumber[10];
	HPT_U16 BufferType;
	HPT_U16 BufferSectorSize;
	HPT_U16 NumberOfEccBytes;
	HPT_U16 FirmwareRevision[4];
	HPT_U16 ModelNumber[20];
	HPT_U8  MaximumBlockTransfer;
	HPT_U8  VendorUnique2;
	HPT_U16 DoubleWordIo;
	HPT_U16 Capabilities;
	HPT_U16 Reserved2;
	HPT_U8  VendorUnique3;
	HPT_U8  PioCycleTimingMode;
	HPT_U8  VendorUnique4;
	HPT_U8  DmaCycleTimingMode;
	HPT_U16 TranslationFieldsValid;
	HPT_U16 NumberOfCurrentCylinders;
	HPT_U16 NumberOfCurrentHeads;
	HPT_U16 CurrentSectorsPerTrack;
	HPT_U32 CurrentSectorCapacity;
	HPT_U16 CurrentMultiSectorSetting;
	HPT_U32 UserAddressableSectors;
	HPT_U8  SingleWordDMASupport;
	HPT_U8  SingleWordDMAActive;
	HPT_U8  MultiWordDMASupport;
	HPT_U8  MultiWordDMAActive;
	HPT_U8  AdvancedPIOModes;
	HPT_U8  Reserved4;
	HPT_U16 MinimumMWXferCycleTime;
	HPT_U16 RecommendedMWXferCycleTime;
	HPT_U16 MinimumPIOCycleTime;
	HPT_U16 MinimumPIOCycleTimeIORDY;
	HPT_U16 Reserved5[2];
	HPT_U16 ReleaseTimeOverlapped;
	HPT_U16 ReleaseTimeServiceCommand;
	HPT_U16 MajorRevision;
	HPT_U16 MinorRevision;
}
#ifdef __GNUC__
__attribute__((packed))
#endif
IDENTIFY_DATA2, *PIDENTIFY_DATA2;
#pragma pack()

typedef struct _HIM_DEVICE_CONFIG
{
	HPT_U64 capacity;

	DEVICE_FLAGS flags;

	HPT_U8  path_id;
	HPT_U8  target_id;
	HPT_U8  max_queue_depth;
	HPT_U8  spin_up_mode;

	HPT_U8  reserved;
	HPT_U8  transfer_mode;        
	HPT_U8  bMaxShowMode;         
	HPT_U8  bDeUsable_Mode;       

	HPT_U16 max_sectors_per_cmd;

	PIDENTIFY_DATA2 pIdentifyData;

}
HIM_DEVICE_CONFIG, *PHIM_DEVICE_CONFIG;


#define _DIT_MODE               0
#define _DIT_601                1
#define _DIT_READ_AHEAD         2
#define _DIT_WRITE_CACHE        3
#define _DIT_TCQ                4
#define _DIT_NCQ                5
#define _DIT_BEEP_OFF           6
#define _DIT_SPIN_UP_MODE       7
#define _DIT_IDLE_STANDBY       8
#define _DIT_IDENTIFY           9

#define SPIN_UP_MODE_NOSUPPORT 0
#define SPIN_UP_MODE_FULL      1
#define SPIN_UP_MODE_STANDBY   2

struct tcq_control {
	HPT_U8 enable;
	HPT_U8 depth;
};

struct ncq_control {
	HPT_U8 enable;
	HPT_U8 depth;
};

typedef struct _HIM_ALTERABLE_DEV_INFO{
	HPT_U8 type;
	union {
		HPT_U8 mode;
		HPT_U8 enable_read_ahead;
		HPT_U8 enable_read_cache;
		HPT_U8 enable_write_cache;
		struct tcq_control tcq;
		struct ncq_control ncq;
		void * adapter;
		HPT_U8 spin_up_mode;
		HPT_U8 idle_standby_timeout;
		HPT_U8 identify_indicator;
	}u;
} HIM_ALTERABLE_DEV_INFO, *PHIM_ALTERABLE_DEV_INFO;

struct _COMMAND;
struct _IOCTL_ARG;

typedef void (*PROBE_CALLBACK)(void *arg, void *dev, int index);

typedef struct _HIM {
	char *name;
	struct _HIM *next;
	HPT_UINT max_sg_descriptors;
	#define _HIM_INTERFACE(_type, _fn, _args) _type (* _fn) _args;
	#include <dev/hptrr/himfuncs.h>
}
HIM, *PHIM;


#pragma pack(1)
#ifdef SG_FLAG_EOT
#error "don't use SG_FLAG_EOT with _SG.eot. clean the code!"
#endif

typedef struct _SG {
	HPT_U32 size;
	HPT_UINT eot; 
	union {
		HPT_U8 FAR * _logical; 
		BUS_ADDRESS bus;
	}
	addr;
}
SG, *PSG;
#pragma pack()

typedef struct _AtaCommand
{
    HPT_U64     Lba;          
    HPT_U16     nSectors;     
    HPT_U16     pad;
} AtaComm, *PAtaComm;

#define ATA_CMD_SET_FEATURES    0xef
#define ATA_CMD_FLUSH           0xE7
#define ATA_CMD_VERIFY          0x40
#define ATA_CMD_STANDBY         0xe2
#define ATA_CMD_READ_MULTI      0xC4
#define ATA_CMD_READ_MULTI_EXT  0x29
#define ATA_CMD_WRITE_MULTI     0xC5
#define ATA_CMD_WRITE_MULTI_EXT 0x39
#define ATA_CMD_WRITE_MULTI_FUA_EXT     0xCE

#define ATA_SET_FEATURES_XFER 0x3
#define ATA_SECTOR_SIZE 512

typedef struct _PassthroughCmd {
	HPT_U16    bFeaturesReg;     
	HPT_U16    bSectorCountReg;  
	HPT_U16    bLbaLowReg;       
	HPT_U16    bLbaMidReg;       
	HPT_U16    bLbaHighReg;      
	HPT_U8     bDriveHeadReg;    
	HPT_U8     bCommandReg;      
	HPT_U8     nSectors;         
	HPT_U8    *pDataBuffer;      
}
PassthroughCmd;

typedef struct _ScsiComm {
	HPT_U8  cdbLength;
	HPT_U8  senseLength; 
	HPT_U8  scsiStatus; 
	HPT_U8  reserve1;
	HPT_U32 dataLength; 
	HPT_U8 *cdb;
	HPT_U8 *senseBuffer;
}
ScsiComm;


#define CTRL_CMD_REBUILD 1
#define CTRL_CMD_VERIFY  2
#define CTRL_CMD_INIT    3


typedef struct _R5ControlCmd {
	HPT_U64  StripeLine;  
	HPT_U16 Offset;       
	HPT_U8  Command;      
	HPT_U8  reserve1;
}
R5ControlCmd, *PR5ControlCmd;

typedef struct _HPT_ADDRESS
{
	HPT_U8 * logical;
	BUS_ADDRESS bus;
}
HPT_ADDRESS;


typedef struct ctl_pages {
	HPT_ADDRESS *pages;
	HPT_UINT        page_size; 
	HPT_UINT        npages;
	HPT_UINT min_sg_descriptors; 
} CONTROL_PAGES, *PCONTROL_PAGES;

typedef struct _R1ControlCmd {
	HPT_U64  Lba;
	HPT_U16 nSectors;
	HPT_U8  Command;      /* CTRL_CMD_XXX */
	HPT_U8  reserve1;
	PCONTROL_PAGES ctl_pages;  
}
R1ControlCmd, *PR1ControlCmd;

typedef void (*TQ_PROC)(void *arg);

struct tq_item {
	TQ_PROC proc;
	void *arg;
	struct tq_item *next;
};

#define INIT_TQ_ITEM(t, p, a) \
	do { (t)->proc = p; (t)->arg = a; (t)->next = 0; } while (0)

typedef struct _COMMAND
{
	
	struct _VBUS * vbus;

	struct freelist *grplist; 
	HPT_UINT grpcnt; 

	
	struct list_head q_link;
	struct tq_item done_dpc;

	HPT_UINT extsize;   
	void *ext;

	

	void *target;      
	void *priv;        
	HPT_UPTR priv2;    

	int priority;
	struct lock_request *owned_lock; 
	struct lock_request *lock_req;   
	void (*dtor)(struct _COMMAND *, void *);
	void *dtor_arg;

	union{
		AtaComm Ide;
		PassthroughCmd Passthrough;
		ScsiComm Scsi;
		R5ControlCmd R5Control;
		R1ControlCmd R1Control;
	} uCmd;

	HPT_U8 type; /* CMD_TYPE_* */

	struct {
		HPT_U8  physical_sg: 1;
		HPT_U8  data_in: 1;
		HPT_U8  data_out: 1;
		HPT_U8  transform : 1;
		HPT_U8  hard_flush: 2; 
		HPT_U8  from_cc: 1;
		HPT_U8  force_cc: 1;
	} flags;

	/* return status */
	HPT_U8  Result;
	/* retry count */
	HPT_U8  RetryCount;

	
	PSG psg;

	
	int  (*buildsgl)(struct _COMMAND *cmd, PSG psg, int logical);
	void (*done)(struct _COMMAND *cmd);
}
COMMAND, *PCOMMAND;

/* command types */
#define   CMD_TYPE_IO           0
#define   CMD_TYPE_CONTROL      1
#define   CMD_TYPE_ATAPI        2
#define   CMD_TYPE_SCSI         CMD_TYPE_ATAPI
#define   CMD_TYPE_PASSTHROUGH  3
#define   CMD_TYPE_FLUSH                4

/* flush command flags */
#define   CF_HARD_FLUSH_CACHE   1
#define   CF_HARD_FLUSH_STANDBY 2

/* command return values */
#define   RETURN_PENDING             0
#define   RETURN_SUCCESS             1
#define   RETURN_BAD_DEVICE          2
#define   RETURN_BAD_PARAMETER       3
#define   RETURN_WRITE_NO_DRQ        4
#define   RETURN_DEVICE_BUSY         5
#define   RETURN_INVALID_REQUEST     6
#define   RETURN_SELECTION_TIMEOUT   7
#define   RETURN_IDE_ERROR           8
#define   RETURN_NEED_LOGICAL_SG     9
#define   RETURN_NEED_PHYSICAL_SG    10
#define   RETURN_RETRY               11
#define   RETURN_DATA_ERROR          12
#define   RETURN_BUS_RESET           13
#define   RETURN_BAD_TRANSFER_LENGTH 14
#define   RETURN_INSUFFICIENT_MEMORY 15 
#define   RETURN_SECTOR_ERROR        16

#if defined(__cplusplus)
}
#endif
#endif
