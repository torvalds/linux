// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip VAD driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_vad.h"
#include "rockchip_multi_dais.h"
#include "vad_preprocess.h"

#define DRV_NAME "rockchip-vad"

#define VAD_RATES	SNDRV_PCM_RATE_8000_192000
#define VAD_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)
#define ACODEC_REG_NUM	28
#define CHUNK_SIZE	64 /* bytes */

static struct snd_pcm_substream *vad_substream;
static unsigned int voice_inactive_frames;
module_param(voice_inactive_frames, uint, 0644);
MODULE_PARM_DESC(voice_inactive_frames, "voice inactive frame count");

enum rk_vad_version {
	VAD_RK1808ES = 1,
	VAD_RK1808,
	VAD_RK3308,
};

struct vad_buf {
	void __iomem *begin;
	void __iomem *end;
	void __iomem *cur;
	void __iomem *pos;
	int size;
	int loop_cnt;
	bool loop;
	bool sorted;
};

struct rockchip_vad {
	struct device *dev;
	struct device_node *audio_node;
	struct clk *hclk;
	struct regmap *regmap;
	unsigned int memphy;
	unsigned int memphy_end;
	void __iomem *membase;
	struct vad_buf vbuf;
	struct vad_params params;
	struct vad_uparams uparams;
	struct snd_soc_dai *cpu_dai;
	struct snd_pcm_substream *substream;
	int mode;
	u32 audio_src;
	u32 audio_src_addr;
	u32 audio_chnl;
	u32 channels;
	u32 sample_bytes;
	u32 buffer_time; /* msec */
	struct dentry *debugfs_dir;
	void *buf;
	bool acodec_cfg;
	bool vswitch;
	bool h_16bit;
	enum rk_vad_version version;
};

struct audio_src_addr_map {
	u32 id;
	u32 addr;
};

static inline int vframe_size(struct rockchip_vad *vad, int bytes)
{
	return bytes / vad->channels / vad->sample_bytes;
}

static int chunk_sort(void __iomem *pos, void __iomem *end, int loop_cnt)
{
	char tbuf[CHUNK_SIZE];
	int size1, size2;

	size1 = loop_cnt * 4;
	size2 = CHUNK_SIZE - size1;

	while (pos < end) {
		memcpy_fromio(&tbuf[0], pos + size1, size2);
		memcpy_fromio(&tbuf[size2], pos, size1);
		memcpy_toio(pos, &tbuf[0], CHUNK_SIZE);
		pos += CHUNK_SIZE;
	}

	return 0;
}

