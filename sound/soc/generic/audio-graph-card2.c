// SPDX-License-Identifier: GPL-2.0
//
// ASoC Audio Graph Card2 support
//
// Copyright (C) 2020 Renesas Electronics Corp.
// Copyright (C) 2020 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// based on ${LINUX}/sound/soc/generic/audio-graph-card.c
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/graph_card.h>

/************************************
	daifmt
 ************************************
	ports {
		format = "left_j";
		port@0 {
			bitclock-master;
			sample0: endpoint@0 {
				frame-master;
			};
			sample1: endpoint@1 {
				format = "i2s";
			};
		};
		...
	};

 You can set daifmt at ports/port/endpoint.
 It uses *latest* format, and *share* master settings.
 In above case,
	sample0: left_j, bitclock-master, frame-master
	sample1: i2s,    bitclock-master

 If there was no settings, *Codec* will be
 bitclock/frame provider as default.
 see
	graph_parse_daifmt().

 "format" property is no longer needed on DT if both CPU/Codec drivers are
 supporting snd_soc_dai_ops :: .auto_selectable_formats.
 see
	snd_soc_runtime_get_dai_fmt()

	sample driver
		linux/sound/soc/sh/rcar/core.c
		linux/sound/soc/codecs/ak4613.c
		linux/sound/soc/codecs/pcm3168a.c
		linux/sound/soc/soc-utils.c
		linux/sound/soc/generic/test-component.c

 ************************************
	Normal Audio-Graph
 ************************************

 CPU <---> Codec

 sound {
	compatible = "audio-graph-card2";
	links = <&cpu>;
 };

 CPU {
	cpu: port {
		bitclock-master;
		frame-master;
		cpu_ep: endpoint { remote-endpoint = <&codec_ep>; }; };
 };

 Codec {
	port {	codec_ep: endpoint { remote-endpoint = <&cpu_ep>; }; };
 };

 ************************************
	Multi-CPU/Codec
 ************************************

It has link connection part (= X,x) and list part (= A,B,a,b).
"links" is connection part of CPU side (= @).

	+----+		+---+
 CPU1 --|A  X| <-@----> |x a|-- Codec1
 CPU2 --|B   |		|  b|-- Codec2
	+----+		+---+

 sound {
	compatible = "audio-graph-card2";

(@)	links = <&mcpu>;

	multi {
		ports@0 {
(@)		mcpu:	port@0 { mcpu0_ep: endpoint { remote-endpoint = <&mcodec0_ep>;	}; };	// (X) to pair
			port@1 { mcpu1_ep: endpoint { remote-endpoint = <&cpu1_ep>;	}; };	// (A) Multi Element
			port@2 { mcpu2_ep: endpoint { remote-endpoint = <&cpu2_ep>;	}; };	// (B) Multi Element
		};
		ports@1 {
			port@0 { mcodec0_ep: endpoint { remote-endpoint = <&mcpu0_ep>;	}; };	// (x) to pair
			port@1 { mcodec1_ep: endpoint { remote-endpoint = <&codec1_ep>;	}; };	// (a) Multi Element
			port@2 { mcodec2_ep: endpoint { remote-endpoint = <&codec2_ep>;	}; };	// (b) Multi Element
		};
	};
 };

 CPU {
	ports {
		bitclock-master;
		frame-master;
		port@0 { cpu1_ep: endpoint { remote-endpoint = <&mcpu1_ep>; }; };
		port@1 { cpu2_ep: endpoint { remote-endpoint = <&mcpu2_ep>; }; };
	};
 };

 Codec {
	ports {
		port@0 { codec1_ep: endpoint { remote-endpoint = <&mcodec1_ep>; }; };
		port@1 { codec2_ep: endpoint { remote-endpoint = <&mcodec2_ep>; }; };
	};
 };

 ************************************
	DPCM
 ************************************

		DSP
	   ************
 PCM0 <--> * fe0  be0 * <--> DAI0: Codec Headset
 PCM1 <--> * fe1  be1 * <--> DAI1: Codec Speakers
 PCM2 <--> * fe2  be2 * <--> DAI2: MODEM
 PCM3 <--> * fe3  be3 * <--> DAI3: BT
	   *	  be4 * <--> DAI4: DMIC
	   *	  be5 * <--> DAI5: FM
	   ************

 sound {
	compatible = "audio-graph-card2";

	// indicate routing
	routing = "xxx Playback", "xxx Playback",
		  "xxx Playback", "xxx Playback",
		  "xxx Playback", "xxx Playback";

	// indicate all Front-End, Back-End
	links = <&fe0, &fe1, ...,
		 &be0, &be1, ...>;

	dpcm {
		// Front-End
		ports@0 {
			fe0: port@0 { fe0_ep: endpoint { remote-endpoint = <&pcm0_ep>; }; };
			fe1: port@1 { fe1_ep: endpoint { remote-endpoint = <&pcm1_ep>; }; };
			...
		};
		// Back-End
		ports@1 {
			be0: port@0 { be0_ep: endpoint { remote-endpoint = <&dai0_ep>; }; };
			be1: port@1 { be1_ep: endpoint { remote-endpoint = <&dai1_ep>; }; };
			...
		};
	};
 };

 CPU {
	ports {
		bitclock-master;
		frame-master;
		port@0 { pcm0_ep: endpoint { remote-endpoint = <&fe0_ep>; }; };
		port@1 { pcm1_ep: endpoint { remote-endpoint = <&fe1_ep>; }; };
		...
	};
 };

 Codec {
	ports {
		port@0 { dai0_ep: endpoint { remote-endpoint = <&be0_ep>; }; };
		port@1 { dai1_ep: endpoint { remote-endpoint = <&be1_ep>; }; };
		...
	};
 };

 ************************************
	Codec to Codec
 ************************************

 +--+
 |  |<-- Codec0 <- IN
 |  |--> Codec1 -> OUT
 +--+

 sound {
	compatible = "audio-graph-card2";

	routing = "OUT" ,"DAI1 Playback",
		  "DAI0 Capture", "IN";

	links = <&c2c>;

	codec2codec {
		ports {
			rate = <48000>;
		c2c:	port@0 { c2cf_ep: endpoint { remote-endpoint = <&codec0_ep>; }; };
			port@1 { c2cb_ep: endpoint { remote-endpoint = <&codec1_ep>; }; };
	};
 };

 Codec {
	ports {
		port@0 {
			bitclock-master;
			frame-master;
			 codec0_ep: endpoint { remote-endpoint = <&c2cf_ep>; }; };
		port@1 { codec1_ep: endpoint { remote-endpoint = <&c2cb_ep>; }; };
	};
 };

*/

