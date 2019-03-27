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

#ifndef _ATAPI_H_
#define _ATAPI_H_

#pragma pack(1)

/***************************************************************************
 *            IDE IO Register File
 ***************************************************************************/

/*
 * IDE IO Port definition
 */
typedef struct _IDE_REGISTERS_1 {
    USHORT Data;               /* RW: Data port feature register      */
    UCHAR BlockCount;          /* RW: Sector count               */
    UCHAR BlockNumber;         /* RW: Sector number & LBA 0-7    */
    UCHAR CylinderLow;         /* RW: Cylinder low & LBA 8-15    */
    UCHAR CylinderHigh;        /* RW: Cylinder hign & LBA 16-23  */
    UCHAR DriveSelect;         /* RW: Drive/head & LBA 24-27     */
    UCHAR Command;             /* RO: Status WR:Command          */
} IDE_REGISTERS_1, *PIDE_REGISTERS_1;


/*
 * IDE status definitions
 */
#define IDE_STATUS_ERROR             0x01 /* Error Occurred in Execution    */
#define IDE_STATUS_INDEX             0x02 /* is vendor specific             */
#define IDE_STATUS_CORRECTED_ERROR   0x04 /* Corrected Data                 */
#define IDE_STATUS_DRQ               0x08 /* Ready to transfer data         */
#define IDE_STATUS_DSC               0x10 /* not defined in ATA-2           */
#define IDE_STATUS_DWF               0x20 /* Device Fault has been detected */
#define IDE_STATUS_DRDY              0x40 /* Device Ready to accept command */
#define IDE_STATUS_IDLE              0x50 /* Device is OK                   */
#define IDE_STATUS_BUSY              0x80 /* Device Busy, must wait         */


#define IDE_ERROR_BAD_BLOCK          0x80 /* Reserved now                   */
#define IDE_ERROR_DATA_ERROR         0x40 /* Uncorreectable  Data Error     */
#define IDE_ERROR_MEDIA_CHANGE       0x20 /* Media Changed                  */
#define IDE_ERROR_ID_NOT_FOUND       0x10 /* ID Not Found                   */
#define IDE_ERROR_MEDIA_CHANGE_REQ   0x08 /* Media Change Requested         */
#define IDE_ERROR_COMMAND_ABORTED    0x04 /* Aborted Command                */
#define IDE_ERROR_TRACK0_NOT_FOUND   0x02 /* Track 0 Not Found              */
#define IDE_ERROR_ADDRESS_NOT_FOUND  0x01 /* Address Mark Not Found         */


#define LBA_MODE                     0x40

/*
 * IDE command definitions
 */

#define IDE_COMMAND_RECALIBRATE      0x10 /* Recalibrate                    */
#define IDE_COMMAND_READ             0x20 /* Read Sectors with retry        */
#define IDE_COMMAND_WRITE            0x30 /* Write Sectors with retry       */
#define IDE_COMMAND_VERIFY           0x40 /* Read Verify Sectors with Retry */
#define IDE_COMMAND_SEEK             0x70 /* Seek                           */
#define IDE_COMMAND_SET_DRIVE_PARAMETER   0x91 /* Initialize Device Parmeters */
#define IDE_COMMAND_GET_MEDIA_STATUS 0xDA
#define IDE_COMMAND_DOOR_LOCK        0xDE /* Door Lock                      */
#define IDE_COMMAND_DOOR_UNLOCK      0xDF /* Door Unlock                          */
#define IDE_COMMAND_ENABLE_MEDIA_STATUS   0xEF /* Set Features              */
#define IDE_COMMAND_IDENTIFY         0xEC /* Identify Device                */
#define IDE_COMMAND_MEDIA_EJECT      0xED
#define IDE_COMMAND_SET_FEATURES     0xEF /* IDE set features command       */

#define IDE_COMMAND_FLUSH_CACHE      0xE7
#define IDE_COMMAND_STANDBY_IMMEDIATE 0xE0

