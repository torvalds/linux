#ifndef __USBAUDIO_QUIRKS_H
#define __USBAUDIO_QUIRKS_H

struct audioformat;
struct snd_usb_endpoint;
struct snd_usb_substream;

int snd_usb_create_quirk(struct snd_usb_audio *chip,
			 struct usb_interface *iface,
			 struct usb_driver *driver,
			 const struct snd_usb_audio_quirk *quirk);

int snd_usb_apply_interface_quirk(struct snd_usb_audio *chip,
				  int iface,
				  int altno);

int snd_usb_apply_boot_quirk(struct usb_device *dev,
			     struct usb_interface *intf,
			     const struct snd_usb_audio_quirk *quirk);

void snd_usb_set_format_quirk(struct snd_usb_substream *subs,
			      struct audioformat *fmt);

int snd_usb_is_big_endian_format(struct snd_usb_audio *chip,
				 struct audioformat *fp);

void snd_usb_endpoint_start_quirk(struct snd_usb_endpoint *ep);

void snd_usb_set_interface_quirk(struct usb_device *dev);
void snd_usb_ctl_msg_quirk(struct usb_device *dev, unsigned int pipe,
			   __u8 request, __u8 requesttype, __u16 value,
			   __u16 index, void *data, __u16 size);

u64 snd_usb_interface_dsd_format_quirks(struct snd_usb_audio *chip,
					struct audioformat *fp,
					unsigned int sample_bytes);

#endif /* __USBAUDIO_QUIRKS_H */
