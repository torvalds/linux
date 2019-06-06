/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/soc.h -- ALSA SoC Layer
 *
 * Author:	Liam Girdwood
 * Created:	Aug 11th 2005
 * Copyright:	Wolfson Microelectronics. PLC.
 */

#ifndef __LINUX_SND_SOC_H
#define __LINUX_SND_SOC_H

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/log2.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/compress_driver.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>

/*
 * Convenience kcontrol builders
 */
#define SOC_DOUBLE_VALUE(xreg, shift_left, shift_right, xmax, xinvert, xautodisable) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .rreg = xreg, .shift = shift_left, \
	.rshift = shift_right, .max = xmax, .platform_max = xmax, \
	.invert = xinvert, .autodisable = xautodisable})
#define SOC_DOUBLE_S_VALUE(xreg, shift_left, shift_right, xmin, xmax, xsign_bit, xinvert, xautodisable) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .rreg = xreg, .shift = shift_left, \
	.rshift = shift_right, .min = xmin, .max = xmax, .platform_max = xmax, \
	.sign_bit = xsign_bit, .invert = xinvert, .autodisable = xautodisable})
#define SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert, xautodisable) \
	SOC_DOUBLE_VALUE(xreg, xshift, xshift, xmax, xinvert, xautodisable)
#define SOC_SINGLE_VALUE_EXT(xreg, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .max = xmax, .platform_max = xmax, .invert = xinvert})
#define SOC_DOUBLE_R_VALUE(xlreg, xrreg, xshift, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xlreg, .rreg = xrreg, .shift = xshift, .rshift = xshift, \
	.max = xmax, .platform_max = xmax, .invert = xinvert})
#define SOC_DOUBLE_R_S_VALUE(xlreg, xrreg, xshift, xmin, xmax, xsign_bit, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xlreg, .rreg = xrreg, .shift = xshift, .rshift = xshift, \
	.max = xmax, .min = xmin, .platform_max = xmax, .sign_bit = xsign_bit, \
	.invert = xinvert})
#define SOC_DOUBLE_R_RANGE_VALUE(xlreg, xrreg, xshift, xmin, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xlreg, .rreg = xrreg, .shift = xshift, .rshift = xshift, \
	.min = xmin, .max = xmax, .platform_max = xmax, .invert = xinvert})
#define SOC_SINGLE(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, max, invert, 0) }
#define SOC_SINGLE_RANGE(xname, xreg, xshift, xmin, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw_range, .get = snd_soc_get_volsw_range, \
	.put = snd_soc_put_volsw_range, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .rreg = xreg, .shift = xshift, \
		 .rshift = xshift,  .min = xmin, .max = xmax, \
		 .platform_max = xmax, .invert = xinvert} }
#define SOC_SINGLE_TLV(xname, reg, shift, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, max, invert, 0) }
#define SOC_SINGLE_SX_TLV(xname, xreg, xshift, xmin, xmax, tlv_array) \
{       .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
	SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p  = (tlv_array),\
	.info = snd_soc_info_volsw_sx, \
	.get = snd_soc_get_volsw_sx,\
	.put = snd_soc_put_volsw_sx, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .rreg = xreg, \
		.shift = xshift, .rshift = xshift, \
		.max = xmax, .min = xmin} }
#define SOC_SINGLE_RANGE_TLV(xname, xreg, xshift, xmin, xmax, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_range, \
	.get = snd_soc_get_volsw_range, .put = snd_soc_put_volsw_range, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .rreg = xreg, .shift = xshift, \
		 .rshift = xshift, .min = xmin, .max = xmax, \
		 .platform_max = xmax, .invert = xinvert} }
#define SOC_DOUBLE(xname, reg, shift_left, shift_right, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw, \
	.put = snd_soc_put_volsw, \
	.private_value = SOC_DOUBLE_VALUE(reg, shift_left, shift_right, \
					  max, invert, 0) }
#define SOC_DOUBLE_STS(xname, reg, shift_left, shift_right, max, invert) \
{									\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),		\
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,		\
	.access = SNDRV_CTL_ELEM_ACCESS_READ |				\
		SNDRV_CTL_ELEM_ACCESS_VOLATILE,				\
	.private_value = SOC_DOUBLE_VALUE(reg, shift_left, shift_right,	\
					  max, invert, 0) }
#define SOC_DOUBLE_R(xname, reg_left, reg_right, xshift, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_get_volsw, .put = snd_soc_put_volsw, \
	.private_value = SOC_DOUBLE_R_VALUE(reg_left, reg_right, xshift, \
					    xmax, xinvert) }
#define SOC_DOUBLE_R_RANGE(xname, reg_left, reg_right, xshift, xmin, \
			   xmax, xinvert)		\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw_range, \
	.get = snd_soc_get_volsw_range, .put = snd_soc_put_volsw_range, \
	.private_value = SOC_DOUBLE_R_RANGE_VALUE(reg_left, reg_right, \
					    xshift, xmin, xmax, xinvert) }
#define SOC_DOUBLE_TLV(xname, reg, shift_left, shift_right, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw, \
	.put = snd_soc_put_volsw, \
	.private_value = SOC_DOUBLE_VALUE(reg, shift_left, shift_right, \
					  max, invert, 0) }
#define SOC_DOUBLE_R_TLV(xname, reg_left, reg_right, xshift, xmax, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_get_volsw, .put = snd_soc_put_volsw, \
	.private_value = SOC_DOUBLE_R_VALUE(reg_left, reg_right, xshift, \
					    xmax, xinvert) }
#define SOC_DOUBLE_R_RANGE_TLV(xname, reg_left, reg_right, xshift, xmin, \
			       xmax, xinvert, tlv_array)		\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_range, \
	.get = snd_soc_get_volsw_range, .put = snd_soc_put_volsw_range, \
	.private_value = SOC_DOUBLE_R_RANGE_VALUE(reg_left, reg_right, \
					    xshift, xmin, xmax, xinvert) }
#define SOC_DOUBLE_R_SX_TLV(xname, xreg, xrreg, xshift, xmin, xmax, tlv_array) \
{       .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
	SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p  = (tlv_array), \
	.info = snd_soc_info_volsw_sx, \
	.get = snd_soc_get_volsw_sx, \
	.put = snd_soc_put_volsw_sx, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .rreg = xrreg, \
		.shift = xshift, .rshift = xshift, \
		.max = xmax, .min = xmin} }
#define SOC_DOUBLE_R_S_TLV(xname, reg_left, reg_right, xshift, xmin, xmax, xsign_bit, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_get_volsw, .put = snd_soc_put_volsw, \
	.private_value = SOC_DOUBLE_R_S_VALUE(reg_left, reg_right, xshift, \
					    xmin, xmax, xsign_bit, xinvert) }
#define SOC_SINGLE_S8_TLV(xname, xreg, xmin, xmax, tlv_array) \
{	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		  SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p  = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .rreg = xreg,  \
	 .min = xmin, .max = xmax, .platform_max = xmax, \
	.sign_bit = 7,} }
#define SOC_DOUBLE_S8_TLV(xname, xreg, xmin, xmax, tlv_array) \
{	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		  SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p  = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value = SOC_DOUBLE_S_VALUE(xreg, 0, 8, xmin, xmax, 7, 0, 0) }
#define SOC_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xitems, xtexts) \
{	.reg = xreg, .shift_l = xshift_l, .shift_r = xshift_r, \
	.items = xitems, .texts = xtexts, \
	.mask = xitems ? roundup_pow_of_two(xitems) - 1 : 0}
#define SOC_ENUM_SINGLE(xreg, xshift, xitems, xtexts) \
	SOC_ENUM_DOUBLE(xreg, xshift, xshift, xitems, xtexts)
