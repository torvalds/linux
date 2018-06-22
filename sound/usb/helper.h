/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_HELPER_H
#define __USBAUDIO_HELPER_H

unsigned int snd_usb_combine_bytes(unsigned char *bytes, int size);

void *snd_usb_find_desc(void *descstart, int desclen, void *after, u8 dtype);
void *snd_usb_find_csint_desc(void *descstart, int desclen, void *after, u8 dsubtype);

int snd_usb_ctl_msg(struct usb_device *dev, unsigned int pipe,
		    __u8 request, __u8 requesttype, __u16 value, __u16 index,
		    void *data, __u16 size);

unsigned char snd_usb_parse_datainterval(struct snd_usb_audio *chip,
					 struct usb_host_interface *alts);

/*
 * retrieve usb_interface descriptor from the host interface
 * (conditional for compatibility with the older API)
 */
#define get_iface_desc(iface)	(&(iface)->desc)
#define get_endpoint(alt,ep)	(&(alt)->endpoint[ep].desc)
#define get_ep_desc(ep)		(&(ep)->desc)
#define get_cfg_desc(cfg)	(&(cfg)->desc)

#define snd_usb_get_speed(dev) ((dev)->speed)

static inline int snd_usb_ctrl_intf(struct snd_usb_audio *chip)
{
	return get_iface_desc(chip->ctrl_intf)->bInterfaceNumber;
}

#endif /* __USBAUDIO_HELPER_H */
