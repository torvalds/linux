/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Main and PCM part
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
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
 *
 *  NOTES:
 *
 *   - async unlink should be used for avoiding the sleep inside lock.
 *     2.4.22 usb-uhci seems buggy for async unlinking and results in
 *     oops.  in such a cse, pass async_unlink=0 option.
 *   - the linked URBs would be preferred but not used so far because of
 *     the instability of unlinking.
 *   - type II is not supported properly.  there is no device which supports
 *     this type *correctly*.  SB extigy looks as if it supports, but it's
 *     indeed an AC3 stream packed in SPDIF frames (i.e. no real AC3 stream).
 */


#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/usb/audio.h>
#include <linux/usb/ch9.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>

#include "usbaudio.h"


MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("USB Audio");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Generic,USB Audio}}");


static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */
/* Vendor/product IDs for this card */
static int vid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 };
static int pid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 };
static int nrpacks = 8;		/* max. number of packets per urb */
static int async_unlink = 1;
static int device_setup[SNDRV_CARDS]; /* device parameter for this card*/
static int ignore_ctl_error;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the USB audio adapter.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the USB audio adapter.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable USB audio adapter.");
module_param_array(vid, int, NULL, 0444);
MODULE_PARM_DESC(vid, "Vendor ID for the USB audio device.");
module_param_array(pid, int, NULL, 0444);
MODULE_PARM_DESC(pid, "Product ID for the USB audio device.");
module_param(nrpacks, int, 0644);
MODULE_PARM_DESC(nrpacks, "Max. number of packets per URB.");
module_param(async_unlink, bool, 0444);
MODULE_PARM_DESC(async_unlink, "Use async unlink mode.");
module_param_array(device_setup, int, NULL, 0444);
MODULE_PARM_DESC(device_setup, "Specific device setup (if needed).");
module_param(ignore_ctl_error, bool, 0444);
MODULE_PARM_DESC(ignore_ctl_error,
		 "Ignore errors from USB controller for mixer interfaces.");

/*
 * debug the h/w constraints
 */
/* #define HW_CONST_DEBUG */


/*
 *
 */

#define MAX_PACKS	20
#define MAX_PACKS_HS	(MAX_PACKS * 8)	/* in high speed mode */
#define MAX_URBS	8
#define SYNC_URBS	4	/* always four urbs for sync */
#define MAX_QUEUE	24	/* try not to exceed this queue length, in ms */

struct audioformat {
	struct list_head list;
	snd_pcm_format_t format;	/* format type */
	unsigned int channels;		/* # channels */
	unsigned int fmt_type;		/* USB audio format type (1-3) */
	unsigned int frame_size;	/* samples per frame for non-audio */
	int iface;			/* interface number */
	unsigned char altsetting;	/* corresponding alternate setting */
	unsigned char altset_idx;	/* array index of altenate setting */
	unsigned char attributes;	/* corresponding attributes of cs endpoint */
	unsigned char endpoint;		/* endpoint */
	unsigned char ep_attr;		/* endpoint attributes */
	unsigned char datainterval;	/* log_2 of data packet interval */
	unsigned int maxpacksize;	/* max. packet size */
	unsigned int rates;		/* rate bitmasks */
	unsigned int rate_min, rate_max;	/* min/max rates */
	unsigned int nr_rates;		/* number of rate table entries */
	unsigned int *rate_table;	/* rate table */
};

struct snd_usb_substream;

struct snd_urb_ctx {
	struct urb *urb;
	unsigned int buffer_size;	/* size of data buffer, if data URB */
	struct snd_usb_substream *subs;
	int index;	/* index for urb array */
	int packets;	/* number of packets per urb */
};

struct snd_urb_ops {
	int (*prepare)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
	int (*retire)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
	int (*prepare_sync)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
	int (*retire_sync)(struct snd_usb_substream *subs, struct snd_pcm_runtime *runtime, struct urb *u);
};

struct snd_usb_substream {
	struct snd_usb_stream *stream;
	struct usb_device *dev;
	struct snd_pcm_substream *pcm_substream;
	int direction;	/* playback or capture */
	int interface;	/* current interface */
	int endpoint;	/* assigned endpoint */
	struct audioformat *cur_audiofmt;	/* current audioformat pointer (for hw_params callback) */
	unsigned int cur_rate;		/* current rate (for hw_params callback) */
	unsigned int period_bytes;	/* current period bytes (for hw_params callback) */
	unsigned int format;     /* USB data format */
	unsigned int datapipe;   /* the data i/o pipe */
	unsigned int syncpipe;   /* 1 - async out or adaptive in */
	unsigned int datainterval;	/* log_2 of data packet interval */
	unsigned int syncinterval;  /* P for adaptive mode, 0 otherwise */
	unsigned int freqn;      /* nominal sampling rate in fs/fps in Q16.16 format */
	unsigned int freqm;      /* momentary sampling rate in fs/fps in Q16.16 format */
	unsigned int freqmax;    /* maximum sampling rate, used for buffer management */
	unsigned int phase;      /* phase accumulator */
	unsigned int maxpacksize;	/* max packet size in bytes */
	unsigned int maxframesize;	/* max packet size in frames */
	unsigned int curpacksize;	/* current packet size in bytes (for capture) */
	unsigned int curframesize;	/* current packet size in frames (for capture) */
	unsigned int fill_max: 1;	/* fill max packet size always */
	unsigned int txfr_quirk:1;	/* allow sub-frame alignment */
	unsigned int fmt_type;		/* USB audio format type (1-3) */

	unsigned int running: 1;	/* running status */

	unsigned int hwptr_done;	/* processed byte position in the buffer */
	unsigned int transfer_done;		/* processed frames since last period update */
	unsigned long active_mask;	/* bitmask of active urbs */
	unsigned long unlink_mask;	/* bitmask of unlinked urbs */

	unsigned int nurbs;			/* # urbs */
	struct snd_urb_ctx dataurb[MAX_URBS];	/* data urb table */
	struct snd_urb_ctx syncurb[SYNC_URBS];	/* sync urb table */
	char *syncbuf;				/* sync buffer for all sync URBs */
	dma_addr_t sync_dma;			/* DMA address of syncbuf */

	u64 formats;			/* format bitmasks (all or'ed) */
	unsigned int num_formats;		/* number of supported audio formats (list) */
	struct list_head fmt_list;	/* format list */
	struct snd_pcm_hw_constraint_list rate_list;	/* limited rates */
	spinlock_t lock;

	struct snd_urb_ops ops;		/* callbacks (must be filled at init) */
};


struct snd_usb_stream {
	struct snd_usb_audio *chip;
	struct snd_pcm *pcm;
	int pcm_index;
	unsigned int fmt_type;		/* USB audio format type (1-3) */
	struct snd_usb_substream substream[2];
	struct list_head list;
};


/*
 * we keep the snd_usb_audio_t instances by ourselves for merging
 * the all interfaces on the same card as one sound device.
 */

static DEFINE_MUTEX(register_mutex);
static struct snd_usb_audio *usb_chip[SNDRV_CARDS];


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

/* convert our full speed USB rate into sampling rate in Hz */
static inline unsigned get_full_speed_hz(unsigned int usb_rate)
{
	return (usb_rate * 125 + (1 << 12)) >> 13;
}

