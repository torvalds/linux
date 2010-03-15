#ifndef __USBAUDIO_H
#define __USBAUDIO_H
/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* maximum number of endpoints per interface */
#define MIDI_MAX_ENDPOINTS 2

/* handling of USB vendor/product ID pairs as 32-bit numbers */
#define USB_ID(vendor, product) (((vendor) << 16) | (product))
#define USB_ID_VENDOR(id) ((id) >> 16)
#define USB_ID_PRODUCT(id) ((u16)(id))

/*
 */

struct snd_usb_audio {
	int index;
	struct usb_device *dev;
	struct snd_card *card;
	u32 usb_id;
	int shutdown;
	unsigned int txfr_quirk:1; /* Subframe boundaries on transfers */
	int num_interfaces;
	int num_suspended_intf;

	/* for audio class v2 */
	int clock_id;

	struct list_head pcm_list;	/* list of pcm streams */
	int pcm_devs;

	struct list_head midi_list;	/* list of midi interfaces */

	struct list_head mixer_list;	/* list of mixer interfaces */
};

/*
 * Information about devices with broken descriptors
 */

/* special values for .ifnum */
#define QUIRK_NO_INTERFACE		-2
#define QUIRK_ANY_INTERFACE		-1

enum quirk_type {
	QUIRK_IGNORE_INTERFACE,
	QUIRK_COMPOSITE,
	QUIRK_MIDI_STANDARD_INTERFACE,
	QUIRK_MIDI_FIXED_ENDPOINT,
	QUIRK_MIDI_YAMAHA,
	QUIRK_MIDI_MIDIMAN,
	QUIRK_MIDI_NOVATION,
	QUIRK_MIDI_FASTLANE,
	QUIRK_MIDI_EMAGIC,
	QUIRK_MIDI_CME,
	QUIRK_MIDI_US122L,
	QUIRK_AUDIO_STANDARD_INTERFACE,
	QUIRK_AUDIO_FIXED_ENDPOINT,
	QUIRK_AUDIO_EDIROL_UAXX,
	QUIRK_AUDIO_ALIGN_TRANSFER,

	QUIRK_TYPE_COUNT
};

struct snd_usb_audio_quirk {
	const char *vendor_name;
	const char *product_name;
	int16_t ifnum;
	uint16_t type;
	const void *data;
};

/* data for QUIRK_MIDI_FIXED_ENDPOINT */
struct snd_usb_midi_endpoint_info {
	int8_t   out_ep;	/* ep number, 0 autodetect */
	uint8_t  out_interval;	/* interval for interrupt endpoints */
	int8_t   in_ep;	
	uint8_t  in_interval;
	uint16_t out_cables;	/* bitmask */
	uint16_t in_cables;	/* bitmask */
};

/* for QUIRK_MIDI_YAMAHA, data is NULL */

/* for QUIRK_MIDI_MIDIMAN, data points to a snd_usb_midi_endpoint_info
 * structure (out_cables and in_cables only) */

/* for QUIRK_COMPOSITE, data points to an array of snd_usb_audio_quirk
 * structures, terminated with .ifnum = -1 */

/* for QUIRK_AUDIO_FIXED_ENDPOINT, data points to an audioformat structure */

/* for QUIRK_AUDIO/MIDI_STANDARD_INTERFACE, data is NULL */

/* for QUIRK_AUDIO_EDIROL_UAXX, data is NULL */

/* for QUIRK_IGNORE_INTERFACE, data is NULL */

/* for QUIRK_MIDI_NOVATION and _RAW, data is NULL */

/* for QUIRK_MIDI_EMAGIC, data points to a snd_usb_midi_endpoint_info
 * structure (out_cables and in_cables only) */

/* for QUIRK_MIDI_CME, data is NULL */

/*
 */

/*E-mu USB samplerate control quirk*/
enum {
	EMU_QUIRK_SR_44100HZ = 0,
	EMU_QUIRK_SR_48000HZ,
	EMU_QUIRK_SR_88200HZ,
	EMU_QUIRK_SR_96000HZ,
	EMU_QUIRK_SR_176400HZ,
	EMU_QUIRK_SR_192000HZ
};

#define combine_word(s)    ((*(s)) | ((unsigned int)(s)[1] << 8))
#define combine_triple(s)  (combine_word(s) | ((unsigned int)(s)[2] << 16))
#define combine_quad(s)    (combine_triple(s) | ((unsigned int)(s)[3] << 24))

unsigned int snd_usb_combine_bytes(unsigned char *bytes, int size);

void *snd_usb_find_desc(void *descstart, int desclen, void *after, u8 dtype);
void *snd_usb_find_csint_desc(void *descstart, int desclen, void *after, u8 dsubtype);

int snd_usb_ctl_msg(struct usb_device *dev, unsigned int pipe,
		    __u8 request, __u8 requesttype, __u16 value, __u16 index,
		    void *data, __u16 size, int timeout);

int snd_usb_create_mixer(struct snd_usb_audio *chip, int ctrlif,
			 int ignore_error);
void snd_usb_mixer_disconnect(struct list_head *p);

int snd_usbmidi_create(struct snd_card *card,
		       struct usb_interface *iface,
		       struct list_head *midi_list,
		       const struct snd_usb_audio_quirk *quirk);
void snd_usbmidi_input_stop(struct list_head* p);
void snd_usbmidi_input_start(struct list_head* p);
void snd_usbmidi_disconnect(struct list_head *p);

void snd_emuusb_set_samplerate(struct snd_usb_audio *chip,
			unsigned char samplerate_id);

/*
 * retrieve usb_interface descriptor from the host interface
 * (conditional for compatibility with the older API)
 */
#ifndef get_iface_desc
#define get_iface_desc(iface)	(&(iface)->desc)
#define get_endpoint(alt,ep)	(&(alt)->endpoint[ep].desc)
#define get_ep_desc(ep)		(&(ep)->desc)
#define get_cfg_desc(cfg)	(&(cfg)->desc)
#endif

#ifndef snd_usb_get_speed
#define snd_usb_get_speed(dev) ((dev)->speed)
#endif

#endif /* __USBAUDIO_H */
