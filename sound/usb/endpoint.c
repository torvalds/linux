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
 *
 */

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/ratelimit.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "usbaudio.h"
#include "helper.h"
#include "card.h"
#include "endpoint.h"
#include "pcm.h"
#include "quirks.h"

#define EP_FLAG_RUNNING		1
#define EP_FLAG_STOPPING	2

/*
 * snd_usb_endpoint is a model that abstracts everything related to an
 * USB endpoint and its streaming.
 *
 * There are functions to activate and deactivate the streaming URBs and
 * optional callbacks to let the pcm logic handle the actual content of the
 * packets for playback and record. Thus, the bus streaming and the audio
 * handlers are fully decoupled.
 *
 * There are two different types of endpoints in audio applications.
 *
 * SND_USB_ENDPOINT_TYPE_DATA handles full audio data payload for both
 * inbound and outbound traffic.
 *
 * SND_USB_ENDPOINT_TYPE_SYNC endpoints are for inbound traffic only and
 * expect the payload to carry Q10.14 / Q16.16 formatted sync information
 * (3 or 4 bytes).
 *
 * Each endpoint has to be configured prior to being used by calling
 * snd_usb_endpoint_set_params().
 *
 * The model incorporates a reference counting, so that multiple users
 * can call snd_usb_endpoint_start() and snd_usb_endpoint_stop(), and
 * only the first user will effectively start the URBs, and only the last
 * one to stop it will tear the URBs down again.
 */

/*
 * convert a sampling rate into our full speed format (fs/1000 in Q16.16)
 * this will overflow at approx 524 kHz
 */
static inline unsigned get_usb_full_speed_rate(unsigned int rate)
{
	return ((rate << 13) + 62) / 125;
}

/*
 * convert a sampling rate into USB high speed format (fs/8000 in Q16.16)
 * this will overflow at approx 4 MHz
 */
static inline unsigned get_usb_high_speed_rate(unsigned int rate)
{
	return ((rate << 10) + 62) / 125;
}

/*
 * release a urb data
 */
static void release_urb_ctx(struct snd_urb_ctx *u)
{
	if (u->buffer_size)
		usb_free_coherent(u->ep->chip->dev, u->buffer_size,
				  u->urb->transfer_buffer,
				  u->urb->transfer_dma);
	usb_free_urb(u->urb);
	u->urb = NULL;
}

static const char *usb_error_string(int err)
{
	switch (err) {
	case -ENODEV:
		return "no device";
	case -ENOENT:
		return "endpoint not enabled";
	case -EPIPE:
		return "endpoint stalled";
	case -ENOSPC:
		return "not enough bandwidth";
	case -ESHUTDOWN:
		return "device disabled";
	case -EHOSTUNREACH:
		return "device suspended";
	case -EINVAL:
	case -EAGAIN:
	case -EFBIG:
	case -EMSGSIZE:
		return "internal error";
	default:
		return "unknown error";
	}
}

/**
 * snd_usb_endpoint_implicit_feedback_sink: Report endpoint usage type
 *
 * @ep: The snd_usb_endpoint
 *
 * Determine whether an endpoint is driven by an implicit feedback
 * data endpoint source.
 */
int snd_usb_endpoint_implicit_feedback_sink(struct snd_usb_endpoint *ep)
{
	return  ep->sync_master &&
		ep->sync_master->type == SND_USB_ENDPOINT_TYPE_DATA &&
		ep->type == SND_USB_ENDPOINT_TYPE_DATA &&
		usb_pipeout(ep->pipe);
}

/*
 * For streaming based on information derived from sync endpoints,
 * prepare_outbound_urb_sizes() will call next_packet_size() to
 * determine the number of samples to be sent in the next packet.
 *
 * For implicit feedback, next_packet_size() is unused.
 */
int snd_usb_endpoint_next_packet_size(struct snd_usb_endpoint *ep)
{
	unsigned long flags;
	int ret;

	if (ep->fill_max)
		return ep->maxframesize;

	spin_lock_irqsave(&ep->lock, flags);
	ep->phase = (ep->phase & 0xffff)
		+ (ep->freqm << ep->datainterval);
	ret = min(ep->phase >> 16, ep->maxframesize);
	spin_unlock_irqrestore(&ep->lock, flags);

	return ret;
}

static void retire_outbound_urb(struct snd_usb_endpoint *ep,
				struct snd_urb_ctx *urb_ctx)
{
	if (ep->retire_data_urb)
		ep->retire_data_urb(ep->data_subs, urb_ctx->urb);
}

static void retire_inbound_urb(struct snd_usb_endpoint *ep,
			       struct snd_urb_ctx *urb_ctx)
{
	struct urb *urb = urb_ctx->urb;

	if (unlikely(ep->skip_packets > 0)) {
		ep->skip_packets--;
		return;
	}