static int vad_buffer_sort(struct rockchip_vad *vad)
{
	struct vad_buf *vbuf = &vad->vbuf;
	int loop_cnt = vbuf->loop_cnt;

	if (vad->version != VAD_RK1808ES)
		return 0;

	if (vbuf->sorted || !vbuf->loop)
		return 0;

	/* 16 words align */
	if ((vbuf->pos - vbuf->begin) % CHUNK_SIZE ||
	    (vbuf->end - vbuf->pos) % CHUNK_SIZE)
		return -EINVAL;

	switch (loop_cnt) {
	case 0:
		loop_cnt = 16;
		chunk_sort(vbuf->pos, vbuf->end, loop_cnt - 1);
		vbuf->sorted = true;
		break;
	case 1:
		chunk_sort(vbuf->begin, vbuf->pos, loop_cnt);
		vbuf->sorted = true;
		break;
	case 2 ... 15:
		chunk_sort(vbuf->pos, vbuf->end, loop_cnt - 1);
		chunk_sort(vbuf->begin, vbuf->pos, loop_cnt);
		vbuf->sorted = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rockchip_vad_stop(struct rockchip_vad *vad)
{
	unsigned int val, frames;
	struct vad_buf *vbuf = &vad->vbuf;
	struct vad_params *params = &vad->params;

	regmap_read(vad->regmap, VAD_CTRL, &val);
	if ((val & VAD_EN_MASK) == VAD_DISABLE)
		return 0;

	/* sample cnt will be clear after vad disabled */
	if (vad->version == VAD_RK1808ES)
		regmap_read(vad->regmap, VAD_SAMPLE_CNT, &frames);
	regmap_update_bits(vad->regmap, VAD_CTRL, VAD_EN_MASK, VAD_DISABLE);
	regmap_read(vad->regmap, VAD_CTRL, &val);
	vad->h_16bit = (val & AUDIO_24BIT_SAT_MASK) == AUDIO_H16B;
	regmap_read(vad->regmap, VAD_RAM_END_ADDR, &val);
	vbuf->end = vbuf->begin + (val - vad->memphy) + 0x8;
	regmap_read(vad->regmap, VAD_INT, &val);
	val &= BIT(8);
	vbuf->loop = val;
	regmap_read(vad->regmap, VAD_RAM_CUR_ADDR, &val);
	if (!val) {
		vbuf->size = 0;
		vbuf->cur = vbuf->begin;
		return 0;
	}
	vbuf->cur = vbuf->begin + (val - vad->memphy);

	if (vbuf->loop) {
		vbuf->size = vbuf->end - vbuf->begin;
		vbuf->pos = vbuf->cur;
	} else {
		vbuf->size = vbuf->cur - vbuf->begin;
		vbuf->end = vbuf->cur;
		vbuf->pos = vbuf->begin;
	}

	if (vad->version == VAD_RK1808ES) {
		vbuf->loop_cnt = (frames / vframe_size(vad, vbuf->size)) % 16;
		/* due to get loop_cnt before vad disable, we should take
		 * the boundary issue into account, and judge whether the
		 * loop_cnt change to loop_cnt + 1 or not when vad disable.
		 */
		if (vbuf->loop) {
			frames = frames % vframe_size(vad, vbuf->size);
			val = vframe_size(vad, vbuf->pos - vbuf->begin);
			if (frames > val)
				vbuf->loop_cnt = (vbuf->loop_cnt + 1) % 16;
		}
		vbuf->sorted = false;
	}
	regmap_read(vad->regmap, VAD_DET_CON0, &val);
	params->noise_level = (val & NOISE_LEVEL_MASK) >> NOISE_LEVEL_SHIFT;
	params->vad_con_thd = (val & VAD_CON_THD_MASK) >> VAD_CON_THD_SHIFT;
	params->voice_gain = (val & GAIN_MASK) >> GAIN_SHIFT;
	regmap_read(vad->regmap, VAD_DET_CON1, &val);
	params->sound_thd = val & SOUND_THD_MASK;
	regmap_read(vad->regmap, VAD_DET_CON5, &val);
	params->noise_abs = val & NOISE_ABS_MASK;

	vad_preprocess_init(params);
	voice_inactive_frames = 0;

	dev_info(vad->dev, "bufsize: %d, hw_abs: 0x%x\n",
		 vbuf->size, params->noise_abs);

	return 0;
}

static int rockchip_vad_setup(struct rockchip_vad *vad)
{
	struct regmap *regmap = vad->regmap;
	u32 val, mask;

	dev_info(vad->dev, "sw_abs: 0x%x\n",
		 vad->uparams.noise_abs);
	regmap_update_bits(regmap, VAD_DET_CON5,
			   NOISE_ABS_MASK, vad->uparams.noise_abs);
	regmap_update_bits(regmap, VAD_CTRL, VAD_EN_MASK, VAD_EN);

	val = ERR_INT_EN | VAD_DET_INT_EN;
	mask = ERR_INT_EN_MASK | VAD_DET_INT_EN_MASK;

	regmap_update_bits(regmap, VAD_INT, mask, val);

	vad_preprocess_destroy();

	return 0;
}

static struct rockchip_vad *substream_get_drvdata(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_vad *vad = NULL;
	unsigned int i;

	if (PCM_RUNTIME_CHECK(substream))
		return NULL;
	if (!rtd)
		return NULL;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];

		if (strstr(codec_dai->name, "vad"))
			vad = snd_soc_codec_get_drvdata(codec_dai->codec);
	}

	return vad;
}

