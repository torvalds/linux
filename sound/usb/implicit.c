// SPDX-License-Identifier: GPL-2.0-or-later
//
// Special handling for implicit feedback mode
//

#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "usbaudio.h"
#include "card.h"
#include "helper.h"
#include "implicit.h"

enum {
	IMPLICIT_FB_NONE,
	IMPLICIT_FB_GENERIC,
	IMPLICIT_FB_FIXED,
	IMPLICIT_FB_BOTH,	/* generic playback + capture (for BOSS) */
};

struct snd_usb_implicit_fb_match {
	unsigned int id;
	unsigned int iface_class;
	unsigned int ep_num;
	unsigned int iface;
	int type;
};

#define IMPLICIT_FB_GENERIC_DEV(vend, prod) \
	{ .id = USB_ID(vend, prod), .type = IMPLICIT_FB_GENERIC }
#define IMPLICIT_FB_FIXED_DEV(vend, prod, ep, ifnum) \
	{ .id = USB_ID(vend, prod), .type = IMPLICIT_FB_FIXED, .ep_num = (ep),\
	    .iface = (ifnum) }
#define IMPLICIT_FB_BOTH_DEV(vend, prod, ep, ifnum) \
	{ .id = USB_ID(vend, prod), .type = IMPLICIT_FB_BOTH, .ep_num = (ep),\
	    .iface = (ifnum) }
#define IMPLICIT_FB_SKIP_DEV(vend, prod) \
	{ .id = USB_ID(vend, prod), .type = IMPLICIT_FB_NONE }

/* Implicit feedback quirk table for playback */
static const struct snd_usb_implicit_fb_match playback_implicit_fb_quirks[] = {
	/* Generic matching */
	IMPLICIT_FB_GENERIC_DEV(0x0499, 0x1509), /* Steinberg UR22 */
	IMPLICIT_FB_GENERIC_DEV(0x0763, 0x2080), /* M-Audio FastTrack Ultra */
	IMPLICIT_FB_GENERIC_DEV(0x0763, 0x2081), /* M-Audio FastTrack Ultra */
	IMPLICIT_FB_GENERIC_DEV(0x0763, 0x2030), /* M-Audio Fast Track C400 */
	IMPLICIT_FB_GENERIC_DEV(0x0763, 0x2031), /* M-Audio Fast Track C600 */

	/* Fixed EP */
	/* FIXME: check the availability of generic matching */
	IMPLICIT_FB_FIXED_DEV(0x1397, 0x0001, 0x81, 1), /* Behringer UFX1604 */
	IMPLICIT_FB_FIXED_DEV(0x1397, 0x0002, 0x81, 1), /* Behringer UFX1204 */
	IMPLICIT_FB_FIXED_DEV(0x2466, 0x8010, 0x81, 2), /* Fractal Audio Axe-Fx III */
	IMPLICIT_FB_FIXED_DEV(0x31e9, 0x0001, 0x81, 2), /* Solid State Logic SSL2 */
	IMPLICIT_FB_FIXED_DEV(0x31e9, 0x0002, 0x81, 2), /* Solid State Logic SSL2+ */
	IMPLICIT_FB_FIXED_DEV(0x0499, 0x172f, 0x81, 2), /* Steinberg UR22C */
	IMPLICIT_FB_FIXED_DEV(0x0d9a, 0x00df, 0x81, 2), /* RTX6001 */
	IMPLICIT_FB_FIXED_DEV(0x22f0, 0x0006, 0x81, 3), /* Allen&Heath Qu-16 */
	IMPLICIT_FB_FIXED_DEV(0x1686, 0xf029, 0x82, 2), /* Zoom UAC-2 */
	IMPLICIT_FB_FIXED_DEV(0x2466, 0x8003, 0x86, 2), /* Fractal Audio Axe-Fx II */
	IMPLICIT_FB_FIXED_DEV(0x0499, 0x172a, 0x86, 2), /* Yamaha MODX */

	/* Special matching */
	{ .id = USB_ID(0x07fd, 0x0004), .iface_class = USB_CLASS_AUDIO,
	  .type = IMPLICIT_FB_NONE },		/* MicroBook IIc */
	/* ep = 0x84, ifnum = 0 */
	{ .id = USB_ID(0x07fd, 0x0004), .iface_class = USB_CLASS_VENDOR_SPEC,
	  .type = IMPLICIT_FB_FIXED,
	  .ep_num = 0x84, .iface = 0 },		/* MOTU MicroBook II */

	{} /* terminator */
};

