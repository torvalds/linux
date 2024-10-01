// SPDX-License-Identifier: GPL-2.0
//
// peb2466.c  --  Infineon PEB2466 ALSA SoC driver
//
// Copyright 2023 CS GROUP France
//
// Author: Herve Codina <herve.codina@bootlin.com>

#include <asm/unaligned.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define PEB2466_NB_CHANNEL	4

struct peb2466_lookup {
	u8 (*table)[4];
	unsigned int count;
};

#define PEB2466_TLV_SIZE  (sizeof((unsigned int []){TLV_DB_SCALE_ITEM(0, 0, 0)}) / \
			   sizeof(unsigned int))

struct peb2466_lkup_ctrl {
	int reg;
	unsigned int index;
	const struct peb2466_lookup *lookup;
	unsigned int tlv_array[PEB2466_TLV_SIZE];
};

struct peb2466 {
	struct spi_device *spi;
	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	u8 spi_tx_buf[2 + 8]; /* Cannot use stack area for SPI (dma-safe memory) */
	u8 spi_rx_buf[2 + 8]; /* Cannot use stack area for SPI (dma-safe memory) */
	struct regmap *regmap;
	struct {
		struct peb2466_lookup ax_lookup;
		struct peb2466_lookup ar_lookup;
		struct peb2466_lkup_ctrl ax_lkup_ctrl;
		struct peb2466_lkup_ctrl ar_lkup_ctrl;
		unsigned int tg1_freq_item;
		unsigned int tg2_freq_item;
	} ch[PEB2466_NB_CHANNEL];
	int max_chan_playback;
	int max_chan_capture;
	struct {
		struct gpio_chip gpio_chip;
		struct mutex lock;
		struct {
			unsigned int xr0;
			unsigned int xr1;
			unsigned int xr2;
			unsigned int xr3;
		} cache;
	} gpio;
};

#define PEB2466_CMD_R	(1 << 5)
#define PEB2466_CMD_W	(0 << 5)

#define PEB2466_CMD_MASK 0x18
#define PEB2466_CMD_XOP  0x18  /* XOP is 0bxxx11xxx */
#define PEB2466_CMD_SOP  0x10  /* SOP is 0bxxx10xxx */
#define PEB2466_CMD_COP  0x00  /* COP is 0bxxx0xxxx, handle 0bxxx00xxx */
#define PEB2466_CMD_COP1 0x08  /* COP is 0bxxx0xxxx, handle 0bxxx01xxx */

#define PEB2466_MAKE_XOP(_lsel)      (PEB2466_CMD_XOP | (_lsel))
#define PEB2466_MAKE_SOP(_ad, _lsel) (PEB2466_CMD_SOP | ((_ad) << 6) | (_lsel))
#define PEB2466_MAKE_COP(_ad, _code) (PEB2466_CMD_COP | ((_ad) << 6) | (_code))

#define PEB2466_CR0(_ch)	PEB2466_MAKE_SOP(_ch, 0x0)
#define   PEB2466_CR0_TH		(1 << 7)
#define   PEB2466_CR0_IMR1		(1 << 6)
#define   PEB2466_CR0_FRX		(1 << 5)
#define   PEB2466_CR0_FRR		(1 << 4)
#define   PEB2466_CR0_AX		(1 << 3)
#define   PEB2466_CR0_AR		(1 << 2)
#define   PEB2466_CR0_THSEL_MASK	(0x3 << 0)
#define   PEB2466_CR0_THSEL(_set)	((_set) << 0)

#define PEB2466_CR1(_ch)	PEB2466_MAKE_SOP(_ch, 0x1)
#define   PEB2466_CR1_ETG2		(1 << 7)
#define   PEB2466_CR1_ETG1		(1 << 6)
#define   PEB2466_CR1_PTG2		(1 << 5)
#define   PEB2466_CR1_PTG1		(1 << 4)
#define   PEB2466_CR1_LAW_MASK		(1 << 3)
#define   PEB2466_CR1_LAW_ALAW		(0 << 3)
#define   PEB2466_CR1_LAW_MULAW		(1 << 3)
#define   PEB2466_CR1_PU		(1 << 0)

#define PEB2466_CR2(_ch)	PEB2466_MAKE_SOP(_ch, 0x2)
#define PEB2466_CR3(_ch)	PEB2466_MAKE_SOP(_ch, 0x3)
#define PEB2466_CR4(_ch)	PEB2466_MAKE_SOP(_ch, 0x4)
#define PEB2466_CR5(_ch)	PEB2466_MAKE_SOP(_ch, 0x5)

#define PEB2466_XR0		PEB2466_MAKE_XOP(0x0)
#define PEB2466_XR1		PEB2466_MAKE_XOP(0x1)
#define PEB2466_XR2		PEB2466_MAKE_XOP(0x2)
#define PEB2466_XR3		PEB2466_MAKE_XOP(0x3)
#define PEB2466_XR4		PEB2466_MAKE_XOP(0x4)
#define PEB2466_XR5		PEB2466_MAKE_XOP(0x5)
#define   PEB2466_XR5_MCLK_1536		(0x0 << 6)
#define   PEB2466_XR5_MCLK_2048		(0x1 << 6)
#define   PEB2466_XR5_MCLK_4096		(0x2 << 6)
#define   PEB2466_XR5_MCLK_8192		(0x3 << 6)

#define PEB2466_XR6		PEB2466_MAKE_XOP(0x6)
#define   PEB2466_XR6_PCM_OFFSET(_off)	((_off) << 0)

#define PEB2466_XR7		PEB2466_MAKE_XOP(0x7)

#define PEB2466_TH_FILTER_P1(_ch)	PEB2466_MAKE_COP(_ch, 0x0)
#define PEB2466_TH_FILTER_P2(_ch)	PEB2466_MAKE_COP(_ch, 0x1)
#define PEB2466_TH_FILTER_P3(_ch)	PEB2466_MAKE_COP(_ch, 0x2)
#define PEB2466_IMR1_FILTER_P1(_ch)	PEB2466_MAKE_COP(_ch, 0x4)
#define PEB2466_IMR1_FILTER_P2(_ch)	PEB2466_MAKE_COP(_ch, 0x5)
#define PEB2466_FRX_FILTER(_ch)		PEB2466_MAKE_COP(_ch, 0x6)
#define PEB2466_FRR_FILTER(_ch)		PEB2466_MAKE_COP(_ch, 0x7)
#define PEB2466_AX_FILTER(_ch)		PEB2466_MAKE_COP(_ch, 0x8)
#define PEB2466_AR_FILTER(_ch)		PEB2466_MAKE_COP(_ch, 0x9)
#define PEB2466_TG1(_ch)		PEB2466_MAKE_COP(_ch, 0xc)
#define PEB2466_TG2(_ch)		PEB2466_MAKE_COP(_ch, 0xd)

static int peb2466_write_byte(struct peb2466 *peb2466, u8 cmd, u8 val)
{
	struct spi_transfer xfer = {
		.tx_buf = &peb2466->spi_tx_buf,
		.len = 2,
	};

	peb2466->spi_tx_buf[0] = cmd | PEB2466_CMD_W;
	peb2466->spi_tx_buf[1] = val;

	dev_dbg(&peb2466->spi->dev, "write byte (cmd %02x) %02x\n",
		peb2466->spi_tx_buf[0], peb2466->spi_tx_buf[1]);

	return spi_sync_transfer(peb2466->spi, &xfer, 1);
}

static int peb2466_read_byte(struct peb2466 *peb2466, u8 cmd, u8 *val)
{
	struct spi_transfer xfer = {
		.tx_buf = &peb2466->spi_tx_buf,
		.rx_buf = &peb2466->spi_rx_buf,
		.len = 3,
	};
	int ret;

	peb2466->spi_tx_buf[0] = cmd | PEB2466_CMD_R;

	ret = spi_sync_transfer(peb2466->spi, &xfer, 1);
	if (ret)
		return ret;

	if (peb2466->spi_rx_buf[1] != 0x81) {
		dev_err(&peb2466->spi->dev,
			"spi xfer rd (cmd %02x) invalid ident byte (0x%02x)\n",
			peb2466->spi_tx_buf[0], peb2466->spi_rx_buf[1]);
		return -EILSEQ;
	}

	*val = peb2466->spi_rx_buf[2];

	dev_dbg(&peb2466->spi->dev, "read byte (cmd %02x) %02x\n",
		peb2466->spi_tx_buf[0], *val);

	return 0;
}

