// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "usbaudio.h"
#include "helper.h"
#include "quirks.h"

/*
 * combine bytes and get an integer value
 */
unsigned int snd_usb_combine_bytes(unsigned char *bytes, int size)
{
	switch (size) {
	case 1:  return *bytes;
	case 2:  return combine_word(bytes);
	case 3:  return combine_triple(bytes);
	case 4:  return combine_quad(bytes);
	default: return 0;
	}
}

/*
 * parse descriptor buffer and return the pointer starting the given
 * descriptor type.
 */
void *snd_usb_find_desc(void *descstart, int desclen, void *after, u8 dtype)
{
	u8 *p, *end, *next;

	p = descstart;
	end = p + desclen;
	for (; p < end;) {
		if (p[0] < 2)
			return NULL;
		next = p + p[0];
		if (next > end)
			return NULL;
		if (p[1] == dtype && (!after || (void *)p > after)) {
			return p;
		}
		p = next;
	}
	return NULL;
}

/*
 * find a class-specified interface descriptor with the given subtype.
 */
void *snd_usb_find_csint_desc(void *buffer, int buflen, void *after, u8 dsubtype)
{
	unsigned char *p = after;

	while ((p = snd_usb_find_desc(buffer, buflen, p,
				      USB_DT_CS_INTERFACE)) != NULL) {
		if (p[0] >= 3 && p[2] == dsubtype)
			return p;
	}
	return NULL;
}

/*
 * Wrapper for usb_control_msg().
 * Allocates a temp buffer to prevent dmaing from/to the stack.
 */
int snd_usb_ctl_msg(struct usb_device *dev, unsigned int pipe, __u8 request,
		    __u8 requesttype, __u16 value, __u16 index, void *data,
		    __u16 size)
{
	int err;
	void *buf = NULL;
	int timeout;

	if (usb_pipe_type_check(dev, pipe))
		return -EINVAL;

	if (size > 0) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	if (requesttype & USB_DIR_IN)
		timeout = USB_CTRL_GET_TIMEOUT;
	else
		timeout = USB_CTRL_SET_TIMEOUT;

	err = usb_control_msg(dev, pipe, request, requesttype,
			      value, index, buf, size, timeout);

	if (size > 0) {
		memcpy(data, buf, size);
		kfree(buf);
	}

	snd_usb_ctl_msg_quirk(dev, pipe, request, requesttype,
			      value, index, data, size);

	return err;
}

unsigned char snd_usb_parse_datainterval(struct snd_usb_audio *chip,
					 struct usb_host_interface *alts)
{
	switch (snd_usb_get_speed(chip->dev)) {
	case USB_SPEED_HIGH:
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		if (get_endpoint(alts, 0)->bInterval >= 1 &&
		    get_endpoint(alts, 0)->bInterval <= 4)
			return get_endpoint(alts, 0)->bInterval - 1;
		break;
	default:
		break;
	}
	return 0;
}

struct usb_host_interface *
snd_usb_get_host_interface(struct snd_usb_audio *chip, int ifnum, int altsetting)
{
	struct usb_interface *iface;

	iface = usb_ifnum_to_if(chip->dev, ifnum);
	if (!iface)
		return NULL;
	return usb_altnum_to_altsetting(iface, altsetting);
}

int snd_usb_add_ctrl_interface_link(struct snd_usb_audio *chip, int ifnum,
		int ctrlif)
{
	struct usb_device *dev = chip->dev;
	struct usb_host_interface *host_iface;

	if (chip->num_intf_to_ctrl >= MAX_CARD_INTERFACES) {
		dev_info(&dev->dev, "Too many interfaces assigned to the single USB-audio card\n");
		return -EINVAL;
	}

	/* find audiocontrol interface */
	host_iface = &usb_ifnum_to_if(dev, ctrlif)->altsetting[0];

	chip->intf_to_ctrl[chip->num_intf_to_ctrl].interface = ifnum;
	chip->intf_to_ctrl[chip->num_intf_to_ctrl].ctrl_intf = host_iface;
	chip->num_intf_to_ctrl++;

	return 0;
}

struct usb_host_interface *snd_usb_find_ctrl_interface(struct snd_usb_audio *chip,
							int ifnum)
{
	int i;

	for (i = 0; i < chip->num_intf_to_ctrl; ++i)
		if (chip->intf_to_ctrl[i].interface == ifnum)
			return chip->intf_to_ctrl[i].ctrl_intf;

	/* Fallback to first audiocontrol interface */
	return chip->ctrl_intf;
}
