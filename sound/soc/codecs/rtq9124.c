// SPDX-License-Identifier: GPL-2.0-only
//
// rtq9124.c -- RTQ9124 ALSA SoC Codec driver
//
// Author: ChiYuan Huang <cy_huang@richtek.com>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/byteorder/generic.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#define RTQ9124_REG_SDI_SEL		0x00
#define RTQ9124_REG_SDO_SEL		0x01
#define RTQ9124_REG_I2S_OPT		0x02
#define RTQ9124_REG_AMP_OPT		0x03
#define RTQ9124_REG_STATE_CTRL		0x04
#define RTQ9124_REG_PWM_PHASE		0x05
#define RTQ9124_REG_SIL_CTRL		0x06
#define RTQ9124_REG_PWM_SS_OPT		0x07
#define RTQ9124_REG_ERR_INT_0		0x10
#define RTQ9124_REG_ERR_MASK6		0x26
#define RTQ9124_REG_TDM_TX_CH0		0x32
#define RTQ9124_REG_TDM_RX_CH0		0x34
#define RTQ9124_REG_VOL_OPT		0x38
#define RTQ9124_REG_DCR_TH		0x4B
#define RTQ9124_REG_ERR_TH		0x4C
#define RTQ9124_REG_PROT_EN		0x5B
#define RTQ9124_REG_PRJ_CODE		0xF9

#define RTQ9124_MASK_CS_DATA_INV	BIT(9)
#define RTQ9124_MASK_VDDIO_SDO_SEL	BIT(8)
#define RTQ9124_MASK_AUD_BITS		GENMASK(5, 4)
#define RTQ9124_MASK_AUD_FMT		GENMASK(3, 0)
#define RTQ9124_MASK_CH_STATE		GENMASK(1, 0)
#define RTQ9124_MASK_SF_RESET		BIT(15)

#define RTQ9124_FIXED_VENID		0x9124

struct rtq9124_priv {
	struct gpio_desc *enable;
	unsigned int dai_fmt;
	int tdm_slots;
	int tdm_slot_width;
};

static int rtq9124_enable_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	unsigned int i, chan_state;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Change state to normal */
		chan_state = 0;
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Change state to HiZ */
		chan_state = 1;
		break;
	default:
		return -EINVAL;
	}

	/* Before amp turn on, clear old events first */
	for (i = 0; !chan_state && i < 8; i++)
		snd_soc_component_write(comp, RTQ9124_REG_ERR_INT_0 + i, 0xffff);

	snd_soc_component_write_field(comp, RTQ9124_REG_STATE_CTRL, RTQ9124_MASK_CH_STATE,
				      chan_state);

	return 0;
}

