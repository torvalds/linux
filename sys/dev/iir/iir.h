/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *       Copyright (c) 2000-04 ICP vortex GmbH
 *       Copyright (c) 2002-04 Intel Corporation
 *       Copyright (c) 2003-04 Adaptec Inc.
 *       All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 *
 * iir.h:       Definitions/Constants used by the Intel Integrated RAID driver
 *
 * Written by: 	Achim Leubner <achim_leubner@adaptec.com>
 * Fixes/Additions:	Boji Tony Kannanthanam <boji.t.kannanthanam@intel.com>
 *
 * credits:     Niklas Hallqvist;       OpenBSD driver for the ICP Controllers.
 *              FreeBSD.ORG;            Great O/S to work on and for.
 *
 * $Id: iir.h 1.6 2004/03/30 10:19:44 achim Exp $"
 */

#ifndef _IIR_H
#define _IIR_H

#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif

#define IIR_DRIVER_VERSION      1
#define IIR_DRIVER_SUBVERSION   5

/* OEM IDs */
#define OEM_ID_ICP              0x941c
#define OEM_ID_INTEL            0x8000

#define GDT_VENDOR_ID           0x1119
#define GDT_DEVICE_ID_MIN       0x100
#define GDT_DEVICE_ID_MAX       0x2ff
#define GDT_DEVICE_ID_NEWRX     0x300

#define INTEL_VENDOR_ID_IIR     0x8086
#define INTEL_DEVICE_ID_IIR     0x600

#define GDT_MAXBUS              6       /* XXX Why not 5? */
#define GDT_MAX_HDRIVES         100     /* max 100 host drives */
#define GDT_MAXID_FC            127     /* Fibre-channel IDs */
#define GDT_MAXID               16      /* SCSI IDs */
#define GDT_MAXOFFSETS          128
#define GDT_MAXSG               32      /* Max. s/g elements */
#define GDT_PROTOCOL_VERSION    1
#define GDT_LINUX_OS            8       /* Used for cache optimization */
#define GDT_SCATTER_GATHER      1       /* s/g feature */
#define GDT_SECS32              0x1f    /* round capacity */
#define GDT_LOCALBOARD          0       /* Board node always 0 */
#define GDT_MAXCMDS             124
#define GDT_SECTOR_SIZE         0x200   /* Always 512 bytes for cache devs */
#define GDT_MAX_EVENTS          0x100   /* event buffer */

/* DPMEM constants */
#define GDT_MPR_MAGIC           0xc0ffee11
#define GDT_IC_HEADER_BYTES     48
#define GDT_IC_QUEUE_BYTES      4
#define GDT_DPMEM_COMMAND_OFFSET \
    (GDT_IC_HEADER_BYTES + GDT_IC_QUEUE_BYTES * GDT_MAXOFFSETS)

/* geometry constants */
#define GDT_MAXCYLS             1024
#define GDT_HEADS               64
#define GDT_SECS                32      /* mapping 64*32 */
#define GDT_MEDHEADS            127
#define GDT_MEDSECS             63      /* mapping 127*63 */
#define GDT_BIGHEADS            255
#define GDT_BIGSECS             63      /* mapping 255*63 */

/* data direction raw service */
#define GDT_DATA_IN             0x01000000L
#define GDT_DATA_OUT            0x00000000L

/* Cache/raw service commands */
#define GDT_INIT        0               /* service initialization */
#define GDT_READ        1               /* read command */
#define GDT_WRITE       2               /* write command */
#define GDT_INFO        3               /* information about devices */
#define GDT_FLUSH       4               /* flush dirty cache buffers */
#define GDT_IOCTL       5               /* ioctl command */
#define GDT_DEVTYPE     9               /* additional information */
#define GDT_MOUNT       10              /* mount cache device */
#define GDT_UNMOUNT     11              /* unmount cache device */
#define GDT_SET_FEAT    12              /* set features (scatter/gather) */
#define GDT_GET_FEAT    13              /* get features */
#define GDT_WRITE_THR   16              /* write through */
#define GDT_READ_THR    17              /* read through */
#define GDT_EXT_INFO    18              /* extended info */
#define GDT_RESET       19              /* controller reset */
#define GDT_FREEZE_IO   25              /* freeze all IOs */
#define GDT_UNFREEZE_IO 26              /* unfreeze all IOs */

