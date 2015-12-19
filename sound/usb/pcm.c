/*
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
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/bitrev.h>
#include <linux/ratelimit.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "usbaudio.h"
#include "card.h"
#include "quirks.h"
#include "debug.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"
#include "clock.h"
#include "power.h"

#define SUBSTREAM_FLAG_DATA_EP_STARTED	0
#define SUBSTREAM_FLAG_SYNC_EP_STARTED	1

/* return the estimated delay based on USB frame counters */
snd_pcm_uframes_t snd_usb_pcm_delay(struct snd_usb_substream *subs,
				    unsigned int rate)
{
	int current_frame_number;
	int frame_diff;
	int est_delay;

	if (!subs->last_delay)
		return 0; /* short path */

	current_frame_number = usb_get_current_frame_number(subs->dev);
	/*
	 * HCD implementations use different widths, use lower 8 bits.
	 * The delay will be managed up to 256ms, which is more than
	 * enough
	 */
	frame_diff = (current_frame_number - subs->last_frame_number) & 0xff;

	/* Approximation based on number of samples per USB frame (ms),
	   some truncation for 44.1 but the estimate is good enough */
	est_delay =  frame_diff * rate / 1000;
	if (subs->direction == SNDRV_PCM_STREAM_PLAYBACK)
		est_delay = subs->last_delay - est_delay;
	else
		est_delay = subs->last_delay + est_delay;

	if (est_delay < 0)
		est_delay = 0;
	return est_delay;
}

/*
 * return the current pcm pointer.  just based on the hwptr_done value.
 */
static snd_pcm_uframes_t snd_usb_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_usb_substream *subs;
	unsigned int hwptr_done;

	subs = (struct snd_usb_substream *)substream->runtime->private_data;
	if (atomic_read(&subs->stream->chip->shutdown))
		return SNDRV_PCM_POS_XRUN;
	spin_lock(&subs->lock);
	hwptr_done = subs->hwptr_done;
	substream->runtime->delay = snd_usb_pcm_delay(subs,
						substream->runtime->rate);
	spin_unlock(&subs->lock);
	return hwptr_done / (substream->runtime->frame_bits >> 3);
}

/*
 * find a matching audio format
 */
static struct audioformat *find_format(struct snd_usb_substream *subs)
{
	struct audioformat *fp;
	struct audioformat *found = NULL;
	int cur_attr = 0, attr;

	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (!(fp->formats & pcm_format_to_bits(subs->pcm_format)))
			continue;
		if (fp->channels != subs->channels)
			continue;
		if (subs->cur_rate < fp->rate_min ||
		    subs->cur_rate > fp->rate_max)
			continue;
		if (! (fp->rates & SNDRV_PCM_RATE_CONTINUOUS)) {
			unsigned int i;
			for (i = 0; i < fp->nr_rates; i++)
				if (fp->rate_table[i] == subs->cur_rate)
					break;
			if (i >= fp->nr_rates)
				continue;
		}
		attr = fp->ep_attr & USB_ENDPOINT_SYNCTYPE;
		if (! found) {
			found = fp;
			cur_attr = attr;
			continue;
		}
		/* avoid async out and adaptive in if the other method
		 * supports the same format.
		 * this is a workaround for the case like
		 * M-audio audiophile USB.
		 */
		if (attr != cur_attr) {
			if ((attr == USB_ENDPOINT_SYNC_ASYNC &&
			     subs->direction == SNDRV_PCM_STREAM_PLAYBACK) ||
			    (attr == USB_ENDPOINT_SYNC_ADAPTIVE &&
			     subs->direction == SNDRV_PCM_STREAM_CAPTURE))
				continue;
			if ((cur_attr == USB_ENDPOINT_SYNC_ASYNC &&
			     subs->direction == SNDRV_PCM_STREAM_PLAYBACK) ||
			    (cur_attr == USB_ENDPOINT_SYNC_ADAPTIVE &&
			     subs->direction == SNDRV_PCM_STREAM_CAPTURE)) {
				found = fp;
				cur_attr = attr;
				continue;
			}
		}
		/* find the format with the largest max. packet size */
		if (fp->maxpacksize > found->maxpacksize) {
			found = fp;
			cur_attr = attr;
		}
	}
	return found;
}

static int init_pitch_v1(struct snd_usb_audio *chip, int iface,
			 struct usb_host_interface *alts,
			 struct audioformat *fmt)
{
	struct usb_device *dev = chip->dev;
	unsigned int ep;
	unsigned char data[1];
	int err;

	ep = get_endpoint(alts, 0)->bEndpointAddress;

	data[0] = 1;
	if ((err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
				   USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT,
				   UAC_EP_CS_ATTR_PITCH_CONTROL << 8, ep,
				   data, sizeof(data))) < 0) {
		usb_audio_err(chip, "%d:%d: cannot set enable PITCH\n",
			      iface, ep);
		return err;
	}

	return 0;
}

static int init_pitch_v2(struct snd_usb_audio *chip, int iface,
			 struct usb_host_interface *alts,
			 struct audioformat *fmt)
{
	struct usb_device *dev = chip->dev;
	unsigned char data[1];
	int err;

	data[0] = 1;
	if ((err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC2_CS_CUR,
				   USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_OUT,
				   UAC2_EP_CS_PITCH << 8, 0,
				   data, sizeof(data))) < 0) {
		usb_audio_err(chip, "%d:%d: cannot set enable PITCH (v2)\n",
			      iface, fmt->altsetting);
		return err;
	}

	return 0;
}

/*
 * initialize the pitch control and sample rate
 */
int snd_usb_init_pitch(struct snd_usb_audio *chip, int iface,
		       struct usb_host_interface *alts,
		       struct audioformat *fmt)
{
	/* if endpoint doesn't have pitch control, bail out */
	if (!(fmt->attributes & UAC_EP_CS_ATTR_PITCH_CONTROL))
		return 0;

	switch (fmt->protocol) {
	case UAC_VERSION_1:
	default:
		return init_pitch_v1(chip, iface, alts, fmt);

	case UAC_VERSION_2:
		return init_pitch_v2(chip, iface, alts, fmt);
	}
}