	if (ep->sync_slave)
		snd_usb_handle_sync_urb(ep->sync_slave, ep, urb);

	if (ep->retire_data_urb)
		ep->retire_data_urb(ep->data_subs, urb);
}

/*
 * Prepare a PLAYBACK urb for submission to the bus.
 */
static void prepare_outbound_urb(struct snd_usb_endpoint *ep,
				 struct snd_urb_ctx *ctx)
{
	int i;
	struct urb *urb = ctx->urb;
	unsigned char *cp = urb->transfer_buffer;

	urb->dev = ep->chip->dev; /* we need to set this at each time */

	switch (ep->type) {
	case SND_USB_ENDPOINT_TYPE_DATA:
		if (ep->prepare_data_urb) {
			ep->prepare_data_urb(ep->data_subs, urb);
		} else {
			/* no data provider, so send silence */
			unsigned int offs = 0;
			for (i = 0; i < ctx->packets; ++i) {
				int counts;

				if (ctx->packet_size[i])
					counts = ctx->packet_size[i];
				else
					counts = snd_usb_endpoint_next_packet_size(ep);

				urb->iso_frame_desc[i].offset = offs * ep->stride;
				urb->iso_frame_desc[i].length = counts * ep->stride;
				offs += counts;
			}

			urb->number_of_packets = ctx->packets;
			urb->transfer_buffer_length = offs * ep->stride;
			memset(urb->transfer_buffer, ep->silence_value,
			       offs * ep->stride);
		}
		break;

	case SND_USB_ENDPOINT_TYPE_SYNC:
		if (snd_usb_get_speed(ep->chip->dev) >= USB_SPEED_HIGH) {
			/*
			 * fill the length and offset of each urb descriptor.
			 * the fixed 12.13 frequency is passed as 16.16 through the pipe.
			 */
			urb->iso_frame_desc[0].length = 4;
			urb->iso_frame_desc[0].offset = 0;
			cp[0] = ep->freqn;
			cp[1] = ep->freqn >> 8;
			cp[2] = ep->freqn >> 16;
			cp[3] = ep->freqn >> 24;
		} else {
			/*
			 * fill the length and offset of each urb descriptor.
			 * the fixed 10.14 frequency is passed through the pipe.
			 */
			urb->iso_frame_desc[0].length = 3;
			urb->iso_frame_desc[0].offset = 0;
			cp[0] = ep->freqn >> 2;
			cp[1] = ep->freqn >> 10;
			cp[2] = ep->freqn >> 18;
		}

		break;
	}
}

/*
 * Prepare a CAPTURE or SYNC urb for submission to the bus.
 */
static inline void prepare_inbound_urb(struct snd_usb_endpoint *ep,
				       struct snd_urb_ctx *urb_ctx)
{
	int i, offs;
	struct urb *urb = urb_ctx->urb;

	urb->dev = ep->chip->dev; /* we need to set this at each time */

	switch (ep->type) {
	case SND_USB_ENDPOINT_TYPE_DATA:
		offs = 0;
		for (i = 0; i < urb_ctx->packets; i++) {
			urb->iso_frame_desc[i].offset = offs;
			urb->iso_frame_desc[i].length = ep->curpacksize;
			offs += ep->curpacksize;
		}

		urb->transfer_buffer_length = offs;
		urb->number_of_packets = urb_ctx->packets;
		break;

	case SND_USB_ENDPOINT_TYPE_SYNC:
		urb->iso_frame_desc[0].length = min(4u, ep->syncmaxsize);
		urb->iso_frame_desc[0].offset = 0;
		break;
	}
}

/*
 * Send output urbs that have been prepared previously. URBs are dequeued
 * from ep->ready_playback_urbs and in case there there aren't any available
 * or there are no packets that have been prepared, this function does
 * nothing.
 *
 * The reason why the functionality of sending and preparing URBs is separated
 * is that host controllers don't guarantee the order in which they return
 * inbound and outbound packets to their submitters.
 *
 * This function is only used for implicit feedback endpoints. For endpoints
 * driven by dedicated sync endpoints, URBs are immediately re-submitted
 * from their completion handler.
 */
