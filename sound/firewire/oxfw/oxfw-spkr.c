/*
 * oxfw-spkr.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

struct fw_spkr {
	bool mute;
	s16 volume[6];
	s16 volume_min;
	s16 volume_max;

	unsigned int mixer_channels;
	u8 mute_fb_id;
	u8 volume_fb_id;
};

enum control_action { CTL_READ, CTL_WRITE };
enum control_attribute {
	CTL_MIN		= 0x02,
	CTL_MAX		= 0x03,
	CTL_CURRENT	= 0x10,
};

static int avc_audio_feature_mute(struct fw_unit *unit, u8 fb_id, bool *value,
				  enum control_action action)
{
	u8 *buf;
	u8 response_ok;
	int err;

	buf = kmalloc(11, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (action == CTL_READ) {
		buf[0] = 0x01;		/* AV/C, STATUS */
		response_ok = 0x0c;	/*       STABLE */
	} else {
		buf[0] = 0x00;		/* AV/C, CONTROL */
		response_ok = 0x09;	/*       ACCEPTED */
	}
	buf[1] = 0x08;			/* audio unit 0 */
	buf[2] = 0xb8;			/* FUNCTION BLOCK */
	buf[3] = 0x81;			/* function block type: feature */
	buf[4] = fb_id;			/* function block ID */
	buf[5] = 0x10;			/* control attribute: current */
	buf[6] = 0x02;			/* selector length */
	buf[7] = 0x00;			/* audio channel number */
	buf[8] = 0x01;			/* control selector: mute */
	buf[9] = 0x01;			/* control data length */
	if (action == CTL_READ)
		buf[10] = 0xff;
	else
		buf[10] = *value ? 0x70 : 0x60;

	err = fcp_avc_transaction(unit, buf, 11, buf, 11, 0x3fe);
	if (err < 0)
		goto error;
	if (err < 11) {
		dev_err(&unit->device, "short FCP response\n");
		err = -EIO;
		goto error;
	}
	if (buf[0] != response_ok) {
		dev_err(&unit->device, "mute command failed\n");
		err = -EIO;
		goto error;
	}
	if (action == CTL_READ)
		*value = buf[10] == 0x70;

	err = 0;

error:
	kfree(buf);

	return err;
}

static int avc_audio_feature_volume(struct fw_unit *unit, u8 fb_id, s16 *value,
				    unsigned int channel,
				    enum control_attribute attribute,
				    enum control_action action)
{
	u8 *buf;
	u8 response_ok;
	int err;

	buf = kmalloc(12, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (action == CTL_READ) {
		buf[0] = 0x01;		/* AV/C, STATUS */
		response_ok = 0x0c;	/*       STABLE */
	} else {
		buf[0] = 0x00;		/* AV/C, CONTROL */
		response_ok = 0x09;	/*       ACCEPTED */
	}
	buf[1] = 0x08;			/* audio unit 0 */
	buf[2] = 0xb8;			/* FUNCTION BLOCK */
	buf[3] = 0x81;			/* function block type: feature */
	buf[4] = fb_id;			/* function block ID */
	buf[5] = attribute;		/* control attribute */
	buf[6] = 0x02;			/* selector length */
	buf[7] = channel;		/* audio channel number */
	buf[8] = 0x02;			/* control selector: volume */
	buf[9] = 0x02;			/* control data length */
	if (action == CTL_READ) {
		buf[10] = 0xff;
		buf[11] = 0xff;
	} else {
		buf[10] = *value >> 8;
		buf[11] = *value;
	}

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0x3fe);
	if (err < 0)
		goto error;
	if (err < 12) {
		dev_err(&unit->device, "short FCP response\n");
		err = -EIO;
		goto error;
	}
	if (buf[0] != response_ok) {
		dev_err(&unit->device, "volume command failed\n");
		err = -EIO;
		goto error;
	}
	if (action == CTL_READ)
		*value = (buf[10] << 8) | buf[11];

	err = 0;

error:
	kfree(buf);

	return err;
}

static int spkr_mute_get(struct snd_kcontrol *control,
			 struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	struct fw_spkr *spkr = oxfw->spec;

	value->value.integer.value[0] = !spkr->mute;

	return 0;
}

static int spkr_mute_put(struct snd_kcontrol *control,
			 struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	struct fw_spkr *spkr = oxfw->spec;
	bool mute;
	int err;

	mute = !value->value.integer.value[0];

	if (mute == spkr->mute)
		return 0;

	err = avc_audio_feature_mute(oxfw->unit, spkr->mute_fb_id, &mute,
				     CTL_WRITE);
	if (err < 0)
		return err;
	spkr->mute = mute;

	return 1;
}

