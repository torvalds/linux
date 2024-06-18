/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation
 */

#ifndef __INCLUDE_SOUND_SOF_TRACE_H__
#define __INCLUDE_SOUND_SOF_TRACE_H__

#include <sound/sof/header.h>
#include <sound/sof/stream.h>

/*
 * DMA for Trace
 */

#define SOF_TRACE_FILENAME_SIZE		32

/* DMA for Trace params info - SOF_IPC_DEBUG_DMA_PARAMS */
/* Deprecated - use sof_ipc_dma_trace_params_ext */
struct sof_ipc_dma_trace_params {
	struct sof_ipc_cmd_hdr hdr;
	struct sof_ipc_host_buffer buffer;
	uint32_t stream_tag;
}  __packed;

/* DMA for Trace params info - SOF_IPC_DEBUG_DMA_PARAMS_EXT */
struct sof_ipc_dma_trace_params_ext {
	struct sof_ipc_cmd_hdr hdr;
	struct sof_ipc_host_buffer buffer;
	uint32_t stream_tag;
	uint64_t timestamp_ns; /* in nanosecond */
	uint32_t reserved[8];
}  __packed;

/* DMA for Trace params info - SOF_IPC_DEBUG_DMA_PARAMS */
struct sof_ipc_dma_trace_posn {
	struct sof_ipc_reply rhdr;
	uint32_t host_offset;	/* Offset of DMA host buffer */
	uint32_t overflow;	/* overflow bytes if any */
	uint32_t messages;	/* total trace messages */
}  __packed;

/* Values used in sof_ipc_trace_filter_elem: */

/* bits 6..0 */
#define SOF_IPC_TRACE_FILTER_ELEM_SET_LEVEL	0x01	/**< trace level for selected components */
#define SOF_IPC_TRACE_FILTER_ELEM_BY_UUID	0x02	/**< filter by uuid key */
#define SOF_IPC_TRACE_FILTER_ELEM_BY_PIPE	0x03	/**< filter by pipeline */
#define SOF_IPC_TRACE_FILTER_ELEM_BY_COMP	0x04	/**< filter by component id */

/* bit 7 */
#define SOF_IPC_TRACE_FILTER_ELEM_FIN		0x80	/**< mark last filter in set */

/* bits 31..8: Unused */

/** part of sof_ipc_trace_filter, ABI3.17 */
struct sof_ipc_trace_filter_elem {
	uint32_t key;		/**< SOF_IPC_TRACE_FILTER_ELEM_ {LEVEL, UUID, COMP, PIPE} */
	uint32_t value;		/**< element value */
} __packed;

/** Runtime tracing filtration data - SOF_IPC_TRACE_FILTER_UPDATE, ABI3.17 */
struct sof_ipc_trace_filter {
	struct sof_ipc_cmd_hdr hdr;	/**< IPC command header */
	uint32_t elem_cnt;		/**< number of entries in elems[] array */
	uint32_t reserved[8];		/**< reserved for future usage */
	/** variable size array with new filtering settings */
	struct sof_ipc_trace_filter_elem elems[];
} __packed;

/*
 * Commom debug
 */

/*
 * SOF panic codes
 */
#define SOF_IPC_PANIC_MAGIC			0x0dead000
#define SOF_IPC_PANIC_MAGIC_MASK		0x0ffff000
#define SOF_IPC_PANIC_CODE_MASK			0x00000fff
#define SOF_IPC_PANIC_MEM			(SOF_IPC_PANIC_MAGIC | 0x0)
#define SOF_IPC_PANIC_WORK			(SOF_IPC_PANIC_MAGIC | 0x1)
#define SOF_IPC_PANIC_IPC			(SOF_IPC_PANIC_MAGIC | 0x2)
#define SOF_IPC_PANIC_ARCH			(SOF_IPC_PANIC_MAGIC | 0x3)
#define SOF_IPC_PANIC_PLATFORM			(SOF_IPC_PANIC_MAGIC | 0x4)
#define SOF_IPC_PANIC_TASK			(SOF_IPC_PANIC_MAGIC | 0x5)
#define SOF_IPC_PANIC_EXCEPTION			(SOF_IPC_PANIC_MAGIC | 0x6)
#define SOF_IPC_PANIC_DEADLOCK			(SOF_IPC_PANIC_MAGIC | 0x7)
#define SOF_IPC_PANIC_STACK			(SOF_IPC_PANIC_MAGIC | 0x8)
#define SOF_IPC_PANIC_IDLE			(SOF_IPC_PANIC_MAGIC | 0x9)
#define SOF_IPC_PANIC_WFI			(SOF_IPC_PANIC_MAGIC | 0xa)
#define SOF_IPC_PANIC_ASSERT			(SOF_IPC_PANIC_MAGIC | 0xb)

/* panic info include filename and line number
 * filename array will not include null terminator if fully filled
 */
struct sof_ipc_panic_info {
	struct sof_ipc_hdr hdr;
	uint32_t code;			/* SOF_IPC_PANIC_ */
	uint8_t filename[SOF_TRACE_FILENAME_SIZE];
	uint32_t linenum;
}  __packed;

#endif
