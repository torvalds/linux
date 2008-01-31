/*
 * soc-core.c  --  ALSA SoC Audio Layer
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Liam Girdwood
 *         liam.girdwood@wolfsonmicro.com or linux@wolfsonmicro.com
 *         with code, comments and ideas from :-
 *         Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Revision history
 *    12th Aug 2005   Initial version.
 *    25th Oct 2005   Working Codec, Interface and Platform registration.
 *
 *  TODO:
 *   o Add hw rules to enforce rates, etc.
 *   o More testing with other codecs/machines.
 *   o Add more codecs and platforms to ensure good API coverage.
 *   o Support TDM on PCM and I2S
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

/* debug */
#define SOC_DEBUG 0
#if SOC_DEBUG
#define dbg(format, arg...) printk(format, ## arg)
#else
#define dbg(format, arg...)
#endif

static DEFINE_MUTEX(pcm_mutex);
static DEFINE_MUTEX(io_mutex);
static DECLARE_WAIT_QUEUE_HEAD(soc_pm_waitq);

/*
 * This is a timeout to do a DAPM powerdown after a stream is closed().
 * It can be used to eliminate pops between different playback streams, e.g.
 * between two audio tracks.
 */
static int pmdown_time = 5000;
module_param(pmdown_time, int, 0);
MODULE_PARM_DESC(pmdown_time, "DAPM stream powerdown time (msecs)");

/*
 * This function forces any delayed work to be queued and run.
 */
static int run_delayed_work(struct delayed_work *dwork)
{
	int ret;

	/* cancel any work waiting to be queued. */
	ret = cancel_delayed_work(dwork);

	/* if there was any work waiting then we run it now and
	 * wait for it's completion */
	if (ret) {
		schedule_delayed_work(dwork, 0);
		flush_scheduled_work();
	}
	return ret;
}

#ifdef CONFIG_SND_SOC_AC97_BUS
/* unregister ac97 codec */
static int soc_ac97_dev_unregister(struct snd_soc_codec *codec)
{
	if (codec->ac97->dev.bus)
		device_unregister(&codec->ac97->dev);
	return 0;
}

/* stop no dev release warning */
static void soc_ac97_device_release(struct device *dev){}

/* register ac97 codec to bus */
static int soc_ac97_dev_register(struct snd_soc_codec *codec)
{
	int err;

	codec->ac97->dev.bus = &ac97_bus_type;
	codec->ac97->dev.parent = NULL;
	codec->ac97->dev.release = soc_ac97_device_release;

	snprintf(codec->ac97->dev.bus_id, BUS_ID_SIZE, "%d-%d:%s",
		 codec->card->number, 0, codec->name);
	err = device_register(&codec->ac97->dev);
	if (err < 0) {
		snd_printk(KERN_ERR "Can't register ac97 bus\n");
		codec->ac97->dev.bus = NULL;
		return err;
	}
	return 0;
}
#endif

static inline const char* get_dai_name(int type)
{
	switch(type) {
	case SND_SOC_DAI_AC97_BUS:
	case SND_SOC_DAI_AC97:
		return "AC97";
	case SND_SOC_DAI_I2S:
		return "I2S";
	case SND_SOC_DAI_PCM:
		return "PCM";
	}
	return NULL;
}

/*
 * Called by ALSA when a PCM substream is opened, the runtime->hw record is
 * then initialized and any private data can be allocated. This also calls
 * startup for the cpu DAI, platform, machine and codec DAI.
 */