snd_pcm_sframes_t snd_pcm_vad_read(struct snd_pcm_substream *substream,
				   void __user *buf, snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_vad *vad = NULL;
	struct vad_buf *vbuf;
	snd_pcm_uframes_t avail;
	int bytes, vbytes, frame_sz, vframe_sz, padding_sz;
	unsigned int i;
	void *pbuf, *sbuf;

	vad = substream_get_drvdata(substream);

	if (!vad)
		return -EFAULT;

	vbuf = &vad->vbuf;

	avail = snd_pcm_vad_avail(substream);
	avail = avail > frames ? frames : avail;
	bytes = frames_to_bytes(runtime, avail);
	if (bytes <= 0)
		return -EFAULT;

	if (vad_buffer_sort(vad) < 0) {
		dev_err(vad->dev, "buffer sort failed\n");
		return -EFAULT;
	}

	if (!vad->buf) {
		vad->buf = kzalloc(bytes, GFP_KERNEL);
		if (!vad->buf)
			return -ENOMEM;
	}

	frame_sz = frames_to_bytes(runtime, 1);
	vframe_sz = samples_to_bytes(runtime, vad->channels);
	padding_sz = frame_sz - vframe_sz;
	vbytes = vframe_sz * avail;
	sbuf = vad->buf;
	pbuf = vad->buf + bytes - vbytes;
	if (!vbuf->loop) {
		memcpy_fromio(pbuf, vbuf->pos, vbytes);
		vbuf->pos += vbytes;
	} else {
		if ((vbuf->pos + vbytes) <= vbuf->end) {
			memcpy_fromio(pbuf, vbuf->pos, vbytes);
			vbuf->pos += vbytes;
		} else {
			int part1 = vbuf->end - vbuf->pos;
			int part2 = vbytes - part1;

			memcpy_fromio(pbuf, vbuf->pos, part1);
			memcpy_fromio(pbuf + part1, vbuf->begin, part2);
			vbuf->pos = vbuf->begin + part2;
		}
	}

	if (padding_sz) {
		for (i = 0; i < avail; i++) {
			memmove(sbuf, pbuf, vframe_sz);
			sbuf += vframe_sz;
			pbuf += vframe_sz;
			memset(sbuf, 0x0, padding_sz);
			sbuf += padding_sz;
		}
	}

	if (copy_to_user(buf, vad->buf, bytes))
		return -EFAULT;

	vbuf->size -= vbytes;
	if (vbuf->size <= 0) {
		kfree(vad->buf);
		vad->buf = NULL;
	}

	return avail;
}
EXPORT_SYMBOL(snd_pcm_vad_read);

/**
 * snd_pcm_vad_avail - Get the available (readable) space for vad
 * @runtime: PCM substream instance
 *
 * Result is between 0 ... (boundary - 1)
 */
snd_pcm_uframes_t snd_pcm_vad_avail(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_vad *vad = NULL;
	struct vad_buf *vbuf;
	snd_pcm_uframes_t vframes;

	vad = substream_get_drvdata(substream);

	if (!vad)
		return 0;

	vbuf = &vad->vbuf;

	if (vbuf->size <= 0)
		return 0;

	vframes = samples_to_bytes(runtime, vad->channels);
	if (vframes)
		vframes = vbuf->size / vframes;
	if (!vframes)
		dev_err(vad->dev, "residue bytes: %d\n", vbuf->size);

	return vframes;
}
EXPORT_SYMBOL(snd_pcm_vad_avail);

int snd_pcm_vad_preprocess(struct snd_pcm_substream *substream,
			   void *buf, snd_pcm_uframes_t size)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_vad *vad = NULL;
	unsigned int i;
	s16 *data;

	vad = substream_get_drvdata(substream);

	if (!vad)
		return 0;

	buf += samples_to_bytes(runtime, vad->audio_chnl);
	/* retrieve the high 16bit data */
	if (runtime->sample_bits == 32 && vad->h_16bit)
		buf += 2;
	for (i = 0; i < size; i++) {
		data = buf;
		if (vad_preprocess(*data))
			voice_inactive_frames = 0;
		else
			voice_inactive_frames++;
		buf += frames_to_bytes(runtime, 1);
	}

	vad_preprocess_update_params(&vad->uparams);
	return 0;
}
EXPORT_SYMBOL(snd_pcm_vad_preprocess);

/**
 * snd_pcm_vad_attached - Check whether vad is attached to substream or not
 * @substream: PCM substream instance
 *
 * Result is true for attached or false for detached
 */
