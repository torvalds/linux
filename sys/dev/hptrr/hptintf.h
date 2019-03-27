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


#ifndef HPT_INTF_H
#define HPT_INTF_H

#if defined(__BIG_ENDIAN__)&&!defined(__BIG_ENDIAN_BITFIELD)
#define __BIG_ENDIAN_BITFIELD
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __GNUC__
#define __attribute__(x)
#endif

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
#define __this_HPT_INTERFACE_VERSION 0x02000001

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
typedef HPT_U32 DEVICEID;

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
#define AT_RAID6    4
#define AT_RAID3    5
#define AT_RAID4    6
#define AT_JBOD     7
#define AT_RAID1E   8

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

#ifndef MAX_ARRAY_MEMBERS_V2
#define MAX_ARRAY_MEMBERS_V2 16
#endif

#ifndef MAX_ARRAY_MEMBERS_V3
#define MAX_ARRAY_MEMBERS_V3 64
#endif

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
#define ARRAY_FLAG_NEEDBUILDING     0x00000002 /* array data need to be rebuilt */
#define ARRAY_FLAG_REBUILDING       0x00000004 /* array is in rebuilding process */
#define ARRAY_FLAG_BROKEN           0x00000008 /* broken but may still working */
#define ARRAY_FLAG_BOOTDISK         0x00000010 /* array has a active partition */

#define ARRAY_FLAG_BOOTMARK         0x00000040 /* array has boot mark set */
#define ARRAY_FLAG_NEED_AUTOREBUILD 0x00000080 /* auto-rebuild should start */
#define ARRAY_FLAG_VERIFYING        0x00000100 /* is being verified */
#define ARRAY_FLAG_INITIALIZING     0x00000200 /* is being initialized */
#define	ARRAY_FLAG_TRANSFORMING     0x00000400 /* transform in progress */
#define	ARRAY_FLAG_NEEDTRANSFORM    0x00000800 /* array need transform */
#define ARRAY_FLAG_NEEDINITIALIZING 0x00001000 /* the array's initialization hasn't finished*/
#define ARRAY_FLAG_BROKEN_REDUNDANT 0x00002000 /* broken but redundant (raid6) */
#define ARRAY_FLAG_RAID15PLUS       0x80000000 /* display this RAID 1 as RAID 1.5 */
/*
 * device flags
 */
#define DEVICE_FLAG_DISABLED        0x00000001 /* device is disabled */
#define DEVICE_FLAG_BOOTDISK        0x00000002 /* disk has a active partition */
#define DEVICE_FLAG_BOOTMARK        0x00000004 /* disk has boot mark set */
#define DEVICE_FLAG_WITH_601        0x00000008 /* has HPT601 connected */
#define DEVICE_FLAG_SATA            0x00000010 /* SATA or SAS device */
#define DEVICE_FLAG_ON_PM_PORT      0x00000020 /* PM port */
#define DEVICE_FLAG_SAS             0x00000040 /* SAS device */

#define DEVICE_FLAG_UNINITIALIZED   0x00010000 /* device is not initialized, can't be used to create array */
#define DEVICE_FLAG_LEGACY          0x00020000 /* single disk & mbr contains at least one partition */

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
#define AS_SAVE_STATE   12
#define AS_TRANSFORM_START 13
#define AS_TRANSFORM_ABORT 14

/************************************************************************
 * ioctl code
 * It would be better if ioctl code are the same on different platforms,
 * but we must not conflict with system defined ioctl code.
 ************************************************************************/
#if defined(LINUX) || defined(__FreeBSD_version) || defined(linux)
#define HPT_CTL_CODE(x) (x+0xFF00)
#define HPT_CTL_CODE_LINUX_TO_IOP(x) ((x)-0xff00)
#elif defined(_MS_WIN32_) || defined(WIN32)