static void queue_pending_output_urbs(struct snd_usb_endpoint *ep)
{
	while (test_bit(EP_FLAG_RUNNING, &ep->flags)) {

		unsigned long flags;
		struct snd_usb_packet_info *uninitialized_var(packet);
		struct snd_urb_ctx *ctx = NULL;
		struct urb *urb;
		int err, i;

		spin_lock_irqsave(&ep->lock, flags);
		if (ep->next_packet_read_pos != ep->next_packet_write_pos) {
			packet = ep->next_packet + ep->next_packet_read_pos;
			ep->next_packet_read_pos++;
			ep->next_packet_read_pos %= MAX_URBS;

			/* take URB out of FIFO */
			if (!list_empty(&ep->ready_playback_urbs))
				ctx = list_first_entry(&ep->ready_playback_urbs,
					       struct snd_urb_ctx, ready_list);
		}
		spin_unlock_irqrestore(&ep->lock, flags);

		if (ctx == NULL)
			return;

		list_del_init(&ctx->ready_list);
		urb = ctx->urb;

		/* copy over the length information */
		for (i = 0; i < packet->packets; i++)
			ctx->packet_size[i] = packet->packet_size[i];

		/* call the data handler to fill in playback data */
		prepare_outbound_urb(ep, ctx);

		err = usb_submit_urb(ctx->urb, GFP_ATOMIC);
		if (err < 0)
			usb_audio_err(ep->chip,
				"Unable to submit urb #%d: %d (urb %p)\n",
				ctx->index, err, ctx->urb);
		else
			set_bit(ctx->index, &ep->active_mask);
	}
}

/*
 * complete callback for urbs
 */
static void snd_complete_urb(struct urb *urb)
{
	struct snd_urb_ctx *ctx = urb->context;
	struct snd_usb_endpoint *ep = ctx->ep;
	struct snd_pcm_substream *substream;
	unsigned long flags;
	int err;

	if (unlikely(urb->status == -ENOENT ||		/* unlinked */
		     urb->status == -ENODEV ||		/* device removed */
		     urb->status == -ECONNRESET ||	/* unlinked */
		     urb->status == -ESHUTDOWN))	/* device disabled */
		goto exit_clear;
	/* device disconnected */
	if (unlikely(atomic_read(&ep->chip->shutdown)))
		goto exit_clear;

	if (usb_pipeout(ep->pipe)) {
		retire_outbound_urb(ep, ctx);
		/* can be stopped during retire callback */
		if (unlikely(!test_bit(EP_FLAG_RUNNING, &ep->flags)))
			goto exit_clear;

		if (snd_usb_endpoint_implicit_feedback_sink(ep)) {
			spin_lock_irqsave(&ep->lock, flags);
			list_add_tail(&ctx->ready_list, &ep->ready_playback_urbs);
			spin_unlock_irqrestore(&ep->lock, flags);
			queue_pending_output_urbs(ep);

			goto exit_clear;
		}

		prepare_outbound_urb(ep, ctx);
	} else {
		retire_inbound_urb(ep, ctx);
		/* can be stopped during retire callback */
		if (unlikely(!test_bit(EP_FLAG_RUNNING, &ep->flags)))
			goto exit_clear;

		prepare_inbound_urb(ep, ctx);
	}

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err == 0)
		return;

	usb_audio_err(ep->chip, "cannot submit urb (err = %d)\n", err);
	if (ep->data_subs && ep->data_subs->pcm_substream) {
		substream = ep->data_subs->pcm_substream;
		snd_pcm_stop_xrun(substream);
	}

exit_clear:
	clear_bit(ctx->index, &ep->active_mask);
}

/**
 * snd_usb_add_endpoint: Add an endpoint to an USB audio chip
 *
 * @chip: The chip
 * @alts: The USB host interface
 * @ep_num: The number of the endpoint to use
 * @direction: SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE
 * @type: SND_USB_ENDPOINT_TYPE_DATA or SND_USB_ENDPOINT_TYPE_SYNC
 *
 * If the requested endpoint has not been added to the given chip before,
 * a new instance is created. Otherwise, a pointer to the previoulsy
 * created instance is returned. In case of any error, NULL is returned.
 *
 * New endpoints will be added to chip->ep_list and must be freed by
 * calling snd_usb_endpoint_free().
 */
