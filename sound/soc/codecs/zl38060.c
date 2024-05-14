// SPDX-License-Identifier: GPL-2.0-only
//
// Codec driver for Microsemi ZL38060 Connected Home Audio Processor.
//
// Copyright(c) 2020 Sven Van Asbroeck

// The ZL38060 is very flexible and configurable. This driver implements only a
// tiny subset of the chip's possible configurations:
//
// - DSP block bypassed: DAI        routed straight to DACs
//                       microphone routed straight to DAI
// - chip's internal clock is driven by a 12 MHz external crystal
// - chip's DAI connected to CPU is I2S, and bit + frame clock master
// - chip must be strapped for "host boot": in this mode, firmware will be
//   provided by this driver.

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/ihex.h>

#include <sound/pcm_params.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define DRV_NAME		"zl38060"

#define ZL38_RATES		(SNDRV_PCM_RATE_8000  |\
				SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_48000)
#define ZL38_FORMATS		SNDRV_PCM_FMTBIT_S16_LE

#define HBI_FIRMWARE_PAGE	0xFF
#define ZL38_MAX_RAW_XFER	0x100

#define REG_TDMA_CFG_CLK	0x0262
#define CFG_CLK_PCLK_SHIFT	4
#define CFG_CLK_PCLK_MASK	(0x7ff << CFG_CLK_PCLK_SHIFT)
#define CFG_CLK_PCLK(bits)	((bits - 1) << CFG_CLK_PCLK_SHIFT)
#define CFG_CLK_MASTER		BIT(15)
#define CFG_CLK_FSRATE_MASK	0x7
#define CFG_CLK_FSRATE_8KHZ	0x1
#define CFG_CLK_FSRATE_16KHZ	0x2
#define CFG_CLK_FSRATE_48KHZ	0x6

#define REG_CLK_CFG		0x0016
#define CLK_CFG_SOURCE_XTAL	BIT(15)

#define REG_CLK_STATUS		0x0014
#define CLK_STATUS_HWRST	BIT(0)

#define REG_PARAM_RESULT	0x0034
#define PARAM_RESULT_READY	0xD3D3

#define REG_PG255_BASE_HI	0x000C
#define REG_PG255_OFFS(addr)	((HBI_FIRMWARE_PAGE << 8) | (addr & 0xFF))
#define REG_FWR_EXEC		0x012C

#define REG_CMD			0x0032
#define REG_HW_REV		0x0020
#define REG_FW_PROD		0x0022
#define REG_FW_REV		0x0024

#define REG_SEMA_FLAGS		0x0006
#define SEMA_FLAGS_BOOT_CMD	BIT(0)
#define SEMA_FLAGS_APP_REBOOT	BIT(1)

#define REG_HW_REV		0x0020
#define REG_FW_PROD		0x0022
#define REG_FW_REV		0x0024
#define REG_GPIO_DIR		0x02DC
#define REG_GPIO_DAT		0x02DA

#define BOOTCMD_LOAD_COMPLETE	0x000D
#define BOOTCMD_FW_GO		0x0008

#define FIRMWARE_MAJOR		2
#define FIRMWARE_MINOR		2

struct zl38_codec_priv {
	struct device *dev;
	struct regmap *regmap;
	bool is_stream_in_use[2];
	struct gpio_chip *gpio_chip;
};

static int zl38_fw_issue_command(struct regmap *regmap, u16 cmd)
{
	unsigned int val;
	int err;

	err = regmap_read_poll_timeout(regmap, REG_SEMA_FLAGS, val,
				       !(val & SEMA_FLAGS_BOOT_CMD), 10000,
				       10000 * 100);
	if (err)
		return err;
	err = regmap_write(regmap, REG_CMD, cmd);
	if (err)
		return err;
	err = regmap_update_bits(regmap, REG_SEMA_FLAGS, SEMA_FLAGS_BOOT_CMD,
				 SEMA_FLAGS_BOOT_CMD);
	if (err)
		return err;

	return regmap_read_poll_timeout(regmap, REG_CMD, val, !val, 10000,
					10000 * 100);
}

