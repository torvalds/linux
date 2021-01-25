// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw - ASOC Machine driver for Intel SoundWire platforms
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "sof_sdw_common.h"

unsigned long sof_sdw_quirk = SOF_RT711_JD_SRC_JD1;
static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

#define INC_ID(BE, CPU, LINK)	do { (BE)++; (CPU)++; (LINK)++; } while (0)

static void log_quirks(struct device *dev)
{
	if (SOF_RT711_JDSRC(sof_sdw_quirk))
		dev_dbg(dev, "quirk realtek,jack-detect-source %ld\n",
			SOF_RT711_JDSRC(sof_sdw_quirk));
	if (sof_sdw_quirk & SOF_SDW_FOUR_SPK)
		dev_dbg(dev, "quirk SOF_SDW_FOUR_SPK enabled\n");
	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		dev_dbg(dev, "quirk SOF_SDW_TGL_HDMI enabled\n");
	if (sof_sdw_quirk & SOF_SDW_PCH_DMIC)
		dev_dbg(dev, "quirk SOF_SDW_PCH_DMIC enabled\n");
	if (SOF_SSP_GET_PORT(sof_sdw_quirk))
		dev_dbg(dev, "SSP port %ld\n",
			SOF_SSP_GET_PORT(sof_sdw_quirk));
	if (sof_sdw_quirk & SOF_RT715_DAI_ID_FIX)
		dev_dbg(dev, "quirk SOF_RT715_DAI_ID_FIX enabled\n");
	if (sof_sdw_quirk & SOF_SDW_NO_AGGREGATION)
		dev_dbg(dev, "quirk SOF_SDW_NO_AGGREGATION enabled\n");
}

static int sof_sdw_quirk_cb(const struct dmi_system_id *id)
{
	sof_sdw_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_sdw_quirk_table[] = {
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A3E")
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD2 |
					SOF_RT715_DAI_ID_FIX),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A5E")
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD2 |
					SOF_RT715_DAI_ID_FIX |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "09C6")
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD2 |
					SOF_RT715_DAI_ID_FIX),
	},
	{
		/* early version of SKU 09C6 */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0983")
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD2 |
					SOF_RT715_DAI_ID_FIX),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "098F"),
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD2 |
					SOF_RT715_DAI_ID_FIX |
					SOF_SDW_FOUR_SPK),
	},
		{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0990"),
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD2 |
					SOF_RT715_DAI_ID_FIX |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME,
				  "Tiger Lake Client Platform"),
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD1 |
				SOF_SDW_TGL_HDMI | SOF_SDW_PCH_DMIC |
				SOF_SSP_PORT(SOF_I2S_SSP2)),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Ice Lake Client"),
		},
		.driver_data = (void *)SOF_SDW_PCH_DMIC,
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CometLake Client"),
		},
		.driver_data = (void *)SOF_SDW_PCH_DMIC,
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Volteer"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI | SOF_SDW_PCH_DMIC |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Ripto"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI | SOF_SDW_PCH_DMIC |
					SOF_SDW_FOUR_SPK),
	},

	{}
};

static struct snd_soc_dai_link_component dmic_component[] = {
	{
		.name = "dmic-codec",
		.dai_name = "dmic-hifi",
	}
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

/* these wrappers are only needed to avoid typecast compilation errors */
int sdw_startup(struct snd_pcm_substream *substream)
{
	return sdw_startup_stream(substream);
}

int sdw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;

	/* Find stream from first CPU DAI */
	dai = asoc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_sdw_stream(dai, substream->stream);

	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s", dai->name);
		return PTR_ERR(sdw_stream);
	}

	return sdw_prepare_stream(sdw_stream);
}

int sdw_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;
	int ret;

	/* Find stream from first CPU DAI */
	dai = asoc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_sdw_stream(dai, substream->stream);

	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s", dai->name);
		return PTR_ERR(sdw_stream);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = sdw_enable_stream(sdw_stream);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = sdw_disable_stream(sdw_stream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		dev_err(rtd->dev, "%s trigger %d failed: %d", __func__, cmd, ret);

	return ret;
}

int sdw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;

	/* Find stream from first CPU DAI */
	dai = asoc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_sdw_stream(dai, substream->stream);

	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s", dai->name);
		return PTR_ERR(sdw_stream);
	}

	return sdw_deprepare_stream(sdw_stream);
}