bool snd_pcm_vad_attached(struct snd_pcm_substream *substream)
{
	struct rockchip_vad *vad = NULL;

	if (vad_substream == substream)
		vad = substream_get_drvdata(substream);

	if (vad && vad->vswitch)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(snd_pcm_vad_attached);

static int vad_memcpy_fromio(void *to, void __iomem *from,
			     int size, int frame_sz, int padding_sz)
{
	int i, step_src, step_dst, fcount;

	step_src = frame_sz;
	step_dst = frame_sz + padding_sz;

	if (size % frame_sz) {
		pr_err("%s: invalid size: %d\n", __func__, size);
		return -EINVAL;
	}

	fcount = size / frame_sz;
	if (padding_sz) {
		for (i = 0; i < fcount; i++) {
			memcpy_fromio(to, from, frame_sz);
			to += step_dst;
			from += step_src;
		}
	} else {
		memcpy_fromio(to, from, size);
	}

	return 0;
}

/**
 * snd_pcm_vad_memcpy - Copy vad data to dst
 * @substream: PCM substream instance
 * @buf: dst buf
 * @frames:  size in frame
 *
 * Result is copied frames for success or errno for fail
 */
snd_pcm_sframes_t snd_pcm_vad_memcpy(struct snd_pcm_substream *substream,
				     void *buf, snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rockchip_vad *vad = NULL;
	struct vad_buf *vbuf;
	snd_pcm_uframes_t avail;
	int bytes, vbytes, frame_sz, vframe_sz, padding_sz;

	vad = substream_get_drvdata(substream);

	if (!vad)
		return -EFAULT;

	vbuf = &vad->vbuf;

	avail = snd_pcm_vad_avail(substream);
	avail = avail > frames ? frames : avail;
	bytes = frames_to_bytes(runtime, avail);

	if (bytes <= 0)
		return -EFAULT;

	if (vad_buffer_sort(vad) < 0) {
		dev_err(vad->dev, "buffer sort failed\n");
		return -EFAULT;
	}

	frame_sz = frames_to_bytes(runtime, 1);
	vframe_sz = samples_to_bytes(runtime, vad->channels);
	padding_sz = frame_sz - vframe_sz;
	vbytes = vframe_sz * avail;

	memset(buf, 0x0, bytes);
	if (!vbuf->loop) {
		vad_memcpy_fromio(buf, vbuf->pos, vbytes,
				  vframe_sz, padding_sz);
		vbuf->pos += vbytes;
	} else {
		if ((vbuf->pos + vbytes) <= vbuf->end) {
			vad_memcpy_fromio(buf, vbuf->pos, vbytes,
					  vframe_sz, padding_sz);
			vbuf->pos += vbytes;
		} else {
			int part1 = vbuf->end - vbuf->pos;
			int part2 = vbytes - part1;
			int offset = part1;

			if (padding_sz)
				offset = part1 / vframe_sz * frame_sz;
			vad_memcpy_fromio(buf, vbuf->pos, part1,
					  vframe_sz, padding_sz);
			vad_memcpy_fromio(buf + offset, vbuf->begin, part2,
					  vframe_sz, padding_sz);
			vbuf->pos = vbuf->begin + part2;
		}
	}

	vbuf->size -= vbytes;

	return avail;
}
EXPORT_SYMBOL(snd_pcm_vad_memcpy);

static bool rockchip_vad_writeable_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static bool rockchip_vad_readable_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static bool rockchip_vad_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case VAD_INT:
	case VAD_RAM_CUR_ADDR:
	case VAD_DET_CON5:
	case VAD_SAMPLE_CNT:
		return true;
	default:
		return false;
	}
}

static const struct reg_default rk1808_vad_reg_defaults[] = {
	{VAD_CTRL,     0x03000000},
	{VAD_DET_CON0, 0x01024008},
	{VAD_DET_CON1, 0x04ff0064},
	{VAD_DET_CON2, 0x3bf5e663},
	{VAD_DET_CON3, 0x3bf58817},
	{VAD_DET_CON4, 0x382b8858},
};

static const struct reg_default rk3308_vad_reg_defaults[] = {
	{VAD_CTRL,     0x03000000},
	{VAD_DET_CON0, 0x00024020},
	{VAD_DET_CON1, 0x00ff0064},
	{VAD_DET_CON2, 0x3bf5e663},
	{VAD_DET_CON3, 0x3bf58817},
	{VAD_DET_CON4, 0x382b8858},
	{VAD_RAM_BEGIN_ADDR, 0xfff88000},
	{VAD_RAM_END_ADDR, 0xfffbfff8},
};

static const struct regmap_config rk1808_vad_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = VAD_NOISE_DATA,
	.reg_defaults = rk1808_vad_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk1808_vad_reg_defaults),
	.writeable_reg = rockchip_vad_writeable_reg,
	.readable_reg = rockchip_vad_readable_reg,
	.volatile_reg = rockchip_vad_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config rk3308_vad_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = VAD_INT,
	.reg_defaults = rk3308_vad_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk3308_vad_reg_defaults),
	.writeable_reg = rockchip_vad_writeable_reg,
	.readable_reg = rockchip_vad_readable_reg,
	.volatile_reg = rockchip_vad_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static const struct audio_src_addr_map addr_maps[] = {
	{0, 0xff300800},
	{1, 0xff310800},
	{2, 0xff320800},
	{3, 0xff330800},
	{4, 0xff380400},
	{1, 0xff7e0800},
	{3, 0xff7f0800},
	{4, 0xff800400},
};

