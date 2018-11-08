/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
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
struct sof_ipc_dma_trace_params {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_host_buffer buffer;
	uint32_t stream_tag;
}  __packed;

/* DMA for Trace params info - SOF_IPC_DEBUG_DMA_PARAMS */
struct sof_ipc_dma_trace_posn {
	struct sof_ipc_reply rhdr;
	uint32_t host_offset;	/* Offset of DMA host buffer */
	uint32_t overflow;	/* overflow bytes if any */
	uint32_t messages;	/* total trace messages */
}  __packed;

/*
 * Commom debug
 */

/* panic info include filename and line number */
struct sof_ipc_panic_info {
	char filename[SOF_TRACE_FILENAME_SIZE];
	uint32_t linenum;
}  __packed;

#endif