/* Additional raw service commands */
#define GDT_RESERVE     14              /* reserve device to raw service */
#define GDT_RELEASE     15              /* release device */
#define GDT_RESERVE_ALL 16              /* reserve all devices */
#define GDT_RELEASE_ALL 17              /* release all devices */
#define GDT_RESET_BUS   18              /* reset bus */
#define GDT_SCAN_START  19              /* start device scan */
#define GDT_SCAN_END    20              /* stop device scan */  

/* IOCTL command defines */
#define GDT_SCSI_DR_INFO        0x00    /* SCSI drive info */
#define GDT_SCSI_CHAN_CNT       0x05    /* SCSI channel count */
#define GDT_SCSI_DR_LIST        0x06    /* SCSI drive list */
#define GDT_SCSI_DEF_CNT        0x15    /* grown/primary defects */
#define GDT_DSK_STATISTICS      0x4b    /* SCSI disk statistics */
#define GDT_IOCHAN_DESC         0x5d    /* description of IO channel */
#define GDT_IOCHAN_RAW_DESC     0x5e    /* description of raw IO channel */

#define GDT_L_CTRL_PATTERN      0x20000000      /* SCSI IOCTL mask */
#define GDT_ARRAY_INFO          0x12            /* array drive info */
#define GDT_ARRAY_DRV_LIST      0x0f            /* array drive list */
#define GDT_LA_CTRL_PATTERN     0x10000000      /* array IOCTL mask */
#define GDT_CACHE_DRV_CNT       0x01            /* cache drive count */
#define GDT_CACHE_DRV_LIST      0x02            /* cache drive list */
#define GDT_CACHE_INFO          0x04            /* cache info */
#define GDT_CACHE_CONFIG        0x05            /* cache configuration */
#define GDT_CACHE_DRV_INFO      0x07            /* cache drive info */
#define GDT_BOARD_FEATURES      0x15            /* controller features */
#define GDT_BOARD_INFO          0x28            /* controller info */
#define GDT_OEM_STR_RECORD      0x84            /* OEM info */
#define GDT_HOST_GET            0x10001         /* get host drive list */
#define GDT_IO_CHANNEL          0x20000         /* default IO channel */
#define GDT_INVALID_CHANNEL     0xffff          /* invalid channel */

/* IOCTLs */
#define GDT_IOCTL_GENERAL       _IOWR('J', 0, gdt_ucmd_t) /* general IOCTL */
#define GDT_IOCTL_DRVERS        _IOR('J', 1, int)      /* get driver version */
#define GDT_IOCTL_CTRTYPE       _IOWR('J', 2, gdt_ctrt_t) /* get ctr. type */
#define GDT_IOCTL_DRVERS_OLD    _IOWR('J', 1, int)      /* get driver version */
#define GDT_IOCTL_CTRTYPE_OLD   _IOR('J', 2, gdt_ctrt_t) /* get ctr. type */
#define GDT_IOCTL_OSVERS        _IOR('J', 3, gdt_osv_t) /* get OS version */
#define GDT_IOCTL_CTRCNT        _IOR('J', 5, int)       /* get ctr. count */
#define GDT_IOCTL_EVENT         _IOWR('J', 8, gdt_event_t) /* get event */
#define GDT_IOCTL_STATIST       _IOR('J', 9, gdt_statist_t) /* get statistics */

/* Service errors */
#define GDT_S_OK                1       /* no error */
#define GDT_S_BSY               7       /* controller busy */
#define GDT_S_RAW_SCSI          12      /* raw service: target error */
#define GDT_S_RAW_ILL           0xff    /* raw service: illegal */
#define GDT_S_NO_STATUS         0x1000  /* got no status (driver-generated) */

/* Controller services */
#define GDT_SCSIRAWSERVICE      3
#define GDT_CACHESERVICE        9
#define GDT_SCREENSERVICE       11

/* Scatter/gather element */
#define GDT_SG_PTR              0x00    /* u_int32_t, address */
#define GDT_SG_LEN              0x04    /* u_int32_t, length */
#define GDT_SG_SZ               0x08

/* Cache service command */
#define GDT_CACHE_DEVICENO      0x00    /* u_int16_t, number of cache drive */
#define GDT_CACHE_BLOCKNO       0x02    /* u_int32_t, block number */
#define GDT_CACHE_BLOCKCNT      0x06    /* u_int32_t, block count */
#define GDT_CACHE_DESTADDR      0x0a    /* u_int32_t, dest. addr. (-1: s/g) */
#define GDT_CACHE_SG_CANZ       0x0e    /* u_int32_t, s/g element count */
#define GDT_CACHE_SG_LST        0x12    /* [GDT_MAXSG], s/g list */
#define GDT_CACHE_SZ            (0x12 + GDT_MAXSG * GDT_SG_SZ)