static int rockchip_vad_get_audio_src_address(struct rockchip_vad *vad,
					      u32 addr)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(addr_maps); i++) {
		if ((addr & addr_maps[i].addr) == addr) {
			vad->audio_src = addr_maps[i].id;
			vad->audio_src_addr = addr_maps[i].addr;
			return 0;
		}
	}

	return -ENODEV;
}

static irqreturn_t rockchip_vad_irq(int irqno, void *dev_id)
{
	struct rockchip_vad *vad = dev_id;
	unsigned  int val;

	regmap_read(vad->regmap, VAD_INT, &val);
	regmap_write(vad->regmap, VAD_INT, val);

	dev_dbg(vad->dev, "irq 0x%08x\n", val);

	return IRQ_HANDLED;
}

static const struct reg_sequence rockchip_vad_acodec_adc_enable[] = {
	{ VAD_OD_ADDR0, 0x36261606 },
	{ VAD_D_DATA0, 0x51515151 },
	{ VAD_OD_ADDR1, 0x30201000 },
	{ VAD_D_DATA1, 0xbbbbbbbb },
	{ VAD_OD_ADDR2, 0x32221202 },
	{ VAD_D_DATA2, 0x11111111 },
	{ VAD_OD_ADDR3, 0x35251505 },
	{ VAD_D_DATA3, 0x77777777 },
	{ VAD_OD_ADDR4, 0x32221202 },
	{ VAD_D_DATA4, 0x33333333 },
	{ VAD_OD_ADDR5, 0x30201000 },
	{ VAD_D_DATA5, 0xffffffff },
	{ VAD_OD_ADDR6, 0x32221202 },
	{ VAD_D_DATA6, 0x77777777 },
};

static int rockchip_vad_config_acodec(struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0;

	if (!vad->acodec_cfg)
		return 0;

	val = ACODEC_BASE + ACODEC_ADC_ANA_CON0;
	regmap_write(vad->regmap, VAD_ID_ADDR, val);

	regmap_multi_reg_write(vad->regmap, rockchip_vad_acodec_adc_enable,
			       ARRAY_SIZE(rockchip_vad_acodec_adc_enable));

	regmap_update_bits(vad->regmap, VAD_CTRL, ACODE_CFG_REG_NUM_MASK,
			   ACODE_CFG_REG_NUM(ACODEC_REG_NUM));
	regmap_update_bits(vad->regmap, VAD_CTRL, CFG_ACODE_AFTER_DET_EN_MASK,
			   CFG_ACODE_AFTER_DET_EN);

	return 0;
}

static struct snd_soc_dai *rockchip_vad_find_dai(struct device_node *np)
{
	struct snd_soc_dai_link_component dai_component = { 0 };

	dai_component.of_node = np;

	return snd_soc_find_dai(&dai_component);
}

static void hw_refine_channels(struct snd_pcm_hw_params *params,
			       unsigned int channel)
{
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	c->min = channel;
	c->max = channel;
}

static void rockchip_vad_params_fixup(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(dai->codec);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai, *audio_src_dai;
	struct device_node *np;
	struct rk_mdais_dev *mdais;
	unsigned int *channel_maps;
	int i;

	cpu_dai = rtd->cpu_dai;
	vad->cpu_dai = cpu_dai;
	vad->substream = substream;
	np = cpu_dai->dev->of_node;
	if (of_device_is_compatible(np, "rockchip,multi-dais")) {
		audio_src_dai = rockchip_vad_find_dai(vad->audio_node);
		mdais = snd_soc_dai_get_drvdata(cpu_dai);
		channel_maps = mdais->capture_channel_maps;
		for (i = 0; i < mdais->num_dais; i++) {
			if (audio_src_dai == mdais->dais[i].dai &&
			    channel_maps[i])
				hw_refine_channels(params, channel_maps[i]);
		}
	}
}

