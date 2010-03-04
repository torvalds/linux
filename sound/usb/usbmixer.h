#ifndef __USBMIXER_H
#define __USBMIXER_H

int snd_usb_create_mixer(struct snd_usb_audio *chip, int ctrlif,
			 int ignore_error);
void snd_usb_mixer_disconnect(struct list_head *p);

void snd_emuusb_set_samplerate(struct snd_usb_audio *chip,
			unsigned char samplerate_id);

#endif /* __USBMIXER_H */
