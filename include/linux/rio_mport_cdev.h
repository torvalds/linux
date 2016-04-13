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

#ifndef __user
#define __user
#endif

struct rio_mport_maint_io {
	uint32_t rioid;		/* destID of remote device */
	uint32_t hopcount;	/* hopcount to remote device */
	uint32_t offset;	/* offset in register space */
	size_t length;		/* length in bytes */
	void __user *buffer;	/* data buffer */
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
	uint16_t hdid;
	uint8_t id;			/* Physical port ID */
	uint8_t  index;
	uint32_t flags;
	uint32_t sys_size;		/* Default addressing size */
	uint8_t  port_ok;
	uint8_t  link_speed;
	uint8_t  link_width;
	uint32_t dma_max_sge;
	uint32_t dma_max_size;
	uint32_t dma_align;
	uint32_t transfer_mode;		/* Default transfer mode */
	uint32_t cap_sys_size;		/* Capable system sizes */
	uint32_t cap_addr_size;		/* Capable addressing sizes */
	uint32_t cap_transfer_mode;	/* Capable transfer modes */
	uint32_t cap_mport;		/* Mport capabilities */
};

/*
 * Definitions for RapidIO events;
 * - incoming port-writes
 * - incoming doorbells
 */
#define RIO_DOORBELL	(1 << 0)
#define RIO_PORTWRITE	(1 << 1)

struct rio_doorbell {
	uint32_t rioid;
	uint16_t payload;
};

struct rio_doorbell_filter {
	uint32_t rioid;			/* 0xffffffff to match all ids */
	uint16_t low;
	uint16_t high;
};


struct rio_portwrite {
	uint32_t payload[16];
};

struct rio_pw_filter {
	uint32_t mask;
	uint32_t low;
	uint32_t high;
};

/* RapidIO base address for inbound requests set to value defined below
 * indicates that no specific RIO-to-local address translation is requested
 * and driver should use direct (one-to-one) address mapping.
*/
#define RIO_MAP_ANY_ADDR	(uint64_t)(~((uint64_t) 0))

struct rio_mmap {
	uint32_t rioid;
	uint64_t rio_addr;
	uint64_t length;
	uint64_t handle;
	void *address;
};

struct rio_dma_mem {
	uint64_t length;		/* length of DMA memory */
	uint64_t dma_handle;		/* handle associated with this memory */
	void *buffer;			/* pointer to this memory */
};


struct rio_event {
	unsigned int header;	/* event type RIO_DOORBELL or RIO_PORTWRITE */
	union {
		struct rio_doorbell doorbell;	/* header for RIO_DOORBELL */
		struct rio_portwrite portwrite; /* header for RIO_PORTWRITE */
	} u;
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
	uint32_t rioid;			/* Target destID */
	uint64_t rio_addr;		/* Address in target's RIO mem space */
	enum rio_exchange method;	/* Data exchange method */
	void __user *loc_addr;
	uint64_t handle;
	uint64_t offset;		/* Offset in buffer */
	uint64_t length;		/* Length in bytes */
	uint32_t completion_code;	/* Completion code for this transfer */
};

struct rio_transaction {
	uint32_t transfer_mode;		/* Data transfer mode */
	enum rio_transfer_sync sync;	/* Synchronization method */
	enum rio_transfer_dir dir;	/* Transfer direction */
	size_t count;			/* Number of transfers */
	struct rio_transfer_io __user *block;	/* Array of <count> transfers */
};

struct rio_async_tx_wait {
	uint32_t token;		/* DMA transaction ID token */
	uint32_t timeout;	/* Wait timeout in msec, if 0 use default TO */
};

#define RIO_MAX_DEVNAME_SZ	20

struct rio_rdev_info {
	uint32_t destid;
	uint8_t hopcount;
	uint32_t comptag;
	char name[RIO_MAX_DEVNAME_SZ + 1];
};

/* Driver IOCTL codes */
#define RIO_MPORT_DRV_MAGIC           'm'

#define RIO_MPORT_MAINT_HDID_SET	\
	_IOW(RIO_MPORT_DRV_MAGIC, 1, uint16_t)
#define RIO_MPORT_MAINT_COMPTAG_SET	\
	_IOW(RIO_MPORT_DRV_MAGIC, 2, uint32_t)
#define RIO_MPORT_MAINT_PORT_IDX_GET	\
	_IOR(RIO_MPORT_DRV_MAGIC, 3, uint32_t)
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
	_IOW(RIO_MPORT_DRV_MAGIC, 13, unsigned int)
#define RIO_GET_EVENT_MASK		\
	_IOR(RIO_MPORT_DRV_MAGIC, 14, unsigned int)
#define RIO_MAP_OUTBOUND \
	_IOWR(RIO_MPORT_DRV_MAGIC, 15, struct rio_mmap)
#define RIO_UNMAP_OUTBOUND \
	_IOW(RIO_MPORT_DRV_MAGIC, 16, struct rio_mmap)
#define RIO_MAP_INBOUND \
	_IOWR(RIO_MPORT_DRV_MAGIC, 17, struct rio_mmap)
#define RIO_UNMAP_INBOUND \
	_IOW(RIO_MPORT_DRV_MAGIC, 18, uint64_t)
#define RIO_ALLOC_DMA \
	_IOWR(RIO_MPORT_DRV_MAGIC, 19, struct rio_dma_mem)
#define RIO_FREE_DMA \
	_IOW(RIO_MPORT_DRV_MAGIC, 20, uint64_t)
#define RIO_TRANSFER \
	_IOWR(RIO_MPORT_DRV_MAGIC, 21, struct rio_transaction)
#define RIO_WAIT_FOR_ASYNC \
	_IOW(RIO_MPORT_DRV_MAGIC, 22, struct rio_async_tx_wait)
#define RIO_DEV_ADD \
	_IOW(RIO_MPORT_DRV_MAGIC, 23, struct rio_rdev_info)
#define RIO_DEV_DEL \
	_IOW(RIO_MPORT_DRV_MAGIC, 24, struct rio_rdev_info)

#endif /* _RIO_MPORT_CDEV_H_ */
