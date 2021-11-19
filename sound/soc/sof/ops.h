/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __SOUND_SOC_SOF_IO_H
#define __SOUND_SOC_SOF_IO_H

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sound/pcm.h>
#include "sof-priv.h"

#define sof_ops(sdev) \
	((sdev)->pdata->desc->ops)

/* Mandatory operations are verified during probing */

/* init */
static inline int snd_sof_probe(struct snd_sof_dev *sdev)
{
	return sof_ops(sdev)->probe(sdev);
}

static inline int snd_sof_remove(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->remove)
		return sof_ops(sdev)->remove(sdev);

	return 0;
}

static inline int snd_sof_shutdown(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->shutdown)
		return sof_ops(sdev)->shutdown(sdev);

	return 0;
}

/* control */

/*
 * snd_sof_dsp_run returns the core mask of the cores that are available
 * after successful fw boot
 */
static inline int snd_sof_dsp_run(struct snd_sof_dev *sdev)
{
	return sof_ops(sdev)->run(sdev);
}

static inline int snd_sof_dsp_stall(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	if (sof_ops(sdev)->stall)
		return sof_ops(sdev)->stall(sdev, core_mask);

	return 0;
}

static inline int snd_sof_dsp_reset(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->reset)
		return sof_ops(sdev)->reset(sdev);

	return 0;
}

/* dsp core power up/power down */
static inline int snd_sof_dsp_core_power_up(struct snd_sof_dev *sdev,
					    unsigned int core_mask)
{
	int ret = 0;

	core_mask &= ~sdev->enabled_cores_mask;
	if (sof_ops(sdev)->core_power_up && core_mask) {
		ret = sof_ops(sdev)->core_power_up(sdev, core_mask);
		if (!ret)
			sdev->enabled_cores_mask |= core_mask;
	}

	return ret;
}

static inline int snd_sof_dsp_core_power_down(struct snd_sof_dev *sdev,
					      unsigned int core_mask)
{
	int ret = 0;

	core_mask &= sdev->enabled_cores_mask;
	if (sof_ops(sdev)->core_power_down && core_mask) {
		ret = sof_ops(sdev)->core_power_down(sdev, core_mask);
		if (!ret)
			sdev->enabled_cores_mask &= ~core_mask;
	}

	return ret;
}

static inline int snd_sof_dsp_core_get(struct snd_sof_dev *sdev, int core)
{
	if (core > sdev->num_cores - 1) {
		dev_err(sdev->dev, "invalid core id: %d for num_cores: %d\n", core,
			sdev->num_cores);
		return -EINVAL;
	}

	if (sof_ops(sdev)->core_get) {
		int ret;

		/* if current ref_count is > 0, increment it and return */
		if (sdev->dsp_core_ref_count[core] > 0) {
			sdev->dsp_core_ref_count[core]++;
			return 0;
		}

		/* power up the core */
		ret = sof_ops(sdev)->core_get(sdev, core);
		if (ret < 0)
			return ret;

		/* increment ref_count */
		sdev->dsp_core_ref_count[core]++;

		/* and update enabled_cores_mask */
		sdev->enabled_cores_mask |= BIT(core);

		dev_dbg(sdev->dev, "Core %d powered up\n", core);
	}

	return 0;
}

static inline int snd_sof_dsp_core_put(struct snd_sof_dev *sdev, int core)
{
	if (core > sdev->num_cores - 1) {
		dev_err(sdev->dev, "invalid core id: %d for num_cores: %d\n", core,
			sdev->num_cores);
		return -EINVAL;
	}

	if (sof_ops(sdev)->core_put) {
		int ret;

		/* decrement ref_count and return if it is > 0 */
		if (--(sdev->dsp_core_ref_count[core]) > 0)
			return 0;

		/* power down the core */
		ret = sof_ops(sdev)->core_put(sdev, core);
		if (ret < 0)
			return ret;

		/* and update enabled_cores_mask */
		sdev->enabled_cores_mask &= ~BIT(core);

		dev_dbg(sdev->dev, "Core %d powered down\n", core);
	}

	return 0;
}

