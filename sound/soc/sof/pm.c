// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/mm.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "ops.h"
#include "sof-priv.h"

#define RUNTIME_PM 1

static int sof_restore_pipelines(struct snd_sof_dev *sdev)
{
	struct snd_sof_widget *swidget = NULL;
	struct snd_sof_route *sroute = NULL;
	struct snd_sof_dai *dai;
	struct sof_ipc_comp_dai *comp_dai;
	struct sof_ipc_hdr *hdr;
	struct snd_sof_control *scontrol = NULL;
	int ipc_cmd, ctrl_type, sof_abi;
	int ret = 0;

	/* restore pipeline components */
	list_for_each_entry_reverse(swidget, &sdev->widget_list, list) {
		struct sof_ipc_comp_reply r;

		/* skip if there is no private data */
		if (!swidget->private)
			continue;

		switch (swidget->id) {
		case snd_soc_dapm_dai_in:
			/* fallthrough */
		case snd_soc_dapm_dai_out:
			dai = (struct snd_sof_dai *)swidget->private;
			comp_dai = &dai->comp_dai;
			ret = sof_ipc_tx_message(sdev->ipc,
						 comp_dai->comp.hdr.cmd,
						 comp_dai, sizeof(*comp_dai),
						 &r, sizeof(r));
			break;
		default:
			hdr = (struct sof_ipc_hdr *)swidget->private;
			ret = sof_ipc_tx_message(sdev->ipc, hdr->cmd,
						 swidget->private, hdr->size,
						 &r, sizeof(r));
			break;
		}
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: failed to load widget type %d with ID: %d\n",
				swidget->widget->id, swidget->comp_id);

			return ret;
		}
	}

	/* restore pipeline connections */
	list_for_each_entry_reverse(sroute, &sdev->route_list, list) {
		struct sof_ipc_pipe_comp_connect *connect;
		struct sof_ipc_reply reply;

		/* skip if there's no private data */
		if (!sroute->private)
			continue;

		connect = sroute->private;

		/* send ipc */
		ret = sof_ipc_tx_message(sdev->ipc,
					 connect->hdr.cmd,
					 connect, sizeof(*connect),
					 &reply, sizeof(reply));
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: failed to load route sink %s control %s source %s\n",
				sroute->route.sink,
				sroute->route.control ? sroute->route.control
					: "none",
				sroute->route.source);

			return ret;
		}
	}

	/* restore dai links */
	list_for_each_entry_reverse(dai, &sdev->dai_list, list) {
		struct sof_ipc_reply reply;
		struct sof_ipc_dai_config *config = &dai->dai_config;

		ret = sof_ipc_tx_message(sdev->ipc,
					 config->hdr.cmd, config,
					 sizeof(*config),
					 &reply, sizeof(reply));

		if (ret < 0) {
			dev_err(sdev->dev,
				"error: failed to set dai config for %s\n",
				dai->name);

			return ret;
		}
	}

	/* complete pipeline */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		switch (swidget->id) {
		case snd_soc_dapm_scheduler:
			swidget->complete =
				snd_sof_complete_pipeline(sdev, swidget);
			break;
		default:
			break;
		}
	}

	/* restore kcontrol values */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {

		/* notify DSP of kcontrol values */
		switch (scontrol->cmd) {
		case SOF_CTRL_CMD_VOLUME:
			ipc_cmd = SOF_IPC_COMP_SET_VALUE;
			ctrl_type = SOF_CTRL_TYPE_VALUE_CHAN_SET;
			ret = snd_sof_ipc_set_comp_data(sdev->ipc, scontrol,
							ipc_cmd, ctrl_type,
							scontrol->cmd);
			break;
		case SOF_CTRL_CMD_BINARY:

			/* Check if control data contains valid data.
			 * SOF_ABI_MAGIC will not match if there is no data.
			 */
			ipc_cmd = SOF_IPC_COMP_SET_DATA;
			ctrl_type = SOF_CTRL_TYPE_DATA_SET;
			sof_abi = scontrol->control_data->data->magic;
			if (sof_abi == SOF_ABI_MAGIC)
				ret = snd_sof_ipc_set_comp_data(sdev->ipc,
								scontrol,
								ipc_cmd,
								ctrl_type,
								scontrol->cmd);
			break;

		default:
			break;
		}
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: failed kcontrol value set for widget: %d\n",
				scontrol->comp_id);

			return ret;
		}
	}

	return 0;
}

static int sof_send_pm_ipc(struct snd_sof_dev *sdev, int cmd)
{
	struct sof_ipc_pm_ctx pm_ctx;

	memset(&pm_ctx, 0, sizeof(pm_ctx));

	/* configure ctx save ipc message */
	pm_ctx.hdr.size = sizeof(pm_ctx);
	pm_ctx.hdr.cmd = SOF_IPC_GLB_PM_MSG | cmd;

	/* send ctx save ipc to dsp */
	return sof_ipc_tx_message(sdev->ipc, pm_ctx.hdr.cmd, &pm_ctx,
				 sizeof(pm_ctx), &pm_ctx, sizeof(pm_ctx));
}

