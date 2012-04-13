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

/* return the estimated delay based on USB frame counters */
snd_pcm_uframes_t snd_usb_pcm_delay(struct snd_usb_substream *subs,
				    unsigned int rate)
{
	int current_frame_number;
	int frame_diff;
	int est_delay;

	current_frame_number = usb_get_current_frame_number(subs->dev);
	/*
	 * HCD implementations use different widths, use lower 8 bits.
	 * The delay will be managed up to 256ms, which is more than
	 * enough
	 */
	frame_diff = (current_frame_number - subs->last_frame_number) & 0xff;

	/* Approximation based on number of samples per USB frame (ms),
	   some truncation for 44.1 but the estimate is good enough */
	est_delay =  subs->last_delay - (frame_diff * rate / 1000);
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
static struct audioformat *find_format(struct snd_usb_substream *subs, unsigned int format,
				       unsigned int rate, unsigned int channels)
{
	struct list_head *p;
	struct audioformat *found = NULL;
	int cur_attr = 0, attr;

	list_for_each(p, &subs->fmt_list) {
		struct audioformat *fp;
		fp = list_entry(p, struct audioformat, list);
		if (!(fp->formats & (1uLL << format)))
			continue;
		if (fp->channels != channels)
			continue;
		if (rate < fp->rate_min || rate > fp->rate_max)
			continue;
		if (! (fp->rates & SNDRV_PCM_RATE_CONTINUOUS)) {
			unsigned int i;
			for (i = 0; i < fp->nr_rates; i++)
				if (fp->rate_table[i] == rate)
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
		snd_printk(KERN_ERR "%d:%d:%d: cannot set enable PITCH\n",
			   dev->devnum, iface, ep);
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
	unsigned int ep;
	int err;

	ep = get_endpoint(alts, 0)->bEndpointAddress;

	data[0] = 1;
	if ((err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC2_CS_CUR,
				   USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_OUT,
				   UAC2_EP_CS_PITCH << 8, 0,
				   data, sizeof(data))) < 0) {
		snd_printk(KERN_ERR "%d:%d:%d: cannot set enable PITCH (v2)\n",
			   dev->devnum, iface, fmt->altsetting);
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
	struct usb_interface_descriptor *altsd = get_iface_desc(alts);

	/* if endpoint doesn't have pitch control, bail out */
	if (!(fmt->attributes & UAC_EP_CS_ATTR_PITCH_CONTROL))
		return 0;

	switch (altsd->bInterfaceProtocol) {
	case UAC_VERSION_1:
	default:
		return init_pitch_v1(chip, iface, alts, fmt);

	case UAC_VERSION_2:
		return init_pitch_v2(chip, iface, alts, fmt);
	}
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
	unsigned int ep, attr;
	int is_playback = subs->direction == SNDRV_PCM_STREAM_PLAYBACK;
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
		if (usb_set_interface(subs->dev, subs->interface, 0) < 0) {
			snd_printk(KERN_ERR "%d:%d:%d: return to setting 0 failed\n",
				dev->devnum, fmt->iface, fmt->altsetting);
			return -EIO;
		}
		subs->interface = -1;
		subs->altset_idx = 0;
	}

	/* set interface */
	if (subs->interface != fmt->iface || subs->altset_idx != fmt->altset_idx) {
		if (usb_set_interface(dev, fmt->iface, fmt->altsetting) < 0) {
			snd_printk(KERN_ERR "%d:%d:%d: usb_set_interface failed\n",
				   dev->devnum, fmt->iface, fmt->altsetting);
			return -EIO;
		}
		snd_printdd(KERN_INFO "setting usb interface %d:%d\n", fmt->iface, fmt->altsetting);
		subs->interface = fmt->iface;
		subs->altset_idx = fmt->altset_idx;
	}

	/* create a data pipe */
	ep = fmt->endpoint & USB_ENDPOINT_NUMBER_MASK;
	if (is_playback)
		subs->datapipe = usb_sndisocpipe(dev, ep);
	else
		subs->datapipe = usb_rcvisocpipe(dev, ep);
	subs->datainterval = fmt->datainterval;
	subs->syncpipe = subs->syncinterval = 0;
	subs->maxpacksize = fmt->maxpacksize;
	subs->syncmaxsize = 0;
	subs->fill_max = 0;

	/* we need a sync pipe in async OUT or adaptive IN mode */
	/* check the number of EP, since some devices have broken
	 * descriptors which fool us.  if it has only one EP,
	 * assume it as adaptive-out or sync-in.
	 */
	attr = fmt->ep_attr & USB_ENDPOINT_SYNCTYPE;
	if (((is_playback && attr == USB_ENDPOINT_SYNC_ASYNC) ||
	     (! is_playback && attr == USB_ENDPOINT_SYNC_ADAPTIVE)) &&
	    altsd->bNumEndpoints >= 2) {
		/* check sync-pipe endpoint */
		/* ... and check descriptor size before accessing bSynchAddress
		   because there is a version of the SB Audigy 2 NX firmware lacking
		   the audio fields in the endpoint descriptors */
		if ((get_endpoint(alts, 1)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != 0x01 ||
		    (get_endpoint(alts, 1)->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE &&
		     get_endpoint(alts, 1)->bSynchAddress != 0)) {
			snd_printk(KERN_ERR "%d:%d:%d : invalid synch pipe\n",
				   dev->devnum, fmt->iface, fmt->altsetting);
			return -EINVAL;
		}
		ep = get_endpoint(alts, 1)->bEndpointAddress;
		if (get_endpoint(alts, 0)->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE &&
		    (( is_playback && ep != (unsigned int)(get_endpoint(alts, 0)->bSynchAddress | USB_DIR_IN)) ||
		     (!is_playback && ep != (unsigned int)(get_endpoint(alts, 0)->bSynchAddress & ~USB_DIR_IN)))) {
			snd_printk(KERN_ERR "%d:%d:%d : invalid synch pipe\n",
				   dev->devnum, fmt->iface, fmt->altsetting);
			return -EINVAL;
		}
		ep &= USB_ENDPOINT_NUMBER_MASK;
		if (is_playback)
			subs->syncpipe = usb_rcvisocpipe(dev, ep);
		else
			subs->syncpipe = usb_sndisocpipe(dev, ep);
		if (get_endpoint(alts, 1)->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE &&
		    get_endpoint(alts, 1)->bRefresh >= 1 &&
		    get_endpoint(alts, 1)->bRefresh <= 9)
			subs->syncinterval = get_endpoint(alts, 1)->bRefresh;
		else if (snd_usb_get_speed(subs->dev) == USB_SPEED_FULL)
			subs->syncinterval = 1;
		else if (get_endpoint(alts, 1)->bInterval >= 1 &&
			 get_endpoint(alts, 1)->bInterval <= 16)
			subs->syncinterval = get_endpoint(alts, 1)->bInterval - 1;
		else
			subs->syncinterval = 3;
		subs->syncmaxsize = le16_to_cpu(get_endpoint(alts, 1)->wMaxPacketSize);
	}

	/* always fill max packet size */
	if (fmt->attributes & UAC_EP_CS_ATTR_FILL_MAX)
		subs->fill_max = 1;

	if ((err = snd_usb_init_pitch(subs->stream->chip, subs->interface, alts, fmt)) < 0)
		return err;

	subs->cur_audiofmt = fmt;

	snd_usb_set_format_quirk(subs, fmt);

#if 0
	printk(KERN_DEBUG
	       "setting done: format = %d, rate = %d..%d, channels = %d\n",
	       fmt->format, fmt->rate_min, fmt->rate_max, fmt->channels);
	printk(KERN_DEBUG
	       "  datapipe = 0x%0x, syncpipe = 0x%0x\n",
	       subs->datapipe, subs->syncpipe);
#endif

	return 0;
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
	unsigned int channels, rate, format;
	int ret, changed;

	ret = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;

	format = params_format(hw_params);
	rate = params_rate(hw_params);
	channels = params_channels(hw_params);
	fmt = find_format(subs, format, rate, channels);
	if (!fmt) {
		snd_printd(KERN_DEBUG "cannot set format: format = %#x, rate = %d, channels = %d\n",
			   format, rate, channels);
		return -EINVAL;
	}

	changed = subs->cur_audiofmt != fmt ||
		subs->period_bytes != params_period_bytes(hw_params) ||
		subs->cur_rate != rate;
	if ((ret = set_format(subs, fmt)) < 0)
		return ret;

	if (subs->cur_rate != rate) {
		struct usb_host_interface *alts;
		struct usb_interface *iface;
		iface = usb_ifnum_to_if(subs->dev, fmt->iface);
		alts = &iface->altsetting[fmt->altset_idx];
		ret = snd_usb_init_sample_rate(subs->stream->chip, subs->interface, alts, fmt, rate);
		if (ret < 0)
			return ret;
		subs->cur_rate = rate;
	}

	if (changed) {
		mutex_lock(&subs->stream->chip->shutdown_mutex);
		/* format changed */
		snd_usb_release_substream_urbs(subs, 0);
		/* influenced: period_bytes, channels, rate, format, */
		ret = snd_usb_init_substream_urbs(subs, params_period_bytes(hw_params),
						  params_rate(hw_params),
						  snd_pcm_format_physical_width(params_format(hw_params)) *
							params_channels(hw_params));
		mutex_unlock(&subs->stream->chip->shutdown_mutex);
	}

	return ret;
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
	mutex_lock(&subs->stream->chip->shutdown_mutex);
	snd_usb_release_substream_urbs(subs, 0);
	mutex_unlock(&subs->stream->chip->shutdown_mutex);
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

	if (! subs->cur_audiofmt) {
		snd_printk(KERN_ERR "usbaudio: no format is specified!\n");
		return -ENXIO;
	}

	/* some unit conversions in runtime */
	subs->maxframesize = bytes_to_frames(runtime, subs->maxpacksize);
	subs->curframesize = bytes_to_frames(runtime, subs->curpacksize);

	/* reset the pointer */
	subs->hwptr_done = 0;
	subs->transfer_done = 0;
	subs->phase = 0;
	subs->last_delay = 0;
	subs->last_frame_number = 0;
	runtime->delay = 0;

	return snd_usb_substream_prepare(subs, runtime);
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
	if (snd_usb_get_speed(subs->dev) != USB_SPEED_FULL) {
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
	struct list_head *p;
	struct snd_interval *it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	unsigned int rmin, rmax;
	int changed;

	hwc_debug("hw_rule_rate: (%d,%d)\n", it->min, it->max);
	changed = 0;
	rmin = rmax = 0;
	list_for_each(p, &subs->fmt_list) {
		struct audioformat *fp;
		fp = list_entry(p, struct audioformat, list);
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
	struct list_head *p;
	struct snd_interval *it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	unsigned int rmin, rmax;
	int changed;

	hwc_debug("hw_rule_channels: (%d,%d)\n", it->min, it->max);
	changed = 0;
	rmin = rmax = 0;
	list_for_each(p, &subs->fmt_list) {
		struct audioformat *fp;
		fp = list_entry(p, struct audioformat, list);
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
	struct list_head *p;
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	u64 fbits;
	u32 oldbits[2];
	int changed;

	hwc_debug("hw_rule_format: %x:%x\n", fmt->bits[0], fmt->bits[1]);
	fbits = 0;
	list_for_each(p, &subs->fmt_list) {
		struct audioformat *fp;
		fp = list_entry(p, struct audioformat, list);
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
	struct list_head *p;
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
	list_for_each(p, &subs->fmt_list) {
		struct audioformat *fp;
		fp = list_entry(p, struct audioformat, list);
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
	if (snd_usb_get_speed(subs->dev) == USB_SPEED_FULL)
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
	return setup_hw_info(runtime, subs);
}

static int snd_usb_pcm_close(struct snd_pcm_substream *substream, int direction)
{
	struct snd_usb_stream *as = snd_pcm_substream_chip(substream);
	struct snd_usb_substream *subs = &as->substream[direction];

	if (!as->chip->shutdown && subs->interface >= 0) {
		usb_set_interface(subs->dev, subs->interface, 0);
		subs->interface = -1;
	}
	subs->pcm_substream = NULL;
	snd_usb_autosuspend(subs->stream->chip);
	return 0;
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
