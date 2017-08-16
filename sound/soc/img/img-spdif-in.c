/*
 * IMG SPDIF input controller driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define IMG_SPDIF_IN_RX_FIFO_OFFSET		0

#define IMG_SPDIF_IN_CTL			0x4
#define IMG_SPDIF_IN_CTL_LOCKLO_MASK		0xff
#define IMG_SPDIF_IN_CTL_LOCKLO_SHIFT		0
#define IMG_SPDIF_IN_CTL_LOCKHI_MASK		0xff00
#define IMG_SPDIF_IN_CTL_LOCKHI_SHIFT		8
#define IMG_SPDIF_IN_CTL_TRK_MASK		0xff0000
#define IMG_SPDIF_IN_CTL_TRK_SHIFT		16
#define IMG_SPDIF_IN_CTL_SRD_MASK		0x70000000
#define IMG_SPDIF_IN_CTL_SRD_SHIFT		28
#define IMG_SPDIF_IN_CTL_SRT_MASK		BIT(31)

#define IMG_SPDIF_IN_STATUS			0x8
#define IMG_SPDIF_IN_STATUS_SAM_MASK		0x7000
#define IMG_SPDIF_IN_STATUS_SAM_SHIFT		12
#define IMG_SPDIF_IN_STATUS_LOCK_MASK		BIT(15)
#define IMG_SPDIF_IN_STATUS_LOCK_SHIFT		15

#define IMG_SPDIF_IN_CLKGEN			0x1c
#define IMG_SPDIF_IN_CLKGEN_NOM_MASK		0x3ff
#define IMG_SPDIF_IN_CLKGEN_NOM_SHIFT		0
#define IMG_SPDIF_IN_CLKGEN_HLD_MASK		0x3ff0000
#define IMG_SPDIF_IN_CLKGEN_HLD_SHIFT		16

#define IMG_SPDIF_IN_CSL			0x20

#define IMG_SPDIF_IN_CSH			0x24
#define IMG_SPDIF_IN_CSH_MASK			0xff
#define IMG_SPDIF_IN_CSH_SHIFT			0

#define IMG_SPDIF_IN_SOFT_RESET			0x28
#define IMG_SPDIF_IN_SOFT_RESET_MASK		BIT(0)

#define IMG_SPDIF_IN_ACLKGEN_START		0x2c
#define IMG_SPDIF_IN_ACLKGEN_NOM_MASK		0x3ff
#define IMG_SPDIF_IN_ACLKGEN_NOM_SHIFT		0
#define IMG_SPDIF_IN_ACLKGEN_HLD_MASK		0xffc00
#define IMG_SPDIF_IN_ACLKGEN_HLD_SHIFT		10
#define IMG_SPDIF_IN_ACLKGEN_TRK_MASK		0xff00000
#define IMG_SPDIF_IN_ACLKGEN_TRK_SHIFT		20

#define IMG_SPDIF_IN_NUM_ACLKGEN		4

struct img_spdif_in {
	spinlock_t lock;
	void __iomem *base;
	struct clk *clk_sys;
	struct snd_dmaengine_dai_dma_data dma_data;
	struct device *dev;
	unsigned int trk;
	bool multi_freq;
	int lock_acquire;
	int lock_release;
	unsigned int single_freq;
	unsigned int multi_freqs[IMG_SPDIF_IN_NUM_ACLKGEN];
	bool active;

	/* Write-only registers */
	unsigned int aclkgen_regs[IMG_SPDIF_IN_NUM_ACLKGEN];
};

static inline void img_spdif_in_writel(struct img_spdif_in *spdif,
					u32 val, u32 reg)
{
	writel(val, spdif->base + reg);
}

static inline u32 img_spdif_in_readl(struct img_spdif_in *spdif, u32 reg)
{
	return readl(spdif->base + reg);
}

static inline void img_spdif_in_aclkgen_writel(struct img_spdif_in *spdif,
						u32 index)
{
	img_spdif_in_writel(spdif, spdif->aclkgen_regs[index],
			IMG_SPDIF_IN_ACLKGEN_START + (index * 0x4));
}

