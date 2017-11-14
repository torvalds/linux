/*
 * Edirol UA-101/UA-1000 driver
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 * This driver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "../usbaudio.h"
#include "../midi.h"

MODULE_DESCRIPTION("Edirol UA-101/1000 driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{Edirol,UA-101},{Edirol,UA-1000}}");

/*
 * Should not be lower than the minimum scheduling delay of the host
 * controller.  Some Intel controllers need more than one frame; as long as
 * that driver doesn't tell us about this, use 1.5 frames just to be sure.
 */
#define MIN_QUEUE_LENGTH	12
/* Somewhat random. */
#define MAX_QUEUE_LENGTH	30
/*
 * This magic value optimizes memory usage efficiency for the UA-101's packet
 * sizes at all sample rates, taking into account the stupid cache pool sizes
 * that usb_alloc_coherent() uses.
 */
#define DEFAULT_QUEUE_LENGTH	21

#define MAX_PACKET_SIZE		672 /* hardware specific */
#define MAX_MEMORY_BUFFERS	DIV_ROUND_UP(MAX_QUEUE_LENGTH, \
					     PAGE_SIZE / MAX_PACKET_SIZE)

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static unsigned int queue_length = 21;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable card");
module_param(queue_length, uint, 0644);
MODULE_PARM_DESC(queue_length, "USB queue length in microframes, "
		 __stringify(MIN_QUEUE_LENGTH)"-"__stringify(MAX_QUEUE_LENGTH));

enum {
	INTF_PLAYBACK,
	INTF_CAPTURE,
	INTF_MIDI,

	INTF_COUNT
};

/* bits in struct ua101::states */
enum {
	USB_CAPTURE_RUNNING,
	USB_PLAYBACK_RUNNING,
	ALSA_CAPTURE_OPEN,
	ALSA_PLAYBACK_OPEN,
	ALSA_CAPTURE_RUNNING,
	ALSA_PLAYBACK_RUNNING,
	CAPTURE_URB_COMPLETED,
	PLAYBACK_URB_COMPLETED,
	DISCONNECTED,
};

struct ua101 {
	struct usb_device *dev;
	struct snd_card *card;
	struct usb_interface *intf[INTF_COUNT];
	int card_index;
	struct snd_pcm *pcm;
	struct list_head midi_list;
	u64 format_bit;
	unsigned int rate;
	unsigned int packets_per_second;
	spinlock_t lock;
	struct mutex mutex;
	unsigned long states;

	/* FIFO to synchronize playback rate to capture rate */
	unsigned int rate_feedback_start;
	unsigned int rate_feedback_count;
	u8 rate_feedback[MAX_QUEUE_LENGTH];

	struct list_head ready_playback_urbs;
	struct tasklet_struct playback_tasklet;
	wait_queue_head_t alsa_capture_wait;
	wait_queue_head_t rate_feedback_wait;
	wait_queue_head_t alsa_playback_wait;
	struct ua101_stream {
		struct snd_pcm_substream *substream;
		unsigned int usb_pipe;
		unsigned int channels;
		unsigned int frame_bytes;
		unsigned int max_packet_bytes;
		unsigned int period_pos;
		unsigned int buffer_pos;
		unsigned int queue_length;
		struct ua101_urb {
			struct urb urb;
			struct usb_iso_packet_descriptor iso_frame_desc[1];
			struct list_head ready_list;
		} *urbs[MAX_QUEUE_LENGTH];
		struct {
			unsigned int size;
			void *addr;
			dma_addr_t dma;
		} buffers[MAX_MEMORY_BUFFERS];
	} capture, playback;
};

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;
static struct usb_driver ua101_driver;

static void abort_alsa_playback(struct ua101 *ua);
static void abort_alsa_capture(struct ua101 *ua);

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

static void abort_usb_capture(struct ua101 *ua)
{
	if (test_and_clear_bit(USB_CAPTURE_RUNNING, &ua->states)) {
		wake_up(&ua->alsa_capture_wait);
		wake_up(&ua->rate_feedback_wait);
	}
}

static void abort_usb_playback(struct ua101 *ua)
{
	if (test_and_clear_bit(USB_PLAYBACK_RUNNING, &ua->states))
		wake_up(&ua->alsa_playback_wait);
}

static void playback_urb_complete(struct urb *usb_urb)
{
	struct ua101_urb *urb = (struct ua101_urb *)usb_urb;
	struct ua101 *ua = urb->urb.context;
	unsigned long flags;

	if (unlikely(urb->urb.status == -ENOENT ||	/* unlinked */
		     urb->urb.status == -ENODEV ||	/* device removed */
		     urb->urb.status == -ECONNRESET ||	/* unlinked */
		     urb->urb.status == -ESHUTDOWN)) {	/* device disabled */
		abort_usb_playback(ua);
		abort_alsa_playback(ua);
		return;
	}

	if (test_bit(USB_PLAYBACK_RUNNING, &ua->states)) {
		/* append URB to FIFO */
		spin_lock_irqsave(&ua->lock, flags);
		list_add_tail(&urb->ready_list, &ua->ready_playback_urbs);
		if (ua->rate_feedback_count > 0)
			tasklet_schedule(&ua->playback_tasklet);
		ua->playback.substream->runtime->delay -=
				urb->urb.iso_frame_desc[0].length /
						ua->playback.frame_bytes;
		spin_unlock_irqrestore(&ua->lock, flags);
	}
}

static void first_playback_urb_complete(struct urb *urb)
{
	struct ua101 *ua = urb->context;

	urb->complete = playback_urb_complete;
	playback_urb_complete(urb);

	set_bit(PLAYBACK_URB_COMPLETED, &ua->states);
	wake_up(&ua->alsa_playback_wait);
}