#define SOC_ENUM_SINGLE_EXT(xitems, xtexts) \
{	.items = xitems, .texts = xtexts }
#define SOC_VALUE_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xmask, xitems, xtexts, xvalues) \
{	.reg = xreg, .shift_l = xshift_l, .shift_r = xshift_r, \
	.mask = xmask, .items = xitems, .texts = xtexts, .values = xvalues}
#define SOC_VALUE_ENUM_SINGLE(xreg, xshift, xmask, xitems, xtexts, xvalues) \
	SOC_VALUE_ENUM_DOUBLE(xreg, xshift, xshift, xmask, xitems, xtexts, xvalues)
#define SOC_VALUE_ENUM_SINGLE_AUTODISABLE(xreg, xshift, xmask, xitems, xtexts, xvalues) \
{	.reg = xreg, .shift_l = xshift, .shift_r = xshift, \
	.mask = xmask, .items = xitems, .texts = xtexts, \
	.values = xvalues, .autodisable = 1}
#define SOC_ENUM_SINGLE_VIRT(xitems, xtexts) \
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, xitems, xtexts)
#define SOC_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,\
	.info = snd_soc_info_enum_double, \
	.get = snd_soc_get_enum_double, .put = snd_soc_put_enum_double, \
	.private_value = (unsigned long)&xenum }
#define SOC_SINGLE_EXT(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert, 0) }
#define SOC_DOUBLE_EXT(xname, reg, shift_left, shift_right, max, invert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = \
		SOC_DOUBLE_VALUE(reg, shift_left, shift_right, max, invert, 0) }
#define SOC_DOUBLE_R_EXT(xname, reg_left, reg_right, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_DOUBLE_R_VALUE(reg_left, reg_right, xshift, \
					    xmax, xinvert) }
#define SOC_SINGLE_EXT_TLV(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert, 0) }
#define SOC_SINGLE_RANGE_EXT_TLV(xname, xreg, xshift, xmin, xmax, xinvert, \
				 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_range, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .rreg = xreg, .shift = xshift, \
		 .rshift = xshift, .min = xmin, .max = xmax, \
		 .platform_max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_EXT_TLV(xname, xreg, shift_left, shift_right, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		 SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_DOUBLE_VALUE(xreg, shift_left, shift_right, \
					  xmax, xinvert, 0) }
#define SOC_DOUBLE_R_EXT_TLV(xname, reg_left, reg_right, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		 SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_DOUBLE_R_VALUE(reg_left, reg_right, xshift, \
					    xmax, xinvert) }
#define SOC_SINGLE_BOOL_EXT(xname, xdata, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_bool_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = xdata }
#define SOC_ENUM_EXT(xname, xenum, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&xenum }
#define SOC_VALUE_ENUM_EXT(xname, xenum, xhandler_get, xhandler_put) \
	SOC_ENUM_EXT(xname, xenum, xhandler_get, xhandler_put)

#define SND_SOC_BYTES(xname, xbase, xregs)		      \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,   \
	.info = snd_soc_bytes_info, .get = snd_soc_bytes_get, \
	.put = snd_soc_bytes_put, .private_value =	      \
		((unsigned long)&(struct soc_bytes)           \
		{.base = xbase, .num_regs = xregs }) }

#define SND_SOC_BYTES_MASK(xname, xbase, xregs, xmask)	      \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,   \
	.info = snd_soc_bytes_info, .get = snd_soc_bytes_get, \
	.put = snd_soc_bytes_put, .private_value =	      \
		((unsigned long)&(struct soc_bytes)           \
		{.base = xbase, .num_regs = xregs,	      \
		 .mask = xmask }) }

/*
 * SND_SOC_BYTES_EXT is deprecated, please USE SND_SOC_BYTES_TLV instead
 */
#define SND_SOC_BYTES_EXT(xname, xcount, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_bytes_info_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_bytes_ext) \
		{.max = xcount} }
#define SND_SOC_BYTES_TLV(xname, xcount, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE | \
		  SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK, \
	.tlv.c = (snd_soc_bytes_tlv_callback), \
	.info = snd_soc_bytes_info_ext, \
	.private_value = (unsigned long)&(struct soc_bytes_ext) \
		{.max = xcount, .get = xhandler_get, .put = xhandler_put, } }
#define SOC_SINGLE_XR_SX(xname, xregbase, xregcount, xnbits, \
		xmin, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_xr_sx, .get = snd_soc_get_xr_sx, \
	.put = snd_soc_put_xr_sx, \
	.private_value = (unsigned long)&(struct soc_mreg_control) \
		{.regbase = xregbase, .regcount = xregcount, .nbits = xnbits, \
		.invert = xinvert, .min = xmin, .max = xmax} }

#define SOC_SINGLE_STROBE(xname, xreg, xshift, xinvert) \
	SOC_SINGLE_EXT(xname, xreg, xshift, 1, xinvert, \
		snd_soc_get_strobe, snd_soc_put_strobe)

/*
 * Simplified versions of above macros, declaring a struct and calculating
 * ARRAY_SIZE internally
 */
#define SOC_ENUM_DOUBLE_DECL(name, xreg, xshift_l, xshift_r, xtexts) \
	const struct soc_enum name = SOC_ENUM_DOUBLE(xreg, xshift_l, xshift_r, \
						ARRAY_SIZE(xtexts), xtexts)
#define SOC_ENUM_SINGLE_DECL(name, xreg, xshift, xtexts) \
	SOC_ENUM_DOUBLE_DECL(name, xreg, xshift, xshift, xtexts)
#define SOC_ENUM_SINGLE_EXT_DECL(name, xtexts) \
	const struct soc_enum name = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(xtexts), xtexts)
#define SOC_VALUE_ENUM_DOUBLE_DECL(name, xreg, xshift_l, xshift_r, xmask, xtexts, xvalues) \
	const struct soc_enum name = SOC_VALUE_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xmask, \
							ARRAY_SIZE(xtexts), xtexts, xvalues)
#define SOC_VALUE_ENUM_SINGLE_DECL(name, xreg, xshift, xmask, xtexts, xvalues) \
	SOC_VALUE_ENUM_DOUBLE_DECL(name, xreg, xshift, xshift, xmask, xtexts, xvalues)

#define SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(name, xreg, xshift, xmask, xtexts, xvalues) \
	const struct soc_enum name = SOC_VALUE_ENUM_SINGLE_AUTODISABLE(xreg, \
		xshift, xmask, ARRAY_SIZE(xtexts), xtexts, xvalues)

#define SOC_ENUM_SINGLE_VIRT_DECL(name, xtexts) \
	const struct soc_enum name = SOC_ENUM_SINGLE_VIRT(ARRAY_SIZE(xtexts), xtexts)

/*
 * Component probe and remove ordering levels for components with runtime
 * dependencies.
 */
#define SND_SOC_COMP_ORDER_FIRST		-2
#define SND_SOC_COMP_ORDER_EARLY		-1
#define SND_SOC_COMP_ORDER_NORMAL		0
#define SND_SOC_COMP_ORDER_LATE		1
#define SND_SOC_COMP_ORDER_LAST		2

#define for_each_comp_order(order)		\
	for (order  = SND_SOC_COMP_ORDER_FIRST;	\
	     order <= SND_SOC_COMP_ORDER_LAST;	\
	     order++)

/*
 * Bias levels
 *
 * @ON:      Bias is fully on for audio playback and capture operations.
 * @PREPARE: Prepare for audio operations. Called before DAPM switching for
 *           stream start and stop operations.
 * @STANDBY: Low power standby state when no playback/capture operations are
 *           in progress. NOTE: The transition time between STANDBY and ON
 *           should be as fast as possible and no longer than 10ms.
 * @OFF:     Power Off. No restrictions on transition times.
 */
