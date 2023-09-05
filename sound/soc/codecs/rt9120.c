// SPDX-License-Identifier: GPL-2.0

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define RT9120_REG_DEVID	0x00
#define RT9120_REG_I2SFMT	0x02
#define RT9120_REG_I2SWL	0x03
#define RT9120_REG_SDIOSEL	0x04
#define RT9120_REG_SYSCTL	0x05
#define RT9120_REG_SPKGAIN	0x07
#define RT9120_REG_VOLRAMP	0x0A
#define RT9120_REG_ERRRPT	0x10
#define RT9120_REG_MSVOL	0x20
#define RT9120_REG_SWRESET	0x40
#define RT9120_REG_INTERCFG	0x63
#define RT9120_REG_INTERNAL0	0x65
#define RT9120_REG_INTERNAL1	0x69
#define RT9120_REG_UVPOPT	0x6C
#define RT9120_REG_DIGCFG	0xF8

#define RT9120_VID_MASK		GENMASK(15, 8)
#define RT9120_SWRST_MASK	BIT(7)
#define RT9120_MUTE_MASK	GENMASK(5, 4)
#define RT9120_I2SFMT_MASK	GENMASK(4, 2)
#define RT9120_I2SFMT_SHIFT	2
#define RT9120_CFG_FMT_I2S	0
#define RT9120_CFG_FMT_LEFTJ	1
#define RT9120_CFG_FMT_RIGHTJ	2
#define RT9120_CFG_FMT_DSPA	3
#define RT9120_CFG_FMT_DSPB	7
#define RT9120_AUDBIT_MASK	GENMASK(1, 0)
#define RT9120_CFG_AUDBIT_16	0
#define RT9120_CFG_AUDBIT_20	1
#define RT9120_CFG_AUDBIT_24	2
#define RT9120_AUDWL_MASK	GENMASK(5, 0)
#define RT9120_CFG_WORDLEN_16	16
#define RT9120_CFG_WORDLEN_24	24
#define RT9120_CFG_WORDLEN_32	32
#define RT9120_DVDD_UVSEL_MASK	GENMASK(5, 4)
#define RT9120_AUTOSYNC_MASK	BIT(6)

#define RT9120_VENDOR_ID	0x42
#define RT9120S_VENDOR_ID	0x43
#define RT9120_RESET_WAITMS	20
#define RT9120_CHIPON_WAITMS	20
#define RT9120_AMPON_WAITMS	50
#define RT9120_AMPOFF_WAITMS	100
#define RT9120_LVAPP_THRESUV	2000000

/* 8000 to 192000 supported , only 176400 not support */
#define RT9120_RATES_MASK	(SNDRV_PCM_RATE_8000_192000 &\
				 ~SNDRV_PCM_RATE_176400)
#define RT9120_FMTS_MASK	(SNDRV_PCM_FMTBIT_S16_LE |\
				 SNDRV_PCM_FMTBIT_S24_LE |\
				 SNDRV_PCM_FMTBIT_S32_LE)

enum {
	CHIP_IDX_RT9120 = 0,
	CHIP_IDX_RT9120S,
	CHIP_IDX_MAX
};

struct rt9120_data {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *pwdnn_gpio;
	int chip_idx;
};

/* 11bit [min,max,step] = [-103.9375dB, 24dB, 0.0625dB] */
static const DECLARE_TLV_DB_SCALE(digital_tlv, -1039375, 625, 1);

/* {6, 8, 10, 12, 13, 14, 15, 16}dB */
static const DECLARE_TLV_DB_RANGE(classd_tlv,
	0, 3, TLV_DB_SCALE_ITEM(600, 200, 0),
	4, 7, TLV_DB_SCALE_ITEM(1300, 100, 0)
);

static const char * const sdo_select_text[] = {
	"None", "INTF", "Final", "RMS Detect"
};

static const struct soc_enum sdo_select_enum =
	SOC_ENUM_SINGLE(RT9120_REG_SDIOSEL, 4, ARRAY_SIZE(sdo_select_text),
			sdo_select_text);