static int soc_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_cpu_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_codec_dai *codec_dai = machine->codec_dai;
	int ret = 0;

	mutex_lock(&pcm_mutex);

	/* startup the audio subsystem */
	if (cpu_dai->ops.startup) {
		ret = cpu_dai->ops.startup(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open interface %s\n",
				cpu_dai->name);
			goto out;
		}
	}

	if (platform->pcm_ops->open) {
		ret = platform->pcm_ops->open(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open platform %s\n", platform->name);
			goto platform_err;
		}
	}

	if (codec_dai->ops.startup) {
		ret = codec_dai->ops.startup(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't open codec %s\n",
				codec_dai->name);
			goto codec_dai_err;
		}
	}

	if (machine->ops && machine->ops->startup) {
		ret = machine->ops->startup(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: %s startup failed\n", machine->name);
			goto machine_err;
		}
	}

	/* Check that the codec and cpu DAI's are compatible */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw.rate_min =
			max(codec_dai->playback.rate_min, cpu_dai->playback.rate_min);
		runtime->hw.rate_max =
			min(codec_dai->playback.rate_max, cpu_dai->playback.rate_max);
		runtime->hw.channels_min =
			max(codec_dai->playback.channels_min,
				cpu_dai->playback.channels_min);
		runtime->hw.channels_max =
			min(codec_dai->playback.channels_max,
				cpu_dai->playback.channels_max);
		runtime->hw.formats =
			codec_dai->playback.formats & cpu_dai->playback.formats;
		runtime->hw.rates =
			codec_dai->playback.rates & cpu_dai->playback.rates;
	} else {
		runtime->hw.rate_min =
			max(codec_dai->capture.rate_min, cpu_dai->capture.rate_min);
		runtime->hw.rate_max =
			min(codec_dai->capture.rate_max, cpu_dai->capture.rate_max);
		runtime->hw.channels_min =
			max(codec_dai->capture.channels_min,
				cpu_dai->capture.channels_min);
		runtime->hw.channels_max =
			min(codec_dai->capture.channels_max,
				cpu_dai->capture.channels_max);
		runtime->hw.formats =
			codec_dai->capture.formats & cpu_dai->capture.formats;
		runtime->hw.rates =
			codec_dai->capture.rates & cpu_dai->capture.rates;
	}

	snd_pcm_limit_hw_rates(runtime);
	if (!runtime->hw.rates) {
		printk(KERN_ERR "asoc: %s <-> %s No matching rates\n",
			codec_dai->name, cpu_dai->name);
		goto machine_err;
	}
	if (!runtime->hw.formats) {
		printk(KERN_ERR "asoc: %s <-> %s No matching formats\n",
			codec_dai->name, cpu_dai->name);
		goto machine_err;
	}
	if (!runtime->hw.channels_min || !runtime->hw.channels_max) {
		printk(KERN_ERR "asoc: %s <-> %s No matching channels\n",
			codec_dai->name, cpu_dai->name);
		goto machine_err;
	}

	dbg("asoc: %s <-> %s info:\n",codec_dai->name, cpu_dai->name);
	dbg("asoc: rate mask 0x%x\n", runtime->hw.rates);
	dbg("asoc: min ch %d max ch %d\n", runtime->hw.channels_min,
		runtime->hw.channels_max);
	dbg("asoc: min rate %d max rate %d\n", runtime->hw.rate_min,
		runtime->hw.rate_max);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_dai->playback.active = codec_dai->playback.active = 1;
	else
		cpu_dai->capture.active = codec_dai->capture.active = 1;
	cpu_dai->active = codec_dai->active = 1;
	cpu_dai->runtime = runtime;
	socdev->codec->active++;
	mutex_unlock(&pcm_mutex);
	return 0;

machine_err:
	if (machine->ops && machine->ops->shutdown)
		machine->ops->shutdown(substream);

codec_dai_err:
	if (platform->pcm_ops->close)
		platform->pcm_ops->close(substream);

platform_err:
	if (cpu_dai->ops.shutdown)
		cpu_dai->ops.shutdown(substream);
out:
	mutex_unlock(&pcm_mutex);
	return ret;
}

/*
 * Power down the audio subsystem pmdown_time msecs after close is called.
 * This is to ensure there are no pops or clicks in between any music tracks
 * due to DAPM power cycling.
 */
static void close_delayed_work(struct work_struct *work)
{
	struct snd_soc_device *socdev =
		container_of(work, struct snd_soc_device, delayed_work.work);
	struct snd_soc_codec *codec = socdev->codec;
	struct snd_soc_codec_dai *codec_dai;
	int i;

	mutex_lock(&pcm_mutex);
	for(i = 0; i < codec->num_dai; i++) {
		codec_dai = &codec->dai[i];

		dbg("pop wq checking: %s status: %s waiting: %s\n",
			codec_dai->playback.stream_name,
			codec_dai->playback.active ? "active" : "inactive",
			codec_dai->pop_wait ? "yes" : "no");

		/* are we waiting on this codec DAI stream */
		if (codec_dai->pop_wait == 1) {

			/* power down the codec to D1 if no longer active */
			if (codec->active == 0) {
				dbg("pop wq D1 %s %s\n", codec->name,
					codec_dai->playback.stream_name);
				snd_soc_dapm_device_event(socdev,
					SNDRV_CTL_POWER_D1);
			}

			codec_dai->pop_wait = 0;
			snd_soc_dapm_stream_event(codec,
				codec_dai->playback.stream_name,
				SND_SOC_DAPM_STREAM_STOP);

			/* power down the codec power domain if no longer active */
			if (codec->active == 0) {
				dbg("pop wq D3 %s %s\n", codec->name,
					codec_dai->playback.stream_name);
				snd_soc_dapm_device_event(socdev,
					SNDRV_CTL_POWER_D3hot);
			}
		}
	}
	mutex_unlock(&pcm_mutex);
}

/*
 * Called by ALSA when a PCM substream is closed. Private data can be
 * freed here. The cpu DAI, codec DAI, machine and platform are also
 * shutdown.
 */
