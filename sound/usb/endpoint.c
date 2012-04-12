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

#define EP_FLAG_ACTIVATED	0
#define EP_FLAG_RUNNING		1

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
 * unlink active urbs.
 */
static int deactivate_urbs_old(struct snd_usb_substream *subs, int force, int can_sleep)
{
	struct snd_usb_audio *chip = subs->stream->chip;
	unsigned int i;
	int async;

	subs->running = 0;

	if (!force && subs->stream->chip->shutdown) /* to be sure... */
		return -EBADFD;

	async = !can_sleep && chip->async_unlink;

	if (!async && in_interrupt())
		return 0;

	for (i = 0; i < subs->nurbs; i++) {
		if (test_bit(i, &subs->active_mask)) {
			if (!test_and_set_bit(i, &subs->unlink_mask)) {
				struct urb *u = subs->dataurb[i].urb;
				if (async)
					usb_unlink_urb(u);
				else
					usb_kill_urb(u);
			}
		}
	}
	if (subs->syncpipe) {
		for (i = 0; i < SYNC_URBS; i++) {
			if (test_bit(i+16, &subs->active_mask)) {
				if (!test_and_set_bit(i+16, &subs->unlink_mask)) {
					struct urb *u = subs->syncurb[i].urb;
					if (async)
						usb_unlink_urb(u);
					else
						usb_kill_urb(u);
				}
			}
		}
	}
	return 0;
}


/*
 * release a urb data
 */
static void release_urb_ctx(struct snd_urb_ctx *u)
{
	if (u->urb) {
		if (u->buffer_size)
			usb_free_coherent(u->subs->dev, u->buffer_size,
					u->urb->transfer_buffer,
					u->urb->transfer_dma);
		usb_free_urb(u->urb);
		u->urb = NULL;
	}
}

/*
 *  wait until all urbs are processed.
 */
static int wait_clear_urbs_old(struct snd_usb_substream *subs)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(1000);
	unsigned int i;
	int alive;

	do {
		alive = 0;
		for (i = 0; i < subs->nurbs; i++) {
			if (test_bit(i, &subs->active_mask))
				alive++;
		}
		if (subs->syncpipe) {
			for (i = 0; i < SYNC_URBS; i++) {
				if (test_bit(i + 16, &subs->active_mask))
					alive++;
			}
		}
		if (! alive)
			break;
		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, end_time));
	if (alive)
		snd_printk(KERN_ERR "timeout: still %d active urbs..\n", alive);
	return 0;
}

/*
 * release a substream
 */
void snd_usb_release_substream_urbs(struct snd_usb_substream *subs, int force)
{
	int i;

	/* stop urbs (to be sure) */
	deactivate_urbs_old(subs, force, 1);
	wait_clear_urbs_old(subs);

	for (i = 0; i < MAX_URBS; i++)
		release_urb_ctx(&subs->dataurb[i]);
	for (i = 0; i < SYNC_URBS; i++)
		release_urb_ctx(&subs->syncurb[i]);
	usb_free_coherent(subs->dev, SYNC_URBS * 4,
			subs->syncbuf, subs->sync_dma);
	subs->syncbuf = NULL;
	subs->nurbs = 0;
}

/*
 * complete callback from data urb
 */