/* Implicit feedback quirk table for capture: only FIXED type */
static const struct snd_usb_implicit_fb_match capture_implicit_fb_quirks[] = {
	{} /* terminator */
};

/* set up sync EP information on the audioformat */
static int add_implicit_fb_sync_ep(struct snd_usb_audio *chip,
				   struct audioformat *fmt,
				   int ep, int ep_idx, int ifnum,
				   const struct usb_host_interface *alts)
{
	struct usb_interface *iface;

	if (!alts) {
		iface = usb_ifnum_to_if(chip->dev, ifnum);
		if (!iface || iface->num_altsetting < 2)
			return 0;
		alts = &iface->altsetting[1];
	}

	fmt->sync_ep = ep;
	fmt->sync_iface = ifnum;
	fmt->sync_altsetting = alts->desc.bAlternateSetting;
	fmt->sync_ep_idx = ep_idx;
	fmt->implicit_fb = 1;
	usb_audio_dbg(chip,
		      "%d:%d: added %s implicit_fb sync_ep %x, iface %d:%d\n",
		      fmt->iface, fmt->altsetting,
		      (ep & USB_DIR_IN) ? "playback" : "capture",
		      fmt->sync_ep, fmt->sync_iface, fmt->sync_altsetting);
	return 1;
}

/* Check whether the given UAC2 iface:altset points to an implicit fb source */
static int add_generic_uac2_implicit_fb(struct snd_usb_audio *chip,
					struct audioformat *fmt,
					unsigned int ifnum,
					unsigned int altsetting)
{
	struct usb_host_interface *alts;
	struct usb_endpoint_descriptor *epd;

	alts = snd_usb_get_host_interface(chip, ifnum, altsetting);
	if (!alts)
		return 0;
	if (alts->desc.bInterfaceClass != USB_CLASS_AUDIO ||
	    alts->desc.bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING ||
	    alts->desc.bInterfaceProtocol != UAC_VERSION_2 ||
	    alts->desc.bNumEndpoints < 1)
		return 0;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_in(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_USAGE_MASK) !=
					USB_ENDPOINT_USAGE_IMPLICIT_FB)
		return 0;
	return add_implicit_fb_sync_ep(chip, fmt, epd->bEndpointAddress, 0,
				       ifnum, alts);
}

static bool roland_sanity_check_iface(struct usb_host_interface *alts)
{
	if (alts->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC ||
	    (alts->desc.bInterfaceSubClass != 2 &&
	     alts->desc.bInterfaceProtocol != 2) ||
	    alts->desc.bNumEndpoints < 1)
		return false;
	return true;
}

/* Like the UAC2 case above, but specific to Roland with vendor class and hack */
static int add_roland_implicit_fb(struct snd_usb_audio *chip,
				  struct audioformat *fmt,
				  struct usb_host_interface *alts)
{
	struct usb_endpoint_descriptor *epd;

	if (!roland_sanity_check_iface(alts))
		return 0;
	/* only when both streams are with ASYNC type */
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_out(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC)
		return 0;

	/* check capture EP */
	alts = snd_usb_get_host_interface(chip,
					  alts->desc.bInterfaceNumber + 1,
					  alts->desc.bAlternateSetting);
	if (!alts || !roland_sanity_check_iface(alts))
		return 0;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_in(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC)
		return 0;
	chip->playback_first = 1;
	return add_implicit_fb_sync_ep(chip, fmt, epd->bEndpointAddress, 0,
				       alts->desc.bInterfaceNumber, alts);
}

/* capture quirk for Roland device; always full-duplex */
static int add_roland_capture_quirk(struct snd_usb_audio *chip,
				    struct audioformat *fmt,
				    struct usb_host_interface *alts)
{
	struct usb_endpoint_descriptor *epd;

	if (!roland_sanity_check_iface(alts))
		return 0;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_in(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC)
		return 0;

	alts = snd_usb_get_host_interface(chip,
					  alts->desc.bInterfaceNumber - 1,
					  alts->desc.bAlternateSetting);
	if (!alts || !roland_sanity_check_iface(alts))
		return 0;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_out(epd))
		return 0;
	return add_implicit_fb_sync_ep(chip, fmt, epd->bEndpointAddress, 0,
				       alts->desc.bInterfaceNumber, alts);
}

/* Playback and capture EPs on Pioneer devices share the same iface/altset
 * for the implicit feedback operation
 */
static bool is_pioneer_implicit_fb(struct snd_usb_audio *chip,
				   struct usb_host_interface *alts)

{
	struct usb_endpoint_descriptor *epd;