static int spkr_volume_info(struct snd_kcontrol *control,
			    struct snd_ctl_elem_info *info)
{
	struct snd_oxfw *oxfw = control->private_data;
	struct fw_spkr *spkr = oxfw->spec;

	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = spkr->mixer_channels;
	info->value.integer.min = spkr->volume_min;
	info->value.integer.max = spkr->volume_max;

	return 0;
}

static const u8 channel_map[6] = { 0, 1, 4, 5, 2, 3 };

static int spkr_volume_get(struct snd_kcontrol *control,
			   struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	struct fw_spkr *spkr = oxfw->spec;
	unsigned int i;

	for (i = 0; i < spkr->mixer_channels; ++i)
		value->value.integer.value[channel_map[i]] = spkr->volume[i];

	return 0;
}

static int spkr_volume_put(struct snd_kcontrol *control,
			   struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	struct fw_spkr *spkr = oxfw->spec;
	unsigned int i, changed_channels;
	bool equal_values = true;
	s16 volume;
	int err;

	for (i = 0; i < spkr->mixer_channels; ++i) {
		if (value->value.integer.value[i] < spkr->volume_min ||
		    value->value.integer.value[i] > spkr->volume_max)
			return -EINVAL;
		if (value->value.integer.value[i] !=
		    value->value.integer.value[0])
			equal_values = false;
	}

	changed_channels = 0;
	for (i = 0; i < spkr->mixer_channels; ++i)
		if (value->value.integer.value[channel_map[i]] !=
							spkr->volume[i])
			changed_channels |= 1 << (i + 1);

	if (equal_values && changed_channels != 0)
		changed_channels = 1 << 0;

	for (i = 0; i <= spkr->mixer_channels; ++i) {
		volume = value->value.integer.value[channel_map[i ? i - 1 : 0]];
		if (changed_channels & (1 << i)) {
			err = avc_audio_feature_volume(oxfw->unit,
						  spkr->volume_fb_id, &volume,
						  i, CTL_CURRENT, CTL_WRITE);
			if (err < 0)
				return err;
		}
		if (i > 0)
			spkr->volume[i - 1] = volume;
	}

	return changed_channels != 0;
}

int snd_oxfw_add_spkr(struct snd_oxfw *oxfw, bool is_lacie)
{
	static const struct snd_kcontrol_new controls[] = {
		{
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.name = "PCM Playback Switch",
			.info = snd_ctl_boolean_mono_info,
			.get = spkr_mute_get,
			.put = spkr_mute_put,
		},
		{
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.name = "PCM Playback Volume",
			.info = spkr_volume_info,
			.get = spkr_volume_get,
			.put = spkr_volume_put,
		},
	};
	struct fw_spkr *spkr;
	unsigned int i, first_ch;
	int err;

	spkr = devm_kzalloc(&oxfw->card->card_dev, sizeof(struct fw_spkr),
			    GFP_KERNEL);
	if (!spkr)
		return -ENOMEM;
	oxfw->spec = spkr;

	if (is_lacie) {
		spkr->mixer_channels = 1;
		spkr->mute_fb_id = 0x01;
		spkr->volume_fb_id = 0x01;
	} else {
		spkr->mixer_channels = 6;
		spkr->mute_fb_id = 0x01;
		spkr->volume_fb_id = 0x02;
	}

	err = avc_audio_feature_volume(oxfw->unit, spkr->volume_fb_id,
				       &spkr->volume_min, 0, CTL_MIN, CTL_READ);
	if (err < 0)
		return err;
	err = avc_audio_feature_volume(oxfw->unit, spkr->volume_fb_id,
				       &spkr->volume_max, 0, CTL_MAX, CTL_READ);
	if (err < 0)
		return err;

	err = avc_audio_feature_mute(oxfw->unit, spkr->mute_fb_id, &spkr->mute,
				     CTL_READ);
	if (err < 0)
		return err;

	first_ch = spkr->mixer_channels == 1 ? 0 : 1;
	for (i = 0; i < spkr->mixer_channels; ++i) {
		err = avc_audio_feature_volume(oxfw->unit, spkr->volume_fb_id,
					       &spkr->volume[i], first_ch + i,
					       CTL_CURRENT, CTL_READ);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(controls); ++i) {
		err = snd_ctl_add(oxfw->card,
				  snd_ctl_new1(&controls[i], oxfw));
		if (err < 0)
			return err;
	}

	return 0;
}
