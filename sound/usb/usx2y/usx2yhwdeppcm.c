// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

/* USX2Y "rawusb" aka hwdep_pcm implementation

 Its usb's unableness to atomically handle power of 2 period sized data chuncs
 at standard samplerates,
 what led to this part of the usx2y module:
 It provides the alsa kernel half of the usx2y-alsa-jack driver pair.
 The pair uses a hardware dependent alsa-device for mmaped pcm transport.
 Advantage achieved:
         The usb_hc moves pcm data from/into memory via DMA.
         That memory is mmaped by jack's usx2y driver.
         Jack's usx2y driver is the first/last to read/write pcm data.
         Read/write is a combination of power of 2 period shaping and
         float/int conversation.
         Compared to mainline alsa/jack we leave out power of 2 period shaping inside
         snd-usb-usx2y which needs memcpy() and additional buffers.
         As a side effect possible unwanted pcm-data coruption resulting of
         standard alsa's snd-usb-usx2y period shaping scheme falls away.
         Result is sane jack operation at buffering schemes down to 128frames,
         2 periods.
         plain usx2y alsa mode is able to achieve 64frames, 4periods, but only at the
         cost of easier triggered i.e. aeolus xruns (128 or 256frames,
         2periods works but is useless cause of crackling).

 This is a first "proof of concept" implementation.
 Later, functionalities should migrate to more appropriate places:
 Userland:
 - The jackd could mmap its float-pcm buffers directly from alsa-lib.
 - alsa-lib could provide power of 2 period sized shaping combined with int/float
   conversation.
   Currently the usx2y jack driver provides above 2 services.
 Kernel:
 - rawusb dma pcm buffer transport should go to snd-usb-lib, so also snd-usb-audio
   devices can use it.
   Currently rawusb dma pcm buffer transport (this file) is only available to snd-usb-usx2y.
*/

#include <linux/delay.h>
#include <linux/gfp.h>
#include "usbusx2yaudio.c"

#if defined(USX2Y_NRPACKS_VARIABLE) || USX2Y_NRPACKS == 1

#include <sound/hwdep.h>

static int usx2y_usbpcm_urb_capt_retire(struct snd_usx2y_substream *subs)
{
	struct urb	*urb = subs->completed_urb;
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	int		i, lens = 0, hwptr_done = subs->hwptr_done;
	struct usx2ydev	*usx2y = subs->usx2y;
	int head;

	if (usx2y->hwdep_pcm_shm->capture_iso_start < 0) { //FIXME
		head = usx2y->hwdep_pcm_shm->captured_iso_head + 1;
		if (head >= ARRAY_SIZE(usx2y->hwdep_pcm_shm->captured_iso))
			head = 0;
		usx2y->hwdep_pcm_shm->capture_iso_start = head;
		snd_printdd("cap start %i\n", head);
	}
	for (i = 0; i < nr_of_packs(); i++) {
		if (urb->iso_frame_desc[i].status) { /* active? hmm, skip this */
			snd_printk(KERN_ERR
				   "active frame status %i. Most probably some hardware problem.\n",
				   urb->iso_frame_desc[i].status);
			return urb->iso_frame_desc[i].status;
		}
		lens += urb->iso_frame_desc[i].actual_length / usx2y->stride;
	}
	hwptr_done += lens;
	if (hwptr_done >= runtime->buffer_size)
		hwptr_done -= runtime->buffer_size;
	subs->hwptr_done = hwptr_done;
	subs->transfer_done += lens;
	/* update the pointer, call callback if necessary */
	if (subs->transfer_done >= runtime->period_size) {
		subs->transfer_done -= runtime->period_size;
		snd_pcm_period_elapsed(subs->pcm_substream);
	}
	return 0;
}

static int usx2y_iso_frames_per_buffer(struct snd_pcm_runtime *runtime,
					      struct usx2ydev *usx2y)
{
	return (runtime->buffer_size * 1000) / usx2y->rate + 1;	//FIXME: so far only correct period_size == 2^x ?
}

/*
 * prepare urb for playback data pipe
 *
 * we copy the data directly from the pcm buffer.
 * the current position to be copied is held in hwptr field.
 * since a urb can handle only a single linear buffer, if the total
 * transferred area overflows the buffer boundary, we cannot send
 * it directly from the buffer.  thus the data is once copied to
 * a temporary buffer and urb points to that.
 */
