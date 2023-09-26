// SPDX-License-Identifier: GPL-2.0
/*
 * McBSP Sidetone support
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com>
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "omap-mcbsp.h"
#include "omap-mcbsp-priv.h"

/* OMAP3 sidetone control registers */
#define OMAP_ST_REG_REV		0x00
#define OMAP_ST_REG_SYSCONFIG	0x10
#define OMAP_ST_REG_IRQSTATUS	0x18
#define OMAP_ST_REG_IRQENABLE	0x1C
#define OMAP_ST_REG_SGAINCR	0x24
#define OMAP_ST_REG_SFIRCR	0x28
#define OMAP_ST_REG_SSELCR	0x2C

/********************** McBSP SSELCR bit definitions ***********************/
#define SIDETONEEN		BIT(10)

/********************** McBSP Sidetone SYSCONFIG bit definitions ***********/
#define ST_AUTOIDLE		BIT(0)

/********************** McBSP Sidetone SGAINCR bit definitions *************/
#define ST_CH0GAIN(value)	((value) & 0xffff)	/* Bits 0:15 */
#define ST_CH1GAIN(value)	(((value) & 0xffff) << 16) /* Bits 16:31 */

/********************** McBSP Sidetone SFIRCR bit definitions **************/
#define ST_FIRCOEFF(value)	((value) & 0xffff)	/* Bits 0:15 */

/********************** McBSP Sidetone SSELCR bit definitions **************/
#define ST_SIDETONEEN		BIT(0)
#define ST_COEFFWREN		BIT(1)
#define ST_COEFFWRDONE		BIT(2)

struct omap_mcbsp_st_data {
	void __iomem *io_base_st;
	struct clk *mcbsp_iclk;
	bool running;
	bool enabled;
	s16 taps[128];	/* Sidetone filter coefficients */
	int nr_taps;	/* Number of filter coefficients in use */
	s16 ch0gain;
	s16 ch1gain;
};

static void omap_mcbsp_st_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	writel_relaxed(val, mcbsp->st_data->io_base_st + reg);
}

static int omap_mcbsp_st_read(struct omap_mcbsp *mcbsp, u16 reg)
{
	return readl_relaxed(mcbsp->st_data->io_base_st + reg);
}

#define MCBSP_ST_READ(mcbsp, reg) omap_mcbsp_st_read(mcbsp, OMAP_ST_REG_##reg)
#define MCBSP_ST_WRITE(mcbsp, reg, val) \
			omap_mcbsp_st_write(mcbsp, OMAP_ST_REG_##reg, val)

static void omap_mcbsp_st_on(struct omap_mcbsp *mcbsp)
{
	unsigned int w;

	if (mcbsp->pdata->force_ick_on)
		mcbsp->pdata->force_ick_on(mcbsp->st_data->mcbsp_iclk, true);

	/* Disable Sidetone clock auto-gating for normal operation */
	w = MCBSP_ST_READ(mcbsp, SYSCONFIG);
	MCBSP_ST_WRITE(mcbsp, SYSCONFIG, w & ~(ST_AUTOIDLE));

	/* Enable McBSP Sidetone */
	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w | SIDETONEEN);

	/* Enable Sidetone from Sidetone Core */
	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w | ST_SIDETONEEN);
}

static void omap_mcbsp_st_off(struct omap_mcbsp *mcbsp)
{
	unsigned int w;

	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w & ~(ST_SIDETONEEN));

	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w & ~(SIDETONEEN));

	/* Enable Sidetone clock auto-gating to reduce power consumption */
	w = MCBSP_ST_READ(mcbsp, SYSCONFIG);
	MCBSP_ST_WRITE(mcbsp, SYSCONFIG, w | ST_AUTOIDLE);

	if (mcbsp->pdata->force_ick_on)
		mcbsp->pdata->force_ick_on(mcbsp->st_data->mcbsp_iclk, false);
}

static void omap_mcbsp_st_fir_write(struct omap_mcbsp *mcbsp, s16 *fir)
{
	u16 val, i;

	val = MCBSP_ST_READ(mcbsp, SSELCR);

	if (val & ST_COEFFWREN)
		MCBSP_ST_WRITE(mcbsp, SSELCR, val & ~(ST_COEFFWREN));

	MCBSP_ST_WRITE(mcbsp, SSELCR, val | ST_COEFFWREN);

	for (i = 0; i < 128; i++)
		MCBSP_ST_WRITE(mcbsp, SFIRCR, fir[i]);

	i = 0;

	val = MCBSP_ST_READ(mcbsp, SSELCR);
	while (!(val & ST_COEFFWRDONE) && (++i < 1000))
		val = MCBSP_ST_READ(mcbsp, SSELCR);

	MCBSP_ST_WRITE(mcbsp, SSELCR, val & ~(ST_COEFFWREN));

	if (i == 1000)
		dev_err(mcbsp->dev, "McBSP FIR load error!\n");
}

