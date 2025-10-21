// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *   Audio Class 3.0 support by Ruslan Bilovol <ruslan.bilovol@gmail.com>
 *
 *  NOTES:
 *
 *   - the linked URBs would be preferred but not used so far because of
 *     the instability of unlinking.
 *   - type II is not supported properly.  there is no device which supports
 *     this type *correctly*.  SB extigy looks as if it supports, but it's
 *     indeed an AC3 stream packed in SPDIF frames (i.e. no real AC3 stream).
 */


#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>
#include <linux/module.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>

#include "usbaudio.h"
#include "card.h"
#include "midi.h"
#include "midi2.h"
#include "mixer.h"
#include "proc.h"
#include "quirks.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"
#include "format.h"
#include "power.h"
#include "stream.h"
#include "media.h"

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("USB Audio");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */
/* Vendor/product IDs for this card */
static int vid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 };
static int pid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 };
static int device_setup[SNDRV_CARDS]; /* device parameter for this card */
static bool ignore_ctl_error;
static bool autoclock = true;
static bool lowlatency = true;
static char *quirk_alias[SNDRV_CARDS];
static char *delayed_register[SNDRV_CARDS];
static bool implicit_fb[SNDRV_CARDS];
static char *quirk_flags[SNDRV_CARDS];

bool snd_usb_use_vmalloc = true;
bool snd_usb_skip_validation;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the USB audio adapter.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the USB audio adapter.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable USB audio adapter.");
module_param_array(vid, int, NULL, 0444);
MODULE_PARM_DESC(vid, "Vendor ID for the USB audio device.");
module_param_array(pid, int, NULL, 0444);
MODULE_PARM_DESC(pid, "Product ID for the USB audio device.");
module_param_array(device_setup, int, NULL, 0444);
MODULE_PARM_DESC(device_setup, "Specific device setup (if needed).");
module_param(ignore_ctl_error, bool, 0444);
MODULE_PARM_DESC(ignore_ctl_error,
		 "Ignore errors from USB controller for mixer interfaces.");
module_param(autoclock, bool, 0444);
MODULE_PARM_DESC(autoclock, "Enable auto-clock selection for UAC2 devices (default: yes).");
module_param(lowlatency, bool, 0444);
MODULE_PARM_DESC(lowlatency, "Enable low latency playback (default: yes).");
module_param_array(quirk_alias, charp, NULL, 0444);
MODULE_PARM_DESC(quirk_alias, "Quirk aliases, e.g. 0123abcd:5678beef.");
module_param_array(delayed_register, charp, NULL, 0444);
MODULE_PARM_DESC(delayed_register, "Quirk for delayed registration, given by id:iface, e.g. 0123abcd:4.");
module_param_array(implicit_fb, bool, NULL, 0444);
MODULE_PARM_DESC(implicit_fb, "Apply generic implicit feedback sync mode.");
module_param_named(use_vmalloc, snd_usb_use_vmalloc, bool, 0444);
MODULE_PARM_DESC(use_vmalloc, "Use vmalloc for PCM intermediate buffers (default: yes).");
module_param_named(skip_validation, snd_usb_skip_validation, bool, 0444);
MODULE_PARM_DESC(skip_validation, "Skip unit descriptor validation (default: no).");

/* protects quirk_flags */
static DEFINE_MUTEX(quirk_flags_mutex);

static int param_set_quirkp(const char *val,
			    const struct kernel_param *kp)
{
	guard(mutex)(&quirk_flags_mutex);
	return param_set_charp(val, kp);
}

static const struct kernel_param_ops param_ops_quirkp = {
	.set = param_set_quirkp,
	.get = param_get_charp,
	.free = param_free_charp,
};

#define param_check_quirkp param_check_charp

module_param_array(quirk_flags, quirkp, NULL, 0644);
MODULE_PARM_DESC(quirk_flags, "Add/modify USB audio quirks");

/*
 * we keep the snd_usb_audio_t instances by ourselves for merging
 * the all interfaces on the same card as one sound device.
 */

static DEFINE_MUTEX(register_mutex);
static struct snd_usb_audio *usb_chip[SNDRV_CARDS];
static struct usb_driver usb_audio_driver;
static struct snd_usb_platform_ops *platform_ops;

/*
 * Register platform specific operations that will be notified on events
 * which occur in USB SND.  The platform driver can utilize this path to
 * enable features, such as USB audio offloading, which allows for audio data
 * to be queued by an audio DSP.
 *
 * Only one set of platform operations can be registered to USB SND.  The
 * platform register operation is protected by the register_mutex.
 */
int snd_usb_register_platform_ops(struct snd_usb_platform_ops *ops)
{
	guard(mutex)(&register_mutex);
	if (platform_ops)
		return -EEXIST;

	platform_ops = ops;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_usb_register_platform_ops);

/*
 * Unregisters the current set of platform operations.  This allows for
 * a new set to be registered if required.
 *
 * The platform unregister operation is protected by the register_mutex.
 */
int snd_usb_unregister_platform_ops(void)
{
	guard(mutex)(&register_mutex);
	platform_ops = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_usb_unregister_platform_ops);

/*
 * in case the platform driver was not ready at the time of USB SND
 * device connect, expose an API to discover all connected USB devices
 * so it can populate any dependent resources/structures.
 */
void snd_usb_rediscover_devices(void)
{
	int i;

	guard(mutex)(&register_mutex);

	if (!platform_ops || !platform_ops->connect_cb)
		return;

	for (i = 0; i < SNDRV_CARDS; i++) {
		if (usb_chip[i])
			platform_ops->connect_cb(usb_chip[i]);
	}
}
EXPORT_SYMBOL_GPL(snd_usb_rediscover_devices);