enum graph_type {
	GRAPH_NORMAL,
	GRAPH_DPCM,
	GRAPH_C2C,

	GRAPH_MULTI,	/* don't use ! Use this only in __graph_get_type() */
};

#define GRAPH_NODENAME_MULTI	"multi"
#define GRAPH_NODENAME_DPCM	"dpcm"
#define GRAPH_NODENAME_C2C	"codec2codec"

#define port_to_endpoint(port) of_get_child_by_name(port, "endpoint")

static enum graph_type __graph_get_type(struct device_node *lnk)
{
	struct device_node *np, *parent_np;
	enum graph_type ret;

	/*
	 * target {
	 *	ports {
	 * =>		lnk:	port@0 { ... };
	 *			port@1 { ... };
	 *	};
	 * };
	 */
	np = of_get_parent(lnk);
	if (of_node_name_eq(np, "ports")) {
		parent_np = of_get_parent(np);
		of_node_put(np);
		np = parent_np;
	}

	if (of_node_name_eq(np, GRAPH_NODENAME_MULTI)) {
		ret = GRAPH_MULTI;
		goto out_put;
	}

	if (of_node_name_eq(np, GRAPH_NODENAME_DPCM)) {
		ret = GRAPH_DPCM;
		goto out_put;
	}

	if (of_node_name_eq(np, GRAPH_NODENAME_C2C)) {
		ret = GRAPH_C2C;
		goto out_put;
	}

	ret = GRAPH_NORMAL;

out_put:
	of_node_put(np);
	return ret;

}

static enum graph_type graph_get_type(struct simple_util_priv *priv,
				      struct device_node *lnk)
{
	enum graph_type type = __graph_get_type(lnk);

	/* GRAPH_MULTI here means GRAPH_NORMAL */
	if (type == GRAPH_MULTI)
		type = GRAPH_NORMAL;

#ifdef DEBUG
	{
		struct device *dev = simple_priv_to_dev(priv);
		const char *str = "Normal";

		switch (type) {
		case GRAPH_DPCM:
			if (graph_util_is_ports0(lnk))
				str = "DPCM Front-End";
			else
				str = "DPCM Back-End";
			break;
		case GRAPH_C2C:
			str = "Codec2Codec";
			break;
		default:
			break;
		}

		dev_dbg(dev, "%pOF (%s)", lnk, str);
	}
#endif
	return type;
}

static int graph_lnk_is_multi(struct device_node *lnk)
{
	return __graph_get_type(lnk) == GRAPH_MULTI;
}

static struct device_node *graph_get_next_multi_ep(struct device_node **port)
{
	struct device_node *ports = of_get_parent(*port);
	struct device_node *ep = NULL;
	struct device_node *rep = NULL;

	/*
	 * multi {
	 *	ports {
	 * =>	lnk:	port@0 { ...		   }; // to pair
	 *		port@1 { ep { ... = rep0 } }; // Multi Element
	 *		port@2 { ep { ... = rep1 } }; // Multi Element
	 *		...
	 *	};
	 * };
	 *
	 * xxx {
	 *	port@0 { rep0 };
	 *	port@1 { rep1 };
	 * };
	 */
	do {
		*port = of_get_next_child(ports, *port);
		if (!*port)
			break;
	} while (!of_node_name_eq(*port, "port"));

	if (*port) {
		ep  = port_to_endpoint(*port);
		rep = of_graph_get_remote_endpoint(ep);
	}

	of_node_put(ep);
	of_node_put(ports);

	return rep;
}

static const struct snd_soc_ops graph_ops = {
	.startup	= simple_util_startup,
	.shutdown	= simple_util_shutdown,
	.hw_params	= simple_util_hw_params,
};

static void graph_parse_convert(struct device_node *ep,
				struct simple_dai_props *props)
{
	struct device_node *port = of_get_parent(ep);
	struct device_node *ports = of_get_parent(port);
	struct simple_util_data *adata = &props->adata;