/* convert our high speed USB rate into sampling rate in Hz */
static inline unsigned get_high_speed_hz(unsigned int usb_rate)
{
	return (usb_rate * 125 + (1 << 9)) >> 10;
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
		if (urb->iso_frame_desc[i].status) {
			snd_printd(KERN_ERR "frame %d active: %d\n", i, urb->iso_frame_desc[i].status);
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
 * prepare urb for full speed playback sync pipe
 *
 * set up the offset and length to receive the current frequency.
 */

static int prepare_playback_sync_urb(struct snd_usb_substream *subs,
				     struct snd_pcm_runtime *runtime,
				     struct urb *urb)
{
	struct snd_urb_ctx *ctx = urb->context;

	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	urb->iso_frame_desc[0].length = 3;
	urb->iso_frame_desc[0].offset = 0;
	return 0;
}

/*
 * prepare urb for high speed playback sync pipe
 *
 * set up the offset and length to receive the current frequency.
 */

static int prepare_playback_sync_urb_hs(struct snd_usb_substream *subs,
					struct snd_pcm_runtime *runtime,
					struct urb *urb)
{
	struct snd_urb_ctx *ctx = urb->context;

	urb->dev = ctx->subs->dev; /* we need to set this at each time */
	urb->iso_frame_desc[0].length = 4;
	urb->iso_frame_desc[0].offset = 0;
	return 0;
}

/*
 * process after full speed playback sync complete
 *
 * retrieve the current 10.14 frequency from pipe, and set it.
 * the value is referred in prepare_playback_urb().
 */
static int retire_playback_sync_urb(struct snd_usb_substream *subs,
				    struct snd_pcm_runtime *runtime,
				    struct urb *urb)
{
	unsigned int f;
	unsigned long flags;

	if (urb->iso_frame_desc[0].status == 0 &&
	    urb->iso_frame_desc[0].actual_length == 3) {
		f = combine_triple((u8*)urb->transfer_buffer) << 2;
		if (f >= subs->freqn - subs->freqn / 8 && f <= subs->freqmax) {
			spin_lock_irqsave(&subs->lock, flags);
			subs->freqm = f;
			spin_unlock_irqrestore(&subs->lock, flags);
		}
	}

	return 0;
}

/*
 * process after high speed playback sync complete
 *
 * retrieve the current 12.13 frequency from pipe, and set it.
 * the value is referred in prepare_playback_urb().
 */
static int retire_playback_sync_urb_hs(struct snd_usb_substream *subs,
				       struct snd_pcm_runtime *runtime,
				       struct urb *urb)
{
	unsigned int f;
	unsigned long flags;

	if (urb->iso_frame_desc[0].status == 0 &&
	    urb->iso_frame_desc[0].actual_length == 4) {
		f = combine_quad((u8*)urb->transfer_buffer) & 0x0fffffff;
		if (f >= subs->freqn - subs->freqn / 8 && f <= subs->freqmax) {
			spin_lock_irqsave(&subs->lock, flags);
			subs->freqm = f;
			spin_unlock_irqrestore(&subs->lock, flags);
		}
	}

	return 0;
}

/*
 * process after E-Mu 0202/0404/Tracker Pre high speed playback sync complete
 *
 * These devices return the number of samples per packet instead of the number
 * of samples per microframe.
 */
static int retire_playback_sync_urb_hs_emu(struct snd_usb_substream *subs,
					   struct snd_pcm_runtime *runtime,
					   struct urb *urb)
{
	unsigned int f;
	unsigned long flags;

	if (urb->iso_frame_desc[0].status == 0 &&
	    urb->iso_frame_desc[0].actual_length == 4) {
		f = combine_quad((u8*)urb->transfer_buffer) & 0x0fffffff;
		f >>= subs->datainterval;
		if (f >= subs->freqn - subs->freqn / 8 && f <= subs->freqmax) {
			spin_lock_irqsave(&subs->lock, flags);
			subs->freqm = f;
			spin_unlock_irqrestore(&subs->lock, flags);
		}
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
	       subs->cur_audiofmt->format == SNDRV_PCM_FORMAT_U8 ? 0x80 : 0,
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
	runtime->delay += frames;
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

	spin_lock_irqsave(&subs->lock, flags);
	if (processed > runtime->delay)
		runtime->delay = 0;
	else
		runtime->delay -= processed;
	spin_unlock_irqrestore(&subs->lock, flags);
	return 0;
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

static struct snd_urb_ops audio_urb_ops_high_speed[2] = {
	{
		.prepare =	prepare_nodata_playback_urb,
		.retire =	retire_playback_urb,
		.prepare_sync =	prepare_playback_sync_urb_hs,
		.retire_sync =	retire_playback_sync_urb_hs,
	},
	{
		.prepare =	prepare_capture_urb,
		.retire =	retire_capture_urb,
		.prepare_sync =	prepare_capture_sync_urb_hs,
		.retire_sync =	retire_capture_sync_urb,
	},
};

/*
 * complete callback from data urb
 */
static void snd_complete_urb(struct urb *urb)
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
 * unlink active urbs.
 */
static int deactivate_urbs(struct snd_usb_substream *subs, int force, int can_sleep)
{
	unsigned int i;
	int async;

	subs->running = 0;

	if (!force && subs->stream->chip->shutdown) /* to be sure... */
		return -EBADFD;

	async = !can_sleep && async_unlink;

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
	deactivate_urbs(subs, 0, 0);
	return -EPIPE;
}


/*
 *  wait until all urbs are processed.
 */
static int wait_clear_urbs(struct snd_usb_substream *subs)
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
 * return the current pcm pointer.  just based on the hwptr_done value.
 */
static snd_pcm_uframes_t snd_usb_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_usb_substream *subs;
	unsigned int hwptr_done;
	
	subs = (struct snd_usb_substream *)substream->runtime->private_data;
	spin_lock(&subs->lock);
	hwptr_done = subs->hwptr_done;
	spin_unlock(&subs->lock);
	return hwptr_done / (substream->runtime->frame_bits >> 3);
}


/*
 * start/stop playback substream
 */
static int snd_usb_pcm_playback_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		subs->ops.prepare = prepare_playback_urb;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		return deactivate_urbs(subs, 0, 0);
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		subs->ops.prepare = prepare_nodata_playback_urb;
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * start/stop capture substream
 */
static int snd_usb_pcm_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_usb_substream *subs = substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		subs->ops.retire = retire_capture_urb;
		return start_urbs(subs, substream->runtime);
	case SNDRV_PCM_TRIGGER_STOP:
		return deactivate_urbs(subs, 0, 0);
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		subs->ops.retire = retire_paused_capture_urb;
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		subs->ops.retire = retire_capture_urb;
		return 0;
	default:
		return -EINVAL;
	}
}


/*
 * release a urb data
 */
static void release_urb_ctx(struct snd_urb_ctx *u)
{
	if (u->urb) {
		if (u->buffer_size)
			usb_buffer_free(u->subs->dev, u->buffer_size,
					u->urb->transfer_buffer,
					u->urb->transfer_dma);
		usb_free_urb(u->urb);
		u->urb = NULL;
	}
}

/*
 * release a substream
 */
static void release_substream_urbs(struct snd_usb_substream *subs, int force)
{
	int i;

	/* stop urbs (to be sure) */
	deactivate_urbs(subs, force, 1);
	wait_clear_urbs(subs);

	for (i = 0; i < MAX_URBS; i++)
		release_urb_ctx(&subs->dataurb[i]);
	for (i = 0; i < SYNC_URBS; i++)
		release_urb_ctx(&subs->syncurb[i]);
	usb_buffer_free(subs->dev, SYNC_URBS * 4,
			subs->syncbuf, subs->sync_dma);
	subs->syncbuf = NULL;
	subs->nurbs = 0;
}

/*
 * initialize a substream for plaback/capture
 */
static int init_substream_urbs(struct snd_usb_substream *subs, unsigned int period_bytes,
			       unsigned int rate, unsigned int frame_bits)
{
	unsigned int maxsize, i;
	int is_playback = subs->direction == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int urb_packs, total_packs, packs_per_ms;

	/* calculate the frequency in 16.16 format */
	if (snd_usb_get_speed(subs->dev) == USB_SPEED_FULL)
		subs->freqn = get_usb_full_speed_rate(rate);
	else
		subs->freqn = get_usb_high_speed_rate(rate);
	subs->freqm = subs->freqn;
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

	if (snd_usb_get_speed(subs->dev) == USB_SPEED_HIGH)
		packs_per_ms = 8 >> subs->datainterval;
	else
		packs_per_ms = 1;

	if (is_playback) {
		urb_packs = max(nrpacks, 1);
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
			usb_buffer_alloc(subs->dev, u->buffer_size, GFP_KERNEL,
					 &u->urb->transfer_dma);
		if (!u->urb->transfer_buffer)
			goto out_of_memory;
		u->urb->pipe = subs->datapipe;
		u->urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		u->urb->interval = 1 << subs->datainterval;
		u->urb->context = u;
		u->urb->complete = snd_complete_urb;
	}

	if (subs->syncpipe) {
		/* allocate and initialize sync urbs */
		subs->syncbuf = usb_buffer_alloc(subs->dev, SYNC_URBS * 4,
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
	release_substream_urbs(subs, 0);
	return -ENOMEM;
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
		if (fp->format != format || fp->channels != channels)
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


/*
 * initialize the picth control and sample rate
 */
static int init_usb_pitch(struct usb_device *dev, int iface,
			  struct usb_host_interface *alts,
			  struct audioformat *fmt)
{
	unsigned int ep;
	unsigned char data[1];
	int err;

	ep = get_endpoint(alts, 0)->bEndpointAddress;
	/* if endpoint has pitch control, enable it */
	if (fmt->attributes & UAC_EP_CS_ATTR_PITCH_CONTROL) {
		data[0] = 1;
		if ((err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
					   USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT,
					   UAC_EP_CS_ATTR_PITCH_CONTROL << 8, ep, data, 1, 1000)) < 0) {
			snd_printk(KERN_ERR "%d:%d:%d: cannot set enable PITCH\n",
				   dev->devnum, iface, ep);
			return err;
		}
	}
	return 0;
}

static int init_usb_sample_rate(struct usb_device *dev, int iface,
				struct usb_host_interface *alts,
				struct audioformat *fmt, int rate)
{
	unsigned int ep;
	unsigned char data[3];
	int err;

	ep = get_endpoint(alts, 0)->bEndpointAddress;
	/* if endpoint has sampling rate control, set it */
	if (fmt->attributes & UAC_EP_CS_ATTR_SAMPLE_RATE) {
		int crate;
		data[0] = rate;
		data[1] = rate >> 8;
		data[2] = rate >> 16;
		if ((err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
					   USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT,
					   UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep, data, 3, 1000)) < 0) {
			snd_printk(KERN_ERR "%d:%d:%d: cannot set freq %d to ep %#x\n",
				   dev->devnum, iface, fmt->altsetting, rate, ep);
			return err;
		}
		if ((err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC_GET_CUR,
					   USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_IN,
					   UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep, data, 3, 1000)) < 0) {
			snd_printk(KERN_WARNING "%d:%d:%d: cannot get freq at ep %#x\n",
				   dev->devnum, iface, fmt->altsetting, ep);
			return 0; /* some devices don't support reading */
		}
		crate = data[0] | (data[1] << 8) | (data[2] << 16);
		if (crate != rate) {
			snd_printd(KERN_WARNING "current rate %d is different from the runtime rate %d\n", crate, rate);
			// runtime->rate = crate;
		}
	}
	return 0;
}

/*
 * For E-Mu 0404USB/0202USB/TrackerPre sample rate should be set for device,
 * not for interface.
 */
static void set_format_emu_quirk(struct snd_usb_substream *subs,
				 struct audioformat *fmt)
{
	unsigned char emu_samplerate_id = 0;

	/* When capture is active
	 * sample rate shouldn't be changed
	 * by playback substream
	 */
	if (subs->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		if (subs->stream->substream[SNDRV_PCM_STREAM_CAPTURE].interface != -1)
			return;
	}

	switch (fmt->rate_min) {
	case 48000:
		emu_samplerate_id = EMU_QUIRK_SR_48000HZ;
		break;
	case 88200:
		emu_samplerate_id = EMU_QUIRK_SR_88200HZ;
		break;
	case 96000:
		emu_samplerate_id = EMU_QUIRK_SR_96000HZ;
		break;
	case 176400:
		emu_samplerate_id = EMU_QUIRK_SR_176400HZ;
		break;
	case 192000:
		emu_samplerate_id = EMU_QUIRK_SR_192000HZ;
		break;
	default:
		emu_samplerate_id = EMU_QUIRK_SR_44100HZ;
		break;
	}
	snd_emuusb_set_samplerate(subs->stream->chip, emu_samplerate_id);
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
		subs->format = 0;
	}

	/* set interface */
	if (subs->interface != fmt->iface || subs->format != fmt->altset_idx) {
		if (usb_set_interface(dev, fmt->iface, fmt->altsetting) < 0) {
			snd_printk(KERN_ERR "%d:%d:%d: usb_set_interface failed\n",
				   dev->devnum, fmt->iface, fmt->altsetting);
			return -EIO;
		}
		snd_printdd(KERN_INFO "setting usb interface %d:%d\n", fmt->iface, fmt->altsetting);
		subs->interface = fmt->iface;
		subs->format = fmt->altset_idx;
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
	}

	/* always fill max packet size */
	if (fmt->attributes & UAC_EP_CS_ATTR_FILL_MAX)
		subs->fill_max = 1;

	if ((err = init_usb_pitch(dev, subs->interface, alts, fmt)) < 0)
		return err;

	subs->cur_audiofmt = fmt;

	switch (subs->stream->chip->usb_id) {
	case USB_ID(0x041e, 0x3f02): /* E-Mu 0202 USB */
	case USB_ID(0x041e, 0x3f04): /* E-Mu 0404 USB */
	case USB_ID(0x041e, 0x3f0a): /* E-Mu Tracker Pre */
		set_format_emu_quirk(subs, fmt);
		break;
	}

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
		ret = init_usb_sample_rate(subs->dev, subs->interface, alts, fmt, rate);
		if (ret < 0)
			return ret;
		subs->cur_rate = rate;
	}

	if (changed) {
		/* format changed */
		release_substream_urbs(subs, 0);
		/* influenced: period_bytes, channels, rate, format, */
		ret = init_substream_urbs(subs, params_period_bytes(hw_params),
					  params_rate(hw_params),
					  snd_pcm_format_physical_width(params_format(hw_params)) * params_channels(hw_params));
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
	if (!subs->stream->chip->shutdown)
		release_substream_urbs(subs, 0);
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
	runtime->delay = 0;

	/* clear urbs (to be sure) */
	deactivate_urbs(subs, 0, 1);
	wait_clear_urbs(subs);

	/* for playback, submit the URBs now; otherwise, the first hwptr_done
	 * updates for all URBs would happen at the same time when starting */
	if (subs->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		subs->ops.prepare = prepare_nodata_playback_urb;
		return start_urbs(subs, runtime);
	} else
		return 0;
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

/*
 * h/w constraints
 */

#ifdef HW_CONST_DEBUG
#define hwc_debug(fmt, args...) printk(KERN_DEBUG fmt, ##args)
#else
#define hwc_debug(fmt, args...) /**/
#endif

static int hw_check_valid_format(struct snd_usb_substream *subs,
				 struct snd_pcm_hw_params *params,
				 struct audioformat *fp)
{
	struct snd_interval *it = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *ct = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmts = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_interval *pt = hw_param_interval(params, SNDRV_PCM_HW_PARAM_PERIOD_TIME);
	unsigned int ptime;

	/* check the format */
	if (!snd_mask_test(fmts, fp->format)) {
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
	if (snd_usb_get_speed(subs->dev) == USB_SPEED_HIGH) {
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
		fbits |= (1ULL << fp->format);
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
		hwc_debug("  --> get emtpy\n");
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

	subs->rate_list.count = count;
	subs->rate_list.list = kmalloc(sizeof(int) * count, GFP_KERNEL);
	subs->rate_list.mask = 0;
	count = 0;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		int i;
		for (i = 0; i < fp->nr_rates; i++)
			subs->rate_list.list[count++] = fp->rate_table[i];
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

	param_period_time_if_needed = SNDRV_PCM_HW_PARAM_PERIOD_TIME;
	if (snd_usb_get_speed(subs->dev) != USB_SPEED_HIGH)
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
		return err;
	if ((err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				       hw_rule_channels, subs,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       SNDRV_PCM_HW_PARAM_RATE,
				       param_period_time_if_needed,
				       -1)) < 0)
		return err;
	if ((err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
				       hw_rule_format, subs,
				       SNDRV_PCM_HW_PARAM_RATE,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       param_period_time_if_needed,
				       -1)) < 0)
		return err;
	if (param_period_time_if_needed >= 0) {
		err = snd_pcm_hw_rule_add(runtime, 0,
					  SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					  hw_rule_period_time, subs,
					  SNDRV_PCM_HW_PARAM_FORMAT,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  SNDRV_PCM_HW_PARAM_RATE,
					  -1);
		if (err < 0)
			return err;
	}
	if ((err = snd_usb_pcm_check_knot(runtime, subs)) < 0)
		return err;
	return 0;
}

static int snd_usb_pcm_open(struct snd_pcm_substream *substream, int direction)
{
	struct snd_usb_stream *as = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usb_substream *subs = &as->substream[direction];

	subs->interface = -1;
	subs->format = 0;
	runtime->hw = snd_usb_hardware;
	runtime->private_data = subs;
	subs->pcm_substream = substream;
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
	.trigger =	snd_usb_pcm_playback_trigger,
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
	.trigger =	snd_usb_pcm_capture_trigger,
	.pointer =	snd_usb_pcm_pointer,
	.page =		snd_pcm_lib_get_vmalloc_page,
	.mmap =		snd_pcm_lib_mmap_vmalloc,
};



/*
 * helper functions
 */

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
		    __u16 size, int timeout)
{
	int err;
	void *buf = NULL;

	if (size > 0) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}
	err = usb_control_msg(dev, pipe, request, requesttype,
			      value, index, buf, size, timeout);
	if (size > 0) {
		memcpy(data, buf, size);
		kfree(buf);
	}
	return err;
}