/* copy data from the ALSA ring buffer into the URB buffer */
static bool copy_playback_data(struct ua101_stream *stream, struct urb *urb,
			       unsigned int frames)
{
	struct snd_pcm_runtime *runtime;
	unsigned int frame_bytes, frames1;
	const u8 *source;

	runtime = stream->substream->runtime;
	frame_bytes = stream->frame_bytes;
	source = runtime->dma_area + stream->buffer_pos * frame_bytes;
	if (stream->buffer_pos + frames <= runtime->buffer_size) {
		memcpy(urb->transfer_buffer, source, frames * frame_bytes);
	} else {
		/* wrap around at end of ring buffer */
		frames1 = runtime->buffer_size - stream->buffer_pos;
		memcpy(urb->transfer_buffer, source, frames1 * frame_bytes);
		memcpy(urb->transfer_buffer + frames1 * frame_bytes,
		       runtime->dma_area, (frames - frames1) * frame_bytes);
	}

	stream->buffer_pos += frames;
	if (stream->buffer_pos >= runtime->buffer_size)
		stream->buffer_pos -= runtime->buffer_size;
	stream->period_pos += frames;
	if (stream->period_pos >= runtime->period_size) {
		stream->period_pos -= runtime->period_size;
		return true;
	}
	return false;
}

static inline void add_with_wraparound(struct ua101 *ua,
				       unsigned int *value, unsigned int add)
{
	*value += add;
	if (*value >= ua->playback.queue_length)
		*value -= ua->playback.queue_length;
}

static void playback_tasklet(unsigned long data)
{
	struct ua101 *ua = (void *)data;
	unsigned long flags;
	unsigned int frames;
	struct ua101_urb *urb;
	bool do_period_elapsed = false;
	int err;

	if (unlikely(!test_bit(USB_PLAYBACK_RUNNING, &ua->states)))
		return;

	/*
	 * Synchronizing the playback rate to the capture rate is done by using
	 * the same sequence of packet sizes for both streams.
	 * Submitting a playback URB therefore requires both a ready URB and
	 * the size of the corresponding capture packet, i.e., both playback
	 * and capture URBs must have been completed.  Since the USB core does
	 * not guarantee that playback and capture complete callbacks are
	 * called alternately, we use two FIFOs for packet sizes and read URBs;
	 * submitting playback URBs is possible as long as both FIFOs are
	 * nonempty.
	 */
	spin_lock_irqsave(&ua->lock, flags);
	while (ua->rate_feedback_count > 0 &&
	       !list_empty(&ua->ready_playback_urbs)) {
		/* take packet size out of FIFO */
		frames = ua->rate_feedback[ua->rate_feedback_start];
		add_with_wraparound(ua, &ua->rate_feedback_start, 1);
		ua->rate_feedback_count--;

		/* take URB out of FIFO */
		urb = list_first_entry(&ua->ready_playback_urbs,
				       struct ua101_urb, ready_list);
		list_del(&urb->ready_list);

		/* fill packet with data or silence */
		urb->urb.iso_frame_desc[0].length =
			frames * ua->playback.frame_bytes;
		if (test_bit(ALSA_PLAYBACK_RUNNING, &ua->states))
			do_period_elapsed |= copy_playback_data(&ua->playback,
								&urb->urb,
								frames);
		else
			memset(urb->urb.transfer_buffer, 0,
			       urb->urb.iso_frame_desc[0].length);

		/* and off you go ... */
		err = usb_submit_urb(&urb->urb, GFP_ATOMIC);
		if (unlikely(err < 0)) {
			spin_unlock_irqrestore(&ua->lock, flags);
			abort_usb_playback(ua);
			abort_alsa_playback(ua);
			dev_err(&ua->dev->dev, "USB request error %d: %s\n",
				err, usb_error_string(err));
			return;
		}
		ua->playback.substream->runtime->delay += frames;
	}
	spin_unlock_irqrestore(&ua->lock, flags);
	if (do_period_elapsed)
		snd_pcm_period_elapsed(ua->playback.substream);
}

/* copy data from the URB buffer into the ALSA ring buffer */
static bool copy_capture_data(struct ua101_stream *stream, struct urb *urb,
			      unsigned int frames)
{
	struct snd_pcm_runtime *runtime;
	unsigned int frame_bytes, frames1;
	u8 *dest;

	runtime = stream->substream->runtime;
	frame_bytes = stream->frame_bytes;
	dest = runtime->dma_area + stream->buffer_pos * frame_bytes;
	if (stream->buffer_pos + frames <= runtime->buffer_size) {
		memcpy(dest, urb->transfer_buffer, frames * frame_bytes);
	} else {
		/* wrap around at end of ring buffer */
		frames1 = runtime->buffer_size - stream->buffer_pos;
		memcpy(dest, urb->transfer_buffer, frames1 * frame_bytes);
		memcpy(runtime->dma_area,
		       urb->transfer_buffer + frames1 * frame_bytes,
		       (frames - frames1) * frame_bytes);
	}

	stream->buffer_pos += frames;
	if (stream->buffer_pos >= runtime->buffer_size)
		stream->buffer_pos -= runtime->buffer_size;
	stream->period_pos += frames;
	if (stream->period_pos >= runtime->period_size) {
		stream->period_pos -= runtime->period_size;
		return true;
	}
	return false;
}