void sdw_shutdown(struct snd_pcm_substream *substream)
{
	sdw_shutdown_stream(substream);
}

static const struct snd_soc_ops sdw_ops = {
	.startup = sdw_startup,
	.prepare = sdw_prepare,
	.trigger = sdw_trigger,
	.hw_free = sdw_hw_free,
	.shutdown = sdw_shutdown,
};

static struct sof_sdw_codec_info codec_info_list[] = {
	{
		.part_id = 0x700,
		.direction = {true, true},
		.dai_name = "rt700-aif1",
		.init = sof_sdw_rt700_init,
	},
	{
		.part_id = 0x711,
		.version_id = 3,
		.direction = {true, true},
		.dai_name = "rt711-sdca-aif1",
		.init = sof_sdw_rt711_sdca_init,
		.exit = sof_sdw_rt711_sdca_exit,
	},
	{
		.part_id = 0x711,
		.version_id = 2,
		.direction = {true, true},
		.dai_name = "rt711-aif1",
		.init = sof_sdw_rt711_init,
		.exit = sof_sdw_rt711_exit,
	},
	{
		.part_id = 0x1308,
		.acpi_id = "10EC1308",
		.direction = {true, false},
		.dai_name = "rt1308-aif",
		.ops = &sof_sdw_rt1308_i2s_ops,
		.init = sof_sdw_rt1308_init,
	},
	{
		.part_id = 0x1316,
		.direction = {true, true},
		.dai_name = "rt1316-aif",
		.init = sof_sdw_rt1316_init,
	},
	{
		.part_id = 0x714,
		.version_id = 3,
		.direction = {false, true},
		.dai_name = "rt715-aif2",
		.init = sof_sdw_rt715_sdca_init,
	},
	{
		.part_id = 0x715,
		.version_id = 3,
		.direction = {false, true},
		.dai_name = "rt715-aif2",
		.init = sof_sdw_rt715_sdca_init,
	},
	{
		.part_id = 0x714,
		.version_id = 2,
		.direction = {false, true},
		.dai_name = "rt715-aif2",
		.init = sof_sdw_rt715_init,
	},
	{
		.part_id = 0x715,
		.version_id = 2,
		.direction = {false, true},
		.dai_name = "rt715-aif2",
		.init = sof_sdw_rt715_init,
	},
	{
		.part_id = 0x8373,
		.direction = {true, true},
		.dai_name = "max98373-aif1",
		.init = sof_sdw_mx8373_init,
		.codec_card_late_probe = sof_sdw_mx8373_late_probe,
	},
	{
		.part_id = 0x5682,
		.direction = {true, true},
		.dai_name = "rt5682-sdw",
		.init = sof_sdw_rt5682_init,
	},
};

static inline int find_codec_info_part(u64 adr)
{
	unsigned int part_id, sdw_version;
	int i;

	part_id = SDW_PART_ID(adr);
	sdw_version = SDW_VERSION(adr);
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		/*
		 * A codec info is for all sdw version with the part id if
		 * version_id is not specified in the codec info.
		 */
		if (part_id == codec_info_list[i].part_id &&
		    (!codec_info_list[i].version_id ||
		     sdw_version == codec_info_list[i].version_id))
			return i;

	return -EINVAL;

}

static inline int find_codec_info_acpi(const u8 *acpi_id)
{
	int i;

	if (!acpi_id[0])
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		if (!memcmp(codec_info_list[i].acpi_id, acpi_id,
			    ACPI_ID_LEN))
			break;

	if (i == ARRAY_SIZE(codec_info_list))
		return -EINVAL;

	return i;
}

/*
 * get BE dailink number and CPU DAI number based on sdw link adr.
 * Since some sdw slaves may be aggregated, the CPU DAI number
 * may be larger than the number of BE dailinks.
 */