/* pre/post fw load */
static inline int snd_sof_dsp_pre_fw_run(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->pre_fw_run)
		return sof_ops(sdev)->pre_fw_run(sdev);

	return 0;
}

static inline int snd_sof_dsp_post_fw_run(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->post_fw_run)
		return sof_ops(sdev)->post_fw_run(sdev);

	return 0;
}

/* parse platform specific extended manifest */
static inline int snd_sof_dsp_parse_platform_ext_manifest(struct snd_sof_dev *sdev,
							  const struct sof_ext_man_elem_header *hdr)
{
	if (sof_ops(sdev)->parse_platform_ext_manifest)
		return sof_ops(sdev)->parse_platform_ext_manifest(sdev, hdr);

	return 0;
}

/* misc */

/**
 * snd_sof_dsp_get_bar_index - Maps a section type with a BAR index
 *
 * @sdev: sof device
 * @type: section type as described by snd_sof_fw_blk_type
 *
 * Returns the corresponding BAR index (a positive integer) or -EINVAL
 * in case there is no mapping
 */
static inline int snd_sof_dsp_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	if (sof_ops(sdev)->get_bar_index)
		return sof_ops(sdev)->get_bar_index(sdev, type);

	return sdev->mmio_bar;
}

static inline int snd_sof_dsp_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->get_mailbox_offset)
		return sof_ops(sdev)->get_mailbox_offset(sdev);

	dev_err(sdev->dev, "error: %s not defined\n", __func__);
	return -ENOTSUPP;
}

static inline int snd_sof_dsp_get_window_offset(struct snd_sof_dev *sdev,
						u32 id)
{
	if (sof_ops(sdev)->get_window_offset)
		return sof_ops(sdev)->get_window_offset(sdev, id);

	dev_err(sdev->dev, "error: %s not defined\n", __func__);
	return -ENOTSUPP;
}
/* power management */
static inline int snd_sof_dsp_resume(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->resume)
		return sof_ops(sdev)->resume(sdev);

	return 0;
}

static inline int snd_sof_dsp_suspend(struct snd_sof_dev *sdev,
				      u32 target_state)
{
	if (sof_ops(sdev)->suspend)
		return sof_ops(sdev)->suspend(sdev, target_state);

	return 0;
}

static inline int snd_sof_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->runtime_resume)
		return sof_ops(sdev)->runtime_resume(sdev);

	return 0;
}

static inline int snd_sof_dsp_runtime_suspend(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->runtime_suspend)
		return sof_ops(sdev)->runtime_suspend(sdev);

	return 0;
}

static inline int snd_sof_dsp_runtime_idle(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->runtime_idle)
		return sof_ops(sdev)->runtime_idle(sdev);

	return 0;
}

static inline int snd_sof_dsp_hw_params_upon_resume(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->set_hw_params_upon_resume)
		return sof_ops(sdev)->set_hw_params_upon_resume(sdev);
	return 0;
}

static inline int snd_sof_dsp_set_clk(struct snd_sof_dev *sdev, u32 freq)
{
	if (sof_ops(sdev)->set_clk)
		return sof_ops(sdev)->set_clk(sdev, freq);

	return 0;
}

static inline int
snd_sof_dsp_set_power_state(struct snd_sof_dev *sdev,
			    const struct sof_dsp_power_state *target_state)
{
	int ret = 0;

	mutex_lock(&sdev->power_state_access);

	if (sof_ops(sdev)->set_power_state)
		ret = sof_ops(sdev)->set_power_state(sdev, target_state);

	mutex_unlock(&sdev->power_state_access);

	return ret;
}

/* debug */
void snd_sof_dsp_dbg_dump(struct snd_sof_dev *sdev, u32 flags);