static const struct snd_kcontrol_new rt9120_snd_controls[] = {
	SOC_SINGLE_TLV("MS Volume", RT9120_REG_MSVOL, 0, 2047, 1, digital_tlv),
	SOC_SINGLE_TLV("SPK Gain Volume", RT9120_REG_SPKGAIN, 0, 7, 0, classd_tlv),
	SOC_SINGLE("PBTL Switch", RT9120_REG_SYSCTL, 3, 1, 0),
	SOC_ENUM("SDO Select", sdo_select_enum),
};

static int internal_power_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write(comp, RT9120_REG_ERRRPT, 0);
		break;
	case SND_SOC_DAPM_POST_PMU:
		msleep(RT9120_AMPON_WAITMS);
		break;
	case SND_SOC_DAPM_POST_PMD:
		msleep(RT9120_AMPOFF_WAITMS);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt9120_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("DMIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_DAC("LDAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("RDAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("PWND", RT9120_REG_SYSCTL, 6, 1,
			    internal_power_event, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA("SPKL PA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPKR PA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("SPKL"),
	SND_SOC_DAPM_OUTPUT("SPKR"),
};

static const struct snd_soc_dapm_route rt9120_dapm_routes[] = {
	{ "DMIX", NULL, "AIF Playback" },
	/* SPKL */
	{ "LDAC", NULL, "PWND" },
	{ "LDAC", NULL, "DMIX" },
	{ "SPKL PA", NULL, "LDAC" },
	{ "SPKL", NULL, "SPKL PA" },
	/* SPKR */
	{ "RDAC", NULL, "PWND" },
	{ "RDAC", NULL, "DMIX" },
	{ "SPKR PA", NULL, "RDAC" },
	{ "SPKR", NULL, "SPKR PA" },
	/* Cap */
	{ "AIF Capture", NULL, "LDAC" },
	{ "AIF Capture", NULL, "RDAC" },
};

static int rt9120_codec_probe(struct snd_soc_component *comp)
{
	struct rt9120_data *data = snd_soc_component_get_drvdata(comp);

	snd_soc_component_init_regmap(comp, data->regmap);

	pm_runtime_get_sync(comp->dev);

	/* Internal setting */
	if (data->chip_idx == CHIP_IDX_RT9120S) {
		snd_soc_component_write(comp, RT9120_REG_INTERCFG, 0xde);
		snd_soc_component_write(comp, RT9120_REG_INTERNAL0, 0x66);
	} else
		snd_soc_component_write(comp, RT9120_REG_INTERNAL0, 0x04);

	pm_runtime_mark_last_busy(comp->dev);
	pm_runtime_put(comp->dev);

	return 0;
}

static int rt9120_codec_suspend(struct snd_soc_component *comp)
{
	return pm_runtime_force_suspend(comp->dev);
}

static int rt9120_codec_resume(struct snd_soc_component *comp)
{
	return pm_runtime_force_resume(comp->dev);
}

static const struct snd_soc_component_driver rt9120_component_driver = {
	.probe = rt9120_codec_probe,
	.suspend = rt9120_codec_suspend,
	.resume = rt9120_codec_resume,
	.controls = rt9120_snd_controls,
	.num_controls = ARRAY_SIZE(rt9120_snd_controls),
	.dapm_widgets = rt9120_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt9120_dapm_widgets),
	.dapm_routes = rt9120_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt9120_dapm_routes),
	.endianness = 1,
};

static int rt9120_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *comp = dai->component;
	unsigned int format;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = RT9120_CFG_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = RT9120_CFG_FMT_LEFTJ;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format = RT9120_CFG_FMT_RIGHTJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format = RT9120_CFG_FMT_DSPA;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format = RT9120_CFG_FMT_DSPB;
		break;
	default:
		dev_err(dai->dev, "Unknown dai format\n");
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, RT9120_REG_I2SFMT,
				      RT9120_I2SFMT_MASK,
				      format << RT9120_I2SFMT_SHIFT);
	return 0;
}

