// SPDX-License-Identifier: GPL-2.0
// HD-Audio helpers to sync with DRM driver

#ifndef __SOUND_HDA_COMPONENT_H
#define __SOUND_HDA_COMPONENT_H

#include <drm/drm_audio_component.h>
#include <sound/hdaudio.h>

/* virtual idx for controller */
#define HDA_CODEC_IDX_CONTROLLER	HDA_MAX_CODECS

#ifdef CONFIG_SND_HDA_COMPONENT
int snd_hdac_set_codec_wakeup(struct hdac_bus *bus, bool enable);
void snd_hdac_display_power(struct hdac_bus *bus, unsigned int idx,
			    bool enable);
int snd_hdac_sync_audio_rate(struct hdac_device *codec, hda_nid_t nid,
			     int dev_id, int rate);
int snd_hdac_acomp_get_eld(struct hdac_device *codec, hda_nid_t nid, int dev_id,
			   bool *audio_enabled, char *buffer, int max_bytes);
int snd_hdac_acomp_init(struct hdac_bus *bus,
			const struct drm_audio_component_audio_ops *aops,
			int (*match_master)(struct device *, void *),
			size_t extra_size);
int snd_hdac_acomp_exit(struct hdac_bus *bus);
int snd_hdac_acomp_register_notifier(struct hdac_bus *bus,
				    const struct drm_audio_component_audio_ops *ops);
#else
static inline int snd_hdac_set_codec_wakeup(struct hdac_bus *bus, bool enable)
{
	return 0;
}
static inline void snd_hdac_display_power(struct hdac_bus *bus,
					  unsigned int idx, bool enable)
{
}
static inline int snd_hdac_sync_audio_rate(struct hdac_device *codec,
					   hda_nid_t nid, int dev_id, int rate)
{
	return 0;
}
static inline int snd_hdac_acomp_get_eld(struct hdac_device *codec, hda_nid_t nid,
					 int dev_id, bool *audio_enabled,
					 char *buffer, int max_bytes)
{
	return -ENODEV;
}
static inline int snd_hdac_acomp_init(struct hdac_bus *bus,
				      const struct drm_audio_component_audio_ops *aops,
				      int (*match_master)(struct device *, void *),
				      size_t extra_size)
{
	return -ENODEV;
}
static inline int snd_hdac_acomp_exit(struct hdac_bus *bus)
{
	return 0;
}
static inline int snd_hdac_acomp_register_notifier(struct hdac_bus *bus,
						  const struct drm_audio_component_audio_ops *ops)
{
	return -ENODEV;
}
#endif

#endif /* __SOUND_HDA_COMPONENT_H */
