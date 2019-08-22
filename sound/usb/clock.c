/*
 *   Clock domain and sample rate management functions
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>

#include "usbaudio.h"
#include "card.h"
#include "helper.h"
#include "clock.h"
#include "quirks.h"

static void *find_uac_clock_desc(struct usb_host_interface *iface, int id,
				 bool (*validator)(void *, int), u8 type)
{
	void *cs = NULL;

	while ((cs = snd_usb_find_csint_desc(iface->extra, iface->extralen,
					     cs, type))) {
		if (validator(cs, id))
			return cs;
	}

	return NULL;
}

static bool validate_clock_source_v2(void *p, int id)
{
	struct uac_clock_source_descriptor *cs = p;
	return cs->bClockID == id;
}

static bool validate_clock_source_v3(void *p, int id)
{
	struct uac3_clock_source_descriptor *cs = p;
	return cs->bClockID == id;
}

static bool validate_clock_selector_v2(void *p, int id)
{
	struct uac_clock_selector_descriptor *cs = p;
	return cs->bClockID == id;
}

static bool validate_clock_selector_v3(void *p, int id)
{
	struct uac3_clock_selector_descriptor *cs = p;
	return cs->bClockID == id;
}

static bool validate_clock_multiplier_v2(void *p, int id)
{
	struct uac_clock_multiplier_descriptor *cs = p;
	return cs->bClockID == id;
}

static bool validate_clock_multiplier_v3(void *p, int id)
{
	struct uac3_clock_multiplier_descriptor *cs = p;
	return cs->bClockID == id;
}

#define DEFINE_FIND_HELPER(name, obj, validator, type)		\
static obj *name(struct usb_host_interface *iface, int id)	\
{								\
	return find_uac_clock_desc(iface, id, validator, type);	\
}

DEFINE_FIND_HELPER(snd_usb_find_clock_source,
		   struct uac_clock_source_descriptor,
		   validate_clock_source_v2, UAC2_CLOCK_SOURCE);
DEFINE_FIND_HELPER(snd_usb_find_clock_source_v3,
		   struct uac3_clock_source_descriptor,
		   validate_clock_source_v3, UAC3_CLOCK_SOURCE);

DEFINE_FIND_HELPER(snd_usb_find_clock_selector,
		   struct uac_clock_selector_descriptor,
		   validate_clock_selector_v2, UAC2_CLOCK_SELECTOR);
DEFINE_FIND_HELPER(snd_usb_find_clock_selector_v3,
		   struct uac3_clock_selector_descriptor,
		   validate_clock_selector_v3, UAC3_CLOCK_SELECTOR);

DEFINE_FIND_HELPER(snd_usb_find_clock_multiplier,
		   struct uac_clock_multiplier_descriptor,
		   validate_clock_multiplier_v2, UAC2_CLOCK_MULTIPLIER);
DEFINE_FIND_HELPER(snd_usb_find_clock_multiplier_v3,
		   struct uac3_clock_multiplier_descriptor,
		   validate_clock_multiplier_v3, UAC3_CLOCK_MULTIPLIER);

static int uac_clock_selector_get_val(struct snd_usb_audio *chip, int selector_id)
{
	unsigned char buf;
	int ret;

	ret = snd_usb_ctl_msg(chip->dev, usb_rcvctrlpipe(chip->dev, 0),
			      UAC2_CS_CUR,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			      UAC2_CX_CLOCK_SELECTOR << 8,
			      snd_usb_ctrl_intf(chip) | (selector_id << 8),
			      &buf, sizeof(buf));

	if (ret < 0)
		return ret;

	return buf;
}

static int uac_clock_selector_set_val(struct snd_usb_audio *chip, int selector_id,
					unsigned char pin)
{
	int ret;

	ret = snd_usb_ctl_msg(chip->dev, usb_sndctrlpipe(chip->dev, 0),
			      UAC2_CS_CUR,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			      UAC2_CX_CLOCK_SELECTOR << 8,
			      snd_usb_ctrl_intf(chip) | (selector_id << 8),
			      &pin, sizeof(pin));
	if (ret < 0)
		return ret;

	if (ret != sizeof(pin)) {
		usb_audio_err(chip,
			"setting selector (id %d) unexpected length %d\n",
			selector_id, ret);
		return -EINVAL;
	}

	ret = uac_clock_selector_get_val(chip, selector_id);
	if (ret < 0)
		return ret;

	if (ret != pin) {
		usb_audio_err(chip,
			"setting selector (id %d) to %x failed (current: %d)\n",
			selector_id, pin, ret);
		return -EINVAL;
	}

	return ret;
}

static bool uac_clock_source_is_valid(struct snd_usb_audio *chip,
				      int protocol,
				      int source_id)
{
	int err;
	unsigned char data;
	struct usb_device *dev = chip->dev;
	u32 bmControls;

	if (protocol == UAC_VERSION_3) {
		struct uac3_clock_source_descriptor *cs_desc =
			snd_usb_find_clock_source_v3(chip->ctrl_intf, source_id);

		if (!cs_desc)
			return 0;
		bmControls = le32_to_cpu(cs_desc->bmControls);
	} else { /* UAC_VERSION_1/2 */
		struct uac_clock_source_descriptor *cs_desc =
			snd_usb_find_clock_source(chip->ctrl_intf, source_id);

		if (!cs_desc)
			return 0;
		bmControls = cs_desc->bmControls;
	}

	/* If a clock source can't tell us whether it's valid, we assume it is */
	if (!uac_v2v3_control_is_readable(bmControls,
				      UAC2_CS_CONTROL_CLOCK_VALID))
		return 1;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_CUR,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			      UAC2_CS_CONTROL_CLOCK_VALID << 8,
			      snd_usb_ctrl_intf(chip) | (source_id << 8),
			      &data, sizeof(data));

	if (err < 0) {
		dev_warn(&dev->dev,
			 "%s(): cannot get clock validity for id %d\n",
			   __func__, source_id);
		return 0;
	}

	return !!data;
}

