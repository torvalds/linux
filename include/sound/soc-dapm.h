/*
 * linux/sound/soc-dapm.h -- ALSA SoC Dynamic Audio Power Management
 *
 * Author:		Liam Girdwood
 * Created:		Aug 11th 2005
 * Copyright:	Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_SOC_DAPM_H
#define __LINUX_SND_SOC_DAPM_H

#include <linux/types.h>
#include <sound/control.h>

struct device;

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
{	.id = snd_soc_dapm_vmid, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0}

/* platform domain */
#define SND_SOC_DAPM_SIGGEN(wname) \
{	.id = snd_soc_dapm_siggen, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_INPUT(wname) \
{	.id = snd_soc_dapm_input, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_OUTPUT(wname) \
{	.id = snd_soc_dapm_output, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM }
#define SND_SOC_DAPM_MIC(wname, wevent) \
{	.id = snd_soc_dapm_mic, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD}
#define SND_SOC_DAPM_HP(wname, wevent) \
{	.id = snd_soc_dapm_hp, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD}
#define SND_SOC_DAPM_SPK(wname, wevent) \
{	.id = snd_soc_dapm_spk, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD}
#define SND_SOC_DAPM_LINE(wname, wevent) \
{	.id = snd_soc_dapm_line, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD}

/* path domain */
#define SND_SOC_DAPM_PGA(wname, wreg, wshift, winvert,\
	 wcontrols, wncontrols) \
{	.id = snd_soc_dapm_pga, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
#define SND_SOC_DAPM_OUT_DRV(wname, wreg, wshift, winvert,\
	 wcontrols, wncontrols) \
{	.id = snd_soc_dapm_out_drv, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
#define SND_SOC_DAPM_MIXER(wname, wreg, wshift, winvert, \
	 wcontrols, wncontrols)\
{	.id = snd_soc_dapm_mixer, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = wncontrols}
#define SND_SOC_DAPM_MIXER_NAMED_CTL(wname, wreg, wshift, winvert, \
	 wcontrols, wncontrols)\
{       .id = snd_soc_dapm_mixer_named_ctl, .name = wname, .reg = wreg, \
	.shift = wshift, .invert = winvert, .kcontrol_news = wcontrols, \
	.num_kcontrols = wncontrols}
#define SND_SOC_DAPM_MICBIAS(wname, wreg, wshift, winvert) \
{	.id = snd_soc_dapm_micbias, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = NULL, .num_kcontrols = 0}
#define SND_SOC_DAPM_SWITCH(wname, wreg, wshift, winvert, wcontrols) \
{	.id = snd_soc_dapm_switch, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = 1}
#define SND_SOC_DAPM_MUX(wname, wreg, wshift, winvert, wcontrols) \
{	.id = snd_soc_dapm_mux, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = 1}
#define SND_SOC_DAPM_VIRT_MUX(wname, wreg, wshift, winvert, wcontrols) \
{	.id = snd_soc_dapm_virt_mux, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = 1}
#define SND_SOC_DAPM_VALUE_MUX(wname, wreg, wshift, winvert, wcontrols) \
{	.id = snd_soc_dapm_value_mux, .name = wname, .reg = wreg, \
	.shift = wshift, .invert = winvert, .kcontrol_news = wcontrols, \
	.num_kcontrols = 1}

/* Simplified versions of above macros, assuming wncontrols = ARRAY_SIZE(wcontrols) */
#define SOC_PGA_ARRAY(wname, wreg, wshift, winvert,\
	 wcontrols) \
{	.id = snd_soc_dapm_pga, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols)}
#define SOC_MIXER_ARRAY(wname, wreg, wshift, winvert, \
	 wcontrols)\
{	.id = snd_soc_dapm_mixer, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols)}
#define SOC_MIXER_NAMED_CTL_ARRAY(wname, wreg, wshift, winvert, \
	 wcontrols)\
{       .id = snd_soc_dapm_mixer_named_ctl, .name = wname, .reg = wreg, \
	.shift = wshift, .invert = winvert, .kcontrol_news = wcontrols, \
	.num_kcontrols = ARRAY_SIZE(wcontrols)}