static void capture_urb_complete(struct urb *urb)
{
	struct ua101 *ua = urb->context;
	struct ua101_stream *stream = &ua->capture;
	unsigned long flags;
	unsigned int frames, write_ptr;
	bool do_period_elapsed;
	int err;

	if (unlikely(urb->status == -ENOENT ||		/* unlinked */
		     urb->status == -ENODEV ||		/* device removed */
		     urb->status == -ECONNRESET ||	/* unlinked */
		     urb->status == -ESHUTDOWN))	/* device disabled */
		goto stream_stopped;

	if (urb->status >= 0 && urb->iso_frame_desc[0].status >= 0)
		frames = urb->iso_frame_desc[0].actual_length /
			stream->frame_bytes;
	else
		frames = 0;

	spin_lock_irqsave(&ua->lock, flags);

	if (frames > 0 && test_bit(ALSA_CAPTURE_RUNNING, &ua->states))
		do_period_elapsed = copy_capture_data(stream, urb, frames);
	else
		do_period_elapsed = false;

	if (test_bit(USB_CAPTURE_RUNNING, &ua->states)) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (unlikely(err < 0)) {
			spin_unlock_irqrestore(&ua->lock, flags);
			dev_err(&ua->dev->dev, "USB request error %d: %s\n",
				err, usb_error_string(err));
			goto stream_stopped;
		}

		/* append packet size to FIFO */
		write_ptr = ua->rate_feedback_start;
		add_with_wraparound(ua, &write_ptr, ua->rate_feedback_count);
		ua->rate_feedback[write_ptr] = frames;
		if (ua->rate_feedback_count < ua->playback.queue_length) {
			ua->rate_feedback_count++;
			if (ua->rate_feedback_count ==
						ua->playback.queue_length)
				wake_up(&ua->rate_feedback_wait);
		} else {
			/*
			 * Ring buffer overflow; this happens when the playback
			 * stream is not running.  Throw away the oldest entry,
			 * so that the playback stream, when it starts, sees
			 * the most recent packet sizes.
			 */
			add_with_wraparound(ua, &ua->rate_feedback_start, 1);
		}
		if (test_bit(USB_PLAYBACK_RUNNING, &ua->states) &&
		    !list_empty(&ua->ready_playback_urbs))
			tasklet_schedule(&ua->playback_tasklet);
	}

	spin_unlock_irqrestore(&ua->lock, flags);

	if (do_period_elapsed)
		snd_pcm_period_elapsed(stream->substream);

	return;

stream_stopped:
	abort_usb_playback(ua);
	abort_usb_capture(ua);
	abort_alsa_playback(ua);
	abort_alsa_capture(ua);
}

static void first_capture_urb_complete(struct urb *urb)
{
	struct ua101 *ua = urb->context;

	urb->complete = capture_urb_complete;
	capture_urb_complete(urb);

	set_bit(CAPTURE_URB_COMPLETED, &ua->states);
	wake_up(&ua->alsa_capture_wait);
}

static int submit_stream_urbs(struct ua101 *ua, struct ua101_stream *stream)
{
	unsigned int i;

	for (i = 0; i < stream->queue_length; ++i) {
		int err = usb_submit_urb(&stream->urbs[i]->urb, GFP_KERNEL);
		if (err < 0) {
			dev_err(&ua->dev->dev, "USB request error %d: %s\n",
				err, usb_error_string(err));
			return err;
		}
	}
	return 0;
}

static void kill_stream_urbs(struct ua101_stream *stream)
{
	unsigned int i;

	for (i = 0; i < stream->queue_length; ++i)
		if (stream->urbs[i])
			usb_kill_urb(&stream->urbs[i]->urb);
}

static int enable_iso_interface(struct ua101 *ua, unsigned int intf_index)
{
	struct usb_host_interface *alts;

	alts = ua->intf[intf_index]->cur_altsetting;
	if (alts->desc.bAlternateSetting != 1) {
		int err = usb_set_interface(ua->dev,
					    alts->desc.bInterfaceNumber, 1);
		if (err < 0) {
			dev_err(&ua->dev->dev,
				"cannot initialize interface; error %d: %s\n",
				err, usb_error_string(err));
			return err;
		}
	}
	return 0;
}

static void disable_iso_interface(struct ua101 *ua, unsigned int intf_index)
{
	struct usb_host_interface *alts;

	if (!ua->intf[intf_index])
		return;

	alts = ua->intf[intf_index]->cur_altsetting;
	if (alts->desc.bAlternateSetting != 0) {
		int err = usb_set_interface(ua->dev,
					    alts->desc.bInterfaceNumber, 0);
		if (err < 0 && !test_bit(DISCONNECTED, &ua->states))
			dev_warn(&ua->dev->dev,
				 "interface reset failed; error %d: %s\n",
				 err, usb_error_string(err));
	}
}

static void stop_usb_capture(struct ua101 *ua)
{
	clear_bit(USB_CAPTURE_RUNNING, &ua->states);

	kill_stream_urbs(&ua->capture);

	disable_iso_interface(ua, INTF_CAPTURE);
}

static int start_usb_capture(struct ua101 *ua)
{
	int err;

	if (test_bit(DISCONNECTED, &ua->states))
		return -ENODEV;

	if (test_bit(USB_CAPTURE_RUNNING, &ua->states))
		return 0;

	kill_stream_urbs(&ua->capture);

	err = enable_iso_interface(ua, INTF_CAPTURE);
	if (err < 0)
		return err;

	clear_bit(CAPTURE_URB_COMPLETED, &ua->states);
	ua->capture.urbs[0]->urb.complete = first_capture_urb_complete;
	ua->rate_feedback_start = 0;
	ua->rate_feedback_count = 0;

	set_bit(USB_CAPTURE_RUNNING, &ua->states);
	err = submit_stream_urbs(ua, &ua->capture);
	if (err < 0)
		stop_usb_capture(ua);
	return err;
}

