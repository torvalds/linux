/* SPDX-License-Identifier: GPL-2.0
 *
 * soc-card.h
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef __SOC_CARD_H
#define __SOC_CARD_H

struct snd_kcontrol *snd_soc_card_get_kcontrol(struct snd_soc_card *soc_card,
					       const char *name);

#endif /* __SOC_CARD_H */