/* path domain with event - event handler must return 0 for success */
#define SND_SOC_DAPM_PGA_E(wname, wreg, wshift, winvert, wcontrols, \
	wncontrols, wevent, wflags) \
{	.id = snd_soc_dapm_pga, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = wncontrols, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_OUT_DRV_E(wname, wreg, wshift, winvert, wcontrols, \
	wncontrols, wevent, wflags) \
{	.id = snd_soc_dapm_out_drv, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = wncontrols, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_MIXER_E(wname, wreg, wshift, winvert, wcontrols, \
	wncontrols, wevent, wflags) \
{	.id = snd_soc_dapm_mixer, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = wncontrols, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_MIXER_NAMED_CTL_E(wname, wreg, wshift, winvert, \
	wcontrols, wncontrols, wevent, wflags) \
{       .id = snd_soc_dapm_mixer, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, \
	.num_kcontrols = wncontrols, .event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_SWITCH_E(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
{	.id = snd_soc_dapm_switch, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = 1, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_MUX_E(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
{	.id = snd_soc_dapm_mux, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = 1, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_VIRT_MUX_E(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
{	.id = snd_soc_dapm_virt_mux, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = 1, \
	.event = wevent, .event_flags = wflags}

/* additional sequencing control within an event type */
#define SND_SOC_DAPM_PGA_S(wname, wsubseq, wreg, wshift, winvert, \
	wevent, wflags) \
{	.id = snd_soc_dapm_pga, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .event = wevent, .event_flags = wflags, \
	.subseq = wsubseq}
#define SND_SOC_DAPM_SUPPLY_S(wname, wsubseq, wreg, wshift, winvert, wevent, \
	wflags)	\
{	.id = snd_soc_dapm_supply, .name = wname, .reg = wreg,	\
	.shift = wshift, .invert = winvert, .event = wevent, \
	.event_flags = wflags, .subseq = wsubseq}

/* Simplified versions of above macros, assuming wncontrols = ARRAY_SIZE(wcontrols) */
#define SOC_PGA_E_ARRAY(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
{	.id = snd_soc_dapm_pga, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols), \
	.event = wevent, .event_flags = wflags}
#define SOC_MIXER_E_ARRAY(wname, wreg, wshift, winvert, wcontrols, \
	wevent, wflags) \
{	.id = snd_soc_dapm_mixer, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols), \
	.event = wevent, .event_flags = wflags}
#define SOC_MIXER_NAMED_CTL_E_ARRAY(wname, wreg, wshift, winvert, \
	wcontrols, wevent, wflags) \
{       .id = snd_soc_dapm_mixer, .name = wname, .reg = wreg, .shift = wshift, \
	.invert = winvert, .kcontrol_news = wcontrols, \
	.num_kcontrols = ARRAY_SIZE(wcontrols), .event = wevent, .event_flags = wflags}

/* events that are pre and post DAPM */
#define SND_SOC_DAPM_PRE(wname, wevent) \
{	.id = snd_soc_dapm_pre, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD}
#define SND_SOC_DAPM_POST(wname, wevent) \
{	.id = snd_soc_dapm_post, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = wevent, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD}

/* stream domain */
#define SND_SOC_DAPM_AIF_IN(wname, stname, wslot, wreg, wshift, winvert) \
{	.id = snd_soc_dapm_aif_in, .name = wname, .sname = stname, \
	.reg = wreg, .shift = wshift, .invert = winvert }
#define SND_SOC_DAPM_AIF_IN_E(wname, stname, wslot, wreg, wshift, winvert, \
			      wevent, wflags)				\
{	.id = snd_soc_dapm_aif_in, .name = wname, .sname = stname, \
	.reg = wreg, .shift = wshift, .invert = winvert, \
	.event = wevent, .event_flags = wflags }
#define SND_SOC_DAPM_AIF_OUT(wname, stname, wslot, wreg, wshift, winvert) \
{	.id = snd_soc_dapm_aif_out, .name = wname, .sname = stname, \
	.reg = wreg, .shift = wshift, .invert = winvert }
#define SND_SOC_DAPM_AIF_OUT_E(wname, stname, wslot, wreg, wshift, winvert, \
			     wevent, wflags)				\
{	.id = snd_soc_dapm_aif_out, .name = wname, .sname = stname, \
	.reg = wreg, .shift = wshift, .invert = winvert, \
	.event = wevent, .event_flags = wflags }
#define SND_SOC_DAPM_DAC(wname, stname, wreg, wshift, winvert) \
{	.id = snd_soc_dapm_dac, .name = wname, .sname = stname, .reg = wreg, \
	.shift = wshift, .invert = winvert}
#define SND_SOC_DAPM_DAC_E(wname, stname, wreg, wshift, winvert, \
			   wevent, wflags)				\
{	.id = snd_soc_dapm_dac, .name = wname, .sname = stname, .reg = wreg, \
	.shift = wshift, .invert = winvert, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_ADC(wname, stname, wreg, wshift, winvert) \
{	.id = snd_soc_dapm_adc, .name = wname, .sname = stname, .reg = wreg, \
	.shift = wshift, .invert = winvert}
#define SND_SOC_DAPM_ADC_E(wname, stname, wreg, wshift, winvert, \
			   wevent, wflags)				\
{	.id = snd_soc_dapm_adc, .name = wname, .sname = stname, .reg = wreg, \
	.shift = wshift, .invert = winvert, \
	.event = wevent, .event_flags = wflags}