static int zl38_fw_go(struct regmap *regmap)
{
	int err;

	err = zl38_fw_issue_command(regmap, BOOTCMD_LOAD_COMPLETE);
	if (err)
		return err;

	return zl38_fw_issue_command(regmap, BOOTCMD_FW_GO);
}

static int zl38_fw_enter_boot_mode(struct regmap *regmap)
{
	unsigned int val;
	int err;

	err = regmap_update_bits(regmap, REG_CLK_STATUS, CLK_STATUS_HWRST,
				 CLK_STATUS_HWRST);
	if (err)
		return err;

	return regmap_read_poll_timeout(regmap, REG_PARAM_RESULT, val,
					val == PARAM_RESULT_READY, 1000, 50000);
}

static int
zl38_fw_send_data(struct regmap *regmap, u32 addr, const void *data, u16 len)
{
	__be32 addr_base = cpu_to_be32(addr & ~0xFF);
	int err;

	err = regmap_raw_write(regmap, REG_PG255_BASE_HI, &addr_base,
			       sizeof(addr_base));
	if (err)
		return err;
	return regmap_raw_write(regmap, REG_PG255_OFFS(addr), data, len);
}

static int zl38_fw_send_xaddr(struct regmap *regmap, const void *data)
{
	/* execution address from ihex: 32-bit little endian.
	 * device register expects 32-bit big endian.
	 */
	u32 addr = le32_to_cpup(data);
	__be32 baddr = cpu_to_be32(addr);

	return regmap_raw_write(regmap, REG_FWR_EXEC, &baddr, sizeof(baddr));
}

static int zl38_load_firmware(struct device *dev, struct regmap *regmap)
{
	const struct ihex_binrec *rec;
	const struct firmware *fw;
	u32 addr;
	u16 len;
	int err;

	/* how to get this firmware:
	 * 1. request and download chip firmware from Microsemi
	 *    (provided by Microsemi in srec format)
	 * 2. convert downloaded firmware from srec to ihex. Simple tool:
	 *    https://gitlab.com/TheSven73/s3-to-irec
	 * 3. convert ihex to binary (.fw) using ihex2fw tool which is included
	 *    with the Linux kernel sources
	 */
	err = request_ihex_firmware(&fw, "zl38060.fw", dev);
	if (err)
		return err;
	err = zl38_fw_enter_boot_mode(regmap);
	if (err)
		goto out;
	rec = (const struct ihex_binrec *)fw->data;
	while (rec) {
		addr = be32_to_cpu(rec->addr);
		len = be16_to_cpu(rec->len);
		if (addr) {
			/* regular data ihex record */
			err = zl38_fw_send_data(regmap, addr, rec->data, len);
		} else if (len == 4) {
			/* execution address ihex record */
			err = zl38_fw_send_xaddr(regmap, rec->data);
		} else {
			err = -EINVAL;
		}
		if (err)
			goto out;
		/* next ! */
		rec = ihex_next_binrec(rec);
	}
	err = zl38_fw_go(regmap);

out:
	release_firmware(fw);
	return err;
}


static int zl38_software_reset(struct regmap *regmap)
{
	unsigned int val;
	int err;

	err = regmap_update_bits(regmap, REG_SEMA_FLAGS, SEMA_FLAGS_APP_REBOOT,
				 SEMA_FLAGS_APP_REBOOT);
	if (err)
		return err;

	/* wait for host bus interface to settle.
	 * Not sure if this is required: Microsemi's vendor driver does this,
	 * but the firmware manual does not mention it. Leave it in, there's
	 * little downside, apart from a slower reset.
	 */
	msleep(50);

	return regmap_read_poll_timeout(regmap, REG_SEMA_FLAGS, val,
					!(val & SEMA_FLAGS_APP_REBOOT), 10000,
					10000 * 100);
}