static int peb2466_write_buf(struct peb2466 *peb2466, u8 cmd, const u8 *buf, unsigned int len)
{
	struct spi_transfer xfer = {
		.tx_buf = &peb2466->spi_tx_buf,
		.len = len + 1,
	};

	if (len > 8)
		return -EINVAL;

	peb2466->spi_tx_buf[0] = cmd | PEB2466_CMD_W;
	memcpy(&peb2466->spi_tx_buf[1], buf, len);

	dev_dbg(&peb2466->spi->dev, "write buf (cmd %02x, %u) %*ph\n",
		peb2466->spi_tx_buf[0], len, len, &peb2466->spi_tx_buf[1]);

	return spi_sync_transfer(peb2466->spi, &xfer, 1);
}

static int peb2466_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct peb2466 *peb2466 = context;
	int ret;

	/*
	 * Only XOP and SOP commands can be handled as registers.
	 * COP commands are handled using direct peb2466_write_buf() calls.
	 */
	switch (reg & PEB2466_CMD_MASK) {
	case PEB2466_CMD_XOP:
	case PEB2466_CMD_SOP:
		ret = peb2466_write_byte(peb2466, reg, val);
		break;
	default:
		dev_err(&peb2466->spi->dev, "Not a XOP or SOP command\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int peb2466_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct peb2466 *peb2466 = context;
	int ret;
	u8 tmp;

	/* Only XOP and SOP commands can be handled as registers */
	switch (reg & PEB2466_CMD_MASK) {
	case PEB2466_CMD_XOP:
	case PEB2466_CMD_SOP:
		ret = peb2466_read_byte(peb2466, reg, &tmp);
		if (!ret)
			*val = tmp;
		break;
	default:
		dev_err(&peb2466->spi->dev, "Not a XOP or SOP command\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct regmap_config peb2466_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
	.reg_write = peb2466_reg_write,
	.reg_read = peb2466_reg_read,
	.cache_type = REGCACHE_NONE,
};

static int peb2466_lkup_ctrl_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct peb2466_lkup_ctrl *lkup_ctrl =
		(struct peb2466_lkup_ctrl *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = lkup_ctrl->lookup->count - 1;
	return 0;
}

static int peb2466_lkup_ctrl_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct peb2466_lkup_ctrl *lkup_ctrl =
		(struct peb2466_lkup_ctrl *)kcontrol->private_value;

	ucontrol->value.integer.value[0] = lkup_ctrl->index;
	return 0;
}

static int peb2466_lkup_ctrl_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct peb2466_lkup_ctrl *lkup_ctrl =
		(struct peb2466_lkup_ctrl *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	unsigned int index;
	int ret;

	index = ucontrol->value.integer.value[0];
	if (index >= lkup_ctrl->lookup->count)
		return -EINVAL;

	if (index == lkup_ctrl->index)
		return 0;

	ret = peb2466_write_buf(peb2466, lkup_ctrl->reg,
				lkup_ctrl->lookup->table[index], 4);
	if (ret)
		return ret;

	lkup_ctrl->index = index;
	return 1; /* The value changed */
}

static int peb2466_add_lkup_ctrl(struct snd_soc_component *component,
				 struct peb2466_lkup_ctrl *lkup_ctrl,
				 const char *name, int min_val, int step)
{
	DECLARE_TLV_DB_SCALE(tlv_array, min_val, step, 0);
	struct snd_kcontrol_new control = {0};

	BUILD_BUG_ON(sizeof(lkup_ctrl->tlv_array) < sizeof(tlv_array));
	memcpy(lkup_ctrl->tlv_array, tlv_array, sizeof(tlv_array));

	control.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	control.name = name;
	control.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			 SNDRV_CTL_ELEM_ACCESS_READWRITE;
	control.tlv.p = lkup_ctrl->tlv_array;
	control.info = peb2466_lkup_ctrl_info;
	control.get = peb2466_lkup_ctrl_get;
	control.put = peb2466_lkup_ctrl_put;
	control.private_value = (unsigned long)lkup_ctrl;

	return snd_soc_add_component_controls(component, &control, 1);
}

enum peb2466_tone_freq {
	PEB2466_TONE_697HZ,
	PEB2466_TONE_800HZ,
	PEB2466_TONE_950HZ,
	PEB2466_TONE_1000HZ,
	PEB2466_TONE_1008HZ,
	PEB2466_TONE_2000HZ,
};

static const u8 peb2466_tone_lookup[][4] = {
	[PEB2466_TONE_697HZ] = {0x0a, 0x33, 0x5a, 0x2c},
	[PEB2466_TONE_800HZ] = {0x12, 0xD6, 0x5a, 0xc0},
	[PEB2466_TONE_950HZ] = {0x1c, 0xf0, 0x5c, 0xc0},
	[PEB2466_TONE_1000HZ] = {0}, /* lookup value not used for 1000Hz */
	[PEB2466_TONE_1008HZ] = {0x1a, 0xae, 0x57, 0x70},
	[PEB2466_TONE_2000HZ] = {0x00, 0x80, 0x50, 0x09},
};

static const char * const peb2466_tone_freq_txt[] = {
	[PEB2466_TONE_697HZ] = "697Hz",
	[PEB2466_TONE_800HZ] = "800Hz",
	[PEB2466_TONE_950HZ] = "950Hz",
	[PEB2466_TONE_1000HZ] = "1000Hz",
	[PEB2466_TONE_1008HZ] = "1008Hz",
	[PEB2466_TONE_2000HZ] = "2000Hz"
};

static const struct soc_enum peb2466_tg_freq[][2] = {
	[0] = {
		SOC_ENUM_SINGLE(PEB2466_TG1(0), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt),
		SOC_ENUM_SINGLE(PEB2466_TG2(0), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt)
	},
	[1] = {
		SOC_ENUM_SINGLE(PEB2466_TG1(1), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt),
		SOC_ENUM_SINGLE(PEB2466_TG2(1), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt)
	},
	[2] = {
		SOC_ENUM_SINGLE(PEB2466_TG1(2), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt),
		SOC_ENUM_SINGLE(PEB2466_TG2(2), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt)
	},
	[3] = {
		SOC_ENUM_SINGLE(PEB2466_TG1(3), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt),
		SOC_ENUM_SINGLE(PEB2466_TG2(3), 0, ARRAY_SIZE(peb2466_tone_freq_txt),
				peb2466_tone_freq_txt)
	}
};

static int peb2466_tg_freq_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	switch (e->reg) {
	case PEB2466_TG1(0):
		ucontrol->value.enumerated.item[0] = peb2466->ch[0].tg1_freq_item;
		break;
	case PEB2466_TG2(0):
		ucontrol->value.enumerated.item[0] = peb2466->ch[0].tg2_freq_item;
		break;
	case PEB2466_TG1(1):
		ucontrol->value.enumerated.item[0] = peb2466->ch[1].tg1_freq_item;
		break;
	case PEB2466_TG2(1):
		ucontrol->value.enumerated.item[0] = peb2466->ch[1].tg2_freq_item;
		break;
	case PEB2466_TG1(2):
		ucontrol->value.enumerated.item[0] = peb2466->ch[2].tg1_freq_item;
		break;
	case PEB2466_TG2(2):
		ucontrol->value.enumerated.item[0] = peb2466->ch[2].tg2_freq_item;
		break;
	case PEB2466_TG1(3):
		ucontrol->value.enumerated.item[0] = peb2466->ch[3].tg1_freq_item;
		break;
	case PEB2466_TG2(3):
		ucontrol->value.enumerated.item[0] = peb2466->ch[3].tg2_freq_item;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int peb2466_tg_freq_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *tg_freq_item;
	u8 cr1_reg, cr1_mask;
	unsigned int index;
	int ret;

	index = ucontrol->value.enumerated.item[0];

	if (index >= ARRAY_SIZE(peb2466_tone_lookup))
		return -EINVAL;

	switch (e->reg) {
	case PEB2466_TG1(0):
		tg_freq_item = &peb2466->ch[0].tg1_freq_item;
		cr1_reg = PEB2466_CR1(0);
		cr1_mask = PEB2466_CR1_PTG1;
		break;
	case PEB2466_TG2(0):
		tg_freq_item = &peb2466->ch[0].tg2_freq_item;
		cr1_reg = PEB2466_CR1(0);
		cr1_mask = PEB2466_CR1_PTG2;
		break;
	case PEB2466_TG1(1):
		tg_freq_item = &peb2466->ch[1].tg1_freq_item;
		cr1_reg = PEB2466_CR1(1);
		cr1_mask = PEB2466_CR1_PTG1;
		break;
	case PEB2466_TG2(1):
		tg_freq_item = &peb2466->ch[1].tg2_freq_item;
		cr1_reg = PEB2466_CR1(1);
		cr1_mask = PEB2466_CR1_PTG2;
		break;
	case PEB2466_TG1(2):
		tg_freq_item = &peb2466->ch[2].tg1_freq_item;
		cr1_reg = PEB2466_CR1(2);
		cr1_mask = PEB2466_CR1_PTG1;
		break;
	case PEB2466_TG2(2):
		tg_freq_item = &peb2466->ch[2].tg2_freq_item;
		cr1_reg = PEB2466_CR1(2);
		cr1_mask = PEB2466_CR1_PTG2;
		break;
	case PEB2466_TG1(3):
		tg_freq_item = &peb2466->ch[3].tg1_freq_item;
		cr1_reg = PEB2466_CR1(3);
		cr1_mask = PEB2466_CR1_PTG1;
		break;
	case PEB2466_TG2(3):
		tg_freq_item = &peb2466->ch[3].tg2_freq_item;
		cr1_reg = PEB2466_CR1(3);
		cr1_mask = PEB2466_CR1_PTG2;
		break;
	default:
		return -EINVAL;
	}

	if (index == *tg_freq_item)
		return 0;

	if (index == PEB2466_TONE_1000HZ) {
		ret = regmap_update_bits(peb2466->regmap, cr1_reg, cr1_mask, 0);
		if (ret)
			return ret;
	} else {
		ret = peb2466_write_buf(peb2466, e->reg, peb2466_tone_lookup[index], 4);
		if (ret)
			return ret;
		ret = regmap_update_bits(peb2466->regmap, cr1_reg, cr1_mask, cr1_mask);
		if (ret)
			return ret;
	}

	*tg_freq_item = index;
	return 1; /* The value changed */
}

static const struct snd_kcontrol_new peb2466_ch0_out_mix_controls[] = {
	SOC_DAPM_SINGLE("TG1 Switch", PEB2466_CR1(0), 6, 1, 0),
	SOC_DAPM_SINGLE("TG2 Switch", PEB2466_CR1(0), 7, 1, 0),
	SOC_DAPM_SINGLE("Voice Switch", PEB2466_CR2(0), 0, 1, 0)
};

static const struct snd_kcontrol_new peb2466_ch1_out_mix_controls[] = {
	SOC_DAPM_SINGLE("TG1 Switch", PEB2466_CR1(1), 6, 1, 0),
	SOC_DAPM_SINGLE("TG2 Switch", PEB2466_CR1(1), 7, 1, 0),
	SOC_DAPM_SINGLE("Voice Switch", PEB2466_CR2(1), 0, 1, 0)
};

static const struct snd_kcontrol_new peb2466_ch2_out_mix_controls[] = {
	SOC_DAPM_SINGLE("TG1 Switch", PEB2466_CR1(2), 6, 1, 0),
	SOC_DAPM_SINGLE("TG2 Switch", PEB2466_CR1(2), 7, 1, 0),
	SOC_DAPM_SINGLE("Voice Switch", PEB2466_CR2(2), 0, 1, 0)
};

static const struct snd_kcontrol_new peb2466_ch3_out_mix_controls[] = {
	SOC_DAPM_SINGLE("TG1 Switch", PEB2466_CR1(3), 6, 1, 0),
	SOC_DAPM_SINGLE("TG2 Switch", PEB2466_CR1(3), 7, 1, 0),
	SOC_DAPM_SINGLE("Voice Switch", PEB2466_CR2(3), 0, 1, 0)
};

static const struct snd_kcontrol_new peb2466_controls[] = {
	/* Attenuators */
	SOC_SINGLE("DAC0 -6dB Playback Switch", PEB2466_CR3(0), 2, 1, 0),
	SOC_SINGLE("DAC1 -6dB Playback Switch", PEB2466_CR3(1), 2, 1, 0),
	SOC_SINGLE("DAC2 -6dB Playback Switch", PEB2466_CR3(2), 2, 1, 0),
	SOC_SINGLE("DAC3 -6dB Playback Switch", PEB2466_CR3(3), 2, 1, 0),

	/* Amplifiers */
	SOC_SINGLE("ADC0 +6dB Capture Switch", PEB2466_CR3(0), 3, 1, 0),
	SOC_SINGLE("ADC1 +6dB Capture Switch", PEB2466_CR3(1), 3, 1, 0),
	SOC_SINGLE("ADC2 +6dB Capture Switch", PEB2466_CR3(2), 3, 1, 0),
	SOC_SINGLE("ADC3 +6dB Capture Switch", PEB2466_CR3(3), 3, 1, 0),

	/* Tone generators */
	SOC_ENUM_EXT("DAC0 TG1 Freq", peb2466_tg_freq[0][0],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
	SOC_ENUM_EXT("DAC1 TG1 Freq", peb2466_tg_freq[1][0],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
	SOC_ENUM_EXT("DAC2 TG1 Freq", peb2466_tg_freq[2][0],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
	SOC_ENUM_EXT("DAC3 TG1 Freq", peb2466_tg_freq[3][0],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),

	SOC_ENUM_EXT("DAC0 TG2 Freq", peb2466_tg_freq[0][1],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
	SOC_ENUM_EXT("DAC1 TG2 Freq", peb2466_tg_freq[1][1],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
	SOC_ENUM_EXT("DAC2 TG2 Freq", peb2466_tg_freq[2][1],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
	SOC_ENUM_EXT("DAC3 TG2 Freq", peb2466_tg_freq[3][1],
		     peb2466_tg_freq_get, peb2466_tg_freq_put),
};

static const struct snd_soc_dapm_widget peb2466_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("CH0 PWR", PEB2466_CR1(0), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CH1 PWR", PEB2466_CR1(1), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CH2 PWR", PEB2466_CR1(2), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CH3 PWR", PEB2466_CR1(3), 0, 0, NULL, 0),

	SND_SOC_DAPM_DAC("CH0 DIN", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("CH1 DIN", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("CH2 DIN", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("CH3 DIN", "Playback", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SIGGEN("CH0 TG1"),
	SND_SOC_DAPM_SIGGEN("CH1 TG1"),
	SND_SOC_DAPM_SIGGEN("CH2 TG1"),
	SND_SOC_DAPM_SIGGEN("CH3 TG1"),

	SND_SOC_DAPM_SIGGEN("CH0 TG2"),
	SND_SOC_DAPM_SIGGEN("CH1 TG2"),
	SND_SOC_DAPM_SIGGEN("CH2 TG2"),
	SND_SOC_DAPM_SIGGEN("CH3 TG2"),

	SND_SOC_DAPM_MIXER("DAC0 Mixer", SND_SOC_NOPM, 0, 0,
			   peb2466_ch0_out_mix_controls,
			   ARRAY_SIZE(peb2466_ch0_out_mix_controls)),
	SND_SOC_DAPM_MIXER("DAC1 Mixer", SND_SOC_NOPM, 0, 0,
			   peb2466_ch1_out_mix_controls,
			   ARRAY_SIZE(peb2466_ch1_out_mix_controls)),
	SND_SOC_DAPM_MIXER("DAC2 Mixer", SND_SOC_NOPM, 0, 0,
			   peb2466_ch2_out_mix_controls,
			   ARRAY_SIZE(peb2466_ch2_out_mix_controls)),
	SND_SOC_DAPM_MIXER("DAC3 Mixer", SND_SOC_NOPM, 0, 0,
			   peb2466_ch3_out_mix_controls,
			   ARRAY_SIZE(peb2466_ch3_out_mix_controls)),

	SND_SOC_DAPM_PGA("DAC0 PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC1 PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC2 PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC3 PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("OUT0"),
	SND_SOC_DAPM_OUTPUT("OUT1"),
	SND_SOC_DAPM_OUTPUT("OUT2"),
	SND_SOC_DAPM_OUTPUT("OUT3"),

	SND_SOC_DAPM_INPUT("IN0"),
	SND_SOC_DAPM_INPUT("IN1"),
	SND_SOC_DAPM_INPUT("IN2"),
	SND_SOC_DAPM_INPUT("IN3"),

	SND_SOC_DAPM_DAC("ADC0", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("ADC1", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("ADC2", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("ADC3", "Capture", SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route peb2466_dapm_routes[] = {
	{ "CH0 DIN", NULL, "CH0 PWR" },
	{ "CH1 DIN", NULL, "CH1 PWR" },
	{ "CH2 DIN", NULL, "CH2 PWR" },
	{ "CH3 DIN", NULL, "CH3 PWR" },

	{ "CH0 TG1", NULL, "CH0 PWR" },
	{ "CH1 TG1", NULL, "CH1 PWR" },
	{ "CH2 TG1", NULL, "CH2 PWR" },
	{ "CH3 TG1", NULL, "CH3 PWR" },

	{ "CH0 TG2", NULL, "CH0 PWR" },
	{ "CH1 TG2", NULL, "CH1 PWR" },
	{ "CH2 TG2", NULL, "CH2 PWR" },
	{ "CH3 TG2", NULL, "CH3 PWR" },

	{ "DAC0 Mixer", "TG1 Switch", "CH0 TG1" },
	{ "DAC0 Mixer", "TG2 Switch", "CH0 TG2" },
	{ "DAC0 Mixer", "Voice Switch", "CH0 DIN" },
	{ "DAC0 Mixer", NULL, "CH0 DIN" },

	{ "DAC1 Mixer", "TG1 Switch", "CH1 TG1" },
	{ "DAC1 Mixer", "TG2 Switch", "CH1 TG2" },
	{ "DAC1 Mixer", "Voice Switch", "CH1 DIN" },
	{ "DAC1 Mixer", NULL, "CH1 DIN" },

	{ "DAC2 Mixer", "TG1 Switch", "CH2 TG1" },
	{ "DAC2 Mixer", "TG2 Switch", "CH2 TG2" },
	{ "DAC2 Mixer", "Voice Switch", "CH2 DIN" },
	{ "DAC2 Mixer", NULL, "CH2 DIN" },

	{ "DAC3 Mixer", "TG1 Switch", "CH3 TG1" },
	{ "DAC3 Mixer", "TG2 Switch", "CH3 TG2" },
	{ "DAC3 Mixer", "Voice Switch", "CH3 DIN" },
	{ "DAC3 Mixer", NULL, "CH3 DIN" },

	{ "DAC0 PGA", NULL, "DAC0 Mixer" },
	{ "DAC1 PGA", NULL, "DAC1 Mixer" },
	{ "DAC2 PGA", NULL, "DAC2 Mixer" },
	{ "DAC3 PGA", NULL, "DAC3 Mixer" },

	{ "OUT0", NULL, "DAC0 PGA" },
	{ "OUT1", NULL, "DAC1 PGA" },
	{ "OUT2", NULL, "DAC2 PGA" },
	{ "OUT3", NULL, "DAC3 PGA" },

	{ "ADC0", NULL, "IN0" },
	{ "ADC1", NULL, "IN1" },
	{ "ADC2", NULL, "IN2" },
	{ "ADC3", NULL, "IN3" },

	{ "ADC0", NULL, "CH0 PWR" },
	{ "ADC1", NULL, "CH1 PWR" },
	{ "ADC2", NULL, "CH2 PWR" },
	{ "ADC3", NULL, "CH3 PWR" },
};

static int peb2466_dai_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				    unsigned int rx_mask, int slots, int width)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(dai->component);
	unsigned int chan;
	unsigned int mask;
	u8 slot;
	int ret;

	switch (width) {
	case 0:
		/* Not set -> default 8 */
	case 8:
		break;
	default:
		dev_err(dai->dev, "tdm slot width %d not supported\n", width);
		return -EINVAL;
	}

	mask = tx_mask;
	slot = 0;
	chan = 0;
	while (mask && chan < PEB2466_NB_CHANNEL) {
		if (mask & 0x1) {
			ret = regmap_write(peb2466->regmap, PEB2466_CR5(chan), slot);
			if (ret) {
				dev_err(dai->dev, "chan %d set tx tdm slot failed (%d)\n",
					chan, ret);
				return ret;
			}
			chan++;
		}
		mask >>= 1;
		slot++;
	}
	if (mask) {
		dev_err(dai->dev, "too much tx slots defined (mask = 0x%x) support max %d\n",
			tx_mask, PEB2466_NB_CHANNEL);
		return -EINVAL;
	}
	peb2466->max_chan_playback = chan;

	mask = rx_mask;
	slot = 0;
	chan = 0;
	while (mask && chan < PEB2466_NB_CHANNEL) {
		if (mask & 0x1) {
			ret = regmap_write(peb2466->regmap, PEB2466_CR4(chan), slot);
			if (ret) {
				dev_err(dai->dev, "chan %d set rx tdm slot failed (%d)\n",
					chan, ret);
				return ret;
			}
			chan++;
		}
		mask >>= 1;
		slot++;
	}
	if (mask) {
		dev_err(dai->dev, "too much rx slots defined (mask = 0x%x) support max %d\n",
			rx_mask, PEB2466_NB_CHANNEL);
		return -EINVAL;
	}
	peb2466->max_chan_capture = chan;

	return 0;
}

static int peb2466_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(dai->component);
	u8 xr6;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		xr6 = PEB2466_XR6_PCM_OFFSET(1);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		xr6 = PEB2466_XR6_PCM_OFFSET(0);
		break;
	default:
		dev_err(dai->dev, "Unsupported format 0x%x\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}
	return regmap_write(peb2466->regmap, PEB2466_XR6, xr6);
}

static int peb2466_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(dai->component);
	unsigned int ch;
	int ret;
	u8 cr1;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_MU_LAW:
		cr1 = PEB2466_CR1_LAW_MULAW;
		break;
	case SNDRV_PCM_FORMAT_A_LAW:
		cr1 = PEB2466_CR1_LAW_ALAW;
		break;
	default:
		dev_err(&peb2466->spi->dev, "Unsupported format 0x%x\n",
			params_format(params));
		return -EINVAL;
	}

	for (ch = 0; ch < PEB2466_NB_CHANNEL; ch++) {
		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR1(ch),
					 PEB2466_CR1_LAW_MASK, cr1);
		if (ret)
			return ret;
	}

	return 0;
}

static const unsigned int peb2466_sample_bits[] = {8};

static struct snd_pcm_hw_constraint_list peb2466_sample_bits_constr = {
	.list = peb2466_sample_bits,
	.count = ARRAY_SIZE(peb2466_sample_bits),
};

static int peb2466_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(dai->component);
	unsigned int max_ch;
	int ret;

	max_ch = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		peb2466->max_chan_playback : peb2466->max_chan_capture;

	/*
	 * Disable stream support (min = 0, max = 0) if no timeslots were
	 * configured.
	 */
	ret = snd_pcm_hw_constraint_minmax(substream->runtime,
					   SNDRV_PCM_HW_PARAM_CHANNELS,
					   max_ch ? 1 : 0, max_ch);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
					  &peb2466_sample_bits_constr);
}

static const u64 peb2466_dai_formats[] = {
	SND_SOC_POSSIBLE_DAIFMT_DSP_A	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_B,
};

static const struct snd_soc_dai_ops peb2466_dai_ops = {
	.startup = peb2466_dai_startup,
	.hw_params = peb2466_dai_hw_params,
	.set_tdm_slot = peb2466_dai_set_tdm_slot,
	.set_fmt = peb2466_dai_set_fmt,
	.auto_selectable_formats     = peb2466_dai_formats,
	.num_auto_selectable_formats = ARRAY_SIZE(peb2466_dai_formats),
};

static struct snd_soc_dai_driver peb2466_dai_driver = {
	.name = "peb2466",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = PEB2466_NB_CHANNEL,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = PEB2466_NB_CHANNEL,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW,
	},
	.ops = &peb2466_dai_ops,
};

static int peb2466_reset_audio(struct peb2466 *peb2466)
{
	static const struct reg_sequence reg_reset[] = {
		{  .reg = PEB2466_XR6,    .def = 0x00 },

		{  .reg = PEB2466_CR5(0), .def = 0x00 },
		{  .reg = PEB2466_CR4(0), .def = 0x00 },
		{  .reg = PEB2466_CR3(0), .def = 0x00 },
		{  .reg = PEB2466_CR2(0), .def = 0x00 },
		{  .reg = PEB2466_CR1(0), .def = 0x00 },
		{  .reg = PEB2466_CR0(0), .def = PEB2466_CR0_IMR1 },

		{  .reg = PEB2466_CR5(1), .def = 0x00 },
		{  .reg = PEB2466_CR4(1), .def = 0x00 },
		{  .reg = PEB2466_CR3(1), .def = 0x00 },
		{  .reg = PEB2466_CR2(1), .def = 0x00 },
		{  .reg = PEB2466_CR1(1), .def = 0x00 },
		{  .reg = PEB2466_CR0(1), .def = PEB2466_CR0_IMR1 },

		{  .reg = PEB2466_CR5(2), .def = 0x00 },
		{  .reg = PEB2466_CR4(2), .def = 0x00 },
		{  .reg = PEB2466_CR3(2), .def = 0x00 },
		{  .reg = PEB2466_CR2(2), .def = 0x00 },
		{  .reg = PEB2466_CR1(2), .def = 0x00 },
		{  .reg = PEB2466_CR0(2), .def = PEB2466_CR0_IMR1 },

		{  .reg = PEB2466_CR5(3), .def = 0x00 },
		{  .reg = PEB2466_CR4(3), .def = 0x00 },
		{  .reg = PEB2466_CR3(3), .def = 0x00 },
		{  .reg = PEB2466_CR2(3), .def = 0x00 },
		{  .reg = PEB2466_CR1(3), .def = 0x00 },
		{  .reg = PEB2466_CR0(3), .def = PEB2466_CR0_IMR1 },
	};
	static const u8 imr1_p1[8] = {0x00, 0x90, 0x09, 0x00, 0x90, 0x09, 0x00, 0x00};
	static const u8 imr1_p2[8] = {0x7F, 0xFF, 0x00, 0x00, 0x90, 0x14, 0x40, 0x08};
	static const u8 zero[8] = {0};
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		peb2466->ch[i].tg1_freq_item = PEB2466_TONE_1000HZ;
		peb2466->ch[i].tg2_freq_item = PEB2466_TONE_1000HZ;

		/*
		 * Even if not used, disabling IM/R1 filter is not recommended.
		 * Instead, we must configure it with default coefficients and
		 * enable it.
		 * The filter will be enabled right after (in the following
		 * regmap_multi_reg_write() call).
		 */
		ret = peb2466_write_buf(peb2466, PEB2466_IMR1_FILTER_P1(i), imr1_p1, 8);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_IMR1_FILTER_P2(i), imr1_p2, 8);
		if (ret)
			return ret;

		/* Set all other filters coefficients to zero */
		ret = peb2466_write_buf(peb2466, PEB2466_TH_FILTER_P1(i), zero, 8);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_TH_FILTER_P2(i), zero, 8);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_TH_FILTER_P3(i), zero, 8);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_FRX_FILTER(i), zero, 8);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_FRR_FILTER(i), zero, 8);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_AX_FILTER(i), zero, 4);
		if (ret)
			return ret;
		ret = peb2466_write_buf(peb2466, PEB2466_AR_FILTER(i), zero, 4);
		if (ret)
			return ret;
	}

	return regmap_multi_reg_write(peb2466->regmap, reg_reset, ARRAY_SIZE(reg_reset));
}

static int peb2466_fw_parse_thfilter(struct snd_soc_component *component,
				     u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	u8 mask;
	int ret;
	int i;

	dev_info(component->dev, "fw TH filter: mask %x, %*phN\n", *data,
		 lng - 1, data + 1);

	/*
	 * TH_FILTER TLV data:
	 *   - @0  1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1  8 bytes: TH-Filter coefficients part1
	 *   - @9  8 bytes: TH-Filter coefficients part2
	 *   - @17 8 bytes: TH-Filter coefficients part3
	 */
	mask = *data;
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_TH, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_TH_FILTER_P1(i), data + 1, 8);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_TH_FILTER_P2(i), data + 9, 8);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_TH_FILTER_P3(i), data + 17, 8);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_TH | PEB2466_CR0_THSEL_MASK,
					 PEB2466_CR0_TH | PEB2466_CR0_THSEL(i));
		if (ret)
			return ret;
	}
	return 0;
}

