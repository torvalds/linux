/* SPDX-License-Identifier: GPL-2.0
 *
 * ASoC audio graph card support
 *
 */

#ifndef __GRAPH_CARD_H
#define __GRAPH_CARD_H

#include <sound/simple_card_utils.h>

int audio_graph_card_probe(struct snd_soc_card *card);

int audio_graph_parse_of(struct asoc_simple_priv *priv, struct device *dev);

int audio_graph_remove(struct platform_device *pdev);

#endif /* __GRAPH_CARD_H */