static const struct snd_soc_dapm_widget rtq9124_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_OUT_DRV_E("Amp Drv", SND_SOC_NOPM, 0, 0, NULL, 0, rtq9124_enable_event,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route rtq9124_dapm_routes[] = {
	{ "Amp Drv", NULL, "HiFi Playback" },
	{ "SPK", NULL, "Amp Drv" },
};

static const DECLARE_TLV_DB_SCALE(dig_tlv, -10375, 25, 0);
static const DECLARE_TLV_DB_RANGE(ana_tlv,
				  0, 3, TLV_DB_SCALE_ITEM(-600, 600, 0),
				  4, 6, TLV_DB_SCALE_ITEM(1400, 200, 0));
static const char * const i2sch_text[] = { "(L+R)/2", "LCH", "RCH", "(L+R)/2" };
static const struct soc_enum rtq9124_i2sch_select_enum =
	SOC_ENUM_SINGLE(RTQ9124_REG_SDI_SEL, 0, ARRAY_SIZE(i2sch_text), i2sch_text);
static const char * const sdo_vsel_text[] = { "1.8V", "3.3V" };
static const struct soc_enum rtq9124_sdo_vselect_enum =
	SOC_ENUM_SINGLE(RTQ9124_REG_SDO_SEL, 8, ARRAY_SIZE(sdo_vsel_text), sdo_vsel_text);
static const char * const pwmfreq_text[] = { "8*fs", "10*fs", "40*fs", "44*fs", "48*fs" };
static const struct soc_enum rtq9124_pwm_freq_enum =
	SOC_ENUM_SINGLE(RTQ9124_REG_AMP_OPT, 4, ARRAY_SIZE(pwmfreq_text), pwmfreq_text);
static const char * const out_angle_text[] = { "0", "45", "90", "135", "180", "225", "270", "315" };
static const struct soc_enum rtq9124_out_angle_enum =
	SOC_ENUM_SINGLE(RTQ9124_REG_PWM_PHASE, 0, ARRAY_SIZE(out_angle_text), out_angle_text);
static const char * const sdo_select_text[] = {
	"None", "I2S DataI", "Interface", "DSP", "DF", "ISense", "ACLoad Cos", "ACLoad Sin",
	"DCR",
};
static const struct soc_enum rtq9124_sdo_select_enum =
	SOC_ENUM_DOUBLE(RTQ9124_REG_SDO_SEL, 4, 0, ARRAY_SIZE(sdo_select_text), sdo_select_text);
static const char * const ulqm_dcvt_text[] = { "Disable", "DC", "VT", "DC+VT" };
static const struct soc_enum rtq9124_ulqm_dcvt_select_enum =
	SOC_ENUM_SINGLE(RTQ9124_REG_STATE_CTRL, 10, ARRAY_SIZE(ulqm_dcvt_text), ulqm_dcvt_text);

static const struct snd_kcontrol_new rtq9124_controls[] = {
	SOC_SINGLE_TLV("Master Volume", RTQ9124_REG_VOL_OPT, 2, 511, 1, dig_tlv),
	SOC_SINGLE_TLV("Speaker Volume", RTQ9124_REG_AMP_OPT, 0, 6, 0, ana_tlv),
	SOC_ENUM("I2S CH Select", rtq9124_i2sch_select_enum),
	SOC_ENUM("SDO VDDIO Select", rtq9124_sdo_vselect_enum),
	SOC_ENUM("PWM Frequency Select", rtq9124_pwm_freq_enum),
	SOC_ENUM("PWM Output Phase Select", rtq9124_out_angle_enum),
	SOC_ENUM("SDO Select", rtq9124_sdo_select_enum),
	SOC_ENUM("ULQM DCVT Select", rtq9124_ulqm_dcvt_select_enum),
	SOC_SINGLE("Silence Detect Enable Switch", RTQ9124_REG_SIL_CTRL, 7, 1, 0),
	SOC_SINGLE("Spread Spectrum Enable Switch", RTQ9124_REG_PWM_SS_OPT, 7, 1, 0),
};

static int rtq9124_comp_probe(struct snd_soc_component *comp)
{
	/* CS Data INV */
	snd_soc_component_write_field(comp, RTQ9124_REG_SDO_SEL, RTQ9124_MASK_CS_DATA_INV, 1);

	/* RTLD */
	snd_soc_component_write(comp, RTQ9124_REG_DCR_TH, 0x5e30);
	snd_soc_component_write(comp, RTQ9124_REG_ERR_TH, 0x3ff);
	snd_soc_component_write(comp, RTQ9124_REG_PROT_EN, 0x3fc);
	snd_soc_component_write(comp, RTQ9124_REG_ERR_MASK6, 0);

	return 0;
}

static const struct snd_soc_component_driver rtq9124_comp_driver = {
	.probe			= rtq9124_comp_probe,
	.controls		= rtq9124_controls,
	.num_controls		= ARRAY_SIZE(rtq9124_controls),
	.dapm_widgets		= rtq9124_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rtq9124_dapm_widgets),
	.dapm_routes		= rtq9124_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rtq9124_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int rtq9124_dai_set_format(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct rtq9124_priv *rtq9124 = snd_soc_dai_get_drvdata(dai);

	rtq9124->dai_fmt = fmt;
	return 0;
}

static int rtq9124_dai_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				    unsigned int rx_mask, int slots, int slot_width)
{
	struct rtq9124_priv *rtq9124 = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_component *comp = dai->component;
	struct device *dev = dai->dev;
	unsigned int byte_loc, i;

	dev_dbg(dev, "(slots, slot_width) = (%d, %d), (txmask, rxmask) = 0x%x, 0x%x\n", slots,
		slot_width, tx_mask, rx_mask);

	if (slots <= 0 || slots > 16 || slot_width <= 0 || slots % 2 || slot_width % 8) {
		dev_err(dev, "Invalid slot parameter (%d, %d)\n", slots, slot_width);
		return -EINVAL;
	}

	if (tx_mask && (hweight_long(tx_mask) > 2 || fls(tx_mask) > slots)) {
		dev_err(dev, "Invalid tx_mask 0x%08x, slots = %d\n", tx_mask, slots);
		return -EINVAL;
	}

	if (!rx_mask || hweight_long(rx_mask) > 1 || fls(rx_mask) > slots) {
		dev_err(dev, "Invalid rx_mask 0x%08x, slots = %d\n", rx_mask, slots);
		return -EINVAL;
	}

	/* Configure tx channel data location */
	for (i = 0; tx_mask; i++, tx_mask ^= BIT(ffs(tx_mask) - 1)) {
		byte_loc = (ffs(tx_mask) - 1) * slot_width / 8;
		snd_soc_component_write(comp, RTQ9124_REG_TDM_TX_CH0 + i, byte_loc);
	}

	/* Configure rx channel data location */
	byte_loc = (ffs(rx_mask) - 1) * slot_width / 8;
	snd_soc_component_write(comp, RTQ9124_REG_TDM_RX_CH0, byte_loc);

	rtq9124->tdm_slots = slots;
	rtq9124->tdm_slot_width = slot_width;

	return 0;
}