struct snd_usb_endpoint *snd_usb_add_endpoint(struct snd_usb_audio *chip,
					      struct usb_host_interface *alts,
					      int ep_num, int direction, int type)
{
	struct snd_usb_endpoint *ep;
	int is_playback = direction == SNDRV_PCM_STREAM_PLAYBACK;

	if (WARN_ON(!alts))
		return NULL;

	mutex_lock(&chip->mutex);

	list_for_each_entry(ep, &chip->ep_list, list) {
		if (ep->ep_num == ep_num &&
		    ep->iface == alts->desc.bInterfaceNumber &&
		    ep->altsetting == alts->desc.bAlternateSetting) {
			usb_audio_dbg(ep->chip,
				      "Re-using EP %x in iface %d,%d @%p\n",
					ep_num, ep->iface, ep->altsetting, ep);
			goto __exit_unlock;
		}
	}

	usb_audio_dbg(chip, "Creating new %s %s endpoint #%x\n",
		    is_playback ? "playback" : "capture",
		    type == SND_USB_ENDPOINT_TYPE_DATA ? "data" : "sync",
		    ep_num);

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		goto __exit_unlock;

	ep->chip = chip;
	spin_lock_init(&ep->lock);
	ep->type = type;
	ep->ep_num = ep_num;
	ep->iface = alts->desc.bInterfaceNumber;
	ep->altsetting = alts->desc.bAlternateSetting;
	INIT_LIST_HEAD(&ep->ready_playback_urbs);
	ep_num &= USB_ENDPOINT_NUMBER_MASK;

	if (is_playback)
		ep->pipe = usb_sndisocpipe(chip->dev, ep_num);
	else
		ep->pipe = usb_rcvisocpipe(chip->dev, ep_num);

	if (type == SND_USB_ENDPOINT_TYPE_SYNC) {
		if (get_endpoint(alts, 1)->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE &&
		    get_endpoint(alts, 1)->bRefresh >= 1 &&
		    get_endpoint(alts, 1)->bRefresh <= 9)
			ep->syncinterval = get_endpoint(alts, 1)->bRefresh;
		else if (snd_usb_get_speed(chip->dev) == USB_SPEED_FULL)
			ep->syncinterval = 1;
		else if (get_endpoint(alts, 1)->bInterval >= 1 &&
			 get_endpoint(alts, 1)->bInterval <= 16)
			ep->syncinterval = get_endpoint(alts, 1)->bInterval - 1;
		else
			ep->syncinterval = 3;

		ep->syncmaxsize = le16_to_cpu(get_endpoint(alts, 1)->wMaxPacketSize);

		if (chip->usb_id == USB_ID(0x0644, 0x8038) /* TEAC UD-H01 */ &&
		    ep->syncmaxsize == 4)
			ep->udh01_fb_quirk = 1;
	}

	list_add_tail(&ep->list, &chip->ep_list);

__exit_unlock:
	mutex_unlock(&chip->mutex);

	return ep;
}

/*
 *  wait until all urbs are processed.
 */
static int wait_clear_urbs(struct snd_usb_endpoint *ep)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(1000);
	int alive;

	do {
		alive = bitmap_weight(&ep->active_mask, ep->nurbs);
		if (!alive)
			break;

		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, end_time));

	if (alive)
		usb_audio_err(ep->chip,
			"timeout: still %d active urbs on EP #%x\n",
			alive, ep->ep_num);
	clear_bit(EP_FLAG_STOPPING, &ep->flags);

	return 0;
}

/* sync the pending stop operation;
 * this function itself doesn't trigger the stop operation
 */
void snd_usb_endpoint_sync_pending_stop(struct snd_usb_endpoint *ep)
{
	if (ep && test_bit(EP_FLAG_STOPPING, &ep->flags))
		wait_clear_urbs(ep);
}

/*
 * unlink active urbs.
 */
static int deactivate_urbs(struct snd_usb_endpoint *ep, bool force)
{
	unsigned int i;

	if (!force && atomic_read(&ep->chip->shutdown)) /* to be sure... */
		return -EBADFD;

	clear_bit(EP_FLAG_RUNNING, &ep->flags);

	INIT_LIST_HEAD(&ep->ready_playback_urbs);
	ep->next_packet_read_pos = 0;
	ep->next_packet_write_pos = 0;

	for (i = 0; i < ep->nurbs; i++) {
		if (test_bit(i, &ep->active_mask)) {
			if (!test_and_set_bit(i, &ep->unlink_mask)) {
				struct urb *u = ep->urb[i].urb;
				usb_unlink_urb(u);
			}
		}
	}

	return 0;
}

/*
 * release an endpoint's urbs
 */
static void release_urbs(struct snd_usb_endpoint *ep, int force)
{
	int i;

	/* route incoming urbs to nirvana */
	ep->retire_data_urb = NULL;
	ep->prepare_data_urb = NULL;

	/* stop urbs */
	deactivate_urbs(ep, force);
	wait_clear_urbs(ep);

	for (i = 0; i < ep->nurbs; i++)
		release_urb_ctx(&ep->urb[i]);

	if (ep->syncbuf)
		usb_free_coherent(ep->chip->dev, SYNC_URBS * 4,
				  ep->syncbuf, ep->sync_dma);

	ep->syncbuf = NULL;
	ep->nurbs = 0;
}

/*
 * configure a data endpoint
 */
static int data_ep_set_params(struct snd_usb_endpoint *ep,
			      snd_pcm_format_t pcm_format,
			      unsigned int channels,
			      unsigned int period_bytes,
			      unsigned int frames_per_period,
			      unsigned int periods_per_buffer,
			      struct audioformat *fmt,
			      struct snd_usb_endpoint *sync_ep)
{
	unsigned int maxsize, minsize, packs_per_ms, max_packs_per_urb;
	unsigned int max_packs_per_period, urbs_per_period, urb_packs;
	unsigned int max_urbs, i;
	int frame_bits = snd_pcm_format_physical_width(pcm_format) * channels;