static inline int snd_sof_debugfs_add_region_item(struct snd_sof_dev *sdev,
		enum snd_sof_fw_blk_type blk_type, u32 offset, size_t size,
		const char *name, enum sof_debugfs_access_type access_type)
{
	if (sof_ops(sdev) && sof_ops(sdev)->debugfs_add_region_item)
		return sof_ops(sdev)->debugfs_add_region_item(sdev, blk_type, offset,
							      size, name, access_type);

	return 0;
}

/* register IO */
static inline void snd_sof_dsp_write(struct snd_sof_dev *sdev, u32 bar,
				     u32 offset, u32 value)
{
	if (sof_ops(sdev)->write) {
		sof_ops(sdev)->write(sdev, sdev->bar[bar] + offset, value);
		return;
	}

	dev_err_ratelimited(sdev->dev, "error: %s not defined\n", __func__);
}

static inline void snd_sof_dsp_write64(struct snd_sof_dev *sdev, u32 bar,
				       u32 offset, u64 value)
{
	if (sof_ops(sdev)->write64) {
		sof_ops(sdev)->write64(sdev, sdev->bar[bar] + offset, value);
		return;
	}

	dev_err_ratelimited(sdev->dev, "error: %s not defined\n", __func__);
}

static inline u32 snd_sof_dsp_read(struct snd_sof_dev *sdev, u32 bar,
				   u32 offset)
{
	if (sof_ops(sdev)->read)
		return sof_ops(sdev)->read(sdev, sdev->bar[bar] + offset);

	dev_err(sdev->dev, "error: %s not defined\n", __func__);
	return -ENOTSUPP;
}

static inline u64 snd_sof_dsp_read64(struct snd_sof_dev *sdev, u32 bar,
				     u32 offset)
{
	if (sof_ops(sdev)->read64)
		return sof_ops(sdev)->read64(sdev, sdev->bar[bar] + offset);

	dev_err(sdev->dev, "error: %s not defined\n", __func__);
	return -ENOTSUPP;
}

/* block IO */
static inline int snd_sof_dsp_block_read(struct snd_sof_dev *sdev,
					 enum snd_sof_fw_blk_type blk_type,
					 u32 offset, void *dest, size_t bytes)
{
	return sof_ops(sdev)->block_read(sdev, blk_type, offset, dest, bytes);
}

static inline int snd_sof_dsp_block_write(struct snd_sof_dev *sdev,
					  enum snd_sof_fw_blk_type blk_type,
					  u32 offset, void *src, size_t bytes)
{
	return sof_ops(sdev)->block_write(sdev, blk_type, offset, src, bytes);
}

/* mailbox IO */
static inline void snd_sof_dsp_mailbox_read(struct snd_sof_dev *sdev,
					    u32 offset, void *dest, size_t bytes)
{
	if (sof_ops(sdev)->mailbox_read)
		sof_ops(sdev)->mailbox_read(sdev, offset, dest, bytes);
}

static inline void snd_sof_dsp_mailbox_write(struct snd_sof_dev *sdev,
					     u32 offset, void *src, size_t bytes)
{
	if (sof_ops(sdev)->mailbox_write)
		sof_ops(sdev)->mailbox_write(sdev, offset, src, bytes);
}

/* ipc */
static inline int snd_sof_dsp_send_msg(struct snd_sof_dev *sdev,
				       struct snd_sof_ipc_msg *msg)
{
	return sof_ops(sdev)->send_msg(sdev, msg);
}

/* host DMA trace */
static inline int snd_sof_dma_trace_init(struct snd_sof_dev *sdev,
					 u32 *stream_tag)
{
	if (sof_ops(sdev)->trace_init)
		return sof_ops(sdev)->trace_init(sdev, stream_tag);

	return 0;
}

static inline int snd_sof_dma_trace_release(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->trace_release)
		return sof_ops(sdev)->trace_release(sdev);

	return 0;
}

static inline int snd_sof_dma_trace_trigger(struct snd_sof_dev *sdev, int cmd)
{
	if (sof_ops(sdev)->trace_trigger)
		return sof_ops(sdev)->trace_trigger(sdev, cmd);

	return 0;
}