/* Ioctl command */
#define GDT_IOCTL_PARAM_SIZE    0x00    /* u_int16_t, size of buffer */
#define GDT_IOCTL_SUBFUNC       0x02    /* u_int32_t, ioctl function */
#define GDT_IOCTL_CHANNEL       0x06    /* u_int32_t, device */
#define GDT_IOCTL_P_PARAM       0x0a    /* u_int32_t, buffer */
#define GDT_IOCTL_SZ            0x0e

/* Screen service defines */
#define GDT_MSG_INV_HANDLE      -1      /* special message handle */
#define GDT_MSGLEN              16      /* size of message text */
#define GDT_MSG_SIZE            34      /* size of message structure */
#define GDT_MSG_REQUEST         0       /* async. event. message */

/* Screen service command */
#define GDT_SCREEN_MSG_HANDLE   0x02    /* u_int32_t, message handle */
#define GDT_SCREEN_MSG_ADDR     0x06    /* u_int32_t, message buffer address */
#define GDT_SCREEN_SZ           0x0a

/* Screen service message */
#define GDT_SCR_MSG_HANDLE      0x00    /* u_int32_t, message handle */
#define GDT_SCR_MSG_LEN         0x04    /* u_int32_t, size of message */
#define GDT_SCR_MSG_ALEN        0x08    /* u_int32_t, answer length */
#define GDT_SCR_MSG_ANSWER      0x0c    /* u_int8_t, answer flag */
#define GDT_SCR_MSG_EXT         0x0d    /* u_int8_t, more messages? */
#define GDT_SCR_MSG_RES         0x0e    /* u_int16_t, reserved */
#define GDT_SCR_MSG_TEXT        0x10    /* GDT_MSGLEN+2, message text */
#define GDT_SCR_MSG_SZ          (0x12 + GDT_MSGLEN)

/* Raw service command */
#define GDT_RAW_DIRECTION       0x02    /* u_int32_t, data direction */
#define GDT_RAW_MDISC_TIME      0x06    /* u_int32_t, disc. time (0: none) */
#define GDT_RAW_MCON_TIME       0x0a    /* u_int32_t, conn. time (0: none) */
#define GDT_RAW_SDATA           0x0e    /* u_int32_t, dest. addr. (-1: s/g) */
#define GDT_RAW_SDLEN           0x12    /* u_int32_t, data length */
#define GDT_RAW_CLEN            0x16    /* u_int32_t, SCSI cmd len (6/10/12) */
#define GDT_RAW_CMD             0x1a    /* u_int8_t [12], SCSI command */
#define GDT_RAW_TARGET          0x26    /* u_int8_t, target ID */
#define GDT_RAW_LUN             0x27    /* u_int8_t, LUN */
#define GDT_RAW_BUS             0x28    /* u_int8_t, SCSI bus number */
#define GDT_RAW_PRIORITY        0x29    /* u_int8_t, only 0 used */
#define GDT_RAW_SENSE_LEN       0x2a    /* u_int32_t, sense data length */
#define GDT_RAW_SENSE_DATA      0x2e    /* u_int32_t, sense data address */
#define GDT_RAW_SG_RANZ         0x36    /* u_int32_t, s/g element count */
#define GDT_RAW_SG_LST          0x3a    /* [GDT_MAXSG], s/g list */
#define GDT_RAW_SZ              (0x3a + GDT_MAXSG * GDT_SG_SZ)

/* Command structure */
#define GDT_CMD_BOARDNODE       0x00    /* u_int32_t, board node (always 0) */
#define GDT_CMD_COMMANDINDEX    0x04    /* u_int32_t, command number */ 
#define GDT_CMD_OPCODE          0x08    /* u_int16_t, opcode (READ, ...) */
#define GDT_CMD_UNION           0x0a    /* cache/screen/raw service command */
#define GDT_CMD_UNION_SZ        GDT_RAW_SZ
#define GDT_CMD_SZ              (0x0a + GDT_CMD_UNION_SZ)

/* Command queue entries */
#define GDT_OFFSET      0x00    /* u_int16_t, command offset in the DP RAM */
#define GDT_SERV_ID     0x02    /* u_int16_t, service */
#define GDT_COMM_Q_SZ   0x04