static int usx2y_hwdep_urb_play_prepare(struct snd_usx2y_substream *subs,
					struct urb *urb)
{
	int count, counts, pack;
	struct usx2ydev *usx2y = subs->usx2y;
	struct snd_usx2y_hwdep_pcm_shm *shm = usx2y->hwdep_pcm_shm;
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;

	if (shm->playback_iso_start < 0) {
		shm->playback_iso_start = shm->captured_iso_head -
			usx2y_iso_frames_per_buffer(runtime, usx2y);
		if (shm->playback_iso_start < 0)
			shm->playback_iso_start += ARRAY_SIZE(shm->captured_iso);
		shm->playback_iso_head = shm->playback_iso_start;
	}

	count = 0;
	for (pack = 0; pack < nr_of_packs(); pack++) {
		/* calculate the size of a packet */
		counts = shm->captured_iso[shm->playback_iso_head].length / usx2y->stride;
		if (counts < 43 || counts > 50) {
			snd_printk(KERN_ERR "should not be here with counts=%i\n", counts);
			return -EPIPE;
		}
		/* set up descriptor */
		urb->iso_frame_desc[pack].offset = shm->captured_iso[shm->playback_iso_head].offset;
		urb->iso_frame_desc[pack].length = shm->captured_iso[shm->playback_iso_head].length;
		if (atomic_read(&subs->state) != STATE_RUNNING)
			memset((char *)urb->transfer_buffer + urb->iso_frame_desc[pack].offset, 0,
			       urb->iso_frame_desc[pack].length);
		if (++shm->playback_iso_head >= ARRAY_SIZE(shm->captured_iso))
			shm->playback_iso_head = 0;
		count += counts;
	}
	urb->transfer_buffer_length = count * usx2y->stride;
	return 0;
}

static void usx2y_usbpcm_urb_capt_iso_advance(struct snd_usx2y_substream *subs,
					      struct urb *urb)
{
	struct usb_iso_packet_descriptor *desc;
	struct snd_usx2y_hwdep_pcm_shm *shm;
	int pack, head;

	for (pack = 0; pack < nr_of_packs(); ++pack) {
		desc = urb->iso_frame_desc + pack;
		if (subs) {
			shm = subs->usx2y->hwdep_pcm_shm;
			head = shm->captured_iso_head + 1;
			if (head >= ARRAY_SIZE(shm->captured_iso))
				head = 0;
			shm->captured_iso[head].frame = urb->start_frame + pack;
			shm->captured_iso[head].offset = desc->offset;
			shm->captured_iso[head].length = desc->actual_length;
			shm->captured_iso_head = head;
			shm->captured_iso_frames++;
		}
		desc->offset += desc->length * NRURBS * nr_of_packs();
		if (desc->offset + desc->length >= SSS)
			desc->offset -= (SSS - desc->length);
	}
}

static int usx2y_usbpcm_usbframe_complete(struct snd_usx2y_substream *capsubs,
					  struct snd_usx2y_substream *capsubs2,
					  struct snd_usx2y_substream *playbacksubs,
					  int frame)
{
	int err, state;
	struct urb *urb = playbacksubs->completed_urb;

	state = atomic_read(&playbacksubs->state);
	if (urb) {
		if (state == STATE_RUNNING)
			usx2y_urb_play_retire(playbacksubs, urb);
		else if (state >= STATE_PRERUNNING)
			atomic_inc(&playbacksubs->state);
	} else {
		switch (state) {
		case STATE_STARTING1:
			urb = playbacksubs->urb[0];
			atomic_inc(&playbacksubs->state);
			break;
		case STATE_STARTING2:
			urb = playbacksubs->urb[1];
			atomic_inc(&playbacksubs->state);
			break;
		}
	}
	if (urb) {
		err = usx2y_hwdep_urb_play_prepare(playbacksubs, urb);
		if (err)
			return err;
		err = usx2y_hwdep_urb_play_prepare(playbacksubs, urb);
		if (err)
			return err;
	}

	playbacksubs->completed_urb = NULL;

	state = atomic_read(&capsubs->state);
	if (state >= STATE_PREPARED) {
		if (state == STATE_RUNNING) {
			err = usx2y_usbpcm_urb_capt_retire(capsubs);
			if (err)
				return err;
		} else if (state >= STATE_PRERUNNING) {
			atomic_inc(&capsubs->state);
		}
		usx2y_usbpcm_urb_capt_iso_advance(capsubs, capsubs->completed_urb);
		if (capsubs2)
			usx2y_usbpcm_urb_capt_iso_advance(NULL, capsubs2->completed_urb);
		err = usx2y_urb_submit(capsubs, capsubs->completed_urb, frame);
		if (err)
			return err;
		if (capsubs2) {
			err = usx2y_urb_submit(capsubs2, capsubs2->completed_urb, frame);
			if (err)
				return err;
		}
	}
	capsubs->completed_urb = NULL;
	if (capsubs2)
		capsubs2->completed_urb = NULL;
	return 0;
}

