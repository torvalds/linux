// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2020 NXP
//
// Common helpers for the audio DSP on i.MX8

#include <linux/module.h>
#include <sound/sof/xtensa.h>
#include "../ops.h"

#include "imx-common.h"

/**
 * imx8_get_registers() - This function is called in case of DSP oops
 * in order to gather information about the registers, filename and
 * linenumber and stack.
 * @sdev: SOF device
 * @xoops: Stores information about registers.
 * @panic_info: Stores information about filename and line number.
 * @stack: Stores the stack dump.
 * @stack_words: Size of the stack dump.
 */
void imx8_get_registers(struct snd_sof_dev *sdev,
			struct sof_ipc_dsp_oops_xtensa *xoops,
			struct sof_ipc_panic_info *panic_info,
			u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read registers */
	sof_mailbox_read(sdev, offset, xoops, sizeof(*xoops));

	/* then get panic info */
	if (xoops->arch_hdr.totalsize > EXCEPT_MAX_HDR_SIZE) {
		dev_err(sdev->dev, "invalid header size 0x%x. FW oops is bogus\n",
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
 * imx8_dump() - This function is called when a panic message is
 * received from the firmware.
 * @sdev: SOF device
 * @flags: parameter not used but required by ops prototype
 */
void imx8_dump(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[IMX8_STACK_DUMP_SIZE];
	u32 status;

	/* Get information about the panic status from the debug box area.
	 * Compute the trace point based on the status.
	 */
	sof_mailbox_read(sdev, sdev->debug_box.offset + 0x4, &status, 4);

	/* Get information about the registers, the filename and line
	 * number and the stack.
	 */
	imx8_get_registers(sdev, &xoops, &panic_info, stack,
			   IMX8_STACK_DUMP_SIZE);

	/* Print the information to the console */
	snd_sof_get_status(sdev, status, status, &xoops, &panic_info, stack,
			   IMX8_STACK_DUMP_SIZE);
}
EXPORT_SYMBOL(imx8_dump);

int imx8_parse_clocks(struct snd_sof_dev *sdev, struct imx_clocks *clks)
{
	int ret;

	ret = devm_clk_bulk_get(sdev->dev, clks->num_dsp_clks, clks->dsp_clks);
	if (ret)
		dev_err(sdev->dev, "Failed to request DSP clocks\n");

	return ret;
}
EXPORT_SYMBOL(imx8_parse_clocks);

int imx8_enable_clocks(struct snd_sof_dev *sdev, struct imx_clocks *clks)
{
	return clk_bulk_prepare_enable(clks->num_dsp_clks, clks->dsp_clks);
}
EXPORT_SYMBOL(imx8_enable_clocks);

void imx8_disable_clocks(struct snd_sof_dev *sdev, struct imx_clocks *clks)
{
	clk_bulk_disable_unprepare(clks->num_dsp_clks, clks->dsp_clks);
}
EXPORT_SYMBOL(imx8_disable_clocks);

MODULE_LICENSE("Dual BSD/GPL");