/*
 * entry point for linux usb interface
 */

static int usb_audio_probe(struct usb_interface *intf,
			   const struct usb_device_id *id);
static void usb_audio_disconnect(struct usb_interface *intf);

#ifdef CONFIG_PM
static int usb_audio_suspend(struct usb_interface *intf, pm_message_t message);
static int usb_audio_resume(struct usb_interface *intf);
#else
#define usb_audio_suspend NULL
#define usb_audio_resume NULL
#endif

static struct usb_device_id usb_audio_ids [] = {
#include "usbquirks.h"
    { .match_flags = (USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS),
      .bInterfaceClass = USB_CLASS_AUDIO,
      .bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL },
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_audio_ids);

static struct usb_driver usb_audio_driver = {
	.name =		"snd-usb-audio",
	.probe =	usb_audio_probe,
	.disconnect =	usb_audio_disconnect,
	.suspend =	usb_audio_suspend,
	.resume =	usb_audio_resume,
	.id_table =	usb_audio_ids,
};


#if defined(CONFIG_PROC_FS) && defined(CONFIG_SND_VERBOSE_PROCFS)

/*
 * proc interface for list the supported pcm formats
 */
static void proc_dump_substream_formats(struct snd_usb_substream *subs, struct snd_info_buffer *buffer)
{
	struct list_head *p;
	static char *sync_types[4] = {
		"NONE", "ASYNC", "ADAPTIVE", "SYNC"
	};

	list_for_each(p, &subs->fmt_list) {
		struct audioformat *fp;
		fp = list_entry(p, struct audioformat, list);
		snd_iprintf(buffer, "  Interface %d\n", fp->iface);
		snd_iprintf(buffer, "    Altset %d\n", fp->altsetting);
		snd_iprintf(buffer, "    Format: %s\n",
			    snd_pcm_format_name(fp->format));
		snd_iprintf(buffer, "    Channels: %d\n", fp->channels);
		snd_iprintf(buffer, "    Endpoint: %d %s (%s)\n",
			    fp->endpoint & USB_ENDPOINT_NUMBER_MASK,
			    fp->endpoint & USB_DIR_IN ? "IN" : "OUT",
			    sync_types[(fp->ep_attr & USB_ENDPOINT_SYNCTYPE) >> 2]);
		if (fp->rates & SNDRV_PCM_RATE_CONTINUOUS) {
			snd_iprintf(buffer, "    Rates: %d - %d (continuous)\n",
				    fp->rate_min, fp->rate_max);
		} else {
			unsigned int i;
			snd_iprintf(buffer, "    Rates: ");
			for (i = 0; i < fp->nr_rates; i++) {
				if (i > 0)
					snd_iprintf(buffer, ", ");
				snd_iprintf(buffer, "%d", fp->rate_table[i]);
			}
			snd_iprintf(buffer, "\n");
		}
		if (snd_usb_get_speed(subs->dev) == USB_SPEED_HIGH)
			snd_iprintf(buffer, "    Data packet interval: %d us\n",
				    125 * (1 << fp->datainterval));
		// snd_iprintf(buffer, "    Max Packet Size = %d\n", fp->maxpacksize);
		// snd_iprintf(buffer, "    EP Attribute = %#x\n", fp->attributes);
	}
}

static void proc_dump_substream_status(struct snd_usb_substream *subs, struct snd_info_buffer *buffer)
{
	if (subs->running) {
		unsigned int i;
		snd_iprintf(buffer, "  Status: Running\n");
		snd_iprintf(buffer, "    Interface = %d\n", subs->interface);
		snd_iprintf(buffer, "    Altset = %d\n", subs->format);
		snd_iprintf(buffer, "    URBs = %d [ ", subs->nurbs);
		for (i = 0; i < subs->nurbs; i++)
			snd_iprintf(buffer, "%d ", subs->dataurb[i].packets);
		snd_iprintf(buffer, "]\n");
		snd_iprintf(buffer, "    Packet Size = %d\n", subs->curpacksize);
		snd_iprintf(buffer, "    Momentary freq = %u Hz (%#x.%04x)\n",
			    snd_usb_get_speed(subs->dev) == USB_SPEED_FULL
			    ? get_full_speed_hz(subs->freqm)
			    : get_high_speed_hz(subs->freqm),
			    subs->freqm >> 16, subs->freqm & 0xffff);
	} else {
		snd_iprintf(buffer, "  Status: Stop\n");
	}
}

static void proc_pcm_format_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_usb_stream *stream = entry->private_data;

	snd_iprintf(buffer, "%s : %s\n", stream->chip->card->longname, stream->pcm->name);

	if (stream->substream[SNDRV_PCM_STREAM_PLAYBACK].num_formats) {
		snd_iprintf(buffer, "\nPlayback:\n");
		proc_dump_substream_status(&stream->substream[SNDRV_PCM_STREAM_PLAYBACK], buffer);
		proc_dump_substream_formats(&stream->substream[SNDRV_PCM_STREAM_PLAYBACK], buffer);
	}
	if (stream->substream[SNDRV_PCM_STREAM_CAPTURE].num_formats) {
		snd_iprintf(buffer, "\nCapture:\n");
		proc_dump_substream_status(&stream->substream[SNDRV_PCM_STREAM_CAPTURE], buffer);
		proc_dump_substream_formats(&stream->substream[SNDRV_PCM_STREAM_CAPTURE], buffer);
	}
}

static void proc_pcm_format_add(struct snd_usb_stream *stream)
{
	struct snd_info_entry *entry;
	char name[32];
	struct snd_card *card = stream->chip->card;

	sprintf(name, "stream%d", stream->pcm_index);
	if (!snd_card_proc_new(card, name, &entry))
		snd_info_set_text_ops(entry, stream, proc_pcm_format_read);
}

#else

static inline void proc_pcm_format_add(struct snd_usb_stream *stream)
{
}

#endif

/*
 * initialize the substream instance.
 */

static void init_substream(struct snd_usb_stream *as, int stream, struct audioformat *fp)
{
	struct snd_usb_substream *subs = &as->substream[stream];

	INIT_LIST_HEAD(&subs->fmt_list);
	spin_lock_init(&subs->lock);

	subs->stream = as;
	subs->direction = stream;
	subs->dev = as->chip->dev;
	subs->txfr_quirk = as->chip->txfr_quirk;
	if (snd_usb_get_speed(subs->dev) == USB_SPEED_FULL) {
		subs->ops = audio_urb_ops[stream];
	} else {
		subs->ops = audio_urb_ops_high_speed[stream];
		switch (as->chip->usb_id) {
		case USB_ID(0x041e, 0x3f02): /* E-Mu 0202 USB */
		case USB_ID(0x041e, 0x3f04): /* E-Mu 0404 USB */
		case USB_ID(0x041e, 0x3f0a): /* E-Mu Tracker Pre */
			subs->ops.retire_sync = retire_playback_sync_urb_hs_emu;
			break;
		}
	}
	snd_pcm_set_ops(as->pcm, stream,
			stream == SNDRV_PCM_STREAM_PLAYBACK ?
			&snd_usb_playback_ops : &snd_usb_capture_ops);

	list_add_tail(&fp->list, &subs->fmt_list);
	subs->formats |= 1ULL << fp->format;
	subs->endpoint = fp->endpoint;
	subs->num_formats++;
	subs->fmt_type = fp->fmt_type;
}


/*
 * free a substream
 */
static void free_substream(struct snd_usb_substream *subs)
{
	struct list_head *p, *n;

	if (!subs->num_formats)
		return; /* not initialized */
	list_for_each_safe(p, n, &subs->fmt_list) {
		struct audioformat *fp = list_entry(p, struct audioformat, list);
		kfree(fp->rate_table);
		kfree(fp);
	}
	kfree(subs->rate_list.list);
}


/*
 * free a usb stream instance
 */
static void snd_usb_audio_stream_free(struct snd_usb_stream *stream)
{
	free_substream(&stream->substream[0]);
	free_substream(&stream->substream[1]);
	list_del(&stream->list);
	kfree(stream);
}

static void snd_usb_audio_pcm_free(struct snd_pcm *pcm)
{
	struct snd_usb_stream *stream = pcm->private_data;
	if (stream) {
		stream->pcm = NULL;
		snd_usb_audio_stream_free(stream);
	}
}


/*
 * add this endpoint to the chip instance.
 * if a stream with the same endpoint already exists, append to it.
 * if not, create a new pcm stream.
 */