#ifndef NOT_SUPPORT_MULTIPLE
#define IDE_COMMAND_READ_MULTIPLE    0xC4 /* Read Multiple                  */
#define IDE_COMMAND_WRITE_MULTIPLE   0xC5 /* Write Multiple                 */
#define IDE_COMMAND_SET_MULTIPLE     0xC6 /* Set Multiple Mode              */
#endif

#ifndef NOT_SUPPORT_DMA
#define IDE_COMMAND_DMA_READ        0xc8  /* IDE DMA read command           */
#define IDE_COMMAND_DMA_WRITE       0xca  /* IDE DMA write command          */
#endif

#define IDE_COMMAND_READ_DMA_QUEUE   0xc7 /* IDE read DMA queue command     */
#define IDE_COMMAND_WRITE_DMA_QUEUE  0xcc /* IDE write DMA queue command    */
#define IDE_COMMAND_SERVICE          0xA2 /* IDE service command command    */
#define IDE_COMMAND_NOP              0x00 /* IDE NOP command                */
#define IDE_STATUS_SRV               0x10
#define IDE_RELEASE_BUS              4

/*#define IDE_COMMAND_FLUSH_CACHE_EXT */
#define IDE_COMMAND_READ_DMA_EXT       	0x25
#define IDE_COMMAND_READ_QUEUE_EXT		0x26
#define IDE_COMMAND_READ_MULTIPLE_EXT	0x29
#define IDE_COMMAND_READ_MAX_ADDR		0x27
#define IDE_COMMAND_READ_EXT			0x24
#define IDE_COMMAND_VERIFY_EXT			0x42
#define IDE_COMMAND_SET_MULTIPLE_EXT	0x37
#define IDE_COMMAND_WRITE_DMA_EXT		0x35
#define IDE_COMMAND_WRITE_QUEUE_EXT		0x36
#define IDE_COMMAND_WRITE_EXT			0x34
#define IDE_COMMAND_WRITE_MULTIPLE_EXT	0x39

/*
 * IDE_COMMAND_SET_FEATURES
 */
#define FT_USE_ULTRA        0x40    /* Set feature for Ultra DMA           */
#define FT_USE_MWDMA        0x20    /* Set feature for MW DMA              */
#define FT_USE_SWDMA        0x10    /* Set feature for SW DMA              */
#define FT_USE_PIO          0x8     /* Set feature for PIO                 */
#define FT_DISABLE_IORDY    0x10    /* Set feature for disabling IORDY     */

/*
 * S.M.A.R.T. commands
 */
#define IDE_COMMAND_SMART       0xB0
#define SMART_READ_VALUES       0xd0
#define SMART_READ_THRESHOLDS   0xd1
#define SMART_AUTOSAVE          0xd2
#define SMART_SAVE              0xd3
#define SMART_IMMEDIATE_OFFLINE 0xd4
#define SMART_READ_LOG_SECTOR   0xd5
#define SMART_WRITE_LOG_SECTOR  0xd6
#define SMART_ENABLE            0xd8
#define SMART_DISABLE           0xd9
#define SMART_STATUS            0xda
#define SMART_AUTO_OFFLINE      0xdb

 /***************************************************************************
 *            IDE Control Register File
 ***************************************************************************/

typedef struct _IDE_REGISTERS_2 {
    UCHAR AlternateStatus;     /* RW: device control port        */
} IDE_REGISTERS_2, *PIDE_REGISTERS_2;


/*
 * IDE drive control definitions
 */
#define IDE_DC_DISABLE_INTERRUPTS    0x02
#define IDE_DC_RESET_CONTROLLER      0x04
#define IDE_DC_REENABLE_CONTROLLER   0x00

/***************************************************************************
 *   MSNS:   Removable device
 ***************************************************************************/
/*
 * Media syatus
 */
#define MSNS_NO_MEDIA             2
#define MSNS_MEDIA_CHANGE_REQUEST 8
#define MSNS_MIDIA_CHANGE         0x20
#define MSNS_WRITE_PROTECT        0x40
#define MSNS_READ_PROTECT         0x80

/*
 * IDENTIFY data
 */