static void snd_complete_urb_old(struct urb *urb)
{
	struct snd_urb_ctx *ctx = urb->context;
	struct snd_usb_substream *subs = ctx->subs;
	struct snd_pcm_substream *substream = ctx->subs->pcm_substream;
	int err = 0;

	if ((subs->running && subs->ops.retire(subs, substream->runtime, urb)) ||
	    !subs->running || /* can be stopped during retire callback */
	    (err = subs->ops.prepare(subs, substream->runtime, urb)) < 0 ||
	    (err = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
		clear_bit(ctx->index, &subs->active_mask);
		if (err < 0) {
			snd_printd(KERN_ERR "cannot submit urb (err = %d)\n", err);
			snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
		}
	}
}


/*
 * complete callback from sync urb
 */
static void snd_complete_sync_urb(struct urb *urb)
{
	struct snd_urb_ctx *ctx = urb->context;
	struct snd_usb_substream *subs = ctx->subs;
	struct snd_pcm_substream *substream = ctx->subs->pcm_substream;
	int err = 0;

	if ((subs->running && subs->ops.retire_sync(subs, substream->runtime, urb)) ||
	    !subs->running || /* can be stopped during retire callback */
	    (err = subs->ops.prepare_sync(subs, substream->runtime, urb)) < 0 ||
	    (err = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
		clear_bit(ctx->index + 16, &subs->active_mask);
		if (err < 0) {
			snd_printd(KERN_ERR "cannot submit sync urb (err = %d)\n", err);
			snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
		}
	}
}


/*
 * initialize a substream for plaback/capture
 */
int snd_usb_init_substream_urbs(struct snd_usb_substream *subs,
				unsigned int period_bytes,
				unsigned int rate,
				unsigned int frame_bits)
{
	unsigned int maxsize, i;
	int is_playback = subs->direction == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int urb_packs, total_packs, packs_per_ms;
	struct snd_usb_audio *chip = subs->stream->chip;

	/* calculate the frequency in 16.16 format */
	if (snd_usb_get_speed(subs->dev) == USB_SPEED_FULL)
		subs->freqn = get_usb_full_speed_rate(rate);
	else
		subs->freqn = get_usb_high_speed_rate(rate);
	subs->freqm = subs->freqn;
	subs->freqshift = INT_MIN;
	/* calculate max. frequency */
	if (subs->maxpacksize) {
		/* whatever fits into a max. size packet */
		maxsize = subs->maxpacksize;
		subs->freqmax = (maxsize / (frame_bits >> 3))
				<< (16 - subs->datainterval);
	} else {
		/* no max. packet size: just take 25% higher than nominal */
		subs->freqmax = subs->freqn + (subs->freqn >> 2);
		maxsize = ((subs->freqmax + 0xffff) * (frame_bits >> 3))
				>> (16 - subs->datainterval);
	}
	subs->phase = 0;

	if (subs->fill_max)
		subs->curpacksize = subs->maxpacksize;
	else
		subs->curpacksize = maxsize;

	if (snd_usb_get_speed(subs->dev) != USB_SPEED_FULL)
		packs_per_ms = 8 >> subs->datainterval;
	else
		packs_per_ms = 1;

	if (is_playback) {
		urb_packs = max(chip->nrpacks, 1);
		urb_packs = min(urb_packs, (unsigned int)MAX_PACKS);
	} else
		urb_packs = 1;
	urb_packs *= packs_per_ms;
	if (subs->syncpipe)
		urb_packs = min(urb_packs, 1U << subs->syncinterval);

	/* decide how many packets to be used */
	if (is_playback) {
		unsigned int minsize, maxpacks;
		/* determine how small a packet can be */
		minsize = (subs->freqn >> (16 - subs->datainterval))
			  * (frame_bits >> 3);
		/* with sync from device, assume it can be 12% lower */
		if (subs->syncpipe)
			minsize -= minsize >> 3;
		minsize = max(minsize, 1u);
		total_packs = (period_bytes + minsize - 1) / minsize;
		/* we need at least two URBs for queueing */
		if (total_packs < 2) {
			total_packs = 2;
		} else {
			/* and we don't want too long a queue either */
			maxpacks = max(MAX_QUEUE * packs_per_ms, urb_packs * 2);
			total_packs = min(total_packs, maxpacks);
		}
	} else {
		while (urb_packs > 1 && urb_packs * maxsize >= period_bytes)
			urb_packs >>= 1;
		total_packs = MAX_URBS * urb_packs;
	}
	subs->nurbs = (total_packs + urb_packs - 1) / urb_packs;
	if (subs->nurbs > MAX_URBS) {
		/* too much... */
		subs->nurbs = MAX_URBS;
		total_packs = MAX_URBS * urb_packs;
	} else if (subs->nurbs < 2) {
		/* too little - we need at least two packets
		 * to ensure contiguous playback/capture
		 */
		subs->nurbs = 2;
	}

	/* allocate and initialize data urbs */
	for (i = 0; i < subs->nurbs; i++) {
		struct snd_urb_ctx *u = &subs->dataurb[i];
		u->index = i;
		u->subs = subs;
		u->packets = (i + 1) * total_packs / subs->nurbs
			- i * total_packs / subs->nurbs;
		u->buffer_size = maxsize * u->packets;
		if (subs->fmt_type == UAC_FORMAT_TYPE_II)
			u->packets++; /* for transfer delimiter */
		u->urb = usb_alloc_urb(u->packets, GFP_KERNEL);
		if (!u->urb)
			goto out_of_memory;
		u->urb->transfer_buffer =
			usb_alloc_coherent(subs->dev, u->buffer_size,
					   GFP_KERNEL, &u->urb->transfer_dma);
		if (!u->urb->transfer_buffer)
			goto out_of_memory;
		u->urb->pipe = subs->datapipe;
		u->urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		u->urb->interval = 1 << subs->datainterval;
		u->urb->context = u;
		u->urb->complete = snd_complete_urb_old;
	}

	if (subs->syncpipe) {
		/* allocate and initialize sync urbs */
		subs->syncbuf = usb_alloc_coherent(subs->dev, SYNC_URBS * 4,
						 GFP_KERNEL, &subs->sync_dma);
		if (!subs->syncbuf)
			goto out_of_memory;
		for (i = 0; i < SYNC_URBS; i++) {
			struct snd_urb_ctx *u = &subs->syncurb[i];
			u->index = i;
			u->subs = subs;
			u->packets = 1;
			u->urb = usb_alloc_urb(1, GFP_KERNEL);
			if (!u->urb)
				goto out_of_memory;
			u->urb->transfer_buffer = subs->syncbuf + i * 4;
			u->urb->transfer_dma = subs->sync_dma + i * 4;
			u->urb->transfer_buffer_length = 4;
			u->urb->pipe = subs->syncpipe;
			u->urb->transfer_flags = URB_ISO_ASAP |
						 URB_NO_TRANSFER_DMA_MAP;
			u->urb->number_of_packets = 1;
			u->urb->interval = 1 << subs->syncinterval;
			u->urb->context = u;
			u->urb->complete = snd_complete_sync_urb;
		}
	}
	return 0;

out_of_memory:
	snd_usb_release_substream_urbs(subs, 0);
	return -ENOMEM;
}

/*
 * prepare urb for full speed capture sync pipe
 *
 * fill the length and offset of each urb descriptor.
 * the fixed 10.14 frequency is passed through the pipe.
 */
static int prepare_capture_sync_urb(struct snd_usb_substream *subs,
				    struct snd_pcm_runtime *runtime,
				    struct urb *urb)
{
	unsigned char *cp = urb->transfer_buffer;
	struct snd_urb_ctx *ctx = urb->context;

	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	urb->iso_frame_desc[0].length = 3;
	urb->iso_frame_desc[0].offset = 0;
	cp[0] = subs->freqn >> 2;
	cp[1] = subs->freqn >> 10;
	cp[2] = subs->freqn >> 18;
	return 0;
}

/*
 * prepare urb for high speed capture sync pipe
 *
 * fill the length and offset of each urb descriptor.
 * the fixed 12.13 frequency is passed as 16.16 through the pipe.
 */
static int prepare_capture_sync_urb_hs(struct snd_usb_substream *subs,
				       struct snd_pcm_runtime *runtime,
				       struct urb *urb)
{
	unsigned char *cp = urb->transfer_buffer;
	struct snd_urb_ctx *ctx = urb->context;

	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	urb->iso_frame_desc[0].length = 4;
	urb->iso_frame_desc[0].offset = 0;
	cp[0] = subs->freqn;
	cp[1] = subs->freqn >> 8;
	cp[2] = subs->freqn >> 16;
	cp[3] = subs->freqn >> 24;
	return 0;
}

/*
 * process after capture sync complete
 * - nothing to do
 */
static int retire_capture_sync_urb(struct snd_usb_substream *subs,
				   struct snd_pcm_runtime *runtime,
				   struct urb *urb)
{
	return 0;
}

/*
 * prepare urb for capture data pipe
 *
 * fill the offset and length of each descriptor.
 *
 * we use a temporary buffer to write the captured data.
 * since the length of written data is determined by host, we cannot
 * write onto the pcm buffer directly...  the data is thus copied
 * later at complete callback to the global buffer.
 */
static int prepare_capture_urb(struct snd_usb_substream *subs,
			       struct snd_pcm_runtime *runtime,
			       struct urb *urb)
{
	int i, offs;
	struct snd_urb_ctx *ctx = urb->context;

	offs = 0;
	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	for (i = 0; i < ctx->packets; i++) {
		urb->iso_frame_desc[i].offset = offs;
		urb->iso_frame_desc[i].length = subs->curpacksize;
		offs += subs->curpacksize;
	}
	urb->transfer_buffer_length = offs;
	urb->number_of_packets = ctx->packets;
	return 0;
}

/*
 * process after capture complete
 *
 * copy the data from each desctiptor to the pcm buffer, and
 * update the current position.
 */
static int retire_capture_urb(struct snd_usb_substream *subs,
			      struct snd_pcm_runtime *runtime,
			      struct urb *urb)
{
	unsigned long flags;
	unsigned char *cp;
	int i;
	unsigned int stride, frames, bytes, oldptr;
	int period_elapsed = 0;

	stride = runtime->frame_bits >> 3;

	for (i = 0; i < urb->number_of_packets; i++) {
		cp = (unsigned char *)urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		if (urb->iso_frame_desc[i].status && printk_ratelimit()) {
			snd_printdd("frame %d active: %d\n", i, urb->iso_frame_desc[i].status);
			// continue;
		}
		bytes = urb->iso_frame_desc[i].actual_length;
		frames = bytes / stride;
		if (!subs->txfr_quirk)
			bytes = frames * stride;
		if (bytes % (runtime->sample_bits >> 3) != 0) {
#ifdef CONFIG_SND_DEBUG_VERBOSE
			int oldbytes = bytes;
#endif
			bytes = frames * stride;
			snd_printdd(KERN_ERR "Corrected urb data len. %d->%d\n",
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
	return 0;
}

/*
 * Process after capture complete when paused.  Nothing to do.
 */
static int retire_paused_capture_urb(struct snd_usb_substream *subs,
				     struct snd_pcm_runtime *runtime,
				     struct urb *urb)
{
	return 0;
}


/*
 * prepare urb for playback sync pipe
 *
 * set up the offset and length to receive the current frequency.
 */
static int prepare_playback_sync_urb(struct snd_usb_substream *subs,
				     struct snd_pcm_runtime *runtime,
				     struct urb *urb)
{
	struct snd_urb_ctx *ctx = urb->context;

	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	urb->iso_frame_desc[0].length = min(4u, ctx->subs->syncmaxsize);
	urb->iso_frame_desc[0].offset = 0;
	return 0;
}

/*
 * process after playback sync complete
 *
 * Full speed devices report feedback values in 10.14 format as samples per
 * frame, high speed devices in 16.16 format as samples per microframe.
 * Because the Audio Class 1 spec was written before USB 2.0, many high speed
 * devices use a wrong interpretation, some others use an entirely different
 * format.  Therefore, we cannot predict what format any particular device uses
 * and must detect it automatically.
 */
static int retire_playback_sync_urb(struct snd_usb_substream *subs,
				    struct snd_pcm_runtime *runtime,
				    struct urb *urb)
{
	unsigned int f;
	int shift;
	unsigned long flags;

	if (urb->iso_frame_desc[0].status != 0 ||
	    urb->iso_frame_desc[0].actual_length < 3)
		return 0;

	f = le32_to_cpup(urb->transfer_buffer);
	if (urb->iso_frame_desc[0].actual_length == 3)
		f &= 0x00ffffff;
	else
		f &= 0x0fffffff;
	if (f == 0)
		return 0;

	if (unlikely(subs->freqshift == INT_MIN)) {
		/*
		 * The first time we see a feedback value, determine its format
		 * by shifting it left or right until it matches the nominal
		 * frequency value.  This assumes that the feedback does not
		 * differ from the nominal value more than +50% or -25%.
		 */
		shift = 0;
		while (f < subs->freqn - subs->freqn / 4) {
			f <<= 1;
			shift++;
		}
		while (f > subs->freqn + subs->freqn / 2) {
			f >>= 1;
			shift--;
		}
		subs->freqshift = shift;
	}
	else if (subs->freqshift >= 0)
		f <<= subs->freqshift;
	else
		f >>= -subs->freqshift;

	if (likely(f >= subs->freqn - subs->freqn / 8 && f <= subs->freqmax)) {
		/*
		 * If the frequency looks valid, set it.
		 * This value is referred to in prepare_playback_urb().
		 */
		spin_lock_irqsave(&subs->lock, flags);
		subs->freqm = f;
		spin_unlock_irqrestore(&subs->lock, flags);
	} else {
		/*
		 * Out of range; maybe the shift value is wrong.
		 * Reset it so that we autodetect again the next time.
		 */
		subs->freqshift = INT_MIN;
	}

	return 0;
}

/* determine the number of frames in the next packet */
static int snd_usb_audio_next_packet_size(struct snd_usb_substream *subs)
{
	if (subs->fill_max)
		return subs->maxframesize;
	else {
		subs->phase = (subs->phase & 0xffff)
			+ (subs->freqm << subs->datainterval);
		return min(subs->phase >> 16, subs->maxframesize);
	}
}

/*
 * Prepare urb for streaming before playback starts or when paused.
 *
 * We don't have any data, so we send silence.
 */
static int prepare_nodata_playback_urb(struct snd_usb_substream *subs,
				       struct snd_pcm_runtime *runtime,
				       struct urb *urb)
{
	unsigned int i, offs, counts;
	struct snd_urb_ctx *ctx = urb->context;
	int stride = runtime->frame_bits >> 3;

	offs = 0;
	urb->dev = ctx->subs->dev;
	for (i = 0; i < ctx->packets; ++i) {
		counts = snd_usb_audio_next_packet_size(subs);
		urb->iso_frame_desc[i].offset = offs * stride;
		urb->iso_frame_desc[i].length = counts * stride;
		offs += counts;
	}
	urb->number_of_packets = ctx->packets;
	urb->transfer_buffer_length = offs * stride;
	memset(urb->transfer_buffer,
	       runtime->format == SNDRV_PCM_FORMAT_U8 ? 0x80 : 0,
	       offs * stride);
	return 0;
}

/*
 * prepare urb for playback data pipe
 *
 * Since a URB can handle only a single linear buffer, we must use double
 * buffering when the data to be transferred overflows the buffer boundary.
 * To avoid inconsistencies when updating hwptr_done, we use double buffering
 * for all URBs.
 */
static int prepare_playback_urb(struct snd_usb_substream *subs,
				struct snd_pcm_runtime *runtime,
				struct urb *urb)
{
	int i, stride;
	unsigned int counts, frames, bytes;
	unsigned long flags;
	int period_elapsed = 0;
	struct snd_urb_ctx *ctx = urb->context;

	stride = runtime->frame_bits >> 3;

	frames = 0;
	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	urb->number_of_packets = 0;
	spin_lock_irqsave(&subs->lock, flags);
	for (i = 0; i < ctx->packets; i++) {
		counts = snd_usb_audio_next_packet_size(subs);
		/* set up descriptor */
		urb->iso_frame_desc[i].offset = frames * stride;
		urb->iso_frame_desc[i].length = counts * stride;
		frames += counts;
		urb->number_of_packets++;
		subs->transfer_done += counts;
		if (subs->transfer_done >= runtime->period_size) {
			subs->transfer_done -= runtime->period_size;
			period_elapsed = 1;
			if (subs->fmt_type == UAC_FORMAT_TYPE_II) {
				if (subs->transfer_done > 0) {
					/* FIXME: fill-max mode is not
					 * supported yet */
					frames -= subs->transfer_done;
					counts -= subs->transfer_done;
					urb->iso_frame_desc[i].length =
						counts * stride;
					subs->transfer_done = 0;
				}
				i++;
				if (i < ctx->packets) {
					/* add a transfer delimiter */
					urb->iso_frame_desc[i].offset =
						frames * stride;
					urb->iso_frame_desc[i].length = 0;
					urb->number_of_packets++;
				}
				break;
			}
		}
		if (period_elapsed) /* finish at the period boundary */
			break;
	}
	bytes = frames * stride;
	if (subs->hwptr_done + bytes > runtime->buffer_size * stride) {
		/* err, the transferred area goes over buffer boundary. */
		unsigned int bytes1 =
			runtime->buffer_size * stride - subs->hwptr_done;
		memcpy(urb->transfer_buffer,
		       runtime->dma_area + subs->hwptr_done, bytes1);
		memcpy(urb->transfer_buffer + bytes1,
		       runtime->dma_area, bytes - bytes1);
	} else {
		memcpy(urb->transfer_buffer,
		       runtime->dma_area + subs->hwptr_done, bytes);
	}
	subs->hwptr_done += bytes;
	if (subs->hwptr_done >= runtime->buffer_size * stride)
		subs->hwptr_done -= runtime->buffer_size * stride;

	/* update delay with exact number of samples queued */
	runtime->delay = subs->last_delay;
	runtime->delay += frames;
	subs->last_delay = runtime->delay;

	/* realign last_frame_number */
	subs->last_frame_number = usb_get_current_frame_number(subs->dev);
	subs->last_frame_number &= 0xFF; /* keep 8 LSBs */

	spin_unlock_irqrestore(&subs->lock, flags);
	urb->transfer_buffer_length = bytes;
	if (period_elapsed)
		snd_pcm_period_elapsed(subs->pcm_substream);
	return 0;
}

/*
 * process after playback data complete
 * - decrease the delay count again
 */
static int retire_playback_urb(struct snd_usb_substream *subs,
			       struct snd_pcm_runtime *runtime,
			       struct urb *urb)
{
	unsigned long flags;
	int stride = runtime->frame_bits >> 3;
	int processed = urb->transfer_buffer_length / stride;
	int est_delay;

	spin_lock_irqsave(&subs->lock, flags);

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
		snd_printk(KERN_DEBUG "delay: estimated %d, actual %d\n",
			est_delay, subs->last_delay);

	spin_unlock_irqrestore(&subs->lock, flags);
	return 0;
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

/*
 * set up and start data/sync urbs
 */
static int start_urbs(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime)
{
	unsigned int i;
	int err;

	if (subs->stream->chip->shutdown)
		return -EBADFD;

	for (i = 0; i < subs->nurbs; i++) {
		if (snd_BUG_ON(!subs->dataurb[i].urb))
			return -EINVAL;
		if (subs->ops.prepare(subs, runtime, subs->dataurb[i].urb) < 0) {
			snd_printk(KERN_ERR "cannot prepare datapipe for urb %d\n", i);
			goto __error;
		}
	}
	if (subs->syncpipe) {
		for (i = 0; i < SYNC_URBS; i++) {
			if (snd_BUG_ON(!subs->syncurb[i].urb))
				return -EINVAL;
			if (subs->ops.prepare_sync(subs, runtime, subs->syncurb[i].urb) < 0) {
				snd_printk(KERN_ERR "cannot prepare syncpipe for urb %d\n", i);
				goto __error;
			}
		}
	}

	subs->active_mask = 0;
	subs->unlink_mask = 0;
	subs->running = 1;
	for (i = 0; i < subs->nurbs; i++) {
		err = usb_submit_urb(subs->dataurb[i].urb, GFP_ATOMIC);
		if (err < 0) {
			snd_printk(KERN_ERR "cannot submit datapipe "
				   "for urb %d, error %d: %s\n",
				   i, err, usb_error_string(err));
			goto __error;
		}
		set_bit(i, &subs->active_mask);
	}
	if (subs->syncpipe) {
		for (i = 0; i < SYNC_URBS; i++) {
			err = usb_submit_urb(subs->syncurb[i].urb, GFP_ATOMIC);
			if (err < 0) {
				snd_printk(KERN_ERR "cannot submit syncpipe "
					   "for urb %d, error %d: %s\n",
					   i, err, usb_error_string(err));
				goto __error;
			}
			set_bit(i + 16, &subs->active_mask);
		}
	}
	return 0;

 __error:
	// snd_pcm_stop(subs->pcm_substream, SNDRV_PCM_STATE_XRUN);
	deactivate_urbs_old(subs, 0, 0);
	return -EPIPE;
}


/*
 */
static struct snd_urb_ops audio_urb_ops[2] = {
	{
		.prepare =	prepare_nodata_playback_urb,
		.retire =	retire_playback_urb,
		.prepare_sync =	prepare_playback_sync_urb,
		.retire_sync =	retire_playback_sync_urb,
	},
	{
		.prepare =	prepare_capture_urb,
		.retire =	retire_capture_urb,
		.prepare_sync =	prepare_capture_sync_urb,
		.retire_sync =	retire_capture_sync_urb,
	},
};

/*
 * initialize the substream instance.
 */

void snd_usb_init_substream(struct snd_usb_stream *as,
			    int stream, struct audioformat *fp)
{
	struct snd_usb_substream *subs = &as->substream[stream];

	INIT_LIST_HEAD(&subs->fmt_list);
	spin_lock_init(&subs->lock);

	subs->stream = as;
	subs->direction = stream;
	subs->dev = as->chip->dev;
	subs->txfr_quirk = as->chip->txfr_quirk;
	subs->ops = audio_urb_ops[stream];
	if (snd_usb_get_speed(subs->dev) >= USB_SPEED_HIGH)
		subs->ops.prepare_sync = prepare_capture_sync_urb_hs;

	snd_usb_set_pcm_ops(as->pcm, stream);

	list_add_tail(&fp->list, &subs->fmt_list);
	subs->formats |= fp->formats;
	subs->endpoint = fp->endpoint;
	subs->num_formats++;
	subs->fmt_type = fp->fmt_type;
}

int snd_usb_substream_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		subs->ops.prepare = prepare_playback_urb;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		return deactivate_urbs_old(subs, 0, 0);
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		subs->ops.prepare = prepare_nodata_playback_urb;
		return 0;
	}

	return -EINVAL;
}

int snd_usb_substream_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		subs->ops.retire = retire_capture_urb;
		return start_urbs(subs, substream->runtime);
	case SNDRV_PCM_TRIGGER_STOP:
		return deactivate_urbs_old(subs, 0, 0);
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		subs->ops.retire = retire_paused_capture_urb;
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		subs->ops.retire = retire_capture_urb;
		return 0;
	}

	return -EINVAL;
}

int snd_usb_substream_prepare(struct snd_usb_substream *subs,
			      struct snd_pcm_runtime *runtime)
{
	/* clear urbs (to be sure) */
	deactivate_urbs_old(subs, 0, 1);
	wait_clear_urbs_old(subs);

	/* for playback, submit the URBs now; otherwise, the first hwptr_done
	 * updates for all URBs would happen at the same time when starting */
	if (subs->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		subs->ops.prepare = prepare_nodata_playback_urb;
		return start_urbs(subs, runtime);
	}

	return 0;
}

int snd_usb_endpoint_implict_feedback_sink(struct snd_usb_endpoint *ep)
{
	return  ep->sync_master &&
		ep->sync_master->type == SND_USB_ENDPOINT_TYPE_DATA &&
		ep->type == SND_USB_ENDPOINT_TYPE_DATA &&
		usb_pipeout(ep->pipe);
}

/* determine the number of frames in the next packet */
static int next_packet_size(struct snd_usb_endpoint *ep)
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

	if (ep->sync_slave)
		snd_usb_handle_sync_urb(ep->sync_slave, ep, urb);

	if (ep->retire_data_urb)
		ep->retire_data_urb(ep->data_subs, urb);
}

static void prepare_outbound_urb_sizes(struct snd_usb_endpoint *ep,
				       struct snd_urb_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->packets; ++i)
		ctx->packet_size[i] = next_packet_size(ep);
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
				int counts = ctx->packet_size[i];
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

static void queue_pending_output_urbs(struct snd_usb_endpoint *ep)
{
	while (test_bit(EP_FLAG_RUNNING, &ep->flags)) {

		unsigned long flags;
		struct snd_usb_packet_info *packet;
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

		prepare_outbound_urb(ep, ctx);

		err = usb_submit_urb(ctx->urb, GFP_ATOMIC);
		if (err < 0)
			snd_printk(KERN_ERR "Unable to submit urb #%d: %d (urb %p)\n",
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
	int err;

	if (unlikely(urb->status == -ENOENT ||		/* unlinked */
		     urb->status == -ENODEV ||		/* device removed */
		     urb->status == -ECONNRESET ||	/* unlinked */
		     urb->status == -ESHUTDOWN ||	/* device disabled */
		     ep->chip->shutdown))		/* device disconnected */
		goto exit_clear;

	if (usb_pipeout(ep->pipe)) {
		retire_outbound_urb(ep, ctx);
		/* can be stopped during retire callback */
		if (unlikely(!test_bit(EP_FLAG_RUNNING, &ep->flags)))
			goto exit_clear;

		if (snd_usb_endpoint_implict_feedback_sink(ep)) {
			unsigned long flags;

			spin_lock_irqsave(&ep->lock, flags);
			list_add_tail(&ctx->ready_list, &ep->ready_playback_urbs);
			spin_unlock_irqrestore(&ep->lock, flags);
			queue_pending_output_urbs(ep);

			goto exit_clear;
		}

		prepare_outbound_urb_sizes(ep, ctx);
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

	snd_printk(KERN_ERR "cannot submit urb (err = %d)\n", err);
	//snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);

exit_clear:
	clear_bit(ctx->index, &ep->active_mask);
}

struct snd_usb_endpoint *snd_usb_add_endpoint(struct snd_usb_audio *chip,
					      struct usb_host_interface *alts,
					      int ep_num, int direction, int type)
{
	struct list_head *p;
	struct snd_usb_endpoint *ep;
	int ret, is_playback = direction == SNDRV_PCM_STREAM_PLAYBACK;

	mutex_lock(&chip->mutex);

	list_for_each(p, &chip->ep_list) {
		ep = list_entry(p, struct snd_usb_endpoint, list);
		if (ep->ep_num == ep_num &&
		    ep->iface == alts->desc.bInterfaceNumber &&
		    ep->alt_idx == alts->desc.bAlternateSetting) {
			snd_printdd(KERN_DEBUG "Re-using EP %x in iface %d,%d @%p\n",
					ep_num, ep->iface, ep->alt_idx, ep);
			goto __exit_unlock;
		}
	}

	snd_printdd(KERN_DEBUG "Creating new %s %s endpoint #%x\n",
		    is_playback ? "playback" : "capture",
		    type == SND_USB_ENDPOINT_TYPE_DATA ? "data" : "sync",
		    ep_num);

	/* select the alt setting once so the endpoints become valid */
	ret = usb_set_interface(chip->dev, alts->desc.bInterfaceNumber,
				alts->desc.bAlternateSetting);
	if (ret < 0) {
		snd_printk(KERN_ERR "%s(): usb_set_interface() failed, ret = %d\n",
					__func__, ret);
		ep = NULL;
		goto __exit_unlock;
	}

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		goto __exit_unlock;

	ep->chip = chip;
	spin_lock_init(&ep->lock);
	ep->type = type;
	ep->ep_num = ep_num;
	ep->iface = alts->desc.bInterfaceNumber;
	ep->alt_idx = alts->desc.bAlternateSetting;
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
	unsigned int i;
	int alive;

	do {
		alive = 0;
		for (i = 0; i < ep->nurbs; i++)
			if (test_bit(i, &ep->active_mask))
				alive++;

		if (!alive)
			break;

		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, end_time));

	if (alive)
		snd_printk(KERN_ERR "timeout: still %d active urbs on EP #%x\n",
					alive, ep->ep_num);

	return 0;
}

/*
 * unlink active urbs.
 */
static int deactivate_urbs(struct snd_usb_endpoint *ep, int force, int can_sleep)
{
	unsigned long flags;
	unsigned int i;
	int async;

	if (!force && ep->chip->shutdown) /* to be sure... */
		return -EBADFD;

	async = !can_sleep && ep->chip->async_unlink;

	clear_bit(EP_FLAG_RUNNING, &ep->flags);

	INIT_LIST_HEAD(&ep->ready_playback_urbs);
	ep->next_packet_read_pos = 0;
	ep->next_packet_write_pos = 0;

	if (!async && in_interrupt())
		return 0;

	for (i = 0; i < ep->nurbs; i++) {
		if (test_bit(i, &ep->active_mask)) {
			if (!test_and_set_bit(i, &ep->unlink_mask)) {
				struct urb *u = ep->urb[i].urb;
				if (async)
					usb_unlink_urb(u);
				else
					usb_kill_urb(u);
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
	deactivate_urbs(ep, force, 1);
	wait_clear_urbs(ep);

	for (i = 0; i < ep->nurbs; i++)
		release_urb_ctx(&ep->urb[i]);

	if (ep->syncbuf)
		usb_free_coherent(ep->chip->dev, SYNC_URBS * 4,
				  ep->syncbuf, ep->sync_dma);

	ep->syncbuf = NULL;
	ep->nurbs = 0;
}

static int data_ep_set_params(struct snd_usb_endpoint *ep,
			      struct snd_pcm_hw_params *hw_params,
			      struct audioformat *fmt,
			      struct snd_usb_endpoint *sync_ep)
{
	unsigned int maxsize, i, urb_packs, total_packs, packs_per_ms;
	int period_bytes = params_period_bytes(hw_params);
	int format = params_format(hw_params);
	int is_playback = usb_pipeout(ep->pipe);
	int frame_bits = snd_pcm_format_physical_width(params_format(hw_params)) *
							params_channels(hw_params);

	ep->datainterval = fmt->datainterval;
	ep->stride = frame_bits >> 3;
	ep->silence_value = format == SNDRV_PCM_FORMAT_U8 ? 0x80 : 0;

	/* calculate max. frequency */
	if (ep->maxpacksize) {
		/* whatever fits into a max. size packet */
		maxsize = ep->maxpacksize;
		ep->freqmax = (maxsize / (frame_bits >> 3))
				<< (16 - ep->datainterval);
	} else {
		/* no max. packet size: just take 25% higher than nominal */
		ep->freqmax = ep->freqn + (ep->freqn >> 2);
		maxsize = ((ep->freqmax + 0xffff) * (frame_bits >> 3))
				>> (16 - ep->datainterval);
	}

	if (ep->fill_max)
		ep->curpacksize = ep->maxpacksize;
	else
		ep->curpacksize = maxsize;

	if (snd_usb_get_speed(ep->chip->dev) != USB_SPEED_FULL)
		packs_per_ms = 8 >> ep->datainterval;
	else
		packs_per_ms = 1;

	if (is_playback && !snd_usb_endpoint_implict_feedback_sink(ep)) {
		urb_packs = max(ep->chip->nrpacks, 1);
		urb_packs = min(urb_packs, (unsigned int) MAX_PACKS);
	} else {
		urb_packs = 1;
	}

	urb_packs *= packs_per_ms;

	if (sync_ep && !snd_usb_endpoint_implict_feedback_sink(ep))
		urb_packs = min(urb_packs, 1U << sync_ep->syncinterval);

	/* decide how many packets to be used */
	if (is_playback && !snd_usb_endpoint_implict_feedback_sink(ep)) {
		unsigned int minsize, maxpacks;
		/* determine how small a packet can be */
		minsize = (ep->freqn >> (16 - ep->datainterval))
			  * (frame_bits >> 3);
		/* with sync from device, assume it can be 12% lower */
		if (sync_ep)
			minsize -= minsize >> 3;
		minsize = max(minsize, 1u);
		total_packs = (period_bytes + minsize - 1) / minsize;
		/* we need at least two URBs for queueing */
		if (total_packs < 2) {
			total_packs = 2;
		} else {
			/* and we don't want too long a queue either */
			maxpacks = max(MAX_QUEUE * packs_per_ms, urb_packs * 2);
			total_packs = min(total_packs, maxpacks);
		}
	} else {
		while (urb_packs > 1 && urb_packs * maxsize >= period_bytes)
			urb_packs >>= 1;
		total_packs = MAX_URBS * urb_packs;
	}

	ep->nurbs = (total_packs + urb_packs - 1) / urb_packs;
	if (ep->nurbs > MAX_URBS) {
		/* too much... */
		ep->nurbs = MAX_URBS;
		total_packs = MAX_URBS * urb_packs;
	} else if (ep->nurbs < 2) {
		/* too little - we need at least two packets
		 * to ensure contiguous playback/capture
		 */
		ep->nurbs = 2;
	}

	/* allocate and initialize data urbs */
	for (i = 0; i < ep->nurbs; i++) {
		struct snd_urb_ctx *u = &ep->urb[i];
		u->index = i;
		u->ep = ep;
		u->packets = (i + 1) * total_packs / ep->nurbs
			- i * total_packs / ep->nurbs;
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
		u->urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
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

static int sync_ep_set_params(struct snd_usb_endpoint *ep,
			      struct snd_pcm_hw_params *hw_params,
			      struct audioformat *fmt)
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
		u->urb->transfer_flags = URB_ISO_ASAP |
					 URB_NO_TRANSFER_DMA_MAP;
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

int snd_usb_endpoint_set_params(struct snd_usb_endpoint *ep,
				struct snd_pcm_hw_params *hw_params,
				struct audioformat *fmt,
				struct snd_usb_endpoint *sync_ep)
{
	int err;

	if (ep->use_count != 0) {
		snd_printk(KERN_WARNING "Unable to change format on ep #%x: already in use\n",
			   ep->ep_num);
		return -EBUSY;
	}

	/* release old buffers, if any */
	release_urbs(ep, 0);

	ep->datainterval = fmt->datainterval;
	ep->maxpacksize = fmt->maxpacksize;
	ep->fill_max = fmt->attributes & UAC_EP_CS_ATTR_FILL_MAX;

	if (snd_usb_get_speed(ep->chip->dev) == USB_SPEED_FULL)
		ep->freqn = get_usb_full_speed_rate(params_rate(hw_params));
	else
		ep->freqn = get_usb_high_speed_rate(params_rate(hw_params));

	/* calculate the frequency in 16.16 format */
	ep->freqm = ep->freqn;
	ep->freqshift = INT_MIN;

	ep->phase = 0;

	switch (ep->type) {
	case  SND_USB_ENDPOINT_TYPE_DATA:
		err = data_ep_set_params(ep, hw_params, fmt, sync_ep);
		break;
	case  SND_USB_ENDPOINT_TYPE_SYNC:
		err = sync_ep_set_params(ep, hw_params, fmt);
		break;
	default:
		err = -EINVAL;
	}

	snd_printdd(KERN_DEBUG "Setting params for ep #%x (type %d, %d urbs), ret=%d\n",
		   ep->ep_num, ep->type, ep->nurbs, err);

	return err;
}

int snd_usb_endpoint_start(struct snd_usb_endpoint *ep)
{
	int err;
	unsigned int i;

	if (ep->chip->shutdown)
		return -EBADFD;

	/* already running? */
	if (++ep->use_count != 1)
		return 0;

	if (snd_BUG_ON(!test_bit(EP_FLAG_ACTIVATED, &ep->flags)))
		return -EINVAL;

	/* just to be sure */
	deactivate_urbs(ep, 0, 1);
	wait_clear_urbs(ep);

	ep->active_mask = 0;
	ep->unlink_mask = 0;
	ep->phase = 0;

	/*
	 * If this endpoint has a data endpoint as implicit feedback source,
	 * don't start the urbs here. Instead, mark them all as available,
	 * wait for the record urbs to arrive and queue from that context.
	 */

	set_bit(EP_FLAG_RUNNING, &ep->flags);

	if (snd_usb_endpoint_implict_feedback_sink(ep)) {
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
			prepare_outbound_urb_sizes(ep, urb->context);
			prepare_outbound_urb(ep, urb->context);
		} else {
			prepare_inbound_urb(ep, urb->context);
		}

		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0) {
			snd_printk(KERN_ERR "cannot submit urb %d, error %d: %s\n",
				   i, err, usb_error_string(err));
			goto __error;
		}
		set_bit(i, &ep->active_mask);
	}

	return 0;

__error:
	clear_bit(EP_FLAG_RUNNING, &ep->flags);
	ep->use_count--;
	deactivate_urbs(ep, 0, 0);
	return -EPIPE;
}

void snd_usb_endpoint_stop(struct snd_usb_endpoint *ep,
			   int force, int can_sleep, int wait)
{
	if (!ep)
		return;

	if (snd_BUG_ON(ep->use_count == 0))
		return;

	if (snd_BUG_ON(!test_bit(EP_FLAG_ACTIVATED, &ep->flags)))
		return;

	if (--ep->use_count == 0) {
		deactivate_urbs(ep, force, can_sleep);
		ep->data_subs = NULL;
		ep->sync_slave = NULL;
		ep->retire_data_urb = NULL;
		ep->prepare_data_urb = NULL;

		if (wait)
			wait_clear_urbs(ep);
	}
}

int snd_usb_endpoint_activate(struct snd_usb_endpoint *ep)
{
	if (ep->use_count != 0)
		return 0;

	if (!ep->chip->shutdown &&
	    !test_and_set_bit(EP_FLAG_ACTIVATED, &ep->flags)) {
		int ret;

		ret = usb_set_interface(ep->chip->dev, ep->iface, ep->alt_idx);
		if (ret < 0) {
			snd_printk(KERN_ERR "%s() usb_set_interface() failed, ret = %d\n",
						__func__, ret);
			clear_bit(EP_FLAG_ACTIVATED, &ep->flags);
			return ret;
		}

		return 0;
	}

	return -EBUSY;
}

int snd_usb_endpoint_deactivate(struct snd_usb_endpoint *ep)
{
	if (!ep)
		return -EINVAL;

	if (ep->use_count != 0)
		return 0;

	if (!ep->chip->shutdown &&
	    test_and_clear_bit(EP_FLAG_ACTIVATED, &ep->flags)) {
		int ret;

		ret = usb_set_interface(ep->chip->dev, ep->iface, 0);
		if (ret < 0) {
			snd_printk(KERN_ERR "%s(): usb_set_interface() failed, ret = %d\n",
						__func__, ret);
			return ret;
		}

		return 0;
	}

	return -EBUSY;
}

void snd_usb_endpoint_free(struct list_head *head)
{
	struct snd_usb_endpoint *ep;

	ep = list_entry(head, struct snd_usb_endpoint, list);
	release_urbs(ep, 1);
	kfree(ep);
}

/*
 * process after playback sync complete
 *
 * Full speed devices report feedback values in 10.14 format as samples per
 * frame, high speed devices in 16.16 format as samples per microframe.
 * Because the Audio Class 1 spec was written before USB 2.0, many high speed
 * devices use a wrong interpretation, some others use an entirely different
 * format.  Therefore, we cannot predict what format any particular device uses
 * and must detect it automatically.
 */
void snd_usb_handle_sync_urb(struct snd_usb_endpoint *ep,
			     struct snd_usb_endpoint *sender,
			     const struct urb *urb)
{
	int shift;
	unsigned int f;
	unsigned long flags;

	snd_BUG_ON(ep == sender);

	if (snd_usb_endpoint_implict_feedback_sink(ep) &&
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
		 * will have the same amount of payload than the IN packet we
		 * just received.
		 */

		out_packet->packets = in_ctx->packets;
		for (i = 0; i < in_ctx->packets; i++) {
			if (urb->iso_frame_desc[i].status == 0)
				out_packet->packet_size[i] =
					urb->iso_frame_desc[i].actual_length / ep->stride;
			else
				out_packet->packet_size[i] = 0;
		}

		ep->next_packet_write_pos++;
		ep->next_packet_write_pos %= MAX_URBS;
		spin_unlock_irqrestore(&ep->lock, flags);
		queue_pending_output_urbs(ep);

		return;
	}

	/* parse sync endpoint packet */

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

	if (unlikely(ep->freqshift == INT_MIN)) {
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