static int get_sdw_dailink_info(const struct snd_soc_acpi_link_adr *links,
				int *sdw_be_num, int *sdw_cpu_dai_num)
{
	const struct snd_soc_acpi_link_adr *link;
	bool group_visited[SDW_MAX_GROUPS];
	bool no_aggregation;
	int i;

	no_aggregation = sof_sdw_quirk & SOF_SDW_NO_AGGREGATION;
	*sdw_cpu_dai_num = 0;
	*sdw_be_num  = 0;

	if (!links)
		return -EINVAL;

	for (i = 0; i < SDW_MAX_GROUPS; i++)
		group_visited[i] = false;

	for (link = links; link->num_adr; link++) {
		const struct snd_soc_acpi_endpoint *endpoint;
		int codec_index;
		int stream;
		u64 adr;

		adr = link->adr_d->adr;
		codec_index = find_codec_info_part(adr);
		if (codec_index < 0)
			return codec_index;

		endpoint = link->adr_d->endpoints;

		/* count DAI number for playback and capture */
		for_each_pcm_streams(stream) {
			if (!codec_info_list[codec_index].direction[stream])
				continue;

			(*sdw_cpu_dai_num)++;

			/* count BE for each non-aggregated slave or group */
			if (!endpoint->aggregated || no_aggregation ||
			    !group_visited[endpoint->group_id])
				(*sdw_be_num)++;
		}

		if (endpoint->aggregated)
			group_visited[endpoint->group_id] = true;
	}

	return 0;
}

static void init_dai_link(struct snd_soc_dai_link *dai_links, int be_id,
			  char *name, int playback, int capture,
			  struct snd_soc_dai_link_component *cpus,
			  int cpus_num,
			  struct snd_soc_dai_link_component *codecs,
			  int codecs_num,
			  int (*init)(struct snd_soc_pcm_runtime *rtd),
			  const struct snd_soc_ops *ops)
{
	dai_links->id = be_id;
	dai_links->name = name;
	dai_links->platforms = platform_component;
	dai_links->num_platforms = ARRAY_SIZE(platform_component);
	dai_links->nonatomic = true;
	dai_links->no_pcm = 1;
	dai_links->cpus = cpus;
	dai_links->num_cpus = cpus_num;
	dai_links->codecs = codecs;
	dai_links->num_codecs = codecs_num;
	dai_links->dpcm_playback = playback;
	dai_links->dpcm_capture = capture;
	dai_links->init = init;
	dai_links->ops = ops;
}

static bool is_unique_device(const struct snd_soc_acpi_link_adr *link,
			     unsigned int sdw_version,
			     unsigned int mfg_id,
			     unsigned int part_id,
			     unsigned int class_id,
			     int index_in_link
			    )
{
	int i;

	for (i = 0; i < link->num_adr; i++) {
		unsigned int sdw1_version, mfg1_id, part1_id, class1_id;
		u64 adr;

		/* skip itself */
		if (i == index_in_link)
			continue;

		adr = link->adr_d[i].adr;

		sdw1_version = SDW_VERSION(adr);
		mfg1_id = SDW_MFG_ID(adr);
		part1_id = SDW_PART_ID(adr);
		class1_id = SDW_CLASS_ID(adr);

		if (sdw_version == sdw1_version &&
		    mfg_id == mfg1_id &&
		    part_id == part1_id &&
		    class_id == class1_id)
			return false;
	}

	return true;
}

static int create_codec_dai_name(struct device *dev,
				 const struct snd_soc_acpi_link_adr *link,
				 struct snd_soc_dai_link_component *codec,
				 int offset,
				 struct snd_soc_codec_conf *codec_conf,
				 int codec_count,
				 int *codec_conf_index)
{
	int i;

	/* sanity check */
	if (*codec_conf_index + link->num_adr > codec_count) {
		dev_err(dev, "codec_conf: out-of-bounds access requested\n");
		return -EINVAL;
	}

	for (i = 0; i < link->num_adr; i++) {
		unsigned int sdw_version, unique_id, mfg_id;
		unsigned int link_id, part_id, class_id;
		int codec_index, comp_index;
		char *codec_str;
		u64 adr;

		adr = link->adr_d[i].adr;

		sdw_version = SDW_VERSION(adr);
		link_id = SDW_DISCO_LINK_ID(adr);
		unique_id = SDW_UNIQUE_ID(adr);
		mfg_id = SDW_MFG_ID(adr);
		part_id = SDW_PART_ID(adr);
		class_id = SDW_CLASS_ID(adr);

		comp_index = i + offset;
		if (is_unique_device(link, sdw_version, mfg_id, part_id,
				     class_id, i)) {
			codec_str = "sdw:%x:%x:%x:%x";
			codec[comp_index].name =
				devm_kasprintf(dev, GFP_KERNEL, codec_str,
					       link_id, mfg_id, part_id,
					       class_id);
		} else {
			codec_str = "sdw:%x:%x:%x:%x:%x";
			codec[comp_index].name =
				devm_kasprintf(dev, GFP_KERNEL, codec_str,
					       link_id, mfg_id, part_id,
					       class_id, unique_id);
		}

		if (!codec[comp_index].name)
			return -ENOMEM;

		codec_index = find_codec_info_part(adr);
		if (codec_index < 0)
			return codec_index;

		codec[comp_index].dai_name =
			codec_info_list[codec_index].dai_name;

		codec_conf[*codec_conf_index].dlc = codec[comp_index];
		codec_conf[*codec_conf_index].name_prefix = link->adr_d[i].name_prefix;

		++*codec_conf_index;
	}

	return 0;
}