#define SND_SOC_DAPM_CLOCK_SUPPLY(wname) \
{	.id = snd_soc_dapm_clock_supply, .name = wname, \
	.reg = SND_SOC_NOPM, .event = dapm_clock_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD }

/* generic widgets */
#define SND_SOC_DAPM_REG(wid, wname, wreg, wshift, wmask, won_val, woff_val) \
{	.id = wid, .name = wname, .kcontrol_news = NULL, .num_kcontrols = 0, \
	.reg = -((wreg) + 1), .shift = wshift, .mask = wmask, \
	.on_val = won_val, .off_val = woff_val, .event = dapm_reg_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD}
#define SND_SOC_DAPM_SUPPLY(wname, wreg, wshift, winvert, wevent, wflags) \
{	.id = snd_soc_dapm_supply, .name = wname, .reg = wreg,	\
	.shift = wshift, .invert = winvert, .event = wevent, \
	.event_flags = wflags}
#define SND_SOC_DAPM_REGULATOR_SUPPLY(wname, wdelay) \
{	.id = snd_soc_dapm_regulator_supply, .name = wname, \
	.reg = SND_SOC_NOPM, .shift = wdelay, .event = dapm_regulator_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD }


/* dapm kcontrol types */
#define SOC_DAPM_SINGLE(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }
#define SOC_DAPM_SINGLE_TLV(xname, reg, shift, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }
#define SOC_DAPM_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
 	.get = snd_soc_dapm_get_enum_double, \
 	.put = snd_soc_dapm_put_enum_double, \
  	.private_value = (unsigned long)&xenum }
#define SOC_DAPM_ENUM_VIRT(xname, xenum)		    \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
	.get = snd_soc_dapm_get_enum_virt, \
	.put = snd_soc_dapm_put_enum_virt, \
	.private_value = (unsigned long)&xenum }
#define SOC_DAPM_ENUM_EXT(xname, xenum, xget, xput) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
	.get = xget, \
	.put = xput, \
	.private_value = (unsigned long)&xenum }
#define SOC_DAPM_VALUE_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
	.get = snd_soc_dapm_get_value_enum_double, \
	.put = snd_soc_dapm_put_value_enum_double, \
	.private_value = (unsigned long)&xenum }
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
#define SND_SOC_DAPM_STREAM_PAUSE_PUSH	0x10
#define SND_SOC_DAPM_STREAM_PAUSE_RELEASE	0x20

/* dapm event types */
#define SND_SOC_DAPM_PRE_PMU	0x1 	/* before widget power up */
#define SND_SOC_DAPM_POST_PMU	0x2		/* after widget power up */
#define SND_SOC_DAPM_PRE_PMD	0x4 	/* before widget power down */
#define SND_SOC_DAPM_POST_PMD	0x8		/* after widget power down */
#define SND_SOC_DAPM_PRE_REG	0x10	/* before audio path setup */
#define SND_SOC_DAPM_POST_REG	0x20	/* after audio path setup */
#define SND_SOC_DAPM_PRE_POST_PMD \
				(SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD)

/* convenience event type detection */
#define SND_SOC_DAPM_EVENT_ON(e)	\
	(e & (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU))
#define SND_SOC_DAPM_EVENT_OFF(e)	\
	(e & (SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD))

struct snd_soc_dapm_widget;
enum snd_soc_dapm_type;
struct snd_soc_dapm_path;
struct snd_soc_dapm_pin;
struct snd_soc_dapm_route;
struct snd_soc_dapm_context;
struct regulator;
struct snd_soc_dapm_widget_list;

int dapm_reg_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event);
int dapm_regulator_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event);
int dapm_clock_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event);

/* dapm controls */
int snd_soc_dapm_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_get_enum_virt(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_put_enum_virt(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_get_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_put_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_dapm_info_pin_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_dapm_get_pin_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *uncontrol);
int snd_soc_dapm_put_pin_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *uncontrol);
int snd_soc_dapm_new_controls(struct snd_soc_dapm_context *dapm,
	const struct snd_soc_dapm_widget *widget,
	int num);