/*
 * Checks to see if requested audio profile, i.e sample rate, # of
 * channels, etc... is supported by the substream associated to the
 * USB audio device.
 */
struct snd_usb_stream *
snd_usb_find_suppported_substream(int card_idx, struct snd_pcm_hw_params *params,
				  int direction)
{
	struct snd_usb_audio *chip;
	struct snd_usb_substream *subs;
	struct snd_usb_stream *as;

	/*
	 * Register mutex is held when populating and clearing usb_chip
	 * array.
	 */
	guard(mutex)(&register_mutex);
	chip = usb_chip[card_idx];

	if (chip && enable[card_idx]) {
		list_for_each_entry(as, &chip->pcm_list, list) {
			subs = &as->substream[direction];
			if (snd_usb_find_substream_format(subs, params))
				return as;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_usb_find_suppported_substream);

/*
 * disconnect streams
 * called from usb_audio_disconnect()
 */
static void snd_usb_stream_disconnect(struct snd_usb_stream *as)
{
	int idx;
	struct snd_usb_substream *subs;

	for (idx = 0; idx < 2; idx++) {
		subs = &as->substream[idx];
		if (!subs->num_formats)
			continue;
		subs->data_endpoint = NULL;
		subs->sync_endpoint = NULL;
	}
}

static int snd_usb_create_stream(struct snd_usb_audio *chip, int ctrlif, int interface)
{
	struct usb_device *dev = chip->dev;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct usb_interface *iface = usb_ifnum_to_if(dev, interface);

	if (!iface) {
		dev_err(&dev->dev, "%u:%d : does not exist\n",
			ctrlif, interface);
		return -EINVAL;
	}

	alts = &iface->altsetting[0];
	altsd = get_iface_desc(alts);

	/*
	 * Android with both accessory and audio interfaces enabled gets the
	 * interface numbers wrong.
	 */
	if ((chip->usb_id == USB_ID(0x18d1, 0x2d04) ||
	     chip->usb_id == USB_ID(0x18d1, 0x2d05)) &&
	    interface == 0 &&
	    altsd->bInterfaceClass == USB_CLASS_VENDOR_SPEC &&
	    altsd->bInterfaceSubClass == USB_SUBCLASS_VENDOR_SPEC) {
		interface = 2;
		iface = usb_ifnum_to_if(dev, interface);
		if (!iface)
			return -EINVAL;
		alts = &iface->altsetting[0];
		altsd = get_iface_desc(alts);
	}

	if (usb_interface_claimed(iface)) {
		dev_dbg(&dev->dev, "%d:%d: skipping, already claimed\n",
			ctrlif, interface);
		return -EINVAL;
	}

	if ((altsd->bInterfaceClass == USB_CLASS_AUDIO ||
	     altsd->bInterfaceClass == USB_CLASS_VENDOR_SPEC) &&
	    altsd->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAMING) {
		int err = snd_usb_midi_v2_create(chip, iface, NULL,
						 chip->usb_id);
		if (err < 0) {
			dev_err(&dev->dev,
				"%u:%d: cannot create sequencer device\n",
				ctrlif, interface);
			return -EINVAL;
		}
		return usb_driver_claim_interface(&usb_audio_driver, iface,
						  USB_AUDIO_IFACE_UNUSED);
	}

	if ((altsd->bInterfaceClass != USB_CLASS_AUDIO &&
	     altsd->bInterfaceClass != USB_CLASS_VENDOR_SPEC) ||
	    altsd->bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING) {
		dev_dbg(&dev->dev,
			"%u:%d: skipping non-supported interface %d\n",
			ctrlif, interface, altsd->bInterfaceClass);
		/* skip non-supported classes */
		return -EINVAL;
	}

	if (snd_usb_get_speed(dev) == USB_SPEED_LOW) {
		dev_err(&dev->dev, "low speed audio streaming not supported\n");
		return -EINVAL;
	}

	snd_usb_add_ctrl_interface_link(chip, interface, ctrlif);

	if (! snd_usb_parse_audio_interface(chip, interface)) {
		usb_set_interface(dev, interface, 0); /* reset the current interface */
		return usb_driver_claim_interface(&usb_audio_driver, iface,
						  USB_AUDIO_IFACE_UNUSED);
	}

	return 0;
}

/*
 * parse audio control descriptor and create pcm/midi streams
 */
static int snd_usb_create_streams(struct snd_usb_audio *chip, int ctrlif)
{
	struct usb_device *dev = chip->dev;
	struct usb_host_interface *host_iface;
	struct usb_interface_descriptor *altsd;
	int i, protocol;

	/* find audiocontrol interface */
	host_iface = &usb_ifnum_to_if(dev, ctrlif)->altsetting[0];
	altsd = get_iface_desc(host_iface);
	protocol = altsd->bInterfaceProtocol;

	switch (protocol) {
	default:
		dev_warn(&dev->dev,
			 "unknown interface protocol %#02x, assuming v1\n",
			 protocol);
		fallthrough;

	case UAC_VERSION_1: {
		struct uac1_ac_header_descriptor *h1;
		int rest_bytes;

		h1 = snd_usb_find_csint_desc(host_iface->extra,
							 host_iface->extralen,
							 NULL, UAC_HEADER);
		if (!h1 || h1->bLength < sizeof(*h1)) {
			dev_err(&dev->dev, "cannot find UAC_HEADER\n");
			return -EINVAL;
		}

		rest_bytes = (void *)(host_iface->extra +
				host_iface->extralen) - (void *)h1;

		/* just to be sure -- this shouldn't hit at all */
		if (rest_bytes <= 0) {
			dev_err(&dev->dev, "invalid control header\n");
			return -EINVAL;
		}

		if (rest_bytes < sizeof(*h1)) {
			dev_err(&dev->dev, "too short v1 buffer descriptor\n");
			return -EINVAL;
		}

		if (!h1->bInCollection) {
			dev_info(&dev->dev, "skipping empty audio interface (v1)\n");
			return -EINVAL;
		}

		if (rest_bytes < h1->bLength) {
			dev_err(&dev->dev, "invalid buffer length (v1)\n");
			return -EINVAL;
		}

		if (h1->bLength < sizeof(*h1) + h1->bInCollection) {
			dev_err(&dev->dev, "invalid UAC_HEADER (v1)\n");
			return -EINVAL;
		}

		for (i = 0; i < h1->bInCollection; i++)
			snd_usb_create_stream(chip, ctrlif, h1->baInterfaceNr[i]);

		break;
	}

	case UAC_VERSION_2:
	case UAC_VERSION_3: {
		struct usb_interface_assoc_descriptor *assoc =
			usb_ifnum_to_if(dev, ctrlif)->intf_assoc;

		if (!assoc) {
			/*
			 * Firmware writers cannot count to three.  So to find
			 * the IAD on the NuForce UDH-100, also check the next
			 * interface.
			 */
			struct usb_interface *iface =
				usb_ifnum_to_if(dev, ctrlif + 1);
			if (iface &&
			    iface->intf_assoc &&
			    iface->intf_assoc->bFunctionClass == USB_CLASS_AUDIO &&
			    iface->intf_assoc->bFunctionProtocol == UAC_VERSION_2)
				assoc = iface->intf_assoc;
		}

		if (!assoc) {
			dev_err(&dev->dev, "Audio class v2/v3 interfaces need an interface association\n");
			return -EINVAL;
		}

		if (protocol == UAC_VERSION_3) {
			int badd = assoc->bFunctionSubClass;

			if (badd != UAC3_FUNCTION_SUBCLASS_FULL_ADC_3_0 &&
			    (badd < UAC3_FUNCTION_SUBCLASS_GENERIC_IO ||
			     badd > UAC3_FUNCTION_SUBCLASS_SPEAKERPHONE)) {
				dev_err(&dev->dev,
					"Unsupported UAC3 BADD profile\n");
				return -EINVAL;
			}

			chip->badd_profile = badd;
		}

		for (i = 0; i < assoc->bInterfaceCount; i++) {
			int intf = assoc->bFirstInterface + i;

			if (intf != ctrlif)
				snd_usb_create_stream(chip, ctrlif, intf);
		}

		break;
	}
	}

	return 0;
}

/*
 * Profile name preset table
 */
struct usb_audio_device_name {
	u32 id;
	const char *vendor_name;
	const char *product_name;
	const char *profile_name;	/* override card->longname */
};

#define PROFILE_NAME(vid, pid, vendor, product, profile)	 \
	{ .id = USB_ID(vid, pid), .vendor_name = (vendor),	 \
	  .product_name = (product), .profile_name = (profile) }
#define DEVICE_NAME(vid, pid, vendor, product) \
	PROFILE_NAME(vid, pid, vendor, product, NULL)

/* vendor/product and profile name presets, sorted in device id order */
static const struct usb_audio_device_name usb_audio_names[] = {
	/* HP Thunderbolt Dock Audio Headset */
	PROFILE_NAME(0x03f0, 0x0269, "HP", "Thunderbolt Dock Audio Headset",
		     "HP-Thunderbolt-Dock-Audio-Headset"),
	/* HP Thunderbolt Dock Audio Module */
	PROFILE_NAME(0x03f0, 0x0567, "HP", "Thunderbolt Dock Audio Module",
		     "HP-Thunderbolt-Dock-Audio-Module"),

	/* Two entries for Gigabyte TRX40 Aorus Master:
	 * TRX40 Aorus Master has two USB-audio devices, one for the front
	 * headphone with ESS SABRE9218 DAC chip, while another for the rest
	 * I/O (the rear panel and the front mic) with Realtek ALC1220-VB.
	 * Here we provide two distinct names for making UCM profiles easier.
	 */
	PROFILE_NAME(0x0414, 0xa000, "Gigabyte", "Aorus Master Front Headphone",
		     "Gigabyte-Aorus-Master-Front-Headphone"),
	PROFILE_NAME(0x0414, 0xa001, "Gigabyte", "Aorus Master Main Audio",
		     "Gigabyte-Aorus-Master-Main-Audio"),

	/* Gigabyte TRX40 Aorus Pro WiFi */
	PROFILE_NAME(0x0414, 0xa002,
		     "Realtek", "ALC1220-VB-DT", "Realtek-ALC1220-VB-Desktop"),

	/* Creative/E-Mu devices */
	DEVICE_NAME(0x041e, 0x3010, "Creative Labs", "Sound Blaster MP3+"),
	/* Creative/Toshiba Multimedia Center SB-0500 */
	DEVICE_NAME(0x041e, 0x3048, "Toshiba", "SB-0500"),

	/* Logitech Audio Devices */
	DEVICE_NAME(0x046d, 0x0867, "Logitech, Inc.", "Logi-MeetUp"),
	DEVICE_NAME(0x046d, 0x0874, "Logitech, Inc.", "Logi-Tap-Audio"),
	DEVICE_NAME(0x046d, 0x087c, "Logitech, Inc.", "Logi-Huddle"),
	DEVICE_NAME(0x046d, 0x0898, "Logitech, Inc.", "Logi-RB-Audio"),
	DEVICE_NAME(0x046d, 0x08d2, "Logitech, Inc.", "Logi-RBM-Audio"),
	DEVICE_NAME(0x046d, 0x0990, "Logitech, Inc.", "QuickCam Pro 9000"),

	DEVICE_NAME(0x05e1, 0x0408, "Syntek", "STK1160"),
	DEVICE_NAME(0x05e1, 0x0480, "Hauppauge", "Woodbury"),

	/* ASUS ROG Zenith II: this machine has also two devices, one for
	 * the front headphone and another for the rest
	 */
	PROFILE_NAME(0x0b05, 0x1915, "ASUS", "Zenith II Front Headphone",
		     "Zenith-II-Front-Headphone"),
	PROFILE_NAME(0x0b05, 0x1916, "ASUS", "Zenith II Main Audio",
		     "Zenith-II-Main-Audio"),

	/* ASUS ROG Strix */
	PROFILE_NAME(0x0b05, 0x1917,
		     "Realtek", "ALC1220-VB-DT", "Realtek-ALC1220-VB-Desktop"),
	/* ASUS PRIME TRX40 PRO-S */
	PROFILE_NAME(0x0b05, 0x1918,
		     "Realtek", "ALC1220-VB-DT", "Realtek-ALC1220-VB-Desktop"),

	/* Dell WD15 Dock */
	PROFILE_NAME(0x0bda, 0x4014, "Dell", "WD15 Dock", "Dell-WD15-Dock"),
	/* Dell WD19 Dock */
	PROFILE_NAME(0x0bda, 0x402e, "Dell", "WD19 Dock", "Dell-WD15-Dock"),

	DEVICE_NAME(0x0ccd, 0x0028, "TerraTec", "Aureon5.1MkII"),

	/*
	 * The original product_name is "USB Sound Device", however this name
	 * is also used by the CM106 based cards, so make it unique.
	 */
	DEVICE_NAME(0x0d8c, 0x0102, NULL, "ICUSBAUDIO7D"),
	DEVICE_NAME(0x0d8c, 0x0103, NULL, "Audio Advantage MicroII"),

	/* MSI TRX40 Creator */
	PROFILE_NAME(0x0db0, 0x0d64,
		     "Realtek", "ALC1220-VB-DT", "Realtek-ALC1220-VB-Desktop"),
	/* MSI TRX40 */
	PROFILE_NAME(0x0db0, 0x543d,
		     "Realtek", "ALC1220-VB-DT", "Realtek-ALC1220-VB-Desktop"),

	DEVICE_NAME(0x0fd9, 0x0008, "Hauppauge", "HVR-950Q"),

	/* Dock/Stand for HP Engage Go */
	PROFILE_NAME(0x103c, 0x830a, "HP", "HP Engage Go Dock",
		     "HP-Engage-Go-Dock"),

	/* Stanton/N2IT Final Scratch v1 device ('Scratchamp') */
	DEVICE_NAME(0x103d, 0x0100, "Stanton", "ScratchAmp"),
	DEVICE_NAME(0x103d, 0x0101, "Stanton", "ScratchAmp"),

	/* aka. Serato Scratch Live DJ Box */
	DEVICE_NAME(0x13e5, 0x0001, "Rane", "SL-1"),

	/* Lenovo ThinkStation P620 Rear Line-in, Line-out and Microphone */
	PROFILE_NAME(0x17aa, 0x1046, "Lenovo", "ThinkStation P620 Rear",
		     "Lenovo-ThinkStation-P620-Rear"),
	/* Lenovo ThinkStation P620 Internal Speaker + Front Headset */
	PROFILE_NAME(0x17aa, 0x104d, "Lenovo", "ThinkStation P620 Main",
		     "Lenovo-ThinkStation-P620-Main"),

	/* Asrock TRX40 Creator */
	PROFILE_NAME(0x26ce, 0x0a01,
		     "Realtek", "ALC1220-VB-DT", "Realtek-ALC1220-VB-Desktop"),

	DEVICE_NAME(0x2040, 0x7200, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7201, "Hauppauge", "HVR-950Q-MXL"),
	DEVICE_NAME(0x2040, 0x7210, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7211, "Hauppauge", "HVR-950Q-MXL"),
	DEVICE_NAME(0x2040, 0x7213, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7217, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x721b, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x721e, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x721f, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7240, "Hauppauge", "HVR-850"),
	DEVICE_NAME(0x2040, 0x7260, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7270, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7280, "Hauppauge", "HVR-950Q"),
	DEVICE_NAME(0x2040, 0x7281, "Hauppauge", "HVR-950Q-MXL"),
	DEVICE_NAME(0x2040, 0x8200, "Hauppauge", "Woodbury"),

	{ } /* terminator */
};

static const struct usb_audio_device_name *
lookup_device_name(u32 id)
{
	static const struct usb_audio_device_name *p;

	for (p = usb_audio_names; p->id; p++)
		if (p->id == id)
			return p;
	return NULL;
}

/*
 * free the chip instance
 *
 * here we have to do not much, since pcm and controls are already freed
 *
 */

static void snd_usb_audio_free(struct snd_card *card)
{
	struct snd_usb_audio *chip = card->private_data;

	snd_usb_endpoint_free_all(chip);
	snd_usb_midi_v2_free_all(chip);

	mutex_destroy(&chip->mutex);
	if (!atomic_read(&chip->shutdown))
		dev_set_drvdata(&chip->dev->dev, NULL);
}

static void usb_audio_make_shortname(struct usb_device *dev,
				     struct snd_usb_audio *chip,
				     const struct snd_usb_audio_quirk *quirk)
{
	struct snd_card *card = chip->card;
	const struct usb_audio_device_name *preset;
	const char *s = NULL;

	preset = lookup_device_name(chip->usb_id);
	if (preset && preset->product_name)
		s = preset->product_name;
	else if (quirk && quirk->product_name)
		s = quirk->product_name;
	if (s && *s) {
		strscpy(card->shortname, s, sizeof(card->shortname));
		return;
	}

	/* retrieve the device string as shortname */
	if (!dev->descriptor.iProduct ||
	    usb_string(dev, dev->descriptor.iProduct,
		       card->shortname, sizeof(card->shortname)) <= 0) {
		/* no name available from anywhere, so use ID */
		scnprintf(card->shortname, sizeof(card->shortname),
			  "USB Device %#04x:%#04x",
			  USB_ID_VENDOR(chip->usb_id),
			  USB_ID_PRODUCT(chip->usb_id));
	}

	strim(card->shortname);
}

static void usb_audio_make_longname(struct usb_device *dev,
				    struct snd_usb_audio *chip,
				    const struct snd_usb_audio_quirk *quirk)
{
	struct snd_card *card = chip->card;
	const struct usb_audio_device_name *preset;
	const char *s = NULL;
	int len;

	preset = lookup_device_name(chip->usb_id);

	/* shortcut - if any pre-defined string is given, use it */
	if (preset && preset->profile_name)
		s = preset->profile_name;
	if (s && *s) {
		strscpy(card->longname, s, sizeof(card->longname));
		return;
	}

	if (preset && preset->vendor_name)
		s = preset->vendor_name;
	else if (quirk && quirk->vendor_name)
		s = quirk->vendor_name;
	*card->longname = 0;
	if (s && *s) {
		strscpy(card->longname, s, sizeof(card->longname));
	} else {
		/* retrieve the vendor and device strings as longname */
		if (dev->descriptor.iManufacturer)
			usb_string(dev, dev->descriptor.iManufacturer,
				   card->longname, sizeof(card->longname));
		/* we don't really care if there isn't any vendor string */
	}
	if (*card->longname) {
		strim(card->longname);
		if (*card->longname)
			strlcat(card->longname, " ", sizeof(card->longname));
	}

	strlcat(card->longname, card->shortname, sizeof(card->longname));

	len = strlcat(card->longname, " at ", sizeof(card->longname));

	if (len < sizeof(card->longname))
		usb_make_path(dev, card->longname + len, sizeof(card->longname) - len);

	switch (snd_usb_get_speed(dev)) {
	case USB_SPEED_LOW:
		strlcat(card->longname, ", low speed", sizeof(card->longname));
		break;
	case USB_SPEED_FULL:
		strlcat(card->longname, ", full speed", sizeof(card->longname));
		break;
	case USB_SPEED_HIGH:
		strlcat(card->longname, ", high speed", sizeof(card->longname));
		break;
	case USB_SPEED_SUPER:
		strlcat(card->longname, ", super speed", sizeof(card->longname));
		break;
	case USB_SPEED_SUPER_PLUS:
		strlcat(card->longname, ", super speed plus", sizeof(card->longname));
		break;
	default:
		break;
	}
}

static void snd_usb_init_quirk_flags(int idx, struct snd_usb_audio *chip)
{
	size_t i;

	guard(mutex)(&quirk_flags_mutex);

	/* old style option found: the position-based integer value */
	if (quirk_flags[idx] &&
	    !kstrtou32(quirk_flags[idx], 0, &chip->quirk_flags)) {
		snd_usb_apply_flag_dbg("module param", chip, chip->quirk_flags);
		return;
	}

	/* take the default quirk from the quirk table */
	snd_usb_init_quirk_flags_table(chip);

	/* add or correct quirk bits from options */
	for (i = 0; i < ARRAY_SIZE(quirk_flags); i++) {
		if (!quirk_flags[i] || !*quirk_flags[i])
			break;

		snd_usb_init_quirk_flags_parse_string(chip, quirk_flags[i]);
	}
}

/*
 * create a chip instance and set its names.
 */
static int snd_usb_audio_create(struct usb_interface *intf,
				struct usb_device *dev, int idx,
				const struct snd_usb_audio_quirk *quirk,
				unsigned int usb_id,
				struct snd_usb_audio **rchip)
{
	struct snd_card *card;
	struct snd_usb_audio *chip;
	int err;
	char component[14];

	*rchip = NULL;

	switch (snd_usb_get_speed(dev)) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		break;
	default:
		dev_err(&dev->dev, "unknown device speed %d\n", snd_usb_get_speed(dev));
		return -ENXIO;
	}

	err = snd_card_new(&intf->dev, index[idx], id[idx], THIS_MODULE,
			   sizeof(*chip), &card);
	if (err < 0) {
		dev_err(&dev->dev, "cannot create card instance %d\n", idx);
		return err;
	}

	chip = card->private_data;
	mutex_init(&chip->mutex);
	init_waitqueue_head(&chip->shutdown_wait);
	chip->index = idx;
	chip->dev = dev;
	chip->card = card;
	chip->setup = device_setup[idx];
	chip->generic_implicit_fb = implicit_fb[idx];
	chip->autoclock = autoclock;
	chip->lowlatency = lowlatency;
	atomic_set(&chip->active, 1); /* avoid autopm during probing */
	atomic_set(&chip->usage_count, 0);
	atomic_set(&chip->shutdown, 0);

	chip->usb_id = usb_id;
	INIT_LIST_HEAD(&chip->pcm_list);
	INIT_LIST_HEAD(&chip->ep_list);
	INIT_LIST_HEAD(&chip->iface_ref_list);
	INIT_LIST_HEAD(&chip->clock_ref_list);
	INIT_LIST_HEAD(&chip->midi_list);
	INIT_LIST_HEAD(&chip->midi_v2_list);
	INIT_LIST_HEAD(&chip->mixer_list);

	snd_usb_init_quirk_flags(idx, chip);

	card->private_free = snd_usb_audio_free;

	strscpy(card->driver, "USB-Audio");
	scnprintf(component, sizeof(component), "USB%04x:%04x",
		  USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	snd_component_add(card, component);

	usb_audio_make_shortname(dev, chip, quirk);
	usb_audio_make_longname(dev, chip, quirk);

	snd_usb_audio_create_proc(chip);

	*rchip = chip;
	return 0;
}

/* look for a matching quirk alias id */
static bool get_alias_id(struct usb_device *dev, unsigned int *id)
{
	int i;
	unsigned int src, dst;

	for (i = 0; i < ARRAY_SIZE(quirk_alias); i++) {
		if (!quirk_alias[i] ||
		    sscanf(quirk_alias[i], "%x:%x", &src, &dst) != 2 ||
		    src != *id)
			continue;
		dev_info(&dev->dev,
			 "device (%04x:%04x): applying quirk alias %04x:%04x\n",
			 USB_ID_VENDOR(*id), USB_ID_PRODUCT(*id),
			 USB_ID_VENDOR(dst), USB_ID_PRODUCT(dst));
		*id = dst;
		return true;
	}

	return false;
}

static int check_delayed_register_option(struct snd_usb_audio *chip)
{
	int i;
	unsigned int id, inum;

	for (i = 0; i < ARRAY_SIZE(delayed_register); i++) {
		if (delayed_register[i] &&
		    sscanf(delayed_register[i], "%x:%x", &id, &inum) == 2 &&
		    id == chip->usb_id)
			return inum;
	}

	return -1;
}

static const struct usb_device_id usb_audio_ids[]; /* defined below */

/* look for the last interface that matches with our ids and remember it */
static void find_last_interface(struct snd_usb_audio *chip)
{
	struct usb_host_config *config = chip->dev->actconfig;
	struct usb_interface *intf;
	int i;

	if (!config)
		return;
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		intf = config->interface[i];
		if (usb_match_id(intf, usb_audio_ids))
			chip->last_iface = intf->altsetting[0].desc.bInterfaceNumber;
	}
	usb_audio_dbg(chip, "Found last interface = %d\n", chip->last_iface);
}

/* look for the corresponding quirk */
static const struct snd_usb_audio_quirk *
get_alias_quirk(struct usb_device *dev, unsigned int id)
{
	const struct usb_device_id *p;

	for (p = usb_audio_ids; p->match_flags; p++) {
		/* FIXME: this checks only vendor:product pair in the list */
		if ((p->match_flags & USB_DEVICE_ID_MATCH_DEVICE) ==
		    USB_DEVICE_ID_MATCH_DEVICE &&
		    p->idVendor == USB_ID_VENDOR(id) &&
		    p->idProduct == USB_ID_PRODUCT(id))
			return (const struct snd_usb_audio_quirk *)p->driver_info;
	}

	return NULL;
}

/* register card if we reach to the last interface or to the specified
 * one given via option
 */
static int try_to_register_card(struct snd_usb_audio *chip, int ifnum)
{
	struct usb_interface *iface;

	if (check_delayed_register_option(chip) == ifnum ||
	    chip->last_iface == ifnum)
		return snd_card_register(chip->card);

	iface = usb_ifnum_to_if(chip->dev, chip->last_iface);
	if (iface && usb_interface_claimed(iface))
		return snd_card_register(chip->card);

	return 0;
}

/*
 * probe the active usb device
 *
 * note that this can be called multiple times per a device, when it
 * includes multiple audio control interfaces.
 *
 * thus we check the usb device pointer and creates the card instance
 * only at the first time.  the successive calls of this function will
 * append the pcm interface to the corresponding card.
 */
static int usb_audio_probe(struct usb_interface *intf,
			   const struct usb_device_id *usb_id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	const struct snd_usb_audio_quirk *quirk =
		(const struct snd_usb_audio_quirk *)usb_id->driver_info;
	struct snd_usb_audio *chip;
	int i, err;
	struct usb_host_interface *alts;
	int ifnum;
	u32 id;

	alts = &intf->altsetting[0];
	ifnum = get_iface_desc(alts)->bInterfaceNumber;
	id = USB_ID(le16_to_cpu(dev->descriptor.idVendor),
		    le16_to_cpu(dev->descriptor.idProduct));
	if (get_alias_id(dev, &id))
		quirk = get_alias_quirk(dev, id);
	if (quirk && quirk->ifnum >= 0 && ifnum != quirk->ifnum)
		return -ENXIO;
	if (quirk && quirk->ifnum == QUIRK_NODEV_INTERFACE)
		return -ENODEV;

	err = snd_usb_apply_boot_quirk(dev, intf, quirk, id);
	if (err < 0)
		return err;

	/*
	 * found a config.  now register to ALSA
	 */

	/* check whether it's already registered */
	chip = NULL;
	guard(mutex)(&register_mutex);
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (usb_chip[i] && usb_chip[i]->dev == dev) {
			if (atomic_read(&usb_chip[i]->shutdown)) {
				dev_err(&dev->dev, "USB device is in the shutdown state, cannot create a card instance\n");
				err = -EIO;
				goto __error;
			}
			chip = usb_chip[i];
			atomic_inc(&chip->active); /* avoid autopm */
			break;
		}
	}
	if (! chip) {
		err = snd_usb_apply_boot_quirk_once(dev, intf, quirk, id);
		if (err < 0)
			goto __error;

		/* it's a fresh one.
		 * now look for an empty slot and create a new card instance
		 */
		for (i = 0; i < SNDRV_CARDS; i++)
			if (!usb_chip[i] &&
			    (vid[i] == -1 || vid[i] == USB_ID_VENDOR(id)) &&
			    (pid[i] == -1 || pid[i] == USB_ID_PRODUCT(id))) {
				if (enable[i]) {
					err = snd_usb_audio_create(intf, dev, i, quirk,
								   id, &chip);
					if (err < 0)
						goto __error;
					break;
				} else if (vid[i] != -1 || pid[i] != -1) {
					dev_info(&dev->dev,
						 "device (%04x:%04x) is disabled\n",
						 USB_ID_VENDOR(id),
						 USB_ID_PRODUCT(id));
					err = -ENOENT;
					goto __error;
				}
			}
		if (!chip) {
			dev_err(&dev->dev, "no available usb audio device\n");
			err = -ENODEV;
			goto __error;
		}
		find_last_interface(chip);
	}

	if (chip->num_interfaces >= MAX_CARD_INTERFACES) {
		dev_info(&dev->dev, "Too many interfaces assigned to the single USB-audio card\n");
		err = -EINVAL;
		goto __error;
	}

	dev_set_drvdata(&dev->dev, chip);

	if (ignore_ctl_error)
		chip->quirk_flags |= QUIRK_FLAG_IGNORE_CTL_ERROR;

	if (chip->quirk_flags & QUIRK_FLAG_DISABLE_AUTOSUSPEND)
		usb_disable_autosuspend(interface_to_usbdev(intf));

	/*
	 * For devices with more than one control interface, we assume the
	 * first contains the audio controls. We might need a more specific
	 * check here in the future.
	 */
	if (!chip->ctrl_intf)
		chip->ctrl_intf = alts;

	err = 1; /* continue */
	if (quirk && quirk->ifnum != QUIRK_NO_INTERFACE) {
		/* need some special handlings */
		err = snd_usb_create_quirk(chip, intf, &usb_audio_driver, quirk);
		if (err < 0)
			goto __error;
	}

	if (err > 0) {
		/* create normal USB audio interfaces */
		err = snd_usb_create_streams(chip, ifnum);
		if (err < 0)
			goto __error;
		err = snd_usb_create_mixer(chip, ifnum);
		if (err < 0)
			goto __error;
	}

	if (chip->need_delayed_register) {
		dev_info(&dev->dev,
			 "Found post-registration device assignment: %08x:%02x\n",
			 chip->usb_id, ifnum);
		chip->need_delayed_register = false; /* clear again */
	}

	err = try_to_register_card(chip, ifnum);
	if (err < 0)
		goto __error_no_register;

	if (chip->quirk_flags & QUIRK_FLAG_SHARE_MEDIA_DEVICE) {
		/* don't want to fail when snd_media_device_create() fails */
		snd_media_device_create(chip, intf);
	}

	if (quirk)
		chip->quirk_type = quirk->type;

	usb_chip[chip->index] = chip;
	chip->intf[chip->num_interfaces] = intf;
	chip->num_interfaces++;
	usb_set_intfdata(intf, chip);
	atomic_dec(&chip->active);

	if (platform_ops && platform_ops->connect_cb)
		platform_ops->connect_cb(chip);

	return 0;

 __error:
	/* in the case of error in secondary interface, still try to register */
	if (chip)
		try_to_register_card(chip, ifnum);

 __error_no_register:
	if (chip) {
		/* chip->active is inside the chip->card object,
		 * decrement before memory is possibly returned.
		 */
		atomic_dec(&chip->active);
		if (!chip->num_interfaces)
			snd_card_free(chip->card);
	}
	return err;
}