static int add_audio_endpoint(struct snd_usb_audio *chip, int stream, struct audioformat *fp)
{
	struct list_head *p;
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs;
	struct snd_pcm *pcm;
	int err;

	list_for_each(p, &chip->pcm_list) {
		as = list_entry(p, struct snd_usb_stream, list);
		if (as->fmt_type != fp->fmt_type)
			continue;
		subs = &as->substream[stream];
		if (!subs->endpoint)
			continue;
		if (subs->endpoint == fp->endpoint) {
			list_add_tail(&fp->list, &subs->fmt_list);
			subs->num_formats++;
			subs->formats |= 1ULL << fp->format;
			return 0;
		}
	}
	/* look for an empty stream */
	list_for_each(p, &chip->pcm_list) {
		as = list_entry(p, struct snd_usb_stream, list);
		if (as->fmt_type != fp->fmt_type)
			continue;
		subs = &as->substream[stream];
		if (subs->endpoint)
			continue;
		err = snd_pcm_new_stream(as->pcm, stream, 1);
		if (err < 0)
			return err;
		init_substream(as, stream, fp);
		return 0;
	}

	/* create a new pcm */
	as = kzalloc(sizeof(*as), GFP_KERNEL);
	if (!as)
		return -ENOMEM;
	as->pcm_index = chip->pcm_devs;
	as->chip = chip;
	as->fmt_type = fp->fmt_type;
	err = snd_pcm_new(chip->card, "USB Audio", chip->pcm_devs,
			  stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0,
			  stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1,
			  &pcm);
	if (err < 0) {
		kfree(as);
		return err;
	}
	as->pcm = pcm;
	pcm->private_data = as;
	pcm->private_free = snd_usb_audio_pcm_free;
	pcm->info_flags = 0;
	if (chip->pcm_devs > 0)
		sprintf(pcm->name, "USB Audio #%d", chip->pcm_devs);
	else
		strcpy(pcm->name, "USB Audio");

	init_substream(as, stream, fp);

	list_add(&as->list, &chip->pcm_list);
	chip->pcm_devs++;

	proc_pcm_format_add(as);

	return 0;
}


/*
 * check if the device uses big-endian samples
 */
static int is_big_endian_format(struct snd_usb_audio *chip, struct audioformat *fp)
{
	switch (chip->usb_id) {
	case USB_ID(0x0763, 0x2001): /* M-Audio Quattro: captured data only */
		if (fp->endpoint & USB_DIR_IN)
			return 1;
		break;
	case USB_ID(0x0763, 0x2003): /* M-Audio Audiophile USB */
		if (device_setup[chip->index] == 0x00 ||
		    fp->altsetting==1 || fp->altsetting==2 || fp->altsetting==3)
			return 1;
	}
	return 0;
}

/*
 * parse the audio format type I descriptor
 * and returns the corresponding pcm format
 *
 * @dev: usb device
 * @fp: audioformat record
 * @format: the format tag (wFormatTag)
 * @fmt: the format type descriptor
 */
static int parse_audio_format_i_type(struct snd_usb_audio *chip,
				     struct audioformat *fp,
				     int format, void *_fmt,
				     int protocol)
{
	int pcm_format, i;
	int sample_width, sample_bytes;

	switch (protocol) {
	case UAC_VERSION_1: {
		struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubframeSize;
		break;
	}

	case UAC_VERSION_2: {
		struct uac_format_type_i_ext_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubslotSize;

		/*
		 * FIXME
		 * USB audio class v2 devices specify a bitmap of possible
		 * audio formats rather than one fix value. For now, we just
		 * pick one of them and report that as the only possible
		 * value for this setting.
		 * The bit allocation map is in fact compatible to the
		 * wFormatTag of the v1 AS streaming descriptors, which is why
		 * we can simply map the matrix.
		 */

		for (i = 0; i < 5; i++)
			if (format & (1UL << i)) {
				format = i + 1;
				break;
			}

		break;
	}

	default:
		return -EINVAL;
	}

	/* FIXME: correct endianess and sign? */
	pcm_format = -1;

	switch (format) {
	case UAC_FORMAT_TYPE_I_UNDEFINED: /* some devices don't define this correctly... */
		snd_printdd(KERN_INFO "%d:%u:%d : format type 0 is detected, processed as PCM\n",
			    chip->dev->devnum, fp->iface, fp->altsetting);
		/* fall-through */
	case UAC_FORMAT_TYPE_I_PCM:
		if (sample_width > sample_bytes * 8) {
			snd_printk(KERN_INFO "%d:%u:%d : sample bitwidth %d in over sample bytes %d\n",
				   chip->dev->devnum, fp->iface, fp->altsetting,
				   sample_width, sample_bytes);
		}
		/* check the format byte size */
		switch (sample_bytes) {
		case 1:
			pcm_format = SNDRV_PCM_FORMAT_S8;
			break;
		case 2:
			if (is_big_endian_format(chip, fp))
				pcm_format = SNDRV_PCM_FORMAT_S16_BE; /* grrr, big endian!! */
			else
				pcm_format = SNDRV_PCM_FORMAT_S16_LE;
			break;
		case 3:
			if (is_big_endian_format(chip, fp))
				pcm_format = SNDRV_PCM_FORMAT_S24_3BE; /* grrr, big endian!! */
			else
				pcm_format = SNDRV_PCM_FORMAT_S24_3LE;
			break;
		case 4:
			pcm_format = SNDRV_PCM_FORMAT_S32_LE;
			break;
		default:
			snd_printk(KERN_INFO "%d:%u:%d : unsupported sample bitwidth %d in %d bytes\n",
				   chip->dev->devnum, fp->iface, fp->altsetting,
				   sample_width, sample_bytes);
			break;
		}
		break;
	case UAC_FORMAT_TYPE_I_PCM8:
		pcm_format = SNDRV_PCM_FORMAT_U8;

		/* Dallas DS4201 workaround: it advertises U8 format, but really
		   supports S8. */
		if (chip->usb_id == USB_ID(0x04fa, 0x4201))
			pcm_format = SNDRV_PCM_FORMAT_S8;
		break;
	case UAC_FORMAT_TYPE_I_IEEE_FLOAT:
		pcm_format = SNDRV_PCM_FORMAT_FLOAT_LE;
		break;
	case UAC_FORMAT_TYPE_I_ALAW:
		pcm_format = SNDRV_PCM_FORMAT_A_LAW;
		break;
	case UAC_FORMAT_TYPE_I_MULAW:
		pcm_format = SNDRV_PCM_FORMAT_MU_LAW;
		break;
	default:
		snd_printk(KERN_INFO "%d:%u:%d : unsupported format type %d\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, format);
		break;
	}
	return pcm_format;
}


/*
 * parse the format descriptor and stores the possible sample rates
 * on the audioformat table (audio class v1).
 *
 * @dev: usb device
 * @fp: audioformat record
 * @fmt: the format descriptor
 * @offset: the start offset of descriptor pointing the rate type
 *          (7 for type I and II, 8 for type II)
 */
static int parse_audio_format_rates_v1(struct snd_usb_audio *chip, struct audioformat *fp,
				       unsigned char *fmt, int offset)
{
	int nr_rates = fmt[offset];

	if (fmt[0] < offset + 1 + 3 * (nr_rates ? nr_rates : 2)) {
		snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_FORMAT_TYPE desc\n",
				   chip->dev->devnum, fp->iface, fp->altsetting);
		return -1;
	}

	if (nr_rates) {
		/*
		 * build the rate table and bitmap flags
		 */
		int r, idx;

		fp->rate_table = kmalloc(sizeof(int) * nr_rates, GFP_KERNEL);
		if (fp->rate_table == NULL) {
			snd_printk(KERN_ERR "cannot malloc\n");
			return -1;
		}

		fp->nr_rates = 0;
		fp->rate_min = fp->rate_max = 0;
		for (r = 0, idx = offset + 1; r < nr_rates; r++, idx += 3) {
			unsigned int rate = combine_triple(&fmt[idx]);
			if (!rate)
				continue;
			/* C-Media CM6501 mislabels its 96 kHz altsetting */
			if (rate == 48000 && nr_rates == 1 &&
			    (chip->usb_id == USB_ID(0x0d8c, 0x0201) ||
			     chip->usb_id == USB_ID(0x0d8c, 0x0102)) &&
			    fp->altsetting == 5 && fp->maxpacksize == 392)
				rate = 96000;
			/* Creative VF0470 Live Cam reports 16 kHz instead of 8kHz */
			if (rate == 16000 && chip->usb_id == USB_ID(0x041e, 0x4068))
				rate = 8000;
			fp->rate_table[fp->nr_rates] = rate;
			if (!fp->rate_min || rate < fp->rate_min)
				fp->rate_min = rate;
			if (!fp->rate_max || rate > fp->rate_max)
				fp->rate_max = rate;
			fp->rates |= snd_pcm_rate_to_rate_bit(rate);
			fp->nr_rates++;
		}
		if (!fp->nr_rates) {
			hwc_debug("All rates were zero. Skipping format!\n");
			return -1;
		}
	} else {
		/* continuous rates */
		fp->rates = SNDRV_PCM_RATE_CONTINUOUS;
		fp->rate_min = combine_triple(&fmt[offset + 1]);
		fp->rate_max = combine_triple(&fmt[offset + 4]);
	}
	return 0;
}

/*
 * parse the format descriptor and stores the possible sample rates
 * on the audioformat table (audio class v2).
 */
static int parse_audio_format_rates_v2(struct snd_usb_audio *chip,
				       struct audioformat *fp,
				       struct usb_host_interface *iface)
{
	struct usb_device *dev = chip->dev;
	unsigned char tmp[2], *data;
	int i, nr_rates, data_size, ret = 0;

	/* get the number of sample rates first by only fetching 2 bytes */
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_RANGE,
			       USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			       0x0100, chip->clock_id << 8, tmp, sizeof(tmp), 1000);

	if (ret < 0) {
		snd_printk(KERN_ERR "unable to retrieve number of sample rates\n");
		goto err;
	}

	nr_rates = (tmp[1] << 8) | tmp[0];
	data_size = 2 + 12 * nr_rates;
	data = kzalloc(data_size, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	/* now get the full information */
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_RANGE,
			       USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			       0x0100, chip->clock_id << 8, data, data_size, 1000);

	if (ret < 0) {
		snd_printk(KERN_ERR "unable to retrieve sample rate range\n");
		ret = -EINVAL;
		goto err_free;
	}

	fp->rate_table = kmalloc(sizeof(int) * nr_rates, GFP_KERNEL);
	if (!fp->rate_table) {
		ret = -ENOMEM;
		goto err_free;
	}

	fp->nr_rates = 0;
	fp->rate_min = fp->rate_max = 0;

	for (i = 0; i < nr_rates; i++) {
		int rate = combine_quad(&data[2 + 12 * i]);

		fp->rate_table[fp->nr_rates] = rate;
		if (!fp->rate_min || rate < fp->rate_min)
			fp->rate_min = rate;
		if (!fp->rate_max || rate > fp->rate_max)
			fp->rate_max = rate;
		fp->rates |= snd_pcm_rate_to_rate_bit(rate);
		fp->nr_rates++;
	}

err_free:
	kfree(data);
err:
	return ret;
}

/*
 * parse the format type I and III descriptors
 */
static int parse_audio_format_i(struct snd_usb_audio *chip,
				struct audioformat *fp,
				int format, void *_fmt,
				struct usb_host_interface *iface)
{
	struct usb_interface_descriptor *altsd = get_iface_desc(iface);
	struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
	int protocol = altsd->bInterfaceProtocol;
	int pcm_format, ret;

	if (fmt->bFormatType == UAC_FORMAT_TYPE_III) {
		/* FIXME: the format type is really IECxxx
		 *        but we give normal PCM format to get the existing
		 *        apps working...
		 */
		switch (chip->usb_id) {

		case USB_ID(0x0763, 0x2003): /* M-Audio Audiophile USB */
			if (device_setup[chip->index] == 0x00 && 
			    fp->altsetting == 6)
				pcm_format = SNDRV_PCM_FORMAT_S16_BE;
			else
				pcm_format = SNDRV_PCM_FORMAT_S16_LE;
			break;
		default:
			pcm_format = SNDRV_PCM_FORMAT_S16_LE;
		}
	} else {
		pcm_format = parse_audio_format_i_type(chip, fp, format, fmt, protocol);
		if (pcm_format < 0)
			return -1;
	}