static int peb2466_fw_parse_imr1filter(struct snd_soc_component *component,
				       u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	u8 mask;
	int ret;
	int i;

	dev_info(component->dev, "fw IM/R1 filter: mask %x, %*phN\n", *data,
		 lng - 1, data + 1);

	/*
	 * IMR1_FILTER TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 8 bytes: IM/R1-Filter coefficients part1
	 *   - @9 8 bytes: IM/R1-Filter coefficients part2
	 */
	mask = *data;
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_IMR1, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_IMR1_FILTER_P1(i), data + 1, 8);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_IMR1_FILTER_P2(i), data + 9, 8);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_IMR1, PEB2466_CR0_IMR1);
		if (ret)
			return ret;
	}
	return 0;
}

static int peb2466_fw_parse_frxfilter(struct snd_soc_component *component,
				      u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	u8 mask;
	int ret;
	int i;

	dev_info(component->dev, "fw FRX filter: mask %x, %*phN\n", *data,
		 lng - 1, data + 1);

	/*
	 * FRX_FILTER TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 8 bytes: FRX-Filter coefficients
	 */
	mask = *data;
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_FRX, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_FRX_FILTER(i), data + 1, 8);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_FRX, PEB2466_CR0_FRX);
		if (ret)
			return ret;
	}
	return 0;
}

