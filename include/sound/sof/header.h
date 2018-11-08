/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_HEADER_H__
#define __INCLUDE_SOUND_SOF_HEADER_H__

#include <uapi/sound/sof/abi.h>

/** \addtogroup sof_uapi uAPI
 *  SOF uAPI specification.
 *  @{
 */

/*
 * IPC messages have a prefixed 32 bit identifier made up as follows :-
 *
 * 0xGCCCNNNN where
 * G is global cmd type (4 bits)
 * C is command type (12 bits)
 * I is the ID number (16 bits) - monotonic and overflows
 *
 * This is sent at the start of the IPM message in the mailbox. Messages should
 * not be sent in the doorbell (special exceptions for firmware .
 */

/* Global Message - Generic */
#define SOF_GLB_TYPE_SHIFT			28
#define SOF_GLB_TYPE_MASK			(0xf << SOF_GLB_TYPE_SHIFT)
#define SOF_GLB_TYPE(x)				((x) << SOF_GLB_TYPE_SHIFT)

/* Command Message - Generic */
#define SOF_CMD_TYPE_SHIFT			16
#define SOF_CMD_TYPE_MASK			(0xfff << SOF_CMD_TYPE_SHIFT)
#define SOF_CMD_TYPE(x)				((x) << SOF_CMD_TYPE_SHIFT)

/* Global Message Types */
#define SOF_IPC_GLB_REPLY			SOF_GLB_TYPE(0x1U)
#define SOF_IPC_GLB_COMPOUND			SOF_GLB_TYPE(0x2U)
#define SOF_IPC_GLB_TPLG_MSG			SOF_GLB_TYPE(0x3U)
#define SOF_IPC_GLB_PM_MSG			SOF_GLB_TYPE(0x4U)
#define SOF_IPC_GLB_COMP_MSG			SOF_GLB_TYPE(0x5U)
#define SOF_IPC_GLB_STREAM_MSG			SOF_GLB_TYPE(0x6U)
#define SOF_IPC_FW_READY			SOF_GLB_TYPE(0x7U)
#define SOF_IPC_GLB_DAI_MSG			SOF_GLB_TYPE(0x8U)
#define SOF_IPC_GLB_TRACE_MSG			SOF_GLB_TYPE(0x9U)

/*
 * DSP Command Message Types
 */

/* topology */
#define SOF_IPC_TPLG_COMP_NEW			SOF_CMD_TYPE(0x001)
#define SOF_IPC_TPLG_COMP_FREE			SOF_CMD_TYPE(0x002)
#define SOF_IPC_TPLG_COMP_CONNECT		SOF_CMD_TYPE(0x003)
#define SOF_IPC_TPLG_PIPE_NEW			SOF_CMD_TYPE(0x010)
#define SOF_IPC_TPLG_PIPE_FREE			SOF_CMD_TYPE(0x011)
#define SOF_IPC_TPLG_PIPE_CONNECT		SOF_CMD_TYPE(0x012)
#define SOF_IPC_TPLG_PIPE_COMPLETE		SOF_CMD_TYPE(0x013)
#define SOF_IPC_TPLG_BUFFER_NEW			SOF_CMD_TYPE(0x020)
#define SOF_IPC_TPLG_BUFFER_FREE		SOF_CMD_TYPE(0x021)

/* PM */
#define SOF_IPC_PM_CTX_SAVE			SOF_CMD_TYPE(0x001)
#define SOF_IPC_PM_CTX_RESTORE			SOF_CMD_TYPE(0x002)
#define SOF_IPC_PM_CTX_SIZE			SOF_CMD_TYPE(0x003)
#define SOF_IPC_PM_CLK_SET			SOF_CMD_TYPE(0x004)
#define SOF_IPC_PM_CLK_GET			SOF_CMD_TYPE(0x005)
#define SOF_IPC_PM_CLK_REQ			SOF_CMD_TYPE(0x006)
#define SOF_IPC_PM_CORE_ENABLE			SOF_CMD_TYPE(0x007)

