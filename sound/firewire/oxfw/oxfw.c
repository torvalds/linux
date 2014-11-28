/*
 * oxfw.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

#define OXFORD_FIRMWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x50000)
/* 0x970?vvvv or 0x971?vvvv, where vvvv = firmware version */

#define OXFORD_HARDWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x90020)
#define OXFORD_HARDWARE_ID_OXFW970	0x39443841
#define OXFORD_HARDWARE_ID_OXFW971	0x39373100

#define VENDOR_GRIFFIN		0x001292
#define VENDOR_LACIE		0x00d04b

#define SPECIFIER_1394TA	0x00a02d
#define VERSION_AVC		0x010001

MODULE_DESCRIPTION("Oxford Semiconductor FW970/971 driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("snd-firewire-speakers");

enum control_action { CTL_READ, CTL_WRITE };
enum control_attribute {
	CTL_MIN		= 0x02,
	CTL_MAX		= 0x03,
	CTL_CURRENT	= 0x10,
};

static int oxfw_mute_command(struct snd_oxfw *oxfw, bool *value,
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
	buf[4] = oxfw->device_info->mute_fb_id; /* function block ID */
	buf[5] = 0x10;			/* control attribute: current */
	buf[6] = 0x02;			/* selector length */
	buf[7] = 0x00;			/* audio channel number */
	buf[8] = 0x01;			/* control selector: mute */
	buf[9] = 0x01;			/* control data length */
	if (action == CTL_READ)
		buf[10] = 0xff;
	else
		buf[10] = *value ? 0x70 : 0x60;

	err = fcp_avc_transaction(oxfw->unit, buf, 11, buf, 11, 0x3fe);
	if (err < 0)
		goto error;
	if (err < 11) {
		dev_err(&oxfw->unit->device, "short FCP response\n");
		err = -EIO;
		goto error;
	}
	if (buf[0] != response_ok) {
		dev_err(&oxfw->unit->device, "mute command failed\n");
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

static int oxfw_volume_command(struct snd_oxfw *oxfw, s16 *value,
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
	buf[4] = oxfw->device_info->volume_fb_id; /* function block ID */
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

	err = fcp_avc_transaction(oxfw->unit, buf, 12, buf, 12, 0x3fe);
	if (err < 0)
		goto error;
	if (err < 12) {
		dev_err(&oxfw->unit->device, "short FCP response\n");
		err = -EIO;
		goto error;
	}
	if (buf[0] != response_ok) {
		dev_err(&oxfw->unit->device, "volume command failed\n");
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

static int oxfw_mute_get(struct snd_kcontrol *control,
			  struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;

	value->value.integer.value[0] = !oxfw->mute;

	return 0;
}

static int oxfw_mute_put(struct snd_kcontrol *control,
			  struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	bool mute;
	int err;

	mute = !value->value.integer.value[0];

	if (mute == oxfw->mute)
		return 0;

	err = oxfw_mute_command(oxfw, &mute, CTL_WRITE);
	if (err < 0)
		return err;
	oxfw->mute = mute;

	return 1;
}

static int oxfw_volume_info(struct snd_kcontrol *control,
			     struct snd_ctl_elem_info *info)
{
	struct snd_oxfw *oxfw = control->private_data;

	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = oxfw->device_info->mixer_channels;
	info->value.integer.min = oxfw->volume_min;
	info->value.integer.max = oxfw->volume_max;

	return 0;
}

static const u8 channel_map[6] = { 0, 1, 4, 5, 2, 3 };

static int oxfw_volume_get(struct snd_kcontrol *control,
			    struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	unsigned int i;

	for (i = 0; i < oxfw->device_info->mixer_channels; ++i)
		value->value.integer.value[channel_map[i]] = oxfw->volume[i];

	return 0;
}

static int oxfw_volume_put(struct snd_kcontrol *control,
			  struct snd_ctl_elem_value *value)
{
	struct snd_oxfw *oxfw = control->private_data;
	unsigned int i, changed_channels;
	bool equal_values = true;
	s16 volume;
	int err;

	for (i = 0; i < oxfw->device_info->mixer_channels; ++i) {
		if (value->value.integer.value[i] < oxfw->volume_min ||
		    value->value.integer.value[i] > oxfw->volume_max)
			return -EINVAL;
		if (value->value.integer.value[i] !=
		    value->value.integer.value[0])
			equal_values = false;
	}

	changed_channels = 0;
	for (i = 0; i < oxfw->device_info->mixer_channels; ++i)
		if (value->value.integer.value[channel_map[i]] !=
							oxfw->volume[i])
			changed_channels |= 1 << (i + 1);

	if (equal_values && changed_channels != 0)
		changed_channels = 1 << 0;

	for (i = 0; i <= oxfw->device_info->mixer_channels; ++i) {
		volume = value->value.integer.value[channel_map[i ? i - 1 : 0]];
		if (changed_channels & (1 << i)) {
			err = oxfw_volume_command(oxfw, &volume, i,
						   CTL_CURRENT, CTL_WRITE);
			if (err < 0)
				return err;
		}
		if (i > 0)
			oxfw->volume[i - 1] = volume;
	}

	return changed_channels != 0;
}

static int oxfw_create_mixer(struct snd_oxfw *oxfw)
{
	static const struct snd_kcontrol_new controls[] = {
		{
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.name = "PCM Playback Switch",
			.info = snd_ctl_boolean_mono_info,
			.get = oxfw_mute_get,
			.put = oxfw_mute_put,
		},
		{
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.name = "PCM Playback Volume",
			.info = oxfw_volume_info,
			.get = oxfw_volume_get,
			.put = oxfw_volume_put,
		},
	};
	unsigned int i, first_ch;
	int err;

	err = oxfw_volume_command(oxfw, &oxfw->volume_min,
				   0, CTL_MIN, CTL_READ);
	if (err < 0)
		return err;
	err = oxfw_volume_command(oxfw, &oxfw->volume_max,
				   0, CTL_MAX, CTL_READ);
	if (err < 0)
		return err;

	err = oxfw_mute_command(oxfw, &oxfw->mute, CTL_READ);
	if (err < 0)
		return err;

	first_ch = oxfw->device_info->mixer_channels == 1 ? 0 : 1;
	for (i = 0; i < oxfw->device_info->mixer_channels; ++i) {
		err = oxfw_volume_command(oxfw, &oxfw->volume[i],
					   first_ch + i, CTL_CURRENT, CTL_READ);
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

static u32 oxfw_read_firmware_version(struct fw_unit *unit)
{
	__be32 data;
	int err;

	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				 OXFORD_FIRMWARE_ID_ADDRESS, &data, 4, 0);
	return err >= 0 ? be32_to_cpu(data) : 0;
}

static void oxfw_card_free(struct snd_card *card)
{
	struct snd_oxfw *oxfw = card->private_data;

	mutex_destroy(&oxfw->mutex);
}

static int oxfw_probe(struct fw_unit *unit,
		       const struct ieee1394_device_id *id)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct snd_card *card;
	struct snd_oxfw *oxfw;
	u32 firmware;
	int err;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(*oxfw), &card);
	if (err < 0)
		return err;

	card->private_free = oxfw_card_free;
	oxfw = card->private_data;
	oxfw->card = card;
	mutex_init(&oxfw->mutex);
	oxfw->unit = unit;
	oxfw->device_info = (const struct device_info *)id->driver_data;

	strcpy(card->driver, oxfw->device_info->driver_name);
	strcpy(card->shortname, oxfw->device_info->short_name);
	firmware = oxfw_read_firmware_version(unit);
	snprintf(card->longname, sizeof(card->longname),
		 "%s (OXFW%x %04x), GUID %08x%08x at %s, S%d",
		 oxfw->device_info->long_name,
		 firmware >> 20, firmware & 0xffff,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&unit->device), 100 << fw_dev->max_speed);
	strcpy(card->mixername, "OXFW");

	err = snd_oxfw_create_pcm(oxfw);
	if (err < 0)
		goto error;

	err = oxfw_create_mixer(oxfw);
	if (err < 0)
		goto error;

	err = snd_oxfw_stream_init_simplex(oxfw);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0) {
		snd_oxfw_stream_destroy_simplex(oxfw);
		goto error;
	}
	dev_set_drvdata(&unit->device, oxfw);

	return 0;
error:
	snd_card_free(card);
	return err;
}

static void oxfw_bus_reset(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	fcp_bus_reset(oxfw->unit);

	mutex_lock(&oxfw->mutex);
	snd_oxfw_stream_update_simplex(oxfw);
	mutex_unlock(&oxfw->mutex);
}

static void oxfw_remove(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	snd_card_disconnect(oxfw->card);

	snd_oxfw_stream_destroy_simplex(oxfw);

	snd_card_free_when_closed(oxfw->card);
}

static const struct device_info griffin_firewave = {
	.driver_name = "FireWave",
	.short_name  = "FireWave",
	.long_name   = "Griffin FireWave Surround",
	.pcm_constraints = firewave_constraints,
	.mixer_channels = 6,
	.mute_fb_id   = 0x01,
	.volume_fb_id = 0x02,
};

static const struct device_info lacie_speakers = {
	.driver_name = "FWSpeakers",
	.short_name  = "FireWire Speakers",
	.long_name   = "LaCie FireWire Speakers",
	.pcm_constraints = lacie_speakers_constraints,
	.mixer_channels = 1,
	.mute_fb_id   = 0x01,
	.volume_fb_id = 0x01,
};

static const struct ieee1394_device_id oxfw_id_table[] = {
	{
		.match_flags  = IEEE1394_MATCH_VENDOR_ID |
				IEEE1394_MATCH_MODEL_ID |
				IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
		.vendor_id    = VENDOR_GRIFFIN,
		.model_id     = 0x00f970,
		.specifier_id = SPECIFIER_1394TA,
		.version      = VERSION_AVC,
		.driver_data  = (kernel_ulong_t)&griffin_firewave,
	},
	{
		.match_flags  = IEEE1394_MATCH_VENDOR_ID |
				IEEE1394_MATCH_MODEL_ID |
				IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
		.vendor_id    = VENDOR_LACIE,
		.model_id     = 0x00f970,
		.specifier_id = SPECIFIER_1394TA,
		.version      = VERSION_AVC,
		.driver_data  = (kernel_ulong_t)&lacie_speakers,
	},
	{ }
};
MODULE_DEVICE_TABLE(ieee1394, oxfw_id_table);

static struct fw_driver oxfw_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = oxfw_probe,
	.update   = oxfw_bus_reset,
	.remove   = oxfw_remove,
	.id_table = oxfw_id_table,
};

static int __init snd_oxfw_init(void)
{
	return driver_register(&oxfw_driver.driver);
}

static void __exit snd_oxfw_exit(void)
{
	driver_unregister(&oxfw_driver.driver);
}

module_init(snd_oxfw_init);
module_exit(snd_oxfw_exit);