static int zl38_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct zl38_codec_priv *priv = snd_soc_dai_get_drvdata(dai);
	int err;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* firmware default is normal i2s */
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/* firmware default is normal bitclock and frame */
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		/* always 32 bits per frame (= 16 bits/channel, 2 channels) */
		err = regmap_update_bits(priv->regmap, REG_TDMA_CFG_CLK,
					 CFG_CLK_MASTER | CFG_CLK_PCLK_MASK,
					 CFG_CLK_MASTER | CFG_CLK_PCLK(32));
		if (err)
			return err;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int zl38_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params,
			  struct snd_soc_dai *dai)
{
	struct zl38_codec_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int fsrate;
	int err;

	/* We cannot change hw_params while the dai is already in use - the
	 * software reset will corrupt the audio. However, this is not required,
	 * as the chip's TDM buses are fully symmetric, which mandates identical
	 * rates, channels, and samplebits for record and playback.
	 */
	if (priv->is_stream_in_use[!tx])
		goto skip_setup;

	switch (params_rate(params)) {
	case 8000:
		fsrate = CFG_CLK_FSRATE_8KHZ;
		break;
	case 16000:
		fsrate = CFG_CLK_FSRATE_16KHZ;
		break;
	case 48000:
		fsrate = CFG_CLK_FSRATE_48KHZ;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_update_bits(priv->regmap, REG_TDMA_CFG_CLK,
				 CFG_CLK_FSRATE_MASK, fsrate);
	if (err)
		return err;

	/* chip requires a software reset to apply audio register changes */
	err = zl38_software_reset(priv->regmap);
	if (err)
		return err;

skip_setup:
	priv->is_stream_in_use[tx] = true;

	return 0;
}

static int zl38_hw_free(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct zl38_codec_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	priv->is_stream_in_use[tx] = false;

	return 0;
}

/* stereo bypass with no AEC */
static const struct reg_sequence cp_config_stereo_bypass[] = {
	/* interconnects must be programmed first */
	{ 0x0210, 0x0005 },	/* DAC1   in <= I2S1-L */
	{ 0x0212, 0x0006 },	/* DAC2   in <= I2S1-R */
	{ 0x0214, 0x0001 },	/* I2S1-L in <= MIC1   */
	{ 0x0216, 0x0001 },	/* I2S1-R in <= MIC1   */
	{ 0x0224, 0x0000 },	/* AEC-S  in <= n/a    */
	{ 0x0226, 0x0000 },	/* AEC-R  in <= n/a    */
	/* output enables must be programmed next */
	{ 0x0202, 0x000F },	/* enable I2S1 + DAC   */
};

static const struct snd_soc_dai_ops zl38_dai_ops = {
	.set_fmt = zl38_set_fmt,
	.hw_params = zl38_hw_params,
	.hw_free = zl38_hw_free,
};

static struct snd_soc_dai_driver zl38_dai = {
	.name = "zl38060-tdma",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = ZL38_RATES,
		.formats = ZL38_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = ZL38_RATES,
		.formats = ZL38_FORMATS,
	},
	.ops = &zl38_dai_ops,
	.symmetric_rate = 1,
	.symmetric_sample_bits = 1,
	.symmetric_channels = 1,
};

static const struct snd_soc_dapm_widget zl38_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("DAC1"),
	SND_SOC_DAPM_OUTPUT("DAC2"),

	SND_SOC_DAPM_INPUT("DMICL"),
};

static const struct snd_soc_dapm_route zl38_dapm_routes[] = {
	{ "DAC1",  NULL, "Playback" },
	{ "DAC2",  NULL, "Playback" },

	{ "Capture",  NULL, "DMICL" },
};