static int peb2466_fw_parse_frrfilter(struct snd_soc_component *component,
				      u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	u8 mask;
	int ret;
	int i;

	dev_info(component->dev, "fw FRR filter: mask %x, %*phN\n", *data,
		 lng - 1, data + 1);

	/*
	 * FRR_FILTER TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 8 bytes: FRR-Filter coefficients
	 */
	mask = *data;
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_FRR, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_FRR_FILTER(i), data + 1, 8);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_FRR, PEB2466_CR0_FRR);
		if (ret)
			return ret;
	}
	return 0;
}

static int peb2466_fw_parse_axfilter(struct snd_soc_component *component,
				     u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	u8 mask;
	int ret;
	int i;

	dev_info(component->dev, "fw AX filter: mask %x, %*phN\n", *data,
		 lng - 1, data + 1);

	/*
	 * AX_FILTER TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 4 bytes: AX-Filter coefficients
	 */
	mask = *data;
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AX, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_AX_FILTER(i), data + 1, 4);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AX, PEB2466_CR0_AX);
		if (ret)
			return ret;
	}
	return 0;
}

static int peb2466_fw_parse_arfilter(struct snd_soc_component *component,
				     u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	u8 mask;
	int ret;
	int i;

	dev_info(component->dev, "fw AR filter: mask %x, %*phN\n", *data,
		 lng - 1, data + 1);

	/*
	 * AR_FILTER TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 4 bytes: AR-Filter coefficients
	 */
	mask = *data;
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AR, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_AR_FILTER(i), data + 1, 4);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AR, PEB2466_CR0_AR);
		if (ret)
			return ret;
	}
	return 0;
}