static int set_codec_init_func(const struct snd_soc_acpi_link_adr *link,
			       struct snd_soc_dai_link *dai_links,
			       bool playback, int group_id)
{
	int i;

	do {
		/*
		 * Initialize the codec. If codec is part of an aggregated
		 * group (group_id>0), initialize all codecs belonging to
		 * same group.
		 */
		for (i = 0; i < link->num_adr; i++) {
			int codec_index;

			codec_index = find_codec_info_part(link->adr_d[i].adr);

			if (codec_index < 0)
				return codec_index;
			/* The group_id is > 0 iff the codec is aggregated */
			if (link->adr_d[i].endpoints->group_id != group_id)
				continue;
			if (codec_info_list[codec_index].init)
				codec_info_list[codec_index].init(link,
						dai_links,
						&codec_info_list[codec_index],
						playback);
		}
		link++;
	} while (link->mask && group_id);

	return 0;
}

/*
 * check endpoint status in slaves and gather link ID for all slaves in
 * the same group to generate different CPU DAI. Now only support
 * one sdw link with all slaves set with only single group id.
 *
 * one slave on one sdw link with aggregated = 0
 * one sdw BE DAI <---> one-cpu DAI <---> one-codec DAI
 *
 * two or more slaves on one sdw link with aggregated = 0
 * one sdw BE DAI  <---> one-cpu DAI <---> multi-codec DAIs
 *
 * multiple links with multiple slaves with aggregated = 1
 * one sdw BE DAI  <---> 1 .. N CPU DAIs <----> 1 .. N codec DAIs
 */
static int get_slave_info(const struct snd_soc_acpi_link_adr *adr_link,
			  struct device *dev, int *cpu_dai_id, int *cpu_dai_num,
			  int *codec_num, int *group_id,
			  bool *group_generated)
{
	const struct snd_soc_acpi_adr_device *adr_d;
	const struct snd_soc_acpi_link_adr *adr_next;
	bool no_aggregation;
	int index = 0;

	no_aggregation = sof_sdw_quirk & SOF_SDW_NO_AGGREGATION;
	*codec_num = adr_link->num_adr;
	adr_d = adr_link->adr_d;

	/* make sure the link mask has a single bit set */
	if (!is_power_of_2(adr_link->mask))
		return -EINVAL;

	cpu_dai_id[index++] = ffs(adr_link->mask) - 1;
	if (!adr_d->endpoints->aggregated || no_aggregation) {
		*cpu_dai_num = 1;
		*group_id = 0;
		return 0;
	}

	*group_id = adr_d->endpoints->group_id;

	/* gather other link ID of slaves in the same group */
	for (adr_next = adr_link + 1; adr_next && adr_next->num_adr;
		adr_next++) {
		const struct snd_soc_acpi_endpoint *endpoint;

		endpoint = adr_next->adr_d->endpoints;
		if (!endpoint->aggregated ||
		    endpoint->group_id != *group_id)
			continue;

		/* make sure the link mask has a single bit set */
		if (!is_power_of_2(adr_next->mask))
			return -EINVAL;

		if (index >= SDW_MAX_CPU_DAIS) {
			dev_err(dev, " cpu_dai_id array overflows");
			return -EINVAL;
		}

		cpu_dai_id[index++] = ffs(adr_next->mask) - 1;
		*codec_num += adr_next->num_adr;
	}

	/*
	 * indicate CPU DAIs for this group have been generated
	 * to avoid generating CPU DAIs for this group again.
	 */
	group_generated[*group_id] = true;
	*cpu_dai_num = index;

	return 0;
}