static void i_usx2y_usbpcm_urb_complete(struct urb *urb)
{
	struct snd_usx2y_substream *subs = urb->context;
	struct usx2ydev *usx2y = subs->usx2y;
	struct snd_usx2y_substream *capsubs, *capsubs2, *playbacksubs;

	if (unlikely(atomic_read(&subs->state) < STATE_PREPARED)) {
		snd_printdd("hcd_frame=%i ep=%i%s status=%i start_frame=%i\n",
			    usb_get_current_frame_number(usx2y->dev),
			    subs->endpoint, usb_pipein(urb->pipe) ? "in" : "out",
			    urb->status, urb->start_frame);
		return;
	}
	if (unlikely(urb->status)) {
		usx2y_error_urb_status(usx2y, subs, urb);
		return;
	}

	subs->completed_urb = urb;
	capsubs = usx2y->subs[SNDRV_PCM_STREAM_CAPTURE];
	capsubs2 = usx2y->subs[SNDRV_PCM_STREAM_CAPTURE + 2];
	playbacksubs = usx2y->subs[SNDRV_PCM_STREAM_PLAYBACK];
	if (capsubs->completed_urb && atomic_read(&capsubs->state) >= STATE_PREPARED &&
	    (!capsubs2 || capsubs2->completed_urb) &&
	    (playbacksubs->completed_urb || atomic_read(&playbacksubs->state) < STATE_PREPARED)) {
		if (!usx2y_usbpcm_usbframe_complete(capsubs, capsubs2, playbacksubs, urb->start_frame)) {
			usx2y->wait_iso_frame += nr_of_packs();
		} else {
			snd_printdd("\n");
			usx2y_clients_stop(usx2y);
		}
	}
}

static void usx2y_hwdep_urb_release(struct urb **urb)
{
	usb_kill_urb(*urb);
	usb_free_urb(*urb);
	*urb = NULL;
}

/*
 * release a substream
 */
static void usx2y_usbpcm_urbs_release(struct snd_usx2y_substream *subs)
{
	int i;

	snd_printdd("snd_usx2y_urbs_release() %i\n", subs->endpoint);
	for (i = 0; i < NRURBS; i++)
		usx2y_hwdep_urb_release(subs->urb + i);
}

static void usx2y_usbpcm_subs_startup_finish(struct usx2ydev *usx2y)
{
	usx2y_urbs_set_complete(usx2y, i_usx2y_usbpcm_urb_complete);
	usx2y->prepare_subs = NULL;
}

static void i_usx2y_usbpcm_subs_startup(struct urb *urb)
{
	struct snd_usx2y_substream *subs = urb->context;
	struct usx2ydev *usx2y = subs->usx2y;
	struct snd_usx2y_substream *prepare_subs = usx2y->prepare_subs;
	struct snd_usx2y_substream *cap_subs2;

	if (prepare_subs &&
	    urb->start_frame == prepare_subs->urb[0]->start_frame) {
		atomic_inc(&prepare_subs->state);
		if (prepare_subs == usx2y->subs[SNDRV_PCM_STREAM_CAPTURE]) {
			cap_subs2 = usx2y->subs[SNDRV_PCM_STREAM_CAPTURE + 2];
			if (cap_subs2)
				atomic_inc(&cap_subs2->state);
		}
		usx2y_usbpcm_subs_startup_finish(usx2y);
		wake_up(&usx2y->prepare_wait_queue);
	}

	i_usx2y_usbpcm_urb_complete(urb);
}

/*
 * initialize a substream's urbs
 */
