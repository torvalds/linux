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

static int sof_restore_kcontrols(struct snd_sof_dev *sdev)
{
	struct snd_sof_control *scontrol = NULL;
	int ipc_cmd, ctrl_type;
	int ret = 0;

	/* restore kcontrol values */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
		/* reset readback offset for scontrol after resuming */
		scontrol->readback_offset = 0;

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
			ipc_cmd = SOF_IPC_COMP_SET_DATA;
			ctrl_type = SOF_CTRL_TYPE_DATA_SET;
			ret = snd_sof_ipc_set_comp_data(sdev->ipc, scontrol,
							ipc_cmd, ctrl_type,
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

static int sof_restore_pipelines(struct snd_sof_dev *sdev)
{
	struct snd_sof_widget *swidget = NULL;
	struct snd_sof_route *sroute = NULL;
	struct sof_ipc_pipe_new *pipeline;
	struct snd_sof_dai *dai;
	struct sof_ipc_comp_dai *comp_dai;
	struct sof_ipc_cmd_hdr *hdr;
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
		case snd_soc_dapm_scheduler:

			/*
			 * During suspend, all DSP cores are powered off.
			 * Therefore upon resume, create the pipeline comp
			 * and power up the core that the pipeline is
			 * scheduled on.
			 */
			pipeline = (struct sof_ipc_pipe_new *)swidget->private;
			ret = sof_load_pipeline_ipc(sdev, pipeline, &r);
			break;
		default:
			hdr = (struct sof_ipc_cmd_hdr *)swidget->private;
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
				sroute->route->sink,
				sroute->route->control ? sroute->route->control
					: "none",
				sroute->route->source);

			return ret;
		}
	}

	/* restore dai links */
	list_for_each_entry_reverse(dai, &sdev->dai_list, list) {
		struct sof_ipc_reply reply;
		struct sof_ipc_dai_config *config = dai->dai_config;

		ret = sof_ipc_tx_message(sdev->ipc,
					 config->hdr.cmd, config,
					 config->hdr.size,
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

	/* restore pipeline kcontrols */
	ret = sof_restore_kcontrols(sdev);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: restoring kcontrols after resume\n");

	return ret;
}

static int sof_send_pm_ipc(struct snd_sof_dev *sdev, int cmd)
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

static void sof_suspend_streams(struct snd_sof_dev *sdev)
{
	struct snd_sof_pcm *spcm;
	struct snd_pcm_substream *substream;
	int dir, playback_dir, capture_dir;

	/* suspend all running streams */
	list_for_each_entry(spcm, &sdev->pcm_list, list) {

		mutex_lock(&spcm->mutex);

		playback_dir = SNDRV_PCM_STREAM_PLAYBACK;
		capture_dir = SNDRV_PCM_STREAM_CAPTURE;

		/* suspend running streams in both directions */
		for (dir = playback_dir; dir <= capture_dir; dir++) {
			substream = spcm->stream[dir].substream;

			if (substream && substream->runtime) {

				snd_pcm_suspend(substream);

				/*
				 * set restore_stream so that hw_params can be
				 * restored during resume
				 */
				spcm->restore_stream[dir] = 1;
			}
		}

		mutex_unlock(&spcm->mutex);
	}
}

static int sof_resume(struct device *dev, bool runtime_resume)
{
	struct sof_platform_priv *priv = dev_get_drvdata(dev);
	struct snd_sof_dev *sdev = dev_get_drvdata(&priv->pdev_pcm->dev);
	int ret = 0;

	/* do nothing if dsp resume callbacks are not set */
	if (!sof_ops(sdev)->resume || !sof_ops(sdev)->runtime_resume)
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
	ret = snd_sof_load_firmware(sdev);
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

	/* resume DMA trace, only need send ipc */
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

static int sof_suspend(struct device *dev, bool runtime_suspend)
{
	struct sof_platform_priv *priv = dev_get_drvdata(dev);
	struct snd_sof_dev *sdev = dev_get_drvdata(&priv->pdev_pcm->dev);
	int ret = 0;

	/* do nothing if dsp suspend callback is not set */
	if (!sof_ops(sdev)->suspend)
		return 0;

	/* release trace */
	snd_sof_release_trace(sdev);

	/*
	 * Suspend running pcm streams.
	 * They will be restarted by ALSA resume trigger call.
	 */
	sof_suspend_streams(sdev);

	/* notify DSP of upcoming power down */
	ret = sof_send_pm_ipc(sdev, SOF_IPC_PM_CTX_SAVE);
	if (ret < 0) {

		/*
		 * FIXME: CTX_SAVE ipc should not time-out.
		 * But if it happens, just report the error
		 * and continue powering off the DSP for now.
		 * This will allow the DSP to power up
		 * normally upon system resume.
		 */
		dev_err(sdev->dev,
			"error: ctx_save ipc error during suspend %d\n",
			ret);
	}

	/* drop all ipc */
	sof_ipc_drop_all(sdev->ipc);

	/* power down all DSP cores */
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
	return sof_suspend(dev, true);
}
EXPORT_SYMBOL(snd_sof_runtime_suspend);

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