/*
 * we need to take care of counter, since disconnection can be called also
 * many times as well as usb_audio_probe().
 */
static bool __usb_audio_disconnect(struct usb_interface *intf,
				   struct snd_usb_audio *chip,
				   struct snd_card *card)
{
	struct list_head *p;

	guard(mutex)(&register_mutex);

	if (platform_ops && platform_ops->disconnect_cb)
		platform_ops->disconnect_cb(chip);

	if (atomic_inc_return(&chip->shutdown) == 1) {
		struct snd_usb_stream *as;
		struct snd_usb_endpoint *ep;
		struct usb_mixer_interface *mixer;

		/* wait until all pending tasks done;
		 * they are protected by snd_usb_lock_shutdown()
		 */
		wait_event(chip->shutdown_wait,
			   !atomic_read(&chip->usage_count));
		snd_card_disconnect(card);
		/* release the pcm resources */
		list_for_each_entry(as, &chip->pcm_list, list) {
			snd_usb_stream_disconnect(as);
		}
		/* release the endpoint resources */
		list_for_each_entry(ep, &chip->ep_list, list) {
			snd_usb_endpoint_release(ep);
		}
		/* release the midi resources */
		list_for_each(p, &chip->midi_list) {
			snd_usbmidi_disconnect(p);
		}
		snd_usb_midi_v2_disconnect_all(chip);
		/*
		 * Nice to check quirk && quirk->shares_media_device and
		 * then call the snd_media_device_delete(). Don't have
		 * access to the quirk here. snd_media_device_delete()
		 * accesses mixer_list
		 */
		snd_media_device_delete(chip);

		/* release mixer resources */
		list_for_each_entry(mixer, &chip->mixer_list, list) {
			snd_usb_mixer_disconnect(mixer);
		}
	}