typedef struct _IDENTIFY_DATA {
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
    USHORT TranslationFieldsValid;          /* 6A  53 */
    USHORT NumberOfCurrentCylinders;        /* 6C  54 */
    USHORT NumberOfCurrentHeads;            /* 6E  55 */
    USHORT CurrentSectorsPerTrack;          /* 70  56 */
    ULONG  CurrentSectorCapacity;           /* 72  57-58 */
    USHORT CurrentMultiSectorSetting;       /* 76  59 */
    ULONG  UserAddressableSectors;          /* 78  60-61 */
    UCHAR  SingleWordDMASupport;            /* 7C  62 */
    UCHAR  SingleWordDMAActive;             /* 7D */
    UCHAR  MultiWordDMASupport;         	/* 7E  63 */
    UCHAR  MultiWordDMAActive;              /* 7F */
    UCHAR  AdvancedPIOModes;                /* 80  64 */
    UCHAR  Reserved4;                       /* 81 */
    USHORT MinimumMWXferCycleTime;          /* 82  65 */
    USHORT RecommendedMWXferCycleTime;      /* 84  66 */
    USHORT MinimumPIOCycleTime;             /* 86  67 */
    USHORT MinimumPIOCycleTimeIORDY;        /* 88  68 */
    USHORT Reserved5[2];                    /* 8A  69-70 */
    USHORT ReleaseTimeOverlapped;           /* 8E  71 */
    USHORT ReleaseTimeServiceCommand;       /* 90  72 */
    USHORT MajorRevision;                   /* 92  73 */
    USHORT MinorRevision;                   /* 94  74 */
    USHORT MaxQueueDepth;                   /* 96  75 */
	USHORT SataCapability;                  /*     76 */
    USHORT Reserved6[9];                    /* 98   77-85 */
    USHORT CommandSupport;                  /*     86 */
    USHORT CommandEnable;                   /*     87 */
    USHORT UtralDmaMode;                    /*     88 */
    USHORT Reserved7[11];                   /*     89-99 */
    ULONG  Lba48BitLow;						/*     101-100 */
    ULONG  Lba48BitHigh;					/*     103-102 */
    USHORT Reserved8[23];                   /*     104-126 */
    USHORT SpecialFunctionsEnabled;         /*     127 */
    USHORT Reserved9[128];                  /*     128-255 */

} IDENTIFY_DATA, *PIDENTIFY_DATA;

typedef struct _CONFIGURATION_IDENTIFY_DATA {
	USHORT Revision;
	USHORT MWDMAModeSupported;
	USHORT UDMAModeSupported;
	ULONG  MaximumLbaLow;
	ULONG  MaximumLbaHigh;
	USHORT CommandSupport;
	USHORT Reserved[247];
	UCHAR  Signature; /* 0xA5 */
	UCHAR  CheckSum;
}
CONFIGURATION_IDENTIFY_DATA, *PCONFIGURATION_IDENTIFY_DATA;

/* */
/* Identify data without the Reserved4. */
/* */
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
    USHORT TranslationFieldsValid;         	/* 6A  53 */
    USHORT NumberOfCurrentCylinders;        /* 6C  54 */
    USHORT NumberOfCurrentHeads;            /* 6E  55 */
    USHORT CurrentSectorsPerTrack;          /* 70  56 */
    ULONG  CurrentSectorCapacity;           /* 72  57-58 */
    USHORT CurrentMultiSectorSetting;       /*     59 */
    ULONG  UserAddressableSectors;          /*     60-61 */
    UCHAR  SingleWordDMASupport;        	/*     62 */
    UCHAR  SingleWordDMAActive;
    UCHAR  MultiWordDMASupport;         	/*     63 */
    UCHAR  MultiWordDMAActive;
    UCHAR  AdvancedPIOModes;            	/*     64 */
    UCHAR  Reserved4;
    USHORT MinimumMWXferCycleTime;          /*     65 */
    USHORT RecommendedMWXferCycleTime;      /*     66 */
    USHORT MinimumPIOCycleTime;             /*     67 */
    USHORT MinimumPIOCycleTimeIORDY;        /*     68 */
    USHORT Reserved5[2];                    /*     69-70 */
    USHORT ReleaseTimeOverlapped;           /*     71 */
    USHORT ReleaseTimeServiceCommand;       /*     72 */
    USHORT MajorRevision;                   /*     73 */
    USHORT MinorRevision;                   /*     74 */