int snd_soc_dapm_new_dai_widgets(struct snd_soc_dapm_context *dapm,
				 struct snd_soc_dai *dai);
int snd_soc_dapm_link_dai_widgets(struct snd_soc_card *card);
int snd_soc_dapm_new_pcm(struct snd_soc_card *card,
			 const struct snd_soc_pcm_stream *params,
			 struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink);

/* dapm path setup */
int snd_soc_dapm_new_widgets(struct snd_soc_dapm_context *dapm);
void snd_soc_dapm_free(struct snd_soc_dapm_context *dapm);
int snd_soc_dapm_add_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num);
int snd_soc_dapm_del_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num);
int snd_soc_dapm_weak_routes(struct snd_soc_dapm_context *dapm,
			     const struct snd_soc_dapm_route *route, int num);

/* dapm events */
void snd_soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream,
	int event);
void snd_soc_dapm_shutdown(struct snd_soc_card *card);

/* external DAPM widget events */
int snd_soc_dapm_mixer_update_power(struct snd_soc_dapm_widget *widget,
		struct snd_kcontrol *kcontrol, int connect);
int snd_soc_dapm_mux_update_power(struct snd_soc_dapm_widget *widget,
				 struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e);

/* dapm sys fs - used by the core */
int snd_soc_dapm_sys_add(struct device *dev);
void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm,
				struct dentry *parent);

/* dapm audio pin control and status */
int snd_soc_dapm_enable_pin(struct snd_soc_dapm_context *dapm,
			    const char *pin);
int snd_soc_dapm_disable_pin(struct snd_soc_dapm_context *dapm,
			     const char *pin);
int snd_soc_dapm_nc_pin(struct snd_soc_dapm_context *dapm, const char *pin);
int snd_soc_dapm_get_pin_status(struct snd_soc_dapm_context *dapm,
				const char *pin);
int snd_soc_dapm_sync(struct snd_soc_dapm_context *dapm);
int snd_soc_dapm_force_enable_pin(struct snd_soc_dapm_context *dapm,
				  const char *pin);
int snd_soc_dapm_ignore_suspend(struct snd_soc_dapm_context *dapm,
				const char *pin);
void snd_soc_dapm_auto_nc_codec_pins(struct snd_soc_codec *codec);

/* Mostly internal - should not normally be used */
void dapm_mark_dirty(struct snd_soc_dapm_widget *w, const char *reason);

/* dapm path query */
int snd_soc_dapm_dai_get_connected_widgets(struct snd_soc_dai *dai, int stream,
	struct snd_soc_dapm_widget_list **list);

/* dapm widget types */
enum snd_soc_dapm_type {
	snd_soc_dapm_input = 0,		/* input pin */
	snd_soc_dapm_output,		/* output pin */
	snd_soc_dapm_mux,			/* selects 1 analog signal from many inputs */
	snd_soc_dapm_virt_mux,			/* virtual version of snd_soc_dapm_mux */
	snd_soc_dapm_value_mux,			/* selects 1 analog signal from many inputs */
	snd_soc_dapm_mixer,			/* mixes several analog signals together */
	snd_soc_dapm_mixer_named_ctl,		/* mixer with named controls */
	snd_soc_dapm_pga,			/* programmable gain/attenuation (volume) */
	snd_soc_dapm_out_drv,			/* output driver */
	snd_soc_dapm_adc,			/* analog to digital converter */
	snd_soc_dapm_dac,			/* digital to analog converter */
	snd_soc_dapm_micbias,		/* microphone bias (power) */
	snd_soc_dapm_mic,			/* microphone */
	snd_soc_dapm_hp,			/* headphones */
	snd_soc_dapm_spk,			/* speaker */
	snd_soc_dapm_line,			/* line input/output */
	snd_soc_dapm_switch,		/* analog switch */
	snd_soc_dapm_vmid,			/* codec bias/vmid - to minimise pops */
	snd_soc_dapm_pre,			/* machine specific pre widget - exec first */
	snd_soc_dapm_post,			/* machine specific post widget - exec last */
	snd_soc_dapm_supply,		/* power/clock supply */
	snd_soc_dapm_regulator_supply,	/* external regulator */
	snd_soc_dapm_clock_supply,	/* external clock */
	snd_soc_dapm_aif_in,		/* audio interface input */
	snd_soc_dapm_aif_out,		/* audio interface output */
	snd_soc_dapm_siggen,		/* signal generator */
	snd_soc_dapm_dai,		/* link to DAI structure */
	snd_soc_dapm_dai_link,		/* link between two DAI structures */
};