static int rtq9124_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *param, struct snd_soc_dai *dai)
{
	struct rtq9124_priv *rtq9124 = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_component *comp = dai->component;
	unsigned int fmtval, width, slot_width, bitrate;
	struct device *dev = dai->dev;
	unsigned int audfmt, audbit;

	fmtval = FIELD_GET(SND_SOC_DAIFMT_FORMAT_MASK, rtq9124->dai_fmt);
	if (rtq9124->tdm_slots && fmtval != SND_SOC_DAIFMT_DSP_A &&
	    fmtval != SND_SOC_DAIFMT_DSP_B) {
		dev_err(dev, "TDM only can support DSP_A or DSP_B format\n");
		return -EINVAL;
	}

	switch (fmtval) {
	case SND_SOC_DAIFMT_I2S:
		audfmt = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		audfmt = 1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		audfmt = 2;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		audfmt = rtq9124->tdm_slots ? 7 : 3;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		audfmt = rtq9124->tdm_slots ? 15 : 11;
		break;
	default:
		dev_err(dev, "Unsupported format %d\n", fmtval);
		return -EINVAL;
	}

	switch (width = params_width(param)) {
	case 16:
		audbit = 0;
		break;
	case 20:
		audbit = 1;
		break;
	case 24:
	case 32:
		audbit = 3;
		break;
	default:
		dev_err(dev, "Unsupported width %d\n", width);
		return -EINVAL;
	}

	if (rtq9124->tdm_slots) {
		slot_width = params_physical_width(param);
		if (slot_width > rtq9124->tdm_slot_width) {
			dev_err(dev, "Slot width is larger than TDM slot width\n");
			return -EINVAL;
		}

		bitrate = rtq9124->tdm_slots * rtq9124->tdm_slot_width * params_rate(param);
		if (bitrate > 24576000) {
			dev_err(dev, "Bitrate exceed the internal PLL 24.576MHz (%d)\n", bitrate);
			return -EINVAL;
		}
	}

	snd_soc_component_write_field(comp, RTQ9124_REG_I2S_OPT, RTQ9124_MASK_AUD_FMT, audfmt);
	snd_soc_component_write_field(comp, RTQ9124_REG_I2S_OPT, RTQ9124_MASK_AUD_BITS, audbit);

	return 0;
}