static int start_endpoints(struct snd_usb_substream *subs, bool can_sleep)
{
	int err;

	if (!subs->data_endpoint)
		return -EINVAL;

	if (!test_and_set_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags)) {
		struct snd_usb_endpoint *ep = subs->data_endpoint;

		dev_dbg(&subs->dev->dev, "Starting data EP @%p\n", ep);

		ep->data_subs = subs;
		err = snd_usb_endpoint_start(ep, can_sleep);
		if (err < 0) {
			clear_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags);
			return err;
		}
	}

	if (subs->sync_endpoint &&
	    !test_and_set_bit(SUBSTREAM_FLAG_SYNC_EP_STARTED, &subs->flags)) {
		struct snd_usb_endpoint *ep = subs->sync_endpoint;

		if (subs->data_endpoint->iface != subs->sync_endpoint->iface ||
		    subs->data_endpoint->altsetting != subs->sync_endpoint->altsetting) {
			err = usb_set_interface(subs->dev,
						subs->sync_endpoint->iface,
						subs->sync_endpoint->altsetting);
			if (err < 0) {
				clear_bit(SUBSTREAM_FLAG_SYNC_EP_STARTED, &subs->flags);
				dev_err(&subs->dev->dev,
					   "%d:%d: cannot set interface (%d)\n",
					   subs->sync_endpoint->iface,
					   subs->sync_endpoint->altsetting, err);
				return -EIO;
			}
		}

		dev_dbg(&subs->dev->dev, "Starting sync EP @%p\n", ep);

		ep->sync_slave = subs->data_endpoint;
		err = snd_usb_endpoint_start(ep, can_sleep);
		if (err < 0) {
			clear_bit(SUBSTREAM_FLAG_SYNC_EP_STARTED, &subs->flags);
			return err;
		}
	}

	return 0;
}

static void stop_endpoints(struct snd_usb_substream *subs, bool wait)
{
	if (test_and_clear_bit(SUBSTREAM_FLAG_SYNC_EP_STARTED, &subs->flags))
		snd_usb_endpoint_stop(subs->sync_endpoint);

	if (test_and_clear_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags))
		snd_usb_endpoint_stop(subs->data_endpoint);

	if (wait) {
		snd_usb_endpoint_sync_pending_stop(subs->sync_endpoint);
		snd_usb_endpoint_sync_pending_stop(subs->data_endpoint);
	}
}

static int search_roland_implicit_fb(struct usb_device *dev, int ifnum,
				     unsigned int altsetting,
				     struct usb_host_interface **alts,
				     unsigned int *ep)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *altsd;
	struct usb_endpoint_descriptor *epd;

	iface = usb_ifnum_to_if(dev, ifnum);
	if (!iface || iface->num_altsetting < altsetting + 1)
		return -ENOENT;
	*alts = &iface->altsetting[altsetting];
	altsd = get_iface_desc(*alts);
	if (altsd->bAlternateSetting != altsetting ||
	    altsd->bInterfaceClass != USB_CLASS_VENDOR_SPEC ||
	    (altsd->bInterfaceSubClass != 2 &&
	     altsd->bInterfaceProtocol != 2   ) ||
	    altsd->bNumEndpoints < 1)
		return -ENOENT;
	epd = get_endpoint(*alts, 0);
	if (!usb_endpoint_is_isoc_in(epd) ||
	    (epd->bmAttributes & USB_ENDPOINT_USAGE_MASK) !=
					USB_ENDPOINT_USAGE_IMPLICIT_FB)
		return -ENOENT;
	*ep = epd->bEndpointAddress;
	return 0;
}