/* Interface area */
#define GDT_S_CMD_INDX  0x00    /* u_int8_t, special command */
#define GDT_S_STATUS    0x01    /* volatile u_int8_t, status special command */
#define GDT_S_INFO      0x04    /* u_int32_t [4], add. info special command */
#define GDT_SEMA0       0x14    /* volatile u_int8_t, command semaphore */
#define GDT_CMD_INDEX   0x18    /* u_int8_t, command number */
#define GDT_STATUS      0x1c    /* volatile u_int16_t, command status */
#define GDT_SERVICE     0x1e    /* u_int16_t, service (for asynch. events) */
#define GDT_DPR_INFO    0x20    /* u_int32_t [2], additional info */
#define GDT_COMM_QUEUE  0x28    /* command queue */
#define GDT_DPR_CMD     (0x30 + GDT_MAXOFFSETS * GDT_COMM_Q_SZ)
                                /* u_int8_t [], commands */

/* I/O channel header */
#define GDT_IOC_VERSION         0x00    /* u_int32_t, version (~0: newest) */
#define GDT_IOC_LIST_ENTRIES    0x04    /* u_int8_t, list entry count */
#define GDT_IOC_FIRST_CHAN      0x05    /* u_int8_t, first channel number */
#define GDT_IOC_LAST_CHAN       0x06    /* u_int8_t, last channel number */
#define GDT_IOC_CHAN_COUNT      0x07    /* u_int8_t, (R) channel count */
#define GDT_IOC_LIST_OFFSET     0x08    /* u_int32_t, offset of list[0] */
#define GDT_IOC_HDR_SZ          0x0c

#define GDT_IOC_NEWEST          0xffffffff      /* goes into GDT_IOC_VERSION */

/* Get I/O channel description */
#define GDT_IOC_ADDRESS         0x00    /* u_int32_t, channel address */
#define GDT_IOC_TYPE            0x04    /* u_int8_t, type (SCSI/FCSL) */
#define GDT_IOC_LOCAL_NO        0x05    /* u_int8_t, local number */
#define GDT_IOC_FEATURES        0x06    /* u_int16_t, channel features */
#define GDT_IOC_SZ              0x08

/* Get raw I/O channel description */
#define GDT_RAWIOC_PROC_ID      0x00    /* u_int8_t, processor id */
#define GDT_RAWIOC_PROC_DEFECT  0x01    /* u_int8_t, defect? */
#define GDT_RAWIOC_SZ           0x04

/* Get SCSI channel count */
#define GDT_GETCH_CHANNEL_NO    0x00    /* u_int32_t, channel number */
#define GDT_GETCH_DRIVE_CNT     0x04    /* u_int32_t, drive count */
#define GDT_GETCH_SIOP_ID       0x08    /* u_int8_t, SCSI processor ID */
#define GDT_GETCH_SIOP_STATE    0x09    /* u_int8_t, SCSI processor state */
#define GDT_GETCH_SZ            0x0a

/* Cache info/config IOCTL structures */
#define GDT_CPAR_VERSION        0x00    /* u_int32_t, firmware version */
#define GDT_CPAR_STATE          0x04    /* u_int16_t, cache state (on/off) */
#define GDT_CPAR_STRATEGY       0x06    /* u_int16_t, cache strategy */
#define GDT_CPAR_WRITE_BACK     0x08    /* u_int16_t, write back (on/off) */
#define GDT_CPAR_BLOCK_SIZE     0x0a    /* u_int16_t, cache block size */
#define GDT_CPAR_SZ             0x0c

#define GDT_CSTAT_CSIZE         0x00    /* u_int32_t, cache size */
#define GDT_CSTAT_READ_CNT      0x04    /* u_int32_t, read counter */
#define GDT_CSTAT_WRITE_CNT     0x08    /* u_int32_t, write counter */
#define GDT_CSTAT_TR_HITS       0x0c    /* u_int32_t, track hits */
#define GDT_CSTAT_SEC_HITS      0x10    /* u_int32_t, sector hits */
#define GDT_CSTAT_SEC_MISS      0x14    /* u_int32_t, sector misses */
#define GDT_CSTAT_SZ            0x18

/* Get cache info */
#define GDT_CINFO_CPAR          0x00
#define GDT_CINFO_CSTAT         GDT_CPAR_SZ
#define GDT_CINFO_SZ            (GDT_CPAR_SZ + GDT_CSTAT_SZ)

