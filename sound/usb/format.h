#ifndef __USBAUDIO_FORMAT_H
#define __USBAUDIO_FORMAT_H

int snd_usb_parse_audio_format(struct snd_usb_audio *chip, struct audioformat *fp,
			       int format, unsigned char *fmt, int stream,
			       struct usb_host_interface *iface);

#endif /*  __USBAUDIO_FORMAT_H */