	if (of_node_name_eq(ports, "ports"))
		simple_util_parse_convert(ports, NULL, adata);
	simple_util_parse_convert(port, NULL, adata);
	simple_util_parse_convert(ep,   NULL, adata);

	of_node_put(port);
	of_node_put(ports);
}

static void graph_parse_mclk_fs(struct device_node *ep,
				struct simple_dai_props *props)
{
	struct device_node *port	= of_get_parent(ep);
	struct device_node *ports	= of_get_parent(port);

	if (of_node_name_eq(ports, "ports"))
		of_property_read_u32(ports, "mclk-fs", &props->mclk_fs);
	of_property_read_u32(port,	"mclk-fs", &props->mclk_fs);
	of_property_read_u32(ep,	"mclk-fs", &props->mclk_fs);

	of_node_put(port);
	of_node_put(ports);
}

static int __graph_parse_node(struct simple_util_priv *priv,
			      enum graph_type gtype,
			      struct device_node *ep,
			      struct link_info *li,
			      int is_cpu, int idx)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct snd_soc_dai_link_component *dlc;
	struct simple_util_dai *dai;
	int ret, is_single_links = 0;

	if (is_cpu) {
		dlc = snd_soc_link_to_cpu(dai_link, idx);
		dai = simple_props_to_dai_cpu(dai_props, idx);
	} else {
		dlc = snd_soc_link_to_codec(dai_link, idx);
		dai = simple_props_to_dai_codec(dai_props, idx);
	}

	graph_parse_mclk_fs(ep, dai_props);

	ret = graph_util_parse_dai(dev, ep, dlc, &is_single_links);
	if (ret < 0)
		return ret;

	ret = simple_util_parse_tdm(ep, dai);
	if (ret < 0)
		return ret;

	ret = simple_util_parse_tdm_width_map(dev, ep, dai);
	if (ret < 0)
		return ret;

	ret = simple_util_parse_clk(dev, ep, dai, dlc);
	if (ret < 0)
		return ret;

	/*
	 * set DAI Name
	 */
	if (!dai_link->name) {
		struct snd_soc_dai_link_component *cpus = dlc;
		struct snd_soc_dai_link_component *codecs = snd_soc_link_to_codec(dai_link, idx);
		char *cpu_multi   = "";
		char *codec_multi = "";

		if (dai_link->num_cpus > 1)
			cpu_multi = "_multi";
		if (dai_link->num_codecs > 1)
			codec_multi = "_multi";

		switch (gtype) {
		case GRAPH_NORMAL:
			/* run is_cpu only. see audio_graph2_link_normal() */
			if (is_cpu)
				simple_util_set_dailink_name(dev, dai_link, "%s%s-%s%s",
							       cpus->dai_name,   cpu_multi,
							     codecs->dai_name, codec_multi);
			break;
		case GRAPH_DPCM:
			if (is_cpu)
				simple_util_set_dailink_name(dev, dai_link, "fe.%pOFP.%s%s",
						cpus->of_node, cpus->dai_name, cpu_multi);
			else
				simple_util_set_dailink_name(dev, dai_link, "be.%pOFP.%s%s",
						codecs->of_node, codecs->dai_name, codec_multi);
			break;
		case GRAPH_C2C:
			/* run is_cpu only. see audio_graph2_link_c2c() */
			if (is_cpu)
				simple_util_set_dailink_name(dev, dai_link, "c2c.%s%s-%s%s",
							     cpus->dai_name,   cpu_multi,
							     codecs->dai_name, codec_multi);
			break;
		default:
			break;
		}
	}

	/*
	 * Check "prefix" from top node
	 * if DPCM-BE case
	 */
	if (!is_cpu && gtype == GRAPH_DPCM) {
		struct snd_soc_dai_link_component *codecs = snd_soc_link_to_codec(dai_link, idx);
		struct snd_soc_codec_conf *cconf = simple_props_to_codec_conf(dai_props, idx);
		struct device_node *rport  = of_get_parent(ep);
		struct device_node *rports = of_get_parent(rport);

		if (of_node_name_eq(rports, "ports"))
			snd_soc_of_parse_node_prefix(rports, cconf, codecs->of_node, "prefix");
		snd_soc_of_parse_node_prefix(rport,  cconf, codecs->of_node, "prefix");

		of_node_put(rport);
		of_node_put(rports);
	}

	if (is_cpu) {
		struct snd_soc_dai_link_component *cpus = dlc;
		struct snd_soc_dai_link_component *platforms = snd_soc_link_to_platform(dai_link, idx);

		simple_util_canonicalize_cpu(cpus, is_single_links);
		simple_util_canonicalize_platform(platforms, cpus);
	}

	return 0;
}