#ifndef CTL_CODE
#define CTL_CODE( DeviceType, Function, Method, Access ) \
			(((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif
#define HPT_CTL_CODE(x) CTL_CODE(0x370, 0x900+(x), 0, 0)
#define HPT_CTL_CODE_WIN32_TO_IOP(x) ((((x) & 0xffff)>>2)-0x900)

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
#define HPT_IOCTL_CREATE_TRANSFORM          HPT_CTL_CODE(41)
#define HPT_IOCTL_STEP_TRANSFORM            HPT_CTL_CODE(42)
#define HPT_IOCTL_SET_VDEV_INFO             HPT_CTL_CODE(43)
#define HPT_IOCTL_CALC_MAX_CAPACITY         HPT_CTL_CODE(44)
#define HPT_IOCTL_INIT_DISKS                HPT_CTL_CODE(45)
#define HPT_IOCTL_GET_DEVICE_INFO_V3        HPT_CTL_CODE(46)
#define HPT_IOCTL_GET_CONTROLLER_INFO_V2    HPT_CTL_CODE(47)
#define HPT_IOCTL_I2C_TRANSACTION           HPT_CTL_CODE(48)
#define HPT_IOCTL_GET_PARAMETER_LIST        HPT_CTL_CODE(49)
#define HPT_IOCTL_GET_PARAMETER             HPT_CTL_CODE(50)
#define HPT_IOCTL_SET_PARAMETER             HPT_CTL_CODE(51)
#define HPT_IOCTL_GET_DRIVER_CAPABILITIES_V2 HPT_CTL_CODE(52)
#define HPT_IOCTL_GET_CHANNEL_INFO_V2       HPT_CTL_CODE(53)
#define HPT_IOCTL_GET_CONTROLLER_INFO_V3    HPT_CTL_CODE(54)
#define HPT_IOCTL_GET_DEVICE_INFO_V4        HPT_CTL_CODE(55)
#define HPT_IOCTL_CREATE_ARRAY_V3           HPT_CTL_CODE(56)
#define HPT_IOCTL_CREATE_TRANSFORM_V2       HPT_CTL_CODE(57)
#define HPT_IOCTL_CALC_MAX_CAPACITY_V2      HPT_CTL_CODE(58)
#define HPT_IOCTL_SCSI_PASSTHROUGH          HPT_CTL_CODE(59)


#define HPT_IOCTL_GET_CONTROLLER_IDS        HPT_CTL_CODE(100)
#define HPT_IOCTL_GET_DCB                   HPT_CTL_CODE(101)

#define HPT_IOCTL_EPROM_IO                  HPT_CTL_CODE(102)
#define HPT_IOCTL_GET_CONTROLLER_VENID      HPT_CTL_CODE(103)

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
#define CHIP_TYPE_MV50XX      20
#define CHIP_TYPE_MV60X1      21
#define CHIP_TYPE_MV60X2      22
#define CHIP_TYPE_MV70X2      23
#define CHIP_TYPE_MV5182      24
#define CHIP_TYPE_IOP331      31
#define CHIP_TYPE_IOP333      32
#define CHIP_TYPE_IOP341      33
#define CHIP_TYPE_IOP348      34

/*
 * Chip Flags
 */
#define CHIP_SUPPORT_ULTRA_66   0x20
#define CHIP_SUPPORT_ULTRA_100  0x40
#define CHIP_HPT3XX_DPLL_MODE   0x80
#define CHIP_SUPPORT_ULTRA_133  0x01
#define CHIP_SUPPORT_ULTRA_150  0x02
#define CHIP_MASTER             0x04
#define CHIP_SUPPORT_SATA_300   0x08

#define HPT_SPIN_UP_MODE_NOSUPPORT 0
#define HPT_SPIN_UP_MODE_FULL      1
#define HPT_SPIN_UP_MODE_STANDBY   2

typedef struct _DRIVER_CAPABILITIES {
	HPT_U32 dwSize;

	HPT_U8 MaximumControllers;           /* maximum controllers the driver can support */
	HPT_U8 SupportCrossControllerRAID;   /* 1-support, 0-not support */
	HPT_U8 MinimumBlockSizeShift;        /* minimum block size shift */
	HPT_U8 MaximumBlockSizeShift;        /* maximum block size shift */

	HPT_U8 SupportDiskModeSetting;
	HPT_U8 SupportSparePool;
	HPT_U8 MaximumArrayNameLength;
	/* only one HPT_U8 left here! */
#ifdef __BIG_ENDIAN_BITFIELD
	HPT_U8 reserved: 3;
	HPT_U8 SupportVariableSectorSize: 1;
	HPT_U8 SupportHotSwap: 1;
	HPT_U8 HighPerformanceRAID1: 1;
	HPT_U8 RebuildProcessInDriver: 1;
	HPT_U8 SupportDedicatedSpare: 1;
#else 
	HPT_U8 SupportDedicatedSpare: 1;     /* call hpt_add_dedicated_spare() for dedicated spare. */
	HPT_U8 RebuildProcessInDriver: 1;    /* Windows only. used by mid layer for rebuild control. */
	HPT_U8 HighPerformanceRAID1: 1;      
	HPT_U8 SupportHotSwap: 1;
	HPT_U8 SupportVariableSectorSize: 1;
	HPT_U8 reserved: 3;
#endif

	
	HPT_U8 SupportedRAIDTypes[16];
	/* maximum members in an array corresponding to SupportedRAIDTypes */
	HPT_U8 MaximumArrayMembers[16];
}
DRIVER_CAPABILITIES, *PDRIVER_CAPABILITIES;

typedef struct _DRIVER_CAPABILITIES_V2 {
	DRIVER_CAPABILITIES v1;
	HPT_U8 SupportedCachePolicies[16];
	HPT_U32 reserved[17];
}
DRIVER_CAPABILITIES_V2, *PDRIVER_CAPABILITIES_V2;

/*
 * Controller information.
 */
typedef struct _CONTROLLER_INFO {
	HPT_U8 ChipType;                    /* chip type */
	HPT_U8 InterruptLevel;              /* IRQ level */
	HPT_U8 NumBuses;                    /* bus count */
	HPT_U8 ChipFlags;

	HPT_U8 szProductID[MAX_NAME_LENGTH];/* product name */
	HPT_U8 szVendorID[MAX_NAME_LENGTH]; /* vender name */

} CONTROLLER_INFO, *PCONTROLLER_INFO;

#if HPT_INTERFACE_VERSION>=0x01020000
typedef struct _CONTROLLER_INFO_V2 {
	HPT_U8 ChipType;                    /* chip type */
	HPT_U8 InterruptLevel;              /* IRQ level */
	HPT_U8 NumBuses;                    /* bus count */
	HPT_U8 ChipFlags;

	HPT_U8 szProductID[MAX_NAME_LENGTH];/* product name */
	HPT_U8 szVendorID[MAX_NAME_LENGTH]; /* vender name */

	HPT_U32 GroupId;                    /* low 32bit of vbus pointer the controller belongs
										 * the master controller has CHIP_MASTER flag set*/
	HPT_U8  pci_tree;
	HPT_U8  pci_bus;
	HPT_U8  pci_device;
	HPT_U8  pci_function;

	HPT_U32 ExFlags;
} CONTROLLER_INFO_V2, *PCONTROLLER_INFO_V2;

 
#define CEXF_IOPModel            1
#define CEXF_SDRAMSize           2
#define CEXF_BatteryInstalled    4
#define CEXF_BatteryStatus       8
#define CEXF_BatteryVoltage      0x10
#define CEXF_BatteryBackupTime   0x20
#define CEXF_FirmwareVersion     0x40
#define CEXF_SerialNumber        0x80
#define CEXF_BatteryTemperature 0x100

typedef struct _CONTROLLER_INFO_V3 {
	HPT_U8 ChipType;
	HPT_U8 InterruptLevel;
	HPT_U8 NumBuses;
	HPT_U8 ChipFlags;
	HPT_U8 szProductID[MAX_NAME_LENGTH];
	HPT_U8 szVendorID[MAX_NAME_LENGTH];
	HPT_U32 GroupId;
	HPT_U8  pci_tree;
	HPT_U8  pci_bus;
	HPT_U8  pci_device;
	HPT_U8  pci_function;
	HPT_U32 ExFlags;
	HPT_U8  IOPModel[32];
	HPT_U32 SDRAMSize;
	HPT_U8  BatteryInstalled; 
	HPT_U8  BatteryStatus; 
	HPT_U16 BatteryVoltage; 
	HPT_U32 BatteryBackupTime; 
	HPT_U32 FirmwareVersion;
	HPT_U8  SerialNumber[32];
	HPT_U8  BatteryMBInstalled; 
	HPT_U8  BatteryTemperature; 
	HPT_U8  reserve[86];
}
CONTROLLER_INFO_V3, *PCONTROLLER_INFO_V3;
typedef char check_CONTROLLER_INFO_V3[sizeof(CONTROLLER_INFO_V3)==256? 1:-1];
#endif
/*
 * Channel information.
 */
typedef struct _CHANNEL_INFO {
	HPT_U32         IoPort;         /* IDE Base Port Address */
	HPT_U32         ControlPort;    /* IDE Control Port Address */

	DEVICEID    Devices[2];         /* device connected to this channel */

} CHANNEL_INFO, *PCHANNEL_INFO;

typedef struct _CHANNEL_INFO_V2 {
	HPT_U32         IoPort;         /* IDE Base Port Address */
	HPT_U32         ControlPort;    /* IDE Control Port Address */

	DEVICEID        Devices[2+13];    /* device connected to this channel, PMPort max=15 */
} CHANNEL_INFO_V2, *PCHANNEL_INFO_V2;

#ifndef __KERNEL__
/*
 * time represented in HPT_U32 format
 */
typedef struct _TIME_RECORD {
   HPT_U32        seconds:6;      /* 0 - 59 */
   HPT_U32        minutes:6;      /* 0 - 59 */
   HPT_U32        month:4;        /* 1 - 12 */
   HPT_U32        hours:6;        /* 0 - 59 */
   HPT_U32        day:5;          /* 1 - 31 */
   HPT_U32        year:5;         /* 0=2000, 31=2031 */
} TIME_RECORD;
#endif

/*
 * Array information.
 */
typedef struct _HPT_ARRAY_INFO {
	HPT_U8      Name[MAX_ARRAYNAME_LEN];/* array name */
	HPT_U8      Description[64];        /* array description */
	HPT_U8      CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	HPT_U8      ArrayType;              /* array type */
	HPT_U8      BlockSizeShift;         /* stripe size */
	HPT_U8      nDisk;                  /* member count: Number of ID in Members[] */
	HPT_U8      SubArrayType;

	HPT_U32     Flags;                  /* working flags, see ARRAY_FLAG_XXX */
	HPT_U32     Members[MAX_ARRAY_MEMBERS_V1];  /* member array/disks */

	/*
	 * rebuilding progress, xx.xx% = sprintf(s, "%.2f%%", RebuildingProgress/100.0);
	 * only valid if rebuilding is done by driver code.
	 * Member Flags will have ARRAY_FLAG_REBUILDING set at this case.
	 * Verify operation use same fields below, the only difference is
	 * ARRAY_FLAG_VERIFYING is set.
	 */
	HPT_U32     RebuildingProgress;
	HPT_U32     RebuiltSectors; /* rebuilding point (LBA) for single member */

} HPT_ARRAY_INFO, *PHPT_ARRAY_INFO;

#if HPT_INTERFACE_VERSION>=0x01010000
typedef struct _HPT_ARRAY_INFO_V2 {
	HPT_U8      Name[MAX_ARRAYNAME_LEN];/* array name */
	HPT_U8      Description[64];        /* array description */
	HPT_U8      CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	HPT_U8      ArrayType;              /* array type */
	HPT_U8      BlockSizeShift;         /* stripe size */
	HPT_U8      nDisk;                  /* member count: Number of ID in Members[] */
	HPT_U8      SubArrayType;

	HPT_U32     Flags;                  /* working flags, see ARRAY_FLAG_XXX */
	HPT_U32     Members[MAX_ARRAY_MEMBERS_V2];  /* member array/disks */

	HPT_U32     RebuildingProgress;
	HPT_U64     RebuiltSectors; /* rebuilding point (LBA) for single member */

	HPT_U32     reserve4[4];
} HPT_ARRAY_INFO_V2, *PHPT_ARRAY_INFO_V2;
#endif

#if HPT_INTERFACE_VERSION>=0x01020000
typedef struct _HPT_ARRAY_INFO_V3 {
	HPT_U8      Name[MAX_ARRAYNAME_LEN];/* array name */
	HPT_U8      Description[64];        /* array description */
	HPT_U8      CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	HPT_U8      ArrayType;              /* array type */
	HPT_U8      BlockSizeShift;         /* stripe size */
	HPT_U8      nDisk;                  /* member count: Number of ID in Members[] */
	HPT_U8      SubArrayType;

	HPT_U32     Flags;                  /* working flags, see ARRAY_FLAG_XXX */
	HPT_U32     Members[MAX_ARRAY_MEMBERS_V2];  /* member array/disks */

	HPT_U32     RebuildingProgress;
	HPT_U64     RebuiltSectors;         /* rebuilding point (LBA) for single member */

	DEVICEID    TransformSource;
	DEVICEID    TransformTarget;        /* destination device ID */
	HPT_U32     TransformingProgress;
	HPT_U32     Signature;              /* persistent identification*/
#if MAX_ARRAY_MEMBERS_V2==16
	HPT_U16     Critical_Members;       /* bit mask of critical members */
	HPT_U16     reserve2;
	HPT_U32     reserve;
#else 
	HPT_U32     Critical_Members;
	HPT_U32     reserve;
#endif
} HPT_ARRAY_INFO_V3, *PHPT_ARRAY_INFO_V3;
#endif

#if HPT_INTERFACE_VERSION>=0x02000001
typedef struct _HPT_ARRAY_INFO_V4 {
	HPT_U8      Name[MAX_ARRAYNAME_LEN];/* array name */
	HPT_U8      Description[64];        /* array description */
	HPT_U8      CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	HPT_U8      ArrayType;              /* array type */
	HPT_U8      BlockSizeShift;         /* stripe size */
	HPT_U8      nDisk;                  /* member count: Number of ID in Members[] */
	HPT_U8      SubArrayType;

	HPT_U32     Flags;                  /* working flags, see ARRAY_FLAG_XXX */
	
	HPT_U32     RebuildingProgress;
	HPT_U64     RebuiltSectors; /* rebuilding point (LBA) for single member */

	DEVICEID    TransformSource;
	DEVICEID    TransformTarget;   /* destination device ID */
	HPT_U32     TransformingProgress;
	HPT_U32     Signature;          /* persistent identification*/
	HPT_U8       SectorSizeShift; /*sector size = 512B<<SectorSizeShift*/
	HPT_U8       reserved2[7];
	HPT_U64     Critical_Members;
	HPT_U32     Members[MAX_ARRAY_MEMBERS_V3];  /* member array/disks */
} HPT_ARRAY_INFO_V4, *PHPT_ARRAY_INFO_V4;
#endif


#ifndef __KERNEL__
/*
 * ATA/ATAPI Device identify data without the Reserved4.
 */
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
} __attribute__((packed)) IDENTIFY_DATA2, *PIDENTIFY_DATA2;
#endif

/*
 * physical device information.
 * IdentifyData.ModelNumber[] is HPT_U8-swapped from the original identify data.
 */
typedef struct _DEVICE_INFO {
	HPT_U8   ControllerId;          /* controller id */
	HPT_U8   PathId;                /* bus */
	HPT_U8   TargetId;              /* id */
	HPT_U8   DeviceModeSetting;     /* Current Data Transfer mode: 0-4 PIO 0-4 */
									/* 5-7 MW DMA0-2, 8-13 UDMA0-5             */
	HPT_U8   DeviceType;            /* device type */
	HPT_U8   UsableMode;            /* highest usable mode */

#ifdef __BIG_ENDIAN_BITFIELD
	HPT_U8   NCQEnabled: 1;
	HPT_U8   NCQSupported: 1;
	HPT_U8   TCQEnabled: 1;
	HPT_U8   TCQSupported: 1;
	HPT_U8   WriteCacheEnabled: 1;
	HPT_U8   WriteCacheSupported: 1;
	HPT_U8   ReadAheadEnabled: 1;
	HPT_U8   ReadAheadSupported: 1;
	HPT_U8   reserved6: 6;
	HPT_U8   SpinUpMode: 2;
#else 
	HPT_U8   ReadAheadSupported: 1;
	HPT_U8   ReadAheadEnabled: 1;
	HPT_U8   WriteCacheSupported: 1;
	HPT_U8   WriteCacheEnabled: 1;
	HPT_U8   TCQSupported: 1;
	HPT_U8   TCQEnabled: 1;
	HPT_U8   NCQSupported: 1;
	HPT_U8   NCQEnabled: 1;
	HPT_U8   SpinUpMode: 2;
	HPT_U8   reserved6: 6;
#endif

	HPT_U32     Flags;              /* working flags, see DEVICE_FLAG_XXX */

	IDENTIFY_DATA2 IdentifyData;    /* Identify Data of this device */

}
__attribute__((packed)) DEVICE_INFO, *PDEVICE_INFO;

#if HPT_INTERFACE_VERSION>=0x01020000
#define MAX_PARENTS_PER_DISK    8
/*
 * physical device information.
 * IdentifyData.ModelNumber[] is HPT_U8-swapped from the original identify data.
 */
typedef struct _DEVICE_INFO_V2 {
	HPT_U8   ControllerId;          /* controller id */
	HPT_U8   PathId;                /* bus */
	HPT_U8   TargetId;              /* id */
	HPT_U8   DeviceModeSetting;     /* Current Data Transfer mode: 0-4 PIO 0-4 */
									/* 5-7 MW DMA0-2, 8-13 UDMA0-5             */
	HPT_U8   DeviceType;            /* device type */
	HPT_U8   UsableMode;            /* highest usable mode */

#ifdef __BIG_ENDIAN_BITFIELD
	HPT_U8   NCQEnabled: 1;
	HPT_U8   NCQSupported: 1;
	HPT_U8   TCQEnabled: 1;
	HPT_U8   TCQSupported: 1;
	HPT_U8   WriteCacheEnabled: 1;
	HPT_U8   WriteCacheSupported: 1;
	HPT_U8   ReadAheadEnabled: 1;
	HPT_U8   ReadAheadSupported: 1;
	HPT_U8   reserved6: 6;
	HPT_U8   SpinUpMode: 2;
#else 
	HPT_U8   ReadAheadSupported: 1;
	HPT_U8   ReadAheadEnabled: 1;
	HPT_U8   WriteCacheSupported: 1;
	HPT_U8   WriteCacheEnabled: 1;
	HPT_U8   TCQSupported: 1;
	HPT_U8   TCQEnabled: 1;
	HPT_U8   NCQSupported: 1;
	HPT_U8   NCQEnabled: 1;
	HPT_U8   SpinUpMode: 2;
	HPT_U8   reserved6: 6;
#endif

	HPT_U32     Flags;              /* working flags, see DEVICE_FLAG_XXX */

	IDENTIFY_DATA2 IdentifyData;    /* Identify Data of this device */

	HPT_U64 TotalFree;
	HPT_U64 MaxFree;
	HPT_U64 BadSectors;
	DEVICEID ParentArrays[MAX_PARENTS_PER_DISK];

}
__attribute__((packed)) DEVICE_INFO_V2, *PDEVICE_INFO_V2, DEVICE_INFO_V3, *PDEVICE_INFO_V3;

/*
 * HPT601 information
 */
#endif
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

typedef struct _HPT601_INFO_ {
	HPT_U16 ValidFields;        /* mark valid fields below */
	HPT_U16 DeviceId;           /* 0x5A3E */
	HPT_U16 Temperature;        /* Read: temperature sensor value. Write: temperature limit */
	HPT_U16 FanStatus;          /* Fan status */
	HPT_U16 BeeperControl;      /* bit4: beeper control bit. bit0-3: frequency bits */
	HPT_U16 LED1Control;        /* bit4: twinkling control bit. bit0-3: frequency bits */
	HPT_U16 LED2Control;        /* bit4: twinkling control bit. bit0-3: frequency bits */
	HPT_U16 PowerStatus;        /* 1: has power 2: no power */
} HPT601_INFO, *PHPT601_INFO;

#if HPT_INTERFACE_VERSION>=0x01010000
#ifndef __KERNEL__
/* cache policy for each vdev, copied from ldm.h */
#define CACHE_POLICY_NONE 0
#define CACHE_POLICY_WRITE_THROUGH 1
#define CACHE_POLICY_WRITE_BACK 2

#endif
#endif
/*
 * Logical device information.
 * Union of ArrayInfo and DeviceInfo.
 * Common properties will be put in logical device information.
 */
typedef struct _LOGICAL_DEVICE_INFO {
	HPT_U8      Type;                   /* LDT_ARRAY or LDT_DEVICE */
	HPT_U8      reserved[3];

	HPT_U32     Capacity;               /* array capacity */
	DEVICEID    ParentArray;

	union {
		HPT_ARRAY_INFO array;
		DEVICE_INFO device;
	} __attribute__((packed)) u;

} __attribute__((packed)) LOGICAL_DEVICE_INFO, *PLOGICAL_DEVICE_INFO;

#if HPT_INTERFACE_VERSION>=0x01010000
typedef struct _LOGICAL_DEVICE_INFO_V2 {
	HPT_U8      Type;                   /* LDT_ARRAY or LDT_DEVICE */
	HPT_U8      reserved[3];

	HPT_U64     Capacity;               /* array capacity */
	DEVICEID    ParentArray;            /* for physical device, Please don't use this field.
										 * use ParentArrays field in DEVICE_INFO_V2
										 */

	union {
		HPT_ARRAY_INFO_V2 array;
		DEVICE_INFO device;
	} __attribute__((packed)) u;

} __attribute__((packed)) LOGICAL_DEVICE_INFO_V2, *PLOGICAL_DEVICE_INFO_V2;
#endif

#if HPT_INTERFACE_VERSION>=0x01020000
#define INVALID_TARGET_ID   0xFF
#define INVALID_BUS_ID      0xFF
typedef struct _LOGICAL_DEVICE_INFO_V3 {
	HPT_U8      Type;                   /* LDT_ARRAY or LDT_DEVICE */
	HPT_U8      CachePolicy;            /* refer to CACHE_POLICY_xxx */
	HPT_U8      VBusId;                 /* vbus sequence in vbus_list */
	HPT_U8      TargetId;               /* OS target id. Value 0xFF is invalid */
										/* OS disk name: HPT DISK $VBusId_$TargetId */
	HPT_U64     Capacity;               /* array capacity */
	DEVICEID    ParentArray;            /* for physical device, don't use this field.
										 * use ParentArrays field in DEVICE_INFO_V2 instead.
										 */
	HPT_U32     TotalIOs;
	HPT_U32     TobalMBs;
	HPT_U32     IOPerSec;
	HPT_U32     MBPerSec;

	union {
		HPT_ARRAY_INFO_V3 array;
		DEVICE_INFO_V2 device;
	} __attribute__((packed)) u;

}
__attribute__((packed)) LOGICAL_DEVICE_INFO_V3, *PLOGICAL_DEVICE_INFO_V3;
#endif

#if HPT_INTERFACE_VERSION>=0x02000001
typedef struct _LOGICAL_DEVICE_INFO_V4 {
	HPT_U32    dwSize;
	HPT_U8      revision;
	HPT_U8      reserved[7];
	
	HPT_U8      Type;                   /* LDT_ARRAY or LDT_DEVICE */
	HPT_U8      CachePolicy;            /* refer to CACHE_POLICY_xxx */
	HPT_U8      VBusId;                 /* vbus sequence in vbus_list */
	HPT_U8      TargetId;               /* OS target id. Value 0xFF is invalid */
										/* OS disk name: HPT DISK $VBusId_$TargetId */
	HPT_U64     Capacity;               /* array capacity */
	DEVICEID    ParentArray;            /* for physical device, don't use this field.
										 * use ParentArrays field in DEVICE_INFO_V2 instead.
										 */
	HPT_U32     TotalIOs;
	HPT_U32     TobalMBs;
	HPT_U32     IOPerSec;
	HPT_U32     MBPerSec;

	union {
		HPT_ARRAY_INFO_V4 array;
		DEVICE_INFO_V3 device;
	} __attribute__((packed)) u;
}
__attribute__((packed)) LOGICAL_DEVICE_INFO_V4, *PLOGICAL_DEVICE_INFO_V4;

/*LOGICAL_DEVICE_INFO_V4 max revision number*/
#define LOGICAL_DEVICE_INFO_V4_REVISION 0
/*If new revision was defined please check evey revision size*/
#define LOGICAL_DEVICE_INFO_V4_R0_SIZE (sizeof(LOGICAL_DEVICE_INFO_V4))
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
#define ADIF_SPIN_UP_MODE   0x20

typedef struct _ALTERABLE_ARRAY_INFO {
	HPT_U32   ValidFields;              /* mark valid fields below */
	HPT_U8  Name[MAX_ARRAYNAME_LEN];    /* array name */
	HPT_U8  Description[64];            /* array description */
}__attribute__((packed))ALTERABLE_ARRAY_INFO, *PALTERABLE_ARRAY_INFO;

typedef struct _ALTERABLE_DEVICE_INFO {
	HPT_U32   ValidFields;              /* mark valid fields below */
	HPT_U8   DeviceModeSetting;         /* 0-4 PIO 0-4, 5-7 MW DMA0-2, 8-13 UDMA0-5 */
}__attribute__((packed))ALTERABLE_DEVICE_INFO, *PALTERABLE_DEVICE_INFO;

typedef struct _ALTERABLE_DEVICE_INFO_V2 {
	HPT_U32   ValidFields;              /* mark valid fields below */
	HPT_U8   DeviceModeSetting;         /* 0-4 PIO 0-4, 5-7 MW DMA0-2, 8-13 UDMA0-5 */
	HPT_U8   TCQEnabled;
	HPT_U8   NCQEnabled;
	HPT_U8   WriteCacheEnabled;
	HPT_U8   ReadAheadEnabled;
	HPT_U8   SpinUpMode;
	HPT_U8   reserve[2];
	HPT_U32  reserve2[13]; /* pad to 64 bytes */
}__attribute__((packed))ALTERABLE_DEVICE_INFO_V2, *PALTERABLE_DEVICE_INFO_V2;

#if HPT_INTERFACE_VERSION>=0x01020000

#define TARGET_TYPE_DEVICE  0
#define TARGET_TYPE_ARRAY   1


#define AIT_NAME            0
#define AIT_DESCRIPTION     1
#define AIT_CACHE_POLICY    2


#define DIT_MODE        0
#define DIT_READ_AHEAD  1
#define DIT_WRITE_CACHE 2
#define DIT_TCQ         3
#define DIT_NCQ         4

/* param type is determined by target_type and info_type*/
typedef struct _SET_DEV_INFO
{
	HPT_U8 target_type;
	HPT_U8 infor_type;
	HPT_U16 param_length;
	#define SET_VDEV_INFO_param(p) ((HPT_U8 *)(p)+sizeof(SET_VDEV_INFO))
	/* HPT_U8 param[0]; */
} SET_VDEV_INFO, * PSET_VDEV_INFO;

typedef HPT_U8 PARAM_ARRAY_NAME[MAX_ARRAYNAME_LEN] ;
typedef HPT_U8 PARAM_ARRAY_DES[64];
typedef HPT_U8 PARAM_DEVICE_MODE, PARAM_TCQ, PARAM_NCQ, PARAM_READ_AHEAD, PARAM_WRITE_CACHE, PARAM_CACHE_POLICY;

#endif

/*
 * CREATE_ARRAY_PARAMS
 *  Param structure used to create an array.
 */
typedef struct _CREATE_ARRAY_PARAMS {
	HPT_U8 ArrayType;                   /* 1-level array type */
	HPT_U8 nDisk;                       /* number of elements in Members[] array */
	HPT_U8 BlockSizeShift;              /* Stripe size if ArrayType==AT_RAID0 / AT_RAID5 */
	HPT_U8 CreateFlags;                 /* See CAF_xxx */

	HPT_U8 ArrayName[MAX_ARRAYNAME_LEN];/* Array name */
	HPT_U8      Description[64];        /* array description */
	HPT_U8      CreateManager[16];      /* who created it */
	TIME_RECORD CreateTime;             /* when created it */

	HPT_U32 Members[MAX_ARRAY_MEMBERS_V1];/* ID of array members, a member can be an array */

} CREATE_ARRAY_PARAMS, *PCREATE_ARRAY_PARAMS;

#if HPT_INTERFACE_VERSION>=0x01010000
typedef struct _CREATE_ARRAY_PARAMS_V2 {
	HPT_U8 ArrayType;                   /* 1-level array type */
	HPT_U8 nDisk;                       /* number of elements in Members[] array */
	HPT_U8 BlockSizeShift;              /* Stripe size if ArrayType==AT_RAID0 / AT_RAID5 */
	HPT_U8 CreateFlags;                 /* See CAF_xxx */

	HPT_U8 ArrayName[MAX_ARRAYNAME_LEN];/* Array name */
	HPT_U8 Description[64];             /* array description */
	HPT_U8 CreateManager[16];           /* who created it */
	TIME_RECORD CreateTime;             /* when created it */
	HPT_U64 Capacity;

	HPT_U32 Members[MAX_ARRAY_MEMBERS_V2];/* ID of array members, a member can be an array */

} CREATE_ARRAY_PARAMS_V2, *PCREATE_ARRAY_PARAMS_V2;
#endif

#if HPT_INTERFACE_VERSION>=0x02000001
typedef struct _CREATE_ARRAY_PARAMS_V3 {
	HPT_U32  dwSize;
	HPT_U8 revision;			/*CREATE_ARRAY_PARAMS_V3_REVISION*/
	HPT_U8 reserved[6];
	HPT_U8 SectorSizeShift;     /*sector size = 512B<<SectorSizeShift*/
	HPT_U8 ArrayType;                   /* 1-level array type */
	HPT_U8 nDisk;                       /* number of elements in Members[] array */
	HPT_U8 BlockSizeShift;              /* Stripe size if ArrayType==AT_RAID0 / AT_RAID5 */
	HPT_U8 CreateFlags;                 /* See CAF_xxx */

	HPT_U8 ArrayName[MAX_ARRAYNAME_LEN];/* Array name */
	HPT_U8 Description[64];     /* array description */
	HPT_U8 CreateManager[16];       /* who created it */
	TIME_RECORD CreateTime;             /* when created it */
	HPT_U64 Capacity;

	HPT_U32 Members[MAX_ARRAY_MEMBERS_V3];/* ID of array members, a member can be an array */
} CREATE_ARRAY_PARAMS_V3, *PCREATE_ARRAY_PARAMS_V3;

/*CREATE_ARRAY_PARAMS_V3 current max revision*/
#define CREATE_ARRAY_PARAMS_V3_REVISION 0
/*If new revision defined please check evey revision size*/
#define CREATE_ARRAY_PARAMS_V3_R0_SIZE (sizeof(CREATE_ARRAY_PARAMS_V3))
#endif

#if HPT_INTERFACE_VERSION < 0x01020000
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

#else 
/*
 * Flags used for creating
 */
#define CAF_FOREGROUND_INITIALIZE   1
#define CAF_BACKGROUND_INITIALIZE   2
#define CAF_CREATE_R5_WRITE_BACK    (CACHE_POLICY_WRITE_BACK<<CAF_CACHE_POLICY_SHIFT)


#define CAF_CACHE_POLICY_MASK       0x1C
#define CAF_CACHE_POLICY_SHIFT      2

#endif

#define CAF_KEEP_DATA_ALWAYS     0x80

/* Flags used for deleting an array
 *
 * DAF_KEEP_DATA_IF_POSSIBLE
 *    If this flag is set, deleting a RAID 1 array will not destroy the data on both disks.
 *    Deleting a JBOD should keep partitions on first disk ( not implement now ).
 *    Deleting a RAID 0/1 should result as two RAID 0 array ( not implement now ).
 */
#define DAF_KEEP_DATA_IF_POSSIBLE 1
#define DAF_KEEP_DATA_ALWAYS      2

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
#define ET_TRANSFORM_STARTED    18
#define ET_TRANSFORM_ABORTED    19
#define ET_TRANSFORM_FAILED     20
#define ET_TRANSFORM_FINISHED   21
#define ET_SMART_FAILED         22
#define ET_SMART_PASSED         23
#define ET_SECTOR_REPAIR_FAIL     24
#define ET_SECTOR_REPAIR_SUCCESS  25
#define ET_ERASE_FAIL		26
#define ET_ERASE_SUCCESS	27
#define ET_CONTINUE_REBUILD_ON_ERROR 28


/*
 * event structure
 */
typedef struct _HPT_EVENT {
	TIME_RECORD Time;
	DEVICEID    DeviceID;
	HPT_U8       EventType;
	HPT_U8      reserved[3];

	HPT_U8      Data[32]; /* various data depend on EventType */
} HPT_EVENT, *PHPT_EVENT;

/*
 * IDE pass-through command. Use it at your own risk!
 */
#ifdef _MSC_VER
#pragma warning(disable:4200)
#endif
typedef struct _IDE_PASS_THROUGH_HEADER {
	DEVICEID idDisk;             /* disk ID */
	HPT_U8     bFeaturesReg;     /* feature register */
	HPT_U8     bSectorCountReg;  /* IDE sector count register. */
	HPT_U8     bLbaLowReg;       /* IDE LBA low value. */
	HPT_U8     bLbaMidReg;       /* IDE LBA mid register. */
	HPT_U8     bLbaHighReg;      /* IDE LBA high value. */
	HPT_U8     bDriveHeadReg;    /* IDE drive/head register. */
	HPT_U8     bCommandReg;      /* Actual IDE command. Checked for validity by driver. */
	HPT_U8     nSectors;         /* data size in sectors, if the command has data transfer */
	HPT_U8     protocol;         /* IO_COMMAND_(READ,WRITE) or zero for non-DATA */
	HPT_U8     reserve[3];
	#define IDE_PASS_THROUGH_buffer(p) ((HPT_U8 *)(p) + sizeof(IDE_PASS_THROUGH_HEADER))
	/* HPT_U8     DataBuffer[0]; */
}
IDE_PASS_THROUGH_HEADER, *PIDE_PASS_THROUGH_HEADER;

typedef struct _HPT_SCSI_PASSTHROUGH_IN {
	DEVICEID idDisk;
	HPT_U8   protocol;
	HPT_U8   reserve1;
	HPT_U8   reserve2;
	HPT_U8   cdbLength;
	HPT_U8   cdb[16];
	HPT_U32  dataLength;
	/* data follows, if any */
}
HPT_SCSI_PASSTHROUGH_IN, *PHPT_SCSI_PASSTHROUGH_IN;

typedef struct _HPT_SCSI_PASSTHROUGH_OUT {
	HPT_U8   scsiStatus;
	HPT_U8   reserve1;
	HPT_U8   reserve2;
	HPT_U8   reserve3;
	HPT_U32  dataLength;
	/* data/sense follows if any */
}
HPT_SCSI_PASSTHROUGH_OUT, *PHPT_SCSI_PASSTHROUGH_OUT;

/*
 * device io packet format
 */
typedef struct _DEVICE_IO_EX_PARAMS {
	DEVICEID idDisk;
	HPT_U32    Lba;
	HPT_U16   nSectors;
	HPT_U8    Command;    /* IO_COMMAD_xxx */
	HPT_U8    BufferType; /* BUFFER_TYPE_xxx, see below */
	HPT_U32    BufferPtr;
}
DEVICE_IO_EX_PARAMS, *PDEVICE_IO_EX_PARAMS;

#define BUFFER_TYPE_LOGICAL              1 /* logical pointer to buffer */
#define BUFFER_TYPE_PHYSICAL             2 /* physical address of buffer */
#define BUFFER_TYPE_LOGICAL_LOGICAL_SG   3 /* logical pointer to logical S/G table */
#define BUFFER_TYPE_LOGICAL_PHYSICAL_SG  4 /* logical pointer to physical S/G table */
#define BUFFER_TYPE_PHYSICAL_LOGICAL_SG  5 /* physical address to logical S/G table */
#define BUFFER_TYPE_PHYSICAL_PHYSICAL_SG 6 /* physical address of physical S/G table */
#define BUFFER_TYPE_PHYSICAL_PHYSICAL_SG_PIO 7 /* non DMA capable physical address of physical S/G table */

typedef struct _HPT_DRIVER_PARAMETER {
	char    name[32];
	HPT_U8  value[32];
	HPT_U8  type;        /* HPT_DRIVER_PARAMETER_TYPE_* */
	HPT_U8  persistent;
	HPT_U8  reserve2[2];
	HPT_U8  location;    /* 0 - system */
	HPT_U8  controller;
	HPT_U8  bus;
	HPT_U8  reserve1;
	char    desc[128];
}
HPT_DRIVER_PARAMETER, *PHPT_DRIVER_PARAMETER;

#define HPT_DRIVER_PARAMETER_TYPE_INT 1
#define HPT_DRIVER_PARAMETER_TYPE_BOOL 2



/*
 * ioctl structure
 */
#define HPT_IOCTL_MAGIC32 0x1A2B3C4D
#define HPT_IOCTL_MAGIC   0xA1B2C3D4

typedef struct _HPT_IOCTL_PARAM {
	HPT_U32   Magic;                 /* used to check if it's a valid ioctl packet */
	HPT_U32   dwIoControlCode;       /* operation control code */
	HPT_PTR   lpInBuffer;            /* input data buffer */
	HPT_U32   nInBufferSize;         /* size of input data buffer */
	HPT_PTR   lpOutBuffer;           /* output data buffer */
	HPT_U32   nOutBufferSize;        /* size of output data buffer */
	HPT_PTR   lpBytesReturned;       /* count of HPT_U8s returned */
}
HPT_IOCTL_PARAM, *PHPT_IOCTL_PARAM;

/* for 32-bit app running on 64-bit system */
typedef struct _HPT_IOCTL_PARAM32 {
	HPT_U32   Magic;
	HPT_U32   dwIoControlCode;
	HPT_U32   lpInBuffer;
	HPT_U32   nInBufferSize;
	HPT_U32   lpOutBuffer;
	HPT_U32   nOutBufferSize;
	HPT_U32   lpBytesReturned;
}
HPT_IOCTL_PARAM32, *PHPT_IOCTL_PARAM32;

#if !defined(__KERNEL__) || defined(SIMULATE)
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
/*
 * hpt_get_version
 * Version compatibility: all versions
 * Parameters:
 *  None
 * Returns:
 *  interface version. 0 when fail.
 */
HPT_U32 hpt_get_version(void);

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
int hpt_get_driver_capabilities_v2(PDRIVER_CAPABILITIES_V2 cap);

/*
 * hpt_get_controller_count
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  None
 * Returns:
 *  number of controllers
 */
int hpt_get_controller_count(void);

/* hpt_get_controller_info
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      Controller id
 *  pInfo   pointer to CONTROLLER_INFO buffer
 * Returns:
 *  0       Success, controller info is put into (*pInfo ).
 */
int hpt_get_controller_info(int id, PCONTROLLER_INFO pInfo);

#if HPT_INTERFACE_VERSION>=0x01020000
/* hpt_get_controller_info_v2
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *  id      Controller id
 *  pInfo   pointer to CONTROLLER_INFO_V2 buffer
 * Returns:
 *  0       Success, controller info is put into (*pInfo ).
 */
int hpt_get_controller_info_v2(int id, PCONTROLLER_INFO_V2 pInfo);

/* hpt_get_controller_info_v3
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *  id      Controller id
 *  pInfo   pointer to CONTROLLER_INFO_V3 buffer
 * Returns:
 *  0       Success, controller info is put into (*pInfo ).
 */
int hpt_get_controller_info_v3(int id, PCONTROLLER_INFO_V3 pInfo);
#endif

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

/* hpt_get_channel_info_v2
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      Controller id
 *  bus     bus number
 *  pInfo   pointer to CHANNEL_INFO buffer
 * Returns:
 *  0       Success, channel info is put into (*pInfo ).
 */
int hpt_get_channel_info_v2(int id, int bus, PCHANNEL_INFO_V2 pInfo);

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

/* hpt_get_device_info
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      logical device id
 *  pInfo   pointer to LOGICAL_DEVICE_INFO structure
 * Returns:
 *  0 - Success
 */
int hpt_get_device_info(DEVICEID id, PLOGICAL_DEVICE_INFO pInfo);

/* hpt_create_array
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  pParam      pointer to CREATE_ARRAY_PARAMS structure
 * Returns:
 *  0   failed
 *  else return array id
 */
DEVICEID hpt_create_array(PCREATE_ARRAY_PARAMS pParam);

/* hpt_delete_array
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  id      array id
 * Returns:
 *  0   Success
 */
int hpt_delete_array(DEVICEID id, HPT_U32 options);

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
int hpt_device_io(DEVICEID id, int cmd, HPT_U32 lba, HPT_U32 nSector, void * buffer);

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

/* hpt_add_spare_disk
 * Version compatibility: v1.0.0.1 or later
 *   Add a disk to spare pool.
 * Parameters:
 *  idDisk      disk id
 * Returns:
 *  0   Success
 */
int hpt_add_spare_disk(DEVICEID idDisk);

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

/* hpt_remove_spare_disk
 *   remove a disk from spare pool.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *  idDisk      disk id
 * Returns:
 *  0   Success
 */
int hpt_remove_spare_disk(DEVICEID idDisk);

/* hpt_get_event
 *   Used to poll events from driver.
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   pEvent    pointer to HPT_EVENT structure
 * Returns:
 *  0   Success, event info is filled in *pEvent
 */
int hpt_get_event(PHPT_EVENT pEvent);

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
int hpt_rebuild_data_block(DEVICEID idMirror, HPT_U32 Lba, HPT_U8 nSector);
#define hpt_rebuild_mirror(p1, p2, p3) hpt_rebuild_data_block(p1, p2, p3)

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
int hpt_set_array_state(DEVICEID idArray, HPT_U32 state);

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

/* hpt_rescan_devices
 *   rescan devices
 * Version compatibility: v1.0.0.1 or later
 * Parameters:
 *   None
 * Returns:
 *   0  Success
 */
int hpt_rescan_devices(void);

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
int hpt_lock_device(DEVICEID idDisk, HPT_U32 Lba, HPT_U8 nSectors);

/* hpt_lock_device
 *   Unlock a device
 * Version compatibiilty: v1.0.0.3 or later
 * Parameters:
 *   idDisk - Disk handle
 * Returns:
 *   0  Success
 */
int hpt_unlock_device(DEVICEID idDisk);

/* hpt_ide_pass_through
 *  send a ATA passthrough command to a device.
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   p - IDE_PASS_THROUGH header pointer
 * Returns:
 *   0  Success
 */
int hpt_ide_pass_through(PIDE_PASS_THROUGH_HEADER p);

/* hpt_scsi_passthrough
 *  send a SCSI passthrough command to a device.
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *   in  - HPT_SCSI_PASSTHROUGH_IN header pointer
 *   out - PHPT_SCSI_PASSTHROUGH_OUT header pointer
 *   insize, outsize - in/out buffer size
 * Returns:
 *   0  Success
 */
int hpt_scsi_passthrough(PHPT_SCSI_PASSTHROUGH_IN in, HPT_U32 insize,
				PHPT_SCSI_PASSTHROUGH_OUT out, HPT_U32 outsize);

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
int hpt_verify_data_block(DEVICEID idArray, HPT_U32 Lba, HPT_U8 nSectors);

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
int hpt_initialize_data_block(DEVICEID idArray, HPT_U32 Lba, HPT_U8 nSectors);

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

/* hpt_set_boot_mark
 *   select boot device
 * Version compatibility: v1.0.0.3 or later
 * Parameters:
 *   id - logical device ID. If id is 0 the boot mark will be removed.
 * Returns:
 *   0  Success
 */
int hpt_set_boot_mark(DEVICEID id);

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
int hpt_query_remove(HPT_U32 ndev, DEVICEID *pIds);

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
int hpt_remove_devices(HPT_U32 ndev, DEVICEID *pIds);

/* hpt_create_array_v2
 * Version compatibility: v1.1.0.0 or later
 * Parameters:
 *  pParam      pointer to CREATE_ARRAY_PARAMS_V2 structure
 * Returns:
 *  0   failed
 *  else return array id
 */
#if HPT_INTERFACE_VERSION>=0x01010000
DEVICEID hpt_create_array_v2(PCREATE_ARRAY_PARAMS_V2 pParam);
#endif

/* hpt_create_array_v3
 * Version compatibility: v2.0.0.1 or later
 * Parameters:
 *  pParam      pointer to CREATE_ARRAY_PARAMS_V3 structure
 * Returns:
 *  0   failed
 *  else return array id
 */
#if HPT_INTERFACE_VERSION>=0x02000001
DEVICEID hpt_create_array_v3(PCREATE_ARRAY_PARAMS_V3 pParam);
#endif

/* hpt_get_device_info_v2
 * Version compatibility: v1.1.0.0 or later
 * Parameters:
 *  id      logical device id
 *  pInfo   pointer to LOGICAL_DEVICE_INFO_V2 structure
 * Returns:
 *  0 - Success
 */
#if HPT_INTERFACE_VERSION>=0x01010000
int hpt_get_device_info_v2(DEVICEID id, PLOGICAL_DEVICE_INFO_V2 pInfo);
#endif

/* hpt_get_device_info_v3
 * Version compatibility: v1.2.0.0 or later
 * Parameters:
 *  id      logical device id
 *  pInfo   pointer to LOGICAL_DEVICE_INFO_V3 structure
 * Returns:
 *  0 - Success
 */
#if HPT_INTERFACE_VERSION>=0x01020000
int hpt_get_device_info_v3(DEVICEID id, PLOGICAL_DEVICE_INFO_V3 pInfo);
#endif

/* hpt_get_device_info_v4
 * Version compatibility: v2.0.0.1 or later
 * Parameters:
 *  id      logical device id
 *  pInfo   pointer to LOGICAL_DEVICE_INFO_V4 structure
 * Returns:
 *  0 - Success
 */
#if HPT_INTERFACE_VERSION>=0x02000001
int hpt_get_device_info_v4(DEVICEID id, PLOGICAL_DEVICE_INFO_V4 pInfo);
#endif

/* hpt_create_transform
 *  create a transform instance.
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *  idArray - source array
 *  destInfo - destination array info
 * Returns:
 *  destination array id
 */
#if HPT_INTERFACE_VERSION>=0x02000000
DEVICEID hpt_create_transform(DEVICEID idArray, PCREATE_ARRAY_PARAMS_V2 destInfo);
#endif

/* hpt_create_transform_v2
 *  create a transform instance.
 * Version compatibility: v2.0.0.1 or later
 * Parameters:
 *  idArray - source array
 *  destInfo - destination array info
 * Returns:
 *  destination array id
 */
#if HPT_INTERFACE_VERSION>=0x02000001
DEVICEID hpt_create_transform_v2(DEVICEID idArray, PCREATE_ARRAY_PARAMS_V3 destInfo);
#endif

/* hpt_step_transform
 *  move a block in a transform progress.
 *  This function is called by mid-layer, not GUI (which uses set_array_state instead).
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *  idArray - destination array ID
 *            the source ID will be invalid when transform complete.
 * Returns:
 *  0 - Success
 */
#if HPT_INTERFACE_VERSION>=0x02000000
int hpt_step_transform(DEVICEID idArray);
#endif

/* hpt_set_vdev_info
 *  set information for disk or array
 * Version compatibility: v1.2.0.0 or later
 * Parameters:
 *  dev - destination device
 *
 * Returns:
 *  0 - Success
 */
#if HPT_INTERFACE_VERSION>=0x01020000
int hpt_set_vdev_info(DEVICEID dev, PSET_VDEV_INFO pInfo);
#endif

/* hpt_init_disks
 *  initialize disks for use
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *  ndev - number of disks to initialize
 *  pIds - array of DEVICEID
 *
 * Returns:
 *  0 - Success
 */
#if HPT_INTERFACE_VERSION>=0x02000000
int hpt_init_disks(HPT_U32 ndev, DEVICEID * pIds);
#endif

/* hpt_calc_max_array_capacity
 *  cap max capacity of the array user want to create or transform
 * Version compatibility: v1.2.0.0 or later
 * Parameters:
 *  source - if transform, this is the source array, otherwise, it should be zero
 *  destInfo - target array params
 * Returns:
 *  0 - Success
 *  cap - max capacity of the target array
 */
#if HPT_INTERFACE_VERSION>=0x01020000
int hpt_calc_max_array_capacity(DEVICEID source, PCREATE_ARRAY_PARAMS_V2 destInfo, HPT_U64 * cap);
#endif

/* hpt_calc_max_array_capacity_v2
 *  cap max capacity of the array user want to create or transform
 * Version compatibility: v2.0.0.1 or later
 * Parameters:
 *  source - if transform, this is the source array, otherwise, it should be zero
 *  destInfo - target array params
 * Returns:
 *  0 - Success
 *  cap - max capacity of the target array
 */
#if HPT_INTERFACE_VERSION>=0x02000001
int hpt_calc_max_array_capacity_v2(DEVICEID source, PCREATE_ARRAY_PARAMS_V3 destInfo, HPT_U64 * cap);
#endif

/* hpt_rebuild_data_block2
 *   Used to copy data from source disk and mirror disk.
 * Version compatibility: v1.1.0.0 or later
 * Parameters:
 *   idArray        Array ID (RAID1, 0/1 or RAID5)
 *   Lba            Start LBA for each array member
 *   nSector        Number of sectors for each array member (RAID 5 will ignore this parameter)
 *
 * Returns:
 *  0   Success, event info is filled in *pEvent
 */
#if HPT_INTERFACE_VERSION>=0x01010000
int hpt_rebuild_data_block_v2(DEVICEID idMirror, HPT_U64 Lba, HPT_U16 nSector);
#endif

/* hpt_verify_data_block2
 *   verify data block on RAID1 or RAID5.
 * Version compatibility: v1.1.0.0 or later
 * Parameters:
 *   idArray - Array ID
 *   Lba - block number (on each array member, not logical block!)
 *   nSectors - Sectors for each member (RAID 5 will ignore this parameter)
 * Returns:
 *   0  Success
 *   1  Data compare error
 *   2  I/O error
 */
#if HPT_INTERFACE_VERSION>=0x01010000
int hpt_verify_data_block_v2(DEVICEID idArray, HPT_U64 Lba, HPT_U16 nSectors);
#endif

/* hpt_initialize_data_block2
 *   initialize data block (fill with zero) on RAID5
 * Version compatibility: v1.1.0.0 or later
 * Parameters:
 *   idArray - Array ID
 *   Lba - block number (on each array member, not logical block!)
 *   nSectors - Sectors for each member (RAID 5 will ignore this parameter)
 * Returns:
 *   0  Success
 */
#if HPT_INTERFACE_VERSION>=0x01010000
int hpt_initialize_data_block_v2(DEVICEID idArray, HPT_U64 Lba, HPT_U16 nSectors);
#endif

/* hpt_i2c_transaction
 *   perform an transaction on i2c bus
 * Version compatibility: v2.0.0.0 or later
 * Parameters:
 *   indata[0] - controller ID
 * Returns:
 *   0  Success
 */
#if HPT_INTERFACE_VERSION>=0x01020000
int hpt_i2c_transaction(HPT_U8 *indata, HPT_U32 inlen, HPT_U8 *outdata, HPT_U32 outlen, HPT_U32 *poutlen);
#endif

/* hpt_get_parameter_list
 *   get a list of driver parameters.
 * Version compatibility: v1.0.0.0 or later
 * Parameters:
 *   location - parameter location
 *   outBuffer - a buffer to hold the output
 *   outBufferSize - size of outBuffer
 * Returns:
 *   0  Success
 *      put in outBuffer a list of zero terminated parameter names. the whole list
 *      is terminated with an additional zero byte.
 */
int hpt_get_parameter_list(HPT_U32 location, char *outBuffer, HPT_U32 outBufferSize);

/* hpt_{get,set}_parameter
 *   get/set a parameter value.
 * Version compatibility: v1.0.0.0 or later
 * Parameters:
 *   pParam - a pointer to HPT_DRIVER_PARAMETER.
 * Returns:
 *   0  Success
 */
int hpt_get_parameter(PHPT_DRIVER_PARAMETER pParam);
int hpt_set_parameter(PHPT_DRIVER_PARAMETER pParam);
int hpt_reenumerate_device(DEVICEID id);

#endif

#pragma pack()

#ifdef __cplusplus
}
#endif
#endif
