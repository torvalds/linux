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

#ifndef HPT_INTF_H
#define HPT_INTF_H
#pragma pack(1)

/*
 * Version of this interface.
 * The user mode application must first issue a hpt_get_version() call to
 * check HPT_INTERFACE_VERSION. When an utility using newer version interface
 * is used with old version drivers, it must call only the functions that
 * driver supported.
 * A new version interface should only add ioctl functions; it should implement
 * all old version functions without change their definition.
 */
#define __this_HPT_INTERFACE_VERSION 0x01010000

#ifndef HPT_INTERFACE_VERSION
#error "You must define HPT_INTERFACE_VERSION you implemented"
#endif

#if HPT_INTERFACE_VERSION > __this_HPT_INTERFACE_VERSION
#error "HPT_INTERFACE_VERSION is invalid"
#endif

/*
 * DEFINITION
 *   Logical device  --- a device that can be accessed by OS.
 *   Physical device --- device attached to the controller.
 *  A logical device can be simply a physical device.
 *
 * Each logical and physical device has a 32bit ID. GUI will use this ID
 * to identify devices.
 *   1. The ID must be unique.
 *   2. The ID must be immutable. Once an ID is assigned to a device, it
 * must not change when system is running and the device exists.
 *   3. The ID of logical device must be NOT reusable. If a device is
 * removed, other newly created logical device must not use the same ID.
 *   4. The ID must not be zero or 0xFFFFFFFF.
 */
typedef DWORD DEVICEID;

/*
 * logical device type.
 * Identify array (logical device) and physical device.
 */
#define LDT_ARRAY   1
#define LDT_DEVICE  2

/*
 * Array types
 * GUI will treat all array as 1-level RAID. No RAID0/1 or RAID1/0.
 * A RAID0/1 device is type AT_RAID1. A RAID1/0 device is type AT_RAID0.
 * Their members may be another array of type RAID0 or RAID1.
 */
#define AT_UNKNOWN  0
#define AT_RAID0    1
#define AT_RAID1    2
#define AT_RAID5    3
#define AT_JBOD     7

/*
 * physical device type
 */
#define PDT_UNKNOWN     0
#define PDT_HARDDISK    1
#define PDT_CDROM       2
#define PDT_TAPE        3

/*
 * Some constants.
 */
#define MAX_NAME_LENGTH     36
#define MAX_ARRAYNAME_LEN   16

#define MAX_ARRAY_MEMBERS_V1 8
#define MAX_ARRAY_MEMBERS_V2 16
/* keep definition for source code compatibility */
#define MAX_ARRAY_MEMBERS MAX_ARRAY_MEMBERS_V1

/*
 * io commands
 * GUI use these commands to do IO on logical/physical devices.
 */
#define IO_COMMAND_READ     1
#define IO_COMMAND_WRITE    2

/*
 * array flags
 */
#define ARRAY_FLAG_DISABLED         0x00000001 /* The array is disabled */
#define ARRAY_FLAG_NEEDBUILDING     0x00000002 /* array need synchronizing */
#define ARRAY_FLAG_REBUILDING       0x00000004 /* array is in rebuilding process */
#define ARRAY_FLAG_BROKEN           0x00000008 /* broken but may still working */
#define ARRAY_FLAG_BOOTDISK         0x00000010 /* array has a active partition */
#define ARRAY_FLAG_NEWLY_CREATED    0x00000020 /* a newly created array */
#define ARRAY_FLAG_BOOTMARK         0x00000040 /* array has boot mark set */
#define ARRAY_FLAG_NEED_AUTOREBUILD 0x00000080 /* auto-rebuild should start */
#define ARRAY_FLAG_VERIFYING        0x00000100 /* is being verified */
#define ARRAY_FLAG_INITIALIZING     0x00000200 /* is being initialized */
#define ARRAY_FLAG_RAID15PLUS       0x80000000 /* display this RAID 1 as RAID 1.5 */

/*
 * device flags
 */
#define DEVICE_FLAG_DISABLED        0x00000001 /* device is disabled */
#define DEVICE_FLAG_BOOTDISK        0x00000002 /* disk has a active partition */
#define DEVICE_FLAG_BOOTMARK        0x00000004 /* disk has boot mark set */
#define DEVICE_FLAG_WITH_601        0x00000008 /* has HPT601 connected */
#define DEVICE_FLAG_SATA            0x00000010 /* S-ATA device */
#define DEVICE_FLAG_IS_SPARE        0x80000000 /* is a spare disk */

/*
 * array states used by hpt_set_array_state()
 */
/* old defines */
#define MIRROR_REBUILD_START    1
#define MIRROR_REBUILD_ABORT    2
#define MIRROR_REBUILD_COMPLETE 3
/* new defines */
#define AS_REBUILD_START 1
#define AS_REBUILD_ABORT 2
#define AS_REBUILD_PAUSE AS_REBUILD_ABORT
#define AS_REBUILD_COMPLETE 3
#define AS_VERIFY_START 4
#define AS_VERIFY_ABORT 5
#define AS_VERIFY_COMPLETE 6
#define AS_INITIALIZE_START 7
#define AS_INITIALIZE_ABORT 8
#define AS_INITIALIZE_COMPLETE 9
#define AS_VERIFY_FAILED 10
#define AS_REBUILD_STOP 11
#define AS_SAVE_STATE	12
/************************************************************************
 * ioctl code
 * It would be better if ioctl code are the same on different platforms,
 * but we must not conflict with system defined ioctl code.
 ************************************************************************/
#if defined(LINUX) || defined(__FreeBSD_version)
#define HPT_CTL_CODE(x) (x+0xFF00)
#elif defined(_MS_WIN32_) || defined(WIN32)

