// SPDX-License-Identifier: GPL-2.0
#ifndef __USBAUDIO_IMPLICIT_H
#define __USBAUDIO_IMPLICIT_H

int snd_usb_parse_implicit_fb_quirk(struct snd_usb_audio *chip,
				    struct audioformat *fmt,
				    struct usb_host_interface *alts);
const struct audioformat *
snd_usb_find_implicit_fb_sync_format(struct snd_usb_audio *chip,
				     const struct audioformat *target,
				     const struct snd_pcm_hw_params *params,
				     int stream);

#endif /* __USBAUDIO_IMPLICIT_H */