static int set_sync_ep_implicit_fb_quirk(struct snd_usb_substream *subs,
					 struct usb_device *dev,
					 struct usb_interface_descriptor *altsd,
					 unsigned int attr)
{
	struct usb_host_interface *alts;
	struct usb_interface *iface;
	unsigned int ep;

	/* Implicit feedback sync EPs consumers are always playback EPs */
	if (subs->direction != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	switch (subs->stream->chip->usb_id) {
	case USB_ID(0x0763, 0x2030): /* M-Audio Fast Track C400 */
	case USB_ID(0x0763, 0x2031): /* M-Audio Fast Track C600 */
		ep = 0x81;
		iface = usb_ifnum_to_if(dev, 3);

		if (!iface || iface->num_altsetting == 0)
			return -EINVAL;

		alts = &iface->altsetting[1];
		goto add_sync_ep;
		break;
	case USB_ID(0x0763, 0x2080): /* M-Audio FastTrack Ultra */
	case USB_ID(0x0763, 0x2081):
		ep = 0x81;
		iface = usb_ifnum_to_if(dev, 2);

		if (!iface || iface->num_altsetting == 0)
			return -EINVAL;

		alts = &iface->altsetting[1];
		goto add_sync_ep;
	}
	if (attr == USB_ENDPOINT_SYNC_ASYNC &&
	    altsd->bInterfaceClass == USB_CLASS_VENDOR_SPEC &&
	    altsd->bInterfaceProtocol == 2 &&
	    altsd->bNumEndpoints == 1 &&
	    USB_ID_VENDOR(subs->stream->chip->usb_id) == 0x0582 /* Roland */ &&
	    search_roland_implicit_fb(dev, altsd->bInterfaceNumber + 1,
				      altsd->bAlternateSetting,
				      &alts, &ep) >= 0) {
		goto add_sync_ep;
	}

	/* No quirk */
	return 0;

add_sync_ep:
	subs->sync_endpoint = snd_usb_add_endpoint(subs->stream->chip,
						   alts, ep, !subs->direction,
						   SND_USB_ENDPOINT_TYPE_DATA);
	if (!subs->sync_endpoint)
		return -EINVAL;

	subs->data_endpoint->sync_master = subs->sync_endpoint;

	return 0;
}

static int set_sync_endpoint(struct snd_usb_substream *subs,
			     struct audioformat *fmt,
			     struct usb_device *dev,
			     struct usb_host_interface *alts,
			     struct usb_interface_descriptor *altsd)
{
	int is_playback = subs->direction == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int ep, attr;
	bool implicit_fb;
	int err;

	/* we need a sync pipe in async OUT or adaptive IN mode */
	/* check the number of EP, since some devices have broken
	 * descriptors which fool us.  if it has only one EP,
	 * assume it as adaptive-out or sync-in.
	 */
	attr = fmt->ep_attr & USB_ENDPOINT_SYNCTYPE;

	if ((is_playback && (attr != USB_ENDPOINT_SYNC_ASYNC)) ||
		(!is_playback && (attr != USB_ENDPOINT_SYNC_ADAPTIVE))) {

		/*
		 * In these modes the notion of sync_endpoint is irrelevant.
		 * Reset pointers to avoid using stale data from previously
		 * used settings, e.g. when configuration and endpoints were
		 * changed
		 */

		subs->sync_endpoint = NULL;
		subs->data_endpoint->sync_master = NULL;
	}

	err = set_sync_ep_implicit_fb_quirk(subs, dev, altsd, attr);
	if (err < 0)
		return err;

	if (altsd->bNumEndpoints < 2)
		return 0;

	if ((is_playback && (attr == USB_ENDPOINT_SYNC_SYNC ||
			     attr == USB_ENDPOINT_SYNC_ADAPTIVE)) ||
	    (!is_playback && attr != USB_ENDPOINT_SYNC_ADAPTIVE))
		return 0;

	/*
	 * In case of illegal SYNC_NONE for OUT endpoint, we keep going to see
	 * if we don't find a sync endpoint, as on M-Audio Transit. In case of
	 * error fall back to SYNC mode and don't create sync endpoint
	 */

	/* check sync-pipe endpoint */
	/* ... and check descriptor size before accessing bSynchAddress
	   because there is a version of the SB Audigy 2 NX firmware lacking
	   the audio fields in the endpoint descriptors */
	if ((get_endpoint(alts, 1)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC ||
	    (get_endpoint(alts, 1)->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE &&
	     get_endpoint(alts, 1)->bSynchAddress != 0)) {
		dev_err(&dev->dev,
			"%d:%d : invalid sync pipe. bmAttributes %02x, bLength %d, bSynchAddress %02x\n",
			   fmt->iface, fmt->altsetting,
			   get_endpoint(alts, 1)->bmAttributes,
			   get_endpoint(alts, 1)->bLength,
			   get_endpoint(alts, 1)->bSynchAddress);
		if (is_playback && attr == USB_ENDPOINT_SYNC_NONE)
			return 0;
		return -EINVAL;
	}
	ep = get_endpoint(alts, 1)->bEndpointAddress;
	if (get_endpoint(alts, 0)->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE &&
	    ((is_playback && ep != (unsigned int)(get_endpoint(alts, 0)->bSynchAddress | USB_DIR_IN)) ||
	     (!is_playback && ep != (unsigned int)(get_endpoint(alts, 0)->bSynchAddress & ~USB_DIR_IN)))) {
		dev_err(&dev->dev,
			"%d:%d : invalid sync pipe. is_playback %d, ep %02x, bSynchAddress %02x\n",
			   fmt->iface, fmt->altsetting,
			   is_playback, ep, get_endpoint(alts, 0)->bSynchAddress);
		if (is_playback && attr == USB_ENDPOINT_SYNC_NONE)
			return 0;
		return -EINVAL;
	}

	implicit_fb = (get_endpoint(alts, 1)->bmAttributes & USB_ENDPOINT_USAGE_MASK)
			== USB_ENDPOINT_USAGE_IMPLICIT_FB;

	subs->sync_endpoint = snd_usb_add_endpoint(subs->stream->chip,
						   alts, ep, !subs->direction,
						   implicit_fb ?
							SND_USB_ENDPOINT_TYPE_DATA :
							SND_USB_ENDPOINT_TYPE_SYNC);
	if (!subs->sync_endpoint) {
		if (is_playback && attr == USB_ENDPOINT_SYNC_NONE)
			return 0;
		return -EINVAL;
	}

	subs->data_endpoint->sync_master = subs->sync_endpoint;

	return 0;
}

/*
 * find a matching format and set up the interface
 */
static int set_format(struct snd_usb_substream *subs, struct audioformat *fmt)
{
	struct usb_device *dev = subs->dev;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct usb_interface *iface;
	int err;

	iface = usb_ifnum_to_if(dev, fmt->iface);
	if (WARN_ON(!iface))
		return -EINVAL;
	alts = &iface->altsetting[fmt->altset_idx];
	altsd = get_iface_desc(alts);
	if (WARN_ON(altsd->bAlternateSetting != fmt->altsetting))
		return -EINVAL;

	if (fmt == subs->cur_audiofmt)
		return 0;

	/* close the old interface */
	if (subs->interface >= 0 && subs->interface != fmt->iface) {
		err = usb_set_interface(subs->dev, subs->interface, 0);
		if (err < 0) {
			dev_err(&dev->dev,
				"%d:%d: return to setting 0 failed (%d)\n",
				fmt->iface, fmt->altsetting, err);
			return -EIO;
		}
		subs->interface = -1;
		subs->altset_idx = 0;
	}

	/* set interface */
	if (subs->interface != fmt->iface ||
	    subs->altset_idx != fmt->altset_idx) {

		err = snd_usb_select_mode_quirk(subs, fmt);
		if (err < 0)
			return -EIO;

		err = usb_set_interface(dev, fmt->iface, fmt->altsetting);
		if (err < 0) {
			dev_err(&dev->dev,
				"%d:%d: usb_set_interface failed (%d)\n",
				fmt->iface, fmt->altsetting, err);
			return -EIO;
		}
		dev_dbg(&dev->dev, "setting usb interface %d:%d\n",
			fmt->iface, fmt->altsetting);
		subs->interface = fmt->iface;
		subs->altset_idx = fmt->altset_idx;

		snd_usb_set_interface_quirk(dev);
	}

	subs->data_endpoint = snd_usb_add_endpoint(subs->stream->chip,
						   alts, fmt->endpoint, subs->direction,
						   SND_USB_ENDPOINT_TYPE_DATA);

	if (!subs->data_endpoint)
		return -EINVAL;

	err = set_sync_endpoint(subs, fmt, dev, alts, altsd);
	if (err < 0)
		return err;

	err = snd_usb_init_pitch(subs->stream->chip, fmt->iface, alts, fmt);
	if (err < 0)
		return err;

	subs->cur_audiofmt = fmt;

	snd_usb_set_format_quirk(subs, fmt);

	return 0;
}

/*
 * Return the score of matching two audioformats.
 * Veto the audioformat if:
 * - It has no channels for some reason.
 * - Requested PCM format is not supported.
 * - Requested sample rate is not supported.
 */
static int match_endpoint_audioformats(struct snd_usb_substream *subs,
				       struct audioformat *fp,
				       struct audioformat *match, int rate,
				       snd_pcm_format_t pcm_format)
{
	int i;
	int score = 0;

	if (fp->channels < 1) {
		dev_dbg(&subs->dev->dev,
			"%s: (fmt @%p) no channels\n", __func__, fp);
		return 0;
	}

	if (!(fp->formats & pcm_format_to_bits(pcm_format))) {
		dev_dbg(&subs->dev->dev,
			"%s: (fmt @%p) no match for format %d\n", __func__,
			fp, pcm_format);
		return 0;
	}

	for (i = 0; i < fp->nr_rates; i++) {
		if (fp->rate_table[i] == rate) {
			score++;
			break;
		}
	}
	if (!score) {
		dev_dbg(&subs->dev->dev,
			"%s: (fmt @%p) no match for rate %d\n", __func__,
			fp, rate);
		return 0;
	}

	if (fp->channels == match->channels)
		score++;

	dev_dbg(&subs->dev->dev,
		"%s: (fmt @%p) score %d\n", __func__, fp, score);

	return score;
}

/*
 * Configure the sync ep using the rate and pcm format of the data ep.
 */
static int configure_sync_endpoint(struct snd_usb_substream *subs)
{
	int ret;
	struct audioformat *fp;
	struct audioformat *sync_fp = NULL;
	int cur_score = 0;
	int sync_period_bytes = subs->period_bytes;
	struct snd_usb_substream *sync_subs =
		&subs->stream->substream[subs->direction ^ 1];

	if (subs->sync_endpoint->type != SND_USB_ENDPOINT_TYPE_DATA ||
	    !subs->stream)
		return snd_usb_endpoint_set_params(subs->sync_endpoint,
						   subs->pcm_format,
						   subs->channels,
						   subs->period_bytes,
						   0, 0,
						   subs->cur_rate,
						   subs->cur_audiofmt,
						   NULL);

	/* Try to find the best matching audioformat. */
	list_for_each_entry(fp, &sync_subs->fmt_list, list) {
		int score = match_endpoint_audioformats(subs,
							fp, subs->cur_audiofmt,
			subs->cur_rate, subs->pcm_format);

		if (score > cur_score) {
			sync_fp = fp;
			cur_score = score;
		}
	}

	if (unlikely(sync_fp == NULL)) {
		dev_err(&subs->dev->dev,
			"%s: no valid audioformat for sync ep %x found\n",
			__func__, sync_subs->ep_num);
		return -EINVAL;
	}

	/*
	 * Recalculate the period bytes if channel number differ between
	 * data and sync ep audioformat.
	 */
	if (sync_fp->channels != subs->channels) {
		sync_period_bytes = (subs->period_bytes / subs->channels) *
			sync_fp->channels;
		dev_dbg(&subs->dev->dev,
			"%s: adjusted sync ep period bytes (%d -> %d)\n",
			__func__, subs->period_bytes, sync_period_bytes);
	}

	ret = snd_usb_endpoint_set_params(subs->sync_endpoint,
					  subs->pcm_format,
					  sync_fp->channels,
					  sync_period_bytes,
					  0, 0,
					  subs->cur_rate,
					  sync_fp,
					  NULL);

	return ret;
}

/*
 * configure endpoint params
 *
 * called  during initial setup and upon resume
 */
static int configure_endpoint(struct snd_usb_substream *subs)
{
	int ret;

	/* format changed */
	stop_endpoints(subs, true);
	ret = snd_usb_endpoint_set_params(subs->data_endpoint,
					  subs->pcm_format,
					  subs->channels,
					  subs->period_bytes,
					  subs->period_frames,
					  subs->buffer_periods,
					  subs->cur_rate,
					  subs->cur_audiofmt,
					  subs->sync_endpoint);
	if (ret < 0)
		return ret;

	if (subs->sync_endpoint)
		ret = configure_sync_endpoint(subs);

	return ret;
}

/*
 * hw_params callback
 *
 * allocate a buffer and set the given audio format.
 *
 * so far we use a physically linear buffer although packetize transfer
 * doesn't need a continuous area.
 * if sg buffer is supported on the later version of alsa, we'll follow
 * that.
 */
static int snd_usb_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;
	struct audioformat *fmt;
	int ret;

	ret = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;

	subs->pcm_format = params_format(hw_params);
	subs->period_bytes = params_period_bytes(hw_params);
	subs->period_frames = params_period_size(hw_params);
	subs->buffer_periods = params_periods(hw_params);
	subs->channels = params_channels(hw_params);
	subs->cur_rate = params_rate(hw_params);

	fmt = find_format(subs);
	if (!fmt) {
		dev_dbg(&subs->dev->dev,
			"cannot set format: format = %#x, rate = %d, channels = %d\n",
			   subs->pcm_format, subs->cur_rate, subs->channels);
		return -EINVAL;
	}

	ret = snd_usb_lock_shutdown(subs->stream->chip);
	if (ret < 0)
		return ret;
	ret = set_format(subs, fmt);
	snd_usb_unlock_shutdown(subs->stream->chip);
	if (ret < 0)
		return ret;

	subs->interface = fmt->iface;
	subs->altset_idx = fmt->altset_idx;
	subs->need_setup_ep = true;

	return 0;
}

/*
 * hw_free callback
 *
 * reset the audio format and release the buffer
 */
static int snd_usb_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;

	subs->cur_audiofmt = NULL;
	subs->cur_rate = 0;
	subs->period_bytes = 0;
	if (!snd_usb_lock_shutdown(subs->stream->chip)) {
		stop_endpoints(subs, true);
		snd_usb_endpoint_deactivate(subs->sync_endpoint);
		snd_usb_endpoint_deactivate(subs->data_endpoint);
		snd_usb_unlock_shutdown(subs->stream->chip);
	}
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

/*
 * prepare callback
 *
 * only a few subtle things...
 */
static int snd_usb_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usb_substream *subs = runtime->private_data;
	struct usb_host_interface *alts;
	struct usb_interface *iface;
	int ret;

	if (! subs->cur_audiofmt) {
		dev_err(&subs->dev->dev, "no format is specified!\n");
		return -ENXIO;
	}

	ret = snd_usb_lock_shutdown(subs->stream->chip);
	if (ret < 0)
		return ret;
	if (snd_BUG_ON(!subs->data_endpoint)) {
		ret = -EIO;
		goto unlock;
	}

	snd_usb_endpoint_sync_pending_stop(subs->sync_endpoint);
	snd_usb_endpoint_sync_pending_stop(subs->data_endpoint);

	ret = set_format(subs, subs->cur_audiofmt);
	if (ret < 0)
		goto unlock;

	iface = usb_ifnum_to_if(subs->dev, subs->cur_audiofmt->iface);
	alts = &iface->altsetting[subs->cur_audiofmt->altset_idx];
	ret = snd_usb_init_sample_rate(subs->stream->chip,
				       subs->cur_audiofmt->iface,
				       alts,
				       subs->cur_audiofmt,
				       subs->cur_rate);
	if (ret < 0)
		goto unlock;

	if (subs->need_setup_ep) {
		ret = configure_endpoint(subs);
		if (ret < 0)
			goto unlock;
		subs->need_setup_ep = false;
	}

	/* some unit conversions in runtime */
	subs->data_endpoint->maxframesize =
		bytes_to_frames(runtime, subs->data_endpoint->maxpacksize);
	subs->data_endpoint->curframesize =
		bytes_to_frames(runtime, subs->data_endpoint->curpacksize);

	/* reset the pointer */
	subs->hwptr_done = 0;
	subs->transfer_done = 0;
	subs->last_delay = 0;
	subs->last_frame_number = 0;
	runtime->delay = 0;

	/* for playback, submit the URBs now; otherwise, the first hwptr_done
	 * updates for all URBs would happen at the same time when starting */
	if (subs->direction == SNDRV_PCM_STREAM_PLAYBACK)
		ret = start_endpoints(subs, true);

 unlock:
	snd_usb_unlock_shutdown(subs->stream->chip);
	return ret;
}

