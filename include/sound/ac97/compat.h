/*
 *  Copyright (C) 2016 Robert Jarzmik <robert.jarzmik@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file is for backward compatibility with snd_ac97 structure and its
 * multiple usages, such as the snd_ac97_bus and snd_ac97_build_ops.
 *
 */
#ifndef AC97_COMPAT_H
#define AC97_COMPAT_H

#include <sound/ac97_codec.h>

struct snd_ac97 *snd_ac97_compat_alloc(struct ac97_codec_device *adev);
void snd_ac97_compat_release(struct snd_ac97 *ac97);

#endif
