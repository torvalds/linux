// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/device.h>
#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/soc-component.h>
#include "avs.h"
#include "path.h"
#include "topology.h"

struct avs_dma_data {
	struct avs_tplg_path_template *template;
	struct avs_path *path;
	/*
	 * link stream is stored within substream's runtime
	 * private_data to fulfill the needs of codec BE path
	 *
	 * host stream assigned
	 */
	struct hdac_ext_stream *host_stream;
};

static ssize_t topology_name_read(struct file *file, char __user *user_buf, size_t count,
				  loff_t *ppos)
{
	struct snd_soc_component *component = file->private_data;
	struct snd_soc_card *card = component->card;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(card->dev);
	char buf[64];
	size_t len;

	len = snprintf(buf, sizeof(buf), "%s/%s\n", component->driver->topology_name_prefix,
		       mach->tplg_filename);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations topology_name_fops = {
	.open = simple_open,
	.read = topology_name_read,
	.llseek = default_llseek,
};

static int avs_component_load_libraries(struct avs_soc_component *acomp)
{
	struct avs_tplg *tplg = acomp->tplg;
	struct avs_dev *adev = to_avs_dev(acomp->base.dev);
	int ret;

	if (!tplg->num_libs)
		return 0;

	/* Parent device may be asleep and library loading involves IPCs. */
	ret = pm_runtime_resume_and_get(adev->dev);
	if (ret < 0)
		return ret;

	avs_hda_clock_gating_enable(adev, false);
	avs_hda_l1sen_enable(adev, false);

	ret = avs_dsp_load_libraries(adev, tplg->libs, tplg->num_libs);

	avs_hda_l1sen_enable(adev, true);
	avs_hda_clock_gating_enable(adev, true);

	if (!ret)
		ret = avs_module_info_init(adev, false);

	pm_runtime_mark_last_busy(adev->dev);
	pm_runtime_put_autosuspend(adev->dev);

	return ret;
}

static int avs_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;
	struct snd_soc_acpi_mach *mach;
	struct avs_soc_component *acomp;
	struct avs_dev *adev;
	char *filename;
	int ret;

	dev_dbg(card->dev, "probing %s card %s\n", component->name, card->name);
	mach = dev_get_platdata(card->dev);
	acomp = to_avs_soc_component(component);
	adev = to_avs_dev(component->dev);

	acomp->tplg = avs_tplg_new(component);
	if (!acomp->tplg)
		return -ENOMEM;

	if (!mach->tplg_filename)
		goto finalize;

	/* Load specified topology and create debugfs for it. */
	filename = kasprintf(GFP_KERNEL, "%s/%s", component->driver->topology_name_prefix,
			     mach->tplg_filename);
	if (!filename)
		return -ENOMEM;

	ret = avs_load_topology(component, filename);
	kfree(filename);
	if (ret < 0)
		return ret;

	ret = avs_component_load_libraries(acomp);
	if (ret < 0) {
		dev_err(card->dev, "libraries loading failed: %d\n", ret);
		goto err_load_libs;
	}

finalize:
	debugfs_create_file("topology_name", 0444, component->debugfs_root, component,
			    &topology_name_fops);

	mutex_lock(&adev->comp_list_mutex);
	list_add_tail(&acomp->node, &adev->comp_list);
	mutex_unlock(&adev->comp_list_mutex);

	return 0;

err_load_libs:
	avs_remove_topology(component);
	return ret;
}

static void avs_component_remove(struct snd_soc_component *component)
{
	struct avs_soc_component *acomp = to_avs_soc_component(component);
	struct snd_soc_acpi_mach *mach;
	struct avs_dev *adev = to_avs_dev(component->dev);
	int ret;

	mach = dev_get_platdata(component->card->dev);

	mutex_lock(&adev->comp_list_mutex);
	list_del(&acomp->node);
	mutex_unlock(&adev->comp_list_mutex);

	if (mach->tplg_filename) {
		ret = avs_remove_topology(component);
		if (ret < 0)
			dev_err(component->dev, "unload topology failed: %d\n", ret);
	}
}

static int avs_component_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct snd_pcm_hardware hwparams;

	/* only FE DAI links are handled here */
	if (rtd->dai_link->no_pcm)
		return 0;

	hwparams.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP;

	hwparams.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE;
	hwparams.period_bytes_min = 128;
	hwparams.period_bytes_max = AZX_MAX_BUF_SIZE / 2;
	hwparams.periods_min = 2;
	hwparams.periods_max = AZX_MAX_FRAG;
	hwparams.buffer_bytes_max = AZX_MAX_BUF_SIZE;
	hwparams.fifo_size = 0;

	return snd_soc_set_runtime_hwparams(substream, &hwparams);
}

static unsigned int avs_hda_stream_dpib_read(struct hdac_ext_stream *stream)
{
	return readl(hdac_stream(stream)->bus->remap_addr + AZX_REG_VS_SDXDPIB_XBASE +
		     (AZX_REG_VS_SDXDPIB_XINTERVAL * hdac_stream(stream)->index));
}

static snd_pcm_uframes_t
avs_component_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	unsigned int pos;

	data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);
	if (!data->host_stream)
		return 0;

	host_stream = data->host_stream;
	pos = avs_hda_stream_dpib_read(host_stream);

	if (pos >= hdac_stream(host_stream)->bufsize)
		pos = 0;

	return bytes_to_frames(substream->runtime, pos);
}

static int avs_component_mmap(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream,
			      struct vm_area_struct *vma)
{
	return snd_pcm_lib_default_mmap(substream, vma);
}

#define MAX_PREALLOC_SIZE	(32 * 1024 * 1024)

static int avs_component_construct(struct snd_soc_component *component,
				   struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_pcm *pcm = rtd->pcm;

	if (dai->driver->playback.channels_min)
		snd_pcm_set_managed_buffer(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
					   SNDRV_DMA_TYPE_DEV_SG, component->dev, 0,
					   MAX_PREALLOC_SIZE);

	if (dai->driver->capture.channels_min)
		snd_pcm_set_managed_buffer(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
					   SNDRV_DMA_TYPE_DEV_SG, component->dev, 0,
					   MAX_PREALLOC_SIZE);

	return 0;
}

static const struct snd_soc_component_driver avs_component_driver = {
	.name			= "avs-pcm",
	.probe			= avs_component_probe,
	.remove			= avs_component_remove,
	.open			= avs_component_open,
	.pointer		= avs_component_pointer,
	.mmap			= avs_component_mmap,
	.pcm_construct		= avs_component_construct,
	.module_get_upon_open	= 1, /* increment refcount when a pcm is opened */
	.topology_name_prefix	= "intel/avs",
	.non_legacy_dai_naming	= true,
};

__maybe_unused
static int avs_soc_component_register(struct device *dev, const char *name,
				      const struct snd_soc_component_driver *drv,
				      struct snd_soc_dai_driver *cpu_dais, int num_cpu_dais)
{
	struct avs_soc_component *acomp;
	int ret;

	acomp = devm_kzalloc(dev, sizeof(*acomp), GFP_KERNEL);
	if (!acomp)
		return -ENOMEM;

	ret = snd_soc_component_initialize(&acomp->base, drv, dev);
	if (ret < 0)
		return ret;

	/* force name change after ASoC is done with its init */
	acomp->base.name = name;
	INIT_LIST_HEAD(&acomp->node);

	return snd_soc_add_component(&acomp->base, cpu_dais, num_cpu_dais);
}
