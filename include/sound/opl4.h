/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_OPL4_H
#define __SOUND_OPL4_H

/*
 * Global definitions for the OPL4 driver
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 */

#include <sound/opl3.h>

struct snd_opl4;

extern int snd_opl4_create(struct snd_card *card,
			   unsigned long fm_port, unsigned long pcm_port,
			   int seq_device,
			   struct snd_opl3 **opl3, struct snd_opl4 **opl4);

#endif /* __SOUND_OPL4_H */
