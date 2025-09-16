/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/soc-dapm.h -- ALSA SoC Dynamic Audio Power Management
 *
 * Author:	Liam Girdwood
 * Created:	Aug 11th 2005
 * Copyright:	Wolfson Microelectronics. PLC.
 */

#ifndef __LINUX_SND_SOC_DAPM_H
#define __LINUX_SND_SOC_DAPM_H

#include <linux/types.h>
#include <sound/control.h>
#include <sound/soc-topology.h>
#include <sound/asoc.h>

struct device;
struct regulator;
struct soc_enum;
struct snd_pcm_substream;
struct snd_soc_pcm_runtime;

/* widget has no PM register bit */
#define SND_SOC_NOPM	-1

/*
 * SoC dynamic audio power management
 *
 * We can have up to 4 power domains
 *  1. Codec domain - VREF, VMID
 *     Usually controlled at codec probe/remove, although can be set
 *     at stream time if power is not needed for sidetone, etc.
 *  2. Platform/Machine domain - physically connected inputs and outputs
 *     Is platform/machine and user action specific, is set in the machine
 *     driver and by userspace e.g when HP are inserted
 *  3. Path domain - Internal codec path mixers
 *     Are automatically set when mixer and mux settings are
 *     changed by the user.
 *  4. Stream domain - DAC's and ADC's.
 *     Enabled when stream playback/capture is started.
 */

/* codec domain */
#define SND_SOC_DAPM_VMID(wname) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_vmid, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0}

/* platform domain */
#define SND_SOC_DAPM_SIGGEN(wname) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_siggen, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_SINK(wname) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_sink, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_INPUT(wname) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_input, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_OUTPUT(wname) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_output, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_MIC(wname, wevent) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mic, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD}
#define SND_SOC_DAPM_HP(wname, wevent) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_hp, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD}
#define SND_SOC_DAPM_SPK(wname, wevent) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_spk, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD}
#define SND_SOC_DAPM_LINE(wname, wevent) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_line, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD}

#define SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert) \
	.reg = wreg, .mask = 1, .shift = wshift, \
	.on_val = winvert ? 0 : 1, .off_val = winvert ? 1 : 0

/* path domain */
#define SND_SOC_DAPM_PGA(wname, wreg, wshift, winvert,\
	 wcontrols, wncontrols) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pga, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
#define SND_SOC_DAPM_OUT_DRV(wname, wreg, wshift, winvert,\
	 wcontrols, wncontrols) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_out_drv, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
#define SND_SOC_DAPM_MIXER(wname, wreg, wshift, winvert, \
	 wcontrols, wncontrols)\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
#define SND_SOC_DAPM_MIXER_NAMED_CTL(wname, wreg, wshift, winvert, \
	 wcontrols, wncontrols)\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer_named_ctl, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
/* DEPRECATED: use SND_SOC_DAPM_SUPPLY */
#define SND_SOC_DAPM_MICBIAS(wname, wreg, wshift, winvert) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_micbias, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = NULL, .num_kcontrols = 0}
#define SND_SOC_DAPM_SWITCH(wname, wreg, wshift, winvert, wcontrols) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_switch, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = 1}
#define SND_SOC_DAPM_MUX(wname, wreg, wshift, winvert, wcontrols) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mux, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = 1}
#define SND_SOC_DAPM_DEMUX(wname, wreg, wshift, winvert, wcontrols) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_demux, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = 1}

/* Simplified versions of above macros, assuming wncontrols = ARRAY_SIZE(wcontrols) */
#define SOC_PGA_ARRAY(wname, wreg, wshift, winvert,\
	 wcontrols) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pga, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols)}
#define SOC_MIXER_ARRAY(wname, wreg, wshift, winvert, \
	 wcontrols)\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols)}
#define SOC_MIXER_NAMED_CTL_ARRAY(wname, wreg, wshift, winvert, \
	 wcontrols)\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer_named_ctl, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols)}