static int soc_codec_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_cpu_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_codec_dai *codec_dai = machine->codec_dai;
	struct snd_soc_codec *codec = socdev->codec;

	mutex_lock(&pcm_mutex);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_dai->playback.active = codec_dai->playback.active = 0;
	else
		cpu_dai->capture.active = codec_dai->capture.active = 0;

	if (codec_dai->playback.active == 0 &&
		codec_dai->capture.active == 0) {
		cpu_dai->active = codec_dai->active = 0;
	}
	codec->active--;

	if (cpu_dai->ops.shutdown)
		cpu_dai->ops.shutdown(substream);

	if (codec_dai->ops.shutdown)
		codec_dai->ops.shutdown(substream);

	if (machine->ops && machine->ops->shutdown)
		machine->ops->shutdown(substream);

	if (platform->pcm_ops->close)
		platform->pcm_ops->close(substream);
	cpu_dai->runtime = NULL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* start delayed pop wq here for playback streams */
		codec_dai->pop_wait = 1;
		schedule_delayed_work(&socdev->delayed_work,
			msecs_to_jiffies(pmdown_time));
	} else {
		/* capture streams can be powered down now */
		snd_soc_dapm_stream_event(codec,
			codec_dai->capture.stream_name,
			SND_SOC_DAPM_STREAM_STOP);

		if (codec->active == 0 && codec_dai->pop_wait == 0)
			snd_soc_dapm_device_event(socdev,
						SNDRV_CTL_POWER_D3hot);
	}

	mutex_unlock(&pcm_mutex);
	return 0;
}

/*
 * Called by ALSA when the PCM substream is prepared, can set format, sample
 * rate, etc.  This function is non atomic and can be called multiple times,
 * it can refer to the runtime info.
 */
static int soc_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_cpu_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_codec_dai *codec_dai = machine->codec_dai;
	struct snd_soc_codec *codec = socdev->codec;
	int ret = 0;

	mutex_lock(&pcm_mutex);

	if (machine->ops && machine->ops->prepare) {
		ret = machine->ops->prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: machine prepare error\n");
			goto out;
		}
	}

	if (platform->pcm_ops->prepare) {
		ret = platform->pcm_ops->prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: platform prepare error\n");
			goto out;
		}
	}

	if (codec_dai->ops.prepare) {
		ret = codec_dai->ops.prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: codec DAI prepare error\n");
			goto out;
		}
	}

	if (cpu_dai->ops.prepare) {
		ret = cpu_dai->ops.prepare(substream);
		if (ret < 0) {
			printk(KERN_ERR "asoc: cpu DAI prepare error\n");
			goto out;
		}
	}

	/* we only want to start a DAPM playback stream if we are not waiting
	 * on an existing one stopping */
	if (codec_dai->pop_wait) {
		/* we are waiting for the delayed work to start */
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
				snd_soc_dapm_stream_event(socdev->codec,
					codec_dai->capture.stream_name,
					SND_SOC_DAPM_STREAM_START);
		else {
			codec_dai->pop_wait = 0;
			cancel_delayed_work(&socdev->delayed_work);
			if (codec_dai->dai_ops.digital_mute)
				codec_dai->dai_ops.digital_mute(codec_dai, 0);
		}
	} else {
		/* no delayed work - do we need to power up codec */
		if (codec->dapm_state != SNDRV_CTL_POWER_D0) {

			snd_soc_dapm_device_event(socdev,  SNDRV_CTL_POWER_D1);

			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				snd_soc_dapm_stream_event(codec,
					codec_dai->playback.stream_name,
					SND_SOC_DAPM_STREAM_START);
			else
				snd_soc_dapm_stream_event(codec,
					codec_dai->capture.stream_name,
					SND_SOC_DAPM_STREAM_START);

			snd_soc_dapm_device_event(socdev, SNDRV_CTL_POWER_D0);
			if (codec_dai->dai_ops.digital_mute)
				codec_dai->dai_ops.digital_mute(codec_dai, 0);

		} else {
			/* codec already powered - power on widgets */
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				snd_soc_dapm_stream_event(codec,
					codec_dai->playback.stream_name,
					SND_SOC_DAPM_STREAM_START);
			else
				snd_soc_dapm_stream_event(codec,
					codec_dai->capture.stream_name,
					SND_SOC_DAPM_STREAM_START);
			if (codec_dai->dai_ops.digital_mute)
				codec_dai->dai_ops.digital_mute(codec_dai, 0);
		}
	}

out:
	mutex_unlock(&pcm_mutex);
	return ret;
}

/*
 * Called by ALSA when the hardware params are set by application. This
 * function can also be called multiple times and can allocate buffers
 * (using snd_pcm_lib_* ). It's non-atomic.
 */
static int soc_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_cpu_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_codec_dai *codec_dai = machine->codec_dai;
	int ret = 0;

	mutex_lock(&pcm_mutex);

	if (machine->ops && machine->ops->hw_params) {
		ret = machine->ops->hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: machine hw_params failed\n");
			goto out;
		}
	}

	if (codec_dai->ops.hw_params) {
		ret = codec_dai->ops.hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't set codec %s hw params\n",
				codec_dai->name);
			goto codec_err;
		}
	}

	if (cpu_dai->ops.hw_params) {
		ret = cpu_dai->ops.hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't set interface %s hw params\n",
				cpu_dai->name);
			goto interface_err;
		}
	}

	if (platform->pcm_ops->hw_params) {
		ret = platform->pcm_ops->hw_params(substream, params);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't set platform %s hw params\n",
				platform->name);
			goto platform_err;
		}
	}

