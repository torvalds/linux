/*
 * For multichannel support
 */

#ifndef __SOUND_HDA_CHMAP_H
#define __SOUND_HDA_CHMAP_H

#include <sound/hdaudio.h>

struct cea_channel_speaker_allocation {
	int ca_index;
	int speakers[8];

	/* derived values, just for convenience */
	int channels;
	int spk_mask;
};
struct hdac_chmap;

struct hdac_chmap_ops {
	/*
	 * Helpers for producing the channel map TLVs. These can be overridden
	 * for devices that have non-standard mapping requirements.
	 */
	int (*chmap_cea_alloc_validate_get_type)(struct hdac_chmap *chmap,
		struct cea_channel_speaker_allocation *cap, int channels);
	void (*cea_alloc_to_tlv_chmap)
		(struct cea_channel_speaker_allocation *cap,
		unsigned int *chmap, int channels);

	/* check that the user-given chmap is supported */
	int (*chmap_validate)(int ca, int channels, unsigned char *chmap);
};

struct hdac_chmap {
	unsigned int channels_max; /* max over all cvts */
	struct hdac_chmap_ops ops;
	struct hdac_device *hdac;
};

#endif /* __SOUND_HDA_CHMAP_H */
