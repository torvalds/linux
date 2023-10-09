/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HD-Audio helpers to sync with i915 driver
 */
#ifndef __SOUND_HDA_I915_H
#define __SOUND_HDA_I915_H

#include "hda_component.h"

#ifdef CONFIG_SND_HDA_I915
void snd_hdac_i915_set_bclk(struct hdac_bus *bus);
int snd_hdac_i915_init(struct hdac_bus *bus);
#else
static inline void snd_hdac_i915_set_bclk(struct hdac_bus *bus)
{
}
static inline int snd_hdac_i915_init(struct hdac_bus *bus)
{
	return -ENODEV;
}
#endif
static inline int snd_hdac_i915_exit(struct hdac_bus *bus)
{
	return snd_hdac_acomp_exit(bus);
}

#endif /* __SOUND_HDA_I915_H */
