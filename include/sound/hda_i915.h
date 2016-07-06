/*
 * HD-Audio helpers to sync with i915 driver
 */
#ifndef __SOUND_HDA_I915_H
#define __SOUND_HDA_I915_H

#include <drm/i915_component.h>

#ifdef CONFIG_SND_HDA_I915
int snd_hdac_set_codec_wakeup(struct hdac_bus *bus, bool enable);
int snd_hdac_display_power(struct hdac_bus *bus, bool enable);
void snd_hdac_i915_set_bclk(struct hdac_bus *bus);
int snd_hdac_sync_audio_rate(struct hdac_device *codec, hda_nid_t nid, int rate);
int snd_hdac_acomp_get_eld(struct hdac_device *codec, hda_nid_t nid,
			   bool *audio_enabled, char *buffer, int max_bytes);
int snd_hdac_i915_init(struct hdac_bus *bus);
int snd_hdac_i915_exit(struct hdac_bus *bus);
int snd_hdac_i915_register_notifier(const struct i915_audio_component_audio_ops *);
#else
static inline int snd_hdac_set_codec_wakeup(struct hdac_bus *bus, bool enable)
{
	return 0;
}
static inline int snd_hdac_display_power(struct hdac_bus *bus, bool enable)
{
	return 0;
}
static inline void snd_hdac_i915_set_bclk(struct hdac_bus *bus)
{
}
static inline int snd_hdac_sync_audio_rate(struct hdac_device *codec,
					   hda_nid_t nid, int rate)
{
	return 0;
}
static inline int snd_hdac_acomp_get_eld(struct hdac_device *codec, hda_nid_t nid,
					 bool *audio_enabled, char *buffer,
					 int max_bytes)
{
	return -ENODEV;
}
static inline int snd_hdac_i915_init(struct hdac_bus *bus)
{
	return -ENODEV;
}
static inline int snd_hdac_i915_exit(struct hdac_bus *bus)
{
	return 0;
}
static inline int snd_hdac_i915_register_notifier(const struct i915_audio_component_audio_ops *ops)
{
	return -ENODEV;
}
#endif

#endif /* __SOUND_HDA_I915_H */