static const struct snd_soc_dai_ops rtq9124_dai_ops = {
	.set_fmt	= rtq9124_dai_set_format,
	.set_tdm_slot	= rtq9124_dai_set_tdm_slot,
	.hw_params	= rtq9124_dai_hw_params,
};

static struct snd_soc_dai_driver rtq9124_dai_driver = {
	.name = "HiFi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.formats	= SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S24 |
				  SNDRV_PCM_FMTBIT_S32,
		.rates		= SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_24000 |
				  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
				  SNDRV_PCM_RATE_192000,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops = &rtq9124_dai_ops,
	.symmetric_rate = 1,
	.symmetric_sample_bits = 1,
};

static bool rtq9124_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00 ... 0x17:
	case 0x20 ... 0x27:
	case 0x30 ... 0x3D:
	case 0x40 ... 0x68:
	case 0x80 ... 0xBC:
	case 0xC0 ... 0xDE:
	case 0xE0 ... 0xE7:
	case 0xF0 ... 0xFD:
		return true;
	default:
		return false;
	}
}

static bool rtq9124_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00 ... 0x09:
	case 0x0C ... 0x0E:
	case 0x10 ... 0x17:
	case 0x20 ... 0x27:
	case 0x30:
	case 0x32 ... 0x3D:
	case 0x40 ... 0x4E:
	case 0x50 ... 0x68:
	case 0x80 ... 0xBC:
	case 0xC0 ... 0xDE:
	case 0xE0 ... 0xE7:
	case 0xF0 ... 0xFD:
		return true;
	default:
		return false;
	}
}

static bool rtq9124_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0A ... 0x0B:
	case 0x0F ... 0x17:
	case 0x31:
	case 0x4F:
	case 0x51:
	case 0x53 ... 0x57:
	case 0x80 ... 0xBC:
	case 0xC0 ... 0xDE:
	case 0xE0 ... 0xE7:
	case 0xF0 ... 0xFD:
		return true;
	default:
		return false;
	}
}

static inline u8 rtq9124_get_reg_len(unsigned int reg)
{
	return (reg >= 0x40 && reg <= 0x47) ? 4 : 2;
}

static int rtq9124_regmap_read(void *context, const void *reg_buf, size_t reg_size, void *val_buf,
			       size_t val_size)
{
	struct i2c_client *i2c = context;
	u8 reg = *(u8 *)reg_buf;
	u8 size = rtq9124_get_reg_len(reg);
	u32 *val = val_buf;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(i2c, reg, size, val_buf);
	if (ret < 0)
		return ret;
	else if (ret != size)
		return -EIO;

	*val = size == 4 ? be32_to_cpup(val_buf) : be16_to_cpup(val_buf);

	return 0;
}

static int rtq9124_regmap_write(void *context, const void *data, size_t count)
{
	struct i2c_client *i2c = context;
	u8 reg = *(u8 *)data, *vbuf;
	u8 size = rtq9124_get_reg_len(reg);
	__be16 val16 = cpu_to_be16p(data + 1);
	__be32 val32 = cpu_to_be32p(data + 1);

	vbuf = size == 4 ? (u8 *)&val32 : (u8 *)&val16;
	return i2c_smbus_write_i2c_block_data(i2c, reg, size, vbuf);
}

