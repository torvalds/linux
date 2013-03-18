/*
 * arizona.h - Wolfson Arizona class device shared support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASOC_ARIZONA_H
#define _ASOC_ARIZONA_H

#include <linux/completion.h>

#include <sound/soc.h>

#include "wm_adsp.h"

#define ARIZONA_CLK_SYSCLK         1
#define ARIZONA_CLK_ASYNCCLK       2
#define ARIZONA_CLK_OPCLK          3
#define ARIZONA_CLK_ASYNC_OPCLK    4

#define ARIZONA_CLK_SRC_MCLK1    0x0
#define ARIZONA_CLK_SRC_MCLK2    0x1
#define ARIZONA_CLK_SRC_FLL1     0x4
#define ARIZONA_CLK_SRC_FLL2     0x5
#define ARIZONA_CLK_SRC_AIF1BCLK 0x8
#define ARIZONA_CLK_SRC_AIF2BCLK 0x9
#define ARIZONA_CLK_SRC_AIF3BCLK 0xa

#define ARIZONA_FLL_SRC_MCLK1      0
#define ARIZONA_FLL_SRC_MCLK2      1
#define ARIZONA_FLL_SRC_SLIMCLK    3
#define ARIZONA_FLL_SRC_FLL1       4
#define ARIZONA_FLL_SRC_FLL2       5
#define ARIZONA_FLL_SRC_AIF1BCLK   8
#define ARIZONA_FLL_SRC_AIF2BCLK   9
#define ARIZONA_FLL_SRC_AIF3BCLK  10
#define ARIZONA_FLL_SRC_AIF1LRCLK 12
#define ARIZONA_FLL_SRC_AIF2LRCLK 13
#define ARIZONA_FLL_SRC_AIF3LRCLK 14

#define ARIZONA_MIXER_VOL_MASK             0x00FE
#define ARIZONA_MIXER_VOL_SHIFT                 1
#define ARIZONA_MIXER_VOL_WIDTH                 7

#define ARIZONA_MAX_DAI  4
#define ARIZONA_MAX_ADSP 4

struct arizona;
struct wm_adsp;

struct arizona_dai_priv {
	int clk;
};

struct arizona_priv {
	struct wm_adsp adsp[ARIZONA_MAX_ADSP];
	struct arizona *arizona;
	int sysclk;
	int asyncclk;
	struct arizona_dai_priv dai[ARIZONA_MAX_DAI];
};

#define ARIZONA_NUM_MIXER_INPUTS 99

extern const unsigned int arizona_mixer_tlv[];
extern const char *arizona_mixer_texts[ARIZONA_NUM_MIXER_INPUTS];
extern int arizona_mixer_values[ARIZONA_NUM_MIXER_INPUTS];

#define ARIZONA_MIXER_CONTROLS(name, base) \
	SOC_SINGLE_RANGE_TLV(name " Input 1 Volume", base + 1,		\
			     ARIZONA_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     arizona_mixer_tlv),			\
	SOC_SINGLE_RANGE_TLV(name " Input 2 Volume", base + 3,		\
			     ARIZONA_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     arizona_mixer_tlv),			\
	SOC_SINGLE_RANGE_TLV(name " Input 3 Volume", base + 5,		\
			     ARIZONA_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     arizona_mixer_tlv),			\
	SOC_SINGLE_RANGE_TLV(name " Input 4 Volume", base + 7,		\
			     ARIZONA_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     arizona_mixer_tlv)

#define ARIZONA_MUX_ENUM_DECL(name, reg) \
	SOC_VALUE_ENUM_SINGLE_DECL(name, reg, 0, 0xff,			\
				   arizona_mixer_texts, arizona_mixer_values)

#define ARIZONA_MUX_CTL_DECL(name) \
	const struct snd_kcontrol_new name##_mux =	\
		SOC_DAPM_VALUE_ENUM("Route", name##_enum)

#define ARIZONA_MUX_ENUMS(name, base_reg) \
	static ARIZONA_MUX_ENUM_DECL(name##_enum, base_reg);      \
	static ARIZONA_MUX_CTL_DECL(name)

#define ARIZONA_MIXER_ENUMS(name, base_reg) \
	ARIZONA_MUX_ENUMS(name##_in1, base_reg);     \
	ARIZONA_MUX_ENUMS(name##_in2, base_reg + 2); \
	ARIZONA_MUX_ENUMS(name##_in3, base_reg + 4); \
	ARIZONA_MUX_ENUMS(name##_in4, base_reg + 6)

#define ARIZONA_DSP_AUX_ENUMS(name, base_reg) \
	ARIZONA_MUX_ENUMS(name##_aux1, base_reg);	\
	ARIZONA_MUX_ENUMS(name##_aux2, base_reg + 8);	\
	ARIZONA_MUX_ENUMS(name##_aux3, base_reg + 16);	\
	ARIZONA_MUX_ENUMS(name##_aux4, base_reg + 24);	\
	ARIZONA_MUX_ENUMS(name##_aux5, base_reg + 32);	\
	ARIZONA_MUX_ENUMS(name##_aux6, base_reg + 40)

#define ARIZONA_MUX(name, ctrl) \
	SND_SOC_DAPM_VALUE_MUX(name, SND_SOC_NOPM, 0, 0, ctrl)

#define ARIZONA_MUX_WIDGETS(name, name_str) \
	ARIZONA_MUX(name_str " Input", &name##_mux)

#define ARIZONA_MIXER_WIDGETS(name, name_str)	\
	ARIZONA_MUX(name_str " Input 1", &name##_in1_mux), \
	ARIZONA_MUX(name_str " Input 2", &name##_in2_mux), \
	ARIZONA_MUX(name_str " Input 3", &name##_in3_mux), \
	ARIZONA_MUX(name_str " Input 4", &name##_in4_mux), \
	SND_SOC_DAPM_MIXER(name_str " Mixer", SND_SOC_NOPM, 0, 0, NULL, 0)

#define ARIZONA_DSP_WIDGETS(name, name_str) \
	ARIZONA_MIXER_WIDGETS(name##L, name_str "L"), \
	ARIZONA_MIXER_WIDGETS(name##R, name_str "R"), \
	ARIZONA_MUX(name_str " Aux 1", &name##_aux1_mux), \
	ARIZONA_MUX(name_str " Aux 2", &name##_aux2_mux), \
	ARIZONA_MUX(name_str " Aux 3", &name##_aux3_mux), \
	ARIZONA_MUX(name_str " Aux 4", &name##_aux4_mux), \
	ARIZONA_MUX(name_str " Aux 5", &name##_aux5_mux), \
	ARIZONA_MUX(name_str " Aux 6", &name##_aux6_mux)

#define ARIZONA_MUX_ROUTES(name) \
	ARIZONA_MIXER_INPUT_ROUTES(name " Input")

#define ARIZONA_MIXER_ROUTES(widget, name) \
	{ widget, NULL, name " Mixer" },         \
	{ name " Mixer", NULL, name " Input 1" }, \
	{ name " Mixer", NULL, name " Input 2" }, \
	{ name " Mixer", NULL, name " Input 3" }, \
	{ name " Mixer", NULL, name " Input 4" }, \
	ARIZONA_MIXER_INPUT_ROUTES(name " Input 1"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Input 2"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Input 3"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Input 4")

#define ARIZONA_DSP_ROUTES(name) \
	{ name, NULL, name " Aux 1" }, \
	{ name, NULL, name " Aux 2" }, \
	{ name, NULL, name " Aux 3" }, \
	{ name, NULL, name " Aux 4" }, \
	{ name, NULL, name " Aux 5" }, \
	{ name, NULL, name " Aux 6" }, \
	ARIZONA_MIXER_INPUT_ROUTES(name " Aux 1"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Aux 2"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Aux 3"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Aux 4"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Aux 5"), \
	ARIZONA_MIXER_INPUT_ROUTES(name " Aux 6"), \
	ARIZONA_MIXER_ROUTES(name, name "L"), \
	ARIZONA_MIXER_ROUTES(name, name "R")

extern const struct soc_enum arizona_in_vi_ramp;
extern const struct soc_enum arizona_in_vd_ramp;

extern const struct soc_enum arizona_out_vi_ramp;
extern const struct soc_enum arizona_out_vd_ramp;

extern const struct soc_enum arizona_lhpf1_mode;
extern const struct soc_enum arizona_lhpf2_mode;
extern const struct soc_enum arizona_lhpf3_mode;
extern const struct soc_enum arizona_lhpf4_mode;

extern const struct soc_enum arizona_ng_hold;

extern int arizona_in_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event);
extern int arizona_out_ev(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event);

extern int arizona_set_sysclk(struct snd_soc_codec *codec, int clk_id,
			      int source, unsigned int freq, int dir);

extern const struct snd_soc_dai_ops arizona_dai_ops;

#define ARIZONA_FLL_NAME_LEN 20

struct arizona_fll {
	struct arizona *arizona;
	int id;
	unsigned int base;
	unsigned int vco_mult;
	struct completion ok;
	unsigned int fref;
	unsigned int fout;

	char lock_name[ARIZONA_FLL_NAME_LEN];
	char clock_ok_name[ARIZONA_FLL_NAME_LEN];
};

extern int arizona_init_fll(struct arizona *arizona, int id, int base,
			    int lock_irq, int ok_irq, struct arizona_fll *fll);
extern int arizona_set_fll(struct arizona_fll *fll, int source,
			   unsigned int Fref, unsigned int Fout);

extern int arizona_init_dai(struct arizona_priv *priv, int dai);

int arizona_set_output_mode(struct snd_soc_codec *codec, int output,
			    bool diff);

#endif