static int graph_parse_node_multi_nm(struct snd_soc_dai_link *dai_link,
				     int *nm_idx, int cpu_idx,
				     struct device_node *mcpu_port)
{
	/*
	 *		+---+		+---+
	 *		|  X|<-@------->|x  |
	 *		|   |		|   |
	 *	cpu0 <--|A 1|<--------->|4 a|-> codec0
	 *	cpu1 <--|B 2|<-----+--->|5 b|-> codec1
	 *	cpu2 <--|C 3|<----/	+---+
	 *		+---+
	 *
	 * multi {
	 *	ports {
	 *		port@0 { mcpu_top_ep	{...  = mcodec_ep;	}; };	// (X) to pair
	 * <mcpu_port>	port@1 { mcpu0_ep	{ ... = cpu0_ep;	};	// (A) Multi Element
	 *			 mcpu0_ep_0	{ ... = mcodec0_ep_0;	}; };	// (1) connected Codec
	 *		port@2 { mcpu1_ep	{ ... = cpu1_ep;	};	// (B) Multi Element
	 *			 mcpu1_ep_0	{ ... = mcodec1_ep_0;	}; };	// (2) connected Codec
	 *		port@3 { mcpu2_ep	{ ... = cpu2_ep;	};	// (C) Multi Element
	 *			 mcpu2_ep_0	{ ... = mcodec1_ep_1;	}; };	// (3) connected Codec
	 *	};
	 *
	 *	ports {
	 *		port@0 { mcodec_top_ep	{...  = mcpu_ep;	}; };	// (x) to pair
	 * <mcodec_port>port@1 { mcodec0_ep	{ ... = codec0_ep;	};	// (a) Multi Element
	 *			 mcodec0_ep_0	{ ... = mcpu0_ep_0;	}; };	// (4) connected CPU
	 *		port@2 { mcodec1_ep	{ ... = codec1_ep;	};	// (b) Multi Element
	 *			 mcodec1_ep_0	{ ... = mcpu1_ep_0;	};	// (5) connected CPU
	 *			 mcodec1_ep_1	{ ... = mcpu2_ep_0;	}; };	// (5) connected CPU
	 *	};
	 * };
	 */
	struct device_node *mcpu_ep		= port_to_endpoint(mcpu_port);
	struct device_node *mcpu_ep_n		= mcpu_ep;
	struct device_node *mcpu_port_top	= of_get_next_child(of_get_parent(mcpu_port), NULL);
	struct device_node *mcpu_ep_top		= port_to_endpoint(mcpu_port_top);
	struct device_node *mcodec_ep_top	= of_graph_get_remote_endpoint(mcpu_ep_top);
	struct device_node *mcodec_port_top	= of_get_parent(mcodec_ep_top);
	struct device_node *mcodec_ports	= of_get_parent(mcodec_port_top);
	int nm_max = max(dai_link->num_cpus, dai_link->num_codecs);
	int ret = -EINVAL;

	if (cpu_idx > dai_link->num_cpus)
		goto mcpu_err;

	while (1) {
		struct device_node *mcodec_ep_n;
		struct device_node *mcodec_port_i;
		struct device_node *mcodec_port;
		int codec_idx;

		if (*nm_idx > nm_max)
			break;

		mcpu_ep_n = of_get_next_child(mcpu_port, mcpu_ep_n);
		if (!mcpu_ep_n) {
			ret = 0;
			break;
		}

		mcodec_ep_n	= of_graph_get_remote_endpoint(mcpu_ep_n);
		mcodec_port	= of_get_parent(mcodec_ep_n);

		if (mcodec_ports != of_get_parent(mcodec_port))
			goto mcpu_err;

		codec_idx = 0;
		mcodec_port_i = of_get_next_child(mcodec_ports, NULL);
		while (1) {
			if (codec_idx > dai_link->num_codecs)
				goto mcodec_err;

			mcodec_port_i = of_get_next_child(mcodec_ports, mcodec_port_i);

			if (!mcodec_port_i)
				goto mcodec_err;

			if (mcodec_port_i == mcodec_port)
				break;

			codec_idx++;
		}

		dai_link->ch_maps[*nm_idx].cpu		= cpu_idx;
		dai_link->ch_maps[*nm_idx].codec	= codec_idx;

		(*nm_idx)++;

		of_node_put(mcodec_port_i);
mcodec_err:
		of_node_put(mcodec_port);
		of_node_put(mcpu_ep_n);
		of_node_put(mcodec_ep_n);
	}
mcpu_err:
	of_node_put(mcpu_ep);
	of_node_put(mcpu_port_top);
	of_node_put(mcpu_ep_top);
	of_node_put(mcodec_ep_top);
	of_node_put(mcodec_port_top);
	of_node_put(mcodec_ports);

	return ret;
}

static int graph_parse_node_multi(struct simple_util_priv *priv,
				  enum graph_type gtype,
				  struct device_node *port,
				  struct link_info *li, int is_cpu)
{
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *ep;
	int ret = -ENOMEM;
	int nm_idx = 0;
	int nm_max = max(dai_link->num_cpus, dai_link->num_codecs);

	/*
	 * create ch_maps if CPU:Codec = N:M
	 * DPCM is out of scope
	 */
	if (gtype != GRAPH_DPCM && !dai_link->ch_maps &&
	    dai_link->num_cpus > 1 && dai_link->num_codecs > 1 &&
	    dai_link->num_cpus != dai_link->num_codecs) {

		dai_link->ch_maps = devm_kcalloc(dev, nm_max,
					sizeof(struct snd_soc_dai_link_ch_map), GFP_KERNEL);
		if (!dai_link->ch_maps)
			goto multi_err;
	}