static int usx2y_usbpcm_urbs_allocate(struct snd_usx2y_substream *subs)
{
	int i;
	unsigned int pipe;
	int is_playback = subs == subs->usx2y->subs[SNDRV_PCM_STREAM_PLAYBACK];
	struct usb_device *dev = subs->usx2y->dev;
	struct urb **purb;

	pipe = is_playback ? usb_sndisocpipe(dev, subs->endpoint) :
			usb_rcvisocpipe(dev, subs->endpoint);
	subs->maxpacksize = usb_maxpacket(dev, pipe, is_playback);
	if (!subs->maxpacksize)
		return -EINVAL;

	/* allocate and initialize data urbs */
	for (i = 0; i < NRURBS; i++) {
		purb = subs->urb + i;
		if (*purb) {
			usb_kill_urb(*purb);
			continue;
		}
		*purb = usb_alloc_urb(nr_of_packs(), GFP_KERNEL);
		if (!*purb) {
			usx2y_usbpcm_urbs_release(subs);
			return -ENOMEM;
		}
		(*purb)->transfer_buffer = is_playback ?
			subs->usx2y->hwdep_pcm_shm->playback : (
				subs->endpoint == 0x8 ?
				subs->usx2y->hwdep_pcm_shm->capture0x8 :
				subs->usx2y->hwdep_pcm_shm->capture0xA);

		(*purb)->dev = dev;
		(*purb)->pipe = pipe;
		(*purb)->number_of_packets = nr_of_packs();
		(*purb)->context = subs;
		(*purb)->interval = 1;
		(*purb)->complete = i_usx2y_usbpcm_subs_startup;
	}
	return 0;
}

/*
 * free the buffer
 */
static int snd_usx2y_usbpcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usx2y_substream *subs = runtime->private_data;
	struct snd_usx2y_substream *cap_subs;
	struct snd_usx2y_substream *playback_subs;
	struct snd_usx2y_substream *cap_subs2;

	mutex_lock(&subs->usx2y->pcm_mutex);
	snd_printdd("%s(%p)\n", __func__, substream);

	cap_subs2 = subs->usx2y->subs[SNDRV_PCM_STREAM_CAPTURE + 2];
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cap_subs = subs->usx2y->subs[SNDRV_PCM_STREAM_CAPTURE];
		atomic_set(&subs->state, STATE_STOPPED);
		usx2y_usbpcm_urbs_release(subs);
		if (!cap_subs->pcm_substream ||
		    !cap_subs->pcm_substream->runtime ||
		    !cap_subs->pcm_substream->runtime->status ||
		    cap_subs->pcm_substream->runtime->status->state < SNDRV_PCM_STATE_PREPARED) {
			atomic_set(&cap_subs->state, STATE_STOPPED);
			if (cap_subs2)
				atomic_set(&cap_subs2->state, STATE_STOPPED);
			usx2y_usbpcm_urbs_release(cap_subs);
			if (cap_subs2)
				usx2y_usbpcm_urbs_release(cap_subs2);
		}
	} else {
		playback_subs = subs->usx2y->subs[SNDRV_PCM_STREAM_PLAYBACK];
		if (atomic_read(&playback_subs->state) < STATE_PREPARED) {
			atomic_set(&subs->state, STATE_STOPPED);
			if (cap_subs2)
				atomic_set(&cap_subs2->state, STATE_STOPPED);
			usx2y_usbpcm_urbs_release(subs);
			if (cap_subs2)
				usx2y_usbpcm_urbs_release(cap_subs2);
		}
	}
	mutex_unlock(&subs->usx2y->pcm_mutex);
	return 0;
}

static void usx2y_usbpcm_subs_startup(struct snd_usx2y_substream *subs)
{
	struct usx2ydev *usx2y = subs->usx2y;

	usx2y->prepare_subs = subs;
	subs->urb[0]->start_frame = -1;
	smp_wmb();	// Make sure above modifications are seen by i_usx2y_subs_startup()
	usx2y_urbs_set_complete(usx2y, i_usx2y_usbpcm_subs_startup);
}

