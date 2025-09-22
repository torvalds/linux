/*	$OpenBSD: gdtreg.h,v 1.4 2006/05/07 23:18:59 marco Exp $	*/

/*
 * Copyright (c) 1999, 2000 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This driver would not have written if it was not for the hardware donations
 * from both ICP-Vortex and Öko.neT.  I want to thank them for their support.
 */

#define GDT_MAXBUS		6	/* XXX Why not 5? */
#define GDT_MAX_HDRIVES		35	/* 5 busses * 7 targets XXX correct? */
#define GDT_MAXID		127	/* Fibre-channel maximum ID */
#define GDT_MAXOFFSETS		128
#define GDT_MAXSG		128	/* Max. s/g elements */
#define GDT_PROTOCOL_VERSION	1
#define GDT_LINUX_OS		8	/* Used for cache optimization */
#define GDT_SCATTER_GATHER	1	/* s/g feature */
#define GDT_SECS32		0x1f	/* round capacity */
#define GDT_LOCALBOARD		0	/* Board node always 0 */
#define GDT_MAXCMDS		124
#define GDT_SECTOR_SIZE		0x200	/* Always 512 bytes for cache devs */

/* DPMEM constants */
#define GDT_MPR_MAGIC		0xc0ffee11
#define GDT_IC_HEADER_BYTES	48
#define GDT_IC_QUEUE_BYTES	4
#define GDT_DPMEM_COMMAND_OFFSET \
    (GDT_IC_HEADER_BYTES + GDT_IC_QUEUE_BYTES * GDT_MAXOFFSETS)

#if 1
/* Geometry constants. XXX probably not needed. */
#define GDT_MAXCYLS		1024
#define GDT_HEADS		64
#define GDT_SECS		32	/* mapping 64*32 */
#define GDT_MEDHEADS		127
#define GDT_MEDSECS		63	/* mapping 127*63 */
#define GDT_BIGHEADS		255
#define GDT_BIGSECS		63	/* mapping 255*63 */
#endif

/* Cache/raw service commands */
#define GDT_INIT	0		/* service initialization */
#define GDT_READ	1		/* read command */
#define GDT_WRITE	2		/* write command */
#define GDT_INFO	3		/* information about devices */
#define GDT_FLUSH	4		/* flush dirty cache buffers */
#define GDT_IOCTL	5		/* ioctl command */
#define GDT_DEVTYPE	9		/* additional information */
#define GDT_MOUNT	10		/* mount cache device */
#define GDT_UNMOUNT	11		/* unmount cache device */
#define GDT_SET_FEAT	12		/* set features (scatter/gather) */
#define GDT_GET_FEAT	13		/* get features */
#define GDT_WRITE_THR	16		/* write through */
#define GDT_READ_THR	17		/* read through */
#define GDT_EXT_INFO	18		/* extended info */
#define GDT_RESET	19		/* controller reset */

/* Additional raw service commands */
#define GDT_RESERVE	14		/* reserve device to raw service */
#define GDT_RELEASE	15		/* release device */
#define GDT_RESERVE_ALL 16		/* reserve all devices */
#define GDT_RELEASE_ALL 17		/* release all devices */
#define GDT_RESET_BUS	18		/* reset bus */
#define GDT_SCAN_START	19		/* start device scan */
#define GDT_SCAN_END	20		/* stop device scan */	

/* IOCTL command defines */
#define GDT_SCSI_DR_INFO	0x00	/* SCSI drive info */
#define GDT_SCSI_CHAN_CNT	0x05	/* SCSI channel count */
#define GDT_SCSI_DR_LIST	0x06	/* SCSI drive list */
#define GDT_SCSI_DEF_CNT	0x15	/* grown/primary defects */
#define GDT_DSK_STATISTICS	0x4b	/* SCSI disk statistics */
#define GDT_IOCHAN_DESC		0x5d	/* description of IO channel */
#define GDT_IOCHAN_RAW_DESC	0x5e	/* description of raw IO channel */

#define GDT_L_CTRL_PATTERN	0x20000000L	/* SCSI IOCTL mask */
#define GDT_ARRAY_INFO		0x12		/* array drive info */
#define GDT_ARRAY_DRV_LIST	0x0f		/* array drive list */
#define GDT_LA_CTRL_PATTERN	0x10000000L	/* array IOCTL mask */
#define GDT_CACHE_DRV_CNT	0x01		/* cache drive count */
#define GDT_CACHE_DRV_LIST	0x02		/* cache drive list */
#define GDT_CACHE_INFO		0x04		/* cache info */
#define GDT_CACHE_CONFIG	0x05		/* cache configuration */
#define GDT_CACHE_DRV_INFO	0x07		/* cache drive info */
#define GDT_BOARD_FEATURES	0x15		/* controller features */
#define GDT_BOARD_INFO		0x28		/* controller info */
#define GDT_HOST_GET		0x10001L	/* get host drive list */
#define GDT_IO_CHANNEL		0x20000L	/* default IO channel */
#define GDT_INVALID_CHANNEL	0xffffL		/* invalid channel */