static int img_spdif_in_check_max_rate(struct img_spdif_in *spdif,
		unsigned int sample_rate, unsigned long *actual_freq)
{
	unsigned long min_freq, freq_t;

	/* Clock rate must be at least 24x the bit rate */
	min_freq = sample_rate * 2 * 32 * 24;

	freq_t = clk_get_rate(spdif->clk_sys);

	if (freq_t < min_freq)
		return -EINVAL;

	*actual_freq = freq_t;

	return 0;
}

static int img_spdif_in_do_clkgen_calc(unsigned int rate, unsigned int *pnom,
		unsigned int *phld, unsigned long clk_rate)
{
	unsigned int ori, nom, hld;

	/*
	 * Calculate oversampling ratio, nominal phase increment and hold
	 * increment for the given rate / frequency
	 */

	if (!rate)
		return -EINVAL;

	ori = clk_rate / (rate * 64);

	if (!ori)
		return -EINVAL;

	nom = (4096 / ori) + 1;
	do
		hld = 4096 - (--nom * (ori - 1));
	while (hld < 120);

	*pnom = nom;
	*phld = hld;

	return 0;
}

static int img_spdif_in_do_clkgen_single(struct img_spdif_in *spdif,
		unsigned int rate)
{
	unsigned int nom, hld;
	unsigned long flags, clk_rate;
	int ret = 0;
	u32 reg;

	ret = img_spdif_in_check_max_rate(spdif, rate, &clk_rate);
	if (ret)
		return ret;

	ret = img_spdif_in_do_clkgen_calc(rate, &nom, &hld, clk_rate);
	if (ret)
		return ret;

	reg = (nom << IMG_SPDIF_IN_CLKGEN_NOM_SHIFT) &
		IMG_SPDIF_IN_CLKGEN_NOM_MASK;
	reg |= (hld << IMG_SPDIF_IN_CLKGEN_HLD_SHIFT) &
		IMG_SPDIF_IN_CLKGEN_HLD_MASK;

	spin_lock_irqsave(&spdif->lock, flags);

	if (spdif->active) {
		spin_unlock_irqrestore(&spdif->lock, flags);
		return -EBUSY;
	}

	img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CLKGEN);

	spdif->single_freq = rate;

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_do_clkgen_multi(struct img_spdif_in *spdif,
		unsigned int multi_freqs[])
{
	unsigned int nom, hld, rate, max_rate = 0;
	unsigned long flags, clk_rate;
	int i, ret = 0;
	u32 reg, trk_reg, temp_regs[IMG_SPDIF_IN_NUM_ACLKGEN];

	for (i = 0; i < IMG_SPDIF_IN_NUM_ACLKGEN; i++)
		if (multi_freqs[i] > max_rate)
			max_rate = multi_freqs[i];

	ret = img_spdif_in_check_max_rate(spdif, max_rate, &clk_rate);
	if (ret)
		return ret;

	for (i = 0; i < IMG_SPDIF_IN_NUM_ACLKGEN; i++) {
		rate = multi_freqs[i];

		ret = img_spdif_in_do_clkgen_calc(rate, &nom, &hld, clk_rate);
		if (ret)
			return ret;

		reg = (nom << IMG_SPDIF_IN_ACLKGEN_NOM_SHIFT) &
			IMG_SPDIF_IN_ACLKGEN_NOM_MASK;
		reg |= (hld << IMG_SPDIF_IN_ACLKGEN_HLD_SHIFT) &
			IMG_SPDIF_IN_ACLKGEN_HLD_MASK;
		temp_regs[i] = reg;
	}

	spin_lock_irqsave(&spdif->lock, flags);

	if (spdif->active) {
		spin_unlock_irqrestore(&spdif->lock, flags);
		return -EBUSY;
	}

	trk_reg = spdif->trk << IMG_SPDIF_IN_ACLKGEN_TRK_SHIFT;

	for (i = 0; i < IMG_SPDIF_IN_NUM_ACLKGEN; i++) {
		spdif->aclkgen_regs[i] = temp_regs[i] | trk_reg;
		img_spdif_in_aclkgen_writel(spdif, i);
	}

	spdif->multi_freq = true;
	spdif->multi_freqs[0] = multi_freqs[0];
	spdif->multi_freqs[1] = multi_freqs[1];
	spdif->multi_freqs[2] = multi_freqs[2];
	spdif->multi_freqs[3] = multi_freqs[3];

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_iec958_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int img_spdif_in_get_status_mask(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	ucontrol->value.iec958.status[4] = 0xff;

	return 0;
}

static int img_spdif_in_get_status(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	u32 reg;

	reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CSL);
	ucontrol->value.iec958.status[0] = reg & 0xff;
	ucontrol->value.iec958.status[1] = (reg >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (reg >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (reg >> 24) & 0xff;
	reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CSH);
	ucontrol->value.iec958.status[4] = (reg & IMG_SPDIF_IN_CSH_MASK)
		>> IMG_SPDIF_IN_CSH_SHIFT;

	return 0;
}

static int img_spdif_in_info_multi_freq(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = IMG_SPDIF_IN_NUM_ACLKGEN;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = LONG_MAX;

	return 0;
}

static int img_spdif_in_get_multi_freq(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;

	spin_lock_irqsave(&spdif->lock, flags);
	if (spdif->multi_freq) {
		ucontrol->value.integer.value[0] = spdif->multi_freqs[0];
		ucontrol->value.integer.value[1] = spdif->multi_freqs[1];
		ucontrol->value.integer.value[2] = spdif->multi_freqs[2];
		ucontrol->value.integer.value[3] = spdif->multi_freqs[3];
	} else {
		ucontrol->value.integer.value[0] = 0;
		ucontrol->value.integer.value[1] = 0;
		ucontrol->value.integer.value[2] = 0;
		ucontrol->value.integer.value[3] = 0;
	}
	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_set_multi_freq(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int multi_freqs[IMG_SPDIF_IN_NUM_ACLKGEN];
	bool multi_freq;
	unsigned long flags;

	if ((ucontrol->value.integer.value[0] == 0) &&
			(ucontrol->value.integer.value[1] == 0) &&
			(ucontrol->value.integer.value[2] == 0) &&
			(ucontrol->value.integer.value[3] == 0)) {
		multi_freq = false;
	} else {
		multi_freqs[0] = ucontrol->value.integer.value[0];
		multi_freqs[1] = ucontrol->value.integer.value[1];
		multi_freqs[2] = ucontrol->value.integer.value[2];
		multi_freqs[3] = ucontrol->value.integer.value[3];
		multi_freq = true;
	}

	if (multi_freq)
		return img_spdif_in_do_clkgen_multi(spdif, multi_freqs);

	spin_lock_irqsave(&spdif->lock, flags);

	if (spdif->active) {
		spin_unlock_irqrestore(&spdif->lock, flags);
		return -EBUSY;
	}

	spdif->multi_freq = false;

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_info_lock_freq(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = LONG_MAX;

	return 0;
}

static int img_spdif_in_get_lock_freq(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *uc)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	u32 reg;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&spdif->lock, flags);

	reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_STATUS);
	if (reg & IMG_SPDIF_IN_STATUS_LOCK_MASK) {
		if (spdif->multi_freq) {
			i = ((reg & IMG_SPDIF_IN_STATUS_SAM_MASK) >>
					IMG_SPDIF_IN_STATUS_SAM_SHIFT) - 1;
			uc->value.integer.value[0] = spdif->multi_freqs[i];
		} else {
			uc->value.integer.value[0] = spdif->single_freq;
		}
	} else {
		uc->value.integer.value[0] = 0;
	}

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_info_trk(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;

	return 0;
}

static int img_spdif_in_get_trk(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.integer.value[0] = spdif->trk;

	return 0;
}

static int img_spdif_in_set_trk(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;
	int i;
	u32 reg;

	spin_lock_irqsave(&spdif->lock, flags);

	if (spdif->active) {
		spin_unlock_irqrestore(&spdif->lock, flags);
		return -EBUSY;
	}

	spdif->trk = ucontrol->value.integer.value[0];

	reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CTL);
	reg &= ~IMG_SPDIF_IN_CTL_TRK_MASK;
	reg |= spdif->trk << IMG_SPDIF_IN_CTL_TRK_SHIFT;
	img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CTL);

	for (i = 0; i < IMG_SPDIF_IN_NUM_ACLKGEN; i++) {
		spdif->aclkgen_regs[i] = (spdif->aclkgen_regs[i] &
			~IMG_SPDIF_IN_ACLKGEN_TRK_MASK) |
			(spdif->trk << IMG_SPDIF_IN_ACLKGEN_TRK_SHIFT);

		img_spdif_in_aclkgen_writel(spdif, i);
	}

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_info_lock(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = -128;
	uinfo->value.integer.max = 127;

	return 0;
}