enum snd_soc_bias_level {
	SND_SOC_BIAS_OFF = 0,
	SND_SOC_BIAS_STANDBY = 1,
	SND_SOC_BIAS_PREPARE = 2,
	SND_SOC_BIAS_ON = 3,
};

struct device_node;
struct snd_jack;
struct snd_soc_card;
struct snd_soc_pcm_stream;
struct snd_soc_ops;
struct snd_soc_pcm_runtime;
struct snd_soc_dai;
struct snd_soc_dai_driver;
struct snd_soc_dai_link;
struct snd_soc_component;
struct snd_soc_component_driver;
struct soc_enum;
struct snd_soc_jack;
struct snd_soc_jack_zone;
struct snd_soc_jack_pin;
#include <sound/soc-dapm.h>
#include <sound/soc-dpcm.h>
#include <sound/soc-topology.h>

struct snd_soc_jack_gpio;

typedef int (*hw_write_t)(void *,const char* ,int);

enum snd_soc_pcm_subclass {
	SND_SOC_PCM_CLASS_PCM	= 0,
	SND_SOC_PCM_CLASS_BE	= 1,
};

enum snd_soc_card_subclass {
	SND_SOC_CARD_CLASS_INIT		= 0,
	SND_SOC_CARD_CLASS_RUNTIME	= 1,
};

int snd_soc_register_card(struct snd_soc_card *card);
int snd_soc_unregister_card(struct snd_soc_card *card);
int devm_snd_soc_register_card(struct device *dev, struct snd_soc_card *card);
#ifdef CONFIG_PM_SLEEP
int snd_soc_suspend(struct device *dev);
int snd_soc_resume(struct device *dev);
#else
static inline int snd_soc_suspend(struct device *dev)
{
	return 0;
}

static inline int snd_soc_resume(struct device *dev)
{
	return 0;
}
#endif
int snd_soc_poweroff(struct device *dev);
int snd_soc_add_component(struct device *dev,
		struct snd_soc_component *component,
		const struct snd_soc_component_driver *component_driver,
		struct snd_soc_dai_driver *dai_drv,
		int num_dai);
int snd_soc_register_component(struct device *dev,
			 const struct snd_soc_component_driver *component_driver,
			 struct snd_soc_dai_driver *dai_drv, int num_dai);
int devm_snd_soc_register_component(struct device *dev,
			 const struct snd_soc_component_driver *component_driver,
			 struct snd_soc_dai_driver *dai_drv, int num_dai);
void snd_soc_unregister_component(struct device *dev);
struct snd_soc_component *snd_soc_lookup_component(struct device *dev,
						   const char *driver_name);

int soc_new_pcm(struct snd_soc_pcm_runtime *rtd, int num);
#ifdef CONFIG_SND_SOC_COMPRESS
int snd_soc_new_compress(struct snd_soc_pcm_runtime *rtd, int num);
#else
static inline int snd_soc_new_compress(struct snd_soc_pcm_runtime *rtd, int num)
{
	return 0;
}
#endif

void snd_soc_disconnect_sync(struct device *dev);

struct snd_pcm_substream *snd_soc_get_dai_substream(struct snd_soc_card *card,
		const char *dai_link, int stream);
struct snd_soc_pcm_runtime *snd_soc_get_pcm_runtime(struct snd_soc_card *card,
		const char *dai_link);

bool snd_soc_runtime_ignore_pmdown_time(struct snd_soc_pcm_runtime *rtd);
void snd_soc_runtime_activate(struct snd_soc_pcm_runtime *rtd, int stream);
void snd_soc_runtime_deactivate(struct snd_soc_pcm_runtime *rtd, int stream);

int snd_soc_runtime_set_dai_fmt(struct snd_soc_pcm_runtime *rtd,
	unsigned int dai_fmt);

#ifdef CONFIG_DMI
int snd_soc_set_dmi_name(struct snd_soc_card *card, const char *flavour);
#else
static inline int snd_soc_set_dmi_name(struct snd_soc_card *card,
				       const char *flavour)
{
	return 0;
}
#endif

/* Utility functions to get clock rates from various things */
int snd_soc_calc_frame_size(int sample_size, int channels, int tdm_slots);
int snd_soc_params_to_frame_size(struct snd_pcm_hw_params *params);
int snd_soc_calc_bclk(int fs, int sample_size, int channels, int tdm_slots);
int snd_soc_params_to_bclk(struct snd_pcm_hw_params *parms);

/* set runtime hw params */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw);

int soc_dai_hw_params(struct snd_pcm_substream *substream,
		      struct snd_pcm_hw_params *params,
		      struct snd_soc_dai *dai);

/* Jack reporting */
int snd_soc_card_jack_new(struct snd_soc_card *card, const char *id, int type,
	struct snd_soc_jack *jack, struct snd_soc_jack_pin *pins,
	unsigned int num_pins);

void snd_soc_jack_report(struct snd_soc_jack *jack, int status, int mask);
int snd_soc_jack_add_pins(struct snd_soc_jack *jack, int count,
			  struct snd_soc_jack_pin *pins);
void snd_soc_jack_notifier_register(struct snd_soc_jack *jack,
				    struct notifier_block *nb);
void snd_soc_jack_notifier_unregister(struct snd_soc_jack *jack,
				      struct notifier_block *nb);
int snd_soc_jack_add_zones(struct snd_soc_jack *jack, int count,
			  struct snd_soc_jack_zone *zones);
int snd_soc_jack_get_type(struct snd_soc_jack *jack, int micbias_voltage);
#ifdef CONFIG_GPIOLIB
int snd_soc_jack_add_gpios(struct snd_soc_jack *jack, int count,
			struct snd_soc_jack_gpio *gpios);
int snd_soc_jack_add_gpiods(struct device *gpiod_dev,
			    struct snd_soc_jack *jack,
			    int count, struct snd_soc_jack_gpio *gpios);
void snd_soc_jack_free_gpios(struct snd_soc_jack *jack, int count,
			struct snd_soc_jack_gpio *gpios);
#else
static inline int snd_soc_jack_add_gpios(struct snd_soc_jack *jack, int count,
					 struct snd_soc_jack_gpio *gpios)
{
	return 0;
}

static inline int snd_soc_jack_add_gpiods(struct device *gpiod_dev,
					  struct snd_soc_jack *jack,
					  int count,
					  struct snd_soc_jack_gpio *gpios)
{
	return 0;
}

static inline void snd_soc_jack_free_gpios(struct snd_soc_jack *jack, int count,
					   struct snd_soc_jack_gpio *gpios)
{
}
#endif

struct snd_ac97 *snd_soc_alloc_ac97_component(struct snd_soc_component *component);
struct snd_ac97 *snd_soc_new_ac97_component(struct snd_soc_component *component,
	unsigned int id, unsigned int id_mask);
void snd_soc_free_ac97_component(struct snd_ac97 *ac97);

#ifdef CONFIG_SND_SOC_AC97_BUS
int snd_soc_set_ac97_ops(struct snd_ac97_bus_ops *ops);
int snd_soc_set_ac97_ops_of_reset(struct snd_ac97_bus_ops *ops,
		struct platform_device *pdev);

extern struct snd_ac97_bus_ops *soc_ac97_ops;
#else
static inline int snd_soc_set_ac97_ops_of_reset(struct snd_ac97_bus_ops *ops,
	struct platform_device *pdev)
{
	return 0;
}

static inline int snd_soc_set_ac97_ops(struct snd_ac97_bus_ops *ops)
{
	return 0;
}
#endif

/*
 *Controls
 */
struct snd_kcontrol *snd_soc_cnew(const struct snd_kcontrol_new *_template,
				  void *data, const char *long_name,
				  const char *prefix);