out:
	mutex_unlock(&pcm_mutex);
	return ret;

platform_err:
	if (cpu_dai->ops.hw_free)
		cpu_dai->ops.hw_free(substream);

interface_err:
	if (codec_dai->ops.hw_free)
		codec_dai->ops.hw_free(substream);

codec_err:
	if(machine->ops && machine->ops->hw_free)
		machine->ops->hw_free(substream);

	mutex_unlock(&pcm_mutex);
	return ret;
}

/*
 * Free's resources allocated by hw_params, can be called multiple times
 */
static int soc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_cpu_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_codec_dai *codec_dai = machine->codec_dai;
	struct snd_soc_codec *codec = socdev->codec;

	mutex_lock(&pcm_mutex);

	/* apply codec digital mute */
	if (!codec->active && codec_dai->dai_ops.digital_mute)
		codec_dai->dai_ops.digital_mute(codec_dai, 1);

	/* free any machine hw params */
	if (machine->ops && machine->ops->hw_free)
		machine->ops->hw_free(substream);

	/* free any DMA resources */
	if (platform->pcm_ops->hw_free)
		platform->pcm_ops->hw_free(substream);

	/* now free hw params for the DAI's  */
	if (codec_dai->ops.hw_free)
		codec_dai->ops.hw_free(substream);

	if (cpu_dai->ops.hw_free)
		cpu_dai->ops.hw_free(substream);

	mutex_unlock(&pcm_mutex);
	return 0;
}

static int soc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_cpu_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_codec_dai *codec_dai = machine->codec_dai;
	int ret;

	if (codec_dai->ops.trigger) {
		ret = codec_dai->ops.trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	if (platform->pcm_ops->trigger) {
		ret = platform->pcm_ops->trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	if (cpu_dai->ops.trigger) {
		ret = cpu_dai->ops.trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/* ASoC PCM operations */
static struct snd_pcm_ops soc_pcm_ops = {
	.open		= soc_pcm_open,
	.close		= soc_codec_close,
	.hw_params	= soc_pcm_hw_params,
	.hw_free	= soc_pcm_hw_free,
	.prepare	= soc_pcm_prepare,
	.trigger	= soc_pcm_trigger,
};

#ifdef CONFIG_PM
/* powers down audio subsystem for suspend */
static int soc_suspend(struct platform_device *pdev, pm_message_t state)
{
 	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
 	struct snd_soc_machine *machine = socdev->machine;
 	struct snd_soc_platform *platform = socdev->platform;
 	struct snd_soc_codec_device *codec_dev = socdev->codec_dev;
	struct snd_soc_codec *codec = socdev->codec;
	int i;

	/* mute any active DAC's */
	for(i = 0; i < machine->num_links; i++) {
		struct snd_soc_codec_dai *dai = machine->dai_link[i].codec_dai;
		if (dai->dai_ops.digital_mute && dai->playback.active)
			dai->dai_ops.digital_mute(dai, 1);
	}

	/* suspend all pcms */
	for (i = 0; i < machine->num_links; i++)
		snd_pcm_suspend_all(machine->dai_link[i].pcm);

	if (machine->suspend_pre)
		machine->suspend_pre(pdev, state);

	for(i = 0; i < machine->num_links; i++) {
		struct snd_soc_cpu_dai  *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->suspend && cpu_dai->type != SND_SOC_DAI_AC97)
			cpu_dai->suspend(pdev, cpu_dai);
		if (platform->suspend)
			platform->suspend(pdev, cpu_dai);
	}

	/* close any waiting streams and save state */
	run_delayed_work(&socdev->delayed_work);
	codec->suspend_dapm_state = codec->dapm_state;

	for(i = 0; i < codec->num_dai; i++) {
		char *stream = codec->dai[i].playback.stream_name;
		if (stream != NULL)
			snd_soc_dapm_stream_event(codec, stream,
				SND_SOC_DAPM_STREAM_SUSPEND);
		stream = codec->dai[i].capture.stream_name;
		if (stream != NULL)
			snd_soc_dapm_stream_event(codec, stream,
				SND_SOC_DAPM_STREAM_SUSPEND);
	}

	if (codec_dev->suspend)
		codec_dev->suspend(pdev, state);

	for(i = 0; i < machine->num_links; i++) {
		struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->suspend && cpu_dai->type == SND_SOC_DAI_AC97)
			cpu_dai->suspend(pdev, cpu_dai);
	}

	if (machine->suspend_post)
		machine->suspend_post(pdev, state);

	return 0;
}

/* powers up audio subsystem after a suspend */
static int soc_resume(struct platform_device *pdev)
{
 	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
 	struct snd_soc_machine *machine = socdev->machine;
 	struct snd_soc_platform *platform = socdev->platform;
 	struct snd_soc_codec_device *codec_dev = socdev->codec_dev;
	struct snd_soc_codec *codec = socdev->codec;
	int i;

	if (machine->resume_pre)
		machine->resume_pre(pdev);

	for(i = 0; i < machine->num_links; i++) {
		struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->resume && cpu_dai->type == SND_SOC_DAI_AC97)
			cpu_dai->resume(pdev, cpu_dai);
	}

	if (codec_dev->resume)
		codec_dev->resume(pdev);

	for(i = 0; i < codec->num_dai; i++) {
		char* stream = codec->dai[i].playback.stream_name;
		if (stream != NULL)
			snd_soc_dapm_stream_event(codec, stream,
				SND_SOC_DAPM_STREAM_RESUME);
		stream = codec->dai[i].capture.stream_name;
		if (stream != NULL)
			snd_soc_dapm_stream_event(codec, stream,
				SND_SOC_DAPM_STREAM_RESUME);
	}

	/* unmute any active DAC's */
	for(i = 0; i < machine->num_links; i++) {
		struct snd_soc_codec_dai *dai = machine->dai_link[i].codec_dai;
		if (dai->dai_ops.digital_mute && dai->playback.active)
			dai->dai_ops.digital_mute(dai, 0);
	}

	for(i = 0; i < machine->num_links; i++) {
		struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->resume && cpu_dai->type != SND_SOC_DAI_AC97)
			cpu_dai->resume(pdev, cpu_dai);
		if (platform->resume)
			platform->resume(pdev, cpu_dai);
	}

	if (machine->resume_post)
		machine->resume_post(pdev);

	return 0;
}