static int rockchip_vad_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0, mask = 0, frame_bytes, buf_time;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	rockchip_vad_params_fixup(substream, params, dai);
	vad->channels = params_channels(params);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = AUDIO_CHNL_16B;
		vad->sample_bytes = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		val = AUDIO_CHNL_24B;
		vad->sample_bytes = 4;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(vad->regmap, VAD_CTRL, AUDIO_CHNL_BW_MASK, val);
	regmap_update_bits(vad->regmap, VAD_CTRL, AUDIO_CHNL_NUM_MASK,
			   AUDIO_CHNL_NUM(params_channels(params)));

	val = SRC_ADDR_MODE_INC | SRC_BURST_INCR;
	mask = SRC_ADDR_MODE_MASK | SRC_BURST_MASK | INCR_BURST_LEN_MASK;
	switch (params_channels(params)) {
	case 8:
		/* fallthrough */
	case 6:
		/* fallthrough */
	case 4:
		/* fallthrough */
	case 2:
		val |= INCR_BURST_LEN(params_channels(params));
		break;
	default:
		return -EINVAL;
	}

	if (vad->version == VAD_RK1808ES) {
		val = SRC_ADDR_MODE_INC | SRC_BURST_INCR16;
		mask = SRC_ADDR_MODE_MASK | SRC_BURST_MASK | SRC_BURST_NUM_MASK;
		if (params_channels(params) == 6)
			val |= SRC_BURST_NUM(3);
	}
	regmap_update_bits(vad->regmap, VAD_CTRL, mask, val);

	/* calculate buffer space according buffer time */
	if (vad->buffer_time) {
		frame_bytes = snd_pcm_format_size(params_format(params),
						  params_channels(params));

		buf_time = vad->memphy_end - vad->memphy + 0x8;
		buf_time *= 1000;
		buf_time /= (frame_bytes * params_rate(params));
		if (buf_time < vad->buffer_time)
			dev_info(vad->dev, "max buffer time: %u ms.\n", buf_time);
		buf_time = min(buf_time, vad->buffer_time);

		val = params_rate(params) * buf_time / 1000;
		if (vad->version == VAD_RK1808ES)
			val &= ~0xf; /* 16 align */
		val *= frame_bytes;
		val += vad->memphy;
		val -= 0x8;
		if (val < vad->memphy || val > vad->memphy_end)
			return -EINVAL;
		regmap_write(vad->regmap, VAD_RAM_END_ADDR, val);
	}

	/*
	 * config acodec
	 * audio_src 2/3 is connected to acodec
	 */
	val = vad->audio_src >> AUDIO_SRC_SEL_SHIFT;
	if (val == 2 || val == 3)
		rockchip_vad_config_acodec(params, dai);

	return 0;
}

static int rockchip_vad_enable_cpudai(struct rockchip_vad *vad)
{
	struct snd_soc_dai *cpu_dai;
	struct snd_pcm_substream *substream;
	int ret = 0;

	cpu_dai = vad->cpu_dai;
	substream = vad->substream;

	if (!cpu_dai || !substream)
		return 0;

	pm_runtime_get_sync(cpu_dai->dev);

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->trigger)
		ret = cpu_dai->driver->ops->trigger(substream,
						    SNDRV_PCM_TRIGGER_START,
						    cpu_dai);

	return ret;
}

static int rockchip_vad_disable_cpudai(struct rockchip_vad *vad)
{
	struct snd_soc_dai *cpu_dai;
	struct snd_pcm_substream *substream;
	int ret = 0;

	cpu_dai = vad->cpu_dai;
	substream = vad->substream;

	if (!cpu_dai || !substream)
		return 0;

	pm_runtime_get_sync(cpu_dai->dev);

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->trigger)
		ret = cpu_dai->driver->ops->trigger(substream,
						    SNDRV_PCM_TRIGGER_STOP,
						    cpu_dai);

	pm_runtime_put(cpu_dai->dev);
	return ret;
}

static int rockchip_vad_pcm_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	vad_substream = substream;

	return 0;
}

static void rockchip_vad_pcm_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return;

	if (vad->vswitch) {
		rockchip_vad_enable_cpudai(vad);
		rockchip_vad_setup(vad);
	}

	vad_substream = NULL;
}

static int rockchip_vad_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(codec);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			return 0;
		rockchip_vad_stop(vad);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		break;
	}

	return 0;
}

static struct snd_soc_dai_ops rockchip_vad_dai_ops = {
	.hw_params = rockchip_vad_hw_params,
	.shutdown = rockchip_vad_pcm_shutdown,
	.startup = rockchip_vad_pcm_startup,
	.trigger = rockchip_vad_trigger,
};

static struct snd_soc_dai_driver vad_dai = {
	.name = "vad",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = VAD_RATES,
		.formats = VAD_FORMATS,
	},
	.capture = {
		 .stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = VAD_RATES,
		.formats = VAD_FORMATS,
	},
	.ops = &rockchip_vad_dai_ops,
};

static int rockchip_vad_switch_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int rockchip_vad_switch_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rockchip_vad *vad = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = vad->vswitch;

	return 0;
}