/* path domain with event - event handler must return 0 for success */
#define SND_SOC_DAPM_PGA_E(wname, wreg, wshift, winvert, wcontrols, \
	wncontrols, wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pga, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_OUT_DRV_E(wname, wreg, wshift, winvert, wcontrols, \
	wncontrols, wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_out_drv, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_MIXER_E(wname, wreg, wshift, winvert, wcontrols, \
	wncontrols, wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = wncontrols, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_MIXER_NAMED_CTL_E(wname, wreg, wshift, winvert, \
	wcontrols, wncontrols, wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, \
	.num_kcontrols = wncontrols, .event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_SWITCH_E(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_switch, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = 1, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_MUX_E(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mux, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = 1, \
	.event = wevent, .event_flags = wflags}

/* additional sequencing control within an event type */
#define SND_SOC_DAPM_PGA_S(wname, wsubseq, wreg, wshift, winvert, \
	wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pga, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags, \
	.subseq = wsubseq}
#define SND_SOC_DAPM_SUPPLY_S(wname, wsubseq, wreg, wshift, winvert, wevent, \
	wflags)	\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_supply, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags, .subseq = wsubseq}

/* Simplified versions of above macros, assuming wncontrols = ARRAY_SIZE(wcontrols) */
#define SOC_PGA_E_ARRAY(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pga, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols), \
	.event = wevent, .event_flags = wflags}
#define SOC_MIXER_E_ARRAY(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols), \
	.event = wevent, .event_flags = wflags}
#define SOC_MIXER_NAMED_CTL_E_ARRAY(wname, wreg, wshift, winvert, \
	wcontrols, wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_mixer, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols), \
	.event = wevent, .event_flags = wflags}

/* events that are pre and post DAPM */
#define SND_SOC_DAPM_PRE(wname, wevent) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pre, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD}
#define SND_SOC_DAPM_POST(wname, wevent) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_post, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD}

/* stream domain */
#define SND_SOC_DAPM_AIF_IN(wname, stname, wchan, wreg, wshift, winvert) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_aif_in, .name = wname, .sname = stname, \
	.channel = wchan, SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), }
#define SND_SOC_DAPM_AIF_IN_E(wname, stname, wchan, wreg, wshift, winvert, \
			      wevent, wflags)				\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_aif_in, .name = wname, .sname = stname, \
	.channel = wchan, SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags }
#define SND_SOC_DAPM_AIF_OUT(wname, stname, wchan, wreg, wshift, winvert) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_aif_out, .name = wname, .sname = stname, \
	.channel = wchan, SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), }
#define SND_SOC_DAPM_AIF_OUT_E(wname, stname, wchan, wreg, wshift, winvert, \
			     wevent, wflags)				\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_aif_out, .name = wname, .sname = stname, \
	.channel = wchan, SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags }
#define SND_SOC_DAPM_DAC(wname, stname, wreg, wshift, winvert) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_dac, .name = wname, .sname = stname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert) }
#define SND_SOC_DAPM_DAC_E(wname, stname, wreg, wshift, winvert, \
			   wevent, wflags)				\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_dac, .name = wname, .sname = stname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags}

#define SND_SOC_DAPM_ADC(wname, stname, wreg, wshift, winvert) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_adc, .name = wname, .sname = stname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), }
#define SND_SOC_DAPM_ADC_E(wname, stname, wreg, wshift, winvert, \
			   wevent, wflags)				\
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_adc, .name = wname, .sname = stname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_CLOCK_SUPPLY(wname) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_clock_supply, .name = wname, \
	.reg = SND_SOC_NOPM, .event = snd_soc_dapm_clock_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD }

/* generic widgets */
#define SND_SOC_DAPM_REG(wid, wname, wreg, wshift, wmask, won_val, woff_val) \
(struct snd_soc_dapm_widget) { \
	.id = wid, .name = wname, .kcontrol_news = NULL, .num_kcontrols = 0, \
	.reg = wreg, .shift = wshift, .mask = wmask, \
	.on_val = won_val, .off_val = woff_val, }
#define SND_SOC_DAPM_SUPPLY(wname, wreg, wshift, winvert, wevent, wflags) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_supply, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_REGULATOR_SUPPLY(wname, wdelay, wflags)	    \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_regulator_supply, .name = wname, \
	.reg = SND_SOC_NOPM, .shift = wdelay, .event = snd_soc_dapm_regulator_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD, \
	.on_val = wflags}