#else
#define soc_suspend	NULL
#define soc_resume	NULL
#endif

/* probes a new socdev */
static int soc_probe(struct platform_device *pdev)
{
	int ret = 0, i;
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_machine *machine = socdev->machine;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_codec_device *codec_dev = socdev->codec_dev;

	if (machine->probe) {
		ret = machine->probe(pdev);
		if(ret < 0)
			return ret;
	}

	for (i = 0; i < machine->num_links; i++) {
		struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->probe) {
			ret = cpu_dai->probe(pdev);
			if(ret < 0)
				goto cpu_dai_err;
		}
	}

	if (codec_dev->probe) {
		ret = codec_dev->probe(pdev);
		if(ret < 0)
			goto cpu_dai_err;
	}

	if (platform->probe) {
		ret = platform->probe(pdev);
		if(ret < 0)
			goto platform_err;
	}

	/* DAPM stream work */
	INIT_DELAYED_WORK(&socdev->delayed_work, close_delayed_work);
	return 0;

platform_err:
	if (codec_dev->remove)
		codec_dev->remove(pdev);

cpu_dai_err:
	for (i--; i >= 0; i--) {
		struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->remove)
			cpu_dai->remove(pdev);
	}

	if (machine->remove)
		machine->remove(pdev);

	return ret;
}

/* removes a socdev */
static int soc_remove(struct platform_device *pdev)
{
	int i;
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_machine *machine = socdev->machine;
	struct snd_soc_platform *platform = socdev->platform;
	struct snd_soc_codec_device *codec_dev = socdev->codec_dev;

	run_delayed_work(&socdev->delayed_work);

	if (platform->remove)
		platform->remove(pdev);

	if (codec_dev->remove)
		codec_dev->remove(pdev);

	for (i = 0; i < machine->num_links; i++) {
		struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[i].cpu_dai;
		if (cpu_dai->remove)
			cpu_dai->remove(pdev);
	}

	if (machine->remove)
		machine->remove(pdev);

	return 0;
}

/* ASoC platform driver */
static struct platform_driver soc_driver = {
	.driver		= {
		.name		= "soc-audio",
	},
	.probe		= soc_probe,
	.remove		= soc_remove,
	.suspend	= soc_suspend,
	.resume		= soc_resume,
};

/* create a new pcm */
static int soc_new_pcm(struct snd_soc_device *socdev,
	struct snd_soc_dai_link *dai_link, int num)
{
	struct snd_soc_codec *codec = socdev->codec;
	struct snd_soc_codec_dai *codec_dai = dai_link->codec_dai;
	struct snd_soc_cpu_dai *cpu_dai = dai_link->cpu_dai;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_pcm *pcm;
	char new_name[64];
	int ret = 0, playback = 0, capture = 0;

	rtd = kzalloc(sizeof(struct snd_soc_pcm_runtime), GFP_KERNEL);
	if (rtd == NULL)
		return -ENOMEM;

	rtd->dai = dai_link;
	rtd->socdev = socdev;
	codec_dai->codec = socdev->codec;

	/* check client and interface hw capabilities */
	sprintf(new_name, "%s %s-%s-%d",dai_link->stream_name, codec_dai->name,
		get_dai_name(cpu_dai->type), num);

	if (codec_dai->playback.channels_min)
		playback = 1;
	if (codec_dai->capture.channels_min)
		capture = 1;

