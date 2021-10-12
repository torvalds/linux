/* SPDX-License-Identifier: GPL-2.0
 *
 * ASoC audio graph card support
 *
 */

#ifndef __GRAPH_CARD_H
#define __GRAPH_CARD_H

#include <sound/simple_card_utils.h>

typedef int (*GRAPH2_CUSTOM)(struct asoc_simple_priv *priv,
			     struct device_node *lnk,
			     struct link_info *li);

struct graph2_custom_hooks {
	int (*hook_pre)(struct asoc_simple_priv *priv);
	int (*hook_post)(struct asoc_simple_priv *priv);
	GRAPH2_CUSTOM custom_normal;
};

int audio_graph_parse_of(struct asoc_simple_priv *priv, struct device *dev);
int audio_graph2_parse_of(struct asoc_simple_priv *priv, struct device *dev,
			  struct graph2_custom_hooks *hooks);

int audio_graph2_link_normal(struct asoc_simple_priv *priv,
			     struct device_node *lnk, struct link_info *li);

#endif /* __GRAPH_CARD_H */