	if (pcm_format == SNDRV_PCM_FORMAT_DSD_U16_LE && fmt->dsd_dop) {
		/*
		 * When operating in DSD DOP mode, the size of a sample frame
		 * in hardware differs from the actual physical format width
		 * because we need to make room for the DOP markers.
		 */
		frame_bits += channels << 3;
	}

	ep->datainterval = fmt->datainterval;
	ep->stride = frame_bits >> 3;
	ep->silence_value = pcm_format == SNDRV_PCM_FORMAT_U8 ? 0x80 : 0;

	/* assume max. frequency is 25% higher than nominal */
	ep->freqmax = ep->freqn + (ep->freqn >> 2);
	maxsize = ((ep->freqmax + 0xffff) * (frame_bits >> 3))
				>> (16 - ep->datainterval);
	/* but wMaxPacketSize might reduce this */
	if (ep->maxpacksize && ep->maxpacksize < maxsize) {
		/* whatever fits into a max. size packet */
		maxsize = ep->maxpacksize;
		ep->freqmax = (maxsize / (frame_bits >> 3))
				<< (16 - ep->datainterval);
	}

	if (ep->fill_max)
		ep->curpacksize = ep->maxpacksize;
	else
		ep->curpacksize = maxsize;

	if (snd_usb_get_speed(ep->chip->dev) != USB_SPEED_FULL) {
		packs_per_ms = 8 >> ep->datainterval;
		max_packs_per_urb = MAX_PACKS_HS;
	} else {
		packs_per_ms = 1;
		max_packs_per_urb = MAX_PACKS;
	}
	if (sync_ep && !snd_usb_endpoint_implicit_feedback_sink(ep))
		max_packs_per_urb = min(max_packs_per_urb,
					1U << sync_ep->syncinterval);
	max_packs_per_urb = max(1u, max_packs_per_urb >> ep->datainterval);

	/*
	 * Capture endpoints need to use small URBs because there's no way
	 * to tell in advance where the next period will end, and we don't
	 * want the next URB to complete much after the period ends.
	 *
	 * Playback endpoints with implicit sync much use the same parameters
	 * as their corresponding capture endpoint.
	 */
	if (usb_pipein(ep->pipe) ||
			snd_usb_endpoint_implicit_feedback_sink(ep)) {

		urb_packs = packs_per_ms;
		/*
		 * Wireless devices can poll at a max rate of once per 4ms.
		 * For dataintervals less than 5, increase the packet count to
		 * allow the host controller to use bursting to fill in the
		 * gaps.
		 */
		if (snd_usb_get_speed(ep->chip->dev) == USB_SPEED_WIRELESS) {
			int interval = ep->datainterval;
			while (interval < 5) {
				urb_packs <<= 1;
				++interval;
			}
		}
		/* make capture URBs <= 1 ms and smaller than a period */
		urb_packs = min(max_packs_per_urb, urb_packs);
		while (urb_packs > 1 && urb_packs * maxsize >= period_bytes)
			urb_packs >>= 1;
		ep->nurbs = MAX_URBS;

	/*
	 * Playback endpoints without implicit sync are adjusted so that
	 * a period fits as evenly as possible in the smallest number of
	 * URBs.  The total number of URBs is adjusted to the size of the
	 * ALSA buffer, subject to the MAX_URBS and MAX_QUEUE limits.
	 */
	} else {
		/* determine how small a packet can be */
		minsize = (ep->freqn >> (16 - ep->datainterval)) *
				(frame_bits >> 3);
		/* with sync from device, assume it can be 12% lower */
		if (sync_ep)
			minsize -= minsize >> 3;
		minsize = max(minsize, 1u);

		/* how many packets will contain an entire ALSA period? */
		max_packs_per_period = DIV_ROUND_UP(period_bytes, minsize);

		/* how many URBs will contain a period? */
		urbs_per_period = DIV_ROUND_UP(max_packs_per_period,
				max_packs_per_urb);
		/* how many packets are needed in each URB? */
		urb_packs = DIV_ROUND_UP(max_packs_per_period, urbs_per_period);

		/* limit the number of frames in a single URB */
		ep->max_urb_frames = DIV_ROUND_UP(frames_per_period,
					urbs_per_period);

		/* try to use enough URBs to contain an entire ALSA buffer */
		max_urbs = min((unsigned) MAX_URBS,
				MAX_QUEUE * packs_per_ms / urb_packs);
		ep->nurbs = min(max_urbs, urbs_per_period * periods_per_buffer);
	}

