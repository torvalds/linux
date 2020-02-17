// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include "ops.h"
#include "sof-priv.h"
#include "sof-audio.h"

static int sof_send_pm_ctx_ipc(struct snd_sof_dev *sdev, int cmd)
{
	struct sof_ipc_pm_ctx pm_ctx;
	struct sof_ipc_reply reply;

	memset(&pm_ctx, 0, sizeof(pm_ctx));

	/* configure ctx save ipc message */
	pm_ctx.hdr.size = sizeof(pm_ctx);
	pm_ctx.hdr.cmd = SOF_IPC_GLB_PM_MSG | cmd;

	/* send ctx save ipc to dsp */
	return sof_ipc_tx_message(sdev->ipc, pm_ctx.hdr.cmd, &pm_ctx,
				 sizeof(pm_ctx), &reply, sizeof(reply));
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_DEBUGFS_CACHE)
static void sof_cache_debugfs(struct snd_sof_dev *sdev)
{
	struct snd_sof_dfsentry *dfse;

	list_for_each_entry(dfse, &sdev->dfsentry_list, list) {

		/* nothing to do if debugfs buffer is not IO mem */
		if (dfse->type == SOF_DFSENTRY_TYPE_BUF)
			continue;

		/* cache memory that is only accessible in D0 */
		if (dfse->access_type == SOF_DEBUGFS_ACCESS_D0_ONLY)
			memcpy_fromio(dfse->cache_buf, dfse->io_mem,
				      dfse->size);
	}
}
#endif

static int sof_resume(struct device *dev, bool runtime_resume)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	int ret;

	/* do nothing if dsp resume callbacks are not set */
	if (!sof_ops(sdev)->resume || !sof_ops(sdev)->runtime_resume)
		return 0;

	/* DSP was never successfully started, nothing to resume */
	if (sdev->first_boot)
		return 0;

	/*
	 * if the runtime_resume flag is set, call the runtime_resume routine
	 * or else call the system resume routine
	 */
	if (runtime_resume)
		ret = snd_sof_dsp_runtime_resume(sdev);
	else
		ret = snd_sof_dsp_resume(sdev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to power up DSP after resume\n");
		return ret;
	}

	sdev->fw_state = SOF_FW_BOOT_PREPARE;

	/* load the firmware */
	ret = snd_sof_load_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to load DSP firmware after resume %d\n",
			ret);
		return ret;
	}

	sdev->fw_state = SOF_FW_BOOT_IN_PROGRESS;

	/*
	 * Boot the firmware. The FW boot status will be modified
	 * in snd_sof_run_firmware() depending on the outcome.
	 */
	ret = snd_sof_run_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to boot DSP firmware after resume %d\n",
			ret);
		return ret;
	}

	/* resume DMA trace, only need send ipc */
	ret = snd_sof_init_trace_ipc(sdev);
	if (ret < 0) {
		/* non fatal */
		dev_warn(sdev->dev,
			 "warning: failed to init trace after resume %d\n",
			 ret);
	}

	/* restore pipelines */
	ret = sof_restore_pipelines(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to restore pipeline after resume %d\n",
			ret);
		return ret;
	}

	/* notify DSP of system resume */
	ret = sof_send_pm_ctx_ipc(sdev, SOF_IPC_PM_CTX_RESTORE);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: ctx_restore ipc error during resume %d\n",
			ret);

	/* initialize default D0 sub-state */
	sdev->d0_substate = SOF_DSP_D0I0;

	return ret;
}

static int sof_suspend(struct device *dev, bool runtime_suspend)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	int ret;

	/* do nothing if dsp suspend callback is not set */
	if (!sof_ops(sdev)->suspend)
		return 0;

	if (sdev->fw_state != SOF_FW_BOOT_COMPLETE)
		goto power_down;

	/* release trace */
	snd_sof_release_trace(sdev);

	/* set restore_stream for all streams during system suspend */
	if (!runtime_suspend) {
		ret = sof_set_hw_params_upon_resume(sdev->dev);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: setting hw_params flag during suspend %d\n",
				ret);
			return ret;
		}
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_DEBUGFS_CACHE)
	/* cache debugfs contents during runtime suspend */
	if (runtime_suspend)
		sof_cache_debugfs(sdev);
#endif
	/* notify DSP of upcoming power down */
	ret = sof_send_pm_ctx_ipc(sdev, SOF_IPC_PM_CTX_SAVE);
	if (ret == -EBUSY || ret == -EAGAIN) {
		/*
		 * runtime PM has logic to handle -EBUSY/-EAGAIN so
		 * pass these errors up
		 */
		dev_err(sdev->dev,
			"error: ctx_save ipc error during suspend %d\n",
			ret);
		return ret;
	} else if (ret < 0) {
		/* FW in unexpected state, continue to power down */
		dev_warn(sdev->dev,
			 "ctx_save ipc error %d, proceeding with suspend\n",
			 ret);
	}