static struct snd_pcm_hardware snd_usb_hardware =
{
	.info =			SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_BATCH |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_PAUSE,
	.buffer_bytes_max =	1024 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	512 * 1024,
	.periods_min =		2,
	.periods_max =		1024,
};

static int hw_check_valid_format(struct snd_usb_substream *subs,
				 struct snd_pcm_hw_params *params,
				 struct audioformat *fp)
{
	struct snd_interval *it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *ct = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmts = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_interval *pt = hw_param_interval(params, SNDRV_PCM_HW_PARAM_PERIOD_TIME);
	struct snd_mask check_fmts;
	unsigned int ptime;

	/* check the format */
	snd_mask_none(&check_fmts);
	check_fmts.bits[0] = (u32)fp->formats;
	check_fmts.bits[1] = (u32)(fp->formats >> 32);
	snd_mask_intersect(&check_fmts, fmts);
	if (snd_mask_empty(&check_fmts)) {
		hwc_debug("   > check: no supported format %d\n", fp->format);
		return 0;
	}
	/* check the channels */
	if (fp->channels < ct->min || fp->channels > ct->max) {
		hwc_debug("   > check: no valid channels %d (%d/%d)\n", fp->channels, ct->min, ct->max);
		return 0;
	}
	/* check the rate is within the range */
	if (fp->rate_min > it->max || (fp->rate_min == it->max && it->openmax)) {
		hwc_debug("   > check: rate_min %d > max %d\n", fp->rate_min, it->max);
		return 0;
	}
	if (fp->rate_max < it->min || (fp->rate_max == it->min && it->openmin)) {
		hwc_debug("   > check: rate_max %d < min %d\n", fp->rate_max, it->min);
		return 0;
	}
	/* check whether the period time is >= the data packet interval */
	if (subs->speed != USB_SPEED_FULL) {
		ptime = 125 * (1 << fp->datainterval);
		if (ptime > pt->max || (ptime == pt->max && pt->openmax)) {
			hwc_debug("   > check: ptime %u > max %u\n", ptime, pt->max);
			return 0;
		}
	}
	return 1;
}

