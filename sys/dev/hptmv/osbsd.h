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
#ifndef _OSBSD_H_
#define _OSBSD_H_

#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/eventhandler.h>
#include <sys/devicestat.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/bus.h>

#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_periph.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>



typedef struct 
{
	UCHAR		status;     		/* 0 nonbootable; 80h bootable */
	UCHAR      	start_head;
	USHORT     	start_sector;
	UCHAR      	type;
	UCHAR      	end_head;
	USHORT     	end_sector;
	ULONG      	start_abs_sector;
	ULONG      	num_of_sector;
} partition;

typedef struct _INQUIRYDATA {
	UCHAR DeviceType : 5;
	UCHAR DeviceTypeQualifier : 3;
	UCHAR DeviceTypeModifier : 7;
	UCHAR RemovableMedia : 1;
	UCHAR Versions;
	UCHAR ResponseDataFormat;
	UCHAR AdditionalLength;
	UCHAR Reserved[2];
	UCHAR SoftReset : 1;
	UCHAR CommandQueue : 1;
	UCHAR Reserved2 : 1;
	UCHAR LinkedCommands : 1;
	UCHAR Synchronous : 1;
	UCHAR Wide16Bit : 1;
	UCHAR Wide32Bit : 1;
	UCHAR RelativeAddressing : 1;
	UCHAR VendorId[8];
	UCHAR ProductId[16];
	UCHAR ProductRevisionLevel[4];
	UCHAR VendorSpecific[20];
	UCHAR Reserved3[40];
} INQUIRYDATA, *PINQUIRYDATA;

#define MV_IAL_HT_SACOALT_DEFAULT	1
#define MV_IAL_HT_SAITMTH_DEFAULT	1

/****************************************/
/*          GENERAL Definitions         */
/****************************************/

/* Bits for HD_ERROR */
#define NM_ERR			0x02	/* media present */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR         0x08	/* media change request */
#define IDNF_ERR        0x10	/* ID field not found */
#define MC_ERR          0x20	/* media changed */
#define UNC_ERR         0x40	/* Uncorrect data */
#define WP_ERR          0x40	/* write protect */
#define ICRC_ERR        0x80	/* new meaning:  CRC error during transfer */

#define REQUESTS_ARRAY_SIZE			(9 * MV_EDMA_REQUEST_QUEUE_SIZE) /* 9 K bytes */
#define RESPONSES_ARRAY_SIZE		(12 * MV_EDMA_RESPONSE_QUEUE_SIZE) /* 3 K bytes */

#define PRD_ENTRIES_PER_CMD         (MAX_SG_DESCRIPTORS+1)
#define PRD_ENTRIES_SIZE            (MV_EDMA_PRD_ENTRY_SIZE*PRD_ENTRIES_PER_CMD)
#define PRD_TABLES_FOR_VBUS         (MV_SATA_CHANNELS_NUM*MV_EDMA_QUEUE_LENGTH)

typedef enum _SataEvent
{
    SATA_EVENT_NO_CHANGE = 0,
	SATA_EVENT_CHANNEL_CONNECTED,
	SATA_EVENT_CHANNEL_DISCONNECTED
} SATA_EVENT;

typedef ULONG_PTR dma_addr_t;

typedef struct _MV_CHANNEL
{
	unsigned int		maxUltraDmaModeSupported;
	unsigned int		maxDmaModeSupported;
	unsigned int		maxPioModeSupported;
	MV_BOOLEAN		online;
	MV_BOOLEAN		writeCacheSupported;
	MV_BOOLEAN		writeCacheEnabled;
	MV_BOOLEAN		readAheadSupported;
	MV_BOOLEAN		readAheadEnabled;
	MV_U8			queueDepth;
	
} MV_CHANNEL;

typedef struct _BUS_DMAMAP
{	struct _BUS_DMAMAP 	 	*next;
	struct IALAdapter 			*pAdapter;
	bus_dmamap_t 			dma_map;
	struct callout		timeout;
	SCAT_GATH				psg[MAX_SG_DESCRIPTORS];
} BUS_DMAMAP, *PBUS_DMAMAP;

typedef struct IALAdapter 
{
	struct cam_path 	*path;
	struct mtx		lock;

	bus_dma_tag_t	  io_dma_parent; /* I/O buffer DMA tag */
	PBUS_DMAMAP	  pbus_dmamap_list;
	PBUS_DMAMAP	  pbus_dmamap;

	device_t			hpt_dev;				/* bus device */
	struct resource		*hpt_irq;					/* interrupt */
	struct resource		*mem_res;
	void				*hpt_intr;				/* interrupt handle */
	struct IALAdapter   *next;

	MV_SATA_ADAPTER     mvSataAdapter;
	MV_CHANNEL          mvChannel[MV_SATA_CHANNELS_NUM];
	MV_U8				*requestsArrayBaseAddr;
	MV_U8				*requestsArrayBaseAlignedAddr;
	dma_addr_t			requestsArrayBaseDmaAddr;
	dma_addr_t			requestsArrayBaseDmaAlignedAddr;
	MV_U8				*responsesArrayBaseAddr;
	MV_U8				*responsesArrayBaseAlignedAddr;
	dma_addr_t			responsesArrayBaseDmaAddr;
	dma_addr_t			responsesArrayBaseDmaAlignedAddr;
	SATA_EVENT			sataEvents[MV_SATA_CHANNELS_NUM];
	
   	struct	callout event_timer_connect;
  	struct	callout event_timer_disconnect;

	struct _VBus        VBus;
	struct _VDevice     VDevices[MV_SATA_CHANNELS_NUM];
	PCommand			pCommandBlocks;
	PUCHAR				prdTableAddr;
	PUCHAR				prdTableAlignedAddr;
	void*				pFreePRDLink;

	union ccb           *pending_Q;

	MV_U8   			outstandingCommands;

	UCHAR               status;
	UCHAR               ver_601;
	UCHAR               beeping;

	eventhandler_tag	eh;
}
IAL_ADAPTER_T;

