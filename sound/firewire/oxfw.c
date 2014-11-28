/*
 * oxfw.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "cmp.h"
#include "fcp.h"
#include "amdtp.h"
#include "lib.h"

#define OXFORD_FIRMWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x50000)
/* 0x970?vvvv or 0x971?vvvv, where vvvv = firmware version */

#define OXFORD_HARDWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x90020)
#define OXFORD_HARDWARE_ID_OXFW970	0x39443841
#define OXFORD_HARDWARE_ID_OXFW971	0x39373100

#define VENDOR_GRIFFIN		0x001292
#define VENDOR_LACIE		0x00d04b

#define SPECIFIER_1394TA	0x00a02d
#define VERSION_AVC		0x010001

struct device_info {
	const char *driver_name;
	const char *short_name;
	const char *long_name;
	int (*pcm_constraints)(struct snd_pcm_runtime *runtime);
	unsigned int mixer_channels;
	u8 mute_fb_id;
	u8 volume_fb_id;
};

struct snd_oxfw {
	struct snd_card *card;
	struct fw_unit *unit;
	const struct device_info *device_info;
	struct mutex mutex;
	struct cmp_connection in_conn;
	struct amdtp_stream rx_stream;
	bool mute;
	s16 volume[6];
	s16 volume_min;
	s16 volume_max;
};

MODULE_DESCRIPTION("Oxford Semiconductor FW970/971 driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("snd-firewire-speakers");

static int firewave_rate_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	static unsigned int stereo_rates[] = { 48000, 96000 };
	struct snd_interval *channels =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *rate =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	/* two channels work only at 48/96 kHz */
	if (snd_interval_max(channels) < 6)
		return snd_interval_list(rate, 2, stereo_rates, 0);
	return 0;
}

static int firewave_channels_constraint(struct snd_pcm_hw_params *params,
					struct snd_pcm_hw_rule *rule)
{
	static const struct snd_interval all_channels = { .min = 6, .max = 6 };
	struct snd_interval *rate =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	/* 32/44.1 kHz work only with all six channels */
	if (snd_interval_max(rate) < 48000)
		return snd_interval_refine(channels, &all_channels);
	return 0;
}

static int firewave_constraints(struct snd_pcm_runtime *runtime)
{
	static unsigned int channels_list[] = { 2, 6 };
	static struct snd_pcm_hw_constraint_list channels_list_constraint = {
		.count = 2,
		.list = channels_list,
	};
	int err;

	runtime->hw.rates = SNDRV_PCM_RATE_32000 |
			    SNDRV_PCM_RATE_44100 |
			    SNDRV_PCM_RATE_48000 |
			    SNDRV_PCM_RATE_96000;
	runtime->hw.channels_max = 6;

	err = snd_pcm_hw_constraint_list(runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 &channels_list_constraint);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  firewave_rate_constraint, NULL,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  firewave_channels_constraint, NULL,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;

	return 0;
}

static int lacie_speakers_constraints(struct snd_pcm_runtime *runtime)
{
	runtime->hw.rates = SNDRV_PCM_RATE_32000 |
			    SNDRV_PCM_RATE_44100 |
			    SNDRV_PCM_RATE_48000 |
			    SNDRV_PCM_RATE_88200 |
			    SNDRV_PCM_RATE_96000;

	return 0;
}

static int oxfw_open(struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		.formats = AMDTP_OUT_PCM_FORMAT_BITS,
		.channels_min = 2,
		.channels_max = 2,
		.buffer_bytes_max = 4 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
	};
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	runtime->hw = hardware;

	err = oxfw->device_info->pcm_constraints(runtime);
	if (err < 0)
		return err;
	err = snd_pcm_limit_hw_rates(runtime);
	if (err < 0)
		return err;

	err = amdtp_stream_add_pcm_hw_constraints(&oxfw->rx_stream, runtime);
	if (err < 0)
		return err;

	return 0;
}

static int oxfw_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static void oxfw_stop_stream(struct snd_oxfw *oxfw)
{
	if (amdtp_stream_running(&oxfw->rx_stream)) {
		amdtp_stream_stop(&oxfw->rx_stream);
		cmp_connection_break(&oxfw->in_conn);
	}
}