static int hw_rule_rate(struct snd_pcm_hw_params *params,
			struct snd_pcm_hw_rule *rule)
{
	struct snd_usb_substream *subs = rule->private;
	struct audioformat *fp;
	struct snd_interval *it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	unsigned int rmin, rmax;
	int changed;

	hwc_debug("hw_rule_rate: (%d,%d)\n", it->min, it->max);
	changed = 0;
	rmin = rmax = 0;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (!hw_check_valid_format(subs, params, fp))
			continue;
		if (changed++) {
			if (rmin > fp->rate_min)
				rmin = fp->rate_min;
			if (rmax < fp->rate_max)
				rmax = fp->rate_max;
		} else {
			rmin = fp->rate_min;
			rmax = fp->rate_max;
		}
	}

	if (!changed) {
		hwc_debug("  --> get empty\n");
		it->empty = 1;
		return -EINVAL;
	}

	changed = 0;
	if (it->min < rmin) {
		it->min = rmin;
		it->openmin = 0;
		changed = 1;
	}
	if (it->max > rmax) {
		it->max = rmax;
		it->openmax = 0;
		changed = 1;
	}
	if (snd_interval_checkempty(it)) {
		it->empty = 1;
		return -EINVAL;
	}
	hwc_debug("  --> (%d, %d) (changed = %d)\n", it->min, it->max, changed);
	return changed;
}


static int hw_rule_channels(struct snd_pcm_hw_params *params,
			    struct snd_pcm_hw_rule *rule)
{
	struct snd_usb_substream *subs = rule->private;
	struct audioformat *fp;
	struct snd_interval *it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	unsigned int rmin, rmax;
	int changed;

	hwc_debug("hw_rule_channels: (%d,%d)\n", it->min, it->max);
	changed = 0;
	rmin = rmax = 0;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (!hw_check_valid_format(subs, params, fp))
			continue;
		if (changed++) {
			if (rmin > fp->channels)
				rmin = fp->channels;
			if (rmax < fp->channels)
				rmax = fp->channels;
		} else {
			rmin = fp->channels;
			rmax = fp->channels;
		}
	}

	if (!changed) {
		hwc_debug("  --> get empty\n");
		it->empty = 1;
		return -EINVAL;
	}

	changed = 0;
	if (it->min < rmin) {
		it->min = rmin;
		it->openmin = 0;
		changed = 1;
	}
	if (it->max > rmax) {
		it->max = rmax;
		it->openmax = 0;
		changed = 1;
	}
	if (snd_interval_checkempty(it)) {
		it->empty = 1;
		return -EINVAL;
	}
	hwc_debug("  --> (%d, %d) (changed = %d)\n", it->min, it->max, changed);
	return changed;
}

static int hw_rule_format(struct snd_pcm_hw_params *params,
			  struct snd_pcm_hw_rule *rule)
{
	struct snd_usb_substream *subs = rule->private;
	struct audioformat *fp;
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	u64 fbits;
	u32 oldbits[2];
	int changed;

	hwc_debug("hw_rule_format: %x:%x\n", fmt->bits[0], fmt->bits[1]);
	fbits = 0;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (!hw_check_valid_format(subs, params, fp))
			continue;
		fbits |= fp->formats;
	}

	oldbits[0] = fmt->bits[0];
	oldbits[1] = fmt->bits[1];
	fmt->bits[0] &= (u32)fbits;
	fmt->bits[1] &= (u32)(fbits >> 32);
	if (!fmt->bits[0] && !fmt->bits[1]) {
		hwc_debug("  --> get empty\n");
		return -EINVAL;
	}
	changed = (oldbits[0] != fmt->bits[0] || oldbits[1] != fmt->bits[1]);
	hwc_debug("  --> %x:%x (changed = %d)\n", fmt->bits[0], fmt->bits[1], changed);
	return changed;
}

static int hw_rule_period_time(struct snd_pcm_hw_params *params,
			       struct snd_pcm_hw_rule *rule)
{
	struct snd_usb_substream *subs = rule->private;
	struct audioformat *fp;
	struct snd_interval *it;
	unsigned char min_datainterval;
	unsigned int pmin;
	int changed;

	it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_PERIOD_TIME);
	hwc_debug("hw_rule_period_time: (%u,%u)\n", it->min, it->max);
	min_datainterval = 0xff;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (!hw_check_valid_format(subs, params, fp))
			continue;
		min_datainterval = min(min_datainterval, fp->datainterval);
	}
	if (min_datainterval == 0xff) {
		hwc_debug("  --> get empty\n");
		it->empty = 1;
		return -EINVAL;
	}
	pmin = 125 * (1 << min_datainterval);
	changed = 0;
	if (it->min < pmin) {
		it->min = pmin;
		it->openmin = 0;
		changed = 1;
	}
	if (snd_interval_checkempty(it)) {
		it->empty = 1;
		return -EINVAL;
	}
	hwc_debug("  --> (%u,%u) (changed = %d)\n", it->min, it->max, changed);
	return changed;
}

/*
 *  If the device supports unusual bit rates, does the request meet these?
 */
static int snd_usb_pcm_check_knot(struct snd_pcm_runtime *runtime,
				  struct snd_usb_substream *subs)
{
	struct audioformat *fp;
	int *rate_list;
	int count = 0, needs_knot = 0;
	int err;