	if (chip->quirk_flags & QUIRK_FLAG_DISABLE_AUTOSUSPEND)
		usb_enable_autosuspend(interface_to_usbdev(intf));

	chip->num_interfaces--;
	if (chip->num_interfaces > 0)
		return false;

	usb_chip[chip->index] = NULL;
	return true;
}

static void usb_audio_disconnect(struct usb_interface *intf)
{
	struct snd_usb_audio *chip = usb_get_intfdata(intf);
	struct snd_card *card;

	if (chip == USB_AUDIO_IFACE_UNUSED)
		return;

	card = chip->card;
	if (__usb_audio_disconnect(intf, chip, card))
		snd_card_free_when_closed(card);
}

/* lock the shutdown (disconnect) task and autoresume */
int snd_usb_lock_shutdown(struct snd_usb_audio *chip)
{
	int err;

	atomic_inc(&chip->usage_count);
	if (atomic_read(&chip->shutdown)) {
		err = -EIO;
		goto error;
	}
	err = snd_usb_autoresume(chip);
	if (err < 0)
		goto error;
	return 0;

 error:
	if (atomic_dec_and_test(&chip->usage_count))
		wake_up(&chip->shutdown_wait);
	return err;
}
EXPORT_SYMBOL_GPL(snd_usb_lock_shutdown);

