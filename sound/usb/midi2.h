// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __USB_AUDIO_MIDI2_H
#define __USB_AUDIO_MIDI2_H

#include "midi.h"

#if IS_ENABLED(CONFIG_SND_USB_AUDIO_MIDI_V2)
int snd_usb_midi_v2_create(struct snd_usb_audio *chip,
			   struct usb_interface *iface,
			   const struct snd_usb_audio_quirk *quirk,
			   unsigned int usb_id);
void snd_usb_midi_v2_suspend_all(struct snd_usb_audio *chip);
void snd_usb_midi_v2_resume_all(struct snd_usb_audio *chip);
void snd_usb_midi_v2_disconnect_all(struct snd_usb_audio *chip);
void snd_usb_midi_v2_free_all(struct snd_usb_audio *chip);
#else /* CONFIG_SND_USB_AUDIO_MIDI_V2 */
/* fallback to MIDI 1.0 creation */
static inline int snd_usb_midi_v2_create(struct snd_usb_audio *chip,
					 struct usb_interface *iface,
					 const struct snd_usb_audio_quirk *quirk,
					 unsigned int usb_id)
{
	return __snd_usbmidi_create(chip->card, iface, &chip->midi_list,
				    quirk, usb_id, &chip->num_rawmidis);
}

static inline void snd_usb_midi_v2_suspend_all(struct snd_usb_audio *chip) {}
static inline void snd_usb_midi_v2_resume_all(struct snd_usb_audio *chip) {}
static inline void snd_usb_midi_v2_disconnect_all(struct snd_usb_audio *chip) {}
static inline void snd_usb_midi_v2_free_all(struct snd_usb_audio *chip) {}
#endif /* CONFIG_SND_USB_AUDIO_MIDI_V2 */

#endif /* __USB_AUDIO_MIDI2_H */
