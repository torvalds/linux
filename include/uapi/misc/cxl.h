/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_MISC_CXL_H
#define _UAPI_MISC_CXL_H

#include <linux/types.h>
#include <linux/ioctl.h>


struct cxl_ioctl_start_work {
	__u64 flags;
	__u64 work_element_descriptor;
	__u64 amr;
	__s16 num_interrupts;
	__s16 reserved1;
	__s32 reserved2;
	__u64 reserved3;
	__u64 reserved4;
	__u64 reserved5;
	__u64 reserved6;
};

#define CXL_START_WORK_AMR		0x0000000000000001ULL
#define CXL_START_WORK_NUM_IRQS		0x0000000000000002ULL
#define CXL_START_WORK_ERR_FF		0x0000000000000004ULL
#define CXL_START_WORK_ALL		(CXL_START_WORK_AMR |\
					 CXL_START_WORK_NUM_IRQS |\
					 CXL_START_WORK_ERR_FF)


/* Possible modes that an afu can be in */
#define CXL_MODE_DEDICATED   0x1
#define CXL_MODE_DIRECTED    0x2

/* possible flags for the cxl_afu_id flags field */
#define CXL_AFUID_FLAG_SLAVE    0x1  /* In directed-mode afu is in slave mode */

struct cxl_afu_id {
	__u64 flags;     /* One of CXL_AFUID_FLAG_X */
	__u32 card_id;
	__u32 afu_offset;
	__u32 afu_mode;  /* one of the CXL_MODE_X */
	__u32 reserved1;
	__u64 reserved2;
	__u64 reserved3;
	__u64 reserved4;
	__u64 reserved5;
	__u64 reserved6;
};

/* base adapter image header is included in the image */
#define CXL_AI_NEED_HEADER	0x0000000000000001ULL
#define CXL_AI_ALL		CXL_AI_NEED_HEADER

#define CXL_AI_HEADER_SIZE 128
#define CXL_AI_BUFFER_SIZE 4096
#define CXL_AI_MAX_ENTRIES 256
#define CXL_AI_MAX_CHUNK_SIZE (CXL_AI_BUFFER_SIZE * CXL_AI_MAX_ENTRIES)

struct cxl_adapter_image {
	__u64 flags;
	__u64 data;
	__u64 len_data;
	__u64 len_image;
	__u64 reserved1;
	__u64 reserved2;
	__u64 reserved3;
	__u64 reserved4;
};

/* ioctl numbers */
#define CXL_MAGIC 0xCA
/* AFU devices */
#define CXL_IOCTL_START_WORK		_IOW(CXL_MAGIC, 0x00, struct cxl_ioctl_start_work)
#define CXL_IOCTL_GET_PROCESS_ELEMENT	_IOR(CXL_MAGIC, 0x01, __u32)
#define CXL_IOCTL_GET_AFU_ID            _IOR(CXL_MAGIC, 0x02, struct cxl_afu_id)
/* adapter devices */
#define CXL_IOCTL_DOWNLOAD_IMAGE        _IOW(CXL_MAGIC, 0x0A, struct cxl_adapter_image)
#define CXL_IOCTL_VALIDATE_IMAGE        _IOW(CXL_MAGIC, 0x0B, struct cxl_adapter_image)

#define CXL_READ_MIN_SIZE 0x1000 /* 4K */

/* Events from read() */
enum cxl_event_type {
	CXL_EVENT_RESERVED      = 0,
	CXL_EVENT_AFU_INTERRUPT = 1,
	CXL_EVENT_DATA_STORAGE  = 2,
	CXL_EVENT_AFU_ERROR     = 3,
	CXL_EVENT_AFU_DRIVER    = 4,
};

struct cxl_event_header {
	__u16 type;
	__u16 size;
	__u16 process_element;
	__u16 reserved1;
};

struct cxl_event_afu_interrupt {
	__u16 flags;
	__u16 irq; /* Raised AFU interrupt number */
	__u32 reserved1;
};

struct cxl_event_data_storage {
	__u16 flags;
	__u16 reserved1;
	__u32 reserved2;
	__u64 addr;
	__u64 dsisr;
	__u64 reserved3;
};

struct cxl_event_afu_error {
	__u16 flags;
	__u16 reserved1;
	__u32 reserved2;
	__u64 error;
};

struct cxl_event_afu_driver_reserved {
	/*
	 * Defines the buffer passed to the cxl driver by the AFU driver.
	 *
	 * This is not ABI since the event header.size passed to the user for
	 * existing events is set in the read call to sizeof(cxl_event_header)
	 * + sizeof(whatever event is being dispatched) and the user is already
	 * required to use a 4K buffer on the read call.
	 *
	 * Of course the contents will be ABI, but that's up the AFU driver.
	 */
	__u32 data_size;
	__u8 data[];
};

struct cxl_event {
	struct cxl_event_header header;
	union {
		struct cxl_event_afu_interrupt irq;
		struct cxl_event_data_storage fault;
		struct cxl_event_afu_error afu_error;
		struct cxl_event_afu_driver_reserved afu_driver_event;
	};
};

#endif /* _UAPI_MISC_CXL_H */
