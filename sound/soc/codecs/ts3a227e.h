/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TS3A227E Autonous Audio Accessory Detection and Configureation Switch
 *
 * Copyright (C) 2014 Google, Inc.
 */

#ifndef _TS3A227E_H
#define _TS3A227E_H

int ts3a227e_enable_jack_detect(struct snd_soc_component *component,
				struct snd_soc_jack *jack);

#endif