	for (int idx = 0;; idx++) {
		/*
		 * multi {
		 *	ports {
		 * <port>	port@0 { ... 			    }; // to pair
		 *		port@1 { mcpu1_ep { ... = cpu1_ep };}; // Multi Element
		 *		port@2 { mcpu2_ep { ... = cpu2_ep };}; // Multi Element
		 *	};
		 * };
		 *
		 * cpu {
		 *	ports {
		 * <ep>		port@0 { cpu1_ep   { ... = mcpu1_ep };};
		 *	};
		 * };
		 */
		ep = graph_get_next_multi_ep(&port);
		if (!ep)
			break;

		ret = __graph_parse_node(priv, gtype, ep, li, is_cpu, idx);
		of_node_put(ep);
		if (ret < 0)
			goto multi_err;

		/* CPU:Codec = N:M */
		if (is_cpu && dai_link->ch_maps) {
			ret = graph_parse_node_multi_nm(dai_link, &nm_idx, idx, port);
			if (ret < 0)
				goto multi_err;
		}
	}

	if (is_cpu && dai_link->ch_maps && (nm_idx != nm_max))
		ret = -EINVAL;

multi_err:
	return ret;
}

static int graph_parse_node_single(struct simple_util_priv *priv,
				   enum graph_type gtype,
				   struct device_node *port,
				   struct link_info *li, int is_cpu)
{
	struct device_node *ep = port_to_endpoint(port);
	int ret = __graph_parse_node(priv, gtype, ep, li, is_cpu, 0);

	of_node_put(ep);

	return ret;
}

static int graph_parse_node(struct simple_util_priv *priv,
			    enum graph_type gtype,
			    struct device_node *port,
			    struct link_info *li, int is_cpu)
{
	if (graph_lnk_is_multi(port))
		return graph_parse_node_multi(priv, gtype, port, li, is_cpu);
	else
		return graph_parse_node_single(priv, gtype, port, li, is_cpu);
}