static int img_spdif_in_get_lock_acquire(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.integer.value[0] = spdif->lock_acquire;

	return 0;
}

static int img_spdif_in_set_lock_acquire(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&spdif->lock, flags);

	if (spdif->active) {
		spin_unlock_irqrestore(&spdif->lock, flags);
		return -EBUSY;
	}

	spdif->lock_acquire = ucontrol->value.integer.value[0];

	reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CTL);
	reg &= ~IMG_SPDIF_IN_CTL_LOCKHI_MASK;
	reg |= (spdif->lock_acquire << IMG_SPDIF_IN_CTL_LOCKHI_SHIFT) &
		IMG_SPDIF_IN_CTL_LOCKHI_MASK;
	img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CTL);

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static int img_spdif_in_get_lock_release(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.integer.value[0] = spdif->lock_release;

	return 0;
}

static int img_spdif_in_set_lock_release(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&spdif->lock, flags);

	if (spdif->active) {
		spin_unlock_irqrestore(&spdif->lock, flags);
		return -EBUSY;
	}

	spdif->lock_release = ucontrol->value.integer.value[0];

	reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CTL);
	reg &= ~IMG_SPDIF_IN_CTL_LOCKLO_MASK;
	reg |= (spdif->lock_release << IMG_SPDIF_IN_CTL_LOCKLO_SHIFT) &
		IMG_SPDIF_IN_CTL_LOCKLO_MASK;
	img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CTL);

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
}