enum snd_soc_dapm_subclass {
	SND_SOC_DAPM_CLASS_INIT		= 0,
	SND_SOC_DAPM_CLASS_RUNTIME	= 1,
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
};

/* dapm audio path between two widgets */
struct snd_soc_dapm_path {
	const char *name;
	const char *long_name;

	/* source (input) and sink (output) widgets */
	struct snd_soc_dapm_widget *source;
	struct snd_soc_dapm_widget *sink;
	struct snd_kcontrol *kcontrol;

	/* status */
	u32 connect:1;	/* source and sink widgets are connected */
	u32 walked:1;	/* path has been walked */
	u32 weak:1;	/* path ignored for power management */

	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink);

	struct list_head list_source;
	struct list_head list_sink;
	struct list_head list;
};

/* dapm widget */
struct snd_soc_dapm_widget {
	enum snd_soc_dapm_type id;
	const char *name;		/* widget name */
	const char *sname;	/* stream name */
	struct snd_soc_codec *codec;
	struct snd_soc_platform *platform;
	struct list_head list;
	struct snd_soc_dapm_context *dapm;

	void *priv;				/* widget specific data */
	struct regulator *regulator;		/* attached regulator */
	const struct snd_soc_pcm_stream *params; /* params for dai links */

	/* dapm control */
	int reg;				/* negative reg = no direct dapm */
	unsigned char shift;			/* bits to shift */
	unsigned int value;				/* widget current value */
	unsigned int mask;			/* non-shifted mask */
	unsigned int on_val;			/* on state value */
	unsigned int off_val;			/* off state value */
	unsigned char power:1;			/* block power status */
	unsigned char invert:1;			/* invert the power bit */
	unsigned char active:1;			/* active stream on DAC, ADC's */
	unsigned char connected:1;		/* connected codec pin */
	unsigned char new:1;			/* cnew complete */
	unsigned char ext:1;			/* has external widgets */
	unsigned char force:1;			/* force state */
	unsigned char ignore_suspend:1;         /* kept enabled over suspend */
	unsigned char new_power:1;		/* power from this run */
	unsigned char power_checked:1;		/* power checked this run */
	int subseq;				/* sort within widget type */

	int (*power_check)(struct snd_soc_dapm_widget *w);

	/* external events */
	unsigned short event_flags;		/* flags to specify event types */
	int (*event)(struct snd_soc_dapm_widget*, struct snd_kcontrol *, int);

	/* kcontrols that relate to this widget */
	int num_kcontrols;
	const struct snd_kcontrol_new *kcontrol_news;
	struct snd_kcontrol **kcontrols;

	/* widget input and outputs */
	struct list_head sources;
	struct list_head sinks;

	/* used during DAPM updates */
	struct list_head power_list;
	struct list_head dirty;
	int inputs;
	int outputs;

	struct clk *clk;
};

struct snd_soc_dapm_update {
	struct snd_soc_dapm_widget *widget;
	struct snd_kcontrol *kcontrol;
	int reg;
	int mask;
	int val;
};

/* DAPM context */
struct snd_soc_dapm_context {
	int n_widgets; /* number of widgets in this context */
	enum snd_soc_bias_level bias_level;
	enum snd_soc_bias_level suspend_bias_level;
	struct delayed_work delayed_work;
	unsigned int idle_bias_off:1; /* Use BIAS_OFF instead of STANDBY */

	struct snd_soc_dapm_update *update;

	void (*seq_notifier)(struct snd_soc_dapm_context *,
			     enum snd_soc_dapm_type, int);

	struct device *dev; /* from parent - for debug */
	struct snd_soc_codec *codec; /* parent codec */
	struct snd_soc_platform *platform; /* parent platform */
	struct snd_soc_card *card; /* parent card */

	/* used during DAPM updates */
	enum snd_soc_bias_level target_bias_level;
	struct list_head list;

	int (*stream_event)(struct snd_soc_dapm_context *dapm, int event);

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dapm;
#endif
};

/* A list of widgets associated with an object, typically a snd_kcontrol */
struct snd_soc_dapm_widget_list {
	int num_widgets;
	struct snd_soc_dapm_widget *widgets[0];
};

struct snd_soc_dapm_stats {
	int power_checks;
	int path_checks;
	int neighbour_checks;
};

#endif
