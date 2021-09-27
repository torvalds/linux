// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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

/*
 * Helper function to determine the target DSP state during
 * system suspend. This function only cares about the device
 * D-states. Platform-specific substates, if any, should be
 * handled by the platform-specific parts.
 */
static u32 snd_sof_dsp_power_target(struct snd_sof_dev *sdev)
{
	u32 target_dsp_state;

	switch (sdev->system_suspend_target) {
	case SOF_SUSPEND_S3:
		/* DSP should be in D3 if the system is suspending to S3 */
		target_dsp_state = SOF_DSP_PM_D3;
		break;
	case SOF_SUSPEND_S0IX:
		/*
		 * Currently, the only criterion for retaining the DSP in D0
		 * is that there are streams that ignored the suspend trigger.
		 * Additional criteria such Soundwire clock-stop mode and
		 * device suspend latency considerations will be added later.
		 */
		if (snd_sof_stream_suspend_ignored(sdev))
			target_dsp_state = SOF_DSP_PM_D0;
		else
			target_dsp_state = SOF_DSP_PM_D3;
		break;
	default:
		/* This case would be during runtime suspend */
		target_dsp_state = SOF_DSP_PM_D3;
		break;
	}

	return target_dsp_state;
}

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
	u32 old_state = sdev->dsp_power_state.state;
	int ret;

	/* do nothing if dsp resume callbacks are not set */
	if (!runtime_resume && !sof_ops(sdev)->resume)
		return 0;

	if (runtime_resume && !sof_ops(sdev)->runtime_resume)
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

	/*
	 * Nothing further to be done for platforms that support the low power
	 * D0 substate.
	 */
	if (!runtime_resume && sof_ops(sdev)->set_power_state &&
	    old_state == SOF_DSP_PM_D0)
		return 0;

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
	ret = sof_set_up_pipelines(sdev->dev, false);
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

	return ret;
}

static int sof_suspend(struct device *dev, bool runtime_suspend)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	u32 target_state = 0;
	int ret;

	/* do nothing if dsp suspend callback is not set */
	if (!runtime_suspend && !sof_ops(sdev)->suspend)
		return 0;

	if (runtime_suspend && !sof_ops(sdev)->runtime_suspend)
		return 0;

	if (sdev->fw_state != SOF_FW_BOOT_COMPLETE)
		goto suspend;

	/* prepare for streams to be resumed properly upon resume */
	if (!runtime_suspend) {
		ret = sof_set_hw_params_upon_resume(sdev->dev);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: setting hw_params flag during suspend %d\n",
				ret);
			return ret;
		}
	}

	target_state = snd_sof_dsp_power_target(sdev);

	/* Skip to platform-specific suspend if DSP is entering D0 */
	if (target_state == SOF_DSP_PM_D0)
		goto suspend;

	sof_tear_down_pipelines(dev, false);

	/* release trace */
	snd_sof_release_trace(sdev);

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

suspend:

	/* return if the DSP was not probed successfully */
	if (sdev->fw_state == SOF_FW_BOOT_NOT_STARTED)
		return 0;

	/* platform-specific suspend */
	if (runtime_suspend)
		ret = snd_sof_dsp_runtime_suspend(sdev);
	else
		ret = snd_sof_dsp_suspend(sdev, target_state);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: failed to power down DSP during suspend %d\n",
			ret);

	/* Do not reset FW state if DSP is in D0 */
	if (target_state == SOF_DSP_PM_D0)
		return ret;

	/* reset FW state */
	sdev->fw_state = SOF_FW_BOOT_NOT_STARTED;
	sdev->enabled_cores_mask = 0;

	return ret;
}

int snd_sof_dsp_power_down_notify(struct snd_sof_dev *sdev)
{
	/* Notify DSP of upcoming power down */
	if (sof_ops(sdev)->remove)
		return sof_send_pm_ctx_ipc(sdev, SOF_IPC_PM_CTX_SAVE);

	return 0;
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

int snd_sof_resume(struct device *dev)
{
	return sof_resume(dev, false);
}
EXPORT_SYMBOL(snd_sof_resume);

int snd_sof_suspend(struct device *dev)
{
	return sof_suspend(dev, false);
}
EXPORT_SYMBOL(snd_sof_suspend);

int snd_sof_prepare(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	const struct sof_dev_desc *desc = sdev->pdata->desc;

	/* will suspend to S3 by default */
	sdev->system_suspend_target = SOF_SUSPEND_S3;

	if (!desc->use_acpi_target_states)
		return 0;

#if defined(CONFIG_ACPI)
	if (acpi_target_system_state() == ACPI_STATE_S0)
		sdev->system_suspend_target = SOF_SUSPEND_S0IX;
#endif

	return 0;
}
EXPORT_SYMBOL(snd_sof_prepare);

void snd_sof_complete(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);

	sdev->system_suspend_target = SOF_SUSPEND_NONE;
}
EXPORT_SYMBOL(snd_sof_complete);
