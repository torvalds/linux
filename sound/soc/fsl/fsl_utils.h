/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freescale ALSA SoC Machine driver utility
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 */

#ifndef _FSL_UTILS_H
#define _FSL_UTILS_H

#define DAI_NAME_SIZE	32

struct snd_soc_dai_link;
struct device_node;

int fsl_asoc_get_dma_channel(struct device_node *ssi_np, const char *name,
			     struct snd_soc_dai_link *dai,
			     unsigned int *dma_channel_id,
			     unsigned int *dma_id);

void fsl_asoc_get_pll_clocks(struct device *dev, struct clk **pll8k_clk,
			     struct clk **pll11k_clk);

void fsl_asoc_reparent_pll_clocks(struct device *dev, struct clk *clk,
				  struct clk *pll8k_clk,
				  struct clk *pll11k_clk, u64 ratio);

void fsl_asoc_constrain_rates(struct snd_pcm_hw_constraint_list *target_constr,
			      const struct snd_pcm_hw_constraint_list *original_constr,
			      struct clk *pll8k_clk, struct clk *pll11k_clk,
			      struct clk *ext_clk, int *target_rates);

/* Similar to SOC_SINGLE_XR_SX, but it is for read only registers. */
#define FSL_ASOC_SINGLE_XR_SX_EXT_RO(xname, xregbase, xregcount, xnbits, \
				xmin, xmax, xinvert, xhandler_get) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READ |		\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE,		\
	.info = snd_soc_info_xr_sx, .get = xhandler_get, \
	.private_value = (unsigned long)&(struct soc_mreg_control) \
		{.regbase = xregbase, .regcount = xregcount, .nbits = xnbits, \
		.invert = xinvert, .min = xmin, .max = xmax} }

/* Similar to SOC_SINGLE_EXT, but it is for volatile register. */
#define FSL_ASOC_SINGLE_EXT(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_VOLATILE |	\
		  SNDRV_CTL_ELEM_ACCESS_READWRITE,	\
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, 0, xmax, xinvert, 0) }

#define FSL_ASOC_ENUM_EXT(xname, xenum, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_VOLATILE |	\
		  SNDRV_CTL_ELEM_ACCESS_READWRITE,	\
	.info = snd_soc_info_enum_double, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&xenum }

int fsl_asoc_get_xr_sx(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);

int fsl_asoc_put_xr_sx(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);

int fsl_asoc_get_enum_double(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol);

int fsl_asoc_put_enum_double(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol);

int fsl_asoc_get_volsw(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);

int fsl_asoc_put_volsw(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);

#endif /* _FSL_UTILS_H */
