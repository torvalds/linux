// SPDX-License-Identifier: GPL-2.0-only
/*
 * fireworks.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013-2014 Takashi Sakamoto
 */

/*
 * Fireworks is a board module which Echo Audio produced. This module consists
 * of three chipsets:
 *  - Communication chipset for IEEE1394 PHY/Link and IEC 61883-1/6
 *  - DSP or/and FPGA for signal processing
 *  - Flash Memory to store firmwares
 */

#include "fireworks.h"

MODULE_DESCRIPTION("Echo Fireworks driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;
unsigned int snd_efw_resp_buf_size	= 1024;
bool snd_efw_resp_buf_debug		= false;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable Fireworks sound card");
module_param_named(resp_buf_size, snd_efw_resp_buf_size, uint, 0444);
MODULE_PARM_DESC(resp_buf_size,
		 "response buffer size (max 4096, default 1024)");
module_param_named(resp_buf_debug, snd_efw_resp_buf_debug, bool, 0444);
MODULE_PARM_DESC(resp_buf_debug, "store all responses to buffer");

static DEFINE_MUTEX(devices_mutex);
static DECLARE_BITMAP(devices_used, SNDRV_CARDS);

#define VENDOR_LOUD			0x000ff2
#define  MODEL_MACKIE_400F		0x00400f
#define  MODEL_MACKIE_1200F		0x01200f

#define VENDOR_ECHO			0x001486
#define  MODEL_ECHO_AUDIOFIRE_12	0x00af12
#define  MODEL_ECHO_AUDIOFIRE_12HD	0x0af12d
#define  MODEL_ECHO_AUDIOFIRE_12_APPLE	0x0af12a
/* This is applied for AudioFire8 (until 2009 July) */
#define  MODEL_ECHO_AUDIOFIRE_8		0x000af8
#define  MODEL_ECHO_AUDIOFIRE_2		0x000af2
#define  MODEL_ECHO_AUDIOFIRE_4		0x000af4
/* AudioFire9 is applied for AudioFire8(since 2009 July) and AudioFirePre8 */
#define  MODEL_ECHO_AUDIOFIRE_9		0x000af9
/* unknown as product */
#define  MODEL_ECHO_FIREWORKS_8		0x0000f8
#define  MODEL_ECHO_FIREWORKS_HDMI	0x00afd1

#define VENDOR_GIBSON			0x00075b
/* for Robot Interface Pack of Dark Fire, Dusk Tiger, Les Paul Standard 2010 */
#define  MODEL_GIBSON_RIP		0x00afb2
/* unknown as product */
#define  MODEL_GIBSON_GOLDTOP		0x00afb9

/* part of hardware capability flags */
#define FLAG_RESP_ADDR_CHANGABLE	0

static int
get_hardware_info(struct snd_efw *efw)
{
	struct fw_device *fw_dev = fw_parent_device(efw->unit);
	struct snd_efw_hwinfo *hwinfo;
	char version[12] = {0};
	int err;

	hwinfo = kzalloc(sizeof(struct snd_efw_hwinfo), GFP_KERNEL);
	if (hwinfo == NULL)
		return -ENOMEM;

	err = snd_efw_command_get_hwinfo(efw, hwinfo);
	if (err < 0)
		goto end;

	/* firmware version for communication chipset */
	snprintf(version, sizeof(version), "%u.%u",
		 (hwinfo->arm_version >> 24) & 0xff,
		 (hwinfo->arm_version >> 16) & 0xff);
	efw->firmware_version = hwinfo->arm_version;

	strscpy(efw->card->driver, "Fireworks");
	strscpy(efw->card->shortname, hwinfo->model_name);
	strscpy(efw->card->mixername, hwinfo->model_name);
	scnprintf(efw->card->longname, sizeof(efw->card->longname),
		  "%s %s v%s, GUID %08x%08x at %s, S%d",
		  hwinfo->vendor_name, hwinfo->model_name, version,
		  hwinfo->guid_hi, hwinfo->guid_lo,
		  dev_name(&efw->unit->device), 100 << fw_dev->max_speed);

	if (hwinfo->flags & BIT(FLAG_RESP_ADDR_CHANGABLE))
		efw->resp_addr_changable = true;

	efw->supported_sampling_rate = 0;
	if ((hwinfo->min_sample_rate <= 22050)
	 && (22050 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_22050;
	if ((hwinfo->min_sample_rate <= 32000)
	 && (32000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_32000;
	if ((hwinfo->min_sample_rate <= 44100)
	 && (44100 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_44100;
	if ((hwinfo->min_sample_rate <= 48000)
	 && (48000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_48000;
	if ((hwinfo->min_sample_rate <= 88200)
	 && (88200 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_88200;
	if ((hwinfo->min_sample_rate <= 96000)
	 && (96000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_96000;
	if ((hwinfo->min_sample_rate <= 176400)
	 && (176400 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_176400;
	if ((hwinfo->min_sample_rate <= 192000)
	 && (192000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_192000;

	/* the number of MIDI ports, not of MIDI conformant data channels */
	if (hwinfo->midi_out_ports > SND_EFW_MAX_MIDI_OUT_PORTS ||
	    hwinfo->midi_in_ports > SND_EFW_MAX_MIDI_IN_PORTS) {
		err = -EIO;
		goto end;
	}
	efw->midi_out_ports = hwinfo->midi_out_ports;
	efw->midi_in_ports = hwinfo->midi_in_ports;

	if (hwinfo->amdtp_tx_pcm_channels    > AM824_MAX_CHANNELS_FOR_PCM ||
	    hwinfo->amdtp_tx_pcm_channels_2x > AM824_MAX_CHANNELS_FOR_PCM ||
	    hwinfo->amdtp_tx_pcm_channels_4x > AM824_MAX_CHANNELS_FOR_PCM ||
	    hwinfo->amdtp_rx_pcm_channels    > AM824_MAX_CHANNELS_FOR_PCM ||
	    hwinfo->amdtp_rx_pcm_channels_2x > AM824_MAX_CHANNELS_FOR_PCM ||
	    hwinfo->amdtp_rx_pcm_channels_4x > AM824_MAX_CHANNELS_FOR_PCM) {
		err = -ENOSYS;
		goto end;
	}
	efw->pcm_capture_channels[0] = hwinfo->amdtp_tx_pcm_channels;
	efw->pcm_capture_channels[1] = hwinfo->amdtp_tx_pcm_channels_2x;
	efw->pcm_capture_channels[2] = hwinfo->amdtp_tx_pcm_channels_4x;
	efw->pcm_playback_channels[0] = hwinfo->amdtp_rx_pcm_channels;
	efw->pcm_playback_channels[1] = hwinfo->amdtp_rx_pcm_channels_2x;
	efw->pcm_playback_channels[2] = hwinfo->amdtp_rx_pcm_channels_4x;

	/* Hardware metering. */
	if (hwinfo->phys_in_grp_count  > HWINFO_MAX_CAPS_GROUPS ||
	    hwinfo->phys_out_grp_count > HWINFO_MAX_CAPS_GROUPS) {
		err = -EIO;
		goto end;
	}
	efw->phys_in = hwinfo->phys_in;
	efw->phys_out = hwinfo->phys_out;
	efw->phys_in_grp_count = hwinfo->phys_in_grp_count;
	efw->phys_out_grp_count = hwinfo->phys_out_grp_count;
	memcpy(&efw->phys_in_grps, hwinfo->phys_in_grps,
	       sizeof(struct snd_efw_phys_grp) * hwinfo->phys_in_grp_count);
	memcpy(&efw->phys_out_grps, hwinfo->phys_out_grps,
	       sizeof(struct snd_efw_phys_grp) * hwinfo->phys_out_grp_count);

	/* AudioFire8 (since 2009) and AudioFirePre8 */
	if (hwinfo->type == MODEL_ECHO_AUDIOFIRE_9)
		efw->is_af9 = true;
	/* These models uses the same firmware. */
	if (hwinfo->type == MODEL_ECHO_AUDIOFIRE_2 ||
	    hwinfo->type == MODEL_ECHO_AUDIOFIRE_4 ||
	    hwinfo->type == MODEL_ECHO_AUDIOFIRE_9 ||
	    hwinfo->type == MODEL_GIBSON_RIP ||
	    hwinfo->type == MODEL_GIBSON_GOLDTOP)
		efw->is_fireworks3 = true;
end:
	kfree(hwinfo);
	return err;
}

static void
efw_card_free(struct snd_card *card)
{
	struct snd_efw *efw = card->private_data;

	scoped_guard(mutex, &devices_mutex) {
		clear_bit(efw->card_index, devices_used);
	}

	snd_efw_stream_destroy_duplex(efw);
	snd_efw_transaction_remove_instance(efw);

	mutex_destroy(&efw->mutex);
	fw_unit_put(efw->unit);
}

static int efw_probe(struct fw_unit *unit, const struct ieee1394_device_id *entry)
{
	unsigned int card_index;
	struct snd_card *card;
	struct snd_efw *efw;
	int err;

	// check registered cards.
	scoped_guard(mutex, &devices_mutex) {
		for (card_index = 0; card_index < SNDRV_CARDS; ++card_index) {
			if (!test_bit(card_index, devices_used) && enable[card_index])
				break;
		}
		if (card_index >= SNDRV_CARDS)
			return -ENOENT;

		err = snd_card_new(&unit->device, index[card_index], id[card_index], THIS_MODULE,
				   sizeof(*efw), &card);
		if (err < 0)
			return err;
		card->private_free = efw_card_free;
		set_bit(card_index, devices_used);
	}

	efw = card->private_data;
	efw->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, efw);
	efw->card = card;
	efw->card_index = card_index;

	mutex_init(&efw->mutex);
	spin_lock_init(&efw->lock);
	init_waitqueue_head(&efw->hwdep_wait);

	// prepare response buffer.
	snd_efw_resp_buf_size = clamp(snd_efw_resp_buf_size, SND_EFW_RESPONSE_MAXIMUM_BYTES, 4096U);
	efw->resp_buf = devm_kzalloc(&card->card_dev, snd_efw_resp_buf_size, GFP_KERNEL);
	if (!efw->resp_buf) {
		err = -ENOMEM;
		goto error;
	}
	efw->pull_ptr = efw->push_ptr = efw->resp_buf;
	snd_efw_transaction_add_instance(efw);

	err = get_hardware_info(efw);
	if (err < 0)
		goto error;

	err = snd_efw_stream_init_duplex(efw);
	if (err < 0)
		goto error;

	snd_efw_proc_init(efw);

	if (efw->midi_out_ports || efw->midi_in_ports) {
		err = snd_efw_create_midi_devices(efw);
		if (err < 0)
			goto error;
	}

	err = snd_efw_create_pcm_devices(efw);
	if (err < 0)
		goto error;

	err = snd_efw_create_hwdep_device(efw);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	return 0;
error:
	snd_card_free(card);
	return err;
}

static void efw_update(struct fw_unit *unit)
{
	struct snd_efw *efw = dev_get_drvdata(&unit->device);

	snd_efw_transaction_bus_reset(efw->unit);

	guard(mutex)(&efw->mutex);
	snd_efw_stream_update_duplex(efw);
}

static void efw_remove(struct fw_unit *unit)
{
	struct snd_efw *efw = dev_get_drvdata(&unit->device);

	// Block till all of ALSA character devices are released.
	snd_card_free(efw->card);
}

#define SPECIFIER_1394TA	0x00a02d
#define VERSION_EFW		0x010000

#define SND_EFW_DEV_ENTRY(vendor, model) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID | \
			  IEEE1394_MATCH_SPECIFIER_ID | \
			  IEEE1394_MATCH_VERSION, \
	.vendor_id	= vendor,\
	.model_id	= model, \
	.specifier_id	= SPECIFIER_1394TA, \
	.version	= VERSION_EFW, \
}

static const struct ieee1394_device_id efw_id_table[] = {
	SND_EFW_DEV_ENTRY(VENDOR_LOUD, MODEL_MACKIE_400F),
	SND_EFW_DEV_ENTRY(VENDOR_LOUD, MODEL_MACKIE_1200F),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_8),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12HD),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12_APPLE),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_2),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_4),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_9),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_FIREWORKS_8),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_FIREWORKS_HDMI),
	SND_EFW_DEV_ENTRY(VENDOR_GIBSON, MODEL_GIBSON_RIP),
	SND_EFW_DEV_ENTRY(VENDOR_GIBSON, MODEL_GIBSON_GOLDTOP),
	{}
};
MODULE_DEVICE_TABLE(ieee1394, efw_id_table);

static struct fw_driver efw_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = KBUILD_MODNAME,
		.bus = &fw_bus_type,
	},
	.probe    = efw_probe,
	.update   = efw_update,
	.remove   = efw_remove,
	.id_table = efw_id_table,
};

static int __init snd_efw_init(void)
{
	int err;

	err = snd_efw_transaction_register();
	if (err < 0)
		goto end;

	err = driver_register(&efw_driver.driver);
	if (err < 0)
		snd_efw_transaction_unregister();

end:
	return err;
}

static void __exit snd_efw_exit(void)
{
	snd_efw_transaction_unregister();
	driver_unregister(&efw_driver.driver);
}

module_init(snd_efw_init);
module_exit(snd_efw_exit);
