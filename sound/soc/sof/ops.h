/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __SOUND_SOC_SOF_IO_H
#define __SOUND_SOC_SOF_IO_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <uapi/sound/sof-ipc.h>
#include "sof-priv.h"

/* init */
static inline int snd_sof_probe(struct snd_sof_dev *sdev)
{
	if (sdev->ops->probe)
		return sdev->ops->probe(sdev);
	else
		return 0;
}

static inline int snd_sof_remove(struct snd_sof_dev *sdev)
{
	if (sdev->ops->remove)
		return sdev->ops->remove(sdev);
	else
		return 0;
}

/* control */
static inline int snd_sof_dsp_run(struct snd_sof_dev *sdev)
{
	if (sdev->ops->run)
		return sdev->ops->run(sdev);
	else
		return 0;
}

static inline int snd_sof_dsp_stall(struct snd_sof_dev *sdev)
{
	if (sdev->ops->stall)
		return sdev->ops->stall(sdev);
	else
		return 0;
}

static inline int snd_sof_dsp_reset(struct snd_sof_dev *sdev)
{
	if (sdev->ops->reset)
		return sdev->ops->reset(sdev);
	else
		return 0;
}

/* power management */
static inline int snd_sof_dsp_resume(struct snd_sof_dev *sdev)
{
	if (sdev->ops->resume)
		return sdev->ops->resume(sdev);
	else
		return 0;
}

static inline int snd_sof_dsp_suspend(struct snd_sof_dev *sdev, int state)
{
	if (sdev->ops->suspend)
		return sdev->ops->suspend(sdev, state);
	else
		return 0;
}

static inline int snd_sof_dsp_set_clk(struct snd_sof_dev *sdev, u32 freq)
{
	if (sdev->ops->set_clk)
		return sdev->ops->set_clk(sdev, freq);
	else
		return 0;
}

/* debug */
static inline void snd_sof_dsp_dbg_dump(struct snd_sof_dev *sdev, u32 flags)
{
	if (sdev->ops->dbg_dump)
		return sdev->ops->dbg_dump(sdev, flags);
}

/* register IO */
static inline void snd_sof_dsp_write(struct snd_sof_dev *sdev, u32 bar,
				     u32 offset, u32 value)
{
	if (sdev->ops->write)
		sdev->ops->write(sdev, sdev->bar[bar] + offset, value);
}

static inline void snd_sof_dsp_write64(struct snd_sof_dev *sdev, u32 bar,
				       u32 offset, u64 value)
{
	if (sdev->ops->write64)
		sdev->ops->write64(sdev,
			sdev->bar[bar] + offset, value);
}

static inline u32 snd_sof_dsp_read(struct snd_sof_dev *sdev, u32 bar,
				   u32 offset)
{
	if (sdev->ops->read)
		return sdev->ops->read(sdev, sdev->bar[bar] + offset);
	else
		return 0;
}

static inline u64 snd_sof_dsp_read64(struct snd_sof_dev *sdev, u32 bar,
				     u32 offset)
{
	if (sdev->ops->read64)
		return sdev->ops->read64(sdev, sdev->bar[bar] + offset);
	else
		return 0;
}

/* block IO */
static inline void snd_sof_dsp_block_read(struct snd_sof_dev *sdev,
					  u32 offset, void *dest, size_t bytes)
{
	if (sdev->ops->block_read)
		sdev->ops->block_read(sdev, offset, dest, bytes);
}

static inline void snd_sof_dsp_block_write(struct snd_sof_dev *sdev,
					   u32 offset, void *src, size_t bytes)
{
	if (sdev->ops->block_write)
		sdev->ops->block_write(sdev, offset, src, bytes);
}

/* mailbox */
static inline void snd_sof_dsp_mailbox_read(struct snd_sof_dev *sdev,
					    u32 offset, void *message,
					    size_t bytes)
{
	if (sdev->ops->mailbox_read)
		sdev->ops->mailbox_read(sdev, offset, message, bytes);
}

static inline void snd_sof_dsp_mailbox_write(struct snd_sof_dev *sdev,
					     u32 offset, void *message,
					     size_t bytes)
{
	if (sdev->ops->mailbox_write)
		sdev->ops->mailbox_write(sdev, offset, message, bytes);
}