static const char * const peb2466_ax_ctrl_names[] = {
	"ADC0 Capture Volume",
	"ADC1 Capture Volume",
	"ADC2 Capture Volume",
	"ADC3 Capture Volume",
};

static int peb2466_fw_parse_axtable(struct snd_soc_component *component,
				    u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	struct peb2466_lkup_ctrl *lkup_ctrl;
	struct peb2466_lookup *lookup;
	u8 (*table)[4];
	u32 table_size;
	u32 init_index;
	s32 min_val;
	s32 step;
	u8 mask;
	int ret;
	int i;

	/*
	 * AX_TABLE TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 32bits signed: Min table value in centi dB (MinVal)
	 *                       ie -300 means -3.0 dB
	 *   - @5 32bits signed: Step from on item to other item in centi dB (Step)
	 *                       ie 25 means 0.25 dB)
	 *   - @9 32bits unsigned: Item index in the table to use for the initial
	 *                         value
	 *   - @13 N*4 bytes: Table composed of 4 bytes items.
	 *                    Each item correspond to an AX filter value.
	 *
	 * The conversion from raw value item in the table to/from the value in
	 * dB is: Raw value at index i <-> (MinVal + i * Step) in centi dB.
	 */

	/* Check Lng and extract the table size. */
	if (lng < 13 || ((lng - 13) % 4)) {
		dev_err(component->dev, "fw AX table lng %u invalid\n", lng);
		return -EINVAL;
	}
	table_size = lng - 13;

	min_val = get_unaligned_be32(data + 1);
	step = get_unaligned_be32(data + 5);
	init_index = get_unaligned_be32(data + 9);
	if (init_index >= (table_size / 4)) {
		dev_err(component->dev, "fw AX table index %u out of table[%u]\n",
			init_index, table_size / 4);
		return -EINVAL;
	}

	dev_info(component->dev,
		 "fw AX table: mask %x, min %d, step %d, %u items, tbl[%u] %*phN\n",
		 *data, min_val, step, table_size / 4, init_index,
		 4, data + 13 + (init_index * 4));

	BUILD_BUG_ON(sizeof(*table) != 4);
	table = devm_kzalloc(&peb2466->spi->dev, table_size, GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	memcpy(table, data + 13, table_size);

	mask = *data;
	BUILD_BUG_ON(ARRAY_SIZE(peb2466_ax_ctrl_names) != ARRAY_SIZE(peb2466->ch));
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		lookup = &peb2466->ch[i].ax_lookup;
		lookup->table = table;
		lookup->count = table_size / 4;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AX, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_AX_FILTER(i),
					lookup->table[init_index], 4);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AX, PEB2466_CR0_AX);
		if (ret)
			return ret;

		lkup_ctrl = &peb2466->ch[i].ax_lkup_ctrl;
		lkup_ctrl->lookup = lookup;
		lkup_ctrl->reg = PEB2466_AX_FILTER(i);
		lkup_ctrl->index = init_index;

		ret = peb2466_add_lkup_ctrl(component, lkup_ctrl,
					    peb2466_ax_ctrl_names[i],
					    min_val, step);
		if (ret)
			return ret;
	}
	return 0;
}