static int usx2y_usbpcm_urbs_start(struct snd_usx2y_substream *subs)
{
	int	p, u, err, stream = subs->pcm_substream->stream;
	struct usx2ydev *usx2y = subs->usx2y;
	struct urb *urb;
	unsigned long pack;

	if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		usx2y->hwdep_pcm_shm->captured_iso_head = -1;
		usx2y->hwdep_pcm_shm->captured_iso_frames = 0;
	}

	for (p = 0; 3 >= (stream + p); p += 2) {
		struct snd_usx2y_substream *subs = usx2y->subs[stream + p];
		if (subs) {
			err = usx2y_usbpcm_urbs_allocate(subs);
			if (err < 0)
				return err;
			subs->completed_urb = NULL;
		}
	}

	for (p = 0; p < 4; p++) {
		struct snd_usx2y_substream *subs = usx2y->subs[p];

		if (subs && atomic_read(&subs->state) >= STATE_PREPARED)
			goto start;
	}

 start:
	usx2y_usbpcm_subs_startup(subs);
	for (u = 0; u < NRURBS; u++) {
		for (p = 0; 3 >= (stream + p); p += 2) {
			struct snd_usx2y_substream *subs = usx2y->subs[stream + p];

			if (!subs)
				continue;
			urb = subs->urb[u];
			if (usb_pipein(urb->pipe)) {
				if (!u)
					atomic_set(&subs->state, STATE_STARTING3);
				urb->dev = usx2y->dev;
				for (pack = 0; pack < nr_of_packs(); pack++) {
					urb->iso_frame_desc[pack].offset = subs->maxpacksize * (pack + u * nr_of_packs());
					urb->iso_frame_desc[pack].length = subs->maxpacksize;
				}
				urb->transfer_buffer_length = subs->maxpacksize * nr_of_packs();
				err = usb_submit_urb(urb, GFP_KERNEL);
				if (err < 0) {
					snd_printk(KERN_ERR "cannot usb_submit_urb() for urb %d, err = %d\n", u, err);
					err = -EPIPE;
					goto cleanup;
				}  else {
					snd_printdd("%i\n", urb->start_frame);
					if (!u)
						usx2y->wait_iso_frame = urb->start_frame;
				}
				urb->transfer_flags = 0;
			} else {
				atomic_set(&subs->state, STATE_STARTING1);
				break;
			}
		}
	}
	err = 0;
	wait_event(usx2y->prepare_wait_queue, !usx2y->prepare_subs);
	if (atomic_read(&subs->state) != STATE_PREPARED)
		err = -EPIPE;

 cleanup:
	if (err) {
		usx2y_subs_startup_finish(usx2y);	// Call it now
		usx2y_clients_stop(usx2y);		// something is completely wroong > stop evrything
	}
	return err;
}

#define USX2Y_HWDEP_PCM_PAGES	\
	PAGE_ALIGN(sizeof(struct snd_usx2y_hwdep_pcm_shm))

/*
 * prepare callback
 *
 * set format and initialize urbs
 */
static int snd_usx2y_usbpcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usx2y_substream *subs = runtime->private_data;
	struct usx2ydev *usx2y = subs->usx2y;
	struct snd_usx2y_substream *capsubs = subs->usx2y->subs[SNDRV_PCM_STREAM_CAPTURE];
	int err = 0;

	snd_printdd("snd_usx2y_pcm_prepare(%p)\n", substream);

	mutex_lock(&usx2y->pcm_mutex);

	if (!usx2y->hwdep_pcm_shm) {
		usx2y->hwdep_pcm_shm = alloc_pages_exact(USX2Y_HWDEP_PCM_PAGES,
							 GFP_KERNEL);
		if (!usx2y->hwdep_pcm_shm) {
			err = -ENOMEM;
			goto up_prepare_mutex;
		}
		memset(usx2y->hwdep_pcm_shm, 0, USX2Y_HWDEP_PCM_PAGES);
	}

	usx2y_subs_prepare(subs);
	// Start hardware streams
	// SyncStream first....
	if (atomic_read(&capsubs->state) < STATE_PREPARED) {
		if (usx2y->format != runtime->format) {
			err = usx2y_format_set(usx2y, runtime->format);
			if (err < 0)
				goto up_prepare_mutex;
		}
		if (usx2y->rate != runtime->rate) {
			err = usx2y_rate_set(usx2y, runtime->rate);
			if (err < 0)
				goto up_prepare_mutex;
		}
		snd_printdd("starting capture pipe for %s\n", subs == capsubs ?
			    "self" : "playpipe");
		err = usx2y_usbpcm_urbs_start(capsubs);
		if (err < 0)
			goto up_prepare_mutex;
	}

	if (subs != capsubs) {
		usx2y->hwdep_pcm_shm->playback_iso_start = -1;
		if (atomic_read(&subs->state) < STATE_PREPARED) {
			while (usx2y_iso_frames_per_buffer(runtime, usx2y) >
			       usx2y->hwdep_pcm_shm->captured_iso_frames) {
				snd_printdd("Wait: iso_frames_per_buffer=%i,captured_iso_frames=%i\n",
					    usx2y_iso_frames_per_buffer(runtime, usx2y),
					    usx2y->hwdep_pcm_shm->captured_iso_frames);
				if (msleep_interruptible(10)) {
					err = -ERESTARTSYS;
					goto up_prepare_mutex;
				}
			}
			err = usx2y_usbpcm_urbs_start(subs);
			if (err < 0)
				goto up_prepare_mutex;
		}
		snd_printdd("Ready: iso_frames_per_buffer=%i,captured_iso_frames=%i\n",
			    usx2y_iso_frames_per_buffer(runtime, usx2y),
			    usx2y->hwdep_pcm_shm->captured_iso_frames);
	} else {
		usx2y->hwdep_pcm_shm->capture_iso_start = -1;
	}

 up_prepare_mutex:
	mutex_unlock(&usx2y->pcm_mutex);
	return err;
}