static int create_sdw_dailink(struct device *dev, int *be_index,
			      struct snd_soc_dai_link *dai_links,
			      int sdw_be_num, int sdw_cpu_dai_num,
			      struct snd_soc_dai_link_component *cpus,
			      const struct snd_soc_acpi_link_adr *link,
			      int *cpu_id, bool *group_generated,
			      struct snd_soc_codec_conf *codec_conf,
			      int codec_count,
			      int *codec_conf_index)
{
	const struct snd_soc_acpi_link_adr *link_next;
	struct snd_soc_dai_link_component *codecs;
	int cpu_dai_id[SDW_MAX_CPU_DAIS];
	int cpu_dai_num, cpu_dai_index;
	unsigned int group_id;
	int codec_idx = 0;
	int i = 0, j = 0;
	int codec_index;
	int codec_num;
	int stream;
	int ret;
	int k;

	ret = get_slave_info(link, dev, cpu_dai_id, &cpu_dai_num, &codec_num,
			     &group_id, group_generated);
	if (ret)
		return ret;

	codecs = devm_kcalloc(dev, codec_num, sizeof(*codecs), GFP_KERNEL);
	if (!codecs)
		return -ENOMEM;

	/* generate codec name on different links in the same group */
	for (link_next = link; link_next && link_next->num_adr &&
	     i < cpu_dai_num; link_next++) {
		const struct snd_soc_acpi_endpoint *endpoints;

		endpoints = link_next->adr_d->endpoints;
		if (group_id && (!endpoints->aggregated ||
				 endpoints->group_id != group_id))
			continue;

		/* skip the link excluded by this processed group */
		if (cpu_dai_id[i] != ffs(link_next->mask) - 1)
			continue;

		ret = create_codec_dai_name(dev, link_next, codecs, codec_idx,
					    codec_conf, codec_count, codec_conf_index);
		if (ret < 0)
			return ret;

		/* check next link to create codec dai in the processed group */
		i++;
		codec_idx += link_next->num_adr;
	}

	/* find codec info to create BE DAI */
	codec_index = find_codec_info_part(link->adr_d[0].adr);
	if (codec_index < 0)
		return codec_index;

	cpu_dai_index = *cpu_id;
	for_each_pcm_streams(stream) {
		char *name, *cpu_name;
		int playback, capture;
		static const char * const sdw_stream_name[] = {
			"SDW%d-Playback",
			"SDW%d-Capture",
		};

		if (!codec_info_list[codec_index].direction[stream])
			continue;

		/* create stream name according to first link id */
		name = devm_kasprintf(dev, GFP_KERNEL,
				      sdw_stream_name[stream], cpu_dai_id[0]);
		if (!name)
			return -ENOMEM;

		/*
		 * generate CPU DAI name base on the sdw link ID and
		 * PIN ID with offset of 2 according to sdw dai driver.
		 */
		for (k = 0; k < cpu_dai_num; k++) {
			cpu_name = devm_kasprintf(dev, GFP_KERNEL,
						  "SDW%d Pin%d", cpu_dai_id[k],
						  j + SDW_INTEL_BIDIR_PDI_BASE);
			if (!cpu_name)
				return -ENOMEM;

			if (cpu_dai_index >= sdw_cpu_dai_num) {
				dev_err(dev, "invalid cpu dai index %d",
					cpu_dai_index);
				return -EINVAL;
			}

			cpus[cpu_dai_index++].dai_name = cpu_name;
		}

		if (*be_index >= sdw_be_num) {
			dev_err(dev, " invalid be dai index %d", *be_index);
			return -EINVAL;
		}

		if (*cpu_id >= sdw_cpu_dai_num) {
			dev_err(dev, " invalid cpu dai index %d", *cpu_id);
			return -EINVAL;
		}

		playback = (stream == SNDRV_PCM_STREAM_PLAYBACK);
		capture = (stream == SNDRV_PCM_STREAM_CAPTURE);
		init_dai_link(dai_links + *be_index, *be_index, name,
			      playback, capture,
			      cpus + *cpu_id, cpu_dai_num,
			      codecs, codec_num,
			      NULL, &sdw_ops);

		ret = set_codec_init_func(link, dai_links + (*be_index)++,
					  playback, group_id);
		if (ret < 0) {
			dev_err(dev, "failed to init codec %d", codec_index);
			return ret;
		}

		*cpu_id += cpu_dai_num;
		j++;
	}

	return 0;
}

/*
 * DAI link ID of SSP & DMIC & HDMI are based on last
 * link ID used by sdw link. Since be_id may be changed
 * in init func of sdw codec, it is not equal to be_id
 */