static int rockchip_vad_switch_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rockchip_vad *vad = snd_soc_component_get_drvdata(component);
	int val;

	val = ucontrol->value.integer.value[0];
	if (val && !vad->vswitch) {
		vad->vswitch = true;
	} else if (!val && vad->vswitch) {
		vad->vswitch = false;

		regmap_read(vad->regmap, VAD_CTRL, &val);
		if ((val & VAD_EN_MASK) == VAD_DISABLE)
			return 0;
		rockchip_vad_stop(vad);
		rockchip_vad_disable_cpudai(vad);
		/* this case we don't need vad data */
		vad->vbuf.size = 0;
	}

	return 0;
}

#define SOC_ROCKCHIP_VAD_SWITCH_DECL(xname) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = rockchip_vad_switch_info, .get = rockchip_vad_switch_get, \
	.put = rockchip_vad_switch_put, }

static const struct snd_kcontrol_new rockchip_vad_dapm_controls[] = {
	SOC_ROCKCHIP_VAD_SWITCH_DECL("vad switch"),
};

static struct snd_soc_codec_driver soc_vad_codec = {
	.component_driver = {
		.controls = rockchip_vad_dapm_controls,
		.num_controls = ARRAY_SIZE(rockchip_vad_dapm_controls),
	},
};

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int rockchip_vad_debugfs_reg_show(struct seq_file *s, void *v)
{
	struct rockchip_vad *vad = s->private;
	unsigned int i;
	unsigned int val;
	unsigned int max_register;

	if (vad->version == VAD_RK1808 ||
	    vad->version == VAD_RK1808ES)
		max_register = VAD_NOISE_DATA;
	else
		max_register = VAD_INT;
	for (i = VAD_CTRL; i <= max_register; i += 4) {
		regmap_read(vad->regmap, i, &val);
		if (!(i % 16))
			seq_printf(s, "\n%08x:  ", i);
		seq_printf(s, "%08x ", val);
	}

	return 0;
}

static ssize_t rockchip_vad_debugfs_reg_write(struct file *file,
					      const char __user *buf,
					      size_t count, loff_t *ppos)
{
	struct rockchip_vad *vad = ((struct seq_file *)file->private_data)->private;
	unsigned int reg, val;
	char kbuf[24];

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	if (sscanf(kbuf, "%x %x", &reg, &val) != 2)
		return -EFAULT;

	regmap_write(vad->regmap, reg, val);

	return count;
}

static int rockchip_vad_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, rockchip_vad_debugfs_reg_show, inode->i_private);
}

static const struct file_operations rockchip_vad_reg_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rockchip_vad_debugfs_open,
	.read = seq_read,
	.write = rockchip_vad_debugfs_reg_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static void rockchip_vad_init(struct rockchip_vad *vad)
{
	unsigned int val, mask;

	regmap_write(vad->regmap, VAD_RAM_BEGIN_ADDR, vad->memphy);
	regmap_write(vad->regmap, VAD_RAM_END_ADDR, vad->memphy_end);
	vad->vbuf.begin = vad->membase;
	regmap_write(vad->regmap, VAD_IS_ADDR, vad->audio_src_addr);

	val = VAD_DET_CHNL(vad->audio_chnl);
	val |= vad->audio_src;
	val |= vad->mode << VAD_MODE_SHIFT;
	mask = VAD_DET_CHNL_MASK | AUDIO_SRC_SEL_MASK |
	       VAD_MODE_MASK;

	regmap_update_bits(vad->regmap, VAD_CTRL, mask, val);
	if (vad->version == VAD_RK1808 ||
	    vad->version == VAD_RK1808ES) {
		regmap_update_bits(vad->regmap, VAD_AUX_CONTROL,
				   RAM_ITF_EN_MASK | BUS_WRITE_EN_MASK,
				   RAM_ITF_DIS | BUS_WRITE_EN);
		regmap_update_bits(vad->regmap, VAD_AUX_CONTROL,
				   SAMPLE_CNT_EN_MASK, SAMPLE_CNT_EN);
	}
}

static const struct of_device_id rockchip_vad_match[] = {
	{ .compatible = "rockchip,rk1808es-vad", .data = (void *)VAD_RK1808ES },
	{ .compatible = "rockchip,rk1808-vad", .data = (void *)VAD_RK1808 },
	{ .compatible = "rockchip,rk3308-vad", .data = (void *)VAD_RK3308 },
	{},
};