static const char * const peb2466_ar_ctrl_names[] = {
	"DAC0 Playback Volume",
	"DAC1 Playback Volume",
	"DAC2 Playback Volume",
	"DAC3 Playback Volume",
};

static int peb2466_fw_parse_artable(struct snd_soc_component *component,
				    u16 tag, u32 lng, const u8 *data)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	struct peb2466_lkup_ctrl *lkup_ctrl;
	struct peb2466_lookup *lookup;
	u8 (*table)[4];
	u32 table_size;
	u32 init_index;
	s32 min_val;
	s32 step;
	u8 mask;
	int ret;
	int i;

	/*
	 * AR_TABLE TLV data:
	 *   - @0 1 byte:  Chan mask (bit set means related channel is concerned)
	 *   - @1 32bits signed: Min table value in centi dB (MinVal)
	 *                       ie -300 means -3.0 dB
	 *   - @5 32bits signed: Step from on item to other item in centi dB (Step)
	 *                       ie 25 means 0.25 dB)
	 *   - @9 32bits unsigned: Item index in the table to use for the initial
	 *                         value
	 *   - @13 N*4 bytes: Table composed of 4 bytes items.
	 *                    Each item correspond to an AR filter value.
	 *
	 * The conversion from raw value item in the table to/from the value in
	 * dB is: Raw value at index i <-> (MinVal + i * Step) in centi dB.
	 */

	/* Check Lng and extract the table size. */
	if (lng < 13 || ((lng - 13) % 4)) {
		dev_err(component->dev, "fw AR table lng %u invalid\n", lng);
		return -EINVAL;
	}
	table_size = lng - 13;

	min_val = get_unaligned_be32(data + 1);
	step = get_unaligned_be32(data + 5);
	init_index = get_unaligned_be32(data + 9);
	if (init_index >= (table_size / 4)) {
		dev_err(component->dev, "fw AR table index %u out of table[%u]\n",
			init_index, table_size / 4);
		return -EINVAL;
	}

	dev_info(component->dev,
		 "fw AR table: mask %x, min %d, step %d, %u items, tbl[%u] %*phN\n",
		 *data, min_val, step, table_size / 4, init_index,
		 4, data + 13 + (init_index * 4));

	BUILD_BUG_ON(sizeof(*table) != 4);
	table = devm_kzalloc(&peb2466->spi->dev, table_size, GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	memcpy(table, data + 13, table_size);

	mask = *data;
	BUILD_BUG_ON(ARRAY_SIZE(peb2466_ar_ctrl_names) != ARRAY_SIZE(peb2466->ch));
	for (i = 0; i < ARRAY_SIZE(peb2466->ch); i++) {
		if (!(mask & (1 << i)))
			continue;

		lookup = &peb2466->ch[i].ar_lookup;
		lookup->table = table;
		lookup->count = table_size / 4;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AR, 0);
		if (ret)
			return ret;

		ret = peb2466_write_buf(peb2466, PEB2466_AR_FILTER(i),
					lookup->table[init_index], 4);
		if (ret)
			return ret;

		ret = regmap_update_bits(peb2466->regmap, PEB2466_CR0(i),
					 PEB2466_CR0_AR, PEB2466_CR0_AR);
		if (ret)
			return ret;

		lkup_ctrl = &peb2466->ch[i].ar_lkup_ctrl;
		lkup_ctrl->lookup = lookup;
		lkup_ctrl->reg = PEB2466_AR_FILTER(i);
		lkup_ctrl->index = init_index;

		ret = peb2466_add_lkup_ctrl(component, lkup_ctrl,
					    peb2466_ar_ctrl_names[i],
					    min_val, step);
		if (ret)
			return ret;
	}
	return 0;
}

struct peb2466_fw_tag_def {
	u16 tag;
	u32 lng_min;
	u32 lng_max;
	int (*parse)(struct snd_soc_component *component,
		     u16 tag, u32 lng, const u8 *data);
};

#define PEB2466_TAG_DEF_LNG_EQ(__tag, __lng, __parse) { \
	.tag = __tag,		\
	.lng_min = __lng,	\
	.lng_max = __lng,	\
	.parse = __parse,	\
}

#define PEB2466_TAG_DEF_LNG_MIN(__tag, __lng_min, __parse) { \
	.tag = __tag,		\
	.lng_min = __lng_min,	\
	.lng_max = U32_MAX,	\
	.parse = __parse,	\
}

static const struct peb2466_fw_tag_def peb2466_fw_tag_defs[] = {
	/* TH FILTER */
	PEB2466_TAG_DEF_LNG_EQ(0x0001, 1 + 3 * 8, peb2466_fw_parse_thfilter),
	/* IMR1 FILTER */
	PEB2466_TAG_DEF_LNG_EQ(0x0002, 1 + 2 * 8, peb2466_fw_parse_imr1filter),
	/* FRX FILTER */
	PEB2466_TAG_DEF_LNG_EQ(0x0003, 1 + 8, peb2466_fw_parse_frxfilter),
	/* FRR FILTER */
	PEB2466_TAG_DEF_LNG_EQ(0x0004, 1 + 8, peb2466_fw_parse_frrfilter),
	/* AX FILTER */
	PEB2466_TAG_DEF_LNG_EQ(0x0005, 1 + 4, peb2466_fw_parse_axfilter),
	/* AR FILTER */
	PEB2466_TAG_DEF_LNG_EQ(0x0006, 1 + 4, peb2466_fw_parse_arfilter),
	/* AX TABLE */
	PEB2466_TAG_DEF_LNG_MIN(0x0105, 1 + 3 * 4, peb2466_fw_parse_axtable),
	/* AR TABLE */
	PEB2466_TAG_DEF_LNG_MIN(0x0106, 1 + 3 * 4, peb2466_fw_parse_artable),
};

static const struct peb2466_fw_tag_def *peb2466_fw_get_tag_def(u16 tag)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(peb2466_fw_tag_defs); i++) {
		if (peb2466_fw_tag_defs[i].tag == tag)
			return &peb2466_fw_tag_defs[i];
	}
	return NULL;
}

static int peb2466_fw_parse(struct snd_soc_component *component,
			    const u8 *data, size_t size)
{
	const struct peb2466_fw_tag_def *tag_def;
	size_t left;
	const u8 *buf;
	u16 val16;
	u16 tag;
	u32 lng;
	int ret;

	/*
	 * Coefficients firmware binary structure (16bits and 32bits are
	 * big-endian values).
	 *
	 * @0, 16bits: Magic (0x2466)
	 * @2, 16bits: Version (0x0100 for version 1.0)
	 * @4, 2+4+N bytes: TLV block
	 * @4+(2+4+N) bytes: Next TLV block
	 * ...
	 *
	 * Detail of a TLV block:
	 *   @0, 16bits: Tag
	 *   @2, 32bits: Lng
	 *   @6, lng bytes: Data
	 *
	 * The detail the Data for a given TLV Tag is provided in the related
	 * parser.
	 */

	left = size;
	buf = data;

	if (left < 4) {
		dev_err(component->dev, "fw size %zu, exp at least 4\n", left);
		return -EINVAL;
	}

	/* Check magic */
	val16 = get_unaligned_be16(buf);
	if (val16 != 0x2466) {
		dev_err(component->dev, "fw magic 0x%04x exp 0x2466\n", val16);
		return -EINVAL;
	}
	buf += 2;
	left -= 2;

	/* Check version */
	val16 = get_unaligned_be16(buf);
	if (val16 != 0x0100) {
		dev_err(component->dev, "fw magic 0x%04x exp 0x0100\n", val16);
		return -EINVAL;
	}
	buf += 2;
	left -= 2;

	while (left) {
		if (left < 6) {
			dev_err(component->dev, "fw %td/%zu left %zu, exp at least 6\n",
				buf - data, size, left);
			return -EINVAL;
		}
		/* Check tag and lng */
		tag = get_unaligned_be16(buf);
		lng = get_unaligned_be32(buf + 2);
		tag_def = peb2466_fw_get_tag_def(tag);
		if (!tag_def) {
			dev_err(component->dev, "fw %td/%zu tag 0x%04x unknown\n",
				buf - data, size, tag);
			return -EINVAL;
		}
		if (lng < tag_def->lng_min || lng > tag_def->lng_max) {
			dev_err(component->dev, "fw %td/%zu tag 0x%04x lng %u, exp [%u;%u]\n",
				buf - data, size, tag, lng, tag_def->lng_min, tag_def->lng_max);
			return -EINVAL;
		}
		buf += 6;
		left -= 6;
		if (left < lng) {
			dev_err(component->dev, "fw %td/%zu tag 0x%04x lng %u, left %zu\n",
				buf - data, size, tag, lng, left);
			return -EINVAL;
		}

		/* TLV block is valid -> parse the data part */
		ret = tag_def->parse(component, tag, lng, buf);
		if (ret) {
			dev_err(component->dev, "fw %td/%zu tag 0x%04x lng %u parse failed\n",
				buf - data, size, tag, lng);
			return ret;
		}

		buf += lng;
		left -= lng;
	}
	return 0;
}