	/* allocate and initialize data urbs */
	for (i = 0; i < ep->nurbs; i++) {
		struct snd_urb_ctx *u = &ep->urb[i];
		u->index = i;
		u->ep = ep;
		u->packets = urb_packs;
		u->buffer_size = maxsize * u->packets;

		if (fmt->fmt_type == UAC_FORMAT_TYPE_II)
			u->packets++; /* for transfer delimiter */
		u->urb = usb_alloc_urb(u->packets, GFP_KERNEL);
		if (!u->urb)
			goto out_of_memory;

		u->urb->transfer_buffer =
			usb_alloc_coherent(ep->chip->dev, u->buffer_size,
					   GFP_KERNEL, &u->urb->transfer_dma);
		if (!u->urb->transfer_buffer)
			goto out_of_memory;
		u->urb->pipe = ep->pipe;
		u->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		u->urb->interval = 1 << ep->datainterval;
		u->urb->context = u;
		u->urb->complete = snd_complete_urb;
		INIT_LIST_HEAD(&u->ready_list);
	}

	return 0;

out_of_memory:
	release_urbs(ep, 0);
	return -ENOMEM;
}

/*
 * configure a sync endpoint
 */
static int sync_ep_set_params(struct snd_usb_endpoint *ep)
{
	int i;

	ep->syncbuf = usb_alloc_coherent(ep->chip->dev, SYNC_URBS * 4,
					 GFP_KERNEL, &ep->sync_dma);
	if (!ep->syncbuf)
		return -ENOMEM;

	for (i = 0; i < SYNC_URBS; i++) {
		struct snd_urb_ctx *u = &ep->urb[i];
		u->index = i;
		u->ep = ep;
		u->packets = 1;
		u->urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!u->urb)
			goto out_of_memory;
		u->urb->transfer_buffer = ep->syncbuf + i * 4;
		u->urb->transfer_dma = ep->sync_dma + i * 4;
		u->urb->transfer_buffer_length = 4;
		u->urb->pipe = ep->pipe;
		u->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		u->urb->number_of_packets = 1;
		u->urb->interval = 1 << ep->syncinterval;
		u->urb->context = u;
		u->urb->complete = snd_complete_urb;
	}

	ep->nurbs = SYNC_URBS;

	return 0;

out_of_memory:
	release_urbs(ep, 0);
	return -ENOMEM;
}

/**
 * snd_usb_endpoint_set_params: configure an snd_usb_endpoint
 *
 * @ep: the snd_usb_endpoint to configure
 * @pcm_format: the audio fomat.
 * @channels: the number of audio channels.
 * @period_bytes: the number of bytes in one alsa period.
 * @period_frames: the number of frames in one alsa period.
 * @buffer_periods: the number of periods in one alsa buffer.
 * @rate: the frame rate.
 * @fmt: the USB audio format information
 * @sync_ep: the sync endpoint to use, if any
 *
 * Determine the number of URBs to be used on this endpoint.
 * An endpoint must be configured before it can be started.
 * An endpoint that is already running can not be reconfigured.
 */
int snd_usb_endpoint_set_params(struct snd_usb_endpoint *ep,
				snd_pcm_format_t pcm_format,
				unsigned int channels,
				unsigned int period_bytes,
				unsigned int period_frames,
				unsigned int buffer_periods,
				unsigned int rate,
				struct audioformat *fmt,
				struct snd_usb_endpoint *sync_ep)
{
	int err;

	if (ep->use_count != 0) {
		usb_audio_warn(ep->chip,
			 "Unable to change format on ep #%x: already in use\n",
			 ep->ep_num);
		return -EBUSY;
	}

	/* release old buffers, if any */
	release_urbs(ep, 0);

	ep->datainterval = fmt->datainterval;
	ep->maxpacksize = fmt->maxpacksize;
	ep->fill_max = !!(fmt->attributes & UAC_EP_CS_ATTR_FILL_MAX);

	if (snd_usb_get_speed(ep->chip->dev) == USB_SPEED_FULL)
		ep->freqn = get_usb_full_speed_rate(rate);
	else
		ep->freqn = get_usb_high_speed_rate(rate);

	/* calculate the frequency in 16.16 format */
	ep->freqm = ep->freqn;
	ep->freqshift = INT_MIN;

	ep->phase = 0;

	switch (ep->type) {
	case  SND_USB_ENDPOINT_TYPE_DATA:
		err = data_ep_set_params(ep, pcm_format, channels,
					 period_bytes, period_frames,
					 buffer_periods, fmt, sync_ep);
		break;
	case  SND_USB_ENDPOINT_TYPE_SYNC:
		err = sync_ep_set_params(ep);
		break;
	default:
		err = -EINVAL;
	}

	usb_audio_dbg(ep->chip,
		"Setting params for ep #%x (type %d, %d urbs), ret=%d\n",
		ep->ep_num, ep->type, ep->nurbs, err);

	return err;
}