static int rockchip_vad_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *asrc_np = NULL;
	struct device_node *sram_np = NULL;
	const struct of_device_id *match;
	const struct regmap_config *regmap_config;
	struct rockchip_vad *vad;
	struct resource *res;
	struct resource audio_res;
	struct resource sram_res;
	void __iomem *regbase;
	int irq;
	int ret;

	vad = devm_kzalloc(&pdev->dev, sizeof(*vad), GFP_KERNEL);
	if (!vad)
		return -ENOMEM;

	vad->dev = &pdev->dev;

	match = of_match_device(rockchip_vad_match, &pdev->dev);
	if (match)
		vad->version = (enum rk_vad_version)match->data;

	switch (vad->version) {
	case VAD_RK1808:
	case VAD_RK1808ES:
		regmap_config = &rk1808_vad_regmap_config;
		break;
	case VAD_RK3308:
		regmap_config = &rk3308_vad_regmap_config;
		break;
	default:
		return -EINVAL;
	}

	vad->acodec_cfg = of_property_read_bool(np, "rockchip,acodec-cfg");
	of_property_read_u32(np, "rockchip,mode", &vad->mode);
	of_property_read_u32(np, "rockchip,det-channel", &vad->audio_chnl);
	of_property_read_u32(np, "rockchip,buffer-time-ms", &vad->buffer_time);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vad");
	regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regbase))
		return PTR_ERR(regbase);

	sram_np = of_parse_phandle(np, "rockchip,audio-sram", 0);
	if (!sram_np) {
		dev_err(&pdev->dev, "could not find sram dt node\n");
		return -ENODEV;
	}

	asrc_np = of_parse_phandle(np, "rockchip,audio-src", 0);
	if (!asrc_np) {
		ret = -ENODEV;
		goto err_phandle;
	}

	ret = of_address_to_resource(asrc_np, 0, &audio_res);
	if (ret)
		goto err_phandle;
	ret = rockchip_vad_get_audio_src_address(vad, audio_res.start);
	if (ret)
		goto err_phandle;
	vad->audio_node = asrc_np;
	vad->audio_src <<= AUDIO_SRC_SEL_SHIFT;

	ret = of_address_to_resource(sram_np, 0, &sram_res);
	if (ret)
		goto err_phandle;
	vad->memphy = sram_res.start;
	vad->memphy_end = sram_res.start + resource_size(&sram_res) - 0x8;
	vad->membase = devm_ioremap(&pdev->dev, sram_res.start,
				    resource_size(&sram_res));
	if (!vad->membase) {
		ret = -ENOMEM;
		goto err_phandle;
	}

	if (IS_ERR(vad->membase)) {
		ret = PTR_ERR(vad->membase);
		goto err_phandle;
	}

	vad->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(vad->hclk)) {
		ret = PTR_ERR(vad->hclk);
		goto err_phandle;
	}

	ret = clk_prepare_enable(vad->hclk);
	if (ret)
		goto err_phandle;

	vad->regmap = devm_regmap_init_mmio(&pdev->dev, regbase,
					    regmap_config);
	if (IS_ERR(vad->regmap)) {
		ret = PTR_ERR(vad->regmap);
		goto err;
	}

	rockchip_vad_init(vad);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, irq, rockchip_vad_irq,
			       0, dev_name(&pdev->dev), vad);
	if (ret < 0)
		goto err;

#if defined(CONFIG_DEBUG_FS)
	vad->debugfs_dir = debugfs_create_dir("vad", NULL);
	if (IS_ERR(vad->debugfs_dir))
		dev_err(&pdev->dev, "failed to create debugfs dir for vad!\n");
	else
		debugfs_create_file("reg", 0644, vad->debugfs_dir, vad,
				    &rockchip_vad_reg_debugfs_fops);
#endif

	platform_set_drvdata(pdev, vad);
	ret = snd_soc_register_codec(&pdev->dev, &soc_vad_codec,
				     &vad_dai, 1);
	if (ret)
		goto err;

	of_node_put(sram_np);

	return 0;
err:
	clk_disable_unprepare(vad->hclk);
err_phandle:
	of_node_put(sram_np);
	of_node_put(asrc_np);
	return ret;
}

static int rockchip_vad_remove(struct platform_device *pdev)
{
	struct rockchip_vad *vad = dev_get_drvdata(&pdev->dev);

	if (!IS_ERR(vad->hclk))
		clk_disable_unprepare(vad->hclk);
	of_node_put(vad->audio_node);
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver rockchip_vad_driver = {
	.probe = rockchip_vad_probe,
	.remove = rockchip_vad_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rockchip_vad_match),
	},
};
module_platform_driver(rockchip_vad_driver);

MODULE_DESCRIPTION("Rockchip VAD Controller");
MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_vad_match);
