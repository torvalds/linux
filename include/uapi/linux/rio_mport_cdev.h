/*
 * Copyright (c) 2015-2016, Integrated Device Technology Inc.
 * Copyright (c) 2015, Prodrive Technologies
 * Copyright (c) 2015, Texas Instruments Incorporated
 * Copyright (c) 2015, RapidIO Trade Association
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License(GPL) Version 2, or the BSD-3 Clause license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _RIO_MPORT_CDEV_H_
#define _RIO_MPORT_CDEV_H_

#include <linux/ioctl.h>
#include <linux/types.h>

struct rio_mport_maint_io {
	__u16 rioid;		/* destID of remote device */
	__u8  hopcount;		/* hopcount to remote device */
	__u8  pad0[5];
	__u32 offset;		/* offset in register space */
	__u32 length;		/* length in bytes */
	__u64 buffer;		/* pointer to data buffer */
};

/*
 * Definitions for RapidIO data transfers:
 * - memory mapped (MAPPED)
 * - packet generation from memory (TRANSFER)
 */
#define RIO_TRANSFER_MODE_MAPPED	(1 << 0)
#define RIO_TRANSFER_MODE_TRANSFER	(1 << 1)
#define RIO_CAP_DBL_SEND		(1 << 2)
#define RIO_CAP_DBL_RECV		(1 << 3)
#define RIO_CAP_PW_SEND			(1 << 4)
#define RIO_CAP_PW_RECV			(1 << 5)
#define RIO_CAP_MAP_OUTB		(1 << 6)
#define RIO_CAP_MAP_INB			(1 << 7)

struct rio_mport_properties {
	__u16 hdid;
	__u8  id;			/* Physical port ID */
	__u8  index;
	__u32 flags;
	__u32 sys_size;		/* Default addressing size */
	__u8  port_ok;
	__u8  link_speed;
	__u8  link_width;
	__u8  pad0;
	__u32 dma_max_sge;
	__u32 dma_max_size;
	__u32 dma_align;
	__u32 transfer_mode;		/* Default transfer mode */
	__u32 cap_sys_size;		/* Capable system sizes */
	__u32 cap_addr_size;		/* Capable addressing sizes */
	__u32 cap_transfer_mode;	/* Capable transfer modes */
	__u32 cap_mport;		/* Mport capabilities */
};

/*
 * Definitions for RapidIO events;
 * - incoming port-writes
 * - incoming doorbells
 */
#define RIO_DOORBELL	(1 << 0)
#define RIO_PORTWRITE	(1 << 1)

struct rio_doorbell {
	__u16 rioid;
	__u16 payload;
};

struct rio_doorbell_filter {
	__u16 rioid;	/* Use RIO_INVALID_DESTID to match all ids */
	__u16 low;
	__u16 high;
	__u16 pad0;
};


struct rio_portwrite {
	__u32 payload[16];
};

struct rio_pw_filter {
	__u32 mask;
	__u32 low;
	__u32 high;
	__u32 pad0;
};

/* RapidIO base address for inbound requests set to value defined below
 * indicates that no specific RIO-to-local address translation is requested
 * and driver should use direct (one-to-one) address mapping.
*/
#define RIO_MAP_ANY_ADDR	(__u64)(~((__u64) 0))

struct rio_mmap {
	__u16 rioid;
	__u16 pad0[3];
	__u64 rio_addr;
	__u64 length;
	__u64 handle;
	__u64 address;
};

struct rio_dma_mem {
	__u64 length;		/* length of DMA memory */
	__u64 dma_handle;	/* handle associated with this memory */
	__u64 address;
};

struct rio_event {
	__u32 header;	/* event type RIO_DOORBELL or RIO_PORTWRITE */
	union {
		struct rio_doorbell doorbell;	/* header for RIO_DOORBELL */
		struct rio_portwrite portwrite; /* header for RIO_PORTWRITE */
	} u;
	__u32 pad0;
};

enum rio_transfer_sync {
	RIO_TRANSFER_SYNC,	/* synchronous transfer */
	RIO_TRANSFER_ASYNC,	/* asynchronous transfer */
	RIO_TRANSFER_FAF,	/* fire-and-forget transfer */
};

enum rio_transfer_dir {
	RIO_TRANSFER_DIR_READ,	/* Read operation */
	RIO_TRANSFER_DIR_WRITE,	/* Write operation */
};

/*
 * RapidIO data exchange transactions are lists of individual transfers. Each
 * transfer exchanges data between two RapidIO devices by remote direct memory
 * access and has its own completion code.
 *
 * The RapidIO specification defines four types of data exchange requests:
 * NREAD, NWRITE, SWRITE and NWRITE_R. The RapidIO DMA channel interface allows
 * to specify the required type of write operation or combination of them when
 * only the last data packet requires response.
 *
 * NREAD:    read up to 256 bytes from remote device memory into local memory
 * NWRITE:   write up to 256 bytes from local memory to remote device memory
 *           without confirmation
 * SWRITE:   as NWRITE, but all addresses and payloads must be 64-bit aligned
 * NWRITE_R: as NWRITE, but expect acknowledgment from remote device.
 *
 * The default exchange is chosen from NREAD and any of the WRITE modes as the
 * driver sees fit. For write requests the user can explicitly choose between
 * any of the write modes for each transaction.
 */