/* component runtime config - multiple different types */
#define SOF_IPC_COMP_SET_VALUE			SOF_CMD_TYPE(0x001)
#define SOF_IPC_COMP_GET_VALUE			SOF_CMD_TYPE(0x002)
#define SOF_IPC_COMP_SET_DATA			SOF_CMD_TYPE(0x003)
#define SOF_IPC_COMP_GET_DATA			SOF_CMD_TYPE(0x004)

/* DAI messages */
#define SOF_IPC_DAI_CONFIG			SOF_CMD_TYPE(0x001)
#define SOF_IPC_DAI_LOOPBACK			SOF_CMD_TYPE(0x002)

/* stream */
#define SOF_IPC_STREAM_PCM_PARAMS		SOF_CMD_TYPE(0x001)
#define SOF_IPC_STREAM_PCM_PARAMS_REPLY		SOF_CMD_TYPE(0x002)
#define SOF_IPC_STREAM_PCM_FREE			SOF_CMD_TYPE(0x003)
#define SOF_IPC_STREAM_TRIG_START		SOF_CMD_TYPE(0x004)
#define SOF_IPC_STREAM_TRIG_STOP		SOF_CMD_TYPE(0x005)
#define SOF_IPC_STREAM_TRIG_PAUSE		SOF_CMD_TYPE(0x006)
#define SOF_IPC_STREAM_TRIG_RELEASE		SOF_CMD_TYPE(0x007)
#define SOF_IPC_STREAM_TRIG_DRAIN		SOF_CMD_TYPE(0x008)
#define SOF_IPC_STREAM_TRIG_XRUN		SOF_CMD_TYPE(0x009)
#define SOF_IPC_STREAM_POSITION			SOF_CMD_TYPE(0x00a)
#define SOF_IPC_STREAM_VORBIS_PARAMS		SOF_CMD_TYPE(0x010)
#define SOF_IPC_STREAM_VORBIS_FREE		SOF_CMD_TYPE(0x011)

/* trace and debug */
#define SOF_IPC_TRACE_DMA_PARAMS		SOF_CMD_TYPE(0x001)
#define SOF_IPC_TRACE_DMA_POSITION		SOF_CMD_TYPE(0x002)

/* Get message component id */
#define SOF_IPC_MESSAGE_ID(x)			((x) & 0xffff)

/* maximum message size for mailbox Tx/Rx */
#define SOF_IPC_MSG_MAX_SIZE			384

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

/*
 * SOF memory capabilities, add new ones at the end
 */
#define SOF_MEM_CAPS_RAM			(1 << 0)
#define SOF_MEM_CAPS_ROM			(1 << 1)
#define SOF_MEM_CAPS_EXT			(1 << 2) /**< external */
#define SOF_MEM_CAPS_LP			(1 << 3) /**< low power */
#define SOF_MEM_CAPS_HP			(1 << 4) /**< high performance */
#define SOF_MEM_CAPS_DMA			(1 << 5) /**< DMA'able */
#define SOF_MEM_CAPS_CACHE			(1 << 6) /**< cacheable */
#define SOF_MEM_CAPS_EXEC			(1 << 7) /**< executable */

/*
 * Command Header - Header for all IPC. Identifies IPC message.
 * The size can be greater than the structure size and that means there is
 * extended bespoke data beyond the end of the structure including variable
 * arrays.
 */

struct sof_ipc_hdr {
	uint32_t cmd;			/**< SOF_IPC_GLB_ + cmd */
	uint32_t size;			/**< size of structure */
}  __packed;

/*
 * Generic reply message. Some commands override this with their own reply
 * types that must include this at start.
 */
struct sof_ipc_reply {
	struct sof_ipc_hdr hdr;
	int32_t error;			/**< negative error numbers */
}  __packed;

/*
 * Compound commands - SOF_IPC_GLB_COMPOUND.
 *
 * Compound commands are sent to the DSP as a single IPC operation. The
 * commands are split into blocks and each block has a header. This header
 * identifies the command type and the number of commands before the next
 * header.
 */

struct sof_ipc_compound_hdr {
	struct sof_ipc_hdr hdr;
	uint32_t count;		/**< count of 0 means end of compound sequence */
}  __packed;

/** @}*/

#endif