static int oxfw_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *hw_params)
{
	struct snd_oxfw *oxfw = substream->private_data;
	int err;

	mutex_lock(&oxfw->mutex);
	oxfw_stop_stream(oxfw);
	mutex_unlock(&oxfw->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		goto error;

	amdtp_stream_set_parameters(&oxfw->rx_stream,
				    params_rate(hw_params),
				    params_channels(hw_params),
				    0);

	amdtp_stream_set_pcm_format(&oxfw->rx_stream,
				    params_format(hw_params));

	err = avc_general_set_sig_fmt(oxfw->unit, params_rate(hw_params),
				      AVC_GENERAL_PLUG_DIR_IN, 0);
	if (err < 0) {
		dev_err(&oxfw->unit->device, "failed to set sample rate\n");
		goto err_buffer;
	}

	return 0;

err_buffer:
	snd_pcm_lib_free_vmalloc_buffer(substream);
error:
	return err;
}

static int oxfw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	mutex_lock(&oxfw->mutex);
	oxfw_stop_stream(oxfw);
	mutex_unlock(&oxfw->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int oxfw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	int err;

	mutex_lock(&oxfw->mutex);

	if (amdtp_streaming_error(&oxfw->rx_stream))
		oxfw_stop_stream(oxfw);

	if (!amdtp_stream_running(&oxfw->rx_stream)) {
		err = cmp_connection_establish(&oxfw->in_conn,
			amdtp_stream_get_max_payload(&oxfw->rx_stream));
		if (err < 0)
			goto err_mutex;

		err = amdtp_stream_start(&oxfw->rx_stream,
					 oxfw->in_conn.resources.channel,
					 oxfw->in_conn.speed);
		if (err < 0)
			goto err_connection;
	}

	mutex_unlock(&oxfw->mutex);

	amdtp_stream_pcm_prepare(&oxfw->rx_stream);

	return 0;

err_connection:
	cmp_connection_break(&oxfw->in_conn);
err_mutex:
	mutex_unlock(&oxfw->mutex);

	return err;
}

static int oxfw_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_substream *pcm;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pcm = NULL;
		break;
	default:
		return -EINVAL;
	}
	amdtp_stream_pcm_trigger(&oxfw->rx_stream, pcm);
	return 0;
}

static snd_pcm_uframes_t oxfw_pointer(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	return amdtp_stream_pcm_pointer(&oxfw->rx_stream);
}

static int oxfw_create_pcm(struct snd_oxfw *oxfw)
{
	static struct snd_pcm_ops ops = {
		.open      = oxfw_open,
		.close     = oxfw_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = oxfw_hw_params,
		.hw_free   = oxfw_hw_free,
		.prepare   = oxfw_prepare,
		.trigger   = oxfw_trigger,
		.pointer   = oxfw_pointer,
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(oxfw->card, "OXFW", 0, 1, 0, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = oxfw;
	strcpy(pcm->name, oxfw->device_info->short_name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &ops);
	return 0;
}

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

	amdtp_stream_destroy(&oxfw->rx_stream);
	cmp_connection_destroy(&oxfw->in_conn);
	fw_unit_put(oxfw->unit);
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

	oxfw = card->private_data;
	oxfw->card = card;
	mutex_init(&oxfw->mutex);
	oxfw->unit = fw_unit_get(unit);
	oxfw->device_info = (const struct device_info *)id->driver_data;

	err = cmp_connection_init(&oxfw->in_conn, unit, CMP_INPUT, 0);
	if (err < 0)
		goto err_unit;

	err = amdtp_stream_init(&oxfw->rx_stream, unit, AMDTP_OUT_STREAM,
				CIP_NONBLOCKING);
	if (err < 0)
		goto err_connection;

	card->private_free = oxfw_card_free;

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

	err = oxfw_create_pcm(oxfw);
	if (err < 0)
		goto error;

	err = oxfw_create_mixer(oxfw);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, oxfw);

	return 0;

err_connection:
	cmp_connection_destroy(&oxfw->in_conn);
err_unit:
	fw_unit_put(oxfw->unit);
	mutex_destroy(&oxfw->mutex);
error:
	snd_card_free(card);
	return err;
}

static void oxfw_bus_reset(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	fcp_bus_reset(oxfw->unit);

	if (cmp_connection_update(&oxfw->in_conn) < 0) {
		amdtp_stream_pcm_abort(&oxfw->rx_stream);
		mutex_lock(&oxfw->mutex);
		oxfw_stop_stream(oxfw);
		mutex_unlock(&oxfw->mutex);
		return;
	}

	amdtp_stream_update(&oxfw->rx_stream);
}

static void oxfw_remove(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	amdtp_stream_pcm_abort(&oxfw->rx_stream);
	snd_card_disconnect(oxfw->card);

	mutex_lock(&oxfw->mutex);
	oxfw_stop_stream(oxfw);
	mutex_unlock(&oxfw->mutex);

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