static int peb2466_load_coeffs(struct snd_soc_component *component, const char *fw_name)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, fw_name, component->dev);
	if (ret)
		return ret;

	ret = peb2466_fw_parse(component, fw->data, fw->size);
	release_firmware(fw);

	return ret;
}

static int peb2466_component_probe(struct snd_soc_component *component)
{
	struct peb2466 *peb2466 = snd_soc_component_get_drvdata(component);
	const char *firmware_name;
	int ret;

	/* reset peb2466 audio part */
	ret = peb2466_reset_audio(peb2466);
	if (ret)
		return ret;

	ret = of_property_read_string(peb2466->spi->dev.of_node,
				      "firmware-name", &firmware_name);
	if (ret)
		return (ret == -EINVAL) ? 0 : ret;

	return peb2466_load_coeffs(component, firmware_name);
}

static const struct snd_soc_component_driver peb2466_component_driver = {
	.probe			= peb2466_component_probe,
	.controls		= peb2466_controls,
	.num_controls		= ARRAY_SIZE(peb2466_controls),
	.dapm_widgets		= peb2466_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(peb2466_dapm_widgets),
	.dapm_routes		= peb2466_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(peb2466_dapm_routes),
	.endianness		= 1,
};

/*
 * The mapping used for the relationship between the gpio offset and the
 * physical pin is the following:
 *
 * offset     pin
 *      0     SI1_0
 *      1     SI1_1
 *      2     SI2_0
 *      3     SI2_1
 *      4     SI3_0
 *      5     SI3_1
 *      6     SI4_0
 *      7     SI4_1
 *      8     SO1_0
 *      9     SO1_1
 *     10     SO2_0
 *     11     SO2_1
 *     12     SO3_0
 *     13     SO3_1
 *     14     SO4_0
 *     15     SO4_1
 *     16     SB1_0
 *     17     SB1_1
 *     18     SB2_0
 *     19     SB2_1
 *     20     SB3_0
 *     21     SB3_1
 *     22     SB4_0
 *     23     SB4_1
 *     24     SB1_2
 *     25     SB2_2
 *     26     SB3_2
 *     27     SB4_2
 */

static int peb2466_chip_gpio_offset_to_data_regmask(unsigned int offset,
						    unsigned int *xr_reg,
						    unsigned int *mask)
{
	if (offset < 16) {
		/*
		 * SIx_{0,1} and SOx_{0,1}
		 *   Read accesses read SIx_{0,1} values
		 *   Write accesses write SOx_{0,1} values
		 */
		*xr_reg = PEB2466_XR0;
		*mask = (1 << (offset % 8));
		return 0;
	}
	if (offset < 24) {
		/* SBx_{0,1} */
		*xr_reg = PEB2466_XR1;
		*mask = (1 << (offset - 16));
		return 0;
	}
	if (offset < 28) {
		/* SBx_2 */
		*xr_reg = PEB2466_XR3;
		*mask = (1 << (offset - 24 + 4));
		return 0;
	}
	return -EINVAL;
}

static int peb2466_chip_gpio_offset_to_dir_regmask(unsigned int offset,
						   unsigned int *xr_reg,
						   unsigned int *mask)
{
	if (offset < 16) {
		/* Direction cannot be changed for these GPIOs */
		return -EINVAL;
	}
	if (offset < 24) {
		*xr_reg = PEB2466_XR2;
		*mask = (1 << (offset - 16));
		return 0;
	}
	if (offset < 28) {
		*xr_reg = PEB2466_XR3;
		*mask = (1 << (offset - 24));
		return 0;
	}
	return -EINVAL;
}

static unsigned int *peb2466_chip_gpio_get_cache(struct peb2466 *peb2466,
						 unsigned int xr_reg)
{
	unsigned int *cache;

	switch (xr_reg) {
	case PEB2466_XR0:
		cache = &peb2466->gpio.cache.xr0;
		break;
	case PEB2466_XR1:
		cache = &peb2466->gpio.cache.xr1;
		break;
	case PEB2466_XR2:
		cache = &peb2466->gpio.cache.xr2;
		break;
	case PEB2466_XR3:
		cache = &peb2466->gpio.cache.xr3;
		break;
	default:
		cache = NULL;
		break;
	}
	return cache;
}

static int peb2466_chip_gpio_update_bits(struct peb2466 *peb2466, unsigned int xr_reg,
					 unsigned int mask, unsigned int val)
{
	unsigned int tmp;
	unsigned int *cache;
	int ret;

	/*
	 * Read and write accesses use different peb2466 internal signals (input
	 * signals on reads and output signals on writes). regmap_update_bits
	 * cannot be used to read/modify/write the value.
	 * So, a specific cache value is used.
	 */

	mutex_lock(&peb2466->gpio.lock);

	cache = peb2466_chip_gpio_get_cache(peb2466, xr_reg);
	if (!cache) {
		ret = -EINVAL;
		goto end;
	}

	tmp = *cache;
	tmp &= ~mask;
	tmp |= val;

	ret = regmap_write(peb2466->regmap, xr_reg, tmp);
	if (ret)
		goto end;

	*cache = tmp;
	ret = 0;

end:
	mutex_unlock(&peb2466->gpio.lock);
	return ret;
}

static void peb2466_chip_gpio_set(struct gpio_chip *c, unsigned int offset, int val)
{
	struct peb2466 *peb2466 = gpiochip_get_data(c);
	unsigned int xr_reg;
	unsigned int mask;
	int ret;

	if (offset < 8) {
		/*
		 * SIx_{0,1} signals cannot be set and writing the related
		 * register will change the SOx_{0,1} signals
		 */
		dev_warn(&peb2466->spi->dev, "cannot set gpio %d (read-only)\n",
			 offset);
		return;
	}

	ret = peb2466_chip_gpio_offset_to_data_regmask(offset, &xr_reg, &mask);
	if (ret) {
		dev_err(&peb2466->spi->dev, "cannot set gpio %d (%d)\n",
			offset, ret);
		return;
	}

	ret = peb2466_chip_gpio_update_bits(peb2466, xr_reg, mask, val ? mask : 0);
	if (ret) {
		dev_err(&peb2466->spi->dev, "set gpio %d (0x%x, 0x%x) failed (%d)\n",
			offset, xr_reg, mask, ret);
	}
}

static int peb2466_chip_gpio_get(struct gpio_chip *c, unsigned int offset)
{
	struct peb2466 *peb2466 = gpiochip_get_data(c);
	bool use_cache = false;
	unsigned int *cache;
	unsigned int xr_reg;
	unsigned int mask;
	unsigned int val;
	int ret;

	if (offset >= 8 && offset < 16) {
		/*
		 * SOx_{0,1} signals cannot be read. Reading the related
		 * register will read the SIx_{0,1} signals.
		 * Use the cache to get value;
		 */
		use_cache = true;
	}

	ret = peb2466_chip_gpio_offset_to_data_regmask(offset, &xr_reg, &mask);
	if (ret) {
		dev_err(&peb2466->spi->dev, "cannot get gpio %d (%d)\n",
			offset, ret);
		return -EINVAL;
	}

	if (use_cache) {
		cache = peb2466_chip_gpio_get_cache(peb2466, xr_reg);
		if (!cache)
			return -EINVAL;
		val = *cache;
	} else {
		ret = regmap_read(peb2466->regmap, xr_reg, &val);
		if (ret) {
			dev_err(&peb2466->spi->dev, "get gpio %d (0x%x, 0x%x) failed (%d)\n",
				offset, xr_reg, mask, ret);
			return ret;
		}
	}

	return !!(val & mask);
}

