/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for M2Tech hiFace compatible devices
 *
 * Copyright 2012-2013 (C) M2TECH S.r.l and Amarula Solutions B.V.
 *
 * Authors:  Michael Trimarchi <michael@amarulasolutions.com>
 *           Antonio Ospite <ao2@amarulasolutions.com>
 *
 * The driver is based on the work done in TerraTec DMX 6Fire USB
 */

#ifndef HIFACE_PCM_H
#define HIFACE_PCM_H

struct hiface_chip;

int hiface_pcm_init(struct hiface_chip *chip, u8 extra_freq);
void hiface_pcm_abort(struct hiface_chip *chip);
#endif /* HIFACE_PCM_H */