	fp->format = pcm_format;

	/* gather possible sample rates */
	/* audio class v1 reports possible sample rates as part of the
	 * proprietary class specific descriptor.
	 * audio class v2 uses class specific EP0 range requests for that.
	 */
	switch (protocol) {
	case UAC_VERSION_1:
		fp->channels = fmt->bNrChannels;
		ret = parse_audio_format_rates_v1(chip, fp, _fmt, 7);
		break;
	case UAC_VERSION_2:
		/* fp->channels is already set in this case */
		ret = parse_audio_format_rates_v2(chip, fp, iface);
		break;
	}

	if (fp->channels < 1) {
		snd_printk(KERN_ERR "%d:%u:%d : invalid channels %d\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, fp->channels);
		return -1;
	}

	return ret;
}

/*
 * parse the format type II descriptor
 */
static int parse_audio_format_ii(struct snd_usb_audio *chip,
				 struct audioformat *fp,
				 int format, void *_fmt,
				 struct usb_host_interface *iface)
{
	int brate, framesize, ret;
	struct usb_interface_descriptor *altsd = get_iface_desc(iface);
	int protocol = altsd->bInterfaceProtocol;

	switch (format) {
	case UAC_FORMAT_TYPE_II_AC3:
		/* FIXME: there is no AC3 format defined yet */
		// fp->format = SNDRV_PCM_FORMAT_AC3;
		fp->format = SNDRV_PCM_FORMAT_U8; /* temporarily hack to receive byte streams */
		break;
	case UAC_FORMAT_TYPE_II_MPEG:
		fp->format = SNDRV_PCM_FORMAT_MPEG;
		break;
	default:
		snd_printd(KERN_INFO "%d:%u:%d : unknown format tag %#x is detected.  processed as MPEG.\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, format);
		fp->format = SNDRV_PCM_FORMAT_MPEG;
		break;
	}

	fp->channels = 1;

	switch (protocol) {
	case UAC_VERSION_1: {
		struct uac_format_type_ii_discrete_descriptor *fmt = _fmt;
		brate = le16_to_cpu(fmt->wMaxBitRate);
		framesize = le16_to_cpu(fmt->wSamplesPerFrame);
		snd_printd(KERN_INFO "found format II with max.bitrate = %d, frame size=%d\n", brate, framesize);
		fp->frame_size = framesize;
		ret = parse_audio_format_rates_v1(chip, fp, _fmt, 8); /* fmt[8..] sample rates */
		break;
	}
	case UAC_VERSION_2: {
		struct uac_format_type_ii_ext_descriptor *fmt = _fmt;
		brate = le16_to_cpu(fmt->wMaxBitRate);
		framesize = le16_to_cpu(fmt->wSamplesPerFrame);
		snd_printd(KERN_INFO "found format II with max.bitrate = %d, frame size=%d\n", brate, framesize);
		fp->frame_size = framesize;
		ret = parse_audio_format_rates_v2(chip, fp, iface);
		break;
	}
	}

	return ret;
}

static int parse_audio_format(struct snd_usb_audio *chip, struct audioformat *fp,
			      int format, unsigned char *fmt, int stream,
			      struct usb_host_interface *iface)
{
	int err;

	switch (fmt[3]) {
	case UAC_FORMAT_TYPE_I:
	case UAC_FORMAT_TYPE_III:
		err = parse_audio_format_i(chip, fp, format, fmt, iface);
		break;
	case UAC_FORMAT_TYPE_II:
		err = parse_audio_format_ii(chip, fp, format, fmt, iface);
		break;
	default:
		snd_printd(KERN_INFO "%d:%u:%d : format type %d is not supported yet\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, fmt[3]);
		return -1;
	}
	fp->fmt_type = fmt[3];
	if (err < 0)
		return err;
#if 1
	/* FIXME: temporary hack for extigy/audigy 2 nx/zs */
	/* extigy apparently supports sample rates other than 48k
	 * but not in ordinary way.  so we enable only 48k atm.
	 */
	if (chip->usb_id == USB_ID(0x041e, 0x3000) ||
	    chip->usb_id == USB_ID(0x041e, 0x3020) ||
	    chip->usb_id == USB_ID(0x041e, 0x3061)) {
		if (fmt[3] == UAC_FORMAT_TYPE_I &&
		    fp->rates != SNDRV_PCM_RATE_48000 &&
		    fp->rates != SNDRV_PCM_RATE_96000)
			return -1;
	}
#endif
	return 0;
}

static unsigned char parse_datainterval(struct snd_usb_audio *chip,
					struct usb_host_interface *alts)
{
	if (snd_usb_get_speed(chip->dev) == USB_SPEED_HIGH &&
	    get_endpoint(alts, 0)->bInterval >= 1 &&
	    get_endpoint(alts, 0)->bInterval <= 4)
		return get_endpoint(alts, 0)->bInterval - 1;
	else
		return 0;
}

static int audiophile_skip_setting_quirk(struct snd_usb_audio *chip,
					 int iface, int altno);
static int parse_audio_endpoints(struct snd_usb_audio *chip, int iface_no)
{
	struct usb_device *dev;
	struct usb_interface *iface;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	int i, altno, err, stream;
	int format = 0, num_channels = 0;
	struct audioformat *fp = NULL;
	unsigned char *fmt, *csep;
	int num, protocol;

	dev = chip->dev;

	/* parse the interface's altsettings */
	iface = usb_ifnum_to_if(dev, iface_no);

	num = iface->num_altsetting;

	/*
	 * Dallas DS4201 workaround: It presents 5 altsettings, but the last
	 * one misses syncpipe, and does not produce any sound.
	 */
	if (chip->usb_id == USB_ID(0x04fa, 0x4201))
		num = 4;

	for (i = 0; i < num; i++) {
		alts = &iface->altsetting[i];
		altsd = get_iface_desc(alts);
		protocol = altsd->bInterfaceProtocol;
		/* skip invalid one */
		if ((altsd->bInterfaceClass != USB_CLASS_AUDIO &&
		     altsd->bInterfaceClass != USB_CLASS_VENDOR_SPEC) ||
		    (altsd->bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING &&
		     altsd->bInterfaceSubClass != USB_SUBCLASS_VENDOR_SPEC) ||
		    altsd->bNumEndpoints < 1 ||
		    le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize) == 0)
			continue;
		/* must be isochronous */
		if ((get_endpoint(alts, 0)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
		    USB_ENDPOINT_XFER_ISOC)
			continue;
		/* check direction */
		stream = (get_endpoint(alts, 0)->bEndpointAddress & USB_DIR_IN) ?
			SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
		altno = altsd->bAlternateSetting;
	
		/* audiophile usb: skip altsets incompatible with device_setup
		 */
		if (chip->usb_id == USB_ID(0x0763, 0x2003) && 
		    audiophile_skip_setting_quirk(chip, iface_no, altno))
			continue;

		/* get audio formats */
		switch (protocol) {
		case UAC_VERSION_1: {
			struct uac_as_header_descriptor_v1 *as =
				snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_AS_GENERAL);

			if (!as) {
				snd_printk(KERN_ERR "%d:%u:%d : UAC_AS_GENERAL descriptor not found\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			if (as->bLength < sizeof(*as)) {
				snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_AS_GENERAL desc\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			format = le16_to_cpu(as->wFormatTag); /* remember the format value */
			break;
		}

		case UAC_VERSION_2: {
			struct uac_as_header_descriptor_v2 *as =
				snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_AS_GENERAL);

			if (!as) {
				snd_printk(KERN_ERR "%d:%u:%d : UAC_AS_GENERAL descriptor not found\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			if (as->bLength < sizeof(*as)) {
				snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_AS_GENERAL desc\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			num_channels = as->bNrChannels;
			format = le32_to_cpu(as->bmFormats);

			break;
		}

		default:
			snd_printk(KERN_ERR "%d:%u:%d : unknown interface protocol %04x\n",
				   dev->devnum, iface_no, altno, protocol);
			continue;
		}

		/* get format type */
		fmt = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_FORMAT_TYPE);
		if (!fmt) {
			snd_printk(KERN_ERR "%d:%u:%d : no UAC_FORMAT_TYPE desc\n",
				   dev->devnum, iface_no, altno);
			continue;
		}
		if (((protocol == UAC_VERSION_1) && (fmt[0] < 8)) ||
		    ((protocol == UAC_VERSION_2) && (fmt[0] != 6))) {
			snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_FORMAT_TYPE desc\n",
				   dev->devnum, iface_no, altno);
			continue;
		}

		/*
		 * Blue Microphones workaround: The last altsetting is identical
		 * with the previous one, except for a larger packet size, but
		 * is actually a mislabeled two-channel setting; ignore it.
		 */
		if (fmt[4] == 1 && fmt[5] == 2 && altno == 2 && num == 3 &&
		    fp && fp->altsetting == 1 && fp->channels == 1 &&
		    fp->format == SNDRV_PCM_FORMAT_S16_LE &&
		    protocol == UAC_VERSION_1 &&
		    le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize) ==
							fp->maxpacksize * 2)
			continue;

		csep = snd_usb_find_desc(alts->endpoint[0].extra, alts->endpoint[0].extralen, NULL, USB_DT_CS_ENDPOINT);
		/* Creamware Noah has this descriptor after the 2nd endpoint */
		if (!csep && altsd->bNumEndpoints >= 2)
			csep = snd_usb_find_desc(alts->endpoint[1].extra, alts->endpoint[1].extralen, NULL, USB_DT_CS_ENDPOINT);
		if (!csep || csep[0] < 7 || csep[2] != UAC_EP_GENERAL) {
			snd_printk(KERN_WARNING "%d:%u:%d : no or invalid"
				   " class specific endpoint descriptor\n",
				   dev->devnum, iface_no, altno);
			csep = NULL;
		}

		fp = kzalloc(sizeof(*fp), GFP_KERNEL);
		if (! fp) {
			snd_printk(KERN_ERR "cannot malloc\n");
			return -ENOMEM;
		}

		fp->iface = iface_no;
		fp->altsetting = altno;
		fp->altset_idx = i;
		fp->endpoint = get_endpoint(alts, 0)->bEndpointAddress;
		fp->ep_attr = get_endpoint(alts, 0)->bmAttributes;
		fp->datainterval = parse_datainterval(chip, alts);
		fp->maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);
		/* num_channels is only set for v2 interfaces */
		fp->channels = num_channels;
		if (snd_usb_get_speed(dev) == USB_SPEED_HIGH)
			fp->maxpacksize = (((fp->maxpacksize >> 11) & 3) + 1)
					* (fp->maxpacksize & 0x7ff);
		fp->attributes = csep ? csep[3] : 0;

		/* some quirks for attributes here */

		switch (chip->usb_id) {
		case USB_ID(0x0a92, 0x0053): /* AudioTrak Optoplay */
			/* Optoplay sets the sample rate attribute although
			 * it seems not supporting it in fact.
			 */
			fp->attributes &= ~UAC_EP_CS_ATTR_SAMPLE_RATE;
			break;
		case USB_ID(0x041e, 0x3020): /* Creative SB Audigy 2 NX */
		case USB_ID(0x0763, 0x2003): /* M-Audio Audiophile USB */
			/* doesn't set the sample rate attribute, but supports it */
			fp->attributes |= UAC_EP_CS_ATTR_SAMPLE_RATE;
			break;
		case USB_ID(0x047f, 0x0ca1): /* plantronics headset */
		case USB_ID(0x077d, 0x07af): /* Griffin iMic (note that there is
						an older model 77d:223) */
		/*
		 * plantronics headset and Griffin iMic have set adaptive-in
		 * although it's really not...
		 */
			fp->ep_attr &= ~USB_ENDPOINT_SYNCTYPE;
			if (stream == SNDRV_PCM_STREAM_PLAYBACK)
				fp->ep_attr |= USB_ENDPOINT_SYNC_ADAPTIVE;
			else
				fp->ep_attr |= USB_ENDPOINT_SYNC_SYNC;
			break;
		}