/* XXX not belonging here */
/* IOCTLs */
#define GDTIOCTL_MASK	    ('J' << 8)
#define GDTIOCTL_GENERAL    (GDTIOCTL_MASK | 0)	/* general IOCTL */
#define GDTIOCTL_DRVERS	    (GDTIOCTL_MASK | 1)	/* get driver version */
#define GDTIOCTL_CTRTYPE    (GDTIOCTL_MASK | 2)	/* get controller type */
#define GDTIOCTL_CTRCNT	    (GDTIOCTL_MASK | 5)	/* get controller count */
#define GDTIOCTL_LOCKDRV    (GDTIOCTL_MASK | 6)	/* lock host drive */
#define GDTIOCTL_LOCKCHN    (GDTIOCTL_MASK | 7)	/* lock channel */
#define GDTIOCTL_EVENT	    (GDTIOCTL_MASK | 8)	/* read controller events */

/* Service errors */
#define GDT_S_OK		1	/* no error */
#define GDT_S_BSY		7	/* controller busy */
#define GDT_S_RAW_SCSI		12	/* raw service: target error */
#define GDT_S_RAW_ILL		0xff	/* raw service: illegal */
#define GDT_S_NO_STATUS		0x1000	/* got no status (driver-generated) */

/* Controller services */
#define GDT_SCSIRAWSERVICE	3
#define GDT_CACHESERVICE	9
#define GDT_SCREENSERVICE	11

/* Scatter/gather element */
#define GDT_SG_PTR		0x00	/* u_int32_t, address */
#define GDT_SG_LEN		0x04	/* u_int32_t, length */
#define GDT_SG_SZ		0x08

/* Cache service command */
#define GDT_CACHE_DEVICENO	0x00	/* u_int16_t, number of cache drive */
#define GDT_CACHE_BLOCKNO	0x02	/* u_int32_t, block number */
#define GDT_CACHE_BLOCKCNT	0x06	/* u_int32_t, block count */
#define GDT_CACHE_DESTADDR	0x0a	/* u_int32_t, dest. addr. (-1: s/g) */
#define GDT_CACHE_SG_CANZ	0x0e	/* u_int32_t, s/g element count */
#define GDT_CACHE_SG_LST	0x12	/* [GDT_MAXSG], s/g list */
#define GDT_CACHE_SZ		(0x12 + GDT_MAXSG * GDT_SG_SZ)

/* Ioctl command */
#define GDT_IOCTL_PARAM_SIZE	0x00	/* u_int16_t, size of buffer */
#define GDT_IOCTL_SUBFUNC	0x02	/* u_int32_t, ioctl function */
#define GDT_IOCTL_CHANNEL	0x06	/* u_int32_t, device */
#define GDT_IOCTL_P_PARAM	0x0a	/* u_int32_t, buffer */
#define GDT_IOCTL_SZ		0x0e

/* Screen service command */
#define GDT_SCREEN_MSG_HANDLE	0x02	/* u_int32_t, message handle */
#define GDT_SCREEN_MSG_ADDR	0x06	/* u_int32_t, message buffer address */
#define GDT_SCREEN_SZ		0x0a

/* Raw service command */
#define GDT_RAW_DIRECTION	0x02	/* u_int32_t, data direction */
#define GDT_RAW_MDISC_TIME	0x06	/* u_int32_t, disc. time (0: none) */
#define GDT_RAW_MCON_TIME	0x0a	/* u_int32_t, conn. time (0: none) */
#define GDT_RAW_SDATA		0x0e	/* u_int32_t, dest. addr. (-1: s/g) */
#define GDT_RAW_SDLEN		0x12	/* u_int32_t, data length */
#define GDT_RAW_CLEN		0x16	/* u_int32_t, SCSI cmd len (6/10/12) */
#define GDT_RAW_CMD		0x1a	/* u_int8_t [12], SCSI command */
#define GDT_RAW_TARGET		0x26	/* u_int8_t, target ID */
#define GDT_RAW_LUN		0x27	/* u_int8_t, LUN */
#define GDT_RAW_BUS		0x28	/* u_int8_t, SCSI bus number */
#define GDT_RAW_PRIORITY	0x29	/* u_int8_t, only 0 used */
#define GDT_RAW_SENSE_LEN	0x2a	/* u_int32_t, sense data length */
#define GDT_RAW_SENSE_DATA	0x2e	/* u_int32_t, sense data address */
#define GDT_RAW_SG_RANZ		0x36	/* u_int32_t, s/g element count */
#define GDT_RAW_SG_LST		0x3a	/* [GDT_MAXSG], s/g list */
#define GDT_RAW_SZ		(0x3e + GDT_MAXSG * GDT_SG_SZ)