	kfree(subs->rate_list.list);
	subs->rate_list.list = NULL;

	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (fp->rates & SNDRV_PCM_RATE_CONTINUOUS)
			return 0;
		count += fp->nr_rates;
		if (fp->rates & SNDRV_PCM_RATE_KNOT)
			needs_knot = 1;
	}
	if (!needs_knot)
		return 0;

	subs->rate_list.list = rate_list =
		kmalloc(sizeof(int) * count, GFP_KERNEL);
	if (!subs->rate_list.list)
		return -ENOMEM;
	subs->rate_list.count = count;
	subs->rate_list.mask = 0;
	count = 0;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		int i;
		for (i = 0; i < fp->nr_rates; i++)
			rate_list[count++] = fp->rate_table[i];
	}
	err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					 &subs->rate_list);
	if (err < 0)
		return err;

	return 0;
}


/*
 * set up the runtime hardware information.
 */

static int setup_hw_info(struct snd_pcm_runtime *runtime, struct snd_usb_substream *subs)
{
	struct audioformat *fp;
	unsigned int pt, ptmin;
	int param_period_time_if_needed;
	int err;

	runtime->hw.formats = subs->formats;

	runtime->hw.rate_min = 0x7fffffff;
	runtime->hw.rate_max = 0;
	runtime->hw.channels_min = 256;
	runtime->hw.channels_max = 0;
	runtime->hw.rates = 0;
	ptmin = UINT_MAX;
	/* check min/max rates and channels */
	list_for_each_entry(fp, &subs->fmt_list, list) {
		runtime->hw.rates |= fp->rates;
		if (runtime->hw.rate_min > fp->rate_min)
			runtime->hw.rate_min = fp->rate_min;
		if (runtime->hw.rate_max < fp->rate_max)
			runtime->hw.rate_max = fp->rate_max;
		if (runtime->hw.channels_min > fp->channels)
			runtime->hw.channels_min = fp->channels;
		if (runtime->hw.channels_max < fp->channels)
			runtime->hw.channels_max = fp->channels;
		if (fp->fmt_type == UAC_FORMAT_TYPE_II && fp->frame_size > 0) {
			/* FIXME: there might be more than one audio formats... */
			runtime->hw.period_bytes_min = runtime->hw.period_bytes_max =
				fp->frame_size;
		}
		pt = 125 * (1 << fp->datainterval);
		ptmin = min(ptmin, pt);
	}
	err = snd_usb_autoresume(subs->stream->chip);
	if (err < 0)
		return err;

	param_period_time_if_needed = SNDRV_PCM_HW_PARAM_PERIOD_TIME;
	if (subs->speed == USB_SPEED_FULL)
		/* full speed devices have fixed data packet interval */
		ptmin = 1000;
	if (ptmin == 1000)
		/* if period time doesn't go below 1 ms, no rules needed */
		param_period_time_if_needed = -1;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME,
				     ptmin, UINT_MAX);

	if ((err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				       hw_rule_rate, subs,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       param_period_time_if_needed,
				       -1)) < 0)
		goto rep_err;
	if ((err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				       hw_rule_channels, subs,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       SNDRV_PCM_HW_PARAM_RATE,
				       param_period_time_if_needed,
				       -1)) < 0)
		goto rep_err;
	if ((err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
				       hw_rule_format, subs,
				       SNDRV_PCM_HW_PARAM_RATE,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       param_period_time_if_needed,
				       -1)) < 0)
		goto rep_err;
	if (param_period_time_if_needed >= 0) {
		err = snd_pcm_hw_rule_add(runtime, 0,
					  SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					  hw_rule_period_time, subs,
					  SNDRV_PCM_HW_PARAM_FORMAT,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  SNDRV_PCM_HW_PARAM_RATE,
					  -1);
		if (err < 0)
			goto rep_err;
	}
	if ((err = snd_usb_pcm_check_knot(runtime, subs)) < 0)
		goto rep_err;
	return 0;

rep_err:
	snd_usb_autosuspend(subs->stream->chip);
	return err;
}

static int snd_usb_pcm_open(struct snd_pcm_substream *substream, int direction)
{
	struct snd_usb_stream *as = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usb_substream *subs = &as->substream[direction];

	subs->interface = -1;
	subs->altset_idx = 0;
	runtime->hw = snd_usb_hardware;
	runtime->private_data = subs;
	subs->pcm_substream = substream;
	/* runtime PM is also done there */

	/* initialize DSD/DOP context */
	subs->dsd_dop.byte_idx = 0;
	subs->dsd_dop.channel = 0;
	subs->dsd_dop.marker = 1;

	return setup_hw_info(runtime, subs);
}

static int snd_usb_pcm_close(struct snd_pcm_substream *substream, int direction)
{
	struct snd_usb_stream *as = snd_pcm_substream_chip(substream);
	struct snd_usb_substream *subs = &as->substream[direction];

	stop_endpoints(subs, true);

	if (subs->interface >= 0 &&
	    !snd_usb_lock_shutdown(subs->stream->chip)) {
		usb_set_interface(subs->dev, subs->interface, 0);
		subs->interface = -1;
		snd_usb_unlock_shutdown(subs->stream->chip);
	}

	subs->pcm_substream = NULL;
	snd_usb_autosuspend(subs->stream->chip);

	return 0;
}

/* Since a URB can handle only a single linear buffer, we must use double
 * buffering when the data to be transferred overflows the buffer boundary.
 * To avoid inconsistencies when updating hwptr_done, we use double buffering
 * for all URBs.
 */
static void retire_capture_urb(struct snd_usb_substream *subs,
			       struct urb *urb)
{
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	unsigned int stride, frames, bytes, oldptr;
	int i, period_elapsed = 0;
	unsigned long flags;
	unsigned char *cp;
	int current_frame_number;

	/* read frame number here, update pointer in critical section */
	current_frame_number = usb_get_current_frame_number(subs->dev);

	stride = runtime->frame_bits >> 3;

	for (i = 0; i < urb->number_of_packets; i++) {
		cp = (unsigned char *)urb->transfer_buffer + urb->iso_frame_desc[i].offset + subs->pkt_offset_adj;
		if (urb->iso_frame_desc[i].status && printk_ratelimit()) {
			dev_dbg(&subs->dev->dev, "frame %d active: %d\n",
				i, urb->iso_frame_desc[i].status);
			// continue;
		}
		bytes = urb->iso_frame_desc[i].actual_length;
		frames = bytes / stride;
		if (!subs->txfr_quirk)
			bytes = frames * stride;
		if (bytes % (runtime->sample_bits >> 3) != 0) {
			int oldbytes = bytes;
			bytes = frames * stride;
			dev_warn(&subs->dev->dev,
				 "Corrected urb data len. %d->%d\n",
							oldbytes, bytes);
		}
		/* update the current pointer */
		spin_lock_irqsave(&subs->lock, flags);
		oldptr = subs->hwptr_done;
		subs->hwptr_done += bytes;
		if (subs->hwptr_done >= runtime->buffer_size * stride)
			subs->hwptr_done -= runtime->buffer_size * stride;
		frames = (bytes + (oldptr % stride)) / stride;
		subs->transfer_done += frames;
		if (subs->transfer_done >= runtime->period_size) {
			subs->transfer_done -= runtime->period_size;
			period_elapsed = 1;
		}
		/* capture delay is by construction limited to one URB,
		 * reset delays here
		 */
		runtime->delay = subs->last_delay = 0;

		/* realign last_frame_number */
		subs->last_frame_number = current_frame_number;
		subs->last_frame_number &= 0xFF; /* keep 8 LSBs */

		spin_unlock_irqrestore(&subs->lock, flags);
		/* copy a data chunk */
		if (oldptr + bytes > runtime->buffer_size * stride) {
			unsigned int bytes1 =
					runtime->buffer_size * stride - oldptr;
			memcpy(runtime->dma_area + oldptr, cp, bytes1);
			memcpy(runtime->dma_area, cp + bytes1, bytes - bytes1);
		} else {
			memcpy(runtime->dma_area + oldptr, cp, bytes);
		}
	}