extern IAL_ADAPTER_T *gIal_Adapter;

/*entry.c*/
typedef void (*HPT_DPC)(IAL_ADAPTER_T *,void*,UCHAR);

int hpt_queue_dpc(HPT_DPC dpc, IAL_ADAPTER_T *pAdapter, void *arg, UCHAR flags);
void hpt_rebuild_data_block(IAL_ADAPTER_T *pAdapter, PVDevice pArray, UCHAR flags);
void Check_Idle_Call(IAL_ADAPTER_T *pAdapter);
void fRescanAllDevice(_VBUS_ARG0);
int hpt_add_disk_to_array(_VBUS_ARG DEVICEID idArray, DEVICEID idDisk);

int Kernel_DeviceIoControl(_VBUS_ARG
							DWORD dwIoControlCode,       	/* operation control code */
							PVOID lpInBuffer,            	/* input data buffer */
							DWORD nInBufferSize,         	/* size of input data buffer */
							PVOID lpOutBuffer,           	/* output data buffer */
							DWORD nOutBufferSize,        	/* size of output data buffer */
							PDWORD lpBytesReturned      	/* byte count */
						);


#define __str_direct(x) #x
#define __str(x) __str_direct(x)
#define KMSG_LEADING __str(PROC_DIR_NAME) ": "
#define hpt_printk(_x_) do { printf(KMSG_LEADING); printf _x_ ; } while (0)

#define DUPLICATE      0
#define INITIALIZE     1
#define REBUILD_PARITY 2
#define VERIFY         3

/***********************************************************/

static __inline struct cam_periph *
hpt_get_periph(int path_id,int target_id)
{
	struct cam_periph	*periph = NULL;
    struct cam_path	*path;
    int			status;

    status = xpt_create_path(&path, NULL, path_id, target_id, 0);
    if (status == CAM_REQ_CMP) {
		periph = cam_periph_find(path, "da");
		xpt_free_path(path);
			
    }
	return periph;	
}

#ifdef __i386__
#define BITS_PER_LONG 32
#define VDEV_TO_ID(pVDev) (DEVICEID)(pVDev)
#define ID_TO_VDEV(id) (PVDevice)(id)
#else /*Only support x86_64(AMD64 and EM64T)*/
#define BITS_PER_LONG 64
#define VDEV_TO_ID(pVDev) (DEVICEID)(ULONG_PTR)(pVDev)
#define ID_TO_VDEV(id) (PVDevice)(((ULONG_PTR)gIal_Adapter & 0xffffffff00000000) | (id))
#endif

#define INVALID_DEVICEID		(-1)
#define INVALID_STRIPSIZE		(-1)

#define shortswap(w) ((WORD)((w)>>8 | ((w) & 0xFF)<<8))

#ifndef MinBlockSizeShift
#define MinBlockSizeShift 5
#define MaxBlockSizeShift 12
#endif

#pragma pack(1)
typedef struct _HPT_IOCTL_TRANSFER_PARAM
{
	ULONG nInBufferSize;
	ULONG nOutBufferSize;
	UCHAR buffer[0];
}HPT_IOCTL_TRANSFER_PARAM, *PHPT_IOCTL_TRANSFER_PARAM;

typedef struct _HPT_SET_STATE_PARAM
{
	DEVICEID idArray;
	DWORD state;
} HPT_SET_STATE_PARAM, *PHPT_SET_STATE_PARAM;

typedef struct _HPT_SET_ARRAY_INFO
{
	DEVICEID idArray;
	ALTERABLE_ARRAY_INFO Info;
} HPT_SET_ARRAY_INFO, *PHPT_SET_ARRAY_INFO;

typedef struct _HPT_SET_DEVICE_INFO
{
	DEVICEID idDisk;
	ALTERABLE_DEVICE_INFO Info;
} HPT_SET_DEVICE_INFO, *PHPT_SET_DEVICE_INFO;

typedef struct _HPT_SET_DEVICE_INFO_V2
{
	DEVICEID idDisk;
	ALTERABLE_DEVICE_INFO_V2 Info;
} HPT_SET_DEVICE_INFO_V2, *PHPT_SET_DEVICE_INFO_V2;

typedef struct _HPT_ADD_DISK_TO_ARRAY
{
	DEVICEID idArray;
	DEVICEID idDisk;
} HPT_ADD_DISK_TO_ARRAY, *PHPT_ADD_DISK_TO_ARRAY;

typedef struct _HPT_DEVICE_IO
{
	DEVICEID	id;
	int			cmd;
	ULONG		lba;
	DWORD		nSector;
	UCHAR		buffer[0];
} HPT_DEVICE_IO, *PHPT_DEVICE_IO;

int check_VDevice_valid(PVDevice);
int hpt_default_ioctl(_VBUS_ARG DWORD, PVOID, DWORD, PVOID, DWORD, PDWORD);

#define HPT_NULL_ID 0

#pragma pack()

#endif
