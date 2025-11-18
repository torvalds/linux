/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2024-2025 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_DEBUG_H
#define __SOUND_SOC_INTEL_AVS_DEBUG_H

#include "messages.h"
#include "registers.h"

struct avs_dev;

#define avs_log_buffer_size(adev) \
	((adev)->fw_cfg.trace_log_bytes / (adev)->hw_cfg.dsp_cores)

#define avs_log_buffer_addr(adev, core) \
({										\
	s32 __offset = avs_dsp_op(adev, log_buffer_offset, core);		\
	(__offset < 0) ? NULL :							\
			 (avs_sram_addr(adev, AVS_DEBUG_WINDOW) + __offset);	\
})

static inline int avs_log_buffer_status_locked(struct avs_dev *adev, union avs_notify_msg *msg)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&adev->trace_lock, flags);
	ret = avs_dsp_op(adev, log_buffer_status, msg);
	spin_unlock_irqrestore(&adev->trace_lock, flags);

	return ret;
}

struct avs_apl_log_buffer_layout {
	u32 read_ptr;
	u32 write_ptr;
	u8 buffer[];
} __packed;
static_assert(sizeof(struct avs_apl_log_buffer_layout) == 8);

#define avs_apl_log_payload_size(adev) \
	(avs_log_buffer_size(adev) - sizeof(struct avs_apl_log_buffer_layout))

#define avs_apl_log_payload_addr(addr) \
	(addr + sizeof(struct avs_apl_log_buffer_layout))

#ifdef CONFIG_DEBUG_FS
int avs_register_probe_component(struct avs_dev *adev, const char *name);

#define AVS_SET_ENABLE_LOGS_OP(name) \
	.enable_logs = avs_##name##_enable_logs

bool avs_logging_fw(struct avs_dev *adev);
void avs_dump_fw_log(struct avs_dev *adev, const void __iomem *src, unsigned int len);
void avs_dump_fw_log_wakeup(struct avs_dev *adev, const void __iomem *src, unsigned int len);

void avs_debugfs_init(struct avs_dev *adev);
void avs_debugfs_exit(struct avs_dev *adev);

#else
static inline int avs_register_probe_component(struct avs_dev *adev, const char *name)
{
	return -EOPNOTSUPP;
}

#define AVS_SET_ENABLE_LOGS_OP(name)

static inline bool avs_logging_fw(struct avs_dev *adev)
{
	return false;
}

static inline void avs_dump_fw_log(struct avs_dev *adev, const void __iomem *src, unsigned int len)
{
}

static inline void avs_dump_fw_log_wakeup(struct avs_dev *adev, const void __iomem *src,
					  unsigned int len)
{
}

static inline void avs_debugfs_init(struct avs_dev *adev) { }
static inline void avs_debugfs_exit(struct avs_dev *adev) { }
#endif

#endif