static const struct snd_soc_component_driver zl38_component_dev = {
	.dapm_widgets		= zl38_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(zl38_dapm_widgets),
	.dapm_routes		= zl38_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(zl38_dapm_routes),
	.endianness		= 1,
};

static void chip_gpio_set(struct gpio_chip *c, unsigned int offset, int val)
{
	struct regmap *regmap = gpiochip_get_data(c);
	unsigned int mask = BIT(offset);

	regmap_update_bits(regmap, REG_GPIO_DAT, mask, val ? mask : 0);
}

static int chip_gpio_get(struct gpio_chip *c, unsigned int offset)
{
	struct regmap *regmap = gpiochip_get_data(c);
	unsigned int mask = BIT(offset);
	unsigned int val;
	int err;

	err = regmap_read(regmap, REG_GPIO_DAT, &val);
	if (err)
		return err;

	return !!(val & mask);
}

static int chip_direction_input(struct gpio_chip *c, unsigned int offset)
{
	struct regmap *regmap = gpiochip_get_data(c);
	unsigned int mask = BIT(offset);

	return regmap_update_bits(regmap, REG_GPIO_DIR, mask, 0);
}

static int
chip_direction_output(struct gpio_chip *c, unsigned int offset, int val)
{
	struct regmap *regmap = gpiochip_get_data(c);
	unsigned int mask = BIT(offset);

	chip_gpio_set(c, offset, val);
	return regmap_update_bits(regmap, REG_GPIO_DIR, mask, mask);
}

static const struct gpio_chip template_chip = {
	.owner = THIS_MODULE,
	.label = DRV_NAME,

	.base = -1,
	.ngpio = 14,
	.direction_input = chip_direction_input,
	.direction_output = chip_direction_output,
	.get = chip_gpio_get,
	.set = chip_gpio_set,

	.can_sleep = true,
};

static int zl38_check_revision(struct device *dev, struct regmap *regmap)
{
	unsigned int hwrev, fwprod, fwrev;
	int fw_major, fw_minor, fw_micro;
	int err;

	err = regmap_read(regmap, REG_HW_REV, &hwrev);
	if (err)
		return err;
	err = regmap_read(regmap, REG_FW_PROD, &fwprod);
	if (err)
		return err;
	err = regmap_read(regmap, REG_FW_REV, &fwrev);
	if (err)
		return err;

	fw_major = (fwrev >> 12) & 0xF;
	fw_minor = (fwrev >>  8) & 0xF;
	fw_micro = fwrev & 0xFF;
	dev_info(dev, "hw rev 0x%x, fw product code %d, firmware rev %d.%d.%d",
		 hwrev & 0x1F, fwprod, fw_major, fw_minor, fw_micro);

	if (fw_major != FIRMWARE_MAJOR || fw_minor < FIRMWARE_MINOR) {
		dev_err(dev, "unsupported firmware. driver supports %d.%d",
			FIRMWARE_MAJOR, FIRMWARE_MINOR);
		return -EINVAL;
	}

	return 0;
}

static int zl38_bus_read(void *context,
			 const void *reg_buf, size_t reg_size,
			 void *val_buf, size_t val_size)
{
	struct spi_device *spi = context;
	const u8 *reg_buf8 = reg_buf;
	size_t len = 0;
	u8 offs, page;
	u8 txbuf[4];

	if (reg_size != 2 || val_size > ZL38_MAX_RAW_XFER)
		return -EINVAL;

	offs = reg_buf8[1] >> 1;
	page = reg_buf8[0];

	if (page) {
		txbuf[len++] = 0xFE;
		txbuf[len++] = page == HBI_FIRMWARE_PAGE ? 0xFF : page - 1;
		txbuf[len++] = offs;
		txbuf[len++] = val_size / 2 - 1;
	} else {
		txbuf[len++] = offs | 0x80;
		txbuf[len++] = val_size / 2 - 1;
	}

	return spi_write_then_read(spi, txbuf, len, val_buf, val_size);
}