struct snd_kcontrol *snd_soc_card_get_kcontrol(struct snd_soc_card *soc_card,
					       const char *name);
int snd_soc_add_component_controls(struct snd_soc_component *component,
	const struct snd_kcontrol_new *controls, unsigned int num_controls);
int snd_soc_add_card_controls(struct snd_soc_card *soc_card,
	const struct snd_kcontrol_new *controls, int num_controls);
int snd_soc_add_dai_controls(struct snd_soc_dai *dai,
	const struct snd_kcontrol_new *controls, int num_controls);
int snd_soc_info_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_info_volsw_sx(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo);
#define snd_soc_info_bool_ext		snd_ctl_boolean_mono_info
int snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
#define snd_soc_get_volsw_2r snd_soc_get_volsw
#define snd_soc_put_volsw_2r snd_soc_put_volsw
int snd_soc_get_volsw_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw_range(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_put_volsw_range(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_get_volsw_range(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_limit_volume(struct snd_soc_card *card,
	const char *name, int max);
int snd_soc_bytes_info(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_info *uinfo);
int snd_soc_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
int snd_soc_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
int snd_soc_bytes_info_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *ucontrol);
int snd_soc_bytes_tlv_callback(struct snd_kcontrol *kcontrol, int op_flag,
	unsigned int size, unsigned int __user *tlv);
int snd_soc_info_xr_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_xr_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_xr_sx(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_get_strobe(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_strobe(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);

/**
 * struct snd_soc_jack_pin - Describes a pin to update based on jack detection
 *
 * @pin:    name of the pin to update
 * @mask:   bits to check for in reported jack status
 * @invert: if non-zero then pin is enabled when status is not reported
 * @list:   internal list entry
 */
struct snd_soc_jack_pin {
	struct list_head list;
	const char *pin;
	int mask;
	bool invert;
};

/**
 * struct snd_soc_jack_zone - Describes voltage zones of jack detection
 *
 * @min_mv: start voltage in mv
 * @max_mv: end voltage in mv
 * @jack_type: type of jack that is expected for this voltage
 * @debounce_time: debounce_time for jack, codec driver should wait for this
 *		duration before reading the adc for voltages
 * @list:   internal list entry
 */
struct snd_soc_jack_zone {
	unsigned int min_mv;
	unsigned int max_mv;
	unsigned int jack_type;
	unsigned int debounce_time;
	struct list_head list;
};

/**
 * struct snd_soc_jack_gpio - Describes a gpio pin for jack detection
 *
 * @gpio:         legacy gpio number
 * @idx:          gpio descriptor index within the function of the GPIO
 *                consumer device
 * @gpiod_dev:    GPIO consumer device
 * @name:         gpio name. Also as connection ID for the GPIO consumer
 *                device function name lookup
 * @report:       value to report when jack detected
 * @invert:       report presence in low state
 * @debounce_time: debounce time in ms
 * @wake:	  enable as wake source
 * @jack_status_check: callback function which overrides the detection
 *		       to provide more complex checks (eg, reading an
 *		       ADC).
 */
struct snd_soc_jack_gpio {
	unsigned int gpio;
	unsigned int idx;
	struct device *gpiod_dev;
	const char *name;
	int report;
	int invert;
	int debounce_time;
	bool wake;

	/* private: */
	struct snd_soc_jack *jack;
	struct delayed_work work;
	struct notifier_block pm_notifier;
	struct gpio_desc *desc;

	void *data;
	/* public: */
	int (*jack_status_check)(void *data);
};

struct snd_soc_jack {
	struct mutex mutex;
	struct snd_jack *jack;
	struct snd_soc_card *card;
	struct list_head pins;
	int status;
	struct blocking_notifier_head notifier;
	struct list_head jack_zones;
};

/* SoC PCM stream information */
struct snd_soc_pcm_stream {
	const char *stream_name;
	u64 formats;			/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;		/* SNDRV_PCM_RATE_* */
	unsigned int rate_min;		/* min rate */
	unsigned int rate_max;		/* max rate */
	unsigned int channels_min;	/* min channels */
	unsigned int channels_max;	/* max channels */
	unsigned int sig_bits;		/* number of bits of content */
};

/* SoC audio ops */
struct snd_soc_ops {
	int (*startup)(struct snd_pcm_substream *);
	void (*shutdown)(struct snd_pcm_substream *);
	int (*hw_params)(struct snd_pcm_substream *, struct snd_pcm_hw_params *);
	int (*hw_free)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
	int (*trigger)(struct snd_pcm_substream *, int);
};

struct snd_soc_compr_ops {
	int (*startup)(struct snd_compr_stream *);
	void (*shutdown)(struct snd_compr_stream *);
	int (*set_params)(struct snd_compr_stream *);
	int (*trigger)(struct snd_compr_stream *);
};

/* component interface */
struct snd_soc_component_driver {
	const char *name;

	/* Default control and setup, added after probe() is run */
	const struct snd_kcontrol_new *controls;
	unsigned int num_controls;
	const struct snd_soc_dapm_widget *dapm_widgets;
	unsigned int num_dapm_widgets;
	const struct snd_soc_dapm_route *dapm_routes;
	unsigned int num_dapm_routes;

	int (*probe)(struct snd_soc_component *);
	void (*remove)(struct snd_soc_component *);
	int (*suspend)(struct snd_soc_component *);
	int (*resume)(struct snd_soc_component *);

	unsigned int (*read)(struct snd_soc_component *, unsigned int);
	int (*write)(struct snd_soc_component *, unsigned int, unsigned int);

	/* pcm creation and destruction */
	int (*pcm_new)(struct snd_soc_pcm_runtime *);
	void (*pcm_free)(struct snd_pcm *);

	/* component wide operations */
	int (*set_sysclk)(struct snd_soc_component *component,
			  int clk_id, int source, unsigned int freq, int dir);
	int (*set_pll)(struct snd_soc_component *component, int pll_id,
		       int source, unsigned int freq_in, unsigned int freq_out);
	int (*set_jack)(struct snd_soc_component *component,
			struct snd_soc_jack *jack,  void *data);

	/* DT */
	int (*of_xlate_dai_name)(struct snd_soc_component *component,
				 struct of_phandle_args *args,
				 const char **dai_name);
	int (*of_xlate_dai_id)(struct snd_soc_component *comment,
			       struct device_node *endpoint);
	void (*seq_notifier)(struct snd_soc_component *, enum snd_soc_dapm_type,
		int subseq);
	int (*stream_event)(struct snd_soc_component *, int event);
	int (*set_bias_level)(struct snd_soc_component *component,
			      enum snd_soc_bias_level level);

	const struct snd_pcm_ops *ops;
	const struct snd_compr_ops *compr_ops;

	/* probe ordering - for components with runtime dependencies */
	int probe_order;
	int remove_order;

	/*
	 * signal if the module handling the component should not be removed
	 * if a pcm is open. Setting this would prevent the module
	 * refcount being incremented in probe() but allow it be incremented
	 * when a pcm is opened and decremented when it is closed.
	 */
	unsigned int module_get_upon_open:1;

	/* bits */
	unsigned int idle_bias_on:1;
	unsigned int suspend_bias_off:1;
	unsigned int use_pmdown_time:1; /* care pmdown_time at stop */
	unsigned int endianness:1;
	unsigned int non_legacy_dai_naming:1;

	/* this component uses topology and ignore machine driver FEs */
	const char *ignore_machine;
	const char *topology_name_prefix;
	int (*be_hw_params_fixup)(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params);
	bool use_dai_pcm_id;	/* use the DAI link PCM ID as PCM device number */
	int be_pcm_base;	/* base device ID for all BE PCMs */
};

struct snd_soc_component {
	const char *name;
	int id;
	const char *name_prefix;
	struct device *dev;
	struct snd_soc_card *card;

	unsigned int active;

	unsigned int suspended:1; /* is in suspend PM state */

	struct list_head list;
	struct list_head card_aux_list; /* for auxiliary bound components */
	struct list_head card_list;

	const struct snd_soc_component_driver *driver;

	struct list_head dai_list;
	int num_dai;

	struct regmap *regmap;
	int val_bytes;

	struct mutex io_mutex;

	/* attached dynamic objects */
	struct list_head dobj_list;

	/*
	* DO NOT use any of the fields below in drivers, they are temporary and
	* are going to be removed again soon. If you use them in driver code the
	* driver will be marked as BROKEN when these fields are removed.
	*/

	/* Don't use these, use snd_soc_component_get_dapm() */
	struct snd_soc_dapm_context dapm;

	/* machine specific init */
	int (*init)(struct snd_soc_component *component);

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	const char *debugfs_prefix;
#endif
};

#define for_each_component_dais(component, dai)\
	list_for_each_entry(dai, &(component)->dai_list, list)
#define for_each_component_dais_safe(component, dai, _dai)\
	list_for_each_entry_safe(dai, _dai, &(component)->dai_list, list)

struct snd_soc_rtdcom_list {
	struct snd_soc_component *component;
	struct list_head list; /* rtd::component_list */
};
struct snd_soc_component*
snd_soc_rtdcom_lookup(struct snd_soc_pcm_runtime *rtd,
		       const char *driver_name);
#define for_each_rtdcom(rtd, rtdcom) \
	list_for_each_entry(rtdcom, &(rtd)->component_list, list)
#define for_each_rtdcom_safe(rtd, rtdcom1, rtdcom2) \
	list_for_each_entry_safe(rtdcom1, rtdcom2, &(rtd)->component_list, list)

struct snd_soc_dai_link_component {
	const char *name;
	struct device_node *of_node;
	const char *dai_name;
};

struct snd_soc_dai_link {
	/* config - must be set by machine driver */
	const char *name;			/* Codec name */
	const char *stream_name;		/* Stream name */

	/*
	 *	cpu_name
	 *	cpu_of_node
	 *	cpu_dai_name
	 *
	 * These are legacy style, and will be replaced to
	 * modern style (= snd_soc_dai_link_component) in the future,
	 * but, not yet supported so far.
	 * If modern style was supported for CPU, all driver will switch
	 * to use it, and, legacy style code will be removed from ALSA SoC.
	 */
	/*
	 * You MAY specify the link's CPU-side device, either by device name,
	 * or by DT/OF node, but not both. If this information is omitted,
	 * the CPU-side DAI is matched using .cpu_dai_name only, which hence
	 * must be globally unique. These fields are currently typically used
	 * only for codec to codec links, or systems using device tree.
	 */
	const char *cpu_name;
	struct device_node *cpu_of_node;
	/*
	 * You MAY specify the DAI name of the CPU DAI. If this information is
	 * omitted, the CPU-side DAI is matched using .cpu_name/.cpu_of_node
	 * only, which only works well when that device exposes a single DAI.
	 */
	const char *cpu_dai_name;

	struct snd_soc_dai_link_component *cpus;
	unsigned int num_cpus;

	/*
	 *	codec_name
	 *	codec_of_node
	 *	codec_dai_name
	 *
	 * These are legacy style, it will be converted to modern style
	 * (= snd_soc_dai_link_component) automatically in soc-core
	 * if driver is using legacy style.
	 * Driver shouldn't use both legacy and modern style in the same time.
	 * If modern style was supported for CPU, all driver will switch
	 * to use it, and, legacy style code will be removed from ALSA SoC.
	 */
	/*
	 * You MUST specify the link's codec, either by device name, or by
	 * DT/OF node, but not both.
	 */
	const char *codec_name;
	struct device_node *codec_of_node;
	/* You MUST specify the DAI name within the codec */
	const char *codec_dai_name;

	struct snd_soc_dai_link_component *codecs;
	unsigned int num_codecs;

	/*
	 *	platform_name
	 *	platform_of_node
	 *
	 * These are legacy style, it will be converted to modern style
	 * (= snd_soc_dai_link_component) automatically in soc-core
	 * if driver is using legacy style.
	 * Driver shouldn't use both legacy and modern style in the same time.
	 * If modern style was supported for CPU, all driver will switch
	 * to use it, and, legacy style code will be removed from ALSA SoC.
	 */
	/*
	 * You MAY specify the link's platform/PCM/DMA driver, either by
	 * device name, or by DT/OF node, but not both. Some forms of link
	 * do not need a platform.
	 */
	const char *platform_name;
	struct device_node *platform_of_node;
	struct snd_soc_dai_link_component *platforms;
	unsigned int num_platforms;

	int id;	/* optional ID for machine driver link identification */

	const struct snd_soc_pcm_stream *params;
	unsigned int num_params;

	unsigned int dai_fmt;           /* format to set on init */

	enum snd_soc_dpcm_trigger trigger[2]; /* trigger type for DPCM */

	/* codec/machine specific init - e.g. add machine controls */
	int (*init)(struct snd_soc_pcm_runtime *rtd);

	/* optional hw_params re-writing for BE and FE sync */
	int (*be_hw_params_fixup)(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params);

	/* machine stream operations */
	const struct snd_soc_ops *ops;
	const struct snd_soc_compr_ops *compr_ops;

	/* Mark this pcm with non atomic ops */
	bool nonatomic;

	/* For unidirectional dai links */
	unsigned int playback_only:1;
	unsigned int capture_only:1;

	/* Keep DAI active over suspend */
	unsigned int ignore_suspend:1;

	/* Symmetry requirements */
	unsigned int symmetric_rates:1;
	unsigned int symmetric_channels:1;
	unsigned int symmetric_samplebits:1;

	/* Do not create a PCM for this DAI link (Backend link) */
	unsigned int no_pcm:1;

	/* This DAI link can route to other DAI links at runtime (Frontend)*/
	unsigned int dynamic:1;

	/* DPCM capture and Playback support */
	unsigned int dpcm_capture:1;
	unsigned int dpcm_playback:1;

	/* DPCM used FE & BE merged format */
	unsigned int dpcm_merged_format:1;
	/* DPCM used FE & BE merged channel */
	unsigned int dpcm_merged_chan:1;
	/* DPCM used FE & BE merged rate */
	unsigned int dpcm_merged_rate:1;

	/* pmdown_time is ignored at stop */
	unsigned int ignore_pmdown_time:1;

	/* Do not create a PCM for this DAI link (Backend link) */
	unsigned int ignore:1;

	/*
	 * This driver uses legacy platform naming. Set by the core, machine
	 * drivers should not modify this value.
	 */
	unsigned int legacy_platform:1;
	unsigned int legacy_cpu:1;

	struct list_head list; /* DAI link list of the soc card */
	struct snd_soc_dobj dobj; /* For topology */
};
#define for_each_link_codecs(link, i, codec)				\
	for ((i) = 0;							\
	     ((i) < link->num_codecs) && ((codec) = &link->codecs[i]);	\
	     (i)++)

/*
 * Sample 1 : Single CPU/Codec/Platform
 *
 * SND_SOC_DAILINK_DEFS(test,
 *	DAILINK_COMP_ARRAY(COMP_CPU("cpu_dai")),
 *	DAILINK_COMP_ARRAY(COMP_CODEC("codec", "codec_dai")),
 *	DAILINK_COMP_ARRAY(COMP_PLATFORM("platform")));
 *
 * struct snd_soc_dai_link link = {
 *	...
 *	SND_SOC_DAILINK_REG(test),
 * };
 *
 * Sample 2 : Multi CPU/Codec, no Platform
 *
 * SND_SOC_DAILINK_DEFS(test,
 *	DAILINK_COMP_ARRAY(COMP_CPU("cpu_dai1"),
 *			   COMP_CPU("cpu_dai2")),
 *	DAILINK_COMP_ARRAY(COMP_CODEC("codec1", "codec_dai1"),
 *			   COMP_CODEC("codec2", "codec_dai2")));
 *
 * struct snd_soc_dai_link link = {
 *	...
 *	SND_SOC_DAILINK_REG(test),
 * };
 *
 * Sample 3 : Define each CPU/Codec/Platform manually
 *
 * SND_SOC_DAILINK_DEF(test_cpu,
 *		DAILINK_COMP_ARRAY(COMP_CPU("cpu_dai1"),
 *				   COMP_CPU("cpu_dai2")));
 * SND_SOC_DAILINK_DEF(test_codec,
 *		DAILINK_COMP_ARRAY(COMP_CODEC("codec1", "codec_dai1"),
 *				   COMP_CODEC("codec2", "codec_dai2")));
 * SND_SOC_DAILINK_DEF(test_platform,
 *		DAILINK_COMP_ARRAY(COMP_PLATFORM("platform")));
 *
 * struct snd_soc_dai_link link = {
 *	...
 *	SND_SOC_DAILINK_REG(test_cpu,
 *			    test_codec,
 *			    test_platform),
 * };
 *
 * Sample 4 : Sample3 without platform
 *
 * struct snd_soc_dai_link link = {
 *	...
 *	SND_SOC_DAILINK_REG(test_cpu,
 *			    test_codec);
 * };
 */

#define SND_SOC_DAILINK_REG1(name)	 SND_SOC_DAILINK_REG3(name##_cpus, name##_codecs, name##_platforms)
#define SND_SOC_DAILINK_REG2(cpu, codec) SND_SOC_DAILINK_REG3(cpu, codec, null_dailink_component)
#define SND_SOC_DAILINK_REG3(cpu, codec, platform)	\
	.cpus		= cpu,				\
	.num_cpus	= ARRAY_SIZE(cpu),		\
	.codecs		= codec,			\
	.num_codecs	= ARRAY_SIZE(codec),		\
	.platforms	= platform,			\
	.num_platforms	= ARRAY_SIZE(platform)

#define SND_SOC_DAILINK_REGx(_1, _2, _3, func, ...) func
#define SND_SOC_DAILINK_REG(...) \
	SND_SOC_DAILINK_REGx(__VA_ARGS__,		\
			SND_SOC_DAILINK_REG3,	\
			SND_SOC_DAILINK_REG2,	\
			SND_SOC_DAILINK_REG1)(__VA_ARGS__)

#define SND_SOC_DAILINK_DEF(name, def...)		\
	static struct snd_soc_dai_link_component name[]	= { def }

#define SND_SOC_DAILINK_DEFS(name, cpu, codec, platform...)	\
	SND_SOC_DAILINK_DEF(name##_cpus, cpu);			\
	SND_SOC_DAILINK_DEF(name##_codecs, codec);		\
	SND_SOC_DAILINK_DEF(name##_platforms, platform)

#define DAILINK_COMP_ARRAY(param...)	param
#define COMP_EMPTY()			{ }
#define COMP_CPU(_dai)			{ .dai_name = _dai, }
#define COMP_CODEC(_name, _dai)		{ .name = _name, .dai_name = _dai, }
#define COMP_PLATFORM(_name)		{ .name = _name }
#define COMP_DUMMY()			{ .name = "snd-soc-dummy", .dai_name = "snd-soc-dummy-dai", }

extern struct snd_soc_dai_link_component null_dailink_component[0];


struct snd_soc_codec_conf {
	/*
	 * specify device either by device name, or by
	 * DT/OF node, but not both.
	 */
	const char *dev_name;
	struct device_node *of_node;

	/*
	 * optional map of kcontrol, widget and path name prefixes that are
	 * associated per device
	 */
	const char *name_prefix;
};

struct snd_soc_aux_dev {
	const char *name;		/* Codec name */

	/*
	 * specify multi-codec either by device name, or by
	 * DT/OF node, but not both.
	 */
	const char *codec_name;
	struct device_node *codec_of_node;

	/* codec/machine specific init - e.g. add machine controls */
	int (*init)(struct snd_soc_component *component);
};

/* SoC card */
struct snd_soc_card {
	const char *name;
	const char *long_name;
	const char *driver_name;
	char dmi_longname[80];
	char topology_shortname[32];

	struct device *dev;
	struct snd_card *snd_card;
	struct module *owner;

	struct mutex mutex;
	struct mutex dapm_mutex;

	spinlock_t dpcm_lock;

	bool instantiated;
	bool topology_shortname_created;

	int (*probe)(struct snd_soc_card *card);
	int (*late_probe)(struct snd_soc_card *card);
	int (*remove)(struct snd_soc_card *card);

	/* the pre and post PM functions are used to do any PM work before and
	 * after the codec and DAI's do any PM work. */
	int (*suspend_pre)(struct snd_soc_card *card);
	int (*suspend_post)(struct snd_soc_card *card);
	int (*resume_pre)(struct snd_soc_card *card);
	int (*resume_post)(struct snd_soc_card *card);

	/* callbacks */
	int (*set_bias_level)(struct snd_soc_card *,
			      struct snd_soc_dapm_context *dapm,
			      enum snd_soc_bias_level level);
	int (*set_bias_level_post)(struct snd_soc_card *,
				   struct snd_soc_dapm_context *dapm,
				   enum snd_soc_bias_level level);

	int (*add_dai_link)(struct snd_soc_card *,
			    struct snd_soc_dai_link *link);
	void (*remove_dai_link)(struct snd_soc_card *,
			    struct snd_soc_dai_link *link);

	long pmdown_time;

	/* CPU <--> Codec DAI links  */
	struct snd_soc_dai_link *dai_link;  /* predefined links only */
	int num_links;  /* predefined links only */
	struct list_head dai_link_list; /* all links */

	struct list_head rtd_list;
	int num_rtd;

	/* optional codec specific configuration */
	struct snd_soc_codec_conf *codec_conf;
	int num_configs;

	/*
	 * optional auxiliary devices such as amplifiers or codecs with DAI
	 * link unused
	 */
	struct snd_soc_aux_dev *aux_dev;
	int num_aux_devs;
	struct list_head aux_comp_list;

	const struct snd_kcontrol_new *controls;
	int num_controls;

	/*
	 * Card-specific routes and widgets.
	 * Note: of_dapm_xxx for Device Tree; Otherwise for driver build-in.
	 */
	const struct snd_soc_dapm_widget *dapm_widgets;
	int num_dapm_widgets;
	const struct snd_soc_dapm_route *dapm_routes;
	int num_dapm_routes;
	const struct snd_soc_dapm_widget *of_dapm_widgets;
	int num_of_dapm_widgets;
	const struct snd_soc_dapm_route *of_dapm_routes;
	int num_of_dapm_routes;
	bool fully_routed;

	struct work_struct deferred_resume_work;

	/* lists of probed devices belonging to this card */
	struct list_head component_dev_list;
	struct list_head list;

	struct list_head widgets;
	struct list_head paths;
	struct list_head dapm_list;
	struct list_head dapm_dirty;

	/* attached dynamic objects */
	struct list_head dobj_list;

	/* Generic DAPM context for the card */
	struct snd_soc_dapm_context dapm;
	struct snd_soc_dapm_stats dapm_stats;
	struct snd_soc_dapm_update *update;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_card_root;
	struct dentry *debugfs_pop_time;
#endif
	u32 pop_time;

	void *drvdata;
};
#define for_each_card_prelinks(card, i, link)				\
	for ((i) = 0;							\
	     ((i) < (card)->num_links) && ((link) = &(card)->dai_link[i]); \
	     (i)++)

#define for_each_card_links(card, link)				\
	list_for_each_entry(dai_link, &(card)->dai_link_list, list)
#define for_each_card_links_safe(card, link, _link)			\
	list_for_each_entry_safe(link, _link, &(card)->dai_link_list, list)

#define for_each_card_rtds(card, rtd)			\
	list_for_each_entry(rtd, &(card)->rtd_list, list)
#define for_each_card_rtds_safe(card, rtd, _rtd)	\
	list_for_each_entry_safe(rtd, _rtd, &(card)->rtd_list, list)

#define for_each_card_components(card, component)			\
	list_for_each_entry(component, &(card)->component_dev_list, card_list)

/* SoC machine DAI configuration, glues a codec and cpu DAI together */
struct snd_soc_pcm_runtime {
	struct device *dev;
	struct snd_soc_card *card;
	struct snd_soc_dai_link *dai_link;
	struct mutex pcm_mutex;
	enum snd_soc_pcm_subclass pcm_subclass;
	struct snd_pcm_ops ops;

	unsigned int params_select; /* currently selected param for dai link */

	/* Dynamic PCM BE runtime data */
	struct snd_soc_dpcm_runtime dpcm[2];

	long pmdown_time;

	/* runtime devices */
	struct snd_pcm *pcm;
	struct snd_compr *compr;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;

	struct snd_soc_dai **codec_dais;
	unsigned int num_codecs;

	struct delayed_work delayed_work;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dpcm_root;
#endif

	unsigned int num; /* 0-based and monotonic increasing */
	struct list_head list; /* rtd list of the soc card */
	struct list_head component_list; /* list of connected components */

	/* bit field */
	unsigned int dev_registered:1;
	unsigned int pop_wait:1;
	unsigned int fe_compr:1; /* for Dynamic PCM */
};
#define for_each_rtd_codec_dai(rtd, i, dai)\
	for ((i) = 0;						       \
	     ((i) < rtd->num_codecs) && ((dai) = rtd->codec_dais[i]); \
	     (i)++)
#define for_each_rtd_codec_dai_rollback(rtd, i, dai)		\
	for (; ((--i) >= 0) && ((dai) = rtd->codec_dais[i]);)


/* mixer control */
struct soc_mixer_control {
	int min, max, platform_max;
	int reg, rreg;
	unsigned int shift, rshift;
	unsigned int sign_bit;
	unsigned int invert:1;
	unsigned int autodisable:1;
	struct snd_soc_dobj dobj;
};

struct soc_bytes {
	int base;
	int num_regs;
	u32 mask;
};

struct soc_bytes_ext {
	int max;
	struct snd_soc_dobj dobj;

	/* used for TLV byte control */
	int (*get)(struct snd_kcontrol *kcontrol, unsigned int __user *bytes,
			unsigned int size);
	int (*put)(struct snd_kcontrol *kcontrol, const unsigned int __user *bytes,
			unsigned int size);
};

/* multi register control */
struct soc_mreg_control {
	long min, max;
	unsigned int regbase, regcount, nbits, invert;
};

/* enumerated kcontrol */
struct soc_enum {
	int reg;
	unsigned char shift_l;
	unsigned char shift_r;
	unsigned int items;
	unsigned int mask;
	const char * const *texts;
	const unsigned int *values;
	unsigned int autodisable:1;
	struct snd_soc_dobj dobj;
};

/**
 * snd_soc_dapm_to_component() - Casts a DAPM context to the component it is
 *  embedded in
 * @dapm: The DAPM context to cast to the component
 *
 * This function must only be used on DAPM contexts that are known to be part of
 * a component (e.g. in a component driver). Otherwise the behavior is
 * undefined.
 */
static inline struct snd_soc_component *snd_soc_dapm_to_component(
	struct snd_soc_dapm_context *dapm)
{
	return container_of(dapm, struct snd_soc_component, dapm);
}

/**
 * snd_soc_component_get_dapm() - Returns the DAPM context associated with a
 *  component
 * @component: The component for which to get the DAPM context
 */
static inline struct snd_soc_dapm_context *snd_soc_component_get_dapm(
	struct snd_soc_component *component)
{
	return &component->dapm;
}

/**
 * snd_soc_component_init_bias_level() - Initialize COMPONENT DAPM bias level
 * @component: The COMPONENT for which to initialize the DAPM bias level
 * @level: The DAPM level to initialize to
 *
 * Initializes the COMPONENT DAPM bias level. See snd_soc_dapm_init_bias_level().
 */
static inline void
snd_soc_component_init_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	snd_soc_dapm_init_bias_level(
		snd_soc_component_get_dapm(component), level);
}

/**
 * snd_soc_component_get_bias_level() - Get current COMPONENT DAPM bias level
 * @component: The COMPONENT for which to get the DAPM bias level
 *
 * Returns: The current DAPM bias level of the COMPONENT.
 */
static inline enum snd_soc_bias_level
snd_soc_component_get_bias_level(struct snd_soc_component *component)
{
	return snd_soc_dapm_get_bias_level(
		snd_soc_component_get_dapm(component));
}

/**
 * snd_soc_component_force_bias_level() - Set the COMPONENT DAPM bias level
 * @component: The COMPONENT for which to set the level
 * @level: The level to set to
 *
 * Forces the COMPONENT bias level to a specific state. See
 * snd_soc_dapm_force_bias_level().
 */
static inline int
snd_soc_component_force_bias_level(struct snd_soc_component *component,
				   enum snd_soc_bias_level level)
{
	return snd_soc_dapm_force_bias_level(
		snd_soc_component_get_dapm(component),
		level);
}

/**
 * snd_soc_dapm_kcontrol_component() - Returns the component associated to a kcontrol
 * @kcontrol: The kcontrol
 *
 * This function must only be used on DAPM contexts that are known to be part of
 * a COMPONENT (e.g. in a COMPONENT driver). Otherwise the behavior is undefined.
 */
static inline struct snd_soc_component *snd_soc_dapm_kcontrol_component(
	struct snd_kcontrol *kcontrol)
{
	return snd_soc_dapm_to_component(snd_soc_dapm_kcontrol_dapm(kcontrol));
}

/**
 * snd_soc_component_cache_sync() - Sync the register cache with the hardware
 * @component: COMPONENT to sync
 *
 * Note: This function will call regcache_sync()
 */
static inline int snd_soc_component_cache_sync(
	struct snd_soc_component *component)
{
	return regcache_sync(component->regmap);
}

/* component IO */
int snd_soc_component_read(struct snd_soc_component *component,
	unsigned int reg, unsigned int *val);
unsigned int snd_soc_component_read32(struct snd_soc_component *component,
				      unsigned int reg);
int snd_soc_component_write(struct snd_soc_component *component,
	unsigned int reg, unsigned int val);
int snd_soc_component_update_bits(struct snd_soc_component *component,
	unsigned int reg, unsigned int mask, unsigned int val);
int snd_soc_component_update_bits_async(struct snd_soc_component *component,
	unsigned int reg, unsigned int mask, unsigned int val);
void snd_soc_component_async_complete(struct snd_soc_component *component);
int snd_soc_component_test_bits(struct snd_soc_component *component,
	unsigned int reg, unsigned int mask, unsigned int value);

/* component wide operations */
int snd_soc_component_set_sysclk(struct snd_soc_component *component,
			int clk_id, int source, unsigned int freq, int dir);
int snd_soc_component_set_pll(struct snd_soc_component *component, int pll_id,
			      int source, unsigned int freq_in,
			      unsigned int freq_out);
int snd_soc_component_set_jack(struct snd_soc_component *component,
			       struct snd_soc_jack *jack, void *data);

#ifdef CONFIG_REGMAP

void snd_soc_component_init_regmap(struct snd_soc_component *component,
	struct regmap *regmap);
void snd_soc_component_exit_regmap(struct snd_soc_component *component);

#endif

/* device driver data */

static inline void snd_soc_card_set_drvdata(struct snd_soc_card *card,
		void *data)
{
	card->drvdata = data;
}

static inline void *snd_soc_card_get_drvdata(struct snd_soc_card *card)
{
	return card->drvdata;
}

static inline void snd_soc_component_set_drvdata(struct snd_soc_component *c,
		void *data)
{
	dev_set_drvdata(c->dev, data);
}

static inline void *snd_soc_component_get_drvdata(struct snd_soc_component *c)
{
	return dev_get_drvdata(c->dev);
}

static inline void snd_soc_initialize_card_lists(struct snd_soc_card *card)
{
	INIT_LIST_HEAD(&card->widgets);
	INIT_LIST_HEAD(&card->paths);
	INIT_LIST_HEAD(&card->dapm_list);
	INIT_LIST_HEAD(&card->aux_comp_list);
	INIT_LIST_HEAD(&card->component_dev_list);
	INIT_LIST_HEAD(&card->list);
}

static inline bool snd_soc_volsw_is_stereo(struct soc_mixer_control *mc)
{
	if (mc->reg == mc->rreg && mc->shift == mc->rshift)
		return 0;
	/*
	 * mc->reg == mc->rreg && mc->shift != mc->rshift, or
	 * mc->reg != mc->rreg means that the control is
	 * stereo (bits in one register or in two registers)
	 */
	return 1;
}

static inline unsigned int snd_soc_enum_val_to_item(struct soc_enum *e,
	unsigned int val)
{
	unsigned int i;

	if (!e->values)
		return val;

	for (i = 0; i < e->items; i++)
		if (val == e->values[i])
			return i;

	return 0;
}

static inline unsigned int snd_soc_enum_item_to_val(struct soc_enum *e,
	unsigned int item)
{
	if (!e->values)
		return item;

	return e->values[item];
}

static inline bool snd_soc_component_is_active(
	struct snd_soc_component *component)
{
	return component->active != 0;
}

/**
 * snd_soc_kcontrol_component() - Returns the component that registered the
 *  control
 * @kcontrol: The control for which to get the component
 *
 * Note: This function will work correctly if the control has been registered
 * for a component. With snd_soc_add_codec_controls() or via table based
 * setup for either a CODEC or component driver. Otherwise the behavior is
 * undefined.
 */
static inline struct snd_soc_component *snd_soc_kcontrol_component(
	struct snd_kcontrol *kcontrol)
{
	return snd_kcontrol_chip(kcontrol);
}

int snd_soc_util_init(void);
void snd_soc_util_exit(void);

int snd_soc_of_parse_card_name(struct snd_soc_card *card,
			       const char *propname);
int snd_soc_of_parse_audio_simple_widgets(struct snd_soc_card *card,
					  const char *propname);
int snd_soc_of_get_slot_mask(struct device_node *np,
			     const char *prop_name,
			     unsigned int *mask);
int snd_soc_of_parse_tdm_slot(struct device_node *np,
			      unsigned int *tx_mask,
			      unsigned int *rx_mask,
			      unsigned int *slots,
			      unsigned int *slot_width);
void snd_soc_of_parse_node_prefix(struct device_node *np,
				   struct snd_soc_codec_conf *codec_conf,
				   struct device_node *of_node,
				   const char *propname);
static inline
void snd_soc_of_parse_audio_prefix(struct snd_soc_card *card,
				   struct snd_soc_codec_conf *codec_conf,
				   struct device_node *of_node,
				   const char *propname)
{
	snd_soc_of_parse_node_prefix(card->dev->of_node,
				     codec_conf, of_node, propname);
}

int snd_soc_of_parse_audio_routing(struct snd_soc_card *card,
				   const char *propname);
unsigned int snd_soc_of_parse_daifmt(struct device_node *np,
				     const char *prefix,
				     struct device_node **bitclkmaster,
				     struct device_node **framemaster);
int snd_soc_get_dai_id(struct device_node *ep);
int snd_soc_get_dai_name(struct of_phandle_args *args,
			 const char **dai_name);
int snd_soc_of_get_dai_name(struct device_node *of_node,
			    const char **dai_name);
int snd_soc_of_get_dai_link_codecs(struct device *dev,
				   struct device_node *of_node,
				   struct snd_soc_dai_link *dai_link);
void snd_soc_of_put_dai_link_codecs(struct snd_soc_dai_link *dai_link);

int snd_soc_add_dai_link(struct snd_soc_card *card,
				struct snd_soc_dai_link *dai_link);
void snd_soc_remove_dai_link(struct snd_soc_card *card,
			     struct snd_soc_dai_link *dai_link);
struct snd_soc_dai_link *snd_soc_find_dai_link(struct snd_soc_card *card,
					       int id, const char *name,
					       const char *stream_name);

int snd_soc_register_dai(struct snd_soc_component *component,
	struct snd_soc_dai_driver *dai_drv);

struct snd_soc_dai *snd_soc_find_dai(
	const struct snd_soc_dai_link_component *dlc);

#include <sound/soc-dai.h>

static inline
struct snd_soc_dai *snd_soc_card_get_codec_dai(struct snd_soc_card *card,
					       const char *dai_name)
{
	struct snd_soc_pcm_runtime *rtd;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		if (!strcmp(rtd->codec_dai->name, dai_name))
			return rtd->codec_dai;
	}

	return NULL;
}

static inline
int snd_soc_fixup_dai_links_platform_name(struct snd_soc_card *card,
					  const char *platform_name)
{
	struct snd_soc_dai_link *dai_link;
	const char *name;
	int i;

	if (!platform_name) /* nothing to do */
		return 0;

	/* set platform name for each dailink */
	for_each_card_prelinks(card, i, dai_link) {
		name = devm_kstrdup(card->dev, platform_name, GFP_KERNEL);
		if (!name)
			return -ENOMEM;

		if (dai_link->platforms)
			/* only single platform is supported for now */
			dai_link->platforms->name = name;
		else
			/*
			 * legacy mode, this case will be removed when all
			 * derivers are switched to modern style dai_link.
			 */
			dai_link->platform_name = name;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
extern struct dentry *snd_soc_debugfs_root;
#endif

extern const struct dev_pm_ops snd_soc_pm_ops;

/* Helper functions */
static inline void snd_soc_dapm_mutex_lock(struct snd_soc_dapm_context *dapm)
{
	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
}

static inline void snd_soc_dapm_mutex_unlock(struct snd_soc_dapm_context *dapm)
{
	mutex_unlock(&dapm->card->dapm_mutex);
}

int snd_soc_component_enable_pin(struct snd_soc_component *component,
				 const char *pin);
int snd_soc_component_enable_pin_unlocked(struct snd_soc_component *component,
					  const char *pin);
int snd_soc_component_disable_pin(struct snd_soc_component *component,
				  const char *pin);
int snd_soc_component_disable_pin_unlocked(struct snd_soc_component *component,
					   const char *pin);
int snd_soc_component_nc_pin(struct snd_soc_component *component,
			     const char *pin);
int snd_soc_component_nc_pin_unlocked(struct snd_soc_component *component,
				      const char *pin);
int snd_soc_component_get_pin_status(struct snd_soc_component *component,
				     const char *pin);
int snd_soc_component_force_enable_pin(struct snd_soc_component *component,
				       const char *pin);
int snd_soc_component_force_enable_pin_unlocked(
					struct snd_soc_component *component,
					const char *pin);

#endif