#define SND_SOC_DAPM_PINCTRL(wname, active, sleep) \
(struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_pinctrl, .name = wname, \
	.priv = (&(struct snd_soc_dapm_pinctrl_priv) \
		{ .active_state = active, .sleep_state = sleep,}), \
	.reg = SND_SOC_NOPM, .event = snd_soc_dapm_pinctrl_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD }



/* dapm kcontrol types */
#define SOC_DAPM_DOUBLE(xname, reg, lshift, rshift, max, invert) \
	SOC_DOUBLE_EXT(xname, reg, lshift, rshift, max, invert, \
		       snd_soc_dapm_get_volsw, snd_soc_dapm_put_volsw)
#define SOC_DAPM_DOUBLE_R(xname, lreg, rreg, shift, max, invert) \
	SOC_DOUBLE_R_EXT(xname, lreg, rreg, shift, max, invert, \
			 snd_soc_dapm_get_volsw, snd_soc_dapm_put_volsw)
#define SOC_DAPM_SINGLE(xname, reg, shift, max, invert) \
	SOC_SINGLE_EXT(xname, reg, shift, max, invert, \
		       snd_soc_dapm_get_volsw, snd_soc_dapm_put_volsw)
#define SOC_DAPM_SINGLE_VIRT(xname, max) \
	SOC_DAPM_SINGLE(xname, SND_SOC_NOPM, 0, max, 0)
#define SOC_DAPM_DOUBLE_R_TLV(xname, lreg, rreg, shift, max, invert, tlv_array) \
	SOC_DOUBLE_R_EXT_TLV(xname, lreg, rreg, shift, max, invert, \
			     snd_soc_dapm_get_volsw, snd_soc_dapm_put_volsw, \
			     tlv_array)
#define SOC_DAPM_SINGLE_TLV(xname, reg, shift, max, invert, tlv_array) \
	SOC_SINGLE_EXT_TLV(xname, reg, shift, max, invert, \
			   snd_soc_dapm_get_volsw, snd_soc_dapm_put_volsw, \
			   tlv_array)
#define SOC_DAPM_SINGLE_TLV_VIRT(xname, max, tlv_array) \
	SOC_DAPM_SINGLE(xname, SND_SOC_NOPM, 0, max, 0, tlv_array)
#define SOC_DAPM_ENUM(xname, xenum) \
	SOC_ENUM_EXT(xname, xenum, snd_soc_dapm_get_enum_double, \
		     snd_soc_dapm_put_enum_double)
#define SOC_DAPM_ENUM_EXT(xname, xenum, xget, xput) \
	SOC_ENUM_EXT(xname, xenum, xget, xput)

#define SOC_DAPM_SINGLE_AUTODISABLE(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, 0, max, invert, 1) }
#define SOC_DAPM_SINGLE_TLV_AUTODISABLE(xname, reg, shift, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, 0, max, invert, 1) }
#define SOC_DAPM_PIN_SWITCH(xname) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname " Switch", \
	.info = snd_soc_dapm_info_pin_switch, \
	.get = snd_soc_dapm_get_pin_switch, \
	.put = snd_soc_dapm_put_pin_switch, \
	.private_value = (unsigned long)xname }

/* dapm stream operations */
#define SND_SOC_DAPM_STREAM_NOP			0x0
#define SND_SOC_DAPM_STREAM_START		0x1
#define SND_SOC_DAPM_STREAM_STOP		0x2
#define SND_SOC_DAPM_STREAM_SUSPEND		0x4
#define SND_SOC_DAPM_STREAM_RESUME		0x8
#define SND_SOC_DAPM_STREAM_PAUSE_PUSH		0x10
#define SND_SOC_DAPM_STREAM_PAUSE_RELEASE	0x20