static void omap_mcbsp_st_chgain(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	MCBSP_ST_WRITE(mcbsp, SGAINCR, ST_CH0GAIN(st_data->ch0gain) |
		       ST_CH1GAIN(st_data->ch1gain));
}

static int omap_mcbsp_st_set_chgain(struct omap_mcbsp *mcbsp, int channel,
				    s16 chgain)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int ret = 0;

	if (!st_data)
		return -ENOENT;

	spin_lock_irq(&mcbsp->lock);
	if (channel == 0)
		st_data->ch0gain = chgain;
	else if (channel == 1)
		st_data->ch1gain = chgain;
	else
		ret = -EINVAL;

	if (st_data->enabled)
		omap_mcbsp_st_chgain(mcbsp);
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}

static int omap_mcbsp_st_get_chgain(struct omap_mcbsp *mcbsp, int channel,
				    s16 *chgain)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int ret = 0;

	if (!st_data)
		return -ENOENT;

	spin_lock_irq(&mcbsp->lock);
	if (channel == 0)
		*chgain = st_data->ch0gain;
	else if (channel == 1)
		*chgain = st_data->ch1gain;
	else
		ret = -EINVAL;
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}

static int omap_mcbsp_st_enable(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;

	spin_lock_irq(&mcbsp->lock);
	st_data->enabled = 1;
	omap_mcbsp_st_start(mcbsp);
	spin_unlock_irq(&mcbsp->lock);

	return 0;
}

static int omap_mcbsp_st_disable(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int ret = 0;

	if (!st_data)
		return -ENODEV;

	spin_lock_irq(&mcbsp->lock);
	omap_mcbsp_st_stop(mcbsp);
	st_data->enabled = 0;
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}

static int omap_mcbsp_st_is_enabled(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;

	return st_data->enabled;
}

static ssize_t st_taps_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	ssize_t status = 0;
	int i;

	spin_lock_irq(&mcbsp->lock);
	for (i = 0; i < st_data->nr_taps; i++)
		status += sysfs_emit_at(buf, status, (i ? ", %d" : "%d"),
					st_data->taps[i]);
	if (i)
		status += sysfs_emit_at(buf, status, "\n");
	spin_unlock_irq(&mcbsp->lock);

	return status;
}

static ssize_t st_taps_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int val, tmp, status, i = 0;

	spin_lock_irq(&mcbsp->lock);
	memset(st_data->taps, 0, sizeof(st_data->taps));
	st_data->nr_taps = 0;

	do {
		status = sscanf(buf, "%d%n", &val, &tmp);
		if (status < 0 || status == 0) {
			size = -EINVAL;
			goto out;
		}
		if (val < -32768 || val > 32767) {
			size = -EINVAL;
			goto out;
		}
		st_data->taps[i++] = val;
		buf += tmp;
		if (*buf != ',')
			break;
		buf++;
	} while (1);

	st_data->nr_taps = i;

out:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR_RW(st_taps);

static const struct attribute *sidetone_attrs[] = {
	&dev_attr_st_taps.attr,
	NULL,
};

static const struct attribute_group sidetone_attr_group = {
	.attrs = (struct attribute **)sidetone_attrs,
};

int omap_mcbsp_st_start(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data->enabled && !st_data->running) {
		omap_mcbsp_st_fir_write(mcbsp, st_data->taps);
		omap_mcbsp_st_chgain(mcbsp);

		if (!mcbsp->free) {
			omap_mcbsp_st_on(mcbsp);
			st_data->running = 1;
		}
	}

	return 0;
}

int omap_mcbsp_st_stop(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data->running) {
		if (!mcbsp->free) {
			omap_mcbsp_st_off(mcbsp);
			st_data->running = 0;
		}
	}

	return 0;
}

int omap_mcbsp_st_init(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);
	struct omap_mcbsp_st_data *st_data;
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sidetone");
	if (!res)
		return 0;

	st_data = devm_kzalloc(mcbsp->dev, sizeof(*mcbsp->st_data), GFP_KERNEL);
	if (!st_data)
		return -ENOMEM;

	st_data->mcbsp_iclk = devm_clk_get(mcbsp->dev, "ick");
	if (IS_ERR(st_data->mcbsp_iclk)) {
		dev_warn(mcbsp->dev,
			 "Failed to get ick, sidetone might be broken\n");
		st_data->mcbsp_iclk = NULL;
	}

	st_data->io_base_st = devm_ioremap(mcbsp->dev, res->start,
					   resource_size(res));
	if (!st_data->io_base_st)
		return -ENOMEM;

	ret = devm_device_add_group(mcbsp->dev, &sidetone_attr_group);
	if (ret)
		return ret;

	mcbsp->st_data = st_data;

	return 0;
}

