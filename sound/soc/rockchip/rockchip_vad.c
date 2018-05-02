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
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_vad.h"

#define DRV_NAME "rockchip-vad"

#define VAD_RATES	SNDRV_PCM_RATE_8000_192000
#define VAD_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)
#define ACODEC_REG_NUM	28

struct vad_buf {
	void __iomem *begin;
	void __iomem *end;
	void __iomem *cur;
	void __iomem *pos;
	int size;
	bool loop;
};

struct rockchip_vad {
	struct device *dev;
	struct clk *hclk;
	struct regmap *regmap;
	unsigned int memphy;
	void __iomem *membase;
	struct vad_buf vbuf;
	int mode;
	u32 audio_src;
	u32 audio_src_addr;
	u32 audio_chnl;
	struct dentry *debugfs_dir;
};

struct audio_src_addr_map {
	u32 id;
	u32 addr;
};

static int rockchip_vad_stop(struct rockchip_vad *vad)
{
	unsigned int val;
	struct vad_buf *vbuf = &vad->vbuf;

	regmap_update_bits(vad->regmap, VAD_CTRL, VAD_EN_MASK, VAD_DISABLE);
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

	dev_info(vad->dev,
		 "begin: %p, cur: %p, end: %p, size: %d, loop: %d\n",
		 vbuf->begin, vbuf->cur, vbuf->end, vbuf->size, vbuf->loop);

	return 0;
}

static int rockchip_vad_setup(struct rockchip_vad *vad)
{
	struct regmap *regmap = vad->regmap;
	u32 val, mask;

	regmap_write(regmap, VAD_IS_ADDR, vad->audio_src_addr);

	val = NOISE_ABS(200);
	mask = NOISE_ABS_MASK;
	/* regmap_update_bits(regmap, VAD_DET_CON5, mask, val); */
	val = VAD_DET_CHNL(vad->audio_chnl);
	val |= vad->audio_src;
	val |= vad->mode << VAD_MODE_SHIFT;
	val |= SRC_ADDR_MODE_INC | SRC_BURST_INCR8;
	val |= VAD_EN;

	mask = VAD_DET_CHNL_MASK | AUDIO_SRC_SEL_MASK |
	       VAD_MODE_MASK | SRC_ADDR_MODE_MASK |
	       SRC_BURST_MASK | VAD_EN_MASK;

	regmap_update_bits(regmap, VAD_CTRL, mask, val);

	val = ERR_INT_EN | VAD_DET_INT_EN;
	mask = ERR_INT_EN_MASK | VAD_DET_INT_EN_MASK;

	regmap_update_bits(regmap, VAD_INT, mask, val);

	return 0;
}

static struct rockchip_vad *substream_get_drvdata(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_vad *vad = NULL;
	int i;

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
	int bytes;

	vad = substream_get_drvdata(substream);

	if (!vad)
		return -EFAULT;

	vbuf = &vad->vbuf;

	avail = snd_pcm_vad_avail(substream);
	avail = avail > frames ? frames : avail;
	bytes = frames_to_bytes(runtime, avail);

	if (bytes <= 0)
		return -EFAULT;

	dev_dbg(vad->dev,
		"begin: %p, pos: %p, end: %p, size: %d, bytes: %d\n",
		vbuf->begin, vbuf->pos, vbuf->end, vbuf->size, bytes);
	if (!vbuf->loop) {
		if (copy_to_user_fromio(buf, vbuf->pos, bytes))
			return -EFAULT;
		vbuf->pos += bytes;
	} else {
		if ((vbuf->pos + bytes) <= vbuf->end) {
			if (copy_to_user_fromio(buf, vbuf->pos, bytes))
				return -EFAULT;
			vbuf->pos += bytes;
		} else {
			int part1 = vbuf->end - vbuf->pos;
			int part2 = bytes - part1;

			copy_to_user_fromio(buf, vbuf->pos, part1);
			copy_to_user_fromio(buf + part1, vbuf->begin, part2);
			vbuf->pos = vbuf->begin + part2;
		}
	}

	vbuf->size -= bytes;
	dev_dbg(vad->dev,
		"begin: %p, pos: %p, end: %p, size: %d, bytes: %d\n",
		vbuf->begin, vbuf->pos, vbuf->end, vbuf->size, bytes);

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

	vad = substream_get_drvdata(substream);

	if (!vad)
		return 0;

	vbuf = &vad->vbuf;

	if (vbuf->size <= 0)
		return 0;

	dev_info(vad->dev, "%s, %d frames\n",
		 __func__, (int)bytes_to_frames(runtime, vbuf->size));
	return bytes_to_frames(runtime, vbuf->size);
}
EXPORT_SYMBOL(snd_pcm_vad_avail);

/**
 * snd_pcm_vad_attached - Check whether vad is attached to substream or not
 * @substream: PCM substream instance
 *
 * Result is true for attached or false for detached
 */