/* dapm event types */
#define SND_SOC_DAPM_PRE_PMU		0x1	/* before widget power up */
#define SND_SOC_DAPM_POST_PMU		0x2	/* after  widget power up */
#define SND_SOC_DAPM_PRE_PMD		0x4	/* before widget power down */
#define SND_SOC_DAPM_POST_PMD		0x8	/* after  widget power down */
#define SND_SOC_DAPM_PRE_REG		0x10	/* before audio path setup */
#define SND_SOC_DAPM_POST_REG		0x20	/* after  audio path setup */
#define SND_SOC_DAPM_WILL_PMU		0x40	/* called at start of sequence */
#define SND_SOC_DAPM_WILL_PMD		0x80	/* called at start of sequence */
#define SND_SOC_DAPM_PRE_POST_PMD	(SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD)
#define SND_SOC_DAPM_PRE_POST_PMU	(SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU)

/* convenience event type detection */
#define SND_SOC_DAPM_EVENT_ON(e)	(e & (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU))
#define SND_SOC_DAPM_EVENT_OFF(e)	(e & (SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD))

/* regulator widget flags */
#define SND_SOC_DAPM_REGULATOR_BYPASS	0x1	/* bypass when disabled */

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

/* dapm widget types */
enum snd_soc_dapm_type {
	snd_soc_dapm_input = 0,		/* input pin */
	snd_soc_dapm_output,		/* output pin */
	snd_soc_dapm_mux,		/* selects 1 analog signal from many inputs */
	snd_soc_dapm_demux,		/* connects the input to one of multiple outputs */
	snd_soc_dapm_mixer,		/* mixes several analog signals together */
	snd_soc_dapm_mixer_named_ctl,	/* mixer with named controls */
	snd_soc_dapm_pga,		/* programmable gain/attenuation (volume) */
	snd_soc_dapm_out_drv,		/* output driver */
	snd_soc_dapm_adc,		/* analog to digital converter */
	snd_soc_dapm_dac,		/* digital to analog converter */
	snd_soc_dapm_micbias,		/* microphone bias (power) - DEPRECATED: use snd_soc_dapm_supply */
	snd_soc_dapm_mic,		/* microphone */
	snd_soc_dapm_hp,		/* headphones */
	snd_soc_dapm_spk,		/* speaker */
	snd_soc_dapm_line,		/* line input/output */
	snd_soc_dapm_switch,		/* analog switch */
	snd_soc_dapm_vmid,		/* codec bias/vmid - to minimise pops */
	snd_soc_dapm_pre,		/* machine specific pre widget - exec first */
	snd_soc_dapm_post,		/* machine specific post widget - exec last */
	snd_soc_dapm_supply,		/* power/clock supply */
	snd_soc_dapm_pinctrl,		/* pinctrl */
	snd_soc_dapm_regulator_supply,	/* external regulator */
	snd_soc_dapm_clock_supply,	/* external clock */
	snd_soc_dapm_aif_in,		/* audio interface input */
	snd_soc_dapm_aif_out,		/* audio interface output */
	snd_soc_dapm_siggen,		/* signal generator */
	snd_soc_dapm_sink,
	snd_soc_dapm_dai_in,		/* link to DAI structure */
	snd_soc_dapm_dai_out,
	snd_soc_dapm_dai_link,		/* link between two DAI structures */
	snd_soc_dapm_kcontrol,		/* Auto-disabled kcontrol */
	snd_soc_dapm_buffer,		/* DSP/CODEC internal buffer */
	snd_soc_dapm_scheduler,		/* DSP/CODEC internal scheduler */
	snd_soc_dapm_effect,		/* DSP/CODEC effect component */
	snd_soc_dapm_src,		/* DSP/CODEC SRC component */
	snd_soc_dapm_asrc,		/* DSP/CODEC ASRC component */
	snd_soc_dapm_encoder,		/* FW/SW audio encoder component */
	snd_soc_dapm_decoder,		/* FW/SW audio decoder component */

	/* Don't edit below this line */
	SND_SOC_DAPM_TYPE_COUNT
};

/*
 * DAPM audio route definition.
 *
 * Defines an audio route originating at source via control and finishing
 * at sink.
 */
struct snd_soc_dapm_route {
	const char *sink;
	const char *control;
	const char *source;

	/* Note: currently only supported for links where source is a supply */
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink);

	struct snd_soc_dobj dobj;
};

/* dapm audio path between two widgets */
struct snd_soc_dapm_path {
	const char *name;