/* Get board info */
#define GDT_BINFO_SER_NO        0x00    /* u_int32_t, serial number */
#define GDT_BINFO_OEM_ID        0x04    /* u_int8_t [2], OEM ID */
#define GDT_BINFO_EP_FLAGS      0x06    /* u_int16_t, eprom flags */
#define GDT_BINFO_PROC_ID       0x08    /* u_int32_t, processor ID */
#define GDT_BINFO_MEMSIZE       0x0c    /* u_int32_t, memory size (bytes) */
#define GDT_BINFO_MEM_BANKS     0x10    /* u_int8_t, memory banks */
#define GDT_BINFO_CHAN_TYPE     0x11    /* u_int8_t, channel type */
#define GDT_BINFO_CHAN_COUNT    0x12    /* u_int8_t, channel count */
#define GDT_BINFO_RDONGLE_PRES  0x13    /* u_int8_t, dongle present */
#define GDT_BINFO_EPR_FW_VER    0x14    /* u_int32_t, (eprom) firmware ver */
#define GDT_BINFO_UPD_FW_VER    0x18    /* u_int32_t, (update) firmware ver */
#define GDT_BINFO_UPD_REVISION  0x1c    /* u_int32_t, update revision */
#define GDT_BINFO_TYPE_STRING   0x20    /* char [16], controller name */
#define GDT_BINFO_RAID_STRING   0x30    /* char [16], RAID firmware name */
#define GDT_BINFO_UPDATE_PRES   0x40    /* u_int8_t, update present? */
#define GDT_BINFO_XOR_PRES      0x41    /* u_int8_t, XOR engine present */
#define GDT_BINFO_PROM_TYPE     0x42    /* u_int8_t, ROM type (eprom/flash) */
#define GDT_BINFO_PROM_COUNT    0x43    /* u_int8_t, number of ROM devices */
#define GDT_BINFO_DUP_PRES      0x44    /* u_int32_t, duplexing module pres? */
#define GDT_BINFO_CHAN_PRES     0x48    /* u_int32_t, # of exp. channels */
#define GDT_BINFO_MEM_PRES      0x4c    /* u_int32_t, memory expansion inst? */
#define GDT_BINFO_FT_BUS_SYSTEM 0x50    /* u_int8_t, fault bus supported? */
#define GDT_BINFO_SUBTYPE_VALID 0x51    /* u_int8_t, board_subtype valid */
#define GDT_BINFO_BOARD_SUBTYPE 0x52    /* u_int8_t, subtype/hardware level */
#define GDT_BINFO_RAMPAR_PRES   0x53    /* u_int8_t, RAM parity check hw? */
#define GDT_BINFO_SZ            0x54

/* Get board features */
#define GDT_BFEAT_CHAINING      0x00    /* u_int8_t, chaining supported */
#define GDT_BFEAT_STRIPING      0x01    /* u_int8_t, striping (RAID-0) supp. */
#define GDT_BFEAT_MIRRORING     0x02    /* u_int8_t, mirroring (RAID-1) supp */
#define GDT_BFEAT_RAID          0x03    /* u_int8_t, RAID-4/5/10 supported */
#define GDT_BFEAT_SZ            0x04

/* Other defines */
#define GDT_ASYNCINDEX  0       /* command index asynchronous event */
#define GDT_SPEZINDEX   1       /* command index unknown service */

/* Debugging */
#ifdef GDT_DEBUG
#define GDT_D_INTR      0x01
#define GDT_D_MISC      0x02
#define GDT_D_CMD       0x04
#define GDT_D_QUEUE     0x08
#define GDT_D_TIMEOUT   0x10
#define GDT_D_INIT      0x20
#define GDT_D_INVALID   0x40
#define GDT_D_DEBUG     0x80
extern int gdt_debug;
#ifdef __SERIAL__
extern int ser_printf(const char *fmt, ...);
#define GDT_DPRINTF(mask, args) if (gdt_debug & (mask)) ser_printf args
#else
#define GDT_DPRINTF(mask, args) if (gdt_debug & (mask)) printf args
#endif
#else
#define GDT_DPRINTF(mask, args)
#endif

/* Miscellaneous constants */
#define GDT_RETRIES             100000000       /* 100000 * 1us = 100s */
#define GDT_TIMEOUT             100000000       /* 100000 * 1us = 100s */
#define GDT_POLL_TIMEOUT        10000000        /* 10000 * 1us = 10s */
#define GDT_WATCH_TIMEOUT       10000000        /* 10000 * 1us = 10s */
#define GDT_SCRATCH_SZ          3072            /* 3KB scratch buffer */