static inline int get_next_be_id(struct snd_soc_dai_link *links,
				 int be_id)
{
	return links[be_id - 1].id + 1;
}

#define IDISP_CODEC_MASK	0x4

static int sof_card_codec_conf_alloc(struct device *dev,
				     struct snd_soc_acpi_mach_params *mach_params,
				     struct snd_soc_codec_conf **codec_conf,
				     int *codec_conf_count)
{
	const struct snd_soc_acpi_link_adr *adr_link;
	struct snd_soc_codec_conf *c_conf;
	int num_codecs = 0;
	int i;

	adr_link = mach_params->links;
	if (!adr_link)
		return -EINVAL;

	/* generate DAI links by each sdw link */
	for (; adr_link->num_adr; adr_link++) {
		for (i = 0; i < adr_link->num_adr; i++) {
			if (!adr_link->adr_d[i].name_prefix) {
				dev_err(dev, "codec 0x%llx does not have a name prefix\n",
					adr_link->adr_d[i].adr);
				return -EINVAL;
			}
		}
		num_codecs += adr_link->num_adr;
	}

	c_conf = devm_kzalloc(dev, num_codecs * sizeof(*c_conf), GFP_KERNEL);
	if (!c_conf)
		return -ENOMEM;

	*codec_conf = c_conf;
	*codec_conf_count = num_codecs;

	return 0;
}

static int sof_card_dai_links_create(struct device *dev,
				     struct snd_soc_acpi_mach *mach,
				     struct snd_soc_card *card)
{
	int ssp_num, sdw_be_num = 0, hdmi_num = 0, dmic_num;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link_component *idisp_components;
	struct snd_soc_dai_link_component *ssp_components;
	struct snd_soc_acpi_mach_params *mach_params;
	const struct snd_soc_acpi_link_adr *adr_link;
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_codec_conf *codec_conf;
	int codec_conf_count;
	int codec_conf_index = 0;
	bool group_generated[SDW_MAX_GROUPS];
	int ssp_codec_index, ssp_mask;
	struct snd_soc_dai_link *links;
	int num_links, link_id = 0;
	char *name, *cpu_name;
	int total_cpu_dai_num;
	int sdw_cpu_dai_num;
	int i, j, be_id = 0;
	int cpu_id = 0;
	int comp_num;
	int ret;

	mach_params = &mach->mach_params;

	/* allocate codec conf, will be populated when dailinks are created */
	ret = sof_card_codec_conf_alloc(dev, mach_params, &codec_conf, &codec_conf_count);
	if (ret < 0)
		return ret;

	/* reset amp_num to ensure amp_num++ starts from 0 in each probe */
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		codec_info_list[i].amp_num = 0;

	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		hdmi_num = SOF_TGL_HDMI_COUNT;
	else
		hdmi_num = SOF_PRE_TGL_HDMI_COUNT;

	ssp_mask = SOF_SSP_GET_PORT(sof_sdw_quirk);
	/*
	 * on generic tgl platform, I2S or sdw mode is supported
	 * based on board rework. A ACPI device is registered in
	 * system only when I2S mode is supported, not sdw mode.
	 * Here check ACPI ID to confirm I2S is supported.
	 */
	ssp_codec_index = find_codec_info_acpi(mach->id);
	ssp_num = ssp_codec_index >= 0 ? hweight_long(ssp_mask) : 0;
	comp_num = hdmi_num + ssp_num;

	ret = get_sdw_dailink_info(mach_params->links,
				   &sdw_be_num, &sdw_cpu_dai_num);
	if (ret < 0) {
		dev_err(dev, "failed to get sdw link info %d", ret);
		return ret;
	}

	if (mach_params->codec_mask & IDISP_CODEC_MASK)
		ctx->idisp_codec = true;

	/* enable dmic01 & dmic16k */
	dmic_num = (sof_sdw_quirk & SOF_SDW_PCH_DMIC) ? 2 : 0;
	comp_num += dmic_num;

	dev_dbg(dev, "sdw %d, ssp %d, dmic %d, hdmi %d", sdw_be_num, ssp_num,
		dmic_num, ctx->idisp_codec ? hdmi_num : 0);

	/* allocate BE dailinks */
	num_links = comp_num + sdw_be_num;
	links = devm_kcalloc(dev, num_links, sizeof(*links), GFP_KERNEL);

