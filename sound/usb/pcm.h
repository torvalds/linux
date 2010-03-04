#ifndef __USBAUDIO_PCM_H
#define __USBAUDIO_PCM_H

void snd_usb_set_pcm_ops(struct snd_pcm *pcm, int stream);

int snd_usb_init_pitch(struct usb_device *dev, int iface,
		       struct usb_host_interface *alts,
		       struct audioformat *fmt);

int snd_usb_init_sample_rate(struct usb_device *dev, int iface,
			     struct usb_host_interface *alts,
			     struct audioformat *fmt, int rate);

#endif /* __USBAUDIO_PCM_H */
