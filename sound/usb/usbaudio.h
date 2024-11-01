/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __USBAUDIO_H
#define __USBAUDIO_H
/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 */

/* handling of USB vendor/product ID pairs as 32-bit numbers */
#define USB_ID(vendor, product) (((unsigned int)(vendor) << 16) | (product))
#define USB_ID_VENDOR(id) ((id) >> 16)
#define USB_ID_PRODUCT(id) ((u16)(id))

#include <linux/android_kabi.h>

/*
 *
 */

struct media_device;
struct media_intf_devnode;
struct snd_usb_substream;

#define MAX_CARD_INTERFACES	16

struct snd_usb_audio {
	int index;
	struct usb_device *dev;
	struct snd_card *card;
	struct usb_interface *intf[MAX_CARD_INTERFACES];
	u32 usb_id;
	uint16_t quirk_type;
	struct mutex mutex;
	unsigned int system_suspend;
	atomic_t active;
	atomic_t shutdown;
	atomic_t usage_count;
	wait_queue_head_t shutdown_wait;
	unsigned int quirk_flags;
	unsigned int need_delayed_register:1; /* warn for delayed registration */
	int num_interfaces;
	int last_iface;
	int num_suspended_intf;
	int sample_rate_read_error;

	int badd_profile;		/* UAC3 BADD profile */

	struct list_head pcm_list;	/* list of pcm streams */
	struct list_head ep_list;	/* list of audio-related endpoints */
	struct list_head iface_ref_list; /* list of interface refcounts */
	struct list_head clock_ref_list; /* list of clock refcounts */
	int pcm_devs;

	struct list_head midi_list;	/* list of midi interfaces */

	struct list_head mixer_list;	/* list of mixer interfaces */

	int setup;			/* from the 'device_setup' module param */
	bool generic_implicit_fb;	/* from the 'implicit_fb' module param */
	bool autoclock;			/* from the 'autoclock' module param */

	bool lowlatency;		/* from the 'lowlatency' module param */
	struct usb_host_interface *ctrl_intf;	/* the audio control interface */
	struct media_device *media_dev;
	struct media_intf_devnode *ctl_intf_media_devnode;

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
};

#define USB_AUDIO_IFACE_UNUSED	((void *)-1L)