/* autosuspend and unlock the shutdown */
void snd_usb_unlock_shutdown(struct snd_usb_audio *chip)
{
	snd_usb_autosuspend(chip);
	if (atomic_dec_and_test(&chip->usage_count))
		wake_up(&chip->shutdown_wait);
}
EXPORT_SYMBOL_GPL(snd_usb_unlock_shutdown);

int snd_usb_autoresume(struct snd_usb_audio *chip)
{
	int i, err;

	if (atomic_read(&chip->shutdown))
		return -EIO;
	if (atomic_inc_return(&chip->active) != 1)
		return 0;

	for (i = 0; i < chip->num_interfaces; i++) {
		err = usb_autopm_get_interface(chip->intf[i]);
		if (err < 0) {
			/* rollback */
			while (--i >= 0)
				usb_autopm_put_interface(chip->intf[i]);
			atomic_dec(&chip->active);
			return err;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_usb_autoresume);

void snd_usb_autosuspend(struct snd_usb_audio *chip)
{
	int i;

	if (atomic_read(&chip->shutdown))
		return;
	if (!atomic_dec_and_test(&chip->active))
		return;

	for (i = 0; i < chip->num_interfaces; i++)
		usb_autopm_put_interface(chip->intf[i]);
}
EXPORT_SYMBOL_GPL(snd_usb_autosuspend);

static int usb_audio_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct snd_usb_audio *chip = usb_get_intfdata(intf);
	struct snd_usb_stream *as;
	struct snd_usb_endpoint *ep;
	struct usb_mixer_interface *mixer;
	struct list_head *p;

	if (chip == USB_AUDIO_IFACE_UNUSED)
		return 0;

	if (!chip->num_suspended_intf++) {
		list_for_each_entry(as, &chip->pcm_list, list)
			snd_usb_pcm_suspend(as);
		list_for_each_entry(ep, &chip->ep_list, list)
			snd_usb_endpoint_suspend(ep);
		list_for_each(p, &chip->midi_list)
			snd_usbmidi_suspend(p);
		list_for_each_entry(mixer, &chip->mixer_list, list)
			snd_usb_mixer_suspend(mixer);
		snd_usb_midi_v2_suspend_all(chip);
	}

	if (!PMSG_IS_AUTO(message) && !chip->system_suspend) {
		snd_power_change_state(chip->card, SNDRV_CTL_POWER_D3hot);
		chip->system_suspend = chip->num_suspended_intf;
	}

	if (platform_ops && platform_ops->suspend_cb)
		platform_ops->suspend_cb(intf, message);

	return 0;
}

static int usb_audio_resume(struct usb_interface *intf)
{
	struct snd_usb_audio *chip = usb_get_intfdata(intf);
	struct snd_usb_stream *as;
	struct usb_mixer_interface *mixer;
	struct list_head *p;
	int err = 0;

	if (chip == USB_AUDIO_IFACE_UNUSED)
		return 0;

	atomic_inc(&chip->active); /* avoid autopm */
	if (chip->num_suspended_intf > 1)
		goto out;

	list_for_each_entry(as, &chip->pcm_list, list) {
		err = snd_usb_pcm_resume(as);
		if (err < 0)
			goto err_out;
	}

	/*
	 * ALSA leaves material resumption to user space
	 * we just notify and restart the mixers
	 */
	list_for_each_entry(mixer, &chip->mixer_list, list) {
		err = snd_usb_mixer_resume(mixer);
		if (err < 0)
			goto err_out;
	}

	list_for_each(p, &chip->midi_list) {
		snd_usbmidi_resume(p);
	}

	snd_usb_midi_v2_resume_all(chip);

	if (platform_ops && platform_ops->resume_cb)
		platform_ops->resume_cb(intf);

 out:
	if (chip->num_suspended_intf == chip->system_suspend) {
		snd_power_change_state(chip->card, SNDRV_CTL_POWER_D0);
		chip->system_suspend = 0;
	}
	chip->num_suspended_intf--;

err_out:
	atomic_dec(&chip->active); /* allow autopm after this point */
	return err;
}

static const struct usb_device_id usb_audio_ids [] = {
#include "quirks-table.h"
    { .match_flags = (USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS),
      .bInterfaceClass = USB_CLASS_AUDIO,
      .bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL },
    { }						/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usb_audio_ids);

/*
 * entry point for linux usb interface
 */

static struct usb_driver usb_audio_driver = {
	.name =		"snd-usb-audio",
	.probe =	usb_audio_probe,
	.disconnect =	usb_audio_disconnect,
	.suspend =	usb_audio_suspend,
	.resume =	usb_audio_resume,
	.reset_resume =	usb_audio_resume,
	.id_table =	usb_audio_ids,
	.supports_autosuspend = 1,
};

module_usb_driver(usb_audio_driver);