static int __uac_clock_find_source(struct snd_usb_audio *chip, int entity_id,
				   unsigned long *visited, bool validate)
{
	struct uac_clock_source_descriptor *source;
	struct uac_clock_selector_descriptor *selector;
	struct uac_clock_multiplier_descriptor *multiplier;

	entity_id &= 0xff;

	if (test_and_set_bit(entity_id, visited)) {
		usb_audio_warn(chip,
			 "%s(): recursive clock topology detected, id %d.\n",
			 __func__, entity_id);
		return -EINVAL;
	}

	/* first, see if the ID we're looking for is a clock source already */
	source = snd_usb_find_clock_source(chip->ctrl_intf, entity_id);
	if (source) {
		entity_id = source->bClockID;
		if (validate && !uac_clock_source_is_valid(chip, UAC_VERSION_2,
								entity_id)) {
			usb_audio_err(chip,
				"clock source %d is not valid, cannot use\n",
				entity_id);
			return -ENXIO;
		}
		return entity_id;
	}

	selector = snd_usb_find_clock_selector(chip->ctrl_intf, entity_id);
	if (selector) {
		int ret, i, cur;

		/* the entity ID we are looking for is a selector.
		 * find out what it currently selects */
		ret = uac_clock_selector_get_val(chip, selector->bClockID);
		if (ret < 0)
			return ret;

		/* Selector values are one-based */

		if (ret > selector->bNrInPins || ret < 1) {
			usb_audio_err(chip,
				"%s(): selector reported illegal value, id %d, ret %d\n",
				__func__, selector->bClockID, ret);

			return -EINVAL;
		}

		cur = ret;
		ret = __uac_clock_find_source(chip, selector->baCSourceID[ret - 1],
					       visited, validate);
		if (!validate || ret > 0 || !chip->autoclock)
			return ret;

		/* The current clock source is invalid, try others. */
		for (i = 1; i <= selector->bNrInPins; i++) {
			int err;

			if (i == cur)
				continue;

			ret = __uac_clock_find_source(chip, selector->baCSourceID[i - 1],
				visited, true);
			if (ret < 0)
				continue;

			err = uac_clock_selector_set_val(chip, entity_id, i);
			if (err < 0)
				continue;

			usb_audio_info(chip,
				 "found and selected valid clock source %d\n",
				 ret);
			return ret;
		}

		return -ENXIO;
	}

	/* FIXME: multipliers only act as pass-thru element for now */
	multiplier = snd_usb_find_clock_multiplier(chip->ctrl_intf, entity_id);
	if (multiplier)
		return __uac_clock_find_source(chip, multiplier->bCSourceID,
						visited, validate);

	return -EINVAL;
}