/* Command structure */
#define GDT_CMD_BOARDNODE	0x00	/* u_int32_t, board node (always 0) */
#define GDT_CMD_COMMANDINDEX	0x04	/* u_int32_t, command number */
#define GDT_CMD_OPCODE		0x08	/* u_int16_t, opcode (READ, ...) */
#define GDT_CMD_UNION		0x0a	/* cache/screen/raw service command */
#define GDT_CMD_UNION_SZ	GDT_RAW_SZ
#define GDT_CMD_SZ		(0x0a + GDT_CMD_UNION_SZ)

/* Command queue entries */
#define GDT_OFFSET	0x00	/* u_int16_t, command offset in the DP RAM */
#define GDT_SERV_ID	0x02	/* u_int16_t, service */
#define GDT_COMM_Q_SZ	0x04

/* Interface area */
#define GDT_S_CMD_INDX	0x00	/* u_int8_t, special command */
#define	GDT_S_STATUS	0x01	/* volatile u_int8_t, status special command */
#define GDT_S_INFO	0x04	/* u_int32_t [4], add. info special command */
#define GDT_SEMA0	0x14	/* volatile u_int8_t, command semaphore */
#define GDT_CMD_INDEX	0x18	/* u_int8_t, command number */
#define GDT_STATUS	0x1c	/* volatile u_int16_t, command status */
#define GDT_SERVICE	0x1e	/* u_int16_t, service (for asynch. events) */
#define GDT_DPR_INFO	0x20	/* u_int32_t [2], additional info */
#define GDT_COMM_QUEUE	0x28	/* command queue */
#define GDT_DPR_CMD	(0x30 + GDT_MAXOFFSETS * GDT_COMM_Q_SZ)
				/* u_int8_t [], commands */
#define GDT_DPR_IF_SZ	GDT_DPR_CMD

/* I/O channel header */
#define GDT_IOC_VERSION		0x00	/* u_int32_t, version (~0: newest) */
#define GDT_IOC_LIST_ENTRIES	0x04	/* u_int8_t, list entry count */
#define GDT_IOC_FIRST_CHAN	0x05	/* u_int8_t, first channel number */
#define GDT_IOC_LAST_CHAN	0x06	/* u_int8_t, last channel number */
#define GDT_IOC_CHAN_COUNT	0x07	/* u_int8_t, (R) channel count */
#define GDT_IOC_LIST_OFFSET	0x08	/* u_int32_t, offset of list[0] */
#define GDT_IOC_HDR_SZ		0x0c

#define GDT_IOC_NEWEST		0xffffffff	/* goes into GDT_IOC_VERSION */

/* Get I/O channel description */
#define GDT_IOC_ADDRESS		0x00	/* u_int32_t, channel address */
#define GDT_IOC_TYPE		0x04	/* u_int8_t, type (SCSI/FCSL) */
#define GDT_IOC_LOCAL_NO	0x05	/* u_int8_t, local number */
#define GDT_IOC_FEATURES	0x06	/* u_int16_t, channel features */
#define GDT_IOC_SZ		0x08

/* Get raw I/O channel description */
#define GDT_RAWIOC_PROC_ID	0x00	/* u_int8_t, processor id */
#define GDT_RAWIOC_PROC_DEFECT	0x01	/* u_int8_t, defect? */
#define GDT_RAWIOC_SZ		0x04

/* Get SCSI channel count */
#define GDT_GETCH_CHANNEL_NO	0x00	/* u_int32_t, channel number */
#define GDT_GETCH_DRIVE_CNT	0x04	/* u_int32_t, drive count */
#define GDT_GETCH_SIOP_ID	0x08	/* u_int8_t, SCSI processor ID */
#define GDT_GETCH_SIOP_STATE	0x09	/* u_int8_t, SCSI processor state */
#define GDT_GETCH_SZ		0x0a

/* Get SCSI drive numbers */
#define GDT_GETSCSI_CHAN	0x00	/* u_int32_t, scsi channel number */
#define GDT_GETSCSI_CNT		0x04	/* u_int32_t, nr of entries */
#define GDT_GETSCSI_LIST	0x08	/* u_int32_t, minor device nr */
#define GDT_GETSCSI_LIST_SZ	0x04
#define GDT_GETSCSI_SZ		(GDT_GETSCSI_LIST_SZ * GDT_MAXID)