static void stop_usb_playback(struct ua101 *ua)
{
	clear_bit(USB_PLAYBACK_RUNNING, &ua->states);

	kill_stream_urbs(&ua->playback);

	tasklet_kill(&ua->playback_tasklet);

	disable_iso_interface(ua, INTF_PLAYBACK);
}

static int start_usb_playback(struct ua101 *ua)
{
	unsigned int i, frames;
	struct urb *urb;
	int err = 0;

	if (test_bit(DISCONNECTED, &ua->states))
		return -ENODEV;

	if (test_bit(USB_PLAYBACK_RUNNING, &ua->states))
		return 0;

	kill_stream_urbs(&ua->playback);
	tasklet_kill(&ua->playback_tasklet);

	err = enable_iso_interface(ua, INTF_PLAYBACK);
	if (err < 0)
		return err;

	clear_bit(PLAYBACK_URB_COMPLETED, &ua->states);
	ua->playback.urbs[0]->urb.complete =
		first_playback_urb_complete;
	spin_lock_irq(&ua->lock);
	INIT_LIST_HEAD(&ua->ready_playback_urbs);
	spin_unlock_irq(&ua->lock);

	/*
	 * We submit the initial URBs all at once, so we have to wait for the
	 * packet size FIFO to be full.
	 */
	wait_event(ua->rate_feedback_wait,
		   ua->rate_feedback_count >= ua->playback.queue_length ||
		   !test_bit(USB_CAPTURE_RUNNING, &ua->states) ||
		   test_bit(DISCONNECTED, &ua->states));
	if (test_bit(DISCONNECTED, &ua->states)) {
		stop_usb_playback(ua);
		return -ENODEV;
	}
	if (!test_bit(USB_CAPTURE_RUNNING, &ua->states)) {
		stop_usb_playback(ua);
		return -EIO;
	}

	for (i = 0; i < ua->playback.queue_length; ++i) {
		/* all initial URBs contain silence */
		spin_lock_irq(&ua->lock);
		frames = ua->rate_feedback[ua->rate_feedback_start];
		add_with_wraparound(ua, &ua->rate_feedback_start, 1);
		ua->rate_feedback_count--;
		spin_unlock_irq(&ua->lock);
		urb = &ua->playback.urbs[i]->urb;
		urb->iso_frame_desc[0].length =
			frames * ua->playback.frame_bytes;
		memset(urb->transfer_buffer, 0,
		       urb->iso_frame_desc[0].length);
	}

	set_bit(USB_PLAYBACK_RUNNING, &ua->states);
	err = submit_stream_urbs(ua, &ua->playback);
	if (err < 0)
		stop_usb_playback(ua);
	return err;
}

static void abort_alsa_capture(struct ua101 *ua)
{
	if (test_bit(ALSA_CAPTURE_RUNNING, &ua->states))
		snd_pcm_stop_xrun(ua->capture.substream);
}

static void abort_alsa_playback(struct ua101 *ua)
{
	if (test_bit(ALSA_PLAYBACK_RUNNING, &ua->states))
		snd_pcm_stop_xrun(ua->playback.substream);
}

static int set_stream_hw(struct ua101 *ua, struct snd_pcm_substream *substream,
			 unsigned int channels)
{
	int err;

	substream->runtime->hw.info =
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_FIFO_IN_FRAMES;
	substream->runtime->hw.formats = ua->format_bit;
	substream->runtime->hw.rates = snd_pcm_rate_to_rate_bit(ua->rate);
	substream->runtime->hw.rate_min = ua->rate;
	substream->runtime->hw.rate_max = ua->rate;
	substream->runtime->hw.channels_min = channels;
	substream->runtime->hw.channels_max = channels;
	substream->runtime->hw.buffer_bytes_max = 45000 * 1024;
	substream->runtime->hw.period_bytes_min = 1;
	substream->runtime->hw.period_bytes_max = UINT_MAX;
	substream->runtime->hw.periods_min = 2;
	substream->runtime->hw.periods_max = UINT_MAX;
	err = snd_pcm_hw_constraint_minmax(substream->runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   1500000 / ua->packets_per_second,
					   UINT_MAX);
	if (err < 0)
		return err;
	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	return err;
}

static int capture_pcm_open(struct snd_pcm_substream *substream)
{
	struct ua101 *ua = substream->private_data;
	int err;

	ua->capture.substream = substream;
	err = set_stream_hw(ua, substream, ua->capture.channels);
	if (err < 0)
		return err;
	substream->runtime->hw.fifo_size =
		DIV_ROUND_CLOSEST(ua->rate, ua->packets_per_second);
	substream->runtime->delay = substream->runtime->hw.fifo_size;

	mutex_lock(&ua->mutex);
	err = start_usb_capture(ua);
	if (err >= 0)
		set_bit(ALSA_CAPTURE_OPEN, &ua->states);
	mutex_unlock(&ua->mutex);
	return err;
}

static int playback_pcm_open(struct snd_pcm_substream *substream)
{
	struct ua101 *ua = substream->private_data;
	int err;

	ua->playback.substream = substream;
	err = set_stream_hw(ua, substream, ua->playback.channels);
	if (err < 0)
		return err;
	substream->runtime->hw.fifo_size =
		DIV_ROUND_CLOSEST(ua->rate * ua->playback.queue_length,
				  ua->packets_per_second);

	mutex_lock(&ua->mutex);
	err = start_usb_capture(ua);
	if (err < 0)
		goto error;
	err = start_usb_playback(ua);
	if (err < 0) {
		if (!test_bit(ALSA_CAPTURE_OPEN, &ua->states))
			stop_usb_capture(ua);
		goto error;
	}
	set_bit(ALSA_PLAYBACK_OPEN, &ua->states);
error:
	mutex_unlock(&ua->mutex);
	return err;
}