/* host PCM ops */
static inline int
snd_sof_pcm_platform_open(struct snd_sof_dev *sdev,
			  struct snd_pcm_substream *substream)
{
	if (sof_ops(sdev) && sof_ops(sdev)->pcm_open)
		return sof_ops(sdev)->pcm_open(sdev, substream);

	return 0;
}

/* disconnect pcm substream to a host stream */
static inline int
snd_sof_pcm_platform_close(struct snd_sof_dev *sdev,
			   struct snd_pcm_substream *substream)
{
	if (sof_ops(sdev) && sof_ops(sdev)->pcm_close)
		return sof_ops(sdev)->pcm_close(sdev, substream);

	return 0;
}

/* host stream hw params */
static inline int
snd_sof_pcm_platform_hw_params(struct snd_sof_dev *sdev,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct sof_ipc_stream_params *ipc_params)
{
	if (sof_ops(sdev) && sof_ops(sdev)->pcm_hw_params)
		return sof_ops(sdev)->pcm_hw_params(sdev, substream,
						    params, ipc_params);

	return 0;
}

/* host stream hw free */
static inline int
snd_sof_pcm_platform_hw_free(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream)
{
	if (sof_ops(sdev) && sof_ops(sdev)->pcm_hw_free)
		return sof_ops(sdev)->pcm_hw_free(sdev, substream);

	return 0;
}

/* host stream trigger */
static inline int
snd_sof_pcm_platform_trigger(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream, int cmd)
{
	if (sof_ops(sdev) && sof_ops(sdev)->pcm_trigger)
		return sof_ops(sdev)->pcm_trigger(sdev, substream, cmd);

	return 0;
}

/* Firmware loading */
static inline int snd_sof_load_firmware(struct snd_sof_dev *sdev)
{
	dev_dbg(sdev->dev, "loading firmware\n");

	return sof_ops(sdev)->load_firmware(sdev);
}

/* host DSP message data */
static inline int snd_sof_ipc_msg_data(struct snd_sof_dev *sdev,
				       struct snd_pcm_substream *substream,
				       void *p, size_t sz)
{
	return sof_ops(sdev)->ipc_msg_data(sdev, substream, p, sz);
}

/* host configure DSP HW parameters */
static inline int
snd_sof_ipc_pcm_params(struct snd_sof_dev *sdev,
		       struct snd_pcm_substream *substream,
		       const struct sof_ipc_pcm_params_reply *reply)
{
	return sof_ops(sdev)->ipc_pcm_params(sdev, substream, reply);
}

/* host stream pointer */
static inline snd_pcm_uframes_t
snd_sof_pcm_platform_pointer(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream)
{
	if (sof_ops(sdev) && sof_ops(sdev)->pcm_pointer)
		return sof_ops(sdev)->pcm_pointer(sdev, substream);

	return 0;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_PROBES)
static inline int
snd_sof_probe_compr_assign(struct snd_sof_dev *sdev,
		struct snd_compr_stream *cstream, struct snd_soc_dai *dai)
{
	return sof_ops(sdev)->probe_assign(sdev, cstream, dai);
}

static inline int
snd_sof_probe_compr_free(struct snd_sof_dev *sdev,
		struct snd_compr_stream *cstream, struct snd_soc_dai *dai)
{
	return sof_ops(sdev)->probe_free(sdev, cstream, dai);
}

static inline int
snd_sof_probe_compr_set_params(struct snd_sof_dev *sdev,
		struct snd_compr_stream *cstream,
		struct snd_compr_params *params, struct snd_soc_dai *dai)
{
	return sof_ops(sdev)->probe_set_params(sdev, cstream, params, dai);
}

static inline int
snd_sof_probe_compr_trigger(struct snd_sof_dev *sdev,
		struct snd_compr_stream *cstream, int cmd,
		struct snd_soc_dai *dai)
{
	return sof_ops(sdev)->probe_trigger(sdev, cstream, cmd, dai);
}