static int rt9120_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *param,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	unsigned int param_width, param_slot_width, auto_sync;
	int width, fs;

	switch (width = params_width(param)) {
	case 16:
		param_width = RT9120_CFG_AUDBIT_16;
		break;
	case 20:
		param_width = RT9120_CFG_AUDBIT_20;
		break;
	case 24:
	case 32:
		param_width = RT9120_CFG_AUDBIT_24;
		break;
	default:
		dev_err(dai->dev, "Unsupported data width [%d]\n", width);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, RT9120_REG_I2SFMT,
				      RT9120_AUDBIT_MASK, param_width);

	switch (width = params_physical_width(param)) {
	case 16:
		param_slot_width = RT9120_CFG_WORDLEN_16;
		break;
	case 24:
		param_slot_width = RT9120_CFG_WORDLEN_24;
		break;
	case 32:
		param_slot_width = RT9120_CFG_WORDLEN_32;
		break;
	default:
		dev_err(dai->dev, "Unsupported slot width [%d]\n", width);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, RT9120_REG_I2SWL,
				      RT9120_AUDWL_MASK, param_slot_width);

	fs = width * params_channels(param);
	/* If fs is divided by 48, disable auto sync */
	if (fs % 48 == 0)
		auto_sync = 0;
	else
		auto_sync = RT9120_AUTOSYNC_MASK;

	snd_soc_component_update_bits(comp, RT9120_REG_DIGCFG,
				      RT9120_AUTOSYNC_MASK, auto_sync);
	return 0;
}

static const struct snd_soc_dai_ops rt9120_dai_ops = {
	.set_fmt = rt9120_set_fmt,
	.hw_params = rt9120_hw_params,
};

static struct snd_soc_dai_driver rt9120_dai = {
	.name = "rt9120_aif",
	.playback = {
		.stream_name = "AIF Playback",
		.rates = RT9120_RATES_MASK,
		.formats = RT9120_FMTS_MASK,
		.rate_max = 192000,
		.rate_min = 8000,
		.channels_min = 1,
		.channels_max = 2,
	},
	.capture = {
		.stream_name = "AIF Capture",
		.rates = RT9120_RATES_MASK,
		.formats = RT9120_FMTS_MASK,
		.rate_max = 192000,
		.rate_min = 8000,
		.channels_min = 1,
		.channels_max = 2,
	},
	.ops = &rt9120_dai_ops,
	.symmetric_rate = 1,
	.symmetric_sample_bits = 1,
};

static const struct regmap_range rt9120_rd_yes_ranges[] = {
	regmap_reg_range(0x00, 0x0C),
	regmap_reg_range(0x10, 0x15),
	regmap_reg_range(0x20, 0x27),
	regmap_reg_range(0x30, 0x38),
	regmap_reg_range(0x3A, 0x40),
	regmap_reg_range(0x63, 0x63),
	regmap_reg_range(0x65, 0x65),
	regmap_reg_range(0x69, 0x69),
	regmap_reg_range(0x6C, 0x6C),
	regmap_reg_range(0xF8, 0xF8)
};

static const struct regmap_access_table rt9120_rd_table = {
	.yes_ranges = rt9120_rd_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(rt9120_rd_yes_ranges),
};

static const struct regmap_range rt9120_wr_yes_ranges[] = {
	regmap_reg_range(0x00, 0x00),
	regmap_reg_range(0x02, 0x0A),
	regmap_reg_range(0x10, 0x15),
	regmap_reg_range(0x20, 0x27),
	regmap_reg_range(0x30, 0x38),
	regmap_reg_range(0x3A, 0x3D),
	regmap_reg_range(0x40, 0x40),
	regmap_reg_range(0x63, 0x63),
	regmap_reg_range(0x65, 0x65),
	regmap_reg_range(0x69, 0x69),
	regmap_reg_range(0x6C, 0x6C),
	regmap_reg_range(0xF8, 0xF8)
};

static const struct regmap_access_table rt9120_wr_table = {
	.yes_ranges = rt9120_wr_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(rt9120_wr_yes_ranges),
};

static bool rt9120_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00 ... 0x01:
	case 0x10:
	case 0x30 ... 0x40:
		return true;
	default:
		return false;
	}
}

static int rt9120_get_reg_size(unsigned int reg)
{
	switch (reg) {
	case 0x00:
	case 0x20 ... 0x27:
		return 2;
	case 0x30 ... 0x3D:
		return 3;
	case 0x3E ... 0x3F:
		return 4;
	default:
		return 1;
	}
}