/**
 * snd_usb_endpoint_start: start an snd_usb_endpoint
 *
 * @ep:		the endpoint to start
 * @can_sleep:	flag indicating whether the operation is executed in
 * 		non-atomic context
 *
 * A call to this function will increment the use count of the endpoint.
 * In case it is not already running, the URBs for this endpoint will be
 * submitted. Otherwise, this function does nothing.
 *
 * Must be balanced to calls of snd_usb_endpoint_stop().
 *
 * Returns an error if the URB submission failed, 0 in all other cases.
 */
int snd_usb_endpoint_start(struct snd_usb_endpoint *ep, bool can_sleep)
{
	int err;
	unsigned int i;

	if (atomic_read(&ep->chip->shutdown))
		return -EBADFD;

	/* already running? */
	if (++ep->use_count != 1)
		return 0;

	/* just to be sure */
	deactivate_urbs(ep, false);
	if (can_sleep)
		wait_clear_urbs(ep);

	ep->active_mask = 0;
	ep->unlink_mask = 0;
	ep->phase = 0;

	snd_usb_endpoint_start_quirk(ep);

	/*
	 * If this endpoint has a data endpoint as implicit feedback source,
	 * don't start the urbs here. Instead, mark them all as available,
	 * wait for the record urbs to return and queue the playback urbs
	 * from that context.
	 */

	set_bit(EP_FLAG_RUNNING, &ep->flags);

	if (snd_usb_endpoint_implicit_feedback_sink(ep)) {
		for (i = 0; i < ep->nurbs; i++) {
			struct snd_urb_ctx *ctx = ep->urb + i;
			list_add_tail(&ctx->ready_list, &ep->ready_playback_urbs);
		}

		return 0;
	}

	for (i = 0; i < ep->nurbs; i++) {
		struct urb *urb = ep->urb[i].urb;

		if (snd_BUG_ON(!urb))
			goto __error;

		if (usb_pipeout(ep->pipe)) {
			prepare_outbound_urb(ep, urb->context);
		} else {
			prepare_inbound_urb(ep, urb->context);
		}

		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0) {
			usb_audio_err(ep->chip,
				"cannot submit urb %d, error %d: %s\n",
				i, err, usb_error_string(err));
			goto __error;
		}
		set_bit(i, &ep->active_mask);
	}

	return 0;

__error:
	clear_bit(EP_FLAG_RUNNING, &ep->flags);
	ep->use_count--;
	deactivate_urbs(ep, false);
	return -EPIPE;
}

/**
 * snd_usb_endpoint_stop: stop an snd_usb_endpoint
 *
 * @ep: the endpoint to stop (may be NULL)
 *
 * A call to this function will decrement the use count of the endpoint.
 * In case the last user has requested the endpoint stop, the URBs will
 * actually be deactivated.
 *
 * Must be balanced to calls of snd_usb_endpoint_start().
 *
 * The caller needs to synchronize the pending stop operation via
 * snd_usb_endpoint_sync_pending_stop().
 */
void snd_usb_endpoint_stop(struct snd_usb_endpoint *ep)
{
	if (!ep)
		return;

	if (snd_BUG_ON(ep->use_count == 0))
		return;

	if (--ep->use_count == 0) {
		deactivate_urbs(ep, false);
		ep->data_subs = NULL;
		ep->sync_slave = NULL;
		ep->retire_data_urb = NULL;
		ep->prepare_data_urb = NULL;
		set_bit(EP_FLAG_STOPPING, &ep->flags);
	}
}

/**
 * snd_usb_endpoint_deactivate: deactivate an snd_usb_endpoint
 *
 * @ep: the endpoint to deactivate
 *
 * If the endpoint is not currently in use, this functions will
 * deactivate its associated URBs.
 *
 * In case of any active users, this functions does nothing.
 */
void snd_usb_endpoint_deactivate(struct snd_usb_endpoint *ep)
{
	if (!ep)
		return;

	if (ep->use_count != 0)
		return;

	deactivate_urbs(ep, true);
	wait_clear_urbs(ep);
}

/**
 * snd_usb_endpoint_release: Tear down an snd_usb_endpoint
 *
 * @ep: the endpoint to release
 *
 * This function does not care for the endpoint's use count but will tear
 * down all the streaming URBs immediately.
 */
void snd_usb_endpoint_release(struct snd_usb_endpoint *ep)
{
	release_urbs(ep, 1);
}

/**
 * snd_usb_endpoint_free: Free the resources of an snd_usb_endpoint
 *
 * @ep: the endpoint to free
 *
 * This free all resources of the given ep.
 */
void snd_usb_endpoint_free(struct snd_usb_endpoint *ep)
{
	kfree(ep);
}

/**
 * snd_usb_handle_sync_urb: parse an USB sync packet
 *
 * @ep: the endpoint to handle the packet
 * @sender: the sending endpoint
 * @urb: the received packet
 *
 * This function is called from the context of an endpoint that received
 * the packet and is used to let another endpoint object handle the payload.
 */