#define usb_audio_err(chip, fmt, args...) \
	dev_err(&(chip)->dev->dev, fmt, ##args)
#define usb_audio_warn(chip, fmt, args...) \
	dev_warn(&(chip)->dev->dev, fmt, ##args)
#define usb_audio_info(chip, fmt, args...) \
	dev_info(&(chip)->dev->dev, fmt, ##args)
#define usb_audio_dbg(chip, fmt, args...) \
	dev_dbg(&(chip)->dev->dev, fmt, ##args)

/*
 * Information about devices with broken descriptors
 */

/* special values for .ifnum */
#define QUIRK_NODEV_INTERFACE		-3	/* return -ENODEV */
#define QUIRK_NO_INTERFACE		-2
#define QUIRK_ANY_INTERFACE		-1

enum quirk_type {
	QUIRK_IGNORE_INTERFACE,
	QUIRK_COMPOSITE,
	QUIRK_AUTODETECT,
	QUIRK_MIDI_STANDARD_INTERFACE,
	QUIRK_MIDI_FIXED_ENDPOINT,
	QUIRK_MIDI_YAMAHA,
	QUIRK_MIDI_ROLAND,
	QUIRK_MIDI_MIDIMAN,
	QUIRK_MIDI_NOVATION,
	QUIRK_MIDI_RAW_BYTES,
	QUIRK_MIDI_EMAGIC,
	QUIRK_MIDI_CME,
	QUIRK_MIDI_AKAI,
	QUIRK_MIDI_US122L,
	QUIRK_MIDI_FTDI,
	QUIRK_MIDI_CH345,
	QUIRK_AUDIO_STANDARD_INTERFACE,
	QUIRK_AUDIO_FIXED_ENDPOINT,
	QUIRK_AUDIO_EDIROL_UAXX,
	QUIRK_AUDIO_STANDARD_MIXER,

	QUIRK_TYPE_COUNT
};

struct snd_usb_audio_quirk {
	const char *vendor_name;
	const char *product_name;
	int16_t ifnum;
	uint16_t type;
	const void *data;
};

#define combine_word(s)    ((*(s)) | ((unsigned int)(s)[1] << 8))
#define combine_triple(s)  (combine_word(s) | ((unsigned int)(s)[2] << 16))
#define combine_quad(s)    (combine_triple(s) | ((unsigned int)(s)[3] << 24))

int snd_usb_lock_shutdown(struct snd_usb_audio *chip);
void snd_usb_unlock_shutdown(struct snd_usb_audio *chip);

extern bool snd_usb_use_vmalloc;
extern bool snd_usb_skip_validation;

/*
 * Driver behavior quirk flags, stored in chip->quirk_flags
 *
 * QUIRK_FLAG_GET_SAMPLE_RATE:
 *  Skip reading sample rate for devices, as some devices behave inconsistently
 *  or return error
 * QUIRK_FLAG_SHARE_MEDIA_DEVICE:
 *  Create Media Controller API entries
 * QUIRK_FLAG_ALIGN_TRANSFER:
 *  Allow alignment on audio sub-slot (channel samples) rather than on audio
 *  slots (audio frames)
 * QUIRK_TX_LENGTH:
 *  Add length specifier to transfers
 * QUIRK_FLAG_PLAYBACK_FIRST:
 *  Start playback stream at first even in implement feedback mode
 * QUIRK_FLAG_SKIP_CLOCK_SELECTOR:
 *  Skip clock selector setup; the device may reset to invalid state
 * QUIRK_FLAG_IGNORE_CLOCK_SOURCE:
 *  Ignore errors from clock source search; i.e. hardcoded clock
 * QUIRK_FLAG_ITF_USB_DSD_DAC:
 *  Indicates the device is for ITF-USB DSD based DACs that need a vendor cmd
 *  to switch between PCM and native DSD mode
 * QUIRK_FLAG_CTL_MSG_DELAY:
 *  Add a delay of 20ms at each control message handling
 * QUIRK_FLAG_CTL_MSG_DELAY_1M:
 *  Add a delay of 1-2ms at each control message handling
 * QUIRK_FLAG_CTL_MSG_DELAY_5M:
 *  Add a delay of 5-6ms at each control message handling
 * QUIRK_FLAG_IFACE_DELAY:
 *  Add a delay of 50ms at each interface setup
 * QUIRK_FLAG_VALIDATE_RATES:
 *  Perform sample rate validations at probe
 * QUIRK_FLAG_DISABLE_AUTOSUSPEND:
 *  Disable runtime PM autosuspend
 * QUIRK_FLAG_IGNORE_CTL_ERROR:
 *  Ignore errors for mixer access
 * QUIRK_FLAG_DSD_RAW:
 *  Support generic DSD raw U32_BE format
 * QUIRK_FLAG_SET_IFACE_FIRST:
 *  Set up the interface at first like UAC1
 * QUIRK_FLAG_GENERIC_IMPLICIT_FB
 *  Apply the generic implicit feedback sync mode (same as implicit_fb=1 option)
 * QUIRK_FLAG_SKIP_IMPLICIT_FB
 *  Don't apply implicit feedback sync mode
 * QUIRK_FLAG_IFACE_SKIP_CLOSE
 *  Don't closed interface during setting sample rate
 * QUIRK_FLAG_FORCE_IFACE_RESET
 *  Force an interface reset whenever stopping & restarting a stream
 *  (e.g. after xrun)
 * QUIRK_FLAG_FIXED_RATE
 *  Do not set PCM rate (frequency) when only one rate is available
 *  for the given endpoint.
 */

#define QUIRK_FLAG_GET_SAMPLE_RATE	(1U << 0)
#define QUIRK_FLAG_SHARE_MEDIA_DEVICE	(1U << 1)
#define QUIRK_FLAG_ALIGN_TRANSFER	(1U << 2)
#define QUIRK_FLAG_TX_LENGTH		(1U << 3)
#define QUIRK_FLAG_PLAYBACK_FIRST	(1U << 4)
#define QUIRK_FLAG_SKIP_CLOCK_SELECTOR	(1U << 5)
#define QUIRK_FLAG_IGNORE_CLOCK_SOURCE	(1U << 6)
#define QUIRK_FLAG_ITF_USB_DSD_DAC	(1U << 7)
#define QUIRK_FLAG_CTL_MSG_DELAY	(1U << 8)
#define QUIRK_FLAG_CTL_MSG_DELAY_1M	(1U << 9)
#define QUIRK_FLAG_CTL_MSG_DELAY_5M	(1U << 10)
#define QUIRK_FLAG_IFACE_DELAY		(1U << 11)
#define QUIRK_FLAG_VALIDATE_RATES	(1U << 12)
#define QUIRK_FLAG_DISABLE_AUTOSUSPEND	(1U << 13)
#define QUIRK_FLAG_IGNORE_CTL_ERROR	(1U << 14)
#define QUIRK_FLAG_DSD_RAW		(1U << 15)
#define QUIRK_FLAG_SET_IFACE_FIRST	(1U << 16)
#define QUIRK_FLAG_GENERIC_IMPLICIT_FB	(1U << 17)
#define QUIRK_FLAG_SKIP_IMPLICIT_FB	(1U << 18)
#define QUIRK_FLAG_IFACE_SKIP_CLOSE	(1U << 19)
#define QUIRK_FLAG_FORCE_IFACE_RESET	(1U << 20)
#define QUIRK_FLAG_FIXED_RATE		(1U << 21)

struct audioformat;

enum snd_vendor_pcm_open_close {
	SOUND_PCM_CLOSE = 0,
	SOUND_PCM_OPEN,
};

/**
 * struct snd_usb_audio_vendor_ops - function callbacks for USB audio accelerators
 * @set_interface: called when an interface is initialized
 * @set_pcm_intf: called when the pcm interface is set
 * @set_pcm_connection: called when pcm is opened/closed
 *
 * Set of callbacks for some accelerated USB audio streaming hardware.
 *
 * TODO: make this USB host-controller specific, right now this only works for
 * one USB controller in the system at a time, which is only realistic for
 * self-contained systems like phones.
 */
struct snd_usb_audio_vendor_ops {
	int (*set_interface)(struct usb_device *udev,
			     struct usb_host_interface *alts,
			     int iface, int alt);
	int (*set_pcm_intf)(struct usb_interface *intf, int iface, int alt,
			    int direction, struct snd_usb_substream *subs);
	int (*set_pcm_connection)(struct usb_device *udev,
				  enum snd_vendor_pcm_open_close onoff,
				  int direction);
	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
};
#endif /* __USBAUDIO_H */