static int omap_mcbsp_st_info_volsw(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int max = mc->max;
	int min = mc->min;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = min;
	uinfo->value.integer.max = max;
	return 0;
}

#define OMAP_MCBSP_ST_CHANNEL_VOLUME(channel)				\
static int								\
omap_mcbsp_set_st_ch##channel##_volume(struct snd_kcontrol *kc,		\
				       struct snd_ctl_elem_value *uc)	\
{									\
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kc);		\
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);	\
	struct soc_mixer_control *mc =					\
		(struct soc_mixer_control *)kc->private_value;		\
	int max = mc->max;						\
	int min = mc->min;						\
	int val = uc->value.integer.value[0];				\
									\
	if (val < min || val > max)					\
		return -EINVAL;						\
									\
	/* OMAP McBSP implementation uses index values 0..4 */		\
	return omap_mcbsp_st_set_chgain(mcbsp, channel, val);		\
}									\
									\
static int								\
omap_mcbsp_get_st_ch##channel##_volume(struct snd_kcontrol *kc,		\
				       struct snd_ctl_elem_value *uc)	\
{									\
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kc);		\
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);	\
	s16 chgain;							\
									\
	if (omap_mcbsp_st_get_chgain(mcbsp, channel, &chgain))		\
		return -EAGAIN;						\
									\
	uc->value.integer.value[0] = chgain;				\
	return 0;							\
}

OMAP_MCBSP_ST_CHANNEL_VOLUME(0)
OMAP_MCBSP_ST_CHANNEL_VOLUME(1)

static int omap_mcbsp_st_put_mode(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	u8 value = ucontrol->value.integer.value[0];

	if (value == omap_mcbsp_st_is_enabled(mcbsp))
		return 0;

	if (value)
		omap_mcbsp_st_enable(mcbsp);
	else
		omap_mcbsp_st_disable(mcbsp);

	return 1;
}

static int omap_mcbsp_st_get_mode(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.integer.value[0] = omap_mcbsp_st_is_enabled(mcbsp);
	return 0;
}

#define OMAP_MCBSP_SOC_SINGLE_S16_EXT(xname, xmin, xmax,		\
				      xhandler_get, xhandler_put)	\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,		\
	.info = omap_mcbsp_st_info_volsw,				\
	.get = xhandler_get, .put = xhandler_put,			\
	.private_value = (unsigned long)&(struct soc_mixer_control)	\
	{.min = xmin, .max = xmax} }

#define OMAP_MCBSP_ST_CONTROLS(port)					  \
static const struct snd_kcontrol_new omap_mcbsp##port##_st_controls[] = { \
SOC_SINGLE_EXT("McBSP" #port " Sidetone Switch", 1, 0, 1, 0,		  \
	       omap_mcbsp_st_get_mode, omap_mcbsp_st_put_mode),		  \
OMAP_MCBSP_SOC_SINGLE_S16_EXT("McBSP" #port " Sidetone Channel 0 Volume", \
			      -32768, 32767,				  \
			      omap_mcbsp_get_st_ch0_volume,		  \
			      omap_mcbsp_set_st_ch0_volume),		  \
OMAP_MCBSP_SOC_SINGLE_S16_EXT("McBSP" #port " Sidetone Channel 1 Volume", \
			      -32768, 32767,				  \
			      omap_mcbsp_get_st_ch1_volume,		  \
			      omap_mcbsp_set_st_ch1_volume),		  \
}

OMAP_MCBSP_ST_CONTROLS(2);
OMAP_MCBSP_ST_CONTROLS(3);

int omap_mcbsp_st_add_controls(struct snd_soc_pcm_runtime *rtd, int port_id)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);

	if (!mcbsp->st_data) {
		dev_warn(mcbsp->dev, "No sidetone data for port\n");
		return 0;
	}

	switch (port_id) {
	case 2: /* McBSP 2 */
		return snd_soc_add_dai_controls(cpu_dai,
					omap_mcbsp2_st_controls,
					ARRAY_SIZE(omap_mcbsp2_st_controls));
	case 3: /* McBSP 3 */
		return snd_soc_add_dai_controls(cpu_dai,
					omap_mcbsp3_st_controls,
					ARRAY_SIZE(omap_mcbsp3_st_controls));
	default:
		dev_err(mcbsp->dev, "Port %d not supported\n", port_id);
		break;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(omap_mcbsp_st_add_controls);