/* Map minor numbers to device identity */
#define LUN_MASK                0x0007
#define TARGET_MASK             0x03f8
#define BUS_MASK                0x1c00
#define HBA_MASK                0xe000

#define minor2lun(minor)        ( minor & LUN_MASK )
#define minor2target(minor)     ( (minor & TARGET_MASK) >> 3 )
#define minor2bus(minor)        ( (minor & BUS_MASK) >> 10 )
#define minor2hba(minor)        ( (minor & HBA_MASK) >> 13 )
#define hba2minor(hba)          ( (hba << 13) & HBA_MASK )


/* struct for GDT_IOCTL_GENERAL */
#pragma pack(1)
typedef struct gdt_ucmd {
    u_int16_t   io_node;
    u_int16_t   service;
    u_int32_t   timeout;
    u_int16_t   status;
    u_int32_t   info;

    u_int32_t   BoardNode;                      /* board node (always 0) */
    u_int32_t   CommandIndex;                   /* command number */
    u_int16_t   OpCode;                         /* the command (READ,..) */
    union {
        struct {
            u_int16_t   DeviceNo;               /* number of cache drive */
            u_int32_t   BlockNo;                /* block number */
            u_int32_t   BlockCnt;               /* block count */
            void        *DestAddr;              /* data */
        } cache;                                /* cache service cmd. str. */
        struct {
            u_int16_t   param_size;             /* size of p_param buffer */
            u_int32_t   subfunc;                /* IOCTL function */
            u_int32_t   channel;                /* device */
            void        *p_param;               /* data */
        } ioctl;                                /* IOCTL command structure */
        struct {
            u_int16_t   reserved;
            u_int32_t   direction;              /* data direction */
            u_int32_t   mdisc_time;             /* disc. time (0: no timeout)*/
            u_int32_t   mcon_time;              /* connect time(0: no to.) */
            void        *sdata;                 /* dest. addr. (if s/g: -1) */
            u_int32_t   sdlen;                  /* data length (bytes) */
            u_int32_t   clen;                   /* SCSI cmd. length(6,10,12) */
            u_int8_t    cmd[12];                /* SCSI command */
            u_int8_t    target;                 /* target ID */
            u_int8_t    lun;                    /* LUN */
            u_int8_t    bus;                    /* SCSI bus number */
            u_int8_t    priority;               /* only 0 used */
            u_int32_t   sense_len;              /* sense data length */
            void        *sense_data;            /* sense data addr. */
            u_int32_t   link_p;                 /* linked cmds (not supp.) */
        } raw;                                  /* raw service cmd. struct. */
    } u;
    u_int8_t            data[GDT_SCRATCH_SZ];
    int                 complete_flag;
    TAILQ_ENTRY(gdt_ucmd) links;
} gdt_ucmd_t;

/* struct for GDT_IOCTL_CTRTYPE */
typedef struct gdt_ctrt {
    u_int16_t io_node;
    u_int16_t oem_id;
    u_int16_t type;
    u_int32_t info;
    u_int8_t  access;
    u_int8_t  remote;
    u_int16_t ext_type;
    u_int16_t device_id;
    u_int16_t sub_device_id;
} gdt_ctrt_t;

/* struct for GDT_IOCTL_OSVERS */
typedef struct gdt_osv {
    u_int8_t  oscode;
    u_int8_t  version;
    u_int8_t  subversion;
    u_int16_t revision;
    char      name[64];
} gdt_osv_t;

/* OEM */
#define GDT_OEM_VERSION     0x00
#define GDT_OEM_BUFSIZE     0x0c
typedef struct {
    u_int32_t ctl_version;
    u_int32_t file_major_version;
    u_int32_t file_minor_version;
    u_int32_t buffer_size;
    u_int32_t cpy_count;
    u_int32_t ext_error;
    u_int32_t oem_id;
    u_int32_t board_id;
} gdt_oem_param_t;