static int capture_pcm_close(struct snd_pcm_substream *substream)
{
	struct ua101 *ua = substream->private_data;

	mutex_lock(&ua->mutex);
	clear_bit(ALSA_CAPTURE_OPEN, &ua->states);
	if (!test_bit(ALSA_PLAYBACK_OPEN, &ua->states))
		stop_usb_capture(ua);
	mutex_unlock(&ua->mutex);
	return 0;
}

static int playback_pcm_close(struct snd_pcm_substream *substream)
{
	struct ua101 *ua = substream->private_data;

	mutex_lock(&ua->mutex);
	stop_usb_playback(ua);
	clear_bit(ALSA_PLAYBACK_OPEN, &ua->states);
	if (!test_bit(ALSA_CAPTURE_OPEN, &ua->states))
		stop_usb_capture(ua);
	mutex_unlock(&ua->mutex);
	return 0;
}

static int capture_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct ua101 *ua = substream->private_data;
	int err;

	mutex_lock(&ua->mutex);
	err = start_usb_capture(ua);
	mutex_unlock(&ua->mutex);
	if (err < 0)
		return err;

	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int playback_pcm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct ua101 *ua = substream->private_data;
	int err;

	mutex_lock(&ua->mutex);
	err = start_usb_capture(ua);
	if (err >= 0)
		err = start_usb_playback(ua);
	mutex_unlock(&ua->mutex);
	if (err < 0)
		return err;

	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int ua101_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int capture_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct ua101 *ua = substream->private_data;
	int err;

	mutex_lock(&ua->mutex);
	err = start_usb_capture(ua);
	mutex_unlock(&ua->mutex);
	if (err < 0)
		return err;

	/*
	 * The EHCI driver schedules the first packet of an iso stream at 10 ms
	 * in the future, i.e., no data is actually captured for that long.
	 * Take the wait here so that the stream is known to be actually
	 * running when the start trigger has been called.
	 */
	wait_event(ua->alsa_capture_wait,
		   test_bit(CAPTURE_URB_COMPLETED, &ua->states) ||
		   !test_bit(USB_CAPTURE_RUNNING, &ua->states));
	if (test_bit(DISCONNECTED, &ua->states))
		return -ENODEV;
	if (!test_bit(USB_CAPTURE_RUNNING, &ua->states))
		return -EIO;

	ua->capture.period_pos = 0;
	ua->capture.buffer_pos = 0;
	return 0;
}

static int playback_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct ua101 *ua = substream->private_data;
	int err;

	mutex_lock(&ua->mutex);
	err = start_usb_capture(ua);
	if (err >= 0)
		err = start_usb_playback(ua);
	mutex_unlock(&ua->mutex);
	if (err < 0)
		return err;

	/* see the comment in capture_pcm_prepare() */
	wait_event(ua->alsa_playback_wait,
		   test_bit(PLAYBACK_URB_COMPLETED, &ua->states) ||
		   !test_bit(USB_PLAYBACK_RUNNING, &ua->states));
	if (test_bit(DISCONNECTED, &ua->states))
		return -ENODEV;
	if (!test_bit(USB_PLAYBACK_RUNNING, &ua->states))
		return -EIO;

	substream->runtime->delay = 0;
	ua->playback.period_pos = 0;
	ua->playback.buffer_pos = 0;
	return 0;
}

static int capture_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct ua101 *ua = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!test_bit(USB_CAPTURE_RUNNING, &ua->states))
			return -EIO;
		set_bit(ALSA_CAPTURE_RUNNING, &ua->states);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		clear_bit(ALSA_CAPTURE_RUNNING, &ua->states);
		return 0;
	default:
		return -EINVAL;
	}
}

static int playback_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct ua101 *ua = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!test_bit(USB_PLAYBACK_RUNNING, &ua->states))
			return -EIO;
		set_bit(ALSA_PLAYBACK_RUNNING, &ua->states);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		clear_bit(ALSA_PLAYBACK_RUNNING, &ua->states);
		return 0;
	default:
		return -EINVAL;
	}
}

static inline snd_pcm_uframes_t ua101_pcm_pointer(struct ua101 *ua,
						  struct ua101_stream *stream)
{
	unsigned long flags;
	unsigned int pos;

	spin_lock_irqsave(&ua->lock, flags);
	pos = stream->buffer_pos;
	spin_unlock_irqrestore(&ua->lock, flags);
	return pos;
}

static snd_pcm_uframes_t capture_pcm_pointer(struct snd_pcm_substream *subs)
{
	struct ua101 *ua = subs->private_data;

	return ua101_pcm_pointer(ua, &ua->capture);
}

static snd_pcm_uframes_t playback_pcm_pointer(struct snd_pcm_substream *subs)
{
	struct ua101 *ua = subs->private_data;

	return ua101_pcm_pointer(ua, &ua->playback);
}

static const struct snd_pcm_ops capture_pcm_ops = {
	.open = capture_pcm_open,
	.close = capture_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = capture_pcm_hw_params,
	.hw_free = ua101_pcm_hw_free,
	.prepare = capture_pcm_prepare,
	.trigger = capture_pcm_trigger,
	.pointer = capture_pcm_pointer,
	.page = snd_pcm_lib_get_vmalloc_page,
	.mmap = snd_pcm_lib_mmap_vmalloc,
};

