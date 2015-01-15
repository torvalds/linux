/*
 * TS3A227E Autonous Audio Accessory Detection and Configureation Switch
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _TS3A227E_H
#define _TS3A227E_H

int ts3a227e_enable_jack_detect(struct snd_soc_component *component,
				struct snd_soc_jack *jack);

#endif
