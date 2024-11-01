// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 MediaTek Inc. All rights reserved.
//
// Author: YC Hung <yc.hung@mediatek.com>

/*
 * Common helpers for the audio DSP on MediaTek platforms
 */

#include <linux/module.h>
#include <sound/sof/xtensa.h>
#include "../ops.h"
#include "mtk-adsp-common.h"

/**
 * mtk_adsp_get_registers() - This function is called in case of DSP oops
 * in order to gather information about the registers, filename and
 * linenumber and stack.
 * @sdev: SOF device
 * @xoops: Stores information about registers.
 * @panic_info: Stores information about filename and line number.
 * @stack: Stores the stack dump.
 * @stack_words: Size of the stack dump.
 */
static void mtk_adsp_get_registers(struct snd_sof_dev *sdev,
				   struct sof_ipc_dsp_oops_xtensa *xoops,
				   struct sof_ipc_panic_info *panic_info,
				   u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read registers */
	sof_mailbox_read(sdev, offset, xoops, sizeof(*xoops));

	/* then get panic info */
	if (xoops->arch_hdr.totalsize > EXCEPT_MAX_HDR_SIZE) {
		dev_err(sdev->dev, "invalid header size 0x%x\n",
			xoops->arch_hdr.totalsize);
		return;
	}
	offset += xoops->arch_hdr.totalsize;
	sof_mailbox_read(sdev, offset, panic_info, sizeof(*panic_info));

	/* then get the stack */
	offset += sizeof(*panic_info);
	sof_mailbox_read(sdev, offset, stack, stack_words * sizeof(u32));
}

/**
 * mtk_adsp_dump() - This function is called when a panic message is
 * received from the firmware.
 * @sdev: SOF device
 * @flags: parameter not used but required by ops prototype
 */
void mtk_adsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	char *level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info = {};
	u32 stack[MTK_ADSP_STACK_DUMP_SIZE];
	u32 status;

	/* Get information about the panic status from the debug box area.
	 * Compute the trace point based on the status.
	 */
	sof_mailbox_read(sdev, sdev->debug_box.offset + 0x4, &status, 4);

	/* Get information about the registers, the filename and line
	 * number and the stack.
	 */
	mtk_adsp_get_registers(sdev, &xoops, &panic_info, stack,
			       MTK_ADSP_STACK_DUMP_SIZE);

	/* Print the information to the console */
	sof_print_oops_and_stack(sdev, level, status, status, &xoops, &panic_info,
				 stack, MTK_ADSP_STACK_DUMP_SIZE);
}
EXPORT_SYMBOL(mtk_adsp_dump);

MODULE_LICENSE("Dual BSD/GPL");