		/* ok, let's parse further... */
		if (parse_audio_format(chip, fp, format, fmt, stream, alts) < 0) {
			kfree(fp->rate_table);
			kfree(fp);
			continue;
		}

		snd_printdd(KERN_INFO "%d:%u:%d: add audio endpoint %#x\n", dev->devnum, iface_no, altno, fp->endpoint);
		err = add_audio_endpoint(chip, stream, fp);
		if (err < 0) {
			kfree(fp->rate_table);
			kfree(fp);
			return err;
		}
		/* try to set the interface... */
		usb_set_interface(chip->dev, iface_no, altno);
		init_usb_pitch(chip->dev, iface_no, alts, fp);
		init_usb_sample_rate(chip->dev, iface_no, alts, fp, fp->rate_max);
	}
	return 0;
}


/*
 * disconnect streams
 * called from snd_usb_audio_disconnect()
 */
static void snd_usb_stream_disconnect(struct list_head *head)
{
	int idx;
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs;

	as = list_entry(head, struct snd_usb_stream, list);
	for (idx = 0; idx < 2; idx++) {
		subs = &as->substream[idx];
		if (!subs->num_formats)
			return;
		release_substream_urbs(subs, 1);
		subs->interface = -1;
	}
}

static int snd_usb_create_stream(struct snd_usb_audio *chip, int ctrlif, int interface)
{
	struct usb_device *dev = chip->dev;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct usb_interface *iface = usb_ifnum_to_if(dev, interface);

	if (!iface) {
		snd_printk(KERN_ERR "%d:%u:%d : does not exist\n",
			   dev->devnum, ctrlif, interface);
		return -EINVAL;
	}

	if (usb_interface_claimed(iface)) {
		snd_printdd(KERN_INFO "%d:%d:%d: skipping, already claimed\n",
						dev->devnum, ctrlif, interface);
		return -EINVAL;
	}

	alts = &iface->altsetting[0];
	altsd = get_iface_desc(alts);
	if ((altsd->bInterfaceClass == USB_CLASS_AUDIO ||
	     altsd->bInterfaceClass == USB_CLASS_VENDOR_SPEC) &&
	    altsd->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAMING) {
		int err = snd_usbmidi_create(chip->card, iface,
					     &chip->midi_list, NULL);
		if (err < 0) {
			snd_printk(KERN_ERR "%d:%u:%d: cannot create sequencer device\n",
						dev->devnum, ctrlif, interface);
			return -EINVAL;
		}
		usb_driver_claim_interface(&usb_audio_driver, iface, (void *)-1L);

		return 0;
	}

	if ((altsd->bInterfaceClass != USB_CLASS_AUDIO &&
	     altsd->bInterfaceClass != USB_CLASS_VENDOR_SPEC) ||
	    altsd->bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING) {
		snd_printdd(KERN_ERR "%d:%u:%d: skipping non-supported interface %d\n",
					dev->devnum, ctrlif, interface, altsd->bInterfaceClass);
		/* skip non-supported classes */
		return -EINVAL;
	}

	if (snd_usb_get_speed(dev) == USB_SPEED_LOW) {
		snd_printk(KERN_ERR "low speed audio streaming not supported\n");
		return -EINVAL;
	}

	if (! parse_audio_endpoints(chip, interface)) {
		usb_set_interface(dev, interface, 0); /* reset the current interface */
		usb_driver_claim_interface(&usb_audio_driver, iface, (void *)-1L);
		return -EINVAL;
	}

	return 0;
}

/*
 * parse audio control descriptor and create pcm/midi streams
 */
static int snd_usb_create_streams(struct snd_usb_audio *chip, int ctrlif)
{
	struct usb_device *dev = chip->dev;
	struct usb_host_interface *host_iface;
	struct usb_interface_descriptor *altsd;
	void *control_header;
	int i, protocol;

	/* find audiocontrol interface */
	host_iface = &usb_ifnum_to_if(dev, ctrlif)->altsetting[0];
	control_header = snd_usb_find_csint_desc(host_iface->extra,
						 host_iface->extralen,
						 NULL, UAC_HEADER);
	altsd = get_iface_desc(host_iface);
	protocol = altsd->bInterfaceProtocol;

	if (!control_header) {
		snd_printk(KERN_ERR "cannot find UAC_HEADER\n");
		return -EINVAL;
	}

	switch (protocol) {
	case UAC_VERSION_1: {
		struct uac_ac_header_descriptor_v1 *h1 = control_header;

		if (!h1->bInCollection) {
			snd_printk(KERN_INFO "skipping empty audio interface (v1)\n");
			return -EINVAL;
		}

		if (h1->bLength < sizeof(*h1) + h1->bInCollection) {
			snd_printk(KERN_ERR "invalid UAC_HEADER (v1)\n");
			return -EINVAL;
		}

		for (i = 0; i < h1->bInCollection; i++)
			snd_usb_create_stream(chip, ctrlif, h1->baInterfaceNr[i]);

		break;
	}

	case UAC_VERSION_2: {
		struct uac_clock_source_descriptor *cs;
		struct usb_interface_assoc_descriptor *assoc =
			usb_ifnum_to_if(dev, ctrlif)->intf_assoc;

		if (!assoc) {
			snd_printk(KERN_ERR "Audio class v2 interfaces need an interface association\n");
			return -EINVAL;
		}

		/* FIXME: for now, we expect there is at least one clock source
		 * descriptor and we always take the first one.
		 * We should properly support devices with multiple clock sources,
		 * clock selectors and sample rate conversion units. */

		cs = snd_usb_find_csint_desc(host_iface->extra, host_iface->extralen,
						NULL, UAC_CLOCK_SOURCE);

		if (!cs) {
			snd_printk(KERN_ERR "CLOCK_SOURCE descriptor not found\n");
			return -EINVAL;
		}

		chip->clock_id = cs->bClockID;

		for (i = 0; i < assoc->bInterfaceCount; i++) {
			int intf = assoc->bFirstInterface + i;

			if (intf != ctrlif)
				snd_usb_create_stream(chip, ctrlif, intf);
		}

		break;
	}

	default:
		snd_printk(KERN_ERR "unknown protocol version 0x%02x\n", protocol);
		return -EINVAL;
	}

	return 0;
}

/*
 * create a stream for an endpoint/altsetting without proper descriptors
 */
static int create_fixed_stream_quirk(struct snd_usb_audio *chip,
				     struct usb_interface *iface,
				     const struct snd_usb_audio_quirk *quirk)
{
	struct audioformat *fp;
	struct usb_host_interface *alts;
	int stream, err;
	unsigned *rate_table = NULL;

	fp = kmemdup(quirk->data, sizeof(*fp), GFP_KERNEL);
	if (! fp) {
		snd_printk(KERN_ERR "cannot memdup\n");
		return -ENOMEM;
	}
	if (fp->nr_rates > 0) {
		rate_table = kmalloc(sizeof(int) * fp->nr_rates, GFP_KERNEL);
		if (!rate_table) {
			kfree(fp);
			return -ENOMEM;
		}
		memcpy(rate_table, fp->rate_table, sizeof(int) * fp->nr_rates);
		fp->rate_table = rate_table;
	}

	stream = (fp->endpoint & USB_DIR_IN)
		? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
	err = add_audio_endpoint(chip, stream, fp);
	if (err < 0) {
		kfree(fp);
		kfree(rate_table);
		return err;
	}
	if (fp->iface != get_iface_desc(&iface->altsetting[0])->bInterfaceNumber ||
	    fp->altset_idx >= iface->num_altsetting) {
		kfree(fp);
		kfree(rate_table);
		return -EINVAL;
	}
	alts = &iface->altsetting[fp->altset_idx];
	fp->datainterval = parse_datainterval(chip, alts);
	fp->maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);
	usb_set_interface(chip->dev, fp->iface, 0);
	init_usb_pitch(chip->dev, fp->iface, alts, fp);
	init_usb_sample_rate(chip->dev, fp->iface, alts, fp, fp->rate_max);
	return 0;
}

/*
 * create a stream for an interface with proper descriptors
 */
static int create_standard_audio_quirk(struct snd_usb_audio *chip,
				       struct usb_interface *iface,
				       const struct snd_usb_audio_quirk *quirk)
{
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	int err;

	alts = &iface->altsetting[0];
	altsd = get_iface_desc(alts);
	err = parse_audio_endpoints(chip, altsd->bInterfaceNumber);
	if (err < 0) {
		snd_printk(KERN_ERR "cannot setup if %d: error %d\n",
			   altsd->bInterfaceNumber, err);
		return err;
	}
	/* reset the current interface */
	usb_set_interface(chip->dev, altsd->bInterfaceNumber, 0);
	return 0;
}

/*
 * Create a stream for an Edirol UA-700/UA-25/UA-4FX interface.  
 * The only way to detect the sample rate is by looking at wMaxPacketSize.
 */
static int create_uaxx_quirk(struct snd_usb_audio *chip,
			      struct usb_interface *iface,
			      const struct snd_usb_audio_quirk *quirk)
{
	static const struct audioformat ua_format = {
		.format = SNDRV_PCM_FORMAT_S24_3LE,
		.channels = 2,
		.fmt_type = UAC_FORMAT_TYPE_I,
		.altsetting = 1,
		.altset_idx = 1,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
	};
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct audioformat *fp;
	int stream, err;

	/* both PCM and MIDI interfaces have 2 or more altsettings */
	if (iface->num_altsetting < 2)
		return -ENXIO;
	alts = &iface->altsetting[1];
	altsd = get_iface_desc(alts);

	if (altsd->bNumEndpoints == 2) {
		static const struct snd_usb_midi_endpoint_info ua700_ep = {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		};
		static const struct snd_usb_audio_quirk ua700_quirk = {
			.type = QUIRK_MIDI_FIXED_ENDPOINT,
			.data = &ua700_ep
		};
		static const struct snd_usb_midi_endpoint_info uaxx_ep = {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		};
		static const struct snd_usb_audio_quirk uaxx_quirk = {
			.type = QUIRK_MIDI_FIXED_ENDPOINT,
			.data = &uaxx_ep
		};
		const struct snd_usb_audio_quirk *quirk =
			chip->usb_id == USB_ID(0x0582, 0x002b)
			? &ua700_quirk : &uaxx_quirk;
		return snd_usbmidi_create(chip->card, iface,
					  &chip->midi_list, quirk);
	}

	if (altsd->bNumEndpoints != 1)
		return -ENXIO;

	fp = kmalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;
	memcpy(fp, &ua_format, sizeof(*fp));

	fp->iface = altsd->bInterfaceNumber;
	fp->endpoint = get_endpoint(alts, 0)->bEndpointAddress;
	fp->ep_attr = get_endpoint(alts, 0)->bmAttributes;
	fp->datainterval = 0;
	fp->maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);

	switch (fp->maxpacksize) {
	case 0x120:
		fp->rate_max = fp->rate_min = 44100;
		break;
	case 0x138:
	case 0x140:
		fp->rate_max = fp->rate_min = 48000;
		break;
	case 0x258:
	case 0x260:
		fp->rate_max = fp->rate_min = 96000;
		break;
	default:
		snd_printk(KERN_ERR "unknown sample rate\n");
		kfree(fp);
		return -ENXIO;
	}

	stream = (fp->endpoint & USB_DIR_IN)
		? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
	err = add_audio_endpoint(chip, stream, fp);
	if (err < 0) {
		kfree(fp);
		return err;
	}
	usb_set_interface(chip->dev, fp->iface, 0);
	return 0;
}