void snd_usb_handle_sync_urb(struct snd_usb_endpoint *ep,
			     struct snd_usb_endpoint *sender,
			     const struct urb *urb)
{
	int shift;
	unsigned int f;
	unsigned long flags;

	snd_BUG_ON(ep == sender);

	/*
	 * In case the endpoint is operating in implicit feedback mode, prepare
	 * a new outbound URB that has the same layout as the received packet
	 * and add it to the list of pending urbs. queue_pending_output_urbs()
	 * will take care of them later.
	 */
	if (snd_usb_endpoint_implicit_feedback_sink(ep) &&
	    ep->use_count != 0) {

		/* implicit feedback case */
		int i, bytes = 0;
		struct snd_urb_ctx *in_ctx;
		struct snd_usb_packet_info *out_packet;

		in_ctx = urb->context;

		/* Count overall packet size */
		for (i = 0; i < in_ctx->packets; i++)
			if (urb->iso_frame_desc[i].status == 0)
				bytes += urb->iso_frame_desc[i].actual_length;

		/*
		 * skip empty packets. At least M-Audio's Fast Track Ultra stops
		 * streaming once it received a 0-byte OUT URB
		 */
		if (bytes == 0)
			return;

		spin_lock_irqsave(&ep->lock, flags);
		out_packet = ep->next_packet + ep->next_packet_write_pos;

		/*
		 * Iterate through the inbound packet and prepare the lengths
		 * for the output packet. The OUT packet we are about to send
		 * will have the same amount of payload bytes per stride as the
		 * IN packet we just received. Since the actual size is scaled
		 * by the stride, use the sender stride to calculate the length
		 * in case the number of channels differ between the implicitly
		 * fed-back endpoint and the synchronizing endpoint.
		 */

		out_packet->packets = in_ctx->packets;
		for (i = 0; i < in_ctx->packets; i++) {
			if (urb->iso_frame_desc[i].status == 0)
				out_packet->packet_size[i] =
					urb->iso_frame_desc[i].actual_length / sender->stride;
			else
				out_packet->packet_size[i] = 0;
		}

		ep->next_packet_write_pos++;
		ep->next_packet_write_pos %= MAX_URBS;
		spin_unlock_irqrestore(&ep->lock, flags);
		queue_pending_output_urbs(ep);

		return;
	}

	/*
	 * process after playback sync complete
	 *
	 * Full speed devices report feedback values in 10.14 format as samples
	 * per frame, high speed devices in 16.16 format as samples per
	 * microframe.
	 *
	 * Because the Audio Class 1 spec was written before USB 2.0, many high
	 * speed devices use a wrong interpretation, some others use an
	 * entirely different format.
	 *
	 * Therefore, we cannot predict what format any particular device uses
	 * and must detect it automatically.
	 */

	if (urb->iso_frame_desc[0].status != 0 ||
	    urb->iso_frame_desc[0].actual_length < 3)
		return;

	f = le32_to_cpup(urb->transfer_buffer);
	if (urb->iso_frame_desc[0].actual_length == 3)
		f &= 0x00ffffff;
	else
		f &= 0x0fffffff;

	if (f == 0)
		return;

	if (unlikely(sender->udh01_fb_quirk)) {
		/*
		 * The TEAC UD-H01 firmware sometimes changes the feedback value
		 * by +/- 0x1.0000.
		 */
		if (f < ep->freqn - 0x8000)
			f += 0x10000;
		else if (f > ep->freqn + 0x8000)
			f -= 0x10000;
	} else if (unlikely(ep->freqshift == INT_MIN)) {
		/*
		 * The first time we see a feedback value, determine its format
		 * by shifting it left or right until it matches the nominal
		 * frequency value.  This assumes that the feedback does not
		 * differ from the nominal value more than +50% or -25%.
		 */
		shift = 0;
		while (f < ep->freqn - ep->freqn / 4) {
			f <<= 1;
			shift++;
		}
		while (f > ep->freqn + ep->freqn / 2) {
			f >>= 1;
			shift--;
		}
		ep->freqshift = shift;
	} else if (ep->freqshift >= 0)
		f <<= ep->freqshift;
	else
		f >>= -ep->freqshift;

	if (likely(f >= ep->freqn - ep->freqn / 8 && f <= ep->freqmax)) {
		/*
		 * If the frequency looks valid, set it.
		 * This value is referred to in prepare_playback_urb().
		 */
		spin_lock_irqsave(&ep->lock, flags);
		ep->freqm = f;
		spin_unlock_irqrestore(&ep->lock, flags);
	} else {
		/*
		 * Out of range; maybe the shift value is wrong.
		 * Reset it so that we autodetect again the next time.
		 */
		ep->freqshift = INT_MIN;
	}
}