static int rt9120_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct rt9120_data *data = context;
	struct i2c_client *i2c = to_i2c_client(data->dev);
	int size = rt9120_get_reg_size(reg);
	u8 raw[4] = {0};
	int ret;

	ret = i2c_smbus_read_i2c_block_data(i2c, reg, size, raw);
	if (ret < 0)
		return ret;
	else if (ret != size)
		return -EIO;

	switch (size) {
	case 4:
		*val = be32_to_cpup((__be32 *)raw);
		break;
	case 3:
		*val = raw[0] << 16 | raw[1] << 8 | raw[2];
		break;
	case 2:
		*val = be16_to_cpup((__be16 *)raw);
		break;
	default:
		*val = raw[0];
	}

	return 0;
}

static int rt9120_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct rt9120_data *data = context;
	struct i2c_client *i2c = to_i2c_client(data->dev);
	int size = rt9120_get_reg_size(reg);
	__be32 be32_val;
	u8 *rawp = (u8 *)&be32_val;
	int offs = 4 - size;

	be32_val = cpu_to_be32(val);
	return i2c_smbus_write_i2c_block_data(i2c, reg, size, rawp + offs);
}

static const struct reg_default rt9120_reg_defaults[] = {
	{ .reg = 0x02, .def = 0x02 },
	{ .reg = 0x03, .def = 0xf2 },
	{ .reg = 0x04, .def = 0x01 },
	{ .reg = 0x05, .def = 0xc0 },
	{ .reg = 0x06, .def = 0x28 },
	{ .reg = 0x07, .def = 0x04 },
	{ .reg = 0x08, .def = 0xff },
	{ .reg = 0x09, .def = 0x01 },
	{ .reg = 0x0a, .def = 0x01 },
	{ .reg = 0x0b, .def = 0x00 },
	{ .reg = 0x0c, .def = 0x04 },
	{ .reg = 0x11, .def = 0x30 },
	{ .reg = 0x12, .def = 0x08 },
	{ .reg = 0x13, .def = 0x12 },
	{ .reg = 0x14, .def = 0x09 },
	{ .reg = 0x15, .def = 0x00 },
	{ .reg = 0x20, .def = 0x7ff },
	{ .reg = 0x21, .def = 0x180 },
	{ .reg = 0x22, .def = 0x180 },
	{ .reg = 0x23, .def = 0x00 },
	{ .reg = 0x24, .def = 0x80 },
	{ .reg = 0x25, .def = 0x180 },
	{ .reg = 0x26, .def = 0x640 },
	{ .reg = 0x27, .def = 0x180 },
	{ .reg = 0x63, .def = 0x5e },
	{ .reg = 0x65, .def = 0x66 },
	{ .reg = 0x6c, .def = 0xe0 },
	{ .reg = 0xf8, .def = 0x44 },
};

static const struct regmap_config rt9120_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.max_register = RT9120_REG_DIGCFG,
	.reg_defaults = rt9120_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt9120_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.reg_read = rt9120_reg_read,
	.reg_write = rt9120_reg_write,

	.volatile_reg = rt9120_volatile_reg,
	.wr_table = &rt9120_wr_table,
	.rd_table = &rt9120_rd_table,
};

static int rt9120_check_vendor_info(struct rt9120_data *data)
{
	unsigned int devid;
	int ret;

	ret = regmap_read(data->regmap, RT9120_REG_DEVID, &devid);
	if (ret)
		return ret;

	devid = FIELD_GET(RT9120_VID_MASK, devid);
	switch (devid) {
	case RT9120_VENDOR_ID:
		data->chip_idx = CHIP_IDX_RT9120;
		break;
	case RT9120S_VENDOR_ID:
		data->chip_idx = CHIP_IDX_RT9120S;
		break;
	default:
		dev_err(data->dev, "DEVID not correct [0x%0x]\n", devid);
		return -ENODEV;
	}

	return 0;
}

static int rt9120_do_register_reset(struct rt9120_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, RT9120_REG_SWRESET,
			   RT9120_SWRST_MASK);
	if (ret)
		return ret;

	msleep(RT9120_RESET_WAITMS);
	return 0;
}