static struct snd_kcontrol_new img_spdif_in_controls[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, MASK),
		.info = img_spdif_in_iec958_info,
		.get = img_spdif_in_get_status_mask
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.info = img_spdif_in_iec958_info,
		.get = img_spdif_in_get_status
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SPDIF In Multi Frequency Acquire",
		.info = img_spdif_in_info_multi_freq,
		.get = img_spdif_in_get_multi_freq,
		.put = img_spdif_in_set_multi_freq
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SPDIF In Lock Frequency",
		.info = img_spdif_in_info_lock_freq,
		.get = img_spdif_in_get_lock_freq
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SPDIF In Lock TRK",
		.info = img_spdif_in_info_trk,
		.get = img_spdif_in_get_trk,
		.put = img_spdif_in_set_trk
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SPDIF In Lock Acquire Threshold",
		.info = img_spdif_in_info_lock,
		.get = img_spdif_in_get_lock_acquire,
		.put = img_spdif_in_set_lock_acquire
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SPDIF In Lock Release Threshold",
		.info = img_spdif_in_info_lock,
		.get = img_spdif_in_get_lock_release,
		.put = img_spdif_in_set_lock_release
	}
};

static int img_spdif_in_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	unsigned long flags;
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(dai);
	int ret = 0;
	u32 reg;

	spin_lock_irqsave(&spdif->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CTL);
		if (spdif->multi_freq)
			reg &= ~IMG_SPDIF_IN_CTL_SRD_MASK;
		else
			reg |= (1UL << IMG_SPDIF_IN_CTL_SRD_SHIFT);
		reg |= IMG_SPDIF_IN_CTL_SRT_MASK;
		img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CTL);
		spdif->active = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		reg = img_spdif_in_readl(spdif, IMG_SPDIF_IN_CTL);
		reg &= ~IMG_SPDIF_IN_CTL_SRT_MASK;
		img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CTL);
		spdif->active = false;
		break;
	default:
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&spdif->lock, flags);

	return ret;
}

static int img_spdif_in_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(dai);
	unsigned int rate, channels;
	snd_pcm_format_t format;

	rate = params_rate(params);
	channels = params_channels(params);
	format = params_format(params);

	if (format != SNDRV_PCM_FORMAT_S32_LE)
		return -EINVAL;

	if (channels != 2)
		return -EINVAL;

	return img_spdif_in_do_clkgen_single(spdif, rate);
}

static const struct snd_soc_dai_ops img_spdif_in_dai_ops = {
	.trigger = img_spdif_in_trigger,
	.hw_params = img_spdif_in_hw_params
};