typedef struct {
    char      product_0_1_name[16];
    char      product_4_5_name[16];
    char      product_cluster_name[16];
    char      product_reserved[16];
    char      scsi_cluster_target_vendor_id[16];
    char      cluster_raid_fw_name[16];
    char      oem_brand_name[16];
    char      oem_raid_type[16];
    char      bios_type[13];
    char      bios_title[50];
    char      oem_company_name[37];
    u_int32_t pci_id_1;
    u_int32_t pci_id_2;
    char      validation_status[80];
    char      reserved_1[4];
    char      scsi_host_drive_inquiry_vendor_id[16];
    char      library_file_template[32];
    char      tool_name_1[32];
    char      tool_name_2[32];
    char      tool_name_3[32];
    char      oem_contact_1[84];
    char      oem_contact_2[84];
    char      oem_contact_3[84];
} gdt_oem_record_t;

typedef struct {
    gdt_oem_param_t  parameters;
    gdt_oem_record_t text;
} gdt_oem_str_record_t;


/* controller event structure */
#define GDT_ES_ASYNC    1
#define GDT_ES_DRIVER   2
#define GDT_ES_TEST     3
#define GDT_ES_SYNC     4
typedef struct {
    u_int16_t           size;               /* size of structure */
    union {
        char            stream[16];
        struct {
            u_int16_t   ionode;
            u_int16_t   service;
            u_int32_t   index;
        } driver;
        struct {
            u_int16_t   ionode;
            u_int16_t   service;
            u_int16_t   status;
            u_int32_t   info;
            u_int8_t    scsi_coord[3];
        } async;
        struct {
            u_int16_t   ionode;
            u_int16_t   service;
            u_int16_t   status;
            u_int32_t   info;
            u_int16_t   hostdrive;
            u_int8_t    scsi_coord[3];
            u_int8_t    sense_key;
        } sync;
        struct {
            u_int32_t   l1, l2, l3, l4;
        } test;
    } eu;
    u_int32_t           severity;
    u_int8_t            event_string[256];          
} gdt_evt_data;

/* dvrevt structure */
typedef struct {
    u_int32_t           first_stamp;
    u_int32_t           last_stamp;
    u_int16_t           same_count;
    u_int16_t           event_source;
    u_int16_t           event_idx;
    u_int8_t            application;
    u_int8_t            reserved;
    gdt_evt_data        event_data;
} gdt_evt_str;

/* struct for GDT_IOCTL_EVENT */
typedef struct gdt_event {
    int erase;
    int handle;
    gdt_evt_str dvr;
} gdt_event_t;

/* struct for GDT_IOCTL_STATIST */
typedef struct gdt_statist {
    u_int16_t io_count_act;
    u_int16_t io_count_max;
    u_int16_t req_queue_act;
    u_int16_t req_queue_max;
    u_int16_t cmd_index_act;
    u_int16_t cmd_index_max;
    u_int16_t sg_count_act;
    u_int16_t sg_count_max;
} gdt_statist_t;

#pragma pack()

/* Context structure for interrupt services */
struct gdt_intr_ctx {
    u_int32_t info, info2;
    u_int16_t cmd_status, service;
    u_int8_t istatus;
};

/* softc structure */
struct gdt_softc {
    device_t sc_devnode;
    struct mtx sc_lock;
    int sc_hanum;
    int sc_class;               /* Controller class */
#define GDT_MPR         0x05
#define GDT_CLASS_MASK  0x07
#define GDT_FC          0x10
#define GDT_CLASS(gdt)  ((gdt)->sc_class & GDT_CLASS_MASK)
    int sc_bus, sc_slot;
    u_int16_t sc_vendor;
    u_int16_t sc_device, sc_subdevice;
    u_int16_t sc_fw_vers;
    int sc_init_level;
    int sc_state;
#define GDT_NORMAL      0x00
#define GDT_POLLING     0x01
#define GDT_SHUTDOWN    0x02
#define GDT_POLL_WAIT   0x80
    struct cdev *sc_dev;
    struct resource *sc_dpmem;
    bus_dma_tag_t sc_parent_dmat;
    bus_dma_tag_t sc_buffer_dmat;
    bus_dma_tag_t sc_gcscratch_dmat;
    bus_dmamap_t sc_gcscratch_dmamap;
    bus_addr_t sc_gcscratch_busbase;

    struct gdt_ccb *sc_gccbs;
    u_int8_t  *sc_gcscratch;
    SLIST_HEAD(, gdt_ccb) sc_free_gccb, sc_pending_gccb;
    TAILQ_HEAD(, ccb_hdr) sc_ccb_queue;
    TAILQ_HEAD(, gdt_ucmd) sc_ucmd_queue;

    u_int16_t sc_ic_all_size;
    u_int16_t sc_cmd_off;
    u_int16_t sc_cmd_cnt;

