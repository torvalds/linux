/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_PROC_H
#define __USBAUDIO_PROC_H

void snd_usb_audio_create_proc(struct snd_usb_audio *chip);
void snd_usb_proc_pcm_format_add(struct snd_usb_stream *stream);

#endif /* __USBAUDIO_PROC_H */