static void graph_parse_daifmt(struct device_node *node,
			       unsigned int *daifmt, unsigned int *bit_frame)
{
	unsigned int fmt;

	/*
	 * see also above "daifmt" explanation
	 * and samples.
	 */

	/*
	 *	ports {
	 * (A)
	 *		port {
	 * (B)
	 *			endpoint {
	 * (C)
	 *			};
	 *		};
	 *	};
	 * };
	 */

	/*
	 * clock_provider:
	 *
	 * It can be judged it is provider
	 * if (A) or (B) or (C) has bitclock-master / frame-master flag.
	 *
	 * use "or"
	 */
	*bit_frame |= snd_soc_daifmt_parse_clock_provider_as_bitmap(node, NULL);

#define update_daifmt(name)					\
	if (!(*daifmt & SND_SOC_DAIFMT_##name##_MASK) &&	\
		 (fmt & SND_SOC_DAIFMT_##name##_MASK))		\
		*daifmt |= fmt & SND_SOC_DAIFMT_##name##_MASK

	/*
	 * format
	 *
	 * This function is called by (C) -> (B) -> (A) order.
	 * Set if applicable part was not yet set.
	 */
	fmt = snd_soc_daifmt_parse_format(node, NULL);
	update_daifmt(FORMAT);
	update_daifmt(CLOCK);
	update_daifmt(INV);
}

static void graph_link_init(struct simple_util_priv *priv,
			    struct device_node *port,
			    struct link_info *li,
			    int is_cpu_node)
{
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct device_node *ep;
	struct device_node *ports;
	unsigned int daifmt = 0, daiclk = 0;
	bool playback_only = 0, capture_only = 0;
	unsigned int bit_frame = 0;

	if (graph_lnk_is_multi(port)) {
		of_node_get(port);
		ep = graph_get_next_multi_ep(&port);
		port = of_get_parent(ep);
	} else {
		ep = port_to_endpoint(port);
	}

	ports = of_get_parent(port);

	/*
	 *	ports {
	 * (A)
	 *		port {
	 * (B)
	 *			endpoint {
	 * (C)
	 *			};
	 *		};
	 *	};
	 * };
	 */
	graph_parse_daifmt(ep,    &daifmt, &bit_frame);		/* (C) */
	graph_parse_daifmt(port,  &daifmt, &bit_frame);		/* (B) */
	if (of_node_name_eq(ports, "ports"))
		graph_parse_daifmt(ports, &daifmt, &bit_frame);	/* (A) */

	/*
	 * convert bit_frame
	 * We need to flip clock_provider if it was CPU node,
	 * because it is Codec base.
	 */
	daiclk = snd_soc_daifmt_clock_provider_from_bitmap(bit_frame);
	if (is_cpu_node)
		daiclk = snd_soc_daifmt_clock_provider_flipped(daiclk);

	graph_util_parse_link_direction(port, &playback_only, &capture_only);

	dai_link->playback_only = playback_only;
	dai_link->capture_only = capture_only;

	dai_link->dai_fmt	= daifmt | daiclk;
	dai_link->init		= simple_util_dai_init;
	dai_link->ops		= &graph_ops;
	if (priv->ops)
		dai_link->ops	= priv->ops;
}

int audio_graph2_link_normal(struct simple_util_priv *priv,
			     struct device_node *lnk,
			     struct link_info *li)
{
	struct device_node *cpu_port = lnk;
	struct device_node *cpu_ep = port_to_endpoint(cpu_port);
	struct device_node *codec_port = of_graph_get_remote_port(cpu_ep);
	int ret;

	/*
	 * call Codec first.
	 * see
	 *	__graph_parse_node() :: DAI Naming
	 */
	ret = graph_parse_node(priv, GRAPH_NORMAL, codec_port, li, 0);
	if (ret < 0)
		goto err;

	/*
	 * call CPU, and set DAI Name
	 */
	ret = graph_parse_node(priv, GRAPH_NORMAL, cpu_port, li, 1);
	if (ret < 0)
		goto err;

	graph_link_init(priv, cpu_port, li, 1);
err:
	of_node_put(codec_port);
	of_node_put(cpu_ep);

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_link_normal);

int audio_graph2_link_dpcm(struct simple_util_priv *priv,
			   struct device_node *lnk,
			   struct link_info *li)
{
	struct device_node *ep = port_to_endpoint(lnk);
	struct device_node *rep = of_graph_get_remote_endpoint(ep);
	struct device_node *rport = of_graph_get_remote_port(ep);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	int is_cpu = graph_util_is_ports0(lnk);
	int ret;

	if (is_cpu) {
		/*
		 * dpcm {
		 *	// Front-End
		 *	ports@0 {
		 * =>		lnk: port@0 { ep: { ... = rep }; };
		 *		 ...
		 *	};
		 *	// Back-End
		 *	ports@0 {
		 *		 ...
		 *	};
		 * };
		 *
		 * CPU {
		 *	rports: ports {
		 *		rport: port@0 { rep: { ... = ep } };
		 *	}
		 * }
		 */
		/*
		 * setup CPU here, Codec is already set as dummy.
		 * see
		 *	simple_util_init_priv()
		 */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		ret = graph_parse_node(priv, GRAPH_DPCM, rport, li, 1);
		if (ret)
			goto err;
	} else {
		/*
		 * dpcm {
		 *	// Front-End
		 *	ports@0 {
		 *		 ...
		 *	};
		 *	// Back-End
		 *	ports@0 {
		 * =>		lnk: port@0 { ep: { ... = rep; }; };
		 *		 ...
		 *	};
		 * };
		 *
		 * Codec {
		 *	rports: ports {
		 *		rport: port@0 { rep: { ... = ep; }; };
		 *	}
		 * }
		 */
		/*
		 * setup Codec here, CPU is already set as dummy.
		 * see
		 *	simple_util_init_priv()
		 */

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= simple_util_be_hw_params_fixup;

		ret = graph_parse_node(priv, GRAPH_DPCM, rport, li, 0);
		if (ret < 0)
			goto err;
	}

	graph_parse_convert(ep,  dai_props); /* at node of <dpcm> */
	graph_parse_convert(rep, dai_props); /* at node of <CPU/Codec> */

	snd_soc_dai_link_set_capabilities(dai_link);

	graph_link_init(priv, rport, li, is_cpu);
err:
	of_node_put(ep);
	of_node_put(rep);
	of_node_put(rport);

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_link_dpcm);

int audio_graph2_link_c2c(struct simple_util_priv *priv,
			  struct device_node *lnk,
			  struct link_info *li)
{
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct device_node *port0, *port1, *ports;
	struct device_node *codec0_port, *codec1_port;
	struct device_node *ep0, *ep1;
	u32 val = 0;
	int ret = -EINVAL;

	/*
	 * codec2codec {
	 *	ports {
	 *		rate = <48000>;
	 * =>	lnk:	port@0 { c2c0_ep: { ... = codec0_ep; }; };
	 *		port@1 { c2c1_ep: { ... = codec1_ep; }; };
	 *	};
	 * };
	 *
	 * Codec {
	 *	ports {
	 *		port@0 { codec0_ep: ... }; };
	 *		port@1 { codec1_ep: ... }; };
	 *	};
	 * };
	 */
	of_node_get(lnk);
	port0 = lnk;
	ports = of_get_parent(port0);
	port1 = of_get_next_child(ports, lnk);

	/*
	 * Card2 can use original Codec2Codec settings if DT has.
	 * It will use default settings if no settings on DT.
	 * see
	 *	simple_util_init_for_codec2codec()
	 *
	 * Add more settings here if needed
	 */
	of_property_read_u32(ports, "rate", &val);
	if (val) {
		struct device *dev = simple_priv_to_dev(priv);
		struct snd_soc_pcm_stream *c2c_conf;

		c2c_conf = devm_kzalloc(dev, sizeof(*c2c_conf), GFP_KERNEL);
		if (!c2c_conf)
			goto err1;

		c2c_conf->formats	= SNDRV_PCM_FMTBIT_S32_LE; /* update ME */
		c2c_conf->rates		= SNDRV_PCM_RATE_8000_384000;
		c2c_conf->rate_min	=
		c2c_conf->rate_max	= val;
		c2c_conf->channels_min	=
		c2c_conf->channels_max	= 2; /* update ME */

		dai_link->c2c_params		= c2c_conf;
		dai_link->num_c2c_params	= 1;
	}

	ep0 = port_to_endpoint(port0);
	ep1 = port_to_endpoint(port1);

	codec0_port = of_graph_get_remote_port(ep0);
	codec1_port = of_graph_get_remote_port(ep1);

	/*
	 * call Codec first.
	 * see
	 *	__graph_parse_node() :: DAI Naming
	 */
	ret = graph_parse_node(priv, GRAPH_C2C, codec1_port, li, 0);
	if (ret < 0)
		goto err2;

	/*
	 * call CPU, and set DAI Name
	 */
	ret = graph_parse_node(priv, GRAPH_C2C, codec0_port, li, 1);
	if (ret < 0)
		goto err2;

	graph_link_init(priv, codec0_port, li, 1);
err2:
	of_node_put(ep0);
	of_node_put(ep1);
	of_node_put(codec0_port);
	of_node_put(codec1_port);
err1:
	of_node_put(ports);
	of_node_put(port0);
	of_node_put(port1);

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_link_c2c);

static int graph_link(struct simple_util_priv *priv,
		      struct graph2_custom_hooks *hooks,
		      enum graph_type gtype,
		      struct device_node *lnk,
		      struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	GRAPH2_CUSTOM func = NULL;
	int ret = -EINVAL;

	switch (gtype) {
	case GRAPH_NORMAL:
		if (hooks && hooks->custom_normal)
			func = hooks->custom_normal;
		else
			func = audio_graph2_link_normal;
		break;
	case GRAPH_DPCM:
		if (hooks && hooks->custom_dpcm)
			func = hooks->custom_dpcm;
		else
			func = audio_graph2_link_dpcm;
		break;
	case GRAPH_C2C:
		if (hooks && hooks->custom_c2c)
			func = hooks->custom_c2c;
		else
			func = audio_graph2_link_c2c;
		break;
	default:
		break;
	}

	if (!func) {
		dev_err(dev, "non supported gtype (%d)\n", gtype);
		goto err;
	}

	ret = func(priv, lnk, li);
	if (ret < 0)
		goto err;

	li->link++;
err:
	return ret;
}

static int graph_counter(struct device_node *lnk)
{
	/*
	 * Multi CPU / Codec
	 *
	 * multi {
	 *	ports {
	 * =>		lnk:	port@0 { ... }; // to pair
	 *			port@1 { ... }; // Multi Element
	 *			port@2 { ... }; // Multi Element
	 *			...
	 *	};
	 * };
	 *
	 * ignore first lnk part
	 */
	if (graph_lnk_is_multi(lnk)) {
		struct device_node *ports = of_get_parent(lnk);
		struct device_node *port = NULL;
		int cnt = 0;

		/*
		 * CPU/Codec = N:M case has many endpoints.
		 * We can't use of_graph_get_endpoint_count() here
		 */
		while(1) {
			port = of_get_next_child(ports, port);
			if (!port)
				break;
			cnt++;
		}

		return cnt - 1;
	}
	/*
	 * Single CPU / Codec
	 */
	else
		return 1;
}

static int graph_count_normal(struct simple_util_priv *priv,
			      struct device_node *lnk,
			      struct link_info *li)
{
	struct device_node *cpu_port = lnk;
	struct device_node *cpu_ep = port_to_endpoint(cpu_port);
	struct device_node *codec_port = of_graph_get_remote_port(cpu_ep);

	/*
	 *	CPU {
	 * =>		lnk: port { endpoint { .. }; };
	 *	};
	 */
	/*
	 * DON'T REMOVE platforms
	 * see
	 *	simple-card.c :: simple_count_noml()
	 */
	li->num[li->link].cpus		=
	li->num[li->link].platforms	= graph_counter(cpu_port);

	li->num[li->link].codecs	= graph_counter(codec_port);

	of_node_put(cpu_ep);
	of_node_put(codec_port);

	return 0;
}

static int graph_count_dpcm(struct simple_util_priv *priv,
			    struct device_node *lnk,
			    struct link_info *li)
{
	struct device_node *ep = port_to_endpoint(lnk);
	struct device_node *rport = of_graph_get_remote_port(ep);

	/*
	 * dpcm {
	 *	// Front-End
	 *	ports@0 {
	 * =>		lnk: port@0 { endpoint { ... }; };
	 *		 ...
	 *	};
	 *	// Back-End
	 *	ports@1 {
	 * =>		lnk: port@0 { endpoint { ... }; };
	 *		 ...
	 *	};
	 * };
	 */

	if (graph_util_is_ports0(lnk)) {
		/*
		 * DON'T REMOVE platforms
		 * see
		 *	simple-card.c :: simple_count_noml()
		 */
		li->num[li->link].cpus		= graph_counter(rport); /* FE */
		li->num[li->link].platforms	= graph_counter(rport);
	} else {
		li->num[li->link].codecs	= graph_counter(rport); /* BE */
	}

	of_node_put(ep);
	of_node_put(rport);

	return 0;
}

static int graph_count_c2c(struct simple_util_priv *priv,
			   struct device_node *lnk,
			   struct link_info *li)
{
	struct device_node *ports = of_get_parent(lnk);
	struct device_node *port0 = lnk;
	struct device_node *port1 = of_get_next_child(ports, of_node_get(lnk));
	struct device_node *ep0 = port_to_endpoint(port0);
	struct device_node *ep1 = port_to_endpoint(port1);
	struct device_node *codec0 = of_graph_get_remote_port(ep0);
	struct device_node *codec1 = of_graph_get_remote_port(ep1);

	/*
	 * codec2codec {
	 *	ports {
	 * =>	lnk:	port@0 { endpoint { ... }; };
	 *		port@1 { endpoint { ... }; };
	 *	};
	 * };
	 */
	/*
	 * DON'T REMOVE platforms
	 * see
	 *	simple-card.c :: simple_count_noml()
	 */
	li->num[li->link].cpus		=
	li->num[li->link].platforms	= graph_counter(codec0);

	li->num[li->link].codecs	= graph_counter(codec1);

	of_node_put(ports);
	of_node_put(port1);
	of_node_put(ep0);
	of_node_put(ep1);
	of_node_put(codec0);
	of_node_put(codec1);

	return 0;
}

static int graph_count(struct simple_util_priv *priv,
		       struct graph2_custom_hooks *hooks,
		       enum graph_type gtype,
		       struct device_node *lnk,
		       struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	GRAPH2_CUSTOM func = NULL;
	int ret = -EINVAL;

	if (li->link >= SNDRV_MAX_LINKS) {
		dev_err(dev, "too many links\n");
		return ret;
	}

	switch (gtype) {
	case GRAPH_NORMAL:
		func = graph_count_normal;
		break;
	case GRAPH_DPCM:
		func = graph_count_dpcm;
		break;
	case GRAPH_C2C:
		func = graph_count_c2c;
		break;
	default:
		break;
	}

	if (!func) {
		dev_err(dev, "non supported gtype (%d)\n", gtype);
		goto err;
	}

	ret = func(priv, lnk, li);
	if (ret < 0)
		goto err;

	li->link++;
err:
	return ret;
}

static int graph_for_each_link(struct simple_util_priv *priv,
			       struct graph2_custom_hooks *hooks,
			       struct link_info *li,
			       int (*func)(struct simple_util_priv *priv,
					   struct graph2_custom_hooks *hooks,
					   enum graph_type gtype,
					   struct device_node *lnk,
					   struct link_info *li))
{
	struct of_phandle_iterator it;
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *node = dev->of_node;
	struct device_node *lnk;
	enum graph_type gtype;
	int rc, ret;

	/* loop for all listed CPU port */
	of_for_each_phandle(&it, rc, node, "links", NULL, 0) {
		lnk = it.node;

		gtype = graph_get_type(priv, lnk);

		ret = func(priv, hooks, gtype, lnk, li);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int audio_graph2_parse_of(struct simple_util_priv *priv, struct device *dev,
			  struct graph2_custom_hooks *hooks)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	struct link_info *li;
	int ret;

	li = devm_kzalloc(dev, sizeof(*li), GFP_KERNEL);
	if (!li)
		return -ENOMEM;

	card->probe	= graph_util_card_probe;
	card->owner	= THIS_MODULE;
	card->dev	= dev;

	if ((hooks) && (hooks)->hook_pre) {
		ret = (hooks)->hook_pre(priv);
		if (ret < 0)
			goto err;
	}

	ret = graph_for_each_link(priv, hooks, li, graph_count);
	if (!li->link)
		ret = -EINVAL;
	if (ret < 0)
		goto err;

	ret = simple_util_init_priv(priv, li);
	if (ret < 0)
		goto err;

	priv->pa_gpio = devm_gpiod_get_optional(dev, "pa", GPIOD_OUT_LOW);
	if (IS_ERR(priv->pa_gpio)) {
		ret = PTR_ERR(priv->pa_gpio);
		dev_err(dev, "failed to get amplifier gpio: %d\n", ret);
		goto err;
	}

	ret = simple_util_parse_widgets(card, NULL);
	if (ret < 0)
		goto err;

	ret = simple_util_parse_routing(card, NULL);
	if (ret < 0)
		goto err;

	memset(li, 0, sizeof(*li));
	ret = graph_for_each_link(priv, hooks, li, graph_link);
	if (ret < 0)
		goto err;

	ret = simple_util_parse_card_name(card, NULL);
	if (ret < 0)
		goto err;

	snd_soc_card_set_drvdata(card, priv);

	if ((hooks) && (hooks)->hook_post) {
		ret = (hooks)->hook_post(priv);
		if (ret < 0)
			goto err;
	}

	simple_util_debug_info(priv);

	ret = devm_snd_soc_register_card(dev, card);
err:
	devm_kfree(dev, li);

	if (ret < 0)
		dev_err_probe(dev, ret, "parse error\n");

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_parse_of);

static int graph_probe(struct platform_device *pdev)
{
	struct simple_util_priv *priv;
	struct device *dev = &pdev->dev;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	return audio_graph2_parse_of(priv, dev, NULL);
}

static const struct of_device_id graph_of_match[] = {
	{ .compatible = "audio-graph-card2", },
	{},
};
MODULE_DEVICE_TABLE(of, graph_of_match);

static struct platform_driver graph_card = {
	.driver = {
		.name = "asoc-audio-graph-card2",
		.pm = &snd_soc_pm_ops,
		.of_match_table = graph_of_match,
	},
	.probe	= graph_probe,
	.remove_new = simple_util_remove,
};
module_platform_driver(graph_card);

MODULE_ALIAS("platform:asoc-audio-graph-card2");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph Card2");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