static int peb2466_chip_get_direction(struct gpio_chip *c, unsigned int offset)
{
	struct peb2466 *peb2466 = gpiochip_get_data(c);
	unsigned int xr_reg;
	unsigned int mask;
	unsigned int val;
	int ret;

	if (offset < 8) {
		/* SIx_{0,1} */
		return GPIO_LINE_DIRECTION_IN;
	}
	if (offset < 16) {
		/* SOx_{0,1} */
		return GPIO_LINE_DIRECTION_OUT;
	}

	ret = peb2466_chip_gpio_offset_to_dir_regmask(offset, &xr_reg, &mask);
	if (ret) {
		dev_err(&peb2466->spi->dev, "cannot get gpio %d direction (%d)\n",
			offset, ret);
		return ret;
	}

	ret = regmap_read(peb2466->regmap, xr_reg, &val);
	if (ret) {
		dev_err(&peb2466->spi->dev, "get dir gpio %d (0x%x, 0x%x) failed (%d)\n",
			offset, xr_reg, mask, ret);
		return ret;
	}

	return val & mask ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int peb2466_chip_direction_input(struct gpio_chip *c, unsigned int offset)
{
	struct peb2466 *peb2466 = gpiochip_get_data(c);
	unsigned int xr_reg;
	unsigned int mask;
	int ret;

	if (offset < 8) {
		/* SIx_{0,1} */
		return 0;
	}
	if (offset < 16) {
		/* SOx_{0,1} */
		return -EINVAL;
	}

	ret = peb2466_chip_gpio_offset_to_dir_regmask(offset, &xr_reg, &mask);
	if (ret) {
		dev_err(&peb2466->spi->dev, "cannot set gpio %d direction (%d)\n",
			offset, ret);
		return ret;
	}

	ret = peb2466_chip_gpio_update_bits(peb2466, xr_reg, mask, 0);
	if (ret) {
		dev_err(&peb2466->spi->dev, "Set dir in gpio %d (0x%x, 0x%x) failed (%d)\n",
			offset, xr_reg, mask, ret);
		return ret;
	}

	return 0;
}

static int peb2466_chip_direction_output(struct gpio_chip *c, unsigned int offset, int val)
{
	struct peb2466 *peb2466 = gpiochip_get_data(c);
	unsigned int xr_reg;
	unsigned int mask;
	int ret;

	if (offset < 8) {
		/* SIx_{0,1} */
		return -EINVAL;
	}

	peb2466_chip_gpio_set(c, offset, val);

	if (offset < 16) {
		/* SOx_{0,1} */
		return 0;
	}

	ret = peb2466_chip_gpio_offset_to_dir_regmask(offset, &xr_reg, &mask);
	if (ret) {
		dev_err(&peb2466->spi->dev, "cannot set gpio %d direction (%d)\n",
			offset, ret);
		return ret;
	}

	ret = peb2466_chip_gpio_update_bits(peb2466, xr_reg, mask, mask);
	if (ret) {
		dev_err(&peb2466->spi->dev, "Set dir in gpio %d (0x%x, 0x%x) failed (%d)\n",
			offset, xr_reg, mask, ret);
		return ret;
	}

	return 0;
}

static int peb2466_reset_gpio(struct peb2466 *peb2466)
{
	static const struct reg_sequence reg_reset[] = {
		/* Output pins at 0, input/output pins as input */
		{  .reg = PEB2466_XR0, .def = 0 },
		{  .reg = PEB2466_XR1, .def = 0 },
		{  .reg = PEB2466_XR2, .def = 0 },
		{  .reg = PEB2466_XR3, .def = 0 },
	};

	peb2466->gpio.cache.xr0 = 0;
	peb2466->gpio.cache.xr1 = 0;
	peb2466->gpio.cache.xr2 = 0;
	peb2466->gpio.cache.xr3 = 0;

	return regmap_multi_reg_write(peb2466->regmap, reg_reset, ARRAY_SIZE(reg_reset));
}

static int peb2466_gpio_init(struct peb2466 *peb2466)
{
	int ret;

	mutex_init(&peb2466->gpio.lock);

	ret = peb2466_reset_gpio(peb2466);
	if (ret)
		return ret;

	peb2466->gpio.gpio_chip.owner = THIS_MODULE;
	peb2466->gpio.gpio_chip.label = dev_name(&peb2466->spi->dev);
	peb2466->gpio.gpio_chip.parent = &peb2466->spi->dev;
	peb2466->gpio.gpio_chip.base = -1;
	peb2466->gpio.gpio_chip.ngpio = 28;
	peb2466->gpio.gpio_chip.get_direction = peb2466_chip_get_direction;
	peb2466->gpio.gpio_chip.direction_input = peb2466_chip_direction_input;
	peb2466->gpio.gpio_chip.direction_output = peb2466_chip_direction_output;
	peb2466->gpio.gpio_chip.get = peb2466_chip_gpio_get;
	peb2466->gpio.gpio_chip.set = peb2466_chip_gpio_set;
	peb2466->gpio.gpio_chip.can_sleep = true;

	return devm_gpiochip_add_data(&peb2466->spi->dev, &peb2466->gpio.gpio_chip,
				      peb2466);
}

static int peb2466_spi_probe(struct spi_device *spi)
{
	struct peb2466 *peb2466;
	unsigned long mclk_rate;
	int ret;
	u8 xr5;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	peb2466 = devm_kzalloc(&spi->dev, sizeof(*peb2466), GFP_KERNEL);
	if (!peb2466)
		return -ENOMEM;

	peb2466->spi = spi;

	peb2466->regmap = devm_regmap_init(&peb2466->spi->dev, NULL, peb2466,
					   &peb2466_regmap_config);
	if (IS_ERR(peb2466->regmap))
		return PTR_ERR(peb2466->regmap);

	peb2466->reset_gpio = devm_gpiod_get_optional(&peb2466->spi->dev,
						      "reset", GPIOD_OUT_LOW);
	if (IS_ERR(peb2466->reset_gpio))
		return PTR_ERR(peb2466->reset_gpio);

	peb2466->mclk = devm_clk_get(&peb2466->spi->dev, "mclk");
	if (IS_ERR(peb2466->mclk))
		return PTR_ERR(peb2466->mclk);
	ret = clk_prepare_enable(peb2466->mclk);
	if (ret)
		return ret;

	if (peb2466->reset_gpio) {
		gpiod_set_value_cansleep(peb2466->reset_gpio, 1);
		udelay(4);
		gpiod_set_value_cansleep(peb2466->reset_gpio, 0);
		udelay(4);
	}

	spi_set_drvdata(spi, peb2466);

	mclk_rate = clk_get_rate(peb2466->mclk);
	switch (mclk_rate) {
	case 1536000:
		xr5 = PEB2466_XR5_MCLK_1536;
		break;
	case 2048000:
		xr5 = PEB2466_XR5_MCLK_2048;
		break;
	case 4096000:
		xr5 = PEB2466_XR5_MCLK_4096;
		break;
	case 8192000:
		xr5 = PEB2466_XR5_MCLK_8192;
		break;
	default:
		dev_err(&peb2466->spi->dev, "Unsupported clock rate %lu\n",
			mclk_rate);
		ret = -EINVAL;
		goto failed;
	}
	ret = regmap_write(peb2466->regmap, PEB2466_XR5, xr5);
	if (ret) {
		dev_err(&peb2466->spi->dev, "Setting MCLK failed (%d)\n", ret);
		goto failed;
	}

	ret = devm_snd_soc_register_component(&spi->dev, &peb2466_component_driver,
					      &peb2466_dai_driver, 1);
	if (ret)
		goto failed;

	if (IS_ENABLED(CONFIG_GPIOLIB)) {
		ret = peb2466_gpio_init(peb2466);
		if (ret)
			goto failed;
	}

	return 0;

failed:
	clk_disable_unprepare(peb2466->mclk);
	return ret;
}

static void peb2466_spi_remove(struct spi_device *spi)
{
	struct peb2466 *peb2466 = spi_get_drvdata(spi);

	clk_disable_unprepare(peb2466->mclk);
}

static const struct of_device_id peb2466_of_match[] = {
	{ .compatible = "infineon,peb2466", },
	{ }
};
MODULE_DEVICE_TABLE(of, peb2466_of_match);

static const struct spi_device_id peb2466_id_table[] = {
	{ "peb2466", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, peb2466_id_table);

static struct spi_driver peb2466_spi_driver = {
	.driver  = {
		.name   = "peb2466",
		.of_match_table = peb2466_of_match,
	},
	.id_table = peb2466_id_table,
	.probe  = peb2466_spi_probe,
	.remove = peb2466_spi_remove,
};

module_spi_driver(peb2466_spi_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("PEB2466 ALSA SoC driver");
MODULE_LICENSE("GPL");
