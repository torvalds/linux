/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_FORMAT_H
#define __USBAUDIO_FORMAT_H

int snd_usb_parse_audio_format(struct snd_usb_audio *chip,
			       struct audioformat *fp, u64 format,
			       struct uac_format_type_i_continuous_descriptor *fmt,
			       int stream);

int snd_usb_parse_audio_format_v3(struct snd_usb_audio *chip,
			       struct audioformat *fp,
			       struct uac3_as_header_descriptor *as,
			       int stream);
#endif /*  __USBAUDIO_FORMAT_H */