static int zl38_bus_write(void *context, const void *data, size_t count)
{
	struct spi_device *spi = context;
	u8 buf[4 + ZL38_MAX_RAW_XFER];
	size_t val_len, len = 0;
	const u8 *data8 = data;
	u8 offs, page;

	if (count > (2 + ZL38_MAX_RAW_XFER) || count < 4)
		return -EINVAL;
	val_len = count - 2;
	offs = data8[1] >> 1;
	page = data8[0];

	if (page) {
		buf[len++] = 0xFE;
		buf[len++] = page == HBI_FIRMWARE_PAGE ? 0xFF : page - 1;
		buf[len++] = offs;
		buf[len++] = (val_len / 2 - 1) | 0x80;
	} else {
		buf[len++] = offs | 0x80;
		buf[len++] = (val_len / 2 - 1) | 0x80;
	}
	memcpy(buf + len, data8 + 2, val_len);
	len += val_len;

	return spi_write(spi, buf, len);
}

static const struct regmap_bus zl38_regmap_bus = {
	.read = zl38_bus_read,
	.write = zl38_bus_write,
	.max_raw_write = ZL38_MAX_RAW_XFER,
	.max_raw_read = ZL38_MAX_RAW_XFER,
};

static const struct regmap_config zl38_regmap_conf = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_stride = 2,
	.use_single_read = true,
	.use_single_write = true,
};

static int zl38_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct zl38_codec_priv *priv;
	struct gpio_desc *reset_gpio;
	int err;

	/* get the chip to a known state by putting it in reset */
	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);
	if (reset_gpio) {
		/* datasheet: need > 10us for a digital + analog reset */
		usleep_range(15, 50);
		/* take the chip out of reset */
		gpiod_set_value_cansleep(reset_gpio, 0);
		/* datasheet: need > 3ms for digital section to become stable */
		usleep_range(3000, 10000);
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	dev_set_drvdata(dev, priv);
	priv->regmap = devm_regmap_init(dev, &zl38_regmap_bus, spi,
					&zl38_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	err = zl38_load_firmware(dev, priv->regmap);
	if (err)
		return err;

	err = zl38_check_revision(dev, priv->regmap);
	if (err)
		return err;

	priv->gpio_chip = devm_kmemdup(dev, &template_chip,
				       sizeof(template_chip), GFP_KERNEL);
	if (!priv->gpio_chip)
		return -ENOMEM;
	priv->gpio_chip->parent = dev;
	err = devm_gpiochip_add_data(dev, priv->gpio_chip, priv->regmap);
	if (err)
		return err;

	/* setup the cross-point switch for stereo bypass */
	err = regmap_multi_reg_write(priv->regmap, cp_config_stereo_bypass,
				     ARRAY_SIZE(cp_config_stereo_bypass));
	if (err)
		return err;
	/* setup for 12MHz crystal connected to the chip */
	err = regmap_update_bits(priv->regmap, REG_CLK_CFG, CLK_CFG_SOURCE_XTAL,
				 CLK_CFG_SOURCE_XTAL);
	if (err)
		return err;

	return devm_snd_soc_register_component(dev, &zl38_component_dev,
					       &zl38_dai, 1);
}

static const struct of_device_id zl38_dt_ids[] = {
	{ .compatible = "mscc,zl38060", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zl38_dt_ids);

static const struct spi_device_id zl38_spi_ids[] = {
	{ "zl38060", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, zl38_spi_ids);

static struct spi_driver zl38060_spi_driver = {
	.driver	= {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(zl38_dt_ids),
	},
	.probe = zl38_spi_probe,
	.id_table = zl38_spi_ids,
};
module_spi_driver(zl38060_spi_driver);

MODULE_DESCRIPTION("ASoC ZL38060 driver");
MODULE_AUTHOR("Sven Van Asbroeck <TheSven73@gmail.com>");
MODULE_LICENSE("GPL v2");
