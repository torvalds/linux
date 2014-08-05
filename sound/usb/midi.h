#ifndef __USBMIDI_H
#define __USBMIDI_H

/* maximum number of endpoints per interface */
#define MIDI_MAX_ENDPOINTS 2

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

/* for QUIRK_AUDIO_EDIROL_UA700_UA25/UA1000, data is NULL */

/* for QUIRK_IGNORE_INTERFACE, data is NULL */

/* for QUIRK_MIDI_NOVATION and _RAW, data is NULL */

/* for QUIRK_MIDI_EMAGIC, data points to a snd_usb_midi_endpoint_info
 * structure (out_cables and in_cables only) */

/* for QUIRK_MIDI_CME, data is NULL */

/* for QUIRK_MIDI_AKAI, data is NULL */

int snd_usbmidi_create(struct snd_card *card,
		       struct usb_interface *iface,
		       struct list_head *midi_list,
		       const struct snd_usb_audio_quirk *quirk);
void snd_usbmidi_input_stop(struct list_head *p);
void snd_usbmidi_input_start(struct list_head *p);
void snd_usbmidi_disconnect(struct list_head *p);
void snd_usbmidi_suspend(struct list_head *p);
void snd_usbmidi_resume(struct list_head *p);

#endif /* __USBMIDI_H */
