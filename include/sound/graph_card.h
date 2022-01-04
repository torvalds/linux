/* SPDX-License-Identifier: GPL-2.0
 *
 * ASoC audio graph card support
 *
 */

#ifndef __GRAPH_CARD_H
#define __GRAPH_CARD_H

#include <sound/simple_card_utils.h>

int audio_graph_parse_of(struct asoc_simple_priv *priv, struct device *dev);

#endif /* __GRAPH_CARD_H */