static void sof_suspend_streams(struct snd_sof_dev *sdev)
{
	struct snd_sof_pcm *spcm;
	struct snd_pcm_substream *substream;
	int dir;

	/* suspend all running streams */
	list_for_each_entry(spcm, &sdev->pcm_list, list) {

		mutex_lock(&spcm->mutex);

		/* suspend running playback stream */
		dir = SNDRV_PCM_STREAM_PLAYBACK;
		substream = spcm->stream[dir].substream;

		if (substream && substream->runtime) {

			snd_pcm_suspend(substream);

			/*
			 * set restore_stream so that hw_params can be
			 * restored during resume
			 */
			spcm->restore_stream[dir] = 1;
		}

		/* suspend running capture stream */
		dir = SNDRV_PCM_STREAM_CAPTURE;
		substream = spcm->stream[dir].substream;

		if (substream && substream->runtime) {

			snd_pcm_suspend(substream);

			/*
			 * set restore_stream so that hw_params can be
			 * restored during resume
			 */
			spcm->restore_stream[dir] = 1;
		}

		mutex_unlock(&spcm->mutex);
	}
}

static int sof_resume(struct device *dev, int runtime_resume)
{
	struct sof_platform_priv *priv = dev_get_drvdata(dev);
	struct snd_sof_dev *sdev = dev_get_drvdata(&priv->pdev_pcm->dev);
	int ret = 0;

	/* do nothing if dsp resume callbacks are not set */
	if (!sdev->ops->resume || !sdev->ops->runtime_resume)
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

	/* load the firmware */
	ret = snd_sof_load_firmware(sdev, false);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to load DSP firmware after resume %d\n",
			ret);
		return ret;
	}

	/* boot the firmware */
	ret = snd_sof_run_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to boot DSP firmware after resume %d\n",
			ret);
		return ret;
	}

	/* init DMA trace */
	ret = snd_sof_init_trace_ipc(sdev);
	if (ret < 0) {
		/* non fatal */
		dev_warn(sdev->dev,
			 "warning: failed to init trace after resume %d\n",
			 ret);
	}

	/* restore pipelines */
	ret = sof_restore_pipelines(sdev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to restore pipeline after resume %d\n",
			ret);
		return ret;
	}

	/* notify DSP of system resume */
	ret = sof_send_pm_ipc(sdev, SOF_IPC_PM_CTX_RESTORE);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: ctx_restore ipc error during resume %d\n",
			ret);

	return ret;
}

static int sof_suspend(struct device *dev, int runtime_suspend)
{
	struct sof_platform_priv *priv = dev_get_drvdata(dev);
	struct snd_sof_dev *sdev = dev_get_drvdata(&priv->pdev_pcm->dev);
	int ret = 0;

	/* do nothing if dsp suspend callback is not set */
	if (!sdev->ops->suspend)
		return 0;

	/*
	 * Suspend running pcm streams.
	 * They will be restarted by ALSA resume trigger call.
	 */
	sof_suspend_streams(sdev);

	/* notify DSP of upcoming power down */
	ret = sof_send_pm_ipc(sdev, SOF_IPC_PM_CTX_SAVE);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: ctx_save ipc error during suspend %d\n",
			ret);
		return ret;
	}

	/* drop all ipc */
	sof_ipc_drop_all(sdev->ipc);

	/* release trace */
	snd_sof_dma_trace_release(sdev);

	/* power down DSP */
	if (runtime_suspend)
		ret = snd_sof_dsp_runtime_suspend(sdev, 0);
	else
		ret = snd_sof_dsp_suspend(sdev, 0);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: failed to power down DSP during suspend %d\n",
			ret);

	return ret;
}

int snd_sof_runtime_suspend(struct device *dev)
{
	return sof_suspend(dev, RUNTIME_PM);
}
EXPORT_SYMBOL(snd_sof_runtime_suspend);

int snd_sof_runtime_resume(struct device *dev)
{
	return sof_resume(dev, RUNTIME_PM);
}
EXPORT_SYMBOL(snd_sof_runtime_resume);

int snd_sof_resume(struct device *dev)
{
	return sof_resume(dev, !RUNTIME_PM);
}
EXPORT_SYMBOL(snd_sof_resume);

int snd_sof_suspend(struct device *dev)
{
	return sof_suspend(dev, !RUNTIME_PM);
}
EXPORT_SYMBOL(snd_sof_suspend);

int snd_sof_suspend_late(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL(snd_sof_suspend_late);