	if (period_elapsed)
		snd_pcm_period_elapsed(subs->pcm_substream);
}

static inline void fill_playback_urb_dsd_dop(struct snd_usb_substream *subs,
					     struct urb *urb, unsigned int bytes)
{
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	unsigned int stride = runtime->frame_bits >> 3;
	unsigned int dst_idx = 0;
	unsigned int src_idx = subs->hwptr_done;
	unsigned int wrap = runtime->buffer_size * stride;
	u8 *dst = urb->transfer_buffer;
	u8 *src = runtime->dma_area;
	u8 marker[] = { 0x05, 0xfa };

	/*
	 * The DSP DOP format defines a way to transport DSD samples over
	 * normal PCM data endpoints. It requires stuffing of marker bytes
	 * (0x05 and 0xfa, alternating per sample frame), and then expects
	 * 2 additional bytes of actual payload. The whole frame is stored
	 * LSB.
	 *
	 * Hence, for a stereo transport, the buffer layout looks like this,
	 * where L refers to left channel samples and R to right.
	 *
	 *   L1 L2 0x05   R1 R2 0x05   L3 L4 0xfa  R3 R4 0xfa
	 *   L5 L6 0x05   R5 R6 0x05   L7 L8 0xfa  R7 R8 0xfa
	 *   .....
	 *
	 */

	while (bytes--) {
		if (++subs->dsd_dop.byte_idx == 3) {
			/* frame boundary? */
			dst[dst_idx++] = marker[subs->dsd_dop.marker];
			src_idx += 2;
			subs->dsd_dop.byte_idx = 0;

			if (++subs->dsd_dop.channel % runtime->channels == 0) {
				/* alternate the marker */
				subs->dsd_dop.marker++;
				subs->dsd_dop.marker %= ARRAY_SIZE(marker);
				subs->dsd_dop.channel = 0;
			}
		} else {
			/* stuff the DSD payload */
			int idx = (src_idx + subs->dsd_dop.byte_idx - 1) % wrap;

			if (subs->cur_audiofmt->dsd_bitrev)
				dst[dst_idx++] = bitrev8(src[idx]);
			else
				dst[dst_idx++] = src[idx];

			subs->hwptr_done++;
		}
	}
	if (subs->hwptr_done >= runtime->buffer_size * stride)
		subs->hwptr_done -= runtime->buffer_size * stride;
}

static void copy_to_urb(struct snd_usb_substream *subs, struct urb *urb,
			int offset, int stride, unsigned int bytes)
{
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;

	if (subs->hwptr_done + bytes > runtime->buffer_size * stride) {
		/* err, the transferred area goes over buffer boundary. */
		unsigned int bytes1 =
			runtime->buffer_size * stride - subs->hwptr_done;
		memcpy(urb->transfer_buffer + offset,
		       runtime->dma_area + subs->hwptr_done, bytes1);
		memcpy(urb->transfer_buffer + offset + bytes1,
		       runtime->dma_area, bytes - bytes1);
	} else {
		memcpy(urb->transfer_buffer + offset,
		       runtime->dma_area + subs->hwptr_done, bytes);
	}
	subs->hwptr_done += bytes;
	if (subs->hwptr_done >= runtime->buffer_size * stride)
		subs->hwptr_done -= runtime->buffer_size * stride;
}

static unsigned int copy_to_urb_quirk(struct snd_usb_substream *subs,
				      struct urb *urb, int stride,
				      unsigned int bytes)
{
	__le32 packet_length;
	int i;

	/* Put __le32 length descriptor at start of each packet. */
	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned int length = urb->iso_frame_desc[i].length;
		unsigned int offset = urb->iso_frame_desc[i].offset;

		packet_length = cpu_to_le32(length);
		offset += i * sizeof(packet_length);
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length += sizeof(packet_length);
		memcpy(urb->transfer_buffer + offset,
		       &packet_length, sizeof(packet_length));
		copy_to_urb(subs, urb, offset + sizeof(packet_length),
			    stride, length);
	}
	/* Adjust transfer size accordingly. */
	bytes += urb->number_of_packets * sizeof(packet_length);
	return bytes;
}

static void prepare_playback_urb(struct snd_usb_substream *subs,
				 struct urb *urb)
{
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	struct snd_usb_endpoint *ep = subs->data_endpoint;
	struct snd_urb_ctx *ctx = urb->context;
	unsigned int counts, frames, bytes;
	int i, stride, period_elapsed = 0;
	unsigned long flags;

	stride = runtime->frame_bits >> 3;

	frames = 0;
	urb->number_of_packets = 0;
	spin_lock_irqsave(&subs->lock, flags);
	subs->frame_limit += ep->max_urb_frames;
	for (i = 0; i < ctx->packets; i++) {
		if (ctx->packet_size[i])
			counts = ctx->packet_size[i];
		else
			counts = snd_usb_endpoint_next_packet_size(ep);

		/* set up descriptor */
		urb->iso_frame_desc[i].offset = frames * ep->stride;
		urb->iso_frame_desc[i].length = counts * ep->stride;
		frames += counts;
		urb->number_of_packets++;
		subs->transfer_done += counts;
		if (subs->transfer_done >= runtime->period_size) {
			subs->transfer_done -= runtime->period_size;
			subs->frame_limit = 0;
			period_elapsed = 1;
			if (subs->fmt_type == UAC_FORMAT_TYPE_II) {
				if (subs->transfer_done > 0) {
					/* FIXME: fill-max mode is not
					 * supported yet */
					frames -= subs->transfer_done;
					counts -= subs->transfer_done;
					urb->iso_frame_desc[i].length =
						counts * ep->stride;
					subs->transfer_done = 0;
				}
				i++;
				if (i < ctx->packets) {
					/* add a transfer delimiter */
					urb->iso_frame_desc[i].offset =
						frames * ep->stride;
					urb->iso_frame_desc[i].length = 0;
					urb->number_of_packets++;
				}
				break;
			}
		}
		/* finish at the period boundary or after enough frames */
		if ((period_elapsed ||
				subs->transfer_done >= subs->frame_limit) &&
		    !snd_usb_endpoint_implicit_feedback_sink(ep))
			break;
	}
	bytes = frames * ep->stride;

