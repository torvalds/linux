/* SPDX-License-Identifier: GPL-2.0
 *
 * simple_card_utils.h
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */

#ifndef __SIMPLE_CARD_UTILS_H
#define __SIMPLE_CARD_UTILS_H

#include <linux/clk.h>
#include <sound/soc.h>

#define simple_util_init_hp(card, sjack, prefix)		\
	simple_util_init_jack(card, sjack, 1, prefix, NULL)
#define simple_util_init_mic(card, sjack, prefix)		\
	simple_util_init_jack(card, sjack, 0, prefix, NULL)

struct simple_util_tdm_width_map {
	u8 sample_bits;
	u8 slot_count;
	u16 slot_width;
};

struct simple_util_dai {
	const char *name;
	unsigned int sysclk;
	int clk_direction;
	int slots;
	int slot_width;
	unsigned int tx_slot_mask;
	unsigned int rx_slot_mask;
	struct clk *clk;
	bool clk_fixed;
	struct simple_util_tdm_width_map *tdm_width_map;
	int n_tdm_widths;
};

struct simple_util_data {
	u32 convert_rate;
	u32 convert_channels;
	const char *convert_sample_format;
};

struct simple_util_jack {
	struct snd_soc_jack jack;
	struct snd_soc_jack_pin pin;
	struct snd_soc_jack_gpio gpio;
};

struct prop_nums {
	int cpus;
	int codecs;
	int platforms;
};

struct simple_util_priv {
	struct snd_soc_card snd_card;
	struct simple_dai_props {
		struct simple_util_dai *cpu_dai;
		struct simple_util_dai *codec_dai;
		struct simple_util_data adata;
		struct snd_soc_codec_conf *codec_conf;
		struct prop_nums num;
		unsigned int mclk_fs;
	} *dai_props;
	struct simple_util_jack hp_jack;
	struct simple_util_jack mic_jack;
	struct snd_soc_jack *aux_jacks;
	struct snd_soc_dai_link *dai_link;
	struct simple_util_dai *dais;
	struct snd_soc_dai_link_component *dlcs;
	struct snd_soc_codec_conf *codec_conf;
	struct gpio_desc *pa_gpio;
	const struct snd_soc_ops *ops;
	unsigned int dpcm_selectable:1;
	unsigned int force_dpcm:1;
};
#define simple_priv_to_card(priv)	(&(priv)->snd_card)
#define simple_priv_to_props(priv, i)	((priv)->dai_props + (i))
#define simple_priv_to_dev(priv)	(simple_priv_to_card(priv)->dev)
#define simple_priv_to_link(priv, i)	(simple_priv_to_card(priv)->dai_link + (i))

#define simple_props_to_dlc_cpu(props, i)	((props)->cpus + i)
#define simple_props_to_dlc_codec(props, i)	((props)->codecs + i)
#define simple_props_to_dlc_platform(props, i)	((props)->platforms + i)

#define simple_props_to_dai_cpu(props, i)	((props)->cpu_dai + i)
#define simple_props_to_dai_codec(props, i)	((props)->codec_dai + i)
#define simple_props_to_codec_conf(props, i)	((props)->codec_conf + i)

/* has the same effect as simple_priv_to_props(). Preferred over
 * simple_priv_to_props() when dealing with PCM runtime data as
 * the ID stored in rtd->id may not be a valid array index.
 */
#define runtime_simple_priv_to_props(priv, rtd)				\
	((priv)->dai_props + ((rtd)->dai_link - (priv)->dai_link))

#define for_each_prop_dlc_cpus(props, i, cpu)				\
	for ((i) = 0;							\
	     ((i) < (props)->num.cpus) &&				\
		     ((cpu) = simple_props_to_dlc_cpu(props, i));	\
	     (i)++)
#define for_each_prop_dlc_codecs(props, i, codec)			\
	for ((i) = 0;							\
	     ((i) < (props)->num.codecs) &&				\
		     ((codec) = simple_props_to_dlc_codec(props, i));	\
	     (i)++)