/*    USHORT Reserved6[14];                 //     75-88 */
} IDENTIFY_DATA2, *PIDENTIFY_DATA2;

#define IDENTIFY_DATA_SIZE sizeof(IDENTIFY_DATA2)

/* */
/* IDENTIFY DMA timing cycle modes. */
/* */

#define IDENTIFY_DMA_CYCLES_MODE_0 0x00
#define IDENTIFY_DMA_CYCLES_MODE_1 0x01
#define IDENTIFY_DMA_CYCLES_MODE_2 0x02

/*
 * Mode definitions
 */
typedef enum _DISK_MODE
{
	IDE_PIO_0 = 0,
	IDE_PIO_1,
	IDE_PIO_2,
	IDE_PIO_3,
	IDE_PIO_4,
	IDE_MWDMA_0,
	IDE_MWDMA_1,
	IDE_MWDMA_2,
	IDE_UDMA_0,
	IDE_UDMA_1,
	IDE_UDMA_2,
	IDE_UDMA_3,
	IDE_UDMA_4,
	IDE_UDMA_5,
	IDE_UDMA_6,
	IDE_UDMA_7,
} DISK_MODE;

/***************************************************************************
 *            IDE Macro
 ***************************************************************************/
#ifndef MAX_LBA_T
#define MAX_LBA_T ((LBA_T)-1)
#endif

#define SECTOR_TO_BYTE_SHIFT 9
#define SECTOR_TO_BYTE(x)  ((ULONG)(x) << SECTOR_TO_BYTE_SHIFT)

#define mGetStatus(IOPort2)           (UCHAR)InPort(&IOPort2->AlternateStatus)
#define mUnitControl(IOPort2, Value)  OutPort(&IOPort2->AlternateStatus,(UCHAR)(Value))

#define mGetErrorCode(IOPort)         (UCHAR)InPort((PUCHAR)&IOPort->Data+1)
#define mSetFeaturePort(IOPort,x)     OutPort((PUCHAR)&IOPort->Data+1, x)
#define mSetBlockCount(IOPort,x)      OutPort(&IOPort->BlockCount, x)
#define mGetBlockCount(IOPort)	      (UCHAR)InPort(&IOPort->BlockCount)
#define mGetInterruptReason(IOPort)   (UCHAR)InPort(&IOPort->BlockCount)
#define mSetBlockNumber(IOPort,x)     OutPort(&IOPort->BlockNumber, x)
#define mGetBlockNumber(IOPort)       (UCHAR)InPort((PUCHAR)&IOPort->BlockNumber)
#define mGetByteLow(IOPort)           (UCHAR)InPort(&IOPort->CylinderLow)
#define mSetCylinderLow(IOPort,x)         OutPort(&IOPort->CylinderLow, x)
#define mGetByteHigh(IOPort)          (UCHAR)InPort(&IOPort->CylinderHigh)
#define mSetCylinderHigh(IOPort,x)    OutPort(&IOPort->CylinderHigh, x)
#define mGetBaseStatus(IOPort)        (UCHAR)InPort(&IOPort->Command)
#ifdef SUPPORT_HPT601
#define mSelectUnit(IOPort,UnitId)  do {\
		OutPort(&IOPort->DriveSelect, (UCHAR)(UnitId));\
		OutPort(&IOPort->DriveSelect, (UCHAR)(UnitId));\
		} while (0)
#else 
#define mSelectUnit(IOPort,UnitId)    OutPort(&IOPort->DriveSelect, (UCHAR)(UnitId))
#endif
#define mGetUnitNumber(IOPort)        InPort(&IOPort->DriveSelect)
#define mIssueCommand(IOPort,Cmd)     OutPort(&IOPort->Command, (UCHAR)(Cmd))

/*
 * WDC old disk, don't care right now
 */
#define WDC_MW1_FIX_FLAG_OFFSET        129
#define WDC_MW1_FIX_FLAG_VALUE        0x00005555

#pragma pack()	
#endif