	if (unlikely(subs->pcm_format == SNDRV_PCM_FORMAT_DSD_U16_LE &&
		     subs->cur_audiofmt->dsd_dop)) {
		fill_playback_urb_dsd_dop(subs, urb, bytes);
	} else if (unlikely(subs->pcm_format == SNDRV_PCM_FORMAT_DSD_U8 &&
			   subs->cur_audiofmt->dsd_bitrev)) {
		/* bit-reverse the bytes */
		u8 *buf = urb->transfer_buffer;
		for (i = 0; i < bytes; i++) {
			int idx = (subs->hwptr_done + i)
				% (runtime->buffer_size * stride);
			buf[i] = bitrev8(runtime->dma_area[idx]);
		}

		subs->hwptr_done += bytes;
		if (subs->hwptr_done >= runtime->buffer_size * stride)
			subs->hwptr_done -= runtime->buffer_size * stride;
	} else {
		/* usual PCM */
		if (!subs->tx_length_quirk)
			copy_to_urb(subs, urb, 0, stride, bytes);
		else
			bytes = copy_to_urb_quirk(subs, urb, stride, bytes);
			/* bytes is now amount of outgoing data */
	}

	/* update delay with exact number of samples queued */
	runtime->delay = subs->last_delay;
	runtime->delay += frames;
	subs->last_delay = runtime->delay;

	/* realign last_frame_number */
	subs->last_frame_number = usb_get_current_frame_number(subs->dev);
	subs->last_frame_number &= 0xFF; /* keep 8 LSBs */

	if (subs->trigger_tstamp_pending_update) {
		/* this is the first actual URB submitted,
		 * update trigger timestamp to reflect actual start time
		 */
		snd_pcm_gettime(runtime, &runtime->trigger_tstamp);
		subs->trigger_tstamp_pending_update = false;
	}

	spin_unlock_irqrestore(&subs->lock, flags);
	urb->transfer_buffer_length = bytes;
	if (period_elapsed)
		snd_pcm_period_elapsed(subs->pcm_substream);
}

/*
 * process after playback data complete
 * - decrease the delay count again
 */
static void retire_playback_urb(struct snd_usb_substream *subs,
			       struct urb *urb)
{
	unsigned long flags;
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	struct snd_usb_endpoint *ep = subs->data_endpoint;
	int processed = urb->transfer_buffer_length / ep->stride;
	int est_delay;

	/* ignore the delay accounting when procssed=0 is given, i.e.
	 * silent payloads are procssed before handling the actual data
	 */
	if (!processed)
		return;

	spin_lock_irqsave(&subs->lock, flags);
	if (!subs->last_delay)
		goto out; /* short path */

	est_delay = snd_usb_pcm_delay(subs, runtime->rate);
	/* update delay with exact number of samples played */
	if (processed > subs->last_delay)
		subs->last_delay = 0;
	else
		subs->last_delay -= processed;
	runtime->delay = subs->last_delay;

	/*
	 * Report when delay estimate is off by more than 2ms.
	 * The error should be lower than 2ms since the estimate relies
	 * on two reads of a counter updated every ms.
	 */
	if (abs(est_delay - subs->last_delay) * 1000 > runtime->rate * 2)
		dev_dbg_ratelimited(&subs->dev->dev,
			"delay: estimated %d, actual %d\n",
			est_delay, subs->last_delay);

	if (!subs->running) {
		/* update last_frame_number for delay counting here since
		 * prepare_playback_urb won't be called during pause
		 */
		subs->last_frame_number =
			usb_get_current_frame_number(subs->dev) & 0xff;
	}

 out:
	spin_unlock_irqrestore(&subs->lock, flags);
}

static int snd_usb_playback_open(struct snd_pcm_substream *substream)
{
	return snd_usb_pcm_open(substream, SNDRV_PCM_STREAM_PLAYBACK);
}

static int snd_usb_playback_close(struct snd_pcm_substream *substream)
{
	return snd_usb_pcm_close(substream, SNDRV_PCM_STREAM_PLAYBACK);
}

static int snd_usb_capture_open(struct snd_pcm_substream *substream)
{
	return snd_usb_pcm_open(substream, SNDRV_PCM_STREAM_CAPTURE);
}

static int snd_usb_capture_close(struct snd_pcm_substream *substream)
{
	return snd_usb_pcm_close(substream, SNDRV_PCM_STREAM_CAPTURE);
}

static int snd_usb_substream_playback_trigger(struct snd_pcm_substream *substream,
					      int cmd)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		subs->trigger_tstamp_pending_update = true;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		subs->data_endpoint->prepare_data_urb = prepare_playback_urb;
		subs->data_endpoint->retire_data_urb = retire_playback_urb;
		subs->running = 1;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		stop_endpoints(subs, false);
		subs->running = 0;
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		subs->data_endpoint->prepare_data_urb = NULL;
		/* keep retire_data_urb for delay calculation */
		subs->data_endpoint->retire_data_urb = retire_playback_urb;
		subs->running = 0;
		return 0;
	}

	return -EINVAL;
}

static int snd_usb_substream_capture_trigger(struct snd_pcm_substream *substream,
					     int cmd)
{
	int err;
	struct snd_usb_substream *subs = substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		err = start_endpoints(subs, false);
		if (err < 0)
			return err;

		subs->data_endpoint->retire_data_urb = retire_capture_urb;
		subs->running = 1;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		stop_endpoints(subs, false);
		subs->running = 0;
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		subs->data_endpoint->retire_data_urb = NULL;
		subs->running = 0;
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		subs->data_endpoint->retire_data_urb = retire_capture_urb;
		subs->running = 1;
		return 0;
	}

	return -EINVAL;
}

static struct snd_pcm_ops snd_usb_playback_ops = {
	.open =		snd_usb_playback_open,
	.close =	snd_usb_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_usb_hw_params,
	.hw_free =	snd_usb_hw_free,
	.prepare =	snd_usb_pcm_prepare,
	.trigger =	snd_usb_substream_playback_trigger,
	.pointer =	snd_usb_pcm_pointer,
	.page =		snd_pcm_lib_get_vmalloc_page,
	.mmap =		snd_pcm_lib_mmap_vmalloc,
};

static struct snd_pcm_ops snd_usb_capture_ops = {
	.open =		snd_usb_capture_open,
	.close =	snd_usb_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_usb_hw_params,
	.hw_free =	snd_usb_hw_free,
	.prepare =	snd_usb_pcm_prepare,
	.trigger =	snd_usb_substream_capture_trigger,
	.pointer =	snd_usb_pcm_pointer,
	.page =		snd_pcm_lib_get_vmalloc_page,
	.mmap =		snd_pcm_lib_mmap_vmalloc,
};

void snd_usb_set_pcm_ops(struct snd_pcm *pcm, int stream)
{
	snd_pcm_set_ops(pcm, stream,
			stream == SNDRV_PCM_STREAM_PLAYBACK ?
			&snd_usb_playback_ops : &snd_usb_capture_ops);
}