static int snd_usb_create_quirk(struct snd_usb_audio *chip,
				struct usb_interface *iface,
				const struct snd_usb_audio_quirk *quirk);

/*
 * handle the quirks for the contained interfaces
 */
static int create_composite_quirk(struct snd_usb_audio *chip,
				  struct usb_interface *iface,
				  const struct snd_usb_audio_quirk *quirk)
{
	int probed_ifnum = get_iface_desc(iface->altsetting)->bInterfaceNumber;
	int err;

	for (quirk = quirk->data; quirk->ifnum >= 0; ++quirk) {
		iface = usb_ifnum_to_if(chip->dev, quirk->ifnum);
		if (!iface)
			continue;
		if (quirk->ifnum != probed_ifnum &&
		    usb_interface_claimed(iface))
			continue;
		err = snd_usb_create_quirk(chip, iface, quirk);
		if (err < 0)
			return err;
		if (quirk->ifnum != probed_ifnum)
			usb_driver_claim_interface(&usb_audio_driver, iface, (void *)-1L);
	}
	return 0;
}

static int ignore_interface_quirk(struct snd_usb_audio *chip,
				  struct usb_interface *iface,
				  const struct snd_usb_audio_quirk *quirk)
{
	return 0;
}

/*
 * Allow alignment on audio sub-slot (channel samples) rather than
 * on audio slots (audio frames)
 */
static int create_align_transfer_quirk(struct snd_usb_audio *chip,
				  struct usb_interface *iface,
				  const struct snd_usb_audio_quirk *quirk)
{
	chip->txfr_quirk = 1;
	return 1;	/* Continue with creating streams and mixer */
}


/*
 * boot quirks
 */

#define EXTIGY_FIRMWARE_SIZE_OLD 794
#define EXTIGY_FIRMWARE_SIZE_NEW 483

static int snd_usb_extigy_boot_quirk(struct usb_device *dev, struct usb_interface *intf)
{
	struct usb_host_config *config = dev->actconfig;
	int err;

	if (le16_to_cpu(get_cfg_desc(config)->wTotalLength) == EXTIGY_FIRMWARE_SIZE_OLD ||
	    le16_to_cpu(get_cfg_desc(config)->wTotalLength) == EXTIGY_FIRMWARE_SIZE_NEW) {
		snd_printdd("sending Extigy boot sequence...\n");
		/* Send message to force it to reconnect with full interface. */
		err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev,0),
				      0x10, 0x43, 0x0001, 0x000a, NULL, 0, 1000);
		if (err < 0) snd_printdd("error sending boot message: %d\n", err);
		err = usb_get_descriptor(dev, USB_DT_DEVICE, 0,
				&dev->descriptor, sizeof(dev->descriptor));
		config = dev->actconfig;
		if (err < 0) snd_printdd("error usb_get_descriptor: %d\n", err);
		err = usb_reset_configuration(dev);
		if (err < 0) snd_printdd("error usb_reset_configuration: %d\n", err);
		snd_printdd("extigy_boot: new boot length = %d\n",
			    le16_to_cpu(get_cfg_desc(config)->wTotalLength));
		return -ENODEV; /* quit this anyway */
	}
	return 0;
}

static int snd_usb_audigy2nx_boot_quirk(struct usb_device *dev)
{
	u8 buf = 1;

	snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), 0x2a,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			0, 0, &buf, 1, 1000);
	if (buf == 0) {
		snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), 0x29,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
				1, 2000, NULL, 0, 1000);
		return -ENODEV;
	}
	return 0;
}

/*
 * C-Media CM106/CM106+ have four 16-bit internal registers that are nicely
 * documented in the device's data sheet.
 */
static int snd_usb_cm106_write_int_reg(struct usb_device *dev, int reg, u16 value)
{
	u8 buf[4];
	buf[0] = 0x20;
	buf[1] = value & 0xff;
	buf[2] = (value >> 8) & 0xff;
	buf[3] = reg;
	return snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), USB_REQ_SET_CONFIGURATION,
			       USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			       0, 0, &buf, 4, 1000);
}

static int snd_usb_cm106_boot_quirk(struct usb_device *dev)
{
	/*
	 * Enable line-out driver mode, set headphone source to front
	 * channels, enable stereo mic.
	 */
	return snd_usb_cm106_write_int_reg(dev, 2, 0x8004);
}

/*
 * C-Media CM6206 is based on CM106 with two additional
 * registers that are not documented in the data sheet.
 * Values here are chosen based on sniffing USB traffic
 * under Windows.
 */
static int snd_usb_cm6206_boot_quirk(struct usb_device *dev)
{
	int err, reg;
	int val[] = {0x200c, 0x3000, 0xf800, 0x143f, 0x0000, 0x3000};

	for (reg = 0; reg < ARRAY_SIZE(val); reg++) {
		err = snd_usb_cm106_write_int_reg(dev, reg, val[reg]);
		if (err < 0)
			return err;
	}

	return err;
}

/*
 * This call will put the synth in "USB send" mode, i.e it will send MIDI
 * messages through USB (this is disabled at startup). The synth will
 * acknowledge by sending a sysex on endpoint 0x85 and by displaying a USB
 * sign on its LCD. Values here are chosen based on sniffing USB traffic
 * under Windows.
 */
static int snd_usb_accessmusic_boot_quirk(struct usb_device *dev)
{
	int err, actual_length;

	/* "midi send" enable */
	static const u8 seq[] = { 0x4e, 0x73, 0x52, 0x01 };

	void *buf = kmemdup(seq, ARRAY_SIZE(seq), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	err = usb_interrupt_msg(dev, usb_sndintpipe(dev, 0x05), buf,
			ARRAY_SIZE(seq), &actual_length, 1000);
	kfree(buf);
	if (err < 0)
		return err;

	return 0;
}

/*
 * Setup quirks
 */
#define AUDIOPHILE_SET			0x01 /* if set, parse device_setup */
#define AUDIOPHILE_SET_DTS              0x02 /* if set, enable DTS Digital Output */
#define AUDIOPHILE_SET_96K              0x04 /* 48-96KHz rate if set, 8-48KHz otherwise */
#define AUDIOPHILE_SET_24B		0x08 /* 24bits sample if set, 16bits otherwise */
#define AUDIOPHILE_SET_DI		0x10 /* if set, enable Digital Input */
#define AUDIOPHILE_SET_MASK		0x1F /* bit mask for setup value */
#define AUDIOPHILE_SET_24B_48K_DI	0x19 /* value for 24bits+48KHz+Digital Input */
#define AUDIOPHILE_SET_24B_48K_NOTDI	0x09 /* value for 24bits+48KHz+No Digital Input */
#define AUDIOPHILE_SET_16B_48K_DI	0x11 /* value for 16bits+48KHz+Digital Input */
#define AUDIOPHILE_SET_16B_48K_NOTDI	0x01 /* value for 16bits+48KHz+No Digital Input */

static int audiophile_skip_setting_quirk(struct snd_usb_audio *chip,
					 int iface, int altno)
{
	/* Reset ALL ifaces to 0 altsetting.
	 * Call it for every possible altsetting of every interface.
	 */
	usb_set_interface(chip->dev, iface, 0);

	if (device_setup[chip->index] & AUDIOPHILE_SET) {
		if ((device_setup[chip->index] & AUDIOPHILE_SET_DTS)
		    && altno != 6)
			return 1; /* skip this altsetting */
		if ((device_setup[chip->index] & AUDIOPHILE_SET_96K)
		    && altno != 1)
			return 1; /* skip this altsetting */
		if ((device_setup[chip->index] & AUDIOPHILE_SET_MASK) ==
		    AUDIOPHILE_SET_24B_48K_DI && altno != 2)
			return 1; /* skip this altsetting */
		if ((device_setup[chip->index] & AUDIOPHILE_SET_MASK) ==
		    AUDIOPHILE_SET_24B_48K_NOTDI && altno != 3)
			return 1; /* skip this altsetting */
		if ((device_setup[chip->index] & AUDIOPHILE_SET_MASK) ==
		    AUDIOPHILE_SET_16B_48K_DI && altno != 4)
			return 1; /* skip this altsetting */
		if ((device_setup[chip->index] & AUDIOPHILE_SET_MASK) ==
		    AUDIOPHILE_SET_16B_48K_NOTDI && altno != 5)
			return 1; /* skip this altsetting */
	}	
	return 0; /* keep this altsetting */
}

static int create_any_midi_quirk(struct snd_usb_audio *chip,
				 struct usb_interface *intf,
				 const struct snd_usb_audio_quirk *quirk)
{
	return snd_usbmidi_create(chip->card, intf, &chip->midi_list, quirk);
}

/*
 * audio-interface quirks
 *
 * returns zero if no standard audio/MIDI parsing is needed.
 * returns a postive value if standard audio/midi interfaces are parsed
 * after this.
 * returns a negative value at error.
 */
static int snd_usb_create_quirk(struct snd_usb_audio *chip,
				struct usb_interface *iface,
				const struct snd_usb_audio_quirk *quirk)
{
	typedef int (*quirk_func_t)(struct snd_usb_audio *, struct usb_interface *,
				    const struct snd_usb_audio_quirk *);
	static const quirk_func_t quirk_funcs[] = {
		[QUIRK_IGNORE_INTERFACE] = ignore_interface_quirk,
		[QUIRK_COMPOSITE] = create_composite_quirk,
		[QUIRK_MIDI_STANDARD_INTERFACE] = create_any_midi_quirk,
		[QUIRK_MIDI_FIXED_ENDPOINT] = create_any_midi_quirk,
		[QUIRK_MIDI_YAMAHA] = create_any_midi_quirk,
		[QUIRK_MIDI_MIDIMAN] = create_any_midi_quirk,
		[QUIRK_MIDI_NOVATION] = create_any_midi_quirk,
		[QUIRK_MIDI_FASTLANE] = create_any_midi_quirk,
		[QUIRK_MIDI_EMAGIC] = create_any_midi_quirk,
		[QUIRK_MIDI_CME] = create_any_midi_quirk,
		[QUIRK_AUDIO_STANDARD_INTERFACE] = create_standard_audio_quirk,
		[QUIRK_AUDIO_FIXED_ENDPOINT] = create_fixed_stream_quirk,
		[QUIRK_AUDIO_EDIROL_UAXX] = create_uaxx_quirk,
		[QUIRK_AUDIO_ALIGN_TRANSFER] = create_align_transfer_quirk
	};

	if (quirk->type < QUIRK_TYPE_COUNT) {
		return quirk_funcs[quirk->type](chip, iface, quirk);
	} else {
		snd_printd(KERN_ERR "invalid quirk type %d\n", quirk->type);
		return -ENXIO;
	}
}


/*
 * common proc files to show the usb device info
 */
static void proc_audio_usbbus_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_usb_audio *chip = entry->private_data;
	if (!chip->shutdown)
		snd_iprintf(buffer, "%03d/%03d\n", chip->dev->bus->busnum, chip->dev->devnum);
}

static void proc_audio_usbid_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_usb_audio *chip = entry->private_data;
	if (!chip->shutdown)
		snd_iprintf(buffer, "%04x:%04x\n", 
			    USB_ID_VENDOR(chip->usb_id),
			    USB_ID_PRODUCT(chip->usb_id));
}