	/* allocated CPU DAIs */
	total_cpu_dai_num = comp_num + sdw_cpu_dai_num;
	cpus = devm_kcalloc(dev, total_cpu_dai_num, sizeof(*cpus),
			    GFP_KERNEL);

	if (!links || !cpus)
		return -ENOMEM;

	/* SDW */
	if (!sdw_be_num)
		goto SSP;

	adr_link = mach_params->links;
	if (!adr_link)
		return -EINVAL;

	/*
	 * SoundWire Slaves aggregated in the same group may be
	 * located on different hardware links. Clear array to indicate
	 * CPU DAIs for this group have not been generated.
	 */
	for (i = 0; i < SDW_MAX_GROUPS; i++)
		group_generated[i] = false;

	/* generate DAI links by each sdw link */
	for (; adr_link->num_adr; adr_link++) {
		const struct snd_soc_acpi_endpoint *endpoint;

		endpoint = adr_link->adr_d->endpoints;
		if (endpoint->aggregated && !endpoint->group_id) {
			dev_err(dev, "invalid group id on link %x",
				adr_link->mask);
			continue;
		}

		/* this group has been generated */
		if (endpoint->aggregated &&
		    group_generated[endpoint->group_id])
			continue;

		ret = create_sdw_dailink(dev, &be_id, links, sdw_be_num,
					 sdw_cpu_dai_num, cpus, adr_link,
					 &cpu_id, group_generated,
					 codec_conf, codec_conf_count,
					 &codec_conf_index);
		if (ret < 0) {
			dev_err(dev, "failed to create dai link %d", be_id);
			return -ENOMEM;
		}
	}

	/* non-sdw DAI follows sdw DAI */
	link_id = be_id;

	/* get BE ID for non-sdw DAI */
	be_id = get_next_be_id(links, be_id);

SSP:
	/* SSP */
	if (!ssp_num)
		goto DMIC;

	for (i = 0, j = 0; ssp_mask; i++, ssp_mask >>= 1) {
		struct sof_sdw_codec_info *info;
		int playback, capture;
		char *codec_name;

		if (!(ssp_mask & 0x1))
			continue;

		name = devm_kasprintf(dev, GFP_KERNEL,
				      "SSP%d-Codec", i);
		if (!name)
			return -ENOMEM;

		cpu_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", i);
		if (!cpu_name)
			return -ENOMEM;

		ssp_components = devm_kzalloc(dev, sizeof(*ssp_components),
					      GFP_KERNEL);
		if (!ssp_components)
			return -ENOMEM;

		info = &codec_info_list[ssp_codec_index];
		codec_name = devm_kasprintf(dev, GFP_KERNEL, "i2c-%s:0%d",
					    info->acpi_id, j++);
		if (!codec_name)
			return -ENOMEM;

		ssp_components->name = codec_name;
		ssp_components->dai_name = info->dai_name;
		cpus[cpu_id].dai_name = cpu_name;

		playback = info->direction[SNDRV_PCM_STREAM_PLAYBACK];
		capture = info->direction[SNDRV_PCM_STREAM_CAPTURE];
		init_dai_link(links + link_id, be_id, name,
			      playback, capture,
			      cpus + cpu_id, 1,
			      ssp_components, 1,
			      NULL, info->ops);

		ret = info->init(NULL, links + link_id, info, 0);
		if (ret < 0)
			return ret;

		INC_ID(be_id, cpu_id, link_id);
	}

DMIC:
	/* dmic */
	if (dmic_num > 0) {
		cpus[cpu_id].dai_name = "DMIC01 Pin";
		init_dai_link(links + link_id, be_id, "dmic01",
			      0, 1, // DMIC only supports capture
			      cpus + cpu_id, 1,
			      dmic_component, 1,
			      sof_sdw_dmic_init, NULL);
		INC_ID(be_id, cpu_id, link_id);

		cpus[cpu_id].dai_name = "DMIC16k Pin";
		init_dai_link(links + link_id, be_id, "dmic16k",
			      0, 1, // DMIC only supports capture
			      cpus + cpu_id, 1,
			      dmic_component, 1,
			      /* don't call sof_sdw_dmic_init() twice */
			      NULL, NULL);
		INC_ID(be_id, cpu_id, link_id);
	}

	/* HDMI */
	if (hdmi_num > 0) {
		idisp_components = devm_kcalloc(dev, hdmi_num,
						sizeof(*idisp_components),
						GFP_KERNEL);
		if (!idisp_components)
			return -ENOMEM;
	}