static const struct snd_pcm_ops playback_pcm_ops = {
	.open = playback_pcm_open,
	.close = playback_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = playback_pcm_hw_params,
	.hw_free = ua101_pcm_hw_free,
	.prepare = playback_pcm_prepare,
	.trigger = playback_pcm_trigger,
	.pointer = playback_pcm_pointer,
	.page = snd_pcm_lib_get_vmalloc_page,
	.mmap = snd_pcm_lib_mmap_vmalloc,
};

static const struct uac_format_type_i_discrete_descriptor *
find_format_descriptor(struct usb_interface *interface)
{
	struct usb_host_interface *alt;
	u8 *extra;
	int extralen;

	if (interface->num_altsetting != 2) {
		dev_err(&interface->dev, "invalid num_altsetting\n");
		return NULL;
	}

	alt = &interface->altsetting[0];
	if (alt->desc.bNumEndpoints != 0) {
		dev_err(&interface->dev, "invalid bNumEndpoints\n");
		return NULL;
	}

	alt = &interface->altsetting[1];
	if (alt->desc.bNumEndpoints != 1) {
		dev_err(&interface->dev, "invalid bNumEndpoints\n");
		return NULL;
	}

	extra = alt->extra;
	extralen = alt->extralen;
	while (extralen >= sizeof(struct usb_descriptor_header)) {
		struct uac_format_type_i_discrete_descriptor *desc;

		desc = (struct uac_format_type_i_discrete_descriptor *)extra;
		if (desc->bLength > extralen) {
			dev_err(&interface->dev, "descriptor overflow\n");
			return NULL;
		}
		if (desc->bLength == UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(1) &&
		    desc->bDescriptorType == USB_DT_CS_INTERFACE &&
		    desc->bDescriptorSubtype == UAC_FORMAT_TYPE) {
			if (desc->bFormatType != UAC_FORMAT_TYPE_I_PCM ||
			    desc->bSamFreqType != 1) {
				dev_err(&interface->dev,
					"invalid format type\n");
				return NULL;
			}
			return desc;
		}
		extralen -= desc->bLength;
		extra += desc->bLength;
	}
	dev_err(&interface->dev, "sample format descriptor not found\n");
	return NULL;
}

static int detect_usb_format(struct ua101 *ua)
{
	const struct uac_format_type_i_discrete_descriptor *fmt_capture;
	const struct uac_format_type_i_discrete_descriptor *fmt_playback;
	const struct usb_endpoint_descriptor *epd;
	unsigned int rate2;

	fmt_capture = find_format_descriptor(ua->intf[INTF_CAPTURE]);
	fmt_playback = find_format_descriptor(ua->intf[INTF_PLAYBACK]);
	if (!fmt_capture || !fmt_playback)
		return -ENXIO;

	switch (fmt_capture->bSubframeSize) {
	case 3:
		ua->format_bit = SNDRV_PCM_FMTBIT_S24_3LE;
		break;
	case 4:
		ua->format_bit = SNDRV_PCM_FMTBIT_S32_LE;
		break;
	default:
		dev_err(&ua->dev->dev, "sample width is not 24 or 32 bits\n");
		return -ENXIO;
	}
	if (fmt_capture->bSubframeSize != fmt_playback->bSubframeSize) {
		dev_err(&ua->dev->dev,
			"playback/capture sample widths do not match\n");
		return -ENXIO;
	}

	if (fmt_capture->bBitResolution != 24 ||
	    fmt_playback->bBitResolution != 24) {
		dev_err(&ua->dev->dev, "sample width is not 24 bits\n");
		return -ENXIO;
	}

	ua->rate = combine_triple(fmt_capture->tSamFreq[0]);
	rate2 = combine_triple(fmt_playback->tSamFreq[0]);
	if (ua->rate != rate2) {
		dev_err(&ua->dev->dev,
			"playback/capture rates do not match: %u/%u\n",
			rate2, ua->rate);
		return -ENXIO;
	}

	switch (ua->dev->speed) {
	case USB_SPEED_FULL:
		ua->packets_per_second = 1000;
		break;
	case USB_SPEED_HIGH:
		ua->packets_per_second = 8000;
		break;
	default:
		dev_err(&ua->dev->dev, "unknown device speed\n");
		return -ENXIO;
	}

	ua->capture.channels = fmt_capture->bNrChannels;
	ua->playback.channels = fmt_playback->bNrChannels;
	ua->capture.frame_bytes =
		fmt_capture->bSubframeSize * ua->capture.channels;
	ua->playback.frame_bytes =
		fmt_playback->bSubframeSize * ua->playback.channels;

	epd = &ua->intf[INTF_CAPTURE]->altsetting[1].endpoint[0].desc;
	if (!usb_endpoint_is_isoc_in(epd)) {
		dev_err(&ua->dev->dev, "invalid capture endpoint\n");
		return -ENXIO;
	}
	ua->capture.usb_pipe = usb_rcvisocpipe(ua->dev, usb_endpoint_num(epd));
	ua->capture.max_packet_bytes = usb_endpoint_maxp(epd);

	epd = &ua->intf[INTF_PLAYBACK]->altsetting[1].endpoint[0].desc;
	if (!usb_endpoint_is_isoc_out(epd)) {
		dev_err(&ua->dev->dev, "invalid playback endpoint\n");
		return -ENXIO;
	}
	ua->playback.usb_pipe = usb_sndisocpipe(ua->dev, usb_endpoint_num(epd));
	ua->playback.max_packet_bytes = usb_endpoint_maxp(epd);
	return 0;
}