    u_int32_t sc_info;
    u_int32_t sc_info2;
    u_int16_t sc_status;
    u_int16_t sc_service;

    u_int8_t sc_bus_cnt;
    u_int8_t sc_virt_bus;
    u_int8_t sc_bus_id[GDT_MAXBUS];
    u_int8_t sc_more_proc;

    struct {
        u_int8_t hd_present;
        u_int8_t hd_is_logdrv;
        u_int8_t hd_is_arraydrv;
        u_int8_t hd_is_master;
        u_int8_t hd_is_parity;
        u_int8_t hd_is_hotfix;
        u_int8_t hd_master_no;
        u_int8_t hd_lock;
        u_int8_t hd_heads;
        u_int8_t hd_secs;
        u_int16_t hd_devtype;
        u_int32_t hd_size;
        u_int8_t hd_ldr_no;
        u_int8_t hd_rw_attribs;
        u_int32_t hd_start_sec;
    } sc_hdr[GDT_MAX_HDRIVES];

    u_int16_t sc_raw_feat;
    u_int16_t sc_cache_feat;

    gdt_evt_data sc_dvr;
    char oem_name[8];

    struct cam_sim *sims[GDT_MAXBUS];
    struct cam_path *paths[GDT_MAXBUS];

    void (*sc_copy_cmd)(struct gdt_softc *, struct gdt_ccb *);
    u_int8_t (*sc_get_status)(struct gdt_softc *);
    void (*sc_intr)(struct gdt_softc *, struct gdt_intr_ctx *);
    void (*sc_release_event)(struct gdt_softc *);
    void (*sc_set_sema0)(struct gdt_softc *);
    int (*sc_test_busy)(struct gdt_softc *);

    TAILQ_ENTRY(gdt_softc) links;
};

/*
 * A command control block, one for each corresponding command index of the
 * controller.
 */
struct gdt_ccb {
    u_int8_t    *gc_scratch;
    bus_addr_t  gc_scratch_busbase;
    union ccb   *gc_ccb;
    gdt_ucmd_t  *gc_ucmd;
    bus_dmamap_t gc_dmamap;
    struct callout gc_timeout;
    int         gc_map_flag;
    u_int8_t    gc_service;
    u_int8_t    gc_cmd_index;
    u_int8_t    gc_flags;
#define GDT_GCF_UNUSED          0       
#define GDT_GCF_INTERNAL        1
#define GDT_GCF_SCREEN          2
#define GDT_GCF_SCSI            3
#define GDT_GCF_IOCTL           4
    u_int16_t	gc_cmd_len;
    u_int8_t	gc_cmd[GDT_CMD_SZ];
    SLIST_ENTRY(gdt_ccb) sle;
};


int     iir_init(struct gdt_softc *);
void    iir_free(struct gdt_softc *);
void    iir_attach(struct gdt_softc *);
void    iir_intr(void *arg);

#ifdef __CC_SUPPORTS___INLINE__
/* These all require correctly aligned buffers */
static __inline__ void gdt_enc16(u_int8_t *, u_int16_t);
static __inline__ void gdt_enc32(u_int8_t *, u_int32_t);
static __inline__ u_int16_t gdt_dec16(u_int8_t *);
static __inline__ u_int32_t gdt_dec32(u_int8_t *);

static __inline__ void
gdt_enc16(u_int8_t *addr, u_int16_t value)
{
        *(u_int16_t *)addr = htole16(value);
}

static __inline__ void
gdt_enc32(u_int8_t *addr, u_int32_t value)
{
        *(u_int32_t *)addr = htole32(value);
}

static __inline__ u_int16_t
gdt_dec16(u_int8_t *addr)
{
        return le16toh(*(u_int16_t *)addr);
}

static __inline__ u_int32_t
gdt_dec32(u_int8_t *addr)
{
        return le32toh(*(u_int32_t *)addr);
}
#endif

extern u_int8_t gdt_polling;

struct cdev *gdt_make_dev(struct gdt_softc *gdt);
void    gdt_destroy_dev(struct cdev *dev);
void    gdt_next(struct gdt_softc *gdt);
void gdt_free_ccb(struct gdt_softc *gdt, struct gdt_ccb *gccb);

void gdt_store_event(u_int16_t source, u_int16_t idx,
                             gdt_evt_data *evt);
int gdt_read_event(int handle, gdt_evt_str *estr);
void gdt_readapp_event(u_int8_t app, gdt_evt_str *estr);
void gdt_clear_events(void);

#endif