	/*
	 * source (input) and sink (output) widgets
	 * The union is for convience, since it is a lot nicer to type
	 * p->source, rather than p->node[SND_SOC_DAPM_DIR_IN]
	 */
	union {
		struct {
			struct snd_soc_dapm_widget *source;
			struct snd_soc_dapm_widget *sink;
		};
		struct snd_soc_dapm_widget *node[2];
	};

	/* status */
	u32 connect:1;		/* source and sink widgets are connected */
	u32 walking:1;		/* path is in the process of being walked */
	u32 is_supply:1;	/* At least one of the connected widgets is a supply */

	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink);

	struct list_head list_node[2];
	struct list_head list_kcontrol;
	struct list_head list;
};

/* dapm widget */
struct snd_soc_dapm_widget {
	enum snd_soc_dapm_type id;
	const char *name;			/* widget name */
	const char *sname;			/* stream name */
	struct list_head list;
	struct snd_soc_dapm_context *dapm;

	void *priv;				/* widget specific data */
	struct regulator *regulator;		/* attached regulator */
	struct pinctrl *pinctrl;		/* attached pinctrl */

	/* dapm control */
	int reg;				/* negative reg = no direct dapm */
	unsigned char shift;			/* bits to shift */
	unsigned int mask;			/* non-shifted mask */
	unsigned int on_val;			/* on state value */
	unsigned int off_val;			/* off state value */
	unsigned char power:1;			/* block power status */
	unsigned char active:1;			/* active stream on DAC, ADC's */
	unsigned char connected:1;		/* connected codec pin */
	unsigned char new:1;			/* cnew complete */
	unsigned char force:1;			/* force state */
	unsigned char ignore_suspend:1;		/* kept enabled over suspend */
	unsigned char new_power:1;		/* power from this run */
	unsigned char power_checked:1;		/* power checked this run */
	unsigned char is_supply:1;		/* Widget is a supply type widget */
	unsigned char is_ep:2;			/* Widget is a endpoint type widget */
	unsigned char no_wname_in_kcontrol_name:1; /* No widget name prefix in kcontrol name */
	int subseq;				/* sort within widget type */

	int (*power_check)(struct snd_soc_dapm_widget *w);

	/* external events */
	unsigned short event_flags;		/* flags to specify event types */
	int (*event)(struct snd_soc_dapm_widget*, struct snd_kcontrol *, int);

	/* kcontrols that relate to this widget */
	int num_kcontrols;
	const struct snd_kcontrol_new *kcontrol_news;
	struct snd_kcontrol **kcontrols;
	struct snd_soc_dobj dobj;

	/* widget input and output edges */
	struct list_head edges[2];

	/* used during DAPM updates */
	struct list_head work_list;
	struct list_head power_list;
	struct list_head dirty;
	int endpoints[2];

	struct clk *clk;

	int channel;
};

struct snd_soc_dapm_update {
	struct snd_kcontrol *kcontrol;
	int reg;
	int mask;
	int val;
	int reg2;
	int mask2;
	int val2;
	bool has_second_set;
};

/* DAPM context */
struct snd_soc_dapm_context {
	enum snd_soc_bias_level bias_level;

	/* bit field */
	unsigned int idle_bias_off:1;		/* Use BIAS_OFF instead of STANDBY */
	unsigned int suspend_bias_off:1;	/* Use BIAS_OFF in suspend if the DAPM is idle */

	struct device *dev;			/* from parent - for debug */
	struct snd_soc_component *component;	/* parent component */
	struct snd_soc_card *card;		/* parent card */

	/* used during DAPM updates */
	enum snd_soc_bias_level target_bias_level;
	struct list_head list;

	struct snd_soc_dapm_widget *wcache_sink;
	struct snd_soc_dapm_widget *wcache_source;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dapm;
#endif
};

/* A list of widgets associated with an object, typically a snd_kcontrol */
struct snd_soc_dapm_widget_list {
	int num_widgets;
	struct snd_soc_dapm_widget *widgets[] __counted_by(num_widgets);
};

struct snd_soc_dapm_stats {
	int power_checks;
	int path_checks;
	int neighbour_checks;
};

struct snd_soc_dapm_pinctrl_priv {
	const char *active_state;
	const char *sleep_state;
};