static const struct snd_pcm_hardware snd_usx2y_4c = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =                 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_3LE,
	.rates =                   SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min =                44100,
	.rate_max =                48000,
	.channels_min =            2,
	.channels_max =            4,
	.buffer_bytes_max =	(2*128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =              0
};

static int snd_usx2y_usbpcm_open(struct snd_pcm_substream *substream)
{
	struct snd_usx2y_substream	*subs =
		((struct snd_usx2y_substream **)
		 snd_pcm_substream_chip(substream))[substream->stream];
	struct snd_pcm_runtime	*runtime = substream->runtime;

	if (!(subs->usx2y->chip_status & USX2Y_STAT_CHIP_MMAP_PCM_URBS))
		return -EBUSY;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = snd_usx2y_2c;
	else
		runtime->hw = (subs->usx2y->subs[3] ? snd_usx2y_4c : snd_usx2y_2c);
	runtime->private_data = subs;
	subs->pcm_substream = substream;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 1000, 200000);
	return 0;
}

static int snd_usx2y_usbpcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usx2y_substream *subs = runtime->private_data;

	subs->pcm_substream = NULL;
	return 0;
}

static const struct snd_pcm_ops snd_usx2y_usbpcm_ops = {
	.open =		snd_usx2y_usbpcm_open,
	.close =	snd_usx2y_usbpcm_close,
	.hw_params =	snd_usx2y_pcm_hw_params,
	.hw_free =	snd_usx2y_usbpcm_hw_free,
	.prepare =	snd_usx2y_usbpcm_prepare,
	.trigger =	snd_usx2y_pcm_trigger,
	.pointer =	snd_usx2y_pcm_pointer,
};

static int usx2y_pcms_busy_check(struct snd_card *card)
{
	struct usx2ydev	*dev = usx2y(card);
	struct snd_usx2y_substream *subs;
	int i;

	for (i = 0; i < dev->pcm_devs * 2; i++) {
		subs = dev->subs[i];
		if (subs && subs->pcm_substream &&
		    SUBSTREAM_BUSY(subs->pcm_substream))
			return -EBUSY;
	}
	return 0;
}

static int snd_usx2y_hwdep_pcm_open(struct snd_hwdep *hw, struct file *file)
{
	struct snd_card *card = hw->card;
	int err;

	mutex_lock(&usx2y(card)->pcm_mutex);
	err = usx2y_pcms_busy_check(card);
	if (!err)
		usx2y(card)->chip_status |= USX2Y_STAT_CHIP_MMAP_PCM_URBS;
	mutex_unlock(&usx2y(card)->pcm_mutex);
	return err;
}