static int alloc_stream_buffers(struct ua101 *ua, struct ua101_stream *stream)
{
	unsigned int remaining_packets, packets, packets_per_page, i;
	size_t size;

	stream->queue_length = queue_length;
	stream->queue_length = max(stream->queue_length,
				   (unsigned int)MIN_QUEUE_LENGTH);
	stream->queue_length = min(stream->queue_length,
				   (unsigned int)MAX_QUEUE_LENGTH);

	/*
	 * The cache pool sizes used by usb_alloc_coherent() (128, 512, 2048) are
	 * quite bad when used with the packet sizes of this device (e.g. 280,
	 * 520, 624).  Therefore, we allocate and subdivide entire pages, using
	 * a smaller buffer only for the last chunk.
	 */
	remaining_packets = stream->queue_length;
	packets_per_page = PAGE_SIZE / stream->max_packet_bytes;
	for (i = 0; i < ARRAY_SIZE(stream->buffers); ++i) {
		packets = min(remaining_packets, packets_per_page);
		size = packets * stream->max_packet_bytes;
		stream->buffers[i].addr =
			usb_alloc_coherent(ua->dev, size, GFP_KERNEL,
					   &stream->buffers[i].dma);
		if (!stream->buffers[i].addr)
			return -ENOMEM;
		stream->buffers[i].size = size;
		remaining_packets -= packets;
		if (!remaining_packets)
			break;
	}
	if (remaining_packets) {
		dev_err(&ua->dev->dev, "too many packets\n");
		return -ENXIO;
	}
	return 0;
}

static void free_stream_buffers(struct ua101 *ua, struct ua101_stream *stream)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(stream->buffers); ++i)
		usb_free_coherent(ua->dev,
				  stream->buffers[i].size,
				  stream->buffers[i].addr,
				  stream->buffers[i].dma);
}

static int alloc_stream_urbs(struct ua101 *ua, struct ua101_stream *stream,
			     void (*urb_complete)(struct urb *))
{
	unsigned max_packet_size = stream->max_packet_bytes;
	struct ua101_urb *urb;
	unsigned int b, u = 0;

	for (b = 0; b < ARRAY_SIZE(stream->buffers); ++b) {
		unsigned int size = stream->buffers[b].size;
		u8 *addr = stream->buffers[b].addr;
		dma_addr_t dma = stream->buffers[b].dma;

		while (size >= max_packet_size) {
			if (u >= stream->queue_length)
				goto bufsize_error;
			urb = kmalloc(sizeof(*urb), GFP_KERNEL);
			if (!urb)
				return -ENOMEM;
			usb_init_urb(&urb->urb);
			urb->urb.dev = ua->dev;
			urb->urb.pipe = stream->usb_pipe;
			urb->urb.transfer_flags = URB_NO_TRANSFER_DMA_MAP;
			urb->urb.transfer_buffer = addr;
			urb->urb.transfer_dma = dma;
			urb->urb.transfer_buffer_length = max_packet_size;
			urb->urb.number_of_packets = 1;
			urb->urb.interval = 1;
			urb->urb.context = ua;
			urb->urb.complete = urb_complete;
			urb->urb.iso_frame_desc[0].offset = 0;
			urb->urb.iso_frame_desc[0].length = max_packet_size;
			stream->urbs[u++] = urb;
			size -= max_packet_size;
			addr += max_packet_size;
			dma += max_packet_size;
		}
	}
	if (u == stream->queue_length)
		return 0;
bufsize_error:
	dev_err(&ua->dev->dev, "internal buffer size error\n");
	return -ENXIO;
}

static void free_stream_urbs(struct ua101_stream *stream)
{
	unsigned int i;

	for (i = 0; i < stream->queue_length; ++i) {
		kfree(stream->urbs[i]);
		stream->urbs[i] = NULL;
	}
}

static void free_usb_related_resources(struct ua101 *ua,
				       struct usb_interface *interface)
{
	unsigned int i;
	struct usb_interface *intf;

	mutex_lock(&ua->mutex);
	free_stream_urbs(&ua->capture);
	free_stream_urbs(&ua->playback);
	mutex_unlock(&ua->mutex);
	free_stream_buffers(ua, &ua->capture);
	free_stream_buffers(ua, &ua->playback);

	for (i = 0; i < ARRAY_SIZE(ua->intf); ++i) {
		mutex_lock(&ua->mutex);
		intf = ua->intf[i];
		ua->intf[i] = NULL;
		mutex_unlock(&ua->mutex);
		if (intf) {
			usb_set_intfdata(intf, NULL);
			if (intf != interface)
				usb_driver_release_interface(&ua101_driver,
							     intf);
		}
	}
}

static void ua101_card_free(struct snd_card *card)
{
	struct ua101 *ua = card->private_data;

	mutex_destroy(&ua->mutex);
}