	ret = snd_pcm_new(codec->card, new_name, codec->pcm_devs++, playback,
		capture, &pcm);
	if (ret < 0) {
		printk(KERN_ERR "asoc: can't create pcm for codec %s\n", codec->name);
		kfree(rtd);
		return ret;
	}

	dai_link->pcm = pcm;
	pcm->private_data = rtd;
	soc_pcm_ops.mmap = socdev->platform->pcm_ops->mmap;
	soc_pcm_ops.pointer = socdev->platform->pcm_ops->pointer;
	soc_pcm_ops.ioctl = socdev->platform->pcm_ops->ioctl;
	soc_pcm_ops.copy = socdev->platform->pcm_ops->copy;
	soc_pcm_ops.silence = socdev->platform->pcm_ops->silence;
	soc_pcm_ops.ack = socdev->platform->pcm_ops->ack;
	soc_pcm_ops.page = socdev->platform->pcm_ops->page;

	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &soc_pcm_ops);

	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &soc_pcm_ops);

	ret = socdev->platform->pcm_new(codec->card, codec_dai, pcm);
	if (ret < 0) {
		printk(KERN_ERR "asoc: platform pcm constructor failed\n");
		kfree(rtd);
		return ret;
	}

	pcm->private_free = socdev->platform->pcm_free;
	printk(KERN_INFO "asoc: %s <-> %s mapping ok\n", codec_dai->name,
		cpu_dai->name);
	return ret;
}

/* codec register dump */
static ssize_t codec_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_device *devdata = dev_get_drvdata(dev);
	struct snd_soc_codec *codec = devdata->codec;
	int i, step = 1, count = 0;

	if (!codec->reg_cache_size)
		return 0;

	if (codec->reg_cache_step)
		step = codec->reg_cache_step;

	count += sprintf(buf, "%s registers\n", codec->name);
	for(i = 0; i < codec->reg_cache_size; i += step)
		count += sprintf(buf + count, "%2x: %4x\n", i, codec->read(codec, i));

	return count;
}
static DEVICE_ATTR(codec_reg, 0444, codec_reg_show, NULL);

/**
 * snd_soc_new_ac97_codec - initailise AC97 device
 * @codec: audio codec
 * @ops: AC97 bus operations
 * @num: AC97 codec number
 *
 * Initialises AC97 codec resources for use by ad-hoc devices only.
 */
int snd_soc_new_ac97_codec(struct snd_soc_codec *codec,
	struct snd_ac97_bus_ops *ops, int num)
{
	mutex_lock(&codec->mutex);

	codec->ac97 = kzalloc(sizeof(struct snd_ac97), GFP_KERNEL);
	if (codec->ac97 == NULL) {
		mutex_unlock(&codec->mutex);
		return -ENOMEM;
	}

	codec->ac97->bus = kzalloc(sizeof(struct snd_ac97_bus), GFP_KERNEL);
	if (codec->ac97->bus == NULL) {
		kfree(codec->ac97);
		codec->ac97 = NULL;
		mutex_unlock(&codec->mutex);
		return -ENOMEM;
	}

	codec->ac97->bus->ops = ops;
	codec->ac97->num = num;
	mutex_unlock(&codec->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_new_ac97_codec);

/**
 * snd_soc_free_ac97_codec - free AC97 codec device
 * @codec: audio codec
 *
 * Frees AC97 codec device resources.
 */
void snd_soc_free_ac97_codec(struct snd_soc_codec *codec)
{
	mutex_lock(&codec->mutex);
	kfree(codec->ac97->bus);
	kfree(codec->ac97);
	codec->ac97 = NULL;
	mutex_unlock(&codec->mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_free_ac97_codec);

/**
 * snd_soc_update_bits - update codec register bits
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change else 0.
 */
int snd_soc_update_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new;

	mutex_lock(&io_mutex);
	old = snd_soc_read(codec, reg);
	new = (old & ~mask) | value;
	change = old != new;
	if (change)
		snd_soc_write(codec, reg, new);

	mutex_unlock(&io_mutex);
	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_update_bits);

/**
 * snd_soc_test_bits - test register for change
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Tests a register with a new value and checks if the new value is
 * different from the old value.
 *
 * Returns 1 for change else 0.
 */
int snd_soc_test_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new;

	mutex_lock(&io_mutex);
	old = snd_soc_read(codec, reg);
	new = (old & ~mask) | value;
	change = old != new;
	mutex_unlock(&io_mutex);

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_test_bits);

/**
 * snd_soc_new_pcms - create new sound card and pcms
 * @socdev: the SoC audio device
 *
 * Create a new sound card based upon the codec and interface pcms.
 *
 * Returns 0 for success, else error.
 */