static inline int
snd_sof_probe_compr_pointer(struct snd_sof_dev *sdev,
		struct snd_compr_stream *cstream,
		struct snd_compr_tstamp *tstamp, struct snd_soc_dai *dai)
{
	if (sof_ops(sdev) && sof_ops(sdev)->probe_pointer)
		return sof_ops(sdev)->probe_pointer(sdev, cstream, tstamp, dai);

	return 0;
}
#endif

/* machine driver */
static inline int
snd_sof_machine_register(struct snd_sof_dev *sdev, void *pdata)
{
	if (sof_ops(sdev) && sof_ops(sdev)->machine_register)
		return sof_ops(sdev)->machine_register(sdev, pdata);

	return 0;
}

static inline void
snd_sof_machine_unregister(struct snd_sof_dev *sdev, void *pdata)
{
	if (sof_ops(sdev) && sof_ops(sdev)->machine_unregister)
		sof_ops(sdev)->machine_unregister(sdev, pdata);
}

static inline void
snd_sof_machine_select(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev) && sof_ops(sdev)->machine_select)
		sof_ops(sdev)->machine_select(sdev);
}

static inline void
snd_sof_set_mach_params(const struct snd_soc_acpi_mach *mach,
			struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev) && sof_ops(sdev)->set_mach_params)
		sof_ops(sdev)->set_mach_params(mach, sdev);
}

/**
 * snd_sof_dsp_register_poll_timeout - Periodically poll an address
 * until a condition is met or a timeout occurs
 * @op: accessor function (takes @addr as its only argument)
 * @addr: Address to poll
 * @val: Variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep between reads in us (0
 *            tight-loops).  Should be less than ~20ms since usleep_range
 *            is used (see Documentation/timers/timers-howto.rst).
 * @timeout_us: Timeout in us, 0 means never timeout
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @addr is stored in @val. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 *
 * This is modelled after the readx_poll_timeout macros in linux/iopoll.h.
 */
#define snd_sof_dsp_read_poll_timeout(sdev, bar, offset, val, cond, sleep_us, timeout_us) \
({ \
	u64 __timeout_us = (timeout_us); \
	unsigned long __sleep_us = (sleep_us); \
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us); \
	might_sleep_if((__sleep_us) != 0); \
	for (;;) {							\
		(val) = snd_sof_dsp_read(sdev, bar, offset);		\
		if (cond) { \
			dev_dbg(sdev->dev, \
				"FW Poll Status: reg[%#x]=%#x successful\n", \
				(offset), (val)); \
			break; \
		} \
		if (__timeout_us && \
		    ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = snd_sof_dsp_read(sdev, bar, offset); \
			dev_dbg(sdev->dev, \
				"FW Poll Status: reg[%#x]=%#x timedout\n", \
				(offset), (val)); \
			break; \
		} \
		if (__sleep_us) \
			usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

/* This is for registers bits with attribute RWC */
bool snd_sof_pci_update_bits(struct snd_sof_dev *sdev, u32 offset,
			     u32 mask, u32 value);

bool snd_sof_dsp_update_bits_unlocked(struct snd_sof_dev *sdev, u32 bar,
				      u32 offset, u32 mask, u32 value);

bool snd_sof_dsp_update_bits64_unlocked(struct snd_sof_dev *sdev, u32 bar,
					u32 offset, u64 mask, u64 value);

bool snd_sof_dsp_update_bits(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			     u32 mask, u32 value);

bool snd_sof_dsp_update_bits64(struct snd_sof_dev *sdev, u32 bar,
			       u32 offset, u64 mask, u64 value);

void snd_sof_dsp_update_bits_forced(struct snd_sof_dev *sdev, u32 bar,
				    u32 offset, u32 mask, u32 value);

int snd_sof_dsp_register_poll(struct snd_sof_dev *sdev, u32 bar, u32 offset,
			      u32 mask, u32 target, u32 timeout_ms,
			      u32 interval_us);

void snd_sof_dsp_panic(struct snd_sof_dev *sdev, u32 offset);
#endif