power_down:

	/* return if the DSP was not probed successfully */
	if (sdev->fw_state == SOF_FW_BOOT_NOT_STARTED)
		return 0;

	/* power down all DSP cores */
	if (runtime_suspend)
		ret = snd_sof_dsp_runtime_suspend(sdev);
	else
		ret = snd_sof_dsp_suspend(sdev);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: failed to power down DSP during suspend %d\n",
			ret);

	/* reset FW state */
	sdev->fw_state = SOF_FW_BOOT_NOT_STARTED;

	return ret;
}

int snd_sof_runtime_suspend(struct device *dev)
{
	return sof_suspend(dev, true);
}
EXPORT_SYMBOL(snd_sof_runtime_suspend);

int snd_sof_runtime_idle(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);

	return snd_sof_dsp_runtime_idle(sdev);
}
EXPORT_SYMBOL(snd_sof_runtime_idle);

int snd_sof_runtime_resume(struct device *dev)
{
	return sof_resume(dev, true);
}
EXPORT_SYMBOL(snd_sof_runtime_resume);

int snd_sof_set_d0_substate(struct snd_sof_dev *sdev,
			    enum sof_d0_substate d0_substate)
{
	int ret;

	if (sdev->d0_substate == d0_substate)
		return 0;

	/* do platform specific set_state */
	ret = snd_sof_dsp_set_power_state(sdev, d0_substate);
	if (ret < 0)
		return ret;

	/* update dsp D0 sub-state */
	sdev->d0_substate = d0_substate;

	return 0;
}
EXPORT_SYMBOL(snd_sof_set_d0_substate);

/*
 * Audio DSP states may transform as below:-
 *
 *                                         D0I3 compatible stream
 *     Runtime    +---------------------+   opened only, timeout
 *     suspend    |                     +--------------------+
 *   +------------+       D0(active)    |                    |
 *   |            |                     <---------------+    |
 *   |   +-------->                     |               |    |
 *   |   |Runtime +--^--+---------^--+--+ The last      |    |
 *   |   |resume     |  |         |  |    opened D0I3   |    |
 *   |   |           |  |         |  |    compatible    |    |
 *   |   |     resume|  |         |  |    stream closed |    |
 *   |   |      from |  | D3      |  |                  |    |
 *   |   |       D3  |  |suspend  |  | d0i3             |    |
 *   |   |           |  |         |  |suspend           |    |
 *   |   |           |  |         |  |                  |    |
 *   |   |           |  |         |  |                  |    |
 * +-v---+-----------+--v-------+ |  |           +------+----v----+
 * |                            | |  +----------->                |
 * |       D3 (suspended)       | |              |      D0I3      +-----+
 * |                            | +--------------+                |     |
 * |                            |  resume from   |                |     |
 * +-------------------^--------+  d0i3 suspend  +----------------+     |
 *                     |                                                |
 *                     |                       D3 suspend               |
 *                     +------------------------------------------------+
 *
 * d0i3_suspend = s0_suspend && D0I3 stream opened,
 * D3 suspend = !d0i3_suspend,
 */

int snd_sof_resume(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	int ret;

	if (snd_sof_dsp_d0i3_on_suspend(sdev)) {
		/* resume from D0I3 */
		dev_dbg(sdev->dev, "DSP will exit from D0i3...\n");
		ret = snd_sof_set_d0_substate(sdev, SOF_DSP_D0I0);
		if (ret == -ENOTSUPP) {
			/* fallback to resume from D3 */
			dev_dbg(sdev->dev, "D0i3 not supported, fall back to resume from D3...\n");
			goto d3_resume;
		} else if (ret < 0) {
			dev_err(sdev->dev, "error: failed to exit from D0I3 %d\n",
				ret);
			return ret;
		}

		/* platform-specific resume from D0i3 */
		return snd_sof_dsp_resume(sdev);
	}

d3_resume:
	/* resume from D3 */
	return sof_resume(dev, false);
}
EXPORT_SYMBOL(snd_sof_resume);

int snd_sof_suspend(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	int ret;

	if (snd_sof_dsp_d0i3_on_suspend(sdev)) {
		/* suspend to D0i3 */
		dev_dbg(sdev->dev, "DSP is trying to enter D0i3...\n");
		ret = snd_sof_set_d0_substate(sdev, SOF_DSP_D0I3);
		if (ret == -ENOTSUPP) {
			/* fallback to D3 suspend */
			dev_dbg(sdev->dev, "D0i3 not supported, fall back to D3...\n");
			goto d3_suspend;
		} else if (ret < 0) {
			dev_err(sdev->dev, "error: failed to enter D0I3, %d\n",
				ret);
			return ret;
		}

		/* platform-specific suspend to D0i3 */
		return snd_sof_dsp_suspend(sdev);
	}

d3_suspend:
	/* suspend to D3 */
	return sof_suspend(dev, false);
}
EXPORT_SYMBOL(snd_sof_suspend);

int snd_sof_prepare(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);

#if defined(CONFIG_ACPI)
	sdev->s0_suspend = acpi_target_system_state() == ACPI_STATE_S0;
#else
	/* will suspend to S3 by default */
	sdev->s0_suspend = false;
#endif

	return 0;
}
EXPORT_SYMBOL(snd_sof_prepare);

void snd_sof_complete(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);

	sdev->s0_suspend = false;
}
EXPORT_SYMBOL(snd_sof_complete);