#define for_each_prop_dlc_platforms(props, i, platform)			\
	for ((i) = 0;							\
	     ((i) < (props)->num.platforms) &&				\
		     ((platform) = simple_props_to_dlc_platform(props, i)); \
	     (i)++)
#define for_each_prop_codec_conf(props, i, conf)			\
	for ((i) = 0;							\
	     ((i) < (props)->num.codecs) &&				\
		     (props)->codec_conf &&				\
		     ((conf) = simple_props_to_codec_conf(props, i));	\
	     (i)++)

#define for_each_prop_dai_cpu(props, i, cpu)				\
	for ((i) = 0;							\
	     ((i) < (props)->num.cpus) &&				\
		     ((cpu) = simple_props_to_dai_cpu(props, i));	\
	     (i)++)
#define for_each_prop_dai_codec(props, i, codec)			\
	for ((i) = 0;							\
	     ((i) < (props)->num.codecs) &&				\
		     ((codec) = simple_props_to_dai_codec(props, i));	\
	     (i)++)

#define SNDRV_MAX_LINKS 512

struct link_info {
	int link; /* number of link */
	int cpu;  /* turn for CPU / Codec */
	struct prop_nums num[SNDRV_MAX_LINKS];
};

int simple_util_parse_daifmt(struct device *dev,
			     struct device_node *node,
			     struct device_node *codec,
			     char *prefix,
			     unsigned int *retfmt);
int simple_util_parse_tdm_width_map(struct simple_util_priv *priv, struct device_node *np,
				    struct simple_util_dai *dai);

__printf(3, 4)
int simple_util_set_dailink_name(struct simple_util_priv *priv,
				 struct snd_soc_dai_link *dai_link,
				 const char *fmt, ...);
int simple_util_parse_card_name(struct simple_util_priv *priv,
				char *prefix);

int simple_util_parse_clk(struct device *dev,
			  struct device_node *node,
			  struct simple_util_dai *simple_dai,
			  struct snd_soc_dai_link_component *dlc);
int simple_util_startup(struct snd_pcm_substream *substream);
void simple_util_shutdown(struct snd_pcm_substream *substream);
int simple_util_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params);
int simple_util_dai_init(struct snd_soc_pcm_runtime *rtd);
int simple_util_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				   struct snd_pcm_hw_params *params);

#define simple_util_parse_tdm(np, dai)				\
	snd_soc_of_parse_tdm_slot(np,	&(dai)->tx_slot_mask,	\
					&(dai)->rx_slot_mask,	\
					&(dai)->slots,		\
					&(dai)->slot_width);

void simple_util_canonicalize_platform(struct snd_soc_dai_link_component *platforms,
				       struct snd_soc_dai_link_component *cpus);
void simple_util_canonicalize_cpu(struct snd_soc_dai_link_component *cpus,
				  int is_single_links);

void simple_util_clean_reference(struct snd_soc_card *card);

void simple_util_parse_convert(struct device_node *np, char *prefix,
			       struct simple_util_data *data);
bool simple_util_is_convert_required(const struct simple_util_data *data);

int simple_util_get_sample_fmt(struct simple_util_data *data);

int simple_util_parse_routing(struct snd_soc_card *card,
				      char *prefix);
int simple_util_parse_widgets(struct snd_soc_card *card,
				      char *prefix);
int simple_util_parse_pin_switches(struct snd_soc_card *card,
				   char *prefix);

int simple_util_init_jack(struct snd_soc_card *card,
			       struct simple_util_jack *sjack,
			       int is_hp, char *prefix, char *pin);
int simple_util_init_aux_jacks(struct simple_util_priv *priv,
				char *prefix);
int simple_util_init_priv(struct simple_util_priv *priv,
			       struct link_info *li);
void simple_util_remove(struct platform_device *pdev);