	if (USB_ID_VENDOR(chip->usb_id) != 0x2b73 &&
	    USB_ID_VENDOR(chip->usb_id) != 0x08e4)
		return false;
	if (alts->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC)
		return false;
	if (alts->desc.bNumEndpoints != 2)
		return false;

	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_out(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC)
		return false;

	epd = get_endpoint(alts, 1);
	if (!usb_endpoint_is_isoc_in(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC ||
	    ((epd->bmAttributes & USB_ENDPOINT_USAGE_MASK) !=
	     USB_ENDPOINT_USAGE_DATA &&
	     (epd->bmAttributes & USB_ENDPOINT_USAGE_MASK) !=
	     USB_ENDPOINT_USAGE_IMPLICIT_FB))
		return false;

	return true;
}

static int __add_generic_implicit_fb(struct snd_usb_audio *chip,
				     struct audioformat *fmt,
				     int iface, int altset)
{
	struct usb_host_interface *alts;
	struct usb_endpoint_descriptor *epd;

	alts = snd_usb_get_host_interface(chip, iface, altset);
	if (!alts)
		return 0;

	if ((alts->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC &&
	     alts->desc.bInterfaceClass != USB_CLASS_AUDIO) ||
	    alts->desc.bNumEndpoints < 1)
		return 0;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_is_isoc_in(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC)
		return 0;
	return add_implicit_fb_sync_ep(chip, fmt, epd->bEndpointAddress, 0,
				       iface, alts);
}

/* More generic quirk: look for the sync EP next to the data EP */
static int add_generic_implicit_fb(struct snd_usb_audio *chip,
				   struct audioformat *fmt,
				   struct usb_host_interface *alts)
{
	if ((fmt->ep_attr & USB_ENDPOINT_SYNCTYPE) != USB_ENDPOINT_SYNC_ASYNC)
		return 0;

	if (__add_generic_implicit_fb(chip, fmt,
				      alts->desc.bInterfaceNumber + 1,
				      alts->desc.bAlternateSetting))
		return 1;
	return __add_generic_implicit_fb(chip, fmt,
					 alts->desc.bInterfaceNumber - 1,
					 alts->desc.bAlternateSetting);
}

static const struct snd_usb_implicit_fb_match *
find_implicit_fb_entry(struct snd_usb_audio *chip,
		       const struct snd_usb_implicit_fb_match *match,
		       const struct usb_host_interface *alts)
{
	for (; match->id; match++)
		if (match->id == chip->usb_id &&
		    (!match->iface_class ||
		     (alts->desc.bInterfaceClass == match->iface_class)))
			return match;

	return NULL;
}

/* Setup an implicit feedback endpoint from a quirk. Returns 0 if no quirk
 * applies. Returns 1 if a quirk was found.
 */
static int audioformat_implicit_fb_quirk(struct snd_usb_audio *chip,
					 struct audioformat *fmt,
					 struct usb_host_interface *alts)
{
	const struct snd_usb_implicit_fb_match *p;
	unsigned int attr = fmt->ep_attr & USB_ENDPOINT_SYNCTYPE;

	p = find_implicit_fb_entry(chip, playback_implicit_fb_quirks, alts);
	if (p) {
		switch (p->type) {
		case IMPLICIT_FB_GENERIC:
			return add_generic_implicit_fb(chip, fmt, alts);
		case IMPLICIT_FB_NONE:
			return 0; /* No quirk */
		case IMPLICIT_FB_FIXED:
			return add_implicit_fb_sync_ep(chip, fmt, p->ep_num, 0,
						       p->iface, NULL);
		}
	}

	/* Special handling for devices with capture quirks */
	p = find_implicit_fb_entry(chip, capture_implicit_fb_quirks, alts);
	if (p) {
		switch (p->type) {
		case IMPLICIT_FB_FIXED:
			return 0; /* no quirk */
		case IMPLICIT_FB_BOTH:
			chip->playback_first = 1;
			return add_generic_implicit_fb(chip, fmt, alts);
		}
	}

	/* Generic UAC2 implicit feedback */
	if (attr == USB_ENDPOINT_SYNC_ASYNC &&
	    alts->desc.bInterfaceClass == USB_CLASS_AUDIO &&
	    alts->desc.bInterfaceProtocol == UAC_VERSION_2 &&
	    alts->desc.bNumEndpoints == 1) {
		if (add_generic_uac2_implicit_fb(chip, fmt,
						 alts->desc.bInterfaceNumber + 1,
						 alts->desc.bAlternateSetting))
			return 1;
	}