static int snd_usx2y_hwdep_pcm_release(struct snd_hwdep *hw, struct file *file)
{
	struct snd_card *card = hw->card;
	int err;

	mutex_lock(&usx2y(card)->pcm_mutex);
	err = usx2y_pcms_busy_check(card);
	if (!err)
		usx2y(hw->card)->chip_status &= ~USX2Y_STAT_CHIP_MMAP_PCM_URBS;
	mutex_unlock(&usx2y(card)->pcm_mutex);
	return err;
}

static void snd_usx2y_hwdep_pcm_vm_open(struct vm_area_struct *area)
{
}

static void snd_usx2y_hwdep_pcm_vm_close(struct vm_area_struct *area)
{
}

static vm_fault_t snd_usx2y_hwdep_pcm_vm_fault(struct vm_fault *vmf)
{
	unsigned long offset;
	void *vaddr;

	offset = vmf->pgoff << PAGE_SHIFT;
	vaddr = (char *)((struct usx2ydev *)vmf->vma->vm_private_data)->hwdep_pcm_shm + offset;
	vmf->page = virt_to_page(vaddr);
	get_page(vmf->page);
	return 0;
}

static const struct vm_operations_struct snd_usx2y_hwdep_pcm_vm_ops = {
	.open = snd_usx2y_hwdep_pcm_vm_open,
	.close = snd_usx2y_hwdep_pcm_vm_close,
	.fault = snd_usx2y_hwdep_pcm_vm_fault,
};

static int snd_usx2y_hwdep_pcm_mmap(struct snd_hwdep *hw, struct file *filp, struct vm_area_struct *area)
{
	unsigned long	size = (unsigned long)(area->vm_end - area->vm_start);
	struct usx2ydev	*usx2y = hw->private_data;

	if (!(usx2y->chip_status & USX2Y_STAT_CHIP_INIT))
		return -EBUSY;

	/* if userspace tries to mmap beyond end of our buffer, fail */
	if (size > USX2Y_HWDEP_PCM_PAGES) {
		snd_printd("%lu > %lu\n", size, (unsigned long)USX2Y_HWDEP_PCM_PAGES);
		return -EINVAL;
	}

	if (!usx2y->hwdep_pcm_shm)
		return -ENODEV;

	area->vm_ops = &snd_usx2y_hwdep_pcm_vm_ops;
	area->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	area->vm_private_data = hw->private_data;
	return 0;
}

static void snd_usx2y_hwdep_pcm_private_free(struct snd_hwdep *hwdep)
{
	struct usx2ydev *usx2y = hwdep->private_data;

	if (usx2y->hwdep_pcm_shm)
		free_pages_exact(usx2y->hwdep_pcm_shm, USX2Y_HWDEP_PCM_PAGES);
}

int usx2y_hwdep_pcm_new(struct snd_card *card)
{
	int err;
	struct snd_hwdep *hw;
	struct snd_pcm *pcm;
	struct usb_device *dev = usx2y(card)->dev;

	if (nr_of_packs() != 1)
		return 0;

	err = snd_hwdep_new(card, SND_USX2Y_USBPCM_ID, 1, &hw);
	if (err < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_USX2Y_PCM;
	hw->private_data = usx2y(card);
	hw->private_free = snd_usx2y_hwdep_pcm_private_free;
	hw->ops.open = snd_usx2y_hwdep_pcm_open;
	hw->ops.release = snd_usx2y_hwdep_pcm_release;
	hw->ops.mmap = snd_usx2y_hwdep_pcm_mmap;
	hw->exclusive = 1;
	sprintf(hw->name, "/dev/bus/usb/%03d/%03d/hwdeppcm", dev->bus->busnum, dev->devnum);

	err = snd_pcm_new(card, NAME_ALLCAPS" hwdep Audio", 2, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_usx2y_usbpcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_usx2y_usbpcm_ops);

	pcm->private_data = usx2y(card)->subs;
	pcm->info_flags = 0;

	sprintf(pcm->name, NAME_ALLCAPS" hwdep Audio");
	snd_pcm_set_managed_buffer(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
				   SNDRV_DMA_TYPE_CONTINUOUS,
				   NULL,
				   64*1024, 128*1024);
	snd_pcm_set_managed_buffer(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
				   SNDRV_DMA_TYPE_CONTINUOUS,
				   NULL,
				   64*1024, 128*1024);

	return 0;
}

#else

int usx2y_hwdep_pcm_new(struct snd_card *card)
{
	return 0;
}

#endif
