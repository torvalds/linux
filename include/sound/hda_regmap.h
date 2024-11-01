/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HD-audio regmap helpers
 */

#ifndef __SOUND_HDA_REGMAP_H
#define __SOUND_HDA_REGMAP_H

#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/hdaudio.h>

#define AC_AMP_FAKE_MUTE	0x10	/* fake mute bit set to amp verbs */

int snd_hdac_regmap_init(struct hdac_device *codec);
void snd_hdac_regmap_exit(struct hdac_device *codec);
int snd_hdac_regmap_add_vendor_verb(struct hdac_device *codec,
				    unsigned int verb);
int snd_hdac_regmap_read_raw(struct hdac_device *codec, unsigned int reg,
			     unsigned int *val);
int snd_hdac_regmap_read_raw_uncached(struct hdac_device *codec,
				      unsigned int reg, unsigned int *val);
int snd_hdac_regmap_write_raw(struct hdac_device *codec, unsigned int reg,
			      unsigned int val);
int snd_hdac_regmap_update_raw(struct hdac_device *codec, unsigned int reg,
			       unsigned int mask, unsigned int val);
int snd_hdac_regmap_update_raw_once(struct hdac_device *codec, unsigned int reg,
				    unsigned int mask, unsigned int val);
void snd_hdac_regmap_sync(struct hdac_device *codec);

/**
 * snd_hdac_regmap_encode_verb - encode the verb to a pseudo register
 * @nid: widget NID
 * @verb: codec verb
 *
 * Returns an encoded pseudo register.
 */
#define snd_hdac_regmap_encode_verb(nid, verb)		\
	(((verb) << 8) | 0x80000 | ((unsigned int)(nid) << 20))

/**
 * snd_hdac_regmap_encode_amp - encode the AMP verb to a pseudo register
 * @nid: widget NID
 * @ch: channel (left = 0, right = 1)
 * @dir: direction (#HDA_INPUT, #HDA_OUTPUT)
 * @idx: input index value
 *
 * Returns an encoded pseudo register.
 */
#define snd_hdac_regmap_encode_amp(nid, ch, dir, idx)			\
	(snd_hdac_regmap_encode_verb(nid, AC_VERB_GET_AMP_GAIN_MUTE) |	\
	 ((ch) ? AC_AMP_GET_RIGHT : AC_AMP_GET_LEFT) |			\
	 ((dir) == HDA_OUTPUT ? AC_AMP_GET_OUTPUT : AC_AMP_GET_INPUT) | \
	 (idx))

/**
 * snd_hdac_regmap_encode_amp_stereo - encode a pseudo register for stereo AMPs
 * @nid: widget NID
 * @dir: direction (#HDA_INPUT, #HDA_OUTPUT)
 * @idx: input index value
 *
 * Returns an encoded pseudo register.
 */
#define snd_hdac_regmap_encode_amp_stereo(nid, dir, idx)		\
	(snd_hdac_regmap_encode_verb(nid, AC_VERB_GET_AMP_GAIN_MUTE) |	\
	 AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT | /* both bits set! */	\
	 ((dir) == HDA_OUTPUT ? AC_AMP_GET_OUTPUT : AC_AMP_GET_INPUT) | \
	 (idx))

/**
 * snd_hdac_regmap_write - Write a verb with caching
 * @nid: codec NID
 * @reg: verb to write
 * @val: value to write
 *
 * For writing an amp value, use snd_hdac_regmap_update_amp().
 */
static inline int
snd_hdac_regmap_write(struct hdac_device *codec, hda_nid_t nid,
		      unsigned int verb, unsigned int val)
{
	unsigned int cmd = snd_hdac_regmap_encode_verb(nid, verb);

	return snd_hdac_regmap_write_raw(codec, cmd, val);
}

/**
 * snd_hda_regmap_update - Update a verb value with caching
 * @nid: codec NID
 * @verb: verb to update
 * @mask: bit mask to update
 * @val: value to update
 *
 * For updating an amp value, use snd_hdac_regmap_update_amp().
 */