bool snd_pcm_vad_attached(struct snd_pcm_substream *substream)
{
	struct rockchip_vad *vad = NULL;

	vad = substream_get_drvdata(substream);

	return vad ? true : false;
}
EXPORT_SYMBOL(snd_pcm_vad_attached);

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
		return true;
	default:
		return false;
	}
}

static const struct reg_default rockchip_vad_reg_defaults[] = {
	{VAD_CTRL,     0x03000000},
	{VAD_DET_CON0, 0x00024020},
	{VAD_DET_CON1, 0x00ff0064},
	{VAD_DET_CON2, 0x3bf5e663},
	{VAD_DET_CON3, 0x3bf58817},
	{VAD_DET_CON4, 0x382b8858},
	{VAD_RAM_BEGIN_ADDR, 0xfff88000},
	{VAD_RAM_END_ADDR, 0xfffbfff8},
};

static const struct regmap_config rockchip_vad_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = VAD_INT,
	.reg_defaults = rockchip_vad_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rockchip_vad_reg_defaults),
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
	{4, 0xff380400}
};

static int rockchip_vad_get_audio_src_address(struct rockchip_vad *vad)
{
	const struct audio_src_addr_map *map;
	int i;

	for (i = 0; i < ARRAY_SIZE(addr_maps); i++) {
		map = &addr_maps[i];
		if (vad->audio_src == map->id) {
			vad->audio_src_addr = map->addr;
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

static int rockchip_vad_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = AUDIO_CHNL_16B;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		val = AUDIO_CHNL_24B;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(vad->regmap, VAD_CTRL, AUDIO_CHNL_BW_MASK, val);
	regmap_update_bits(vad->regmap, VAD_CTRL, AUDIO_CHNL_NUM_MASK,
			   AUDIO_CHNL_NUM(params_channels(params)));

	/*
	 * config acodec
	 * audio_src 2/3 is connected to acodec
	 */
	val = vad->audio_src >> AUDIO_SRC_SEL_SHIFT;
	if (val == 2 || val == 3)
		rockchip_vad_config_acodec(params, dai);

	return 0;
}

static int rockchip_vad_enable_cpudai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai;
	int ret = 0;

	if (PCM_RUNTIME_CHECK(substream))
		return -EFAULT;
	if (!rtd)
		return -EFAULT;

	cpu_dai = rtd->cpu_dai;

	pm_runtime_get_sync(cpu_dai->dev);

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->trigger)
		ret = cpu_dai->driver->ops->trigger(substream,
						    SNDRV_PCM_TRIGGER_START,
						    cpu_dai);

	return ret;
}

static void rockchip_vad_pcm_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rockchip_vad *vad = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return;

	rockchip_vad_enable_cpudai(substream);
	rockchip_vad_setup(vad);
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

static struct snd_soc_codec_driver soc_vad_codec;

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int rockchip_vad_debugfs_reg_show(struct seq_file *s, void *v)
{
	struct rockchip_vad *vad = s->private;
	unsigned int i;
	unsigned int val;

	for (i = VAD_CTRL; i <= VAD_INT; i += 4) {
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

static int rockchip_vad_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rockchip_vad *vad;
	struct resource *res;
	void __iomem *regbase;
	int irq;
	int ret;

	vad = devm_kzalloc(&pdev->dev, sizeof(*vad), GFP_KERNEL);
	if (!vad)
		return -ENOMEM;

	vad->dev = &pdev->dev;

	of_property_read_u32(np, "rockchip,mode", &vad->mode);
	of_property_read_u32(np, "rockchip,audio-src", &vad->audio_src);
	ret = rockchip_vad_get_audio_src_address(vad);
	if (ret)
		return ret;

	vad->audio_src = vad->audio_src << AUDIO_SRC_SEL_SHIFT;
	of_property_read_u32(np, "rockchip,det-channel", &vad->audio_chnl);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vad");
	regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regbase))
		return PTR_ERR(regbase);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "vad-memory");
	vad->memphy = res->start;
	vad->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vad->membase))
		return PTR_ERR(vad->membase);

	vad->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(vad->hclk))
		return PTR_ERR(vad->hclk);

	ret = clk_prepare_enable(vad->hclk);
	if (ret)
		return ret;

	vad->regmap = devm_regmap_init_mmio(&pdev->dev, regbase,
					    &rockchip_vad_regmap_config);
	if (IS_ERR(vad->regmap)) {
		ret = PTR_ERR(vad->regmap);
		goto err;
	}

	regmap_write(vad->regmap, VAD_RAM_BEGIN_ADDR, vad->memphy);
	vad->vbuf.begin = vad->membase;

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
	return snd_soc_register_codec(&pdev->dev, &soc_vad_codec,
				      &vad_dai, 1);
err:
	clk_disable_unprepare(vad->hclk);
	return ret;
}

static int rockchip_vad_remove(struct platform_device *pdev)
{
	struct rockchip_vad *vad = dev_get_drvdata(&pdev->dev);

	if (!IS_ERR(vad->hclk))
		clk_disable_unprepare(vad->hclk);
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id rockchip_vad_match[] = {
	{ .compatible = "rockchip,vad", },
	{},
};

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