enum snd_soc_dapm_direction {
	SND_SOC_DAPM_DIR_IN,
	SND_SOC_DAPM_DIR_OUT
};

#define SND_SOC_DAPM_DIR_TO_EP(x) BIT(x)

#define SND_SOC_DAPM_EP_SOURCE	SND_SOC_DAPM_DIR_TO_EP(SND_SOC_DAPM_DIR_IN)
#define SND_SOC_DAPM_EP_SINK	SND_SOC_DAPM_DIR_TO_EP(SND_SOC_DAPM_DIR_OUT)

int snd_soc_dapm_regulator_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event);
int snd_soc_dapm_clock_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event);
int snd_soc_dapm_pinctrl_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event);

/* dapm controls */
int snd_soc_dapm_put_volsw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_get_volsw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_get_enum_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_put_enum_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_info_pin_switch(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo);
int snd_soc_dapm_get_pin_switch(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *uncontrol);
int snd_soc_dapm_put_pin_switch(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *uncontrol);
int snd_soc_dapm_get_component_pin_switch(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *uncontrol);
int snd_soc_dapm_put_component_pin_switch(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *uncontrol);
int snd_soc_dapm_new_controls(struct snd_soc_dapm_context *dapm,
	const struct snd_soc_dapm_widget *widget, unsigned int num);
struct snd_soc_dapm_widget *snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
		const struct snd_soc_dapm_widget *widget);
struct snd_soc_dapm_widget *snd_soc_dapm_new_control_unlocked(struct snd_soc_dapm_context *dapm,
		const struct snd_soc_dapm_widget *widget);
int snd_soc_dapm_new_dai_widgets(struct snd_soc_dapm_context *dapm, struct snd_soc_dai *dai);
void snd_soc_dapm_free_widget(struct snd_soc_dapm_widget *w);
int snd_soc_dapm_link_dai_widgets(struct snd_soc_card *card);
void snd_soc_dapm_connect_dai_link_widgets(struct snd_soc_card *card);

int snd_soc_dapm_update_dai(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params, struct snd_soc_dai *dai);
int snd_soc_dapm_widget_name_cmp(struct snd_soc_dapm_widget *widget, const char *s);

/* dapm path setup */
int snd_soc_dapm_new_widgets(struct snd_soc_card *card);
void snd_soc_dapm_free(struct snd_soc_dapm_context *dapm);
void snd_soc_dapm_init(struct snd_soc_dapm_context *dapm,
		       struct snd_soc_card *card, struct snd_soc_component *component);
int snd_soc_dapm_add_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num);
int snd_soc_dapm_del_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num);
void snd_soc_dapm_free_widget(struct snd_soc_dapm_widget *w);

/* dapm events */
void snd_soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream, int event);
void snd_soc_dapm_stream_stop(struct snd_soc_pcm_runtime *rtd, int stream);
void snd_soc_dapm_shutdown(struct snd_soc_card *card);

/* external DAPM widget events */
int snd_soc_dapm_mixer_update_power(struct snd_soc_dapm_context *dapm,
		struct snd_kcontrol *kcontrol, int connect, struct snd_soc_dapm_update *update);
int snd_soc_dapm_mux_update_power(struct snd_soc_dapm_context *dapm,
		struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e,
		struct snd_soc_dapm_update *update);

/* dapm sys fs - used by the core */
extern struct attribute *snd_soc_dapm_dev_attrs[];
void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm, struct dentry *parent);

