/* SPDX-License-Identifier: GPL-2.0 */
/*
 * For multichannel support
 */

#ifndef __SOUND_HDA_CHMAP_H
#define __SOUND_HDA_CHMAP_H

#include <sound/pcm.h>
#include <sound/hdaudio.h>


#define SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE 80

struct hdac_cea_channel_speaker_allocation {
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
		struct hdac_cea_channel_speaker_allocation *cap, int channels);
	void (*cea_alloc_to_tlv_chmap)(struct hdac_chmap *hchmap,
		struct hdac_cea_channel_speaker_allocation *cap,
		unsigned int *chmap, int channels);

	/* check that the user-given chmap is supported */
	int (*chmap_validate)(struct hdac_chmap *hchmap, int ca,
			int channels, unsigned char *chmap);

	int (*get_spk_alloc)(struct hdac_device *hdac, int pcm_idx);

	void (*get_chmap)(struct hdac_device *hdac, int pcm_idx,
					unsigned char *chmap);
	void (*set_chmap)(struct hdac_device *hdac, int pcm_idx,
			unsigned char *chmap, int prepared);
	bool (*is_pcm_attached)(struct hdac_device *hdac, int pcm_idx);

	/* get and set channel assigned to each HDMI ASP (audio sample packet) slot */
	int (*pin_get_slot_channel)(struct hdac_device *codec,
			hda_nid_t pin_nid, int asp_slot);
	int (*pin_set_slot_channel)(struct hdac_device *codec,
			hda_nid_t pin_nid, int asp_slot, int channel);
	void (*set_channel_count)(struct hdac_device *codec,
				hda_nid_t cvt_nid, int chs);
};

struct hdac_chmap {
	unsigned int channels_max; /* max over all cvts */
	struct hdac_chmap_ops ops;
	struct hdac_device *hdac;
};

void snd_hdac_register_chmap_ops(struct hdac_device *hdac,
				struct hdac_chmap *chmap);
int snd_hdac_channel_allocation(struct hdac_device *hdac, int spk_alloc,
			int channels, bool chmap_set,
			bool non_pcm, unsigned char *map);
int snd_hdac_get_active_channels(int ca);
void snd_hdac_setup_channel_mapping(struct hdac_chmap *chmap,
		       hda_nid_t pin_nid, bool non_pcm, int ca,
		       int channels, unsigned char *map,
		       bool chmap_set);
void snd_hdac_print_channel_allocation(int spk_alloc, char *buf, int buflen);
struct hdac_cea_channel_speaker_allocation *snd_hdac_get_ch_alloc_from_ca(int ca);
int snd_hdac_chmap_to_spk_mask(unsigned char c);
int snd_hdac_spk_to_chmap(int spk);
int snd_hdac_add_chmap_ctls(struct snd_pcm *pcm, int pcm_idx,
				struct hdac_chmap *chmap);
#endif /* __SOUND_HDA_CHMAP_H */