static int ua101_probe(struct usb_interface *interface,
		       const struct usb_device_id *usb_id)
{
	static const struct snd_usb_midi_endpoint_info midi_ep = {
		.out_cables = 0x0001,
		.in_cables = 0x0001
	};
	static const struct snd_usb_audio_quirk midi_quirk = {
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = &midi_ep
	};
	static const int intf_numbers[2][3] = {
		{	/* UA-101 */
			[INTF_PLAYBACK] = 0,
			[INTF_CAPTURE] = 1,
			[INTF_MIDI] = 2,
		},
		{	/* UA-1000 */
			[INTF_CAPTURE] = 1,
			[INTF_PLAYBACK] = 2,
			[INTF_MIDI] = 3,
		},
	};
	struct snd_card *card;
	struct ua101 *ua;
	unsigned int card_index, i;
	int is_ua1000;
	const char *name;
	char usb_path[32];
	int err;

	is_ua1000 = usb_id->idProduct == 0x0044;

	if (interface->altsetting->desc.bInterfaceNumber !=
	    intf_numbers[is_ua1000][0])
		return -ENODEV;

	mutex_lock(&devices_mutex);

	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (enable[card_index] && !(devices_used & (1 << card_index)))
			break;
	if (card_index >= SNDRV_CARDS) {
		mutex_unlock(&devices_mutex);
		return -ENOENT;
	}
	err = snd_card_new(&interface->dev,
			   index[card_index], id[card_index], THIS_MODULE,
			   sizeof(*ua), &card);
	if (err < 0) {
		mutex_unlock(&devices_mutex);
		return err;
	}
	card->private_free = ua101_card_free;
	ua = card->private_data;
	ua->dev = interface_to_usbdev(interface);
	ua->card = card;
	ua->card_index = card_index;
	INIT_LIST_HEAD(&ua->midi_list);
	spin_lock_init(&ua->lock);
	mutex_init(&ua->mutex);
	INIT_LIST_HEAD(&ua->ready_playback_urbs);
	tasklet_init(&ua->playback_tasklet,
		     playback_tasklet, (unsigned long)ua);
	init_waitqueue_head(&ua->alsa_capture_wait);
	init_waitqueue_head(&ua->rate_feedback_wait);
	init_waitqueue_head(&ua->alsa_playback_wait);

	ua->intf[0] = interface;
	for (i = 1; i < ARRAY_SIZE(ua->intf); ++i) {
		ua->intf[i] = usb_ifnum_to_if(ua->dev,
					      intf_numbers[is_ua1000][i]);
		if (!ua->intf[i]) {
			dev_err(&ua->dev->dev, "interface %u not found\n",
				intf_numbers[is_ua1000][i]);
			err = -ENXIO;
			goto probe_error;
		}
		err = usb_driver_claim_interface(&ua101_driver,
						 ua->intf[i], ua);
		if (err < 0) {
			ua->intf[i] = NULL;
			err = -EBUSY;
			goto probe_error;
		}
	}

	err = detect_usb_format(ua);
	if (err < 0)
		goto probe_error;

	name = usb_id->idProduct == 0x0044 ? "UA-1000" : "UA-101";
	strcpy(card->driver, "UA-101");
	strcpy(card->shortname, name);
	usb_make_path(ua->dev, usb_path, sizeof(usb_path));
	snprintf(ua->card->longname, sizeof(ua->card->longname),
		 "EDIROL %s (serial %s), %u Hz at %s, %s speed", name,
		 ua->dev->serial ? ua->dev->serial : "?", ua->rate, usb_path,
		 ua->dev->speed == USB_SPEED_HIGH ? "high" : "full");

	err = alloc_stream_buffers(ua, &ua->capture);
	if (err < 0)
		goto probe_error;
	err = alloc_stream_buffers(ua, &ua->playback);
	if (err < 0)
		goto probe_error;

	err = alloc_stream_urbs(ua, &ua->capture, capture_urb_complete);
	if (err < 0)
		goto probe_error;
	err = alloc_stream_urbs(ua, &ua->playback, playback_urb_complete);
	if (err < 0)
		goto probe_error;

	err = snd_pcm_new(card, name, 0, 1, 1, &ua->pcm);
	if (err < 0)
		goto probe_error;
	ua->pcm->private_data = ua;
	strcpy(ua->pcm->name, name);
	snd_pcm_set_ops(ua->pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_pcm_ops);
	snd_pcm_set_ops(ua->pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_pcm_ops);

	err = snd_usbmidi_create(card, ua->intf[INTF_MIDI],
				 &ua->midi_list, &midi_quirk);
	if (err < 0)
		goto probe_error;

	err = snd_card_register(card);
	if (err < 0)
		goto probe_error;

	usb_set_intfdata(interface, ua);
	devices_used |= 1 << card_index;

	mutex_unlock(&devices_mutex);
	return 0;

probe_error:
	free_usb_related_resources(ua, interface);
	snd_card_free(card);
	mutex_unlock(&devices_mutex);
	return err;
}

static void ua101_disconnect(struct usb_interface *interface)
{
	struct ua101 *ua = usb_get_intfdata(interface);
	struct list_head *midi;

	if (!ua)
		return;

	mutex_lock(&devices_mutex);

	set_bit(DISCONNECTED, &ua->states);
	wake_up(&ua->rate_feedback_wait);

	/* make sure that userspace cannot create new requests */
	snd_card_disconnect(ua->card);

	/* make sure that there are no pending USB requests */
	list_for_each(midi, &ua->midi_list)
		snd_usbmidi_disconnect(midi);
	abort_alsa_playback(ua);
	abort_alsa_capture(ua);
	mutex_lock(&ua->mutex);
	stop_usb_playback(ua);
	stop_usb_capture(ua);
	mutex_unlock(&ua->mutex);

	free_usb_related_resources(ua, interface);

	devices_used &= ~(1 << ua->card_index);

	snd_card_free_when_closed(ua->card);

	mutex_unlock(&devices_mutex);
}

static const struct usb_device_id ua101_ids[] = {
	{ USB_DEVICE(0x0582, 0x0044) }, /* UA-1000 high speed */
	{ USB_DEVICE(0x0582, 0x007d) }, /* UA-101 high speed */
	{ USB_DEVICE(0x0582, 0x008d) }, /* UA-101 full speed */
	{ }
};
MODULE_DEVICE_TABLE(usb, ua101_ids);

static struct usb_driver ua101_driver = {
	.name = "snd-ua101",
	.id_table = ua101_ids,
	.probe = ua101_probe,
	.disconnect = ua101_disconnect,
#if 0
	.suspend = ua101_suspend,
	.resume = ua101_resume,
#endif
};

module_usb_driver(ua101_driver);