enum rio_exchange {
	RIO_EXCHANGE_DEFAULT,	/* Default method */
	RIO_EXCHANGE_NWRITE,	/* All packets using NWRITE */
	RIO_EXCHANGE_SWRITE,	/* All packets using SWRITE */
	RIO_EXCHANGE_NWRITE_R,	/* Last packet NWRITE_R, others NWRITE */
	RIO_EXCHANGE_SWRITE_R,	/* Last packet NWRITE_R, others SWRITE */
	RIO_EXCHANGE_NWRITE_R_ALL, /* All packets using NWRITE_R */
};

struct rio_transfer_io {
	__u64 rio_addr;	/* Address in target's RIO mem space */
	__u64 loc_addr;
	__u64 handle;
	__u64 offset;	/* Offset in buffer */
	__u64 length;	/* Length in bytes */
	__u16 rioid;	/* Target destID */
	__u16 method;	/* Data exchange method, one of rio_exchange enum */
	__u32 completion_code;	/* Completion code for this transfer */
};

struct rio_transaction {
	__u64 block;	/* Pointer to array of <count> transfers */
	__u32 count;	/* Number of transfers */
	__u32 transfer_mode;	/* Data transfer mode */
	__u16 sync;	/* Synch method, one of rio_transfer_sync enum */
	__u16 dir;	/* Transfer direction, one of rio_transfer_dir enum */
	__u32 pad0;
};

struct rio_async_tx_wait {
	__u32 token;	/* DMA transaction ID token */
	__u32 timeout;	/* Wait timeout in msec, if 0 use default TO */
};

#define RIO_MAX_DEVNAME_SZ	20

struct rio_rdev_info {
	__u16 destid;
	__u8 hopcount;
	__u8 pad0;
	__u32 comptag;
	char name[RIO_MAX_DEVNAME_SZ + 1];
};

/* Driver IOCTL codes */
#define RIO_MPORT_DRV_MAGIC           'm'

#define RIO_MPORT_MAINT_HDID_SET	\
	_IOW(RIO_MPORT_DRV_MAGIC, 1, __u16)
#define RIO_MPORT_MAINT_COMPTAG_SET	\
	_IOW(RIO_MPORT_DRV_MAGIC, 2, __u32)
#define RIO_MPORT_MAINT_PORT_IDX_GET	\
	_IOR(RIO_MPORT_DRV_MAGIC, 3, __u32)
#define RIO_MPORT_GET_PROPERTIES \
	_IOR(RIO_MPORT_DRV_MAGIC, 4, struct rio_mport_properties)
#define RIO_MPORT_MAINT_READ_LOCAL \
	_IOR(RIO_MPORT_DRV_MAGIC, 5, struct rio_mport_maint_io)
#define RIO_MPORT_MAINT_WRITE_LOCAL \
	_IOW(RIO_MPORT_DRV_MAGIC, 6, struct rio_mport_maint_io)
#define RIO_MPORT_MAINT_READ_REMOTE \
	_IOR(RIO_MPORT_DRV_MAGIC, 7, struct rio_mport_maint_io)
#define RIO_MPORT_MAINT_WRITE_REMOTE \
	_IOW(RIO_MPORT_DRV_MAGIC, 8, struct rio_mport_maint_io)
#define RIO_ENABLE_DOORBELL_RANGE	\
	_IOW(RIO_MPORT_DRV_MAGIC, 9, struct rio_doorbell_filter)
#define RIO_DISABLE_DOORBELL_RANGE	\
	_IOW(RIO_MPORT_DRV_MAGIC, 10, struct rio_doorbell_filter)
#define RIO_ENABLE_PORTWRITE_RANGE	\
	_IOW(RIO_MPORT_DRV_MAGIC, 11, struct rio_pw_filter)
#define RIO_DISABLE_PORTWRITE_RANGE	\
	_IOW(RIO_MPORT_DRV_MAGIC, 12, struct rio_pw_filter)
#define RIO_SET_EVENT_MASK		\
	_IOW(RIO_MPORT_DRV_MAGIC, 13, __u32)
#define RIO_GET_EVENT_MASK		\
	_IOR(RIO_MPORT_DRV_MAGIC, 14, __u32)
#define RIO_MAP_OUTBOUND \
	_IOWR(RIO_MPORT_DRV_MAGIC, 15, struct rio_mmap)
#define RIO_UNMAP_OUTBOUND \
	_IOW(RIO_MPORT_DRV_MAGIC, 16, struct rio_mmap)
#define RIO_MAP_INBOUND \
	_IOWR(RIO_MPORT_DRV_MAGIC, 17, struct rio_mmap)
#define RIO_UNMAP_INBOUND \
	_IOW(RIO_MPORT_DRV_MAGIC, 18, __u64)
#define RIO_ALLOC_DMA \
	_IOWR(RIO_MPORT_DRV_MAGIC, 19, struct rio_dma_mem)
#define RIO_FREE_DMA \
	_IOW(RIO_MPORT_DRV_MAGIC, 20, __u64)
#define RIO_TRANSFER \
	_IOWR(RIO_MPORT_DRV_MAGIC, 21, struct rio_transaction)
#define RIO_WAIT_FOR_ASYNC \
	_IOW(RIO_MPORT_DRV_MAGIC, 22, struct rio_async_tx_wait)
#define RIO_DEV_ADD \
	_IOW(RIO_MPORT_DRV_MAGIC, 23, struct rio_rdev_info)
#define RIO_DEV_DEL \
	_IOW(RIO_MPORT_DRV_MAGIC, 24, struct rio_rdev_info)

#endif /* _RIO_MPORT_CDEV_H_ */