	/* Roland/BOSS implicit feedback with vendor spec class */
	if (USB_ID_VENDOR(chip->usb_id) == 0x0582) {
		if (add_roland_implicit_fb(chip, fmt, alts) > 0)
			return 1;
	}

	/* Pioneer devices with vendor spec class */
	if (is_pioneer_implicit_fb(chip, alts)) {
		chip->playback_first = 1;
		return add_implicit_fb_sync_ep(chip, fmt,
					       get_endpoint(alts, 1)->bEndpointAddress,
					       1, alts->desc.bInterfaceNumber,
					       alts);
	}

	/* Try the generic implicit fb if available */
	if (chip->generic_implicit_fb)
		return add_generic_implicit_fb(chip, fmt, alts);

	/* No quirk */
	return 0;
}

/* same for capture, but only handling FIXED entry */
static int audioformat_capture_quirk(struct snd_usb_audio *chip,
				     struct audioformat *fmt,
				     struct usb_host_interface *alts)
{
	const struct snd_usb_implicit_fb_match *p;

	p = find_implicit_fb_entry(chip, capture_implicit_fb_quirks, alts);
	if (p && (p->type == IMPLICIT_FB_FIXED || p->type == IMPLICIT_FB_BOTH))
		return add_implicit_fb_sync_ep(chip, fmt, p->ep_num, 0,
					       p->iface, NULL);

	/* Roland/BOSS need full-duplex streams */
	if (USB_ID_VENDOR(chip->usb_id) == 0x0582) {
		if (add_roland_capture_quirk(chip, fmt, alts) > 0)
			return 1;
	}

	if (is_pioneer_implicit_fb(chip, alts))
		return 1; /* skip the quirk, also don't handle generic sync EP */
	return 0;
}

/*
 * Parse altset and set up implicit feedback endpoint on the audioformat
 */
int snd_usb_parse_implicit_fb_quirk(struct snd_usb_audio *chip,
				    struct audioformat *fmt,
				    struct usb_host_interface *alts)
{
	if (fmt->endpoint & USB_DIR_IN)
		return audioformat_capture_quirk(chip, fmt, alts);
	else
		return audioformat_implicit_fb_quirk(chip, fmt, alts);
}

/*
 * Return the score of matching two audioformats.
 * Veto the audioformat if:
 * - It has no channels for some reason.
 * - Requested PCM format is not supported.
 * - Requested sample rate is not supported.
 */
static int match_endpoint_audioformats(struct snd_usb_substream *subs,
				       const struct audioformat *fp,
				       int rate, int channels,
				       snd_pcm_format_t pcm_format)
{
	int i, score;

	if (fp->channels < 1)
		return 0;

	if (!(fp->formats & pcm_format_to_bits(pcm_format)))
		return 0;

	if (fp->rates & SNDRV_PCM_RATE_CONTINUOUS) {
		if (rate < fp->rate_min || rate > fp->rate_max)
			return 0;
	} else {
		for (i = 0; i < fp->nr_rates; i++) {
			if (fp->rate_table[i] == rate)
				break;
		}
		if (i >= fp->nr_rates)
			return 0;
	}

	score = 1;
	if (fp->channels == channels)
		score++;

	return score;
}

static struct snd_usb_substream *
find_matching_substream(struct snd_usb_audio *chip, int stream, int ep_num,
			int fmt_type)
{
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs;

	list_for_each_entry(as, &chip->pcm_list, list) {
		subs = &as->substream[stream];
		if (as->fmt_type == fmt_type && subs->ep_num == ep_num)
			return subs;
	}

	return NULL;
}

/*
 * Return the audioformat that is suitable for the implicit fb
 */
const struct audioformat *
snd_usb_find_implicit_fb_sync_format(struct snd_usb_audio *chip,
				     const struct audioformat *target,
				     const struct snd_pcm_hw_params *params,
				     int stream)
{
	struct snd_usb_substream *subs;
	const struct audioformat *fp, *sync_fmt = NULL;
	int score, high_score;

	/* Use the original audioformat as fallback for the shared altset */
	if (target->iface == target->sync_iface &&
	    target->altsetting == target->sync_altsetting)
		sync_fmt = target;

	subs = find_matching_substream(chip, stream, target->sync_ep,
				       target->fmt_type);
	if (!subs)
		return sync_fmt;

	high_score = 0;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		score = match_endpoint_audioformats(subs, fp,
						    params_rate(params),
						    params_channels(params),
						    params_format(params));
		if (score > high_score) {
			sync_fmt = fp;
			high_score = score;
		}
	}

	return sync_fmt;
}

