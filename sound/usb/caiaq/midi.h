/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CAIAQ_MIDI_H
#define CAIAQ_MIDI_H

int snd_usb_caiaq_midi_init(struct snd_usb_caiaqdev *cdev);
void snd_usb_caiaq_midi_handle_input(struct snd_usb_caiaqdev *cdev,
				     int port, const char *buf, int len);
void snd_usb_caiaq_midi_output_done(struct urb *urb);

#endif /* CAIAQ_MIDI_H */