#ifndef CTL_CODE
#define CTL_CODE( DeviceType, Function, Method, Access ) \
			(((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif
#define HPT_CTL_CODE(x) CTL_CODE(0x370, 0x900+(x), 0, 0)
#define HPT_CTL_CODE_WIN32_TO_I960(x) ((((x) & 0xffff)>>2)-0x900)

#else 
#define HPT_CTL_CODE(x) (x)
#endif

#define HPT_IOCTL_GET_VERSION               HPT_CTL_CODE(0)
#define HPT_IOCTL_GET_CONTROLLER_COUNT      HPT_CTL_CODE(1)
#define HPT_IOCTL_GET_CONTROLLER_INFO       HPT_CTL_CODE(2)
#define HPT_IOCTL_GET_CHANNEL_INFO          HPT_CTL_CODE(3)
#define HPT_IOCTL_GET_LOGICAL_DEVICES       HPT_CTL_CODE(4)
#define HPT_IOCTL_GET_DEVICE_INFO           HPT_CTL_CODE(5)
#define HPT_IOCTL_CREATE_ARRAY              HPT_CTL_CODE(6)
#define HPT_IOCTL_DELETE_ARRAY              HPT_CTL_CODE(7)
#define HPT_IOCTL_ARRAY_IO                  HPT_CTL_CODE(8)
#define HPT_IOCTL_DEVICE_IO                 HPT_CTL_CODE(9)
#define HPT_IOCTL_GET_EVENT                 HPT_CTL_CODE(10)
#define HPT_IOCTL_REBUILD_MIRROR            HPT_CTL_CODE(11)
/* use HPT_IOCTL_REBUILD_DATA_BLOCK from now on */
#define HPT_IOCTL_REBUILD_DATA_BLOCK HPT_IOCTL_REBUILD_MIRROR
#define HPT_IOCTL_ADD_SPARE_DISK            HPT_CTL_CODE(12)
#define HPT_IOCTL_REMOVE_SPARE_DISK         HPT_CTL_CODE(13)
#define HPT_IOCTL_ADD_DISK_TO_ARRAY         HPT_CTL_CODE(14)
#define HPT_IOCTL_SET_ARRAY_STATE           HPT_CTL_CODE(15)
#define HPT_IOCTL_SET_ARRAY_INFO            HPT_CTL_CODE(16)
#define HPT_IOCTL_SET_DEVICE_INFO           HPT_CTL_CODE(17)
#define HPT_IOCTL_RESCAN_DEVICES            HPT_CTL_CODE(18)
#define HPT_IOCTL_GET_DRIVER_CAPABILITIES   HPT_CTL_CODE(19)
#define HPT_IOCTL_GET_601_INFO              HPT_CTL_CODE(20)
#define HPT_IOCTL_SET_601_INFO              HPT_CTL_CODE(21)
#define HPT_IOCTL_LOCK_DEVICE               HPT_CTL_CODE(22)
#define HPT_IOCTL_UNLOCK_DEVICE             HPT_CTL_CODE(23)
#define HPT_IOCTL_IDE_PASS_THROUGH          HPT_CTL_CODE(24)
#define HPT_IOCTL_VERIFY_DATA_BLOCK         HPT_CTL_CODE(25)
#define HPT_IOCTL_INITIALIZE_DATA_BLOCK     HPT_CTL_CODE(26)
#define HPT_IOCTL_ADD_DEDICATED_SPARE       HPT_CTL_CODE(27)
#define HPT_IOCTL_DEVICE_IO_EX              HPT_CTL_CODE(28)
#define HPT_IOCTL_SET_BOOT_MARK             HPT_CTL_CODE(29)
#define HPT_IOCTL_QUERY_REMOVE              HPT_CTL_CODE(30)
#define HPT_IOCTL_REMOVE_DEVICES            HPT_CTL_CODE(31)
#define HPT_IOCTL_CREATE_ARRAY_V2           HPT_CTL_CODE(32)
#define HPT_IOCTL_GET_DEVICE_INFO_V2        HPT_CTL_CODE(33)
#define HPT_IOCTL_SET_DEVICE_INFO_V2        HPT_CTL_CODE(34)
#define HPT_IOCTL_REBUILD_DATA_BLOCK_V2     HPT_CTL_CODE(35)
#define HPT_IOCTL_VERIFY_DATA_BLOCK_V2      HPT_CTL_CODE(36)
#define HPT_IOCTL_INITIALIZE_DATA_BLOCK_V2  HPT_CTL_CODE(37)
#define HPT_IOCTL_LOCK_DEVICE_V2            HPT_CTL_CODE(38)
#define HPT_IOCTL_DEVICE_IO_V2              HPT_CTL_CODE(39)
#define HPT_IOCTL_DEVICE_IO_EX_V2           HPT_CTL_CODE(40)

#define HPT_IOCTL_I2C_TRANSACTION           HPT_CTL_CODE(48)
#define HPT_IOCTL_GET_PARAMETER_LIST        HPT_CTL_CODE(49)
#define HPT_IOCTL_GET_PARAMETER             HPT_CTL_CODE(50)
#define HPT_IOCTL_SET_PARAMETER             HPT_CTL_CODE(51)

/* Windows only */
#define HPT_IOCTL_GET_CONTROLLER_IDS        HPT_CTL_CODE(100)
#define HPT_IOCTL_GET_DCB                   HPT_CTL_CODE(101)
#define	HPT_IOCTL_EPROM_IO                  HPT_CTL_CODE(102)
#define	HPT_IOCTL_GET_CONTROLLER_VENID      HPT_CTL_CODE(103)

/************************************************************************
 * shared data structures
 ************************************************************************/

/*
 * Chip Type
 */
#define CHIP_TYPE_HPT366      1
#define CHIP_TYPE_HPT368      2
#define CHIP_TYPE_HPT370      3
#define CHIP_TYPE_HPT370A     4
#define CHIP_TYPE_HPT370B     5
#define CHIP_TYPE_HPT374      6
#define CHIP_TYPE_HPT372      7
#define CHIP_TYPE_HPT372A     8
#define CHIP_TYPE_HPT302      9
#define CHIP_TYPE_HPT371      10
#define CHIP_TYPE_HPT372N     11
#define CHIP_TYPE_HPT302N     12
#define CHIP_TYPE_HPT371N     13
#define CHIP_TYPE_SI3112A     14
#define CHIP_TYPE_ICH5        15
#define CHIP_TYPE_ICH5R       16

/*
 * Chip Flags
 */
#define CHIP_SUPPORT_ULTRA_66   0x20
#define CHIP_SUPPORT_ULTRA_100  0x40
#define CHIP_HPT3XX_DPLL_MODE   0x80
#define CHIP_SUPPORT_ULTRA_133  0x01
#define CHIP_SUPPORT_ULTRA_150  0x02

typedef struct _DRIVER_CAPABILITIES {
	DWORD dwSize;

	UCHAR MaximumControllers;           /* maximum controllers the driver can support */
	UCHAR SupportCrossControllerRAID;   /* 1-support, 0-not support */
	UCHAR MinimumBlockSizeShift;        /* minimum block size shift */
	UCHAR MaximumBlockSizeShift;        /* maximum block size shift */

	UCHAR SupportDiskModeSetting;
	UCHAR SupportSparePool;
	UCHAR MaximumArrayNameLength;
	/* only one byte left here! */
#ifdef __BIG_ENDIAN_BITFIELD
	UCHAR reserved: 4;
	UCHAR SupportHotSwap: 1;
	UCHAR HighPerformanceRAID1: 1;
	UCHAR RebuildProcessInDriver: 1;
	UCHAR SupportDedicatedSpare: 1;
#else 
	UCHAR SupportDedicatedSpare: 1; /* call hpt_add_dedicated_spare() for dedicated spare. */
	UCHAR RebuildProcessInDriver: 1; /* Windows only. used by mid layer for rebuild control. */
	UCHAR HighPerformanceRAID1: 1; /* Support RAID1.5 */
	UCHAR SupportHotSwap: 1;
	UCHAR reserved: 4;
#endif

	/* SupportedRAIDTypes is an array of bytes, one of each is an array type.
	 * Only non-zero values is valid. Bit0-3 represents the lower(child) level RAID type;
	 * bit4-7 represents the top level. i.e.
	 *     RAID 0/1 is (AT_RAID1<<4) | AT_RAID0
	 *     RAID 5/0 is (AT_RAID0<<4) | AT_RAID5
	 */
	UCHAR SupportedRAIDTypes[16];
	/* maximum members in an array corresponding to SupportedRAIDTypes */
	UCHAR MaximumArrayMembers[16];
}
DRIVER_CAPABILITIES, *PDRIVER_CAPABILITIES;

/*
 * Controller information.
 */
typedef struct _CONTROLLER_INFO {
	UCHAR ChipType;                     /* chip type */
	UCHAR InterruptLevel;               /* IRQ level */
	UCHAR NumBuses;                     /* bus count */
	UCHAR ChipFlags;

	UCHAR szProductID[MAX_NAME_LENGTH]; /* product name */
	UCHAR szVendorID[MAX_NAME_LENGTH];  /* vender name */

} CONTROLLER_INFO, *PCONTROLLER_INFO;

/*
 * Channel information.
 */
typedef struct _CHANNEL_INFO {
	ULONG       IoPort;         /* IDE Base Port Address */
	ULONG       ControlPort;    /* IDE Control Port Address */

	DEVICEID    Devices[2];     /* device connected to this channel */

} CHANNEL_INFO, *PCHANNEL_INFO;

/*
 * time represented in DWORD format
 */
#ifndef __KERNEL__
typedef struct _TIME_RECORD {
   UINT        seconds:6;      /* 0 - 59 */
   UINT        minutes:6;      /* 0 - 59 */
   UINT        month:4;        /* 1 - 12 */
   UINT        hours:6;        /* 0 - 59 */
   UINT        day:5;          /* 1 - 31 */
   UINT        year:5;         /* 0=2000, 31=2031 */
} TIME_RECORD;
#endif

/*
 * Array information.
 */
typedef struct _HPT_ARRAY_INFO {
	UCHAR       Name[MAX_ARRAYNAME_LEN];/* array name */
	UCHAR       Description[64];        /* array description */
	UCHAR       CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	UCHAR       ArrayType;              /* array type */
	UCHAR       BlockSizeShift;         /* stripe size */
	UCHAR       nDisk;                  /* member count: Number of ID in Members[] */
	UCHAR       reserved;

	DWORD       Flags;                  /* working flags, see ARRAY_FLAG_XXX */
	DWORD       Members[MAX_ARRAY_MEMBERS_V1];  /* member array/disks */

	/*
	 * rebuilding progress, xx.xx% = sprintf(s, "%.2f%%", RebuildingProgress/100.0);
	 * only valid if rebuilding is done by driver code.
	 * Member Flags will have ARRAY_FLAG_REBUILDING set at this case.
	 * Verify operation use same fields below, the only difference is
	 * ARRAY_FLAG_VERIFYING is set.
	 */
	DWORD       RebuildingProgress;
	DWORD       RebuiltSectors; /* rebuilding point (LBA) for single member */

} HPT_ARRAY_INFO, *PHPT_ARRAY_INFO;  /*LDX modify ARRAY_INFO TO HPT_ARRAY_INFO to avoid compiling error in Windows*/

#if HPT_INTERFACE_VERSION>=0x01010000
typedef struct _LBA64 {
#ifdef __BIG_ENDIAN_BITFIELD
	DWORD hi32;
	DWORD lo32;
#else 
	DWORD lo32;
	DWORD hi32;
#endif
}
LBA64;
typedef struct _HPT_ARRAY_INFO_V2 {
	UCHAR       Name[MAX_ARRAYNAME_LEN];/* array name */
	UCHAR       Description[64];        /* array description */
	UCHAR       CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	UCHAR       ArrayType;              /* array type */
	UCHAR       BlockSizeShift;         /* stripe size */
	UCHAR       nDisk;                  /* member count: Number of ID in Members[] */
	UCHAR       reserved;

	DWORD       Flags;                  /* working flags, see ARRAY_FLAG_XXX */
	DWORD       Members[MAX_ARRAY_MEMBERS_V2];  /* member array/disks */

	DWORD       RebuildingProgress;
	LBA64       RebuiltSectors; /* rebuilding point (LBA) for single member */
	
	DWORD       reserve4[4];

} HPT_ARRAY_INFO_V2, *PHPT_ARRAY_INFO_V2;
#endif

/*
 * ATA/ATAPI Device identify data without the Reserved4.
 */
#ifndef __KERNEL__
typedef struct _IDENTIFY_DATA2 {
	USHORT GeneralConfiguration;            /* 00 00 */
	USHORT NumberOfCylinders;               /* 02  1 */
	USHORT Reserved1;                       /* 04  2 */
	USHORT NumberOfHeads;                   /* 06  3 */
	USHORT UnformattedBytesPerTrack;        /* 08  4 */
	USHORT UnformattedBytesPerSector;       /* 0A  5 */
	USHORT SectorsPerTrack;                 /* 0C  6 */
	USHORT VendorUnique1[3];                /* 0E  7-9 */
	USHORT SerialNumber[10];                /* 14  10-19 */
	USHORT BufferType;                      /* 28  20 */
	USHORT BufferSectorSize;                /* 2A  21 */
	USHORT NumberOfEccBytes;                /* 2C  22 */
	USHORT FirmwareRevision[4];             /* 2E  23-26 */
	USHORT ModelNumber[20];                 /* 36  27-46 */
	UCHAR  MaximumBlockTransfer;            /* 5E  47 */
	UCHAR  VendorUnique2;                   /* 5F */
	USHORT DoubleWordIo;                    /* 60  48 */
	USHORT Capabilities;                    /* 62  49 */
	USHORT Reserved2;                       /* 64  50 */
	UCHAR  VendorUnique3;                   /* 66  51 */
	UCHAR  PioCycleTimingMode;              /* 67 */
	UCHAR  VendorUnique4;                   /* 68  52 */
	UCHAR  DmaCycleTimingMode;              /* 69 */
	USHORT TranslationFieldsValid:1;        /* 6A  53 */
	USHORT Reserved3:15;
	USHORT NumberOfCurrentCylinders;        /* 6C  54 */
	USHORT NumberOfCurrentHeads;            /* 6E  55 */
	USHORT CurrentSectorsPerTrack;          /* 70  56 */
	ULONG  CurrentSectorCapacity;           /* 72  57-58 */
	USHORT CurrentMultiSectorSetting;       /*     59 */
	ULONG  UserAddressableSectors;          /*     60-61 */
	USHORT SingleWordDMASupport : 8;        /*     62 */
	USHORT SingleWordDMAActive : 8;
	USHORT MultiWordDMASupport : 8;         /*     63 */
	USHORT MultiWordDMAActive : 8;
	USHORT AdvancedPIOModes : 8;            /*     64 */
	USHORT Reserved4 : 8;
	USHORT MinimumMWXferCycleTime;          /*     65 */
	USHORT RecommendedMWXferCycleTime;      /*     66 */
	USHORT MinimumPIOCycleTime;             /*     67 */
	USHORT MinimumPIOCycleTimeIORDY;        /*     68 */
	USHORT Reserved5[2];                    /*     69-70 */
	USHORT ReleaseTimeOverlapped;           /*     71 */
	USHORT ReleaseTimeServiceCommand;       /*     72 */
	USHORT MajorRevision;                   /*     73 */
	USHORT MinorRevision;                   /*     74 */
/*    USHORT Reserved6[14];                   //     75-88 */
} IDENTIFY_DATA2, *PIDENTIFY_DATA2;
#endif

/*
 * physical device information.
 * IdentifyData.ModelNumber[] is byte-swapped from the original identify data.
 */
typedef struct _DEVICE_INFO {
	UCHAR   ControllerId;           /* controller id */
	UCHAR   PathId;                 /* bus */
	UCHAR   TargetId;               /* id */
	UCHAR   DeviceModeSetting;      /* Current Data Transfer mode: 0-4 PIO 0-4 */
									/* 5-7 MW DMA0-2, 8-13 UDMA0-5             */
	UCHAR   DeviceType;             /* device type */
	UCHAR   UsableMode;             /* highest usable mode */
	
	UCHAR   ReadAheadSupported: 1;
	UCHAR   ReadAheadEnabled: 1;
	UCHAR   WriteCacheSupported: 1;
	UCHAR   WriteCacheEnabled: 1;
	UCHAR   TCQSupported: 1;
	UCHAR   TCQEnabled: 1;
	UCHAR   NCQSupported: 1;
	UCHAR   NCQEnabled: 1;
	UCHAR   reserved;

	DWORD   Flags;                  /* working flags, see DEVICE_FLAG_XXX */

	IDENTIFY_DATA2 IdentifyData;    /* Identify Data of this device */

} DEVICE_INFO, *PDEVICE_INFO;

/*
 * HPT601 information
 */
#define HPT601_INFO_DEVICEID      1
#define HPT601_INFO_TEMPERATURE   2
#define HPT601_INFO_FANSTATUS     4
#define HPT601_INFO_BEEPERCONTROL 8
#define HPT601_INFO_LED1CONTROL   0x10
#define HPT601_INFO_LED2CONTROL   0x20
#define HPT601_INFO_POWERSTATUS   0x40

typedef struct _HPT601_INFO {
	WORD ValidFields;       /* mark valid fields below */
	WORD DeviceId;          /* 0x5A3E */
	WORD Temperature;       /* Read: temperature sensor value. Write: temperature limit */
	WORD FanStatus;         /* Fan status */
	WORD BeeperControl;     /* bit4: beeper control bit. bit0-3: frequency bits */
	WORD LED1Control;       /* bit4: twinkling control bit. bit0-3: frequency bits */
	WORD LED2Control;       /* bit4: twinkling control bit. bit0-3: frequency bits */
	WORD PowerStatus;       /* 1: has power 2: no power */
} HPT601_INFO, *PHPT601_INFO;

/*
 * Logical device information.
 * Union of ArrayInfo and DeviceInfo.
 * Common properties will be put in logical device information.
 */
typedef struct _LOGICAL_DEVICE_INFO {
	UCHAR       Type;                   /* LDT_ARRAY or LDT_DEVICE */
	UCHAR       reserved[3];

	DWORD       Capacity;               /* array capacity */
	DEVICEID    ParentArray;

	union {
		HPT_ARRAY_INFO array;
		DEVICE_INFO device;
	} u;

} LOGICAL_DEVICE_INFO, *PLOGICAL_DEVICE_INFO;

#if HPT_INTERFACE_VERSION>=0x01010000
typedef struct _LOGICAL_DEVICE_INFO_V2 {
	UCHAR       Type;                   /* LDT_ARRAY or LDT_DEVICE */
	UCHAR       reserved[3];

	LBA64       Capacity;           /* array capacity */
	DEVICEID    ParentArray;

	union {
		HPT_ARRAY_INFO_V2 array;
		DEVICE_INFO device;
	} u;

} LOGICAL_DEVICE_INFO_V2, *PLOGICAL_DEVICE_INFO_V2;
#endif

/*
 * ALTERABLE_ARRAY_INFO and ALTERABLE_DEVICE_INFO, used in set_array_info()
 * and set_device_info().
 * When set_xxx_info() is called, the ValidFields member indicates which
 * fields in the structure are valid.
 */
/* field masks */
#define AAIF_NAME           1
#define AAIF_DESCRIPTION    2
#define ADIF_MODE           1
#define ADIF_TCQ            2
#define ADIF_NCQ            4
#define ADIF_WRITE_CACHE    8
#define ADIF_READ_AHEAD     0x10

typedef struct _ALTERABLE_ARRAY_INFO {
	DWORD   ValidFields;                /* mark valid fields below */
	UCHAR   Name[MAX_ARRAYNAME_LEN];    /* array name */
	UCHAR   Description[64];            /* array description */
}
ALTERABLE_ARRAY_INFO, *PALTERABLE_ARRAY_INFO;

typedef struct _ALTERABLE_DEVICE_INFO {
	DWORD   ValidFields;                /* mark valid fields below */
	UCHAR   DeviceModeSetting;          /* 0-4 PIO 0-4, 5-7 MW DMA0-2, 8-13 UDMA0-5 */
}
ALTERABLE_DEVICE_INFO, *PALTERABLE_DEVICE_INFO;

typedef struct _ALTERABLE_DEVICE_INFO_V2 {
	DWORD   ValidFields;                /* mark valid fields below */
	UCHAR   DeviceModeSetting;          /* 0-4 PIO 0-4, 5-7 MW DMA0-2, 8-13 UDMA0-5 */
	UCHAR   TCQEnabled;
	UCHAR   NCQEnabled;
	UCHAR   WriteCacheEnabled;
	UCHAR   ReadAheadEnabled;
	UCHAR   reserve[3];
	ULONG   reserve2[13]; /* pad to 64 bytes */
}
ALTERABLE_DEVICE_INFO_V2, *PALTERABLE_DEVICE_INFO_V2;

/*
 * CREATE_ARRAY_PARAMS
 *  Param structure used to create an array.
 */
typedef struct _CREATE_ARRAY_PARAMS {
	UCHAR ArrayType;                    /* 1-level array type */
	UCHAR nDisk;                        /* number of elements in Members[] array */
	UCHAR BlockSizeShift;               /* Stripe size if ArrayType==AT_RAID0 / AT_RAID5 */
	UCHAR CreateFlags;                  /* See CAF_xxx */

	UCHAR ArrayName[MAX_ARRAYNAME_LEN]; /* Array name */
	UCHAR       Description[64];        /* array description */
	UCHAR       CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	DWORD Members[MAX_ARRAY_MEMBERS_V1];/* ID of array members, a member can be an array */

} CREATE_ARRAY_PARAMS, *PCREATE_ARRAY_PARAMS;

#if HPT_INTERFACE_VERSION>=0x01010000
typedef struct _CREATE_ARRAY_PARAMS_V2 {
	UCHAR ArrayType;                    /* 1-level array type */
	UCHAR nDisk;                        /* number of elements in Members[] array */
	UCHAR BlockSizeShift;               /* Stripe size if ArrayType==AT_RAID0 / AT_RAID5 */
	UCHAR CreateFlags;                  /* See CAF_xxx */

	UCHAR ArrayName[MAX_ARRAYNAME_LEN]; /* Array name */
	UCHAR       Description[64];        /* array description */
	UCHAR       CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */
	LBA64 Capacity;                     /* specify array capacity (0 for default) */

	DWORD Members[MAX_ARRAY_MEMBERS_V2];/* ID of array members, a member can be an array */

} CREATE_ARRAY_PARAMS_V2, *PCREATE_ARRAY_PARAMS_V2;
#endif

/*
 * Flags used for creating an RAID 1 array
 *
 * CAF_CREATE_AND_DUPLICATE
 *    Copy source disk contents to target for RAID 1. If user choose "create and duplicate"
 *    to create an array, GUI will call CreateArray() with this flag set. Then GUI should
 *    call hpt_get_device_info() with the returned array ID and check returned flags to
 *    see if ARRAY_FLAG_REBUILDING is set. If not set, driver does not support rebuilding
 *    and GUI must do duplication itself.
 * CAF_DUPLICATE_MUST_DONE
 *    If the duplication is aborted or fails, do not create the array.
 */
#define CAF_CREATE_AND_DUPLICATE 1
#define CAF_DUPLICATE_MUST_DONE  2
#define CAF_CREATE_AS_RAID15     4
/*
 * Flags used for creating an RAID 5 array
 */
#define CAF_CREATE_R5_NO_BUILD     1
#define CAF_CREATE_R5_ZERO_INIT    2
#define CAF_CREATE_R5_BUILD_PARITY 4

/*
 * Flags used for deleting an array
 *
 * DAF_KEEP_DATA_IF_POSSIBLE
 *    If this flag is set, deleting a RAID 1 array will not destroy the data on both disks.
 *    Deleting a JBOD should keep partitions on first disk ( not implement now ).
 *    Deleting a RAID 0/1 should result as two RAID 0 array ( not implement now ).
 */
#define DAF_KEEP_DATA_IF_POSSIBLE 1

/*
 * event types
 */
#define ET_DEVICE_REMOVED   1   /* device removed */
#define ET_DEVICE_PLUGGED   2   /* device plugged */
#define ET_DEVICE_ERROR     3   /* device I/O error */
#define ET_REBUILD_STARTED  4
#define ET_REBUILD_ABORTED  5
#define ET_REBUILD_FINISHED 6
#define ET_SPARE_TOOK_OVER  7
#define ET_REBUILD_FAILED   8
#define ET_VERIFY_STARTED   9
#define ET_VERIFY_ABORTED   10
#define ET_VERIFY_FAILED    11
#define ET_VERIFY_FINISHED  12
#define ET_INITIALIZE_STARTED   13
#define ET_INITIALIZE_ABORTED   14
#define ET_INITIALIZE_FAILED    15
#define ET_INITIALIZE_FINISHED  16
#define ET_VERIFY_DATA_ERROR    17

/*
 * event structure
 */
typedef struct _HPT_EVENT {
	TIME_RECORD Time;
	DEVICEID    DeviceID;
	UCHAR       EventType;
	UCHAR       reserved[3];

	UCHAR       Data[32]; /* various data depend on EventType */
} HPT_EVENT, *PHPT_EVENT;

/*
 * IDE pass-through command. Use it at your own risk!
 */
#ifdef _MSC_VER
#pragma warning(disable:4200)
#endif
typedef struct _IDE_PASS_THROUGH_HEADER {
	DEVICEID idDisk;           /* disk ID */
	BYTE     bFeaturesReg;     /* feature register */
	BYTE     bSectorCountReg;  /* IDE sector count register. */
	BYTE     bLbaLowReg; /* IDE sector number register. */
	BYTE     bLbaMidReg;       /* IDE low order cylinder value. */
	BYTE     bLbaHighReg;      /* IDE high order cylinder value. */
	BYTE     bDriveHeadReg;    /* IDE drive/head register. */
	BYTE     bCommandReg;      /* Actual IDE command. Checked for validity by driver. */
	BYTE     nSectors;		/* data sze in sectors, if the command has data transfer */
	BYTE     protocol;            /* IO_COMMAND_(READ,WRITE) or zero for non-DATA */
	BYTE     reserve[3];
#define IDE_PASS_THROUGH_buffer(p) ((unsigned char *)(p) + sizeof(IDE_PASS_THROUGH_HEADER))	
}
IDE_PASS_THROUGH_HEADER, *PIDE_PASS_THROUGH_HEADER;

/*
 * device io packet format
 */
typedef struct _DEVICE_IO_EX_PARAMS {
	DEVICEID idDisk;
	ULONG    Lba;
	USHORT   nSectors;
	UCHAR    Command; /* IO_COMMAD_xxx */
	UCHAR    BufferType; /* BUFFER_TYPE_xxx, see below */
	ULONG    BufferPtr;
}
DEVICE_IO_EX_PARAMS, *PDEVICE_IO_EX_PARAMS;

#define BUFFER_TYPE_LOGICAL              1 /* logical pointer to buffer */
#define BUFFER_TYPE_PHYSICAL             2 /* physical address of buffer */
#define BUFFER_TYPE_LOGICAL_LOGICAL_SG   3 /* logical pointer to logical S/G table */
#define BUFFER_TYPE_LOGICAL_PHYSICAL_SG  4 /* logical pointer to physical S/G table */
#define BUFFER_TYPE_PHYSICAL_LOGICAL_SG  5 /* physical address to logical S/G table */
#define BUFFER_TYPE_PHYSICAL_PHYSICAL_SG 6 /* physical address of physical S/G table */
#define BUFFER_TYPE_PHYSICAL_PHYSICAL_SG_PIO 7 /* non DMA capable physical address of physical S/G table */

/*
 * all ioctl functions should use far pointers. It's not a problem on
 * 32bit platforms, however, BIOS needs care.
 */

/*
 * ioctl structure
 */
#define HPT_IOCTL_MAGIC32 0x1A2B3C4D
#define HPT_IOCTL_MAGIC   0xA1B2C3D4

typedef struct _HPT_IOCTL_PARAM {
	DWORD   Magic;                 /* used to check if it's a valid ioctl packet */
	DWORD   dwIoControlCode;       /* operation control code */
	LPVOID  lpInBuffer;            /* input data buffer */
	DWORD   nInBufferSize;         /* size of input data buffer */
	LPVOID  lpOutBuffer;           /* output data buffer */
	DWORD   nOutBufferSize;        /* size of output data buffer */
	LPDWORD lpBytesReturned;       /* count of bytes returned */
}
HPT_IOCTL_PARAM, *PHPT_IOCTL_PARAM;

/* for 32-bit app running on 64-bit system */
typedef struct _HPT_IOCTL_PARAM32 {
	DWORD   Magic;
	DWORD   dwIoControlCode;
	DWORD   lpInBuffer;
	DWORD   nInBufferSize;
	DWORD   lpOutBuffer;
	DWORD   nOutBufferSize;
	DWORD   lpBytesReturned;
}
HPT_IOCTL_PARAM32, *PHPT_IOCTL_PARAM32;

/*
 * User-mode ioctl parameter passing conventions:
 *   The ioctl function implementation is platform specific, so we don't
 * have forced rules for it. However, it's suggested to use a parameter
 * passing method as below
 *   1) Put all input data continuously in an input buffer.
 *   2) Prepare an output buffer with enough size if needed.
 *   3) Fill a HPT_IOCTL_PARAM structure.
 *   4) Pass the structure to driver through a platform-specific method.
 * This is implemented in the mid-layer user-mode library. The UI
 * programmer needn't care about it.
 */

/************************************************************************
 * User mode functions
 ************************************************************************/
#ifndef __KERNEL__
/*
 * hpt_get_version
 * Version compatibility: all versions
 * Parameters:
 *  None
 * Returns:
 *  interface version. 0 when fail.
 */
DWORD hpt_get_version();

/*-------------------------------------------------------------------------- */

/*
 * hpt_get_driver_capabilities
 * Version compatibility: v1.0.0.2 or later
 * Parameters:
 *  Pointer to receive a DRIVE_CAPABILITIES structure. The caller must set
 *  dwSize member to sizeof(DRIVER_CAPABILITIES). The callee must check this
 *  member to see if it's correct.
 * Returns:
 *  0 - Success
 */
int hpt_get_driver_capabilities(PDRIVER_CAPABILITIES cap);

/*-------------------------------------------------------------------------- */

/*
 * hpt_get_controller_count
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  None
 * Returns:
 *  number of controllers
 */
int hpt_get_controller_count();

/*-------------------------------------------------------------------------- */

/* hpt_get_controller_info
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      Controller id
 *  pInfo   pointer to CONTROLLER_INFO buffer
 * Returns:
 *  0       Success, controller info is put into (*pInfo ).
 */
int hpt_get_controller_info(int id, PCONTROLLER_INFO pInfo);

/*-------------------------------------------------------------------------- */

/* hpt_get_channel_info
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      Controller id
 *  bus     bus number
 *  pInfo   pointer to CHANNEL_INFO buffer
 * Returns:
 *  0       Success, channel info is put into (*pInfo ).
 */
int hpt_get_channel_info(int id, int bus, PCHANNEL_INFO pInfo);

/*-------------------------------------------------------------------------- */

/* hpt_get_logical_devices
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  pIds        pointer to a DEVICEID array
 *  nMaxCount   array size
 * Returns:
 *  Number of ID returned. All logical device IDs are put into pIds array.
 *  Note: A spare disk is not a logical device.
 */
int hpt_get_logical_devices(DEVICEID * pIds, int nMaxCount);

/*-------------------------------------------------------------------------- */

/* hpt_get_device_info
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      logical device id
 *  pInfo   pointer to HPT_ARRAY_INFO structure
 * Returns:
 *  0 - Success
 */
int hpt_get_device_info(DEVICEID id, PLOGICAL_DEVICE_INFO pInfo);

#if HPT_INTERFACE_VERSION>=0x01010000
int hpt_get_device_info_v2(DEVICEID id, PLOGICAL_DEVICE_INFO_V2 pInfo);
#endif

/*-------------------------------------------------------------------------- */

/* hpt_create_array
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  pParam      pointer to CREATE_ARRAY_PARAMS structure
 * Returns:
 *  0   failed
 *  else return array id
 */
DEVICEID hpt_create_array(PCREATE_ARRAY_PARAMS pParam);

#if HPT_INTERFACE_VERSION>=0x01010000
DEVICEID hpt_create_array_v2(PCREATE_ARRAY_PARAMS_V2 pParam);
#endif

/*-------------------------------------------------------------------------- */

/* hpt_delete_array
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      array id
 * Returns:
 *  0   Success
 */
int hpt_delete_array(DEVICEID id, DWORD options);

/*-------------------------------------------------------------------------- */

/* hpt_device_io
 *  Read/write data on array and physcal device.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      device id. If it's an array ID, IO will be performed on the array.
 *          If it's a physical device ID, IO will be performed on the device.
 *  cmd     IO_COMMAND_READ or IO_COMMAND_WRITE
 *  buffer  data buffer
 *  length  data size
 * Returns:
 *  0   Success
 */
int hpt_device_io(DEVICEID id, int cmd, ULONG lba, DWORD nSector, LPVOID buffer);

#if HPT_INTERFACE_VERSION >= 0x01010000
int hpt_device_io_v2(DEVICEID id, int cmd, LBA64 lba, DWORD nSector, LPVOID buffer);
#endif

/* hpt_add_disk_to_array
 *   Used to dynamicly add a disk to an RAID1, RAID0/1, RAID1/0 or RAID5 array.
 *   Auto-rebuild will start.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  idArray     array id
 *  idDisk      disk id
 * Returns:
 *  0   Success
 */
int hpt_add_disk_to_array(DEVICEID idArray, DEVICEID idDisk);
/*-------------------------------------------------------------------------- */

/* hpt_add_spare_disk
 * Version compatibility: v1.0.0.1 or later
 *   Add a disk to spare pool.
 * Parameters:
 *  idDisk      disk id
 * Returns:
 *  0   Success
 */
int hpt_add_spare_disk(DEVICEID idDisk);
/*-------------------------------------------------------------------------- */

/* hpt_add_dedicated_spare
 * Version compatibility: v1.0.0.3 or later
 *   Add a spare disk to an array
 * Parameters:
 *  idDisk      disk id
 *  idArray     array id
 * Returns:
 *  0   Success
 */
int hpt_add_dedicated_spare(DEVICEID idDisk, DEVICEID idArray);
/*-------------------------------------------------------------------------- */

/* hpt_remove_spare_disk
 *   remove a disk from spare pool.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  idDisk      disk id
 * Returns:
 *  0   Success
 */
int hpt_remove_spare_disk(DEVICEID idDisk);
/*-------------------------------------------------------------------------- */

/* hpt_get_event
 *   Used to poll events from driver.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   pEvent    pointer to HPT_EVENT structure
 * Returns:
 *  0   Success, event info is filled in *pEvent
 */
int hpt_get_event(PHPT_EVENT pEvent);
/*-------------------------------------------------------------------------- */

/* hpt_rebuild_data_block
 *   Used to copy data from source disk and mirror disk.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   idArray        Array ID (RAID1, 0/1 or RAID5)
 *   Lba            Start LBA for each array member
 *   nSector        Number of sectors for each array member (RAID 5 will ignore this parameter)
 *
 * Returns:
 *  0   Success, event info is filled in *pEvent
 */
int hpt_rebuild_data_block(DEVICEID idMirror, DWORD Lba, UCHAR nSector);
#define hpt_rebuild_mirror(p1, p2, p3) hpt_rebuild_data_block(p1, p2, p3)

#if HPT_INTERFACE_VERSION >= 0x01010000
int hpt_rebuild_data_block_v2(DEVICEID idArray, LBA64 Lba, USHORT nSector);
#endif
/*-------------------------------------------------------------------------- */

/* hpt_set_array_state
 *   set array state.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   idArray        Array ID
 *   state          See above 'array states' constants, possible values are:
 *     MIRROR_REBUILD_START
 *        Indicate that GUI wants to rebuild a mirror array
 *     MIRROR_REBUILD_ABORT
 *        GUI wants to abort rebuilding an array
 *     MIRROR_REBUILD_COMPLETE
 *        GUI finished to rebuild an array. If rebuild is done by driver this
 *        state has no use
 *
 * Returns:
 *  0   Success
 */
int hpt_set_array_state(DEVICEID idArray, DWORD state);
/*-------------------------------------------------------------------------- */

/* hpt_set_array_info
 *   set array info.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   idArray        Array ID
 *   pInfo          pointer to new info
 *
 * Returns:
 *  0   Success
 */
int hpt_set_array_info(DEVICEID idArray, PALTERABLE_ARRAY_INFO pInfo);
/*-------------------------------------------------------------------------- */

/* hpt_set_device_info
 *   set device info.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   idDisk         device ID
 *   pInfo          pointer to new info
 *
 * Returns:
 *  0   Success
 * Additional notes:
 *  If idDisk==0, call to this function will stop buzzer on the adapter
 *  (if supported by driver).
 */
int hpt_set_device_info(DEVICEID idDisk, PALTERABLE_DEVICE_INFO pInfo);

#if HPT_INTERFACE_VERSION >= 0x01000004
int hpt_set_device_info_v2(DEVICEID idDisk, PALTERABLE_DEVICE_INFO_V2 pInfo);
#endif

/*-------------------------------------------------------------------------- */

/* hpt_rescan_devices
 *   rescan devices
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   None
 * Returns:
 *   0  Success
 */
int hpt_rescan_devices();
/*-------------------------------------------------------------------------- */

/* hpt_get_601_info
 *   Get HPT601 status
 * Version compatibiilty: v1.0.0.3 or later
 * Parameters:
 *   idDisk - Disk handle
 *   PHPT601_INFO - pointer to HPT601 info buffer
 * Returns:
 *   0  Success
 */
int hpt_get_601_info(DEVICEID idDisk, PHPT601_INFO pInfo);
/*-------------------------------------------------------------------------- */

/* hpt_set_601_info
 *   HPT601 function control
 * Version compatibiilty: v1.0.0.3 or later
 * Parameters:
 *   idDisk - Disk handle
 *   PHPT601_INFO - pointer to HPT601 info buffer
 * Returns:
 *   0  Success
 */
int hpt_set_601_info(DEVICEID idDisk, PHPT601_INFO pInfo);
/*-------------------------------------------------------------------------- */

/* hpt_lock_device
 *   Lock a block on a device (prevent OS accessing it)
 * Version compatibiilty: v1.0.0.3 or later
 * Parameters:
 *   idDisk - Disk handle
 *   Lba - Start LBA
 *   nSectors - number of sectors
 * Returns:
 *   0  Success
 */
int hpt_lock_device(DEVICEID idDisk, ULONG Lba, UCHAR nSectors);

#if HPT_INTERFACE_VERSION >= 0x01010000
int hpt_lock_device_v2(DEVICEID idDisk, LBA64 Lba, USHORT nSectors);
#endif
/*-------------------------------------------------------------------------- */

/* hpt_lock_device
 *   Unlock a device
 * Version compatibiilty: v1.0.0.3 or later
 * Parameters:
 *   idDisk - Disk handle
 * Returns:
 *   0  Success
 */
int hpt_unlock_device(DEVICEID idDisk);
/*-------------------------------------------------------------------------- */

/* hpt_ide_pass_through
 *  directly access controller's command and control registers.
 *  Can only call it on physical devices.
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   p - IDE_PASS_THROUGH header pointer
 * Returns:
 *   0  Success
 */
int hpt_ide_pass_through(PIDE_PASS_THROUGH_HEADER p);
/*-------------------------------------------------------------------------- */

/* hpt_verify_data_block
 *   verify data block on RAID1 or RAID5.
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   idArray - Array ID
 *   Lba - block number (on each array member, not logical block!)
 *   nSectors - Sectors for each member (RAID 5 will ignore this parameter)
 * Returns:
 *   0  Success
 *   1  Data compare error
 *   2  I/O error
 */
int hpt_verify_data_block(DEVICEID idArray, ULONG Lba, UCHAR nSectors);

#if HPT_INTERFACE_VERSION >= 0x01010000
int hpt_verify_data_block_v2(DEVICEID idArray, LBA64 Lba, USHORT nSectors);
#endif
/*-------------------------------------------------------------------------- */

/* hpt_initialize_data_block
 *   initialize data block (fill with zero) on RAID5
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   idArray - Array ID
 *   Lba - block number (on each array member, not logical block!)
 *   nSectors - Sectors for each member (RAID 5 will ignore this parameter)
 * Returns:
 *   0  Success
 */
int hpt_initialize_data_block(DEVICEID idArray, ULONG Lba, UCHAR nSectors);

#if HPT_INTERFACE_VERSION >= 0x01010000
int hpt_initialize_data_block_v2(DEVICEID idArray, LBA64 Lba, USHORT nSectors);
#endif
/*-------------------------------------------------------------------------- */

/* hpt_device_io_ex
 *   extended device I/O function
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   idArray - Array ID
 *   Lba - block number (on each array member, not logical block!)
 *   nSectors - Sectors for each member
 *   buffer - I/O buffer or s/g address
 * Returns:
 *   0  Success
 */
int hpt_device_io_ex(PDEVICE_IO_EX_PARAMS param);
#if HPT_INTERFACE_VERSION >= 0x01010000
int hpt_device_io_ex_v2(void * param); /* NOT IMPLEMENTED */
#endif
/*-------------------------------------------------------------------------- */

/* hpt_set_boot_mark
 *   select boot device
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   id - logical device ID. If id is 0 the boot mark will be removed.
 * Returns:
 *   0  Success
 */
int hpt_set_boot_mark(DEVICEID id);
/*-------------------------------------------------------------------------- */

/* hpt_query_remove
 *  check if device can be removed safely
 * Version compatibility: v1.0.0.4 or later
 * Parameters:
 *  ndev - number of devices
 *  pIds - device ID list
 * Returns:
 *  0  - Success
 *  -1 - unknown error
 *  n  - the n-th device that can't be removed
 */
int hpt_query_remove(DWORD ndev, DEVICEID *pIds);
/*-------------------------------------------------------------------------- */

/* hpt_remove_devices
 *  remove a list of devices
 * Version compatibility: v1.0.0.4 or later
 * Parameters:
 *  ndev - number of devices
 *  pIds - device ID list
 * Returns:
 *  0  - Success
 *  -1 - unknown error
 *  n  - the n-th device that can't be removed
 */
int hpt_remove_devices(DWORD ndev, DEVICEID *pIds);
/*-------------------------------------------------------------------------- */

/* hpt_ide_pass_through
 *  directly access controller's command and control registers.
 *  Can only call it on physical devices.
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   p - IDE_PASS_THROUGH header pointer
 * Returns:
 *   0  Success
 */
int hpt_ide_pass_through(PIDE_PASS_THROUGH_HEADER p);
/*-------------------------------------------------------------------------- */

#endif

#pragma pack()
#endif