static int img_spdif_in_dai_probe(struct snd_soc_dai *dai)
{
	struct img_spdif_in *spdif = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, NULL, &spdif->dma_data);

	snd_soc_add_dai_controls(dai, img_spdif_in_controls,
			ARRAY_SIZE(img_spdif_in_controls));

	return 0;
}

static struct snd_soc_dai_driver img_spdif_in_dai = {
	.probe = img_spdif_in_dai_probe,
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE
	},
	.ops = &img_spdif_in_dai_ops
};

static const struct snd_soc_component_driver img_spdif_in_component = {
	.name = "img-spdif-in"
};

static int img_spdif_in_probe(struct platform_device *pdev)
{
	struct img_spdif_in *spdif;
	struct resource *res;
	void __iomem *base;
	int ret;
	struct reset_control *rst;
	u32 reg;
	struct device *dev = &pdev->dev;

	spdif = devm_kzalloc(&pdev->dev, sizeof(*spdif), GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	platform_set_drvdata(pdev, spdif);

	spdif->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	spdif->base = base;

	spdif->clk_sys = devm_clk_get(dev, "sys");
	if (IS_ERR(spdif->clk_sys)) {
		if (PTR_ERR(spdif->clk_sys) != -EPROBE_DEFER)
			dev_err(dev, "Failed to acquire clock 'sys'\n");
		return PTR_ERR(spdif->clk_sys);
	}

	ret = clk_prepare_enable(spdif->clk_sys);
	if (ret)
		return ret;

	rst = devm_reset_control_get(&pdev->dev, "rst");
	if (IS_ERR(rst)) {
		if (PTR_ERR(rst) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_clk_disable;
		}
		dev_dbg(dev, "No top level reset found\n");
		img_spdif_in_writel(spdif, IMG_SPDIF_IN_SOFT_RESET_MASK,
				IMG_SPDIF_IN_SOFT_RESET);
		img_spdif_in_writel(spdif, 0, IMG_SPDIF_IN_SOFT_RESET);
	} else {
		reset_control_assert(rst);
		reset_control_deassert(rst);
	}

	spin_lock_init(&spdif->lock);

	spdif->dma_data.addr = res->start + IMG_SPDIF_IN_RX_FIFO_OFFSET;
	spdif->dma_data.addr_width = 4;
	spdif->dma_data.maxburst = 4;
	spdif->trk = 0x80;
	spdif->lock_acquire = 4;
	spdif->lock_release = -128;

	reg = (spdif->lock_acquire << IMG_SPDIF_IN_CTL_LOCKHI_SHIFT) &
		IMG_SPDIF_IN_CTL_LOCKHI_MASK;
	reg |= (spdif->lock_release << IMG_SPDIF_IN_CTL_LOCKLO_SHIFT) &
		IMG_SPDIF_IN_CTL_LOCKLO_MASK;
	reg |= (spdif->trk << IMG_SPDIF_IN_CTL_TRK_SHIFT) &
		IMG_SPDIF_IN_CTL_TRK_MASK;
	img_spdif_in_writel(spdif, reg, IMG_SPDIF_IN_CTL);

	ret = devm_snd_soc_register_component(&pdev->dev,
			&img_spdif_in_component, &img_spdif_in_dai, 1);
	if (ret)
		goto err_clk_disable;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		goto err_clk_disable;

	return 0;

err_clk_disable:
	clk_disable_unprepare(spdif->clk_sys);

	return ret;
}

static int img_spdif_in_dev_remove(struct platform_device *pdev)
{
	struct img_spdif_in *spdif = platform_get_drvdata(pdev);

	clk_disable_unprepare(spdif->clk_sys);

	return 0;
}

static const struct of_device_id img_spdif_in_of_match[] = {
	{ .compatible = "img,spdif-in" },
	{}
};
MODULE_DEVICE_TABLE(of, img_spdif_in_of_match);

static struct platform_driver img_spdif_in_driver = {
	.driver = {
		.name = "img-spdif-in",
		.of_match_table = img_spdif_in_of_match
	},
	.probe = img_spdif_in_probe,
	.remove = img_spdif_in_dev_remove
};
module_platform_driver(img_spdif_in_driver);

MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_DESCRIPTION("IMG SPDIF Input driver");
MODULE_LICENSE("GPL v2");
