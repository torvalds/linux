// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <uapi/sound/sof-ipc.h>
#include "ops.h"
#include "sof-priv.h"

int snd_sof_pci_update_bits_unlocked(struct snd_sof_dev *sdev, u32 offset,
				     u32 mask, u32 value)
{
	bool change;
	unsigned int old, new;
	u32 ret = ~0; /* explicit init to remove uninitialized use warnings */

	pci_read_config_dword(sdev->pci, offset, &ret);
	dev_dbg(sdev->dev, "Debug PCIR: %8.8x at  %8.8x\n",
		pci_read_config_dword(sdev->pci, offset, &ret), offset);

	old = ret;
	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change) {
		pci_write_config_dword(sdev->pci, offset, new);
		dev_dbg(sdev->dev, "Debug PCIW: %8.8x at  %8.8x\n", value,
			offset);
	}

	return change;
}
EXPORT_SYMBOL(snd_sof_pci_update_bits_unlocked);

int snd_sof_pci_update_bits(struct snd_sof_dev *sdev, u32 offset,
			    u32 mask, u32 value)
{
	unsigned long flags;
	bool change;

	spin_lock_irqsave(&sdev->hw_lock, flags);
	change = snd_sof_pci_update_bits_unlocked(sdev, offset, mask, value);
	spin_unlock_irqrestore(&sdev->hw_lock, flags);
	return change;
}
EXPORT_SYMBOL(snd_sof_pci_update_bits);

int snd_sof_dsp_update_bits_unlocked(struct snd_sof_dev *sdev, u32 bar,
				     u32 offset, u32 mask, u32 value)
{
	bool change;
	unsigned int old, new;
	u32 ret;

	ret = snd_sof_dsp_read(sdev, bar, offset);

	old = ret;
	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		snd_sof_dsp_write(sdev, bar, offset, new);

	return change;
}
EXPORT_SYMBOL(snd_sof_dsp_update_bits_unlocked);

int snd_sof_dsp_update_bits64_unlocked(struct snd_sof_dev *sdev, u32 bar,
				       u32 offset, u64 mask, u64 value)
{
	bool change;
	u64 old, new;

	old = snd_sof_dsp_read64(sdev, bar, offset);

	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		snd_sof_dsp_write64(sdev, bar, offset, new);

	return change;
}
EXPORT_SYMBOL(snd_sof_dsp_update_bits64_unlocked);

/* This is for registers bits with attribute RWC */
void snd_sof_dsp_update_bits_forced_unlocked(struct snd_sof_dev *sdev, u32 bar,
					     u32 offset, u32 mask, u32 value)
{
	unsigned int old, new;
	u32 ret;

	ret = snd_sof_dsp_read(sdev, bar, offset);

	old = ret;
	new = (old & (~mask)) | (value & mask);

	snd_sof_dsp_write(sdev, bar, offset, new);
}
EXPORT_SYMBOL(snd_sof_dsp_update_bits_forced_unlocked);

int snd_sof_dsp_update_bits(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			    u32 mask, u32 value)
{
	unsigned long flags;
	bool change;

	spin_lock_irqsave(&sdev->hw_lock, flags);
	change = snd_sof_dsp_update_bits_unlocked(sdev, bar, offset, mask,
						  value);
	spin_unlock_irqrestore(&sdev->hw_lock, flags);
	return change;
}
EXPORT_SYMBOL(snd_sof_dsp_update_bits);

int snd_sof_dsp_update_bits64(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			      u64 mask, u64 value)
{
	unsigned long flags;
	bool change;

	spin_lock_irqsave(&sdev->hw_lock, flags);
	change = snd_sof_dsp_update_bits64_unlocked(sdev, bar, offset, mask,
						    value);
	spin_unlock_irqrestore(&sdev->hw_lock, flags);
	return change;
}
EXPORT_SYMBOL(snd_sof_dsp_update_bits64);

/* This is for registers bits with attribute RWC */
void snd_sof_dsp_update_bits_forced(struct snd_sof_dev *sdev, u32 bar,
				    u32 offset, u32 mask, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&sdev->hw_lock, flags);
	snd_sof_dsp_update_bits_forced_unlocked(sdev, bar, offset, mask, value);
	spin_unlock_irqrestore(&sdev->hw_lock, flags);
}
EXPORT_SYMBOL(snd_sof_dsp_update_bits_forced);

int snd_sof_dsp_register_poll(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			      u32 mask, u32 target, u32 timeout)
{
	int time, ret;
	bool done = false;

	/*
	 * we will poll for couple of ms using mdelay, if not successful
	 * then go to longer sleep using usleep_range
	 */

	/* check if set state successful */
	for (time = 0; time < 5; time++) {
		if ((snd_sof_dsp_read(sdev, bar, offset) & mask) == target) {
			done = true;
			break;
		}
		msleep(20);
	}

	if (!done) {
		/* sleeping in 10ms steps so adjust timeout value */
		timeout /= 10;

		for (time = 0; time < timeout; time++) {
			if ((snd_sof_dsp_read(sdev, bar, offset) & mask) ==
				target)
				break;

			usleep_range(5000, 10000);
		}
	}

	ret = time < timeout ? 0 : -ETIME;

	return ret;
}
EXPORT_SYMBOL(snd_sof_dsp_register_poll);

void snd_sof_dsp_panic(struct snd_sof_dev *sdev, u32 offset)
{
	dev_err(sdev->dev, "error : DSP panic!\n");

	/* check if DSP is not ready and did not set the dsp_oops_offset.
	 * if the dsp_oops_offset is not set, set it from the panic message.
	 * Also add a check to memory window setting with panic message.
	 */
	if (!sdev->dsp_oops_offset)
		sdev->dsp_oops_offset = offset;
	else
		dev_dbg(sdev->dev, "panic: dsp_oops_offset %zu offset %d\n",
			sdev->dsp_oops_offset, offset);

	snd_sof_dsp_dbg_dump(sdev, SOF_DBG_REGS | SOF_DBG_MBOX);
	snd_sof_trace_notify_for_error(sdev);
	snd_sof_dsp_cmd_done(sdev, SOF_IPC_HOST_REPLY);
}
EXPORT_SYMBOL(snd_sof_dsp_panic);