/* dapm audio pin control and status */
int snd_soc_dapm_enable_pin(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_enable_pin_unlocked(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_disable_pin(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_disable_pin_unlocked(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_get_pin_status(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_sync(struct snd_soc_dapm_context *dapm);
int snd_soc_dapm_sync_unlocked(struct snd_soc_dapm_context *dapm);
int snd_soc_dapm_force_enable_pin(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_force_enable_pin_unlocked(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_ignore_suspend(struct snd_soc_dapm_context *dapm, const char *pin);
unsigned int dapm_kcontrol_get_value(const struct snd_kcontrol *kcontrol);
void snd_soc_dapm_mark_endpoints_dirty(struct snd_soc_card *card);

/*
 * Marks the specified pin as being not connected, disabling it along
 * any parent or child widgets.  At present this is identical to
 * snd_soc_dapm_disable_pin[_unlocked]() but in future it will be extended to do
 * additional things such as disabling controls which only affect
 * paths through the pin.
 */
#define snd_soc_dapm_nc_pin		snd_soc_dapm_disable_pin
#define snd_soc_dapm_nc_pin_unlocked	snd_soc_dapm_disable_pin_unlocked

/* dapm path query */
int snd_soc_dapm_dai_get_connected_widgets(struct snd_soc_dai *dai, int stream,
	struct snd_soc_dapm_widget_list **list,
	bool (*custom_stop_condition)(struct snd_soc_dapm_widget *, enum snd_soc_dapm_direction));
void snd_soc_dapm_dai_free_widgets(struct snd_soc_dapm_widget_list **list);

struct snd_soc_dapm_context *snd_soc_dapm_kcontrol_dapm(struct snd_kcontrol *kcontrol);
struct snd_soc_dapm_widget *snd_soc_dapm_kcontrol_widget(struct snd_kcontrol *kcontrol);

int snd_soc_dapm_force_bias_level(struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level);

#define for_each_dapm_widgets(list, i, widget)				\
	for ((i) = 0;							\
	     (i) < list->num_widgets && (widget = list->widgets[i]);	\
	     (i)++)

/**
 * snd_soc_dapm_init_bias_level() - Initialize DAPM bias level
 * @dapm: The DAPM context to initialize
 * @level: The DAPM level to initialize to
 *
 * This function only sets the driver internal state of the DAPM level and will
 * not modify the state of the device. Hence it should not be used during normal
 * operation, but only to synchronize the internal state to the device state.
 * E.g. during driver probe to set the DAPM level to the one corresponding with
 * the power-on reset state of the device.
 *
 * To change the DAPM state of the device use snd_soc_dapm_set_bias_level().
 */
static inline void snd_soc_dapm_init_bias_level(
	struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
	dapm->bias_level = level;
}

/**
 * snd_soc_dapm_get_bias_level() - Get current DAPM bias level
 * @dapm: The context for which to get the bias level
 *
 * Returns: The current bias level of the passed DAPM context.
 */
static inline enum snd_soc_bias_level snd_soc_dapm_get_bias_level(
	struct snd_soc_dapm_context *dapm)
{
	return dapm->bias_level;
}

/**
 * snd_soc_dapm_widget_for_each_path - Iterates over all paths in the
 *   specified direction of a widget
 * @w: The widget
 * @dir: Whether to iterate over the paths where the specified widget is the
 *       incoming or outgoing widgets
 * @p: The path iterator variable
 */
#define snd_soc_dapm_widget_for_each_path(w, dir, p) \
	list_for_each_entry(p, &w->edges[dir], list_node[dir])

/**
 * snd_soc_dapm_widget_for_each_path_safe - Iterates over all paths in the
 *   specified direction of a widget
 * @w: The widget
 * @dir: Whether to iterate over the paths where the specified widget is the
 *       incoming or outgoing widgets
 * @p: The path iterator variable
 * @next_p: Temporary storage for the next path
 *
 *  This function works like snd_soc_dapm_widget_for_each_path, expect that
 *  it is safe to remove the current path from the list while iterating
 */
#define snd_soc_dapm_widget_for_each_path_safe(w, dir, p, next_p) \
	list_for_each_entry_safe(p, next_p, &w->edges[dir], list_node[dir])

/**
 * snd_soc_dapm_widget_for_each_sink_path - Iterates over all paths leaving a
 *  widget
 * @w: The widget
 * @p: The path iterator variable
 */
#define snd_soc_dapm_widget_for_each_sink_path(w, p) \
	snd_soc_dapm_widget_for_each_path(w, SND_SOC_DAPM_DIR_IN, p)

/**
 * snd_soc_dapm_widget_for_each_source_path - Iterates over all paths leading to
 *  a widget
 * @w: The widget
 * @p: The path iterator variable
 */
#define snd_soc_dapm_widget_for_each_source_path(w, p) \
	snd_soc_dapm_widget_for_each_path(w, SND_SOC_DAPM_DIR_OUT, p)

#endif
