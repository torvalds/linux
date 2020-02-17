// SPDX-License-Identifier: GPL-2.0
/*
 *   UAC3 Power Domain state management functions
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>

#include "usbaudio.h"
#include "helper.h"
#include "power.h"

struct snd_usb_power_domain *
snd_usb_find_power_domain(struct usb_host_interface *ctrl_iface,
			  unsigned char id)
{
	struct snd_usb_power_domain *pd;
	void *p;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return NULL;

	p = NULL;
	while ((p = snd_usb_find_csint_desc(ctrl_iface->extra,
					    ctrl_iface->extralen,
					    p, UAC3_POWER_DOMAIN)) != NULL) {
		struct uac3_power_domain_descriptor *pd_desc = p;
		int i;

		if (!snd_usb_validate_audio_desc(p, UAC_VERSION_3))
			continue;
		for (i = 0; i < pd_desc->bNrEntities; i++) {
			if (pd_desc->baEntityID[i] == id) {
				pd->pd_id = pd_desc->bPowerDomainID;
				pd->pd_d1d0_rec =
					le16_to_cpu(pd_desc->waRecoveryTime1);
				pd->pd_d2d0_rec =
					le16_to_cpu(pd_desc->waRecoveryTime2);
				return pd;
			}
		}
	}

	kfree(pd);
	return NULL;
}

int snd_usb_power_domain_set(struct snd_usb_audio *chip,
			     struct snd_usb_power_domain *pd,
			     unsigned char state)
{
	struct usb_device *dev = chip->dev;
	unsigned char current_state;
	int err, idx;

	idx = snd_usb_ctrl_intf(chip) | (pd->pd_id << 8);

	err = snd_usb_ctl_msg(chip->dev, usb_rcvctrlpipe(chip->dev, 0),
			      UAC2_CS_CUR,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			      UAC3_AC_POWER_DOMAIN_CONTROL << 8, idx,
			      &current_state, sizeof(current_state));
	if (err < 0) {
		dev_err(&dev->dev, "Can't get UAC3 power state for id %d\n",
			pd->pd_id);
		return err;
	}

	if (current_state == state) {
		dev_dbg(&dev->dev, "UAC3 power domain id %d already in state %d\n",
			pd->pd_id, state);
		return 0;
	}

	err = snd_usb_ctl_msg(chip->dev, usb_sndctrlpipe(chip->dev, 0), UAC2_CS_CUR,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			      UAC3_AC_POWER_DOMAIN_CONTROL << 8, idx,
			      &state, sizeof(state));
	if (err < 0) {
		dev_err(&dev->dev, "Can't set UAC3 power state to %d for id %d\n",
			state, pd->pd_id);
		return err;
	}

	if (state == UAC3_PD_STATE_D0) {
		switch (current_state) {
		case UAC3_PD_STATE_D2:
			udelay(pd->pd_d2d0_rec * 50);
			break;
		case UAC3_PD_STATE_D1:
			udelay(pd->pd_d1d0_rec * 50);
			break;
		default:
			return -EINVAL;
		}
	}

	dev_dbg(&dev->dev, "UAC3 power domain id %d change to state %d\n",
		pd->pd_id, state);

	return 0;
}