static int __uac3_clock_find_source(struct snd_usb_audio *chip, int entity_id,
				   unsigned long *visited, bool validate)
{
	struct uac3_clock_source_descriptor *source;
	struct uac3_clock_selector_descriptor *selector;
	struct uac3_clock_multiplier_descriptor *multiplier;

	entity_id &= 0xff;

	if (test_and_set_bit(entity_id, visited)) {
		usb_audio_warn(chip,
			 "%s(): recursive clock topology detected, id %d.\n",
			 __func__, entity_id);
		return -EINVAL;
	}

	/* first, see if the ID we're looking for is a clock source already */
	source = snd_usb_find_clock_source_v3(chip->ctrl_intf, entity_id);
	if (source) {
		entity_id = source->bClockID;
		if (validate && !uac_clock_source_is_valid(chip, UAC_VERSION_3,
								entity_id)) {
			usb_audio_err(chip,
				"clock source %d is not valid, cannot use\n",
				entity_id);
			return -ENXIO;
		}
		return entity_id;
	}

	selector = snd_usb_find_clock_selector_v3(chip->ctrl_intf, entity_id);
	if (selector) {
		int ret, i, cur;

		/* the entity ID we are looking for is a selector.
		 * find out what it currently selects */
		ret = uac_clock_selector_get_val(chip, selector->bClockID);
		if (ret < 0)
			return ret;

		/* Selector values are one-based */

		if (ret > selector->bNrInPins || ret < 1) {
			usb_audio_err(chip,
				"%s(): selector reported illegal value, id %d, ret %d\n",
				__func__, selector->bClockID, ret);

			return -EINVAL;
		}

		cur = ret;
		ret = __uac3_clock_find_source(chip, selector->baCSourceID[ret - 1],
					       visited, validate);
		if (!validate || ret > 0 || !chip->autoclock)
			return ret;

		/* The current clock source is invalid, try others. */
		for (i = 1; i <= selector->bNrInPins; i++) {
			int err;

			if (i == cur)
				continue;

			ret = __uac3_clock_find_source(chip, selector->baCSourceID[i - 1],
				visited, true);
			if (ret < 0)
				continue;

			err = uac_clock_selector_set_val(chip, entity_id, i);
			if (err < 0)
				continue;

			usb_audio_info(chip,
				 "found and selected valid clock source %d\n",
				 ret);
			return ret;
		}

		return -ENXIO;
	}

	/* FIXME: multipliers only act as pass-thru element for now */
	multiplier = snd_usb_find_clock_multiplier_v3(chip->ctrl_intf,
						      entity_id);
	if (multiplier)
		return __uac3_clock_find_source(chip, multiplier->bCSourceID,
						visited, validate);

	return -EINVAL;
}

/*
 * For all kinds of sample rate settings and other device queries,
 * the clock source (end-leaf) must be used. However, clock selectors,
 * clock multipliers and sample rate converters may be specified as
 * clock source input to terminal. This functions walks the clock path
 * to its end and tries to find the source.
 *
 * The 'visited' bitfield is used internally to detect recursive loops.
 *
 * Returns the clock source UnitID (>=0) on success, or an error.
 */
int snd_usb_clock_find_source(struct snd_usb_audio *chip, int protocol,
			      int entity_id, bool validate)
{
	DECLARE_BITMAP(visited, 256);
	memset(visited, 0, sizeof(visited));

	switch (protocol) {
	case UAC_VERSION_2:
		return __uac_clock_find_source(chip, entity_id, visited,
					       validate);
	case UAC_VERSION_3:
		return __uac3_clock_find_source(chip, entity_id, visited,
					       validate);
	default:
		return -EINVAL;
	}
}

static int set_sample_rate_v1(struct snd_usb_audio *chip, int iface,
			      struct usb_host_interface *alts,
			      struct audioformat *fmt, int rate)
{
	struct usb_device *dev = chip->dev;
	unsigned int ep;
	unsigned char data[3];
	int err, crate;

	if (get_iface_desc(alts)->bNumEndpoints < 1)
		return -EINVAL;
	ep = get_endpoint(alts, 0)->bEndpointAddress;

	/* if endpoint doesn't have sampling rate control, bail out */
	if (!(fmt->attributes & UAC_EP_CS_ATTR_SAMPLE_RATE))
		return 0;

	data[0] = rate;
	data[1] = rate >> 8;
	data[2] = rate >> 16;
	err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
			      USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_OUT,
			      UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep,
			      data, sizeof(data));
	if (err < 0) {
		dev_err(&dev->dev, "%d:%d: cannot set freq %d to ep %#x\n",
			iface, fmt->altsetting, rate, ep);
		return err;
	}

	/* Don't check the sample rate for devices which we know don't
	 * support reading */
	if (snd_usb_get_sample_rate_quirk(chip))
		return 0;
	/* the firmware is likely buggy, don't repeat to fail too many times */
	if (chip->sample_rate_read_error > 2)
		return 0;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC_GET_CUR,
			      USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_IN,
			      UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep,
			      data, sizeof(data));
	if (err < 0) {
		dev_err(&dev->dev, "%d:%d: cannot get freq at ep %#x\n",
			iface, fmt->altsetting, ep);
		chip->sample_rate_read_error++;
		return 0; /* some devices don't support reading */
	}

	crate = data[0] | (data[1] << 8) | (data[2] << 16);
	if (crate != rate) {
		dev_warn(&dev->dev, "current rate %d is different from the runtime rate %d\n", crate, rate);
		// runtime->rate = crate;
	}

	return 0;
}