/* Cache info/config IOCTL structures */
#define GDT_CPAR_VERSION	0x00	/* u_int32_t, firmware version */
#define GDT_CPAR_STATE		0x04	/* u_int16_t, cache state (on/off) */
#define GDT_CPAR_STRATEGY	0x06	/* u_int16_t, cache strategy */
#define GDT_CPAR_WRITE_BACK	0x08	/* u_int16_t, write back (on/off) */
#define GDT_CPAR_BLOCK_SIZE	0x0a	/* u_int16_t, cache block size */
#define GDT_CPAR_SZ		0x0c

#define GDT_CSTAT_CSIZE		0x00	/* u_int32_t, cache size */
#define GDT_CSTAT_READ_CNT	0x04	/* u_int32_t, read counter */
#define GDT_CSTAT_WRITE_CNT	0x08	/* u_int32_t, write counter */
#define GDT_CSTAT_TR_HITS	0x0c	/* u_int32_t, track hits */
#define GDT_CSTAT_SEC_HITS	0x10	/* u_int32_t, sector hits */
#define GDT_CSTAT_SEC_MISS	0x14	/* u_int32_t, sector misses */
#define GDT_CSTAT_SZ		0x18

/* Get cache info */
#define GDT_CINFO_CPAR		0x00
#define GDT_CINFO_CSTAT		GDT_CPAR_SZ
#define GDT_CINFO_SZ		(GDT_CPAR_SZ + GDT_CSTAT_SZ)

/* Get board info */
#define GDT_BINFO_SER_NO	0x00	/* u_int32_t, serial number */
#define GDT_BINFO_OEM_ID	0x04	/* u_int8_t [2], OEM ID */
#define GDT_BINFO_EP_FLAGS	0x06	/* u_int16_t, eprom flags */
#define GDT_BINFO_PROC_ID	0x08	/* u_int32_t, processor ID */
#define GDT_BINFO_MEMSIZE	0x0c	/* u_int32_t, memory size (bytes) */
#define GDT_BINFO_MEM_BANKS	0x10	/* u_int8_t, memory banks */
#define GDT_BINFO_CHAN_TYPE	0x11	/* u_int8_t, channel type */
#define GDT_BINFO_CHAN_COUNT	0x12	/* u_int8_t, channel count */
#define GDT_BINFO_RDONGLE_PRES	0x13	/* u_int8_t, dongle present */
#define GDT_BINFO_EPR_FW_VER	0x14	/* u_int32_t, (eprom) firmware ver */
#define GDT_BINFO_UPD_FW_VER	0x18	/* u_int32_t, (update) firmware ver */
#define GDT_BINFO_UPD_REVISION	0x1c	/* u_int32_t, update revision */
#define GDT_BINFO_TYPE_STRING	0x20	/* char [16], controller name */
#define GDT_BINFO_RAID_STRING	0x30	/* char [16], RAID firmware name */
#define GDT_BINFO_UPDATE_PRES	0x40	/* u_int8_t, update present? */
#define GDT_BINFO_XOR_PRES	0x41	/* u_int8_t, XOR engine present */
#define GDT_BINFO_PROM_TYPE	0x42	/* u_int8_t, ROM type (eprom/flash) */
#define GDT_BINFO_PROM_COUNT	0x43	/* u_int8_t, number of ROM devices */
#define GDT_BINFO_DUP_PRES	0x44	/* u_int32_t, duplexing module pres? */
#define GDT_BINFO_CHAN_PRES	0x48	/* u_int32_t, # of exp. channels */
#define GDT_BINFO_MEM_PRES	0x4c	/* u_int32_t, memory expansion inst? */
#define GDT_BINFO_FT_BUS_SYSTEM	0x50	/* u_int8_t, fault bus supported? */
#define GDT_BINFO_SUBTYPE_VALID	0x51	/* u_int8_t, board_subtype valid */
#define GDT_BINFO_BOARD_SUBTYPE	0x52	/* u_int8_t, subtype/hardware level */
#define GDT_BINFO_RAMPAR_PRES	0x53	/* u_int8_t, RAM parity check hw? */
#define GDT_BINFO_SZ		0x54

/* Get board features */
#define GDT_BFEAT_CHAINING	0x00	/* u_int8_t, chaining supported */
#define GDT_BFEAT_STRIPING	0x01	/* u_int8_t, striping (RAID-0) supp. */
#define GDT_BFEAT_MIRRORING	0x02	/* u_int8_t, mirroring (RAID-1) supp */
#define GDT_BFEAT_RAID		0x03	/* u_int8_t, RAID-4/5/10 supported */
#define GDT_BFEAT_SZ		0x04

/* Other defines */
#define GDT_ASYNCINDEX	0	/* command index asynchronous event */
#define GDT_SPEZINDEX	1	/* command index unknown service */