int graph_util_card_probe(struct snd_soc_card *card);
int graph_util_is_ports0(struct device_node *port);
int graph_util_parse_dai(struct simple_util_priv *priv, struct device_node *ep,
			 struct snd_soc_dai_link_component *dlc, int *is_single_link);

void graph_util_parse_link_direction(struct device_node *np,
				    bool *is_playback_only, bool *is_capture_only);
void graph_util_parse_trigger_order(struct simple_util_priv *priv,
				    struct device_node *np,
				    enum snd_soc_trigger_order *trigger_start,
				    enum snd_soc_trigger_order *trigger_stop);

#ifdef DEBUG
static inline void simple_util_debug_dai(struct simple_util_priv *priv,
					 char *name,
					 struct simple_util_dai *dai)
{
	struct device *dev = simple_priv_to_dev(priv);

	/* dai might be NULL */
	if (!dai)
		return;

	if (dai->name)
		dev_dbg(dev, "%s dai name = %s\n",
			name, dai->name);

	if (dai->slots)
		dev_dbg(dev, "%s slots = %d\n", name, dai->slots);
	if (dai->slot_width)
		dev_dbg(dev, "%s slot width = %d\n", name, dai->slot_width);
	if (dai->tx_slot_mask)
		dev_dbg(dev, "%s tx slot mask = %d\n", name, dai->tx_slot_mask);
	if (dai->rx_slot_mask)
		dev_dbg(dev, "%s rx slot mask = %d\n", name, dai->rx_slot_mask);
	if (dai->clk)
		dev_dbg(dev, "%s clk %luHz\n", name, clk_get_rate(dai->clk));
	if (dai->sysclk)
		dev_dbg(dev, "%s sysclk = %dHz\n",
			name, dai->sysclk);
	if (dai->clk || dai->sysclk)
		dev_dbg(dev, "%s direction = %s\n",
			name, dai->clk_direction ? "OUT" : "IN");
}

static inline void simple_util_debug_info(struct simple_util_priv *priv)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	struct device *dev = simple_priv_to_dev(priv);

	int i;

	if (card->name)
		dev_dbg(dev, "Card Name: %s\n", card->name);

	for (i = 0; i < card->num_links; i++) {
		struct simple_dai_props *props = simple_priv_to_props(priv, i);
		struct snd_soc_dai_link *link = simple_priv_to_link(priv, i);
		struct simple_util_dai *dai;
		struct snd_soc_codec_conf *cnf;
		int j;

		dev_dbg(dev, "DAI%d\n", i);

		dev_dbg(dev, "cpu num = %d\n", link->num_cpus);
		for_each_prop_dai_cpu(props, j, dai)
			simple_util_debug_dai(priv, "cpu", dai);
		dev_dbg(dev, "codec num = %d\n", link->num_codecs);
		for_each_prop_dai_codec(props, j, dai)
			simple_util_debug_dai(priv, "codec", dai);

		if (link->name)
			dev_dbg(dev, "link name = %s\n", link->name);
		if (link->dai_fmt)
			dev_dbg(dev, "link format = %04x\n", link->dai_fmt);
		if (link->playback_only)
			dev_dbg(dev, "link has playback_only");
		if (link->capture_only)
			dev_dbg(dev, "link has capture_only");
		if (props->adata.convert_rate)
			dev_dbg(dev, "convert_rate = %d\n", props->adata.convert_rate);
		if (props->adata.convert_channels)
			dev_dbg(dev, "convert_channels = %d\n", props->adata.convert_channels);
		for_each_prop_codec_conf(props, j, cnf)
			if (cnf->name_prefix)
				dev_dbg(dev, "name prefix = %s\n", cnf->name_prefix);
		if (props->mclk_fs)
			dev_dbg(dev, "mclk-fs = %d\n", props->mclk_fs);
	}
}
#else
#define  simple_util_debug_info(priv)
#endif /* DEBUG */

#endif /* __SIMPLE_CARD_UTILS_H */
