/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 */

#ifndef MOP500_AB8500_H
#define MOP500_AB8500_H

extern const struct snd_soc_ops mop500_ab8500_ops[];

int mop500_ab8500_machine_init(struct snd_soc_pcm_runtime *rtd);
void mop500_ab8500_remove(struct snd_soc_card *card);

#endif
