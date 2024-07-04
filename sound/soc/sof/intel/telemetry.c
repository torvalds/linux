// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Intel Corporation

/* telemetry data queried from debug window */

#include <sound/sof/ipc4/header.h>
#include <sound/sof/xtensa.h>
#include "../ipc4-priv.h"
#include "../sof-priv.h"
#include "hda.h"
#include "telemetry.h"

void sof_ipc4_intel_dump_telemetry_state(struct snd_sof_dev *sdev, u32 flags)
{
	static const char invalid_slot_msg[] = "Core dump is not available due to";
	struct sof_ipc4_telemetry_slot_data *telemetry_data;
	struct sof_ipc_dsp_oops_xtensa *xoops;
	struct xtensa_arch_block *block;
	u32 slot_offset;
	char *level;

	level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;

	slot_offset = sof_ipc4_find_debug_slot_offset_by_type(sdev, SOF_IPC4_DEBUG_SLOT_TELEMETRY);
	if (!slot_offset)
		return;

	telemetry_data = kmalloc(sizeof(*telemetry_data), GFP_KERNEL);
	if (!telemetry_data)
		return;
	sof_mailbox_read(sdev, slot_offset, telemetry_data, sizeof(*telemetry_data));
	if (telemetry_data->separator != XTENSA_CORE_DUMP_SEPARATOR) {
		dev_err(sdev->dev, "%s invalid separator %#x\n", invalid_slot_msg,
			telemetry_data->separator);
		goto free_telemetry_data;
	}

	block = kmalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		goto free_telemetry_data;

	sof_mailbox_read(sdev, slot_offset + sizeof(*telemetry_data), block, sizeof(*block));
	if (block->soc != XTENSA_SOC_INTEL_ADSP) {
		dev_err(sdev->dev, "%s invalid SOC %d\n", invalid_slot_msg, block->soc);
		goto free_block;
	}

	if (telemetry_data->hdr.id[0] != COREDUMP_HDR_ID0 ||
	    telemetry_data->hdr.id[1] != COREDUMP_HDR_ID1 ||
	    telemetry_data->arch_hdr.id != COREDUMP_ARCH_HDR_ID) {
		dev_err(sdev->dev, "%s invalid coredump header %c%c, arch hdr %c\n",
			invalid_slot_msg, telemetry_data->hdr.id[0],
			telemetry_data->hdr.id[1],
			telemetry_data->arch_hdr.id);
		goto free_block;
	}

	switch (block->toolchain) {
	case XTENSA_TOOL_CHAIN_ZEPHYR:
		dev_printk(level, sdev->dev, "FW is built with Zephyr toolchain\n");
		break;
	case XTENSA_TOOL_CHAIN_XCC:
		dev_printk(level, sdev->dev, "FW is built with XCC toolchain\n");
		break;
	default:
		dev_printk(level, sdev->dev, "Unknown toolchain is used\n");
		break;
	}

	xoops = kzalloc(struct_size(xoops, ar, XTENSA_CORE_AR_REGS_COUNT), GFP_KERNEL);
	if (!xoops)
		goto free_block;

	xoops->exccause = block->exccause;
	xoops->excvaddr = block->excvaddr;
	xoops->epc1 = block->pc;
	xoops->ps = block->ps;
	xoops->sar = block->sar;

	xoops->plat_hdr.numaregs = XTENSA_CORE_AR_REGS_COUNT;
	memcpy((void *)xoops->ar, block->ar, XTENSA_CORE_AR_REGS_COUNT * sizeof(u32));

	sof_oops(sdev, level, xoops);
	sof_stack(sdev, level, xoops, NULL, 0);

	kfree(xoops);
free_block:
	kfree(block);
free_telemetry_data:
	kfree(telemetry_data);
}
EXPORT_SYMBOL_NS(sof_ipc4_intel_dump_telemetry_state, SND_SOC_SOF_INTEL_HDA_COMMON);
