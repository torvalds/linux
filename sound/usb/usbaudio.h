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

/*
 *
 */

struct media_device;
struct media_intf_devnode;

#define MAX_CARD_INTERFACES	16

/*
 * Structure holding assosiation between Audio Control Interface
 * and given Streaming or Midi Interface.
 */
struct snd_intf_to_ctrl {
	u8 interface;
	struct usb_host_interface *ctrl_intf;
};

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

	unsigned int num_rawmidis;	/* number of created rawmidi devices */
	struct list_head midi_list;	/* list of midi interfaces */
	struct list_head midi_v2_list;	/* list of MIDI 2 interfaces */

	struct list_head mixer_list;	/* list of mixer interfaces */

	int setup;			/* from the 'device_setup' module param */
	bool generic_implicit_fb;	/* from the 'implicit_fb' module param */
	bool autoclock;			/* from the 'autoclock' module param */

	bool lowlatency;		/* from the 'lowlatency' module param */
	struct usb_host_interface *ctrl_intf;	/* the audio control interface */
	struct media_device *media_dev;
	struct media_intf_devnode *ctl_intf_media_devnode;

	unsigned int num_intf_to_ctrl;
	struct snd_intf_to_ctrl intf_to_ctrl[MAX_CARD_INTERFACES];
};

#define USB_AUDIO_IFACE_UNUSED	((void *)-1L)