/* ipc */
static inline int snd_sof_dsp_send_msg(struct snd_sof_dev *sdev,
				       struct snd_sof_ipc_msg *msg)
{
	if (sdev->ops->send_msg)
		return sdev->ops->send_msg(sdev, msg);
	else
		return 0;
}

static inline int snd_sof_dsp_get_reply(struct snd_sof_dev *sdev,
					struct snd_sof_ipc_msg *msg)
{
	if (sdev->ops->get_reply)
		return sdev->ops->get_reply(sdev, msg);
	else
		return 0;
}

static inline int snd_sof_dsp_is_ready(struct snd_sof_dev *sdev)
{
	if (sdev->ops->is_ready)
		return sdev->ops->is_ready(sdev);
	else
		return 0;
}

static inline int snd_sof_dsp_cmd_done(struct snd_sof_dev *sdev,
				       int dir)
{
	if (sdev->ops->cmd_done)
		return sdev->ops->cmd_done(sdev, dir);
	else
		return 0;
}

/* host DMA trace */
static inline int snd_sof_dma_trace_init(struct snd_sof_dev *sdev,
					 u32 *stream_tag)
{
	if (sdev->ops->trace_init)
		return sdev->ops->trace_init(sdev, stream_tag);
	else
		return 0;
}

static inline int snd_sof_dma_trace_release(struct snd_sof_dev *sdev)
{
	if (sdev->ops->trace_release)
		return sdev->ops->trace_release(sdev);
	else
		return 0;
}

static inline int snd_sof_dma_trace_trigger(struct snd_sof_dev *sdev, int cmd)
{
	if (sdev->ops->trace_trigger)
		return sdev->ops->trace_trigger(sdev, cmd);
	else
		return 0;
}

/* host PCM ops */
static inline int
snd_sof_pcm_platform_open(struct snd_sof_dev *sdev,
			  struct snd_pcm_substream *substream)
{
	if (sdev->ops && sdev->ops->pcm_open)
		return sdev->ops->pcm_open(sdev, substream);
	else
		return 0;
}

/* disconnect pcm substream to a host stream */
static inline int
snd_sof_pcm_platform_close(struct snd_sof_dev *sdev,
			   struct snd_pcm_substream *substream)
{
	if (sdev->ops && sdev->ops->pcm_close)
		return sdev->ops->pcm_close(sdev, substream);
	else
		return 0;
}

/* host stream hw params */
static inline int
snd_sof_pcm_platform_hw_params(struct snd_sof_dev *sdev,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	if (sdev->ops && sdev->ops->pcm_hw_params)
		return sdev->ops->pcm_hw_params(sdev, substream, params);
	else
		return 0;
}

/* host stream trigger */
static inline int
snd_sof_pcm_platform_trigger(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream, int cmd)
{
	if (sdev->ops && sdev->ops->pcm_trigger)
		return sdev->ops->pcm_trigger(sdev, substream, cmd);
	else
		return 0;
}

int snd_sof_dsp_update_bits_unlocked(struct snd_sof_dev *sdev, u32 bar,
				     u32 offset, u32 mask, u32 value);

int snd_sof_dsp_update_bits64_unlocked(struct snd_sof_dev *sdev, u32 bar,
				       u32 offset, u64 mask, u64 value);

/* This is for registers bits with attribute RWC */
void snd_sof_dsp_update_bits_forced_unlocked(struct snd_sof_dev *sdev, u32 bar,
					     u32 offset, u32 mask, u32 value);

int snd_sof_dsp_update_bits(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			    u32 mask, u32 value);

int snd_sof_dsp_update_bits64(struct snd_sof_dev *sdev, u32 bar,
			      u32 offset, u64 mask, u64 value);

/* This is for registers bits with attribute RWC */
void snd_sof_dsp_update_bits_forced(struct snd_sof_dev *sdev, u32 bar,
				    u32 offset, u32 mask, u32 value);

int snd_sof_pci_update_bits_unlocked(struct snd_sof_dev *sdev, u32 offset,
				     u32 mask, u32 value);

int snd_sof_pci_update_bits(struct snd_sof_dev *sdev, u32 offset,
			    u32 mask, u32 value);

int snd_sof_dsp_register_poll(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			      u32 mask, u32 target, u32 timeout);

void snd_sof_dsp_panic(struct snd_sof_dev *sdev, u32 offset);
#endif