static void snd_usb_audio_create_proc(struct snd_usb_audio *chip)
{
	struct snd_info_entry *entry;
	if (!snd_card_proc_new(chip->card, "usbbus", &entry))
		snd_info_set_text_ops(entry, chip, proc_audio_usbbus_read);
	if (!snd_card_proc_new(chip->card, "usbid", &entry))
		snd_info_set_text_ops(entry, chip, proc_audio_usbid_read);
}

/*
 * free the chip instance
 *
 * here we have to do not much, since pcm and controls are already freed
 *
 */

static int snd_usb_audio_free(struct snd_usb_audio *chip)
{
	kfree(chip);
	return 0;
}

static int snd_usb_audio_dev_free(struct snd_device *device)
{
	struct snd_usb_audio *chip = device->device_data;
	return snd_usb_audio_free(chip);
}


/*
 * create a chip instance and set its names.
 */
static int snd_usb_audio_create(struct usb_device *dev, int idx,
				const struct snd_usb_audio_quirk *quirk,
				struct snd_usb_audio **rchip)
{
	struct snd_card *card;
	struct snd_usb_audio *chip;
	int err, len;
	char component[14];
	static struct snd_device_ops ops = {
		.dev_free =	snd_usb_audio_dev_free,
	};

	*rchip = NULL;

	if (snd_usb_get_speed(dev) != USB_SPEED_LOW &&
	    snd_usb_get_speed(dev) != USB_SPEED_FULL &&
	    snd_usb_get_speed(dev) != USB_SPEED_HIGH) {
		snd_printk(KERN_ERR "unknown device speed %d\n", snd_usb_get_speed(dev));
		return -ENXIO;
	}

	err = snd_card_create(index[idx], id[idx], THIS_MODULE, 0, &card);
	if (err < 0) {
		snd_printk(KERN_ERR "cannot create card instance %d\n", idx);
		return err;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (! chip) {
		snd_card_free(card);
		return -ENOMEM;
	}

	chip->index = idx;
	chip->dev = dev;
	chip->card = card;
	chip->usb_id = USB_ID(le16_to_cpu(dev->descriptor.idVendor),
			      le16_to_cpu(dev->descriptor.idProduct));
	INIT_LIST_HEAD(&chip->pcm_list);
	INIT_LIST_HEAD(&chip->midi_list);
	INIT_LIST_HEAD(&chip->mixer_list);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_usb_audio_free(chip);
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "USB-Audio");
	sprintf(component, "USB%04x:%04x",
		USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	snd_component_add(card, component);

	/* retrieve the device string as shortname */
 	if (quirk && quirk->product_name) {
		strlcpy(card->shortname, quirk->product_name, sizeof(card->shortname));
	} else {
		if (!dev->descriptor.iProduct ||
		    usb_string(dev, dev->descriptor.iProduct,
      			       card->shortname, sizeof(card->shortname)) <= 0) {
			/* no name available from anywhere, so use ID */
			sprintf(card->shortname, "USB Device %#04x:%#04x",
				USB_ID_VENDOR(chip->usb_id),
				USB_ID_PRODUCT(chip->usb_id));
		}
	}

	/* retrieve the vendor and device strings as longname */
	if (quirk && quirk->vendor_name) {
		len = strlcpy(card->longname, quirk->vendor_name, sizeof(card->longname));
	} else {
		if (dev->descriptor.iManufacturer)
			len = usb_string(dev, dev->descriptor.iManufacturer,
					 card->longname, sizeof(card->longname));
		else
			len = 0;
		/* we don't really care if there isn't any vendor string */
	}
	if (len > 0)
		strlcat(card->longname, " ", sizeof(card->longname));

	strlcat(card->longname, card->shortname, sizeof(card->longname));

	len = strlcat(card->longname, " at ", sizeof(card->longname));

	if (len < sizeof(card->longname))
		usb_make_path(dev, card->longname + len, sizeof(card->longname) - len);

	strlcat(card->longname,
		snd_usb_get_speed(dev) == USB_SPEED_LOW ? ", low speed" :
		snd_usb_get_speed(dev) == USB_SPEED_FULL ? ", full speed" :
		", high speed",
		sizeof(card->longname));

	snd_usb_audio_create_proc(chip);

	*rchip = chip;
	return 0;
}


/*
 * probe the active usb device
 *
 * note that this can be called multiple times per a device, when it
 * includes multiple audio control interfaces.
 *
 * thus we check the usb device pointer and creates the card instance
 * only at the first time.  the successive calls of this function will
 * append the pcm interface to the corresponding card.
 */
static void *snd_usb_audio_probe(struct usb_device *dev,
				 struct usb_interface *intf,
				 const struct usb_device_id *usb_id)
{
	const struct snd_usb_audio_quirk *quirk = (const struct snd_usb_audio_quirk *)usb_id->driver_info;
	int i, err;
	struct snd_usb_audio *chip;
	struct usb_host_interface *alts;
	int ifnum;
	u32 id;

	alts = &intf->altsetting[0];
	ifnum = get_iface_desc(alts)->bInterfaceNumber;
	id = USB_ID(le16_to_cpu(dev->descriptor.idVendor),
		    le16_to_cpu(dev->descriptor.idProduct));
	if (quirk && quirk->ifnum >= 0 && ifnum != quirk->ifnum)
		goto __err_val;

	/* SB Extigy needs special boot-up sequence */
	/* if more models come, this will go to the quirk list. */
	if (id == USB_ID(0x041e, 0x3000)) {
		if (snd_usb_extigy_boot_quirk(dev, intf) < 0)
			goto __err_val;
	}
	/* SB Audigy 2 NX needs its own boot-up magic, too */
	if (id == USB_ID(0x041e, 0x3020)) {
		if (snd_usb_audigy2nx_boot_quirk(dev) < 0)
			goto __err_val;
	}

	/* C-Media CM106 / Turtle Beach Audio Advantage Roadie */
	if (id == USB_ID(0x10f5, 0x0200)) {
		if (snd_usb_cm106_boot_quirk(dev) < 0)
			goto __err_val;
	}

	/* C-Media CM6206 / CM106-Like Sound Device */
	if (id == USB_ID(0x0d8c, 0x0102)) {
		if (snd_usb_cm6206_boot_quirk(dev) < 0)
			goto __err_val;
	}

	/* Access Music VirusTI Desktop */
	if (id == USB_ID(0x133e, 0x0815)) {
		if (snd_usb_accessmusic_boot_quirk(dev) < 0)
			goto __err_val;
	}

	/*
	 * found a config.  now register to ALSA
	 */

	/* check whether it's already registered */
	chip = NULL;
	mutex_lock(&register_mutex);
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (usb_chip[i] && usb_chip[i]->dev == dev) {
			if (usb_chip[i]->shutdown) {
				snd_printk(KERN_ERR "USB device is in the shutdown state, cannot create a card instance\n");
				goto __error;
			}
			chip = usb_chip[i];
			break;
		}
	}
	if (! chip) {
		/* it's a fresh one.
		 * now look for an empty slot and create a new card instance
		 */
		for (i = 0; i < SNDRV_CARDS; i++)
			if (enable[i] && ! usb_chip[i] &&
			    (vid[i] == -1 || vid[i] == USB_ID_VENDOR(id)) &&
			    (pid[i] == -1 || pid[i] == USB_ID_PRODUCT(id))) {
				if (snd_usb_audio_create(dev, i, quirk, &chip) < 0) {
					goto __error;
				}
				snd_card_set_dev(chip->card, &intf->dev);
				break;
			}
		if (!chip) {
			printk(KERN_ERR "no available usb audio device\n");
			goto __error;
		}
	}

	chip->txfr_quirk = 0;
	err = 1; /* continue */
	if (quirk && quirk->ifnum != QUIRK_NO_INTERFACE) {
		/* need some special handlings */
		if ((err = snd_usb_create_quirk(chip, intf, quirk)) < 0)
			goto __error;
	}

	if (err > 0) {
		/* create normal USB audio interfaces */
		if (snd_usb_create_streams(chip, ifnum) < 0 ||
		    snd_usb_create_mixer(chip, ifnum, ignore_ctl_error) < 0) {
			goto __error;
		}
	}

	/* we are allowed to call snd_card_register() many times */
	if (snd_card_register(chip->card) < 0) {
		goto __error;
	}

	usb_chip[chip->index] = chip;
	chip->num_interfaces++;
	mutex_unlock(&register_mutex);
	return chip;

 __error:
	if (chip && !chip->num_interfaces)
		snd_card_free(chip->card);
	mutex_unlock(&register_mutex);
 __err_val:
	return NULL;
}

/*
 * we need to take care of counter, since disconnection can be called also
 * many times as well as usb_audio_probe().
 */
static void snd_usb_audio_disconnect(struct usb_device *dev, void *ptr)
{
	struct snd_usb_audio *chip;
	struct snd_card *card;
	struct list_head *p;

	if (ptr == (void *)-1L)
		return;

	chip = ptr;
	card = chip->card;
	mutex_lock(&register_mutex);
	chip->shutdown = 1;
	chip->num_interfaces--;
	if (chip->num_interfaces <= 0) {
		snd_card_disconnect(card);
		/* release the pcm resources */
		list_for_each(p, &chip->pcm_list) {
			snd_usb_stream_disconnect(p);
		}
		/* release the midi resources */
		list_for_each(p, &chip->midi_list) {
			snd_usbmidi_disconnect(p);
		}
		/* release mixer resources */
		list_for_each(p, &chip->mixer_list) {
			snd_usb_mixer_disconnect(p);
		}
		usb_chip[chip->index] = NULL;
		mutex_unlock(&register_mutex);
		snd_card_free_when_closed(card);
	} else {
		mutex_unlock(&register_mutex);
	}
}

/*
 * new 2.5 USB kernel API
 */
static int usb_audio_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	void *chip;
	chip = snd_usb_audio_probe(interface_to_usbdev(intf), intf, id);
	if (chip) {
		usb_set_intfdata(intf, chip);
		return 0;
	} else
		return -EIO;
}

static void usb_audio_disconnect(struct usb_interface *intf)
{
	snd_usb_audio_disconnect(interface_to_usbdev(intf),
				 usb_get_intfdata(intf));
}

#ifdef CONFIG_PM
static int usb_audio_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct snd_usb_audio *chip = usb_get_intfdata(intf);
	struct list_head *p;
	struct snd_usb_stream *as;

	if (chip == (void *)-1L)
		return 0;

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D3hot);
	if (!chip->num_suspended_intf++) {
		list_for_each(p, &chip->pcm_list) {
			as = list_entry(p, struct snd_usb_stream, list);
			snd_pcm_suspend_all(as->pcm);
		}
	}

	return 0;
}

static int usb_audio_resume(struct usb_interface *intf)
{
	struct snd_usb_audio *chip = usb_get_intfdata(intf);

	if (chip == (void *)-1L)
		return 0;
	if (--chip->num_suspended_intf)
		return 0;
	/*
	 * ALSA leaves material resumption to user space
	 * we just notify
	 */

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D0);

	return 0;
}
#endif		/* CONFIG_PM */

static int __init snd_usb_audio_init(void)
{
	if (nrpacks < 1 || nrpacks > MAX_PACKS) {
		printk(KERN_WARNING "invalid nrpacks value.\n");
		return -EINVAL;
	}
	return usb_register(&usb_audio_driver);
}


static void __exit snd_usb_audio_cleanup(void)
{
	usb_deregister(&usb_audio_driver);
}

module_init(snd_usb_audio_init);
module_exit(snd_usb_audio_cleanup);