#define usb_audio_err(chip, fmt, args...) \
	dev_err(&(chip)->dev->dev, fmt, ##args)
#define usb_audio_err_ratelimited(chip, fmt, args...) \
	dev_err_ratelimited(&(chip)->dev->dev, fmt, ##args)
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

/* auto-cleanup */
struct __snd_usb_lock {
	struct snd_usb_audio *chip;
	int err;
};

static inline struct __snd_usb_lock __snd_usb_lock_shutdown(struct snd_usb_audio *chip)
{
	struct __snd_usb_lock T = { .chip = chip };
	T.err = snd_usb_lock_shutdown(chip);
	return T;
}

static inline void __snd_usb_unlock_shutdown(struct __snd_usb_lock *lock)
{
	if (!lock->err)
		snd_usb_unlock_shutdown(lock->chip);
}

DEFINE_CLASS(snd_usb_lock, struct __snd_usb_lock,
	     __snd_usb_unlock_shutdown(&(_T)), __snd_usb_lock_shutdown(chip),
	     struct snd_usb_audio *chip)

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
 * QUIRK_FLAG_MIC_RES_16 and QUIRK_FLAG_MIC_RES_384
 *  Set the fixed resolution for Mic Capture Volume (mostly for webcams)
 * QUIRK_FLAG_MIXER_PLAYBACK_MIN_MUTE
 *  Set minimum volume control value as mute for devices where the lowest
 *  playback value represents muted state instead of minimum audible volume
 * QUIRK_FLAG_MIXER_CAPTURE_MIN_MUTE
 *  Similar to QUIRK_FLAG_MIXER_PLAYBACK_MIN_MUTE, but for capture streams
 */

enum {
	QUIRK_TYPE_GET_SAMPLE_RATE		= 0,
	QUIRK_TYPE_SHARE_MEDIA_DEVICE		= 1,
	QUIRK_TYPE_ALIGN_TRANSFER		= 2,
	QUIRK_TYPE_TX_LENGTH			= 3,
	QUIRK_TYPE_PLAYBACK_FIRST		= 4,
	QUIRK_TYPE_SKIP_CLOCK_SELECTOR		= 5,
	QUIRK_TYPE_IGNORE_CLOCK_SOURCE		= 6,
	QUIRK_TYPE_ITF_USB_DSD_DAC		= 7,
	QUIRK_TYPE_CTL_MSG_DELAY		= 8,
	QUIRK_TYPE_CTL_MSG_DELAY_1M		= 9,
	QUIRK_TYPE_CTL_MSG_DELAY_5M		= 10,
	QUIRK_TYPE_IFACE_DELAY			= 11,
	QUIRK_TYPE_VALIDATE_RATES		= 12,
	QUIRK_TYPE_DISABLE_AUTOSUSPEND		= 13,
	QUIRK_TYPE_IGNORE_CTL_ERROR		= 14,
	QUIRK_TYPE_DSD_RAW			= 15,
	QUIRK_TYPE_SET_IFACE_FIRST		= 16,
	QUIRK_TYPE_GENERIC_IMPLICIT_FB		= 17,
	QUIRK_TYPE_SKIP_IMPLICIT_FB		= 18,
	QUIRK_TYPE_IFACE_SKIP_CLOSE		= 19,
	QUIRK_TYPE_FORCE_IFACE_RESET		= 20,
	QUIRK_TYPE_FIXED_RATE			= 21,
	QUIRK_TYPE_MIC_RES_16			= 22,
	QUIRK_TYPE_MIC_RES_384			= 23,
	QUIRK_TYPE_MIXER_PLAYBACK_MIN_MUTE	= 24,
	QUIRK_TYPE_MIXER_CAPTURE_MIN_MUTE	= 25,
/* Please also edit snd_usb_audio_quirk_flag_names */
};

#define QUIRK_FLAG(x)	BIT_U32(QUIRK_TYPE_ ## x)

#define QUIRK_FLAG_GET_SAMPLE_RATE		QUIRK_FLAG(GET_SAMPLE_RATE)
#define QUIRK_FLAG_SHARE_MEDIA_DEVICE		QUIRK_FLAG(SHARE_MEDIA_DEVICE)
#define QUIRK_FLAG_ALIGN_TRANSFER		QUIRK_FLAG(ALIGN_TRANSFER)
#define QUIRK_FLAG_TX_LENGTH			QUIRK_FLAG(TX_LENGTH)
#define QUIRK_FLAG_PLAYBACK_FIRST		QUIRK_FLAG(PLAYBACK_FIRST)
#define QUIRK_FLAG_SKIP_CLOCK_SELECTOR		QUIRK_FLAG(SKIP_CLOCK_SELECTOR)
#define QUIRK_FLAG_IGNORE_CLOCK_SOURCE		QUIRK_FLAG(IGNORE_CLOCK_SOURCE)
#define QUIRK_FLAG_ITF_USB_DSD_DAC		QUIRK_FLAG(ITF_USB_DSD_DAC)
#define QUIRK_FLAG_CTL_MSG_DELAY		QUIRK_FLAG(CTL_MSG_DELAY)
#define QUIRK_FLAG_CTL_MSG_DELAY_1M		QUIRK_FLAG(CTL_MSG_DELAY_1M)
#define QUIRK_FLAG_CTL_MSG_DELAY_5M		QUIRK_FLAG(CTL_MSG_DELAY_5M)
#define QUIRK_FLAG_IFACE_DELAY			QUIRK_FLAG(IFACE_DELAY)
#define QUIRK_FLAG_VALIDATE_RATES		QUIRK_FLAG(VALIDATE_RATES)
#define QUIRK_FLAG_DISABLE_AUTOSUSPEND		QUIRK_FLAG(DISABLE_AUTOSUSPEND)
#define QUIRK_FLAG_IGNORE_CTL_ERROR		QUIRK_FLAG(IGNORE_CTL_ERROR)
#define QUIRK_FLAG_DSD_RAW			QUIRK_FLAG(DSD_RAW)
#define QUIRK_FLAG_SET_IFACE_FIRST		QUIRK_FLAG(SET_IFACE_FIRST)
#define QUIRK_FLAG_GENERIC_IMPLICIT_FB		QUIRK_FLAG(GENERIC_IMPLICIT_FB)
#define QUIRK_FLAG_SKIP_IMPLICIT_FB		QUIRK_FLAG(SKIP_IMPLICIT_FB)
#define QUIRK_FLAG_IFACE_SKIP_CLOSE		QUIRK_FLAG(IFACE_SKIP_CLOSE)
#define QUIRK_FLAG_FORCE_IFACE_RESET		QUIRK_FLAG(FORCE_IFACE_RESET)
#define QUIRK_FLAG_FIXED_RATE			QUIRK_FLAG(FIXED_RATE)
#define QUIRK_FLAG_MIC_RES_16			QUIRK_FLAG(MIC_RES_16)
#define QUIRK_FLAG_MIC_RES_384			QUIRK_FLAG(MIC_RES_384)
#define QUIRK_FLAG_MIXER_PLAYBACK_MIN_MUTE	QUIRK_FLAG(MIXER_PLAYBACK_MIN_MUTE)
#define QUIRK_FLAG_MIXER_CAPTURE_MIN_MUTE	QUIRK_FLAG(MIXER_CAPTURE_MIN_MUTE)

#endif /* __USBAUDIO_H */