static const struct regmap_config rtq9124_regmap_config = {
	.name			= "rtq9124",
	.reg_bits		= 8,
	.val_bits		= 32,
	.read			= rtq9124_regmap_read,
	.write			= rtq9124_regmap_write,
	.readable_reg		= rtq9124_readable_reg,
	.writeable_reg		= rtq9124_writeable_reg,
	.volatile_reg		= rtq9124_volatile_reg,
	.cache_type		= REGCACHE_MAPLE,
	.num_reg_defaults_raw	= 0xFD + 1,
	.use_single_read	= 1,
	.use_single_write	= 1,
};

static const struct reg_sequence rtq9124_init_regs[] = {
	{ 0xfb, 0x0065 },
	{ 0x93, 0x2000 },
	{ 0xfb, 0x0000 },
};

static int rtq9124_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct rtq9124_priv *rtq9124;
	struct regmap *regmap;
	int ret;

	rtq9124 = devm_kzalloc(dev, sizeof(*rtq9124), GFP_KERNEL);
	if (!rtq9124)
		return -ENOMEM;

	rtq9124->enable = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(rtq9124->enable))
		return PTR_ERR(rtq9124->enable);
	else if (rtq9124->enable)
		usleep_range(6000, 7000);
	else
		dev_dbg(dev, "No 'enable' GPIO specified, treat it as default on\n");

	/* Check vendor id information */
	ret = i2c_smbus_read_word_swapped(i2c, RTQ9124_REG_PRJ_CODE);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read project code\n");
	else if (ret != RTQ9124_FIXED_VENID)
		return dev_err_probe(dev, -ENODEV, "Incorrect project-code 0x%04x\n", ret);

	/* Trigger RG reset before regmap init */
	ret = i2c_smbus_write_word_swapped(i2c, RTQ9124_REG_STATE_CTRL, RTQ9124_MASK_SF_RESET);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to trigger RG reset\n");

	/* Need to wait 10ms for the reset to complete */
	usleep_range(10000, 11000);

	regmap = devm_regmap_init(dev, NULL, i2c, &rtq9124_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to init regmap\n");

	ret = regmap_register_patch(regmap, rtq9124_init_regs, ARRAY_SIZE(rtq9124_init_regs));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register regmap patch\n");

	i2c_set_clientdata(i2c, rtq9124);

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable pm runtime\n");

	return devm_snd_soc_register_component(dev, &rtq9124_comp_driver, &rtq9124_dai_driver, 1);
}

#ifdef CONFIG_PM
static int rtq9124_runtime_suspend(struct device *dev)
{
	struct rtq9124_priv *rtq9124 = dev_get_drvdata(dev);
	struct regmap *regmap = dev_get_regmap(dev, NULL);

	if (rtq9124->enable) {
		regcache_cache_only(regmap, true);
		regcache_mark_dirty(regmap);
		gpiod_set_value(rtq9124->enable, 0);
	}

	return 0;
}

static int rtq9124_runtime_resume(struct device *dev)
{
	struct rtq9124_priv *rtq9124 = dev_get_drvdata(dev);
	struct regmap *regmap = dev_get_regmap(dev, NULL);
	int ret;

	if (rtq9124->enable) {
		gpiod_set_value(rtq9124->enable, 1);
		usleep_range(6000, 7000);

		regcache_cache_only(regmap, false);
		ret = regcache_sync(regmap);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops rtq9124_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(rtq9124_runtime_suspend, rtq9124_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id rtq9124_device_id[] = {
	{ .compatible = "richtek,rtq9124" },
	{}
};
MODULE_DEVICE_TABLE(of, rtq9124_device_id);
#endif

static struct i2c_driver rtq9124_driver = {
	.driver = {
		.name = "rtq9124",
		.of_match_table = of_match_ptr(rtq9124_device_id),
		.pm = pm_ptr(&rtq9124_dev_pm_ops),
	},
	.probe	= rtq9124_probe,
};
module_i2c_driver(rtq9124_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("ASoC RTQ9124 Driver");
MODULE_LICENSE("GPL");