static int get_sample_rate_v2v3(struct snd_usb_audio *chip, int iface,
			      int altsetting, int clock)
{
	struct usb_device *dev = chip->dev;
	__le32 data;
	int err;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_CUR,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			      UAC2_CS_CONTROL_SAM_FREQ << 8,
			      snd_usb_ctrl_intf(chip) | (clock << 8),
			      &data, sizeof(data));
	if (err < 0) {
		dev_warn(&dev->dev, "%d:%d: cannot get freq (v2/v3): err %d\n",
			 iface, altsetting, err);
		return 0;
	}

	return le32_to_cpu(data);
}

static int set_sample_rate_v2v3(struct snd_usb_audio *chip, int iface,
			      struct usb_host_interface *alts,
			      struct audioformat *fmt, int rate)
{
	struct usb_device *dev = chip->dev;
	__le32 data;
	int err, cur_rate, prev_rate;
	int clock;
	bool writeable;
	u32 bmControls;

	/* First, try to find a valid clock. This may trigger
	 * automatic clock selection if the current clock is not
	 * valid.
	 */
	clock = snd_usb_clock_find_source(chip, fmt->protocol,
					  fmt->clock, true);
	if (clock < 0) {
		/* We did not find a valid clock, but that might be
		 * because the current sample rate does not match an
		 * external clock source. Try again without validation
		 * and we will do another validation after setting the
		 * rate.
		 */
		clock = snd_usb_clock_find_source(chip, fmt->protocol,
						  fmt->clock, false);
		if (clock < 0)
			return clock;
	}

	prev_rate = get_sample_rate_v2v3(chip, iface, fmt->altsetting, clock);
	if (prev_rate == rate)
		goto validation;

	if (fmt->protocol == UAC_VERSION_3) {
		struct uac3_clock_source_descriptor *cs_desc;

		cs_desc = snd_usb_find_clock_source_v3(chip->ctrl_intf, clock);
		bmControls = le32_to_cpu(cs_desc->bmControls);
	} else {
		struct uac_clock_source_descriptor *cs_desc;

		cs_desc = snd_usb_find_clock_source(chip->ctrl_intf, clock);
		bmControls = cs_desc->bmControls;
	}

	writeable = uac_v2v3_control_is_writeable(bmControls,
						  UAC2_CS_CONTROL_SAM_FREQ);
	if (writeable) {
		data = cpu_to_le32(rate);
		err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC2_CS_CUR,
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
				      UAC2_CS_CONTROL_SAM_FREQ << 8,
				      snd_usb_ctrl_intf(chip) | (clock << 8),
				      &data, sizeof(data));
		if (err < 0) {
			usb_audio_err(chip,
				"%d:%d: cannot set freq %d (v2/v3): err %d\n",
				iface, fmt->altsetting, rate, err);
			return err;
		}

		cur_rate = get_sample_rate_v2v3(chip, iface,
						fmt->altsetting, clock);
	} else {
		cur_rate = prev_rate;
	}

	if (cur_rate != rate) {
		if (!writeable) {
			usb_audio_warn(chip,
				 "%d:%d: freq mismatch (RO clock): req %d, clock runs @%d\n",
				 iface, fmt->altsetting, rate, cur_rate);
			return -ENXIO;
		}
		usb_audio_dbg(chip,
			"current rate %d is different from the runtime rate %d\n",
			cur_rate, rate);
	}

	/* Some devices doesn't respond to sample rate changes while the
	 * interface is active. */
	if (rate != prev_rate) {
		usb_set_interface(dev, iface, 0);
		snd_usb_set_interface_quirk(dev);
		usb_set_interface(dev, iface, fmt->altsetting);
		snd_usb_set_interface_quirk(dev);
	}

validation:
	/* validate clock after rate change */
	if (!uac_clock_source_is_valid(chip, fmt->protocol, clock))
		return -ENXIO;
	return 0;
}

int snd_usb_init_sample_rate(struct snd_usb_audio *chip, int iface,
			     struct usb_host_interface *alts,
			     struct audioformat *fmt, int rate)
{
	switch (fmt->protocol) {
	case UAC_VERSION_1:
	default:
		return set_sample_rate_v1(chip, iface, alts, fmt, rate);

	case UAC_VERSION_3:
		if (chip->badd_profile >= UAC3_FUNCTION_SUBCLASS_GENERIC_IO) {
			if (rate != UAC3_BADD_SAMPLING_RATE)
				return -ENXIO;
			else
				return 0;
		}
	/* fall through */
	case UAC_VERSION_2:
		return set_sample_rate_v2v3(chip, iface, alts, fmt, rate);
	}
}

