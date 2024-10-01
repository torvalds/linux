/* SPDX-License-Identifier: GPL-2.0
 *
 * ASoC audio graph card support
 *
 */

#ifndef __GRAPH_CARD_H
#define __GRAPH_CARD_H

#include <sound/simple_card_utils.h>

typedef int (*GRAPH2_CUSTOM)(struct simple_util_priv *priv,
			     struct device_node *lnk,
			     struct link_info *li);

struct graph2_custom_hooks {
	int (*hook_pre)(struct simple_util_priv *priv);
	int (*hook_post)(struct simple_util_priv *priv);
	GRAPH2_CUSTOM custom_normal;
	GRAPH2_CUSTOM custom_dpcm;
	GRAPH2_CUSTOM custom_c2c;
};

int audio_graph_parse_of(struct simple_util_priv *priv, struct device *dev);
int audio_graph2_parse_of(struct simple_util_priv *priv, struct device *dev,
			  struct graph2_custom_hooks *hooks);

int audio_graph2_link_normal(struct simple_util_priv *priv,
			     struct device_node *lnk, struct link_info *li);
int audio_graph2_link_dpcm(struct simple_util_priv *priv,
			   struct device_node *lnk, struct link_info *li);
int audio_graph2_link_c2c(struct simple_util_priv *priv,
			  struct device_node *lnk, struct link_info *li);

#endif /* __GRAPH_CARD_H */