	for (i = 0; i < hdmi_num; i++) {
		name = devm_kasprintf(dev, GFP_KERNEL,
				      "iDisp%d", i + 1);
		if (!name)
			return -ENOMEM;

		if (ctx->idisp_codec) {
			idisp_components[i].name = "ehdaudio0D2";
			idisp_components[i].dai_name = devm_kasprintf(dev,
								      GFP_KERNEL,
								      "intel-hdmi-hifi%d",
								      i + 1);
			if (!idisp_components[i].dai_name)
				return -ENOMEM;
		} else {
			idisp_components[i].name = "snd-soc-dummy";
			idisp_components[i].dai_name = "snd-soc-dummy-dai";
		}

		cpu_name = devm_kasprintf(dev, GFP_KERNEL,
					  "iDisp%d Pin", i + 1);
		if (!cpu_name)
			return -ENOMEM;

		cpus[cpu_id].dai_name = cpu_name;
		init_dai_link(links + link_id, be_id, name,
			      1, 0, // HDMI only supports playback
			      cpus + cpu_id, 1,
			      idisp_components + i, 1,
			      sof_sdw_hdmi_init, NULL);
		INC_ID(be_id, cpu_id, link_id);
	}

	card->dai_link = links;
	card->num_links = num_links;

	card->codec_conf = codec_conf;
	card->num_configs = codec_conf_count;

	return 0;
}

static int sof_sdw_card_late_probe(struct snd_soc_card *card)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		if (!codec_info_list[i].late_probe)
			continue;

		ret = codec_info_list[i].codec_card_late_probe(card);
		if (ret < 0)
			return ret;
	}

	return sof_sdw_hdmi_card_late_probe(card);
}

/* SoC card */
static const char sdw_card_long_name[] = "Intel Soundwire SOF";

static struct snd_soc_card card_sof_sdw = {
	.name = "soundwire",
	.owner = THIS_MODULE,
	.late_probe = sof_sdw_card_late_probe,
};

static int mc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &card_sof_sdw;
	struct snd_soc_acpi_mach *mach;
	struct mc_private *ctx;
	int amp_num = 0, i;
	int ret;

	dev_dbg(&pdev->dev, "Entry %s\n", __func__);

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	dmi_check_system(sof_sdw_quirk_table);

	if (quirk_override != -1) {
		dev_info(&pdev->dev, "Overriding quirk 0x%lx => 0x%x\n",
			 sof_sdw_quirk, quirk_override);
		sof_sdw_quirk = quirk_override;
	}
	log_quirks(&pdev->dev);

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, ctx);

	mach = pdev->dev.platform_data;
	ret = sof_card_dai_links_create(&pdev->dev, mach,
					card);
	if (ret < 0)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	/*
	 * the default amp_num is zero for each codec and
	 * amp_num will only be increased for active amp
	 * codecs on used platform
	 */
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		amp_num += codec_info_list[i].amp_num;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "cfg-spk:%d cfg-amp:%d",
					  (sof_sdw_quirk & SOF_SDW_FOUR_SPK)
					  ? 4 : 2, amp_num);
	if (!card->components)
		return -ENOMEM;

	card->long_name = sdw_card_long_name;

	/* Register the card */
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(card->dev, "snd_soc_register_card failed %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static int mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct snd_soc_dai_link *link;
	int ret;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		if (!codec_info_list[i].exit)
			continue;
		/*
		 * We don't need to call .exit function if there is no matched
		 * dai link found.
		 */
		for_each_card_prelinks(card, j, link) {
			if (!strcmp(link->codecs[0].dai_name,
				    codec_info_list[i].dai_name)) {
				ret = codec_info_list[i].exit(&pdev->dev, link);
				if (ret)
					dev_warn(&pdev->dev,
						 "codec exit failed %d\n",
						 ret);
				break;
			}
		}
	}

	return 0;
}

static struct platform_driver sof_sdw_driver = {
	.driver = {
		.name = "sof_sdw",
		.pm = &snd_soc_pm_ops,
	},
	.probe = mc_probe,
	.remove = mc_remove,
};

module_platform_driver(sof_sdw_driver);

MODULE_DESCRIPTION("ASoC SoundWire Generic Machine driver");
MODULE_AUTHOR("Bard Liao <yung-chuan.liao@linux.intel.com>");
MODULE_AUTHOR("Rander Wang <rander.wang@linux.intel.com>");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sof_sdw");