static int rt9120_probe(struct i2c_client *i2c)
{
	struct rt9120_data *data;
	struct regulator *dvdd_supply;
	int dvdd_supply_volt, ret;

	data = devm_kzalloc(&i2c->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &i2c->dev;
	i2c_set_clientdata(i2c, data);

	data->pwdnn_gpio = devm_gpiod_get_optional(&i2c->dev, "pwdnn",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(data->pwdnn_gpio)) {
		dev_err(&i2c->dev, "Failed to initialize 'pwdnn' gpio\n");
		return PTR_ERR(data->pwdnn_gpio);
	} else if (data->pwdnn_gpio) {
		dev_dbg(&i2c->dev, "'pwdnn' from low to high, wait chip on\n");
		msleep(RT9120_CHIPON_WAITMS);
	}

	data->regmap = devm_regmap_init(&i2c->dev, NULL, data,
					&rt9120_regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err(&i2c->dev, "Failed to init regmap [%d]\n", ret);
		return ret;
	}

	ret = rt9120_check_vendor_info(data);
	if (ret) {
		dev_err(&i2c->dev, "Failed to check vendor info\n");
		return ret;
	}

	ret = rt9120_do_register_reset(data);
	if (ret) {
		dev_err(&i2c->dev, "Failed to do register reset\n");
		return ret;
	}

	dvdd_supply = devm_regulator_get(&i2c->dev, "dvdd");
	if (IS_ERR(dvdd_supply)) {
		dev_err(&i2c->dev, "No dvdd regulator found\n");
		return PTR_ERR(dvdd_supply);
	}

	dvdd_supply_volt = regulator_get_voltage(dvdd_supply);
	if (dvdd_supply_volt <= RT9120_LVAPP_THRESUV) {
		dev_dbg(&i2c->dev, "dvdd low voltage design\n");
		ret = regmap_update_bits(data->regmap, RT9120_REG_UVPOPT,
					 RT9120_DVDD_UVSEL_MASK, 0);
		if (ret) {
			dev_err(&i2c->dev, "Failed to config dvdd uvsel\n");
			return ret;
		}
	}

	pm_runtime_set_autosuspend_delay(&i2c->dev, 1000);
	pm_runtime_use_autosuspend(&i2c->dev);
	pm_runtime_set_active(&i2c->dev);
	pm_runtime_mark_last_busy(&i2c->dev);
	pm_runtime_enable(&i2c->dev);

	return devm_snd_soc_register_component(&i2c->dev,
					       &rt9120_component_driver,
					       &rt9120_dai, 1);
}

static void rt9120_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);
	pm_runtime_set_suspended(&i2c->dev);
}

static int __maybe_unused rt9120_runtime_suspend(struct device *dev)
{
	struct rt9120_data *data = dev_get_drvdata(dev);

	if (data->pwdnn_gpio) {
		regcache_cache_only(data->regmap, true);
		regcache_mark_dirty(data->regmap);
		gpiod_set_value(data->pwdnn_gpio, 0);
	}

	return 0;
}

static int __maybe_unused rt9120_runtime_resume(struct device *dev)
{
	struct rt9120_data *data = dev_get_drvdata(dev);

	if (data->pwdnn_gpio) {
		gpiod_set_value(data->pwdnn_gpio, 1);
		msleep(RT9120_CHIPON_WAITMS);
		regcache_cache_only(data->regmap, false);
		regcache_sync(data->regmap);
	}

	return 0;
}

static const struct dev_pm_ops rt9120_pm_ops = {
	SET_RUNTIME_PM_OPS(rt9120_runtime_suspend, rt9120_runtime_resume, NULL)
};

static const struct of_device_id __maybe_unused rt9120_device_table[] = {
	{ .compatible = "richtek,rt9120", },
	{ }
};
MODULE_DEVICE_TABLE(of, rt9120_device_table);

static struct i2c_driver rt9120_driver = {
	.driver = {
		.name = "rt9120",
		.of_match_table = rt9120_device_table,
		.pm = &rt9120_pm_ops,
	},
	.probe = rt9120_probe,
	.remove = rt9120_remove,
};
module_i2c_driver(rt9120_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("RT9120 Audio Amplifier Driver");
MODULE_LICENSE("GPL");