static inline int
snd_hdac_regmap_update(struct hdac_device *codec, hda_nid_t nid,
		       unsigned int verb, unsigned int mask,
		       unsigned int val)
{
	unsigned int cmd = snd_hdac_regmap_encode_verb(nid, verb);

	return snd_hdac_regmap_update_raw(codec, cmd, mask, val);
}

/**
 * snd_hda_regmap_read - Read a verb with caching
 * @nid: codec NID
 * @verb: verb to read
 * @val: pointer to store the value
 *
 * For reading an amp value, use snd_hda_regmap_get_amp().
 */
static inline int
snd_hdac_regmap_read(struct hdac_device *codec, hda_nid_t nid,
		     unsigned int verb, unsigned int *val)
{
	unsigned int cmd = snd_hdac_regmap_encode_verb(nid, verb);

	return snd_hdac_regmap_read_raw(codec, cmd, val);
}

/**
 * snd_hdac_regmap_get_amp - Read AMP value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @index: the index value (only for input direction)
 * @val: the pointer to store the value
 *
 * Read AMP value.  The volume is between 0 to 0x7f, 0x80 = mute bit.
 * Returns the value or a negative error.
 */
static inline int
snd_hdac_regmap_get_amp(struct hdac_device *codec, hda_nid_t nid,
			int ch, int dir, int idx)
{
	unsigned int cmd = snd_hdac_regmap_encode_amp(nid, ch, dir, idx);
	int err, val;

	err = snd_hdac_regmap_read_raw(codec, cmd, &val);
	return err < 0 ? err : val;
}

/**
 * snd_hdac_regmap_update_amp - update the AMP value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the AMP value with a bit mask.
 * Returns 0 if the value is unchanged, 1 if changed, or a negative error.
 */
static inline int
snd_hdac_regmap_update_amp(struct hdac_device *codec, hda_nid_t nid,
			   int ch, int dir, int idx, int mask, int val)
{
	unsigned int cmd = snd_hdac_regmap_encode_amp(nid, ch, dir, idx);

	return snd_hdac_regmap_update_raw(codec, cmd, mask, val);
}

/**
 * snd_hdac_regmap_get_amp_stereo - Read stereo AMP values
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @index: the index value (only for input direction)
 * @val: the pointer to store the value
 *
 * Read stereo AMP values.  The lower byte is left, the upper byte is right.
 * Returns the value or a negative error.
 */
static inline int
snd_hdac_regmap_get_amp_stereo(struct hdac_device *codec, hda_nid_t nid,
			       int dir, int idx)
{
	unsigned int cmd = snd_hdac_regmap_encode_amp_stereo(nid, dir, idx);
	int err, val;

	err = snd_hdac_regmap_read_raw(codec, cmd, &val);
	return err < 0 ? err : val;
}

/**
 * snd_hdac_regmap_update_amp_stereo - update the stereo AMP value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the stereo AMP value with a bit mask.
 * The lower byte is left, the upper byte is right.
 * Returns 0 if the value is unchanged, 1 if changed, or a negative error.
 */
static inline int
snd_hdac_regmap_update_amp_stereo(struct hdac_device *codec, hda_nid_t nid,
				  int dir, int idx, int mask, int val)
{
	unsigned int cmd = snd_hdac_regmap_encode_amp_stereo(nid, dir, idx);

	return snd_hdac_regmap_update_raw(codec, cmd, mask, val);
}

/**
 * snd_hdac_regmap_sync_node - sync the widget node attributes
 * @codec: HD-audio codec
 * @nid: NID to sync
 */
static inline void
snd_hdac_regmap_sync_node(struct hdac_device *codec, hda_nid_t nid)
{
	regcache_mark_dirty(codec->regmap);
	regcache_sync_region(codec->regmap, nid << 20, ((nid + 1) << 20) - 1);
}

#endif /* __SOUND_HDA_REGMAP_H */