int snd_soc_new_pcms(struct snd_soc_device *socdev, int idx, const char *xid)
{
	struct snd_soc_codec *codec = socdev->codec;
	struct snd_soc_machine *machine = socdev->machine;
	int ret = 0, i;

	mutex_lock(&codec->mutex);

	/* register a sound card */
	codec->card = snd_card_new(idx, xid, codec->owner, 0);
	if (!codec->card) {
		printk(KERN_ERR "asoc: can't create sound card for codec %s\n",
			codec->name);
		mutex_unlock(&codec->mutex);
		return -ENODEV;
	}

	codec->card->dev = socdev->dev;
	codec->card->private_data = codec;
	strncpy(codec->card->driver, codec->name, sizeof(codec->card->driver));

	/* create the pcms */
	for(i = 0; i < machine->num_links; i++) {
		ret = soc_new_pcm(socdev, &machine->dai_link[i], i);
		if (ret < 0) {
			printk(KERN_ERR "asoc: can't create pcm %s\n",
				machine->dai_link[i].stream_name);
			mutex_unlock(&codec->mutex);
			return ret;
		}
	}

	mutex_unlock(&codec->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_new_pcms);

/**
 * snd_soc_register_card - register sound card
 * @socdev: the SoC audio device
 *
 * Register a SoC sound card. Also registers an AC97 device if the
 * codec is AC97 for ad hoc devices.
 *
 * Returns 0 for success, else error.
 */
int snd_soc_register_card(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;
	struct snd_soc_machine *machine = socdev->machine;
	int ret = 0, i, ac97 = 0, err = 0;

	for(i = 0; i < machine->num_links; i++) {
		if (socdev->machine->dai_link[i].init) {
			err = socdev->machine->dai_link[i].init(codec);
			if (err < 0) {
				printk(KERN_ERR "asoc: failed to init %s\n",
					socdev->machine->dai_link[i].stream_name);
				continue;
			}
		}
		if (socdev->machine->dai_link[i].codec_dai->type == 
			SND_SOC_DAI_AC97_BUS)
			ac97 = 1;
	}
	snprintf(codec->card->shortname, sizeof(codec->card->shortname),
		 "%s", machine->name);
	snprintf(codec->card->longname, sizeof(codec->card->longname),
		 "%s (%s)", machine->name, codec->name);

	ret = snd_card_register(codec->card);
	if (ret < 0) {
		printk(KERN_ERR "asoc: failed to register soundcard for codec %s\n",
				codec->name);
		goto out;
	}

	mutex_lock(&codec->mutex);
#ifdef CONFIG_SND_SOC_AC97_BUS
	if (ac97) {
		ret = soc_ac97_dev_register(codec);
		if (ret < 0) {
			printk(KERN_ERR "asoc: AC97 device register failed\n");
			snd_card_free(codec->card);
			mutex_unlock(&codec->mutex);
			goto out;
		}
	}
#endif

	err = snd_soc_dapm_sys_add(socdev->dev);
	if (err < 0)
		printk(KERN_WARNING "asoc: failed to add dapm sysfs entries\n");

	err = device_create_file(socdev->dev, &dev_attr_codec_reg);
	if (err < 0)
		printk(KERN_WARNING "asoc: failed to add codec sysfs entries\n");

	mutex_unlock(&codec->mutex);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_register_card);

/**
 * snd_soc_free_pcms - free sound card and pcms
 * @socdev: the SoC audio device
 *
 * Frees sound card and pcms associated with the socdev.
 * Also unregister the codec if it is an AC97 device.
 */
void snd_soc_free_pcms(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;
#ifdef CONFIG_SND_SOC_AC97_BUS
	struct snd_soc_codec_dai *codec_dai;
	int i;
#endif

	mutex_lock(&codec->mutex);
#ifdef CONFIG_SND_SOC_AC97_BUS
	for(i = 0; i < codec->num_dai; i++) {
		codec_dai = &codec->dai[i];
		if (codec_dai->type == SND_SOC_DAI_AC97_BUS && codec->ac97) {
			soc_ac97_dev_unregister(codec);
			goto free_card;
		}
	}
free_card:
#endif

	if (codec->card)
		snd_card_free(codec->card);
	device_remove_file(socdev->dev, &dev_attr_codec_reg);
	mutex_unlock(&codec->mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_free_pcms);

/**
 * snd_soc_set_runtime_hwparams - set the runtime hardware parameters
 * @substream: the pcm substream
 * @hw: the hardware parameters
 *
 * Sets the substream runtime hardware parameters.
 */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->hw.info = hw->info;
	runtime->hw.formats = hw->formats;
	runtime->hw.period_bytes_min = hw->period_bytes_min;
	runtime->hw.period_bytes_max = hw->period_bytes_max;
	runtime->hw.periods_min = hw->periods_min;
	runtime->hw.periods_max = hw->periods_max;
	runtime->hw.buffer_bytes_max = hw->buffer_bytes_max;
	runtime->hw.fifo_size = hw->fifo_size;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_runtime_hwparams);

/**
 * snd_soc_cnew - create new control
 * @_template: control template
 * @data: control private data
 * @lnng_name: control long name
 *
 * Create a new mixer control from a template control.
 *
 * Returns 0 for success, else error.
 */
struct snd_kcontrol *snd_soc_cnew(const struct snd_kcontrol_new *_template,
	void *data, char *long_name)
{
	struct snd_kcontrol_new template;

	memcpy(&template, _template, sizeof(template));
	if (long_name)
		template.name = long_name;
	template.index = 0;

	return snd_ctl_new1(&template, data);
}
EXPORT_SYMBOL_GPL(snd_soc_cnew);

/**
 * snd_soc_info_enum_double - enumerated double mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a double enumerated
 * mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = e->shift_l == e->shift_r ? 1 : 2;
	uinfo->value.enumerated.items = e->mask;

	if (uinfo->value.enumerated.item > e->mask - 1)
		uinfo->value.enumerated.item = e->mask - 1;
	strcpy(uinfo->value.enumerated.name,
		e->texts[uinfo->value.enumerated.item]);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_enum_double);

/**
 * snd_soc_get_enum_double - enumerated double mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to get the value of a double enumerated mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short val, bitmask;

	for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
	val = snd_soc_read(codec, e->reg);
	ucontrol->value.enumerated.item[0] = (val >> e->shift_l) & (bitmask - 1);
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			(val >> e->shift_r) & (bitmask - 1);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_enum_double);

/**
 * snd_soc_put_enum_double - enumerated double mixer put callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to set the value of a double enumerated mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short val;
	unsigned short mask, bitmask;

	for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->mask - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->mask - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}

	return snd_soc_update_bits(codec, e->reg, mask, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_enum_double);

/**
 * snd_soc_info_enum_ext - external enumerated single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about an external enumerated
 * single mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_info_enum_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = e->mask;

	if (uinfo->value.enumerated.item > e->mask - 1)
		uinfo->value.enumerated.item = e->mask - 1;
	strcpy(uinfo->value.enumerated.name,
		e->texts[uinfo->value.enumerated.item]);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_enum_ext);

/**
 * snd_soc_info_volsw_ext - external single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a single external mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	int max = kcontrol->private_value;

	if (max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_ext);

/**
 * snd_soc_info_volsw - single mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	int max = (kcontrol->private_value >> 16) & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;

	if (max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = shift == rshift ? 1 : 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw);

/**
 * snd_soc_get_volsw - single mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to get the value of a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int max = (kcontrol->private_value >> 16) & 0xff;
	int mask = (1 << fls(max)) - 1;
	int invert = (kcontrol->private_value >> 24) & 0x01;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	if (shift != rshift)
		ucontrol->value.integer.value[1] =
			(snd_soc_read(codec, reg) >> rshift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		if (shift != rshift)
			ucontrol->value.integer.value[1] =
				max - ucontrol->value.integer.value[1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw);

/**
 * snd_soc_put_volsw - single mixer put callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to set the value of a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int max = (kcontrol->private_value >> 16) & 0xff;
	int mask = (1 << fls(max)) - 1;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	unsigned short val, val2, val_mask;

	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = max - val;
	val_mask = mask << shift;
	val = val << shift;
	if (shift != rshift) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		if (invert)
			val2 = max - val2;
		val_mask |= mask << rshift;
		val |= val2 << rshift;
	}
	return snd_soc_update_bits(codec, reg, val_mask, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw);

/**
 * snd_soc_info_volsw_2r - double mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a double mixer control that
 * spans 2 codec registers.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	int max = (kcontrol->private_value >> 12) & 0xff;

	if (max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_2r);

/**
 * snd_soc_get_volsw_2r - double mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to get the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 24) & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int max = (kcontrol->private_value >> 12) & 0xff;
	int mask = (1<<fls(max))-1;
	int invert = (kcontrol->private_value >> 20) & 0x01;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	ucontrol->value.integer.value[1] =
		(snd_soc_read(codec, reg2) >> shift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] =
			max - ucontrol->value.integer.value[1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_2r);

/**
 * snd_soc_put_volsw_2r - double mixer set callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to set the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 24) & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int max = (kcontrol->private_value >> 12) & 0xff;
	int mask = (1 << fls(max)) - 1;
	int invert = (kcontrol->private_value >> 20) & 0x01;
	int err;
	unsigned short val, val2, val_mask;

	val_mask = mask << shift;
	val = (ucontrol->value.integer.value[0] & mask);
	val2 = (ucontrol->value.integer.value[1] & mask);

	if (invert) {
		val = max - val;
		val2 = max - val2;
	}

	val = val << shift;
	val2 = val2 << shift;

	if ((err = snd_soc_update_bits(codec, reg, val_mask, val)) < 0)
		return err;

	err = snd_soc_update_bits(codec, reg2, val_mask, val2);
	return err;
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_2r);

static int __devinit snd_soc_init(void)
{
	printk(KERN_INFO "ASoC version %s\n", SND_SOC_VERSION);
	return platform_driver_register(&soc_driver);
}

static void snd_soc_exit(void)
{
 	platform_driver_unregister(&soc_driver);
}

module_init(snd_soc_init);
module_exit(snd_soc_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, liam.girdwood@wolfsonmicro.com, www.wolfsonmicro.com");
MODULE_DESCRIPTION("ALSA SoC Core");
MODULE_LICENSE("GPL");
