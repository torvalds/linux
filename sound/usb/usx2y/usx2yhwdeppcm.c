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


static int usX2Y_usbpcm_urb_capt_retire(struct snd_usX2Y_substream *subs)
{
	struct urb	*urb = subs->completed_urb;
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	int 		i, lens = 0, hwptr_done = subs->hwptr_done;
	struct usX2Ydev	*usX2Y = subs->usX2Y;
	if (0 > usX2Y->hwdep_pcm_shm->capture_iso_start) { //FIXME
		int head = usX2Y->hwdep_pcm_shm->captured_iso_head + 1;
		if (head >= ARRAY_SIZE(usX2Y->hwdep_pcm_shm->captured_iso))
			head = 0;
		usX2Y->hwdep_pcm_shm->capture_iso_start = head;
		snd_printdd("cap start %i\n", head);
	}
	for (i = 0; i < nr_of_packs(); i++) {
		if (urb->iso_frame_desc[i].status) { /* active? hmm, skip this */
			snd_printk(KERN_ERR "active frame status %i. Most probably some hardware problem.\n", urb->iso_frame_desc[i].status);
			return urb->iso_frame_desc[i].status;
		}
		lens += urb->iso_frame_desc[i].actual_length / usX2Y->stride;
	}
	if ((hwptr_done += lens) >= runtime->buffer_size)
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

static inline int usX2Y_iso_frames_per_buffer(struct snd_pcm_runtime *runtime,
					      struct usX2Ydev * usX2Y)
{
	return (runtime->buffer_size * 1000) / usX2Y->rate + 1;	//FIXME: so far only correct period_size == 2^x ?
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
static int usX2Y_hwdep_urb_play_prepare(struct snd_usX2Y_substream *subs,
					struct urb *urb)
{
	int count, counts, pack;
	struct usX2Ydev *usX2Y = subs->usX2Y;
	struct snd_usX2Y_hwdep_pcm_shm *shm = usX2Y->hwdep_pcm_shm;
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;

	if (0 > shm->playback_iso_start) {
		shm->playback_iso_start = shm->captured_iso_head -
			usX2Y_iso_frames_per_buffer(runtime, usX2Y);
		if (0 > shm->playback_iso_start)
			shm->playback_iso_start += ARRAY_SIZE(shm->captured_iso);
		shm->playback_iso_head = shm->playback_iso_start;
	}

	count = 0;
	for (pack = 0; pack < nr_of_packs(); pack++) {
		/* calculate the size of a packet */
		counts = shm->captured_iso[shm->playback_iso_head].length / usX2Y->stride;
		if (counts < 43 || counts > 50) {
			snd_printk(KERN_ERR "should not be here with counts=%i\n", counts);
			return -EPIPE;
		}
		/* set up descriptor */
		urb->iso_frame_desc[pack].offset = shm->captured_iso[shm->playback_iso_head].offset;
		urb->iso_frame_desc[pack].length = shm->captured_iso[shm->playback_iso_head].length;
		if (atomic_read(&subs->state) != state_RUNNING)
			memset((char *)urb->transfer_buffer + urb->iso_frame_desc[pack].offset, 0,
			       urb->iso_frame_desc[pack].length);
		if (++shm->playback_iso_head >= ARRAY_SIZE(shm->captured_iso))
			shm->playback_iso_head = 0;
		count += counts;
	}
	urb->transfer_buffer_length = count * usX2Y->stride;
	return 0;
}


static inline void usX2Y_usbpcm_urb_capt_iso_advance(struct snd_usX2Y_substream *subs,
						     struct urb *urb)
{
	int pack;
	for (pack = 0; pack < nr_of_packs(); ++pack) {
		struct usb_iso_packet_descriptor *desc = urb->iso_frame_desc + pack;
		if (NULL != subs) {
			struct snd_usX2Y_hwdep_pcm_shm *shm = subs->usX2Y->hwdep_pcm_shm;
			int head = shm->captured_iso_head + 1;
			if (head >= ARRAY_SIZE(shm->captured_iso))
				head = 0;
			shm->captured_iso[head].frame = urb->start_frame + pack;
			shm->captured_iso[head].offset = desc->offset;
			shm->captured_iso[head].length = desc->actual_length;
			shm->captured_iso_head = head;
			shm->captured_iso_frames++;
		}
		if ((desc->offset += desc->length * NRURBS*nr_of_packs()) +
		    desc->length >= SSS)
			desc->offset -= (SSS - desc->length);
	}
}

static inline int usX2Y_usbpcm_usbframe_complete(struct snd_usX2Y_substream *capsubs,
						 struct snd_usX2Y_substream *capsubs2,
						 struct snd_usX2Y_substream *playbacksubs,
						 int frame)
{
	int err, state;
	struct urb *urb = playbacksubs->completed_urb;

	state = atomic_read(&playbacksubs->state);
	if (NULL != urb) {
		if (state == state_RUNNING)
			usX2Y_urb_play_retire(playbacksubs, urb);
		else if (state >= state_PRERUNNING)
			atomic_inc(&playbacksubs->state);
	} else {
		switch (state) {
		case state_STARTING1:
			urb = playbacksubs->urb[0];
			atomic_inc(&playbacksubs->state);
			break;
		case state_STARTING2:
			urb = playbacksubs->urb[1];
			atomic_inc(&playbacksubs->state);
			break;
		}
	}
	if (urb) {
		if ((err = usX2Y_hwdep_urb_play_prepare(playbacksubs, urb)) ||
		    (err = usX2Y_urb_submit(playbacksubs, urb, frame))) {
			return err;
		}
	}
	
	playbacksubs->completed_urb = NULL;

	state = atomic_read(&capsubs->state);
	if (state >= state_PREPARED) {
		if (state == state_RUNNING) {
			if ((err = usX2Y_usbpcm_urb_capt_retire(capsubs)))
				return err;
		} else if (state >= state_PRERUNNING)
			atomic_inc(&capsubs->state);
		usX2Y_usbpcm_urb_capt_iso_advance(capsubs, capsubs->completed_urb);
		if (NULL != capsubs2)
			usX2Y_usbpcm_urb_capt_iso_advance(NULL, capsubs2->completed_urb);
		if ((err = usX2Y_urb_submit(capsubs, capsubs->completed_urb, frame)))
			return err;
		if (NULL != capsubs2)
			if ((err = usX2Y_urb_submit(capsubs2, capsubs2->completed_urb, frame)))
				return err;
	}
	capsubs->completed_urb = NULL;
	if (NULL != capsubs2)
		capsubs2->completed_urb = NULL;
	return 0;
}


static void i_usX2Y_usbpcm_urb_complete(struct urb *urb)
{
	struct snd_usX2Y_substream *subs = urb->context;
	struct usX2Ydev *usX2Y = subs->usX2Y;
	struct snd_usX2Y_substream *capsubs, *capsubs2, *playbacksubs;

	if (unlikely(atomic_read(&subs->state) < state_PREPARED)) {
		snd_printdd("hcd_frame=%i ep=%i%s status=%i start_frame=%i\n",
			    usb_get_current_frame_number(usX2Y->dev),
			    subs->endpoint, usb_pipein(urb->pipe) ? "in" : "out",
			    urb->status, urb->start_frame);
		return;
	}
	if (unlikely(urb->status)) {
		usX2Y_error_urb_status(usX2Y, subs, urb);
		return;
	}
	if (likely((urb->start_frame & 0xFFFF) == (usX2Y->wait_iso_frame & 0xFFFF)))
		subs->completed_urb = urb;
	else {
		usX2Y_error_sequence(usX2Y, subs, urb);
		return;
	}

	capsubs = usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE];
	capsubs2 = usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE + 2];
	playbacksubs = usX2Y->subs[SNDRV_PCM_STREAM_PLAYBACK];
	if (capsubs->completed_urb && atomic_read(&capsubs->state) >= state_PREPARED &&
	    (NULL == capsubs2 || capsubs2->completed_urb) &&
	    (playbacksubs->completed_urb || atomic_read(&playbacksubs->state) < state_PREPARED)) {
		if (!usX2Y_usbpcm_usbframe_complete(capsubs, capsubs2, playbacksubs, urb->start_frame))
			usX2Y->wait_iso_frame += nr_of_packs();
		else {
			snd_printdd("\n");
			usX2Y_clients_stop(usX2Y);
		}
	}
}


static void usX2Y_hwdep_urb_release(struct urb **urb)
{
	usb_kill_urb(*urb);
	usb_free_urb(*urb);
	*urb = NULL;
}

/*
 * release a substream
 */
static void usX2Y_usbpcm_urbs_release(struct snd_usX2Y_substream *subs)
{
	int i;
	snd_printdd("snd_usX2Y_urbs_release() %i\n", subs->endpoint);
	for (i = 0; i < NRURBS; i++)
		usX2Y_hwdep_urb_release(subs->urb + i);
}

static void usX2Y_usbpcm_subs_startup_finish(struct usX2Ydev * usX2Y)
{
	usX2Y_urbs_set_complete(usX2Y, i_usX2Y_usbpcm_urb_complete);
	usX2Y->prepare_subs = NULL;
}

static void i_usX2Y_usbpcm_subs_startup(struct urb *urb)
{
	struct snd_usX2Y_substream *subs = urb->context;
	struct usX2Ydev *usX2Y = subs->usX2Y;
	struct snd_usX2Y_substream *prepare_subs = usX2Y->prepare_subs;
	if (NULL != prepare_subs &&
	    urb->start_frame == prepare_subs->urb[0]->start_frame) {
		atomic_inc(&prepare_subs->state);
		if (prepare_subs == usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE]) {
			struct snd_usX2Y_substream *cap_subs2 = usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE + 2];
			if (cap_subs2 != NULL)
				atomic_inc(&cap_subs2->state);
		}
		usX2Y_usbpcm_subs_startup_finish(usX2Y);
		wake_up(&usX2Y->prepare_wait_queue);
	}

	i_usX2Y_usbpcm_urb_complete(urb);
}

/*
 * initialize a substream's urbs
 */
static int usX2Y_usbpcm_urbs_allocate(struct snd_usX2Y_substream *subs)
{
	int i;
	unsigned int pipe;
	int is_playback = subs == subs->usX2Y->subs[SNDRV_PCM_STREAM_PLAYBACK];
	struct usb_device *dev = subs->usX2Y->dev;

	pipe = is_playback ? usb_sndisocpipe(dev, subs->endpoint) :
			usb_rcvisocpipe(dev, subs->endpoint);
	subs->maxpacksize = usb_maxpacket(dev, pipe, is_playback);
	if (!subs->maxpacksize)
		return -EINVAL;

	/* allocate and initialize data urbs */
	for (i = 0; i < NRURBS; i++) {
		struct urb **purb = subs->urb + i;
		if (*purb) {
			usb_kill_urb(*purb);
			continue;
		}
		*purb = usb_alloc_urb(nr_of_packs(), GFP_KERNEL);
		if (NULL == *purb) {
			usX2Y_usbpcm_urbs_release(subs);
			return -ENOMEM;
		}
		(*purb)->transfer_buffer = is_playback ?
			subs->usX2Y->hwdep_pcm_shm->playback : (
				subs->endpoint == 0x8 ?
				subs->usX2Y->hwdep_pcm_shm->capture0x8 :
				subs->usX2Y->hwdep_pcm_shm->capture0xA);

		(*purb)->dev = dev;
		(*purb)->pipe = pipe;
		(*purb)->number_of_packets = nr_of_packs();
		(*purb)->context = subs;
		(*purb)->interval = 1;
		(*purb)->complete = i_usX2Y_usbpcm_subs_startup;
	}
	return 0;
}

/*
 * free the buffer
 */
static int snd_usX2Y_usbpcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usX2Y_substream *subs = runtime->private_data,
		*cap_subs2 = subs->usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE + 2];
	mutex_lock(&subs->usX2Y->prepare_mutex);
	snd_printdd("snd_usX2Y_usbpcm_hw_free(%p)\n", substream);

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) {
		struct snd_usX2Y_substream *cap_subs = subs->usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE];
		atomic_set(&subs->state, state_STOPPED);
		usX2Y_usbpcm_urbs_release(subs);
		if (!cap_subs->pcm_substream ||
		    !cap_subs->pcm_substream->runtime ||
		    !cap_subs->pcm_substream->runtime->status ||
		    cap_subs->pcm_substream->runtime->status->state < SNDRV_PCM_STATE_PREPARED) {
			atomic_set(&cap_subs->state, state_STOPPED);
			if (NULL != cap_subs2)
				atomic_set(&cap_subs2->state, state_STOPPED);
			usX2Y_usbpcm_urbs_release(cap_subs);
			if (NULL != cap_subs2)
				usX2Y_usbpcm_urbs_release(cap_subs2);
		}
	} else {
		struct snd_usX2Y_substream *playback_subs = subs->usX2Y->subs[SNDRV_PCM_STREAM_PLAYBACK];
		if (atomic_read(&playback_subs->state) < state_PREPARED) {
			atomic_set(&subs->state, state_STOPPED);
			if (NULL != cap_subs2)
				atomic_set(&cap_subs2->state, state_STOPPED);
			usX2Y_usbpcm_urbs_release(subs);
			if (NULL != cap_subs2)
				usX2Y_usbpcm_urbs_release(cap_subs2);
		}
	}
	mutex_unlock(&subs->usX2Y->prepare_mutex);
	return snd_pcm_lib_free_pages(substream);
}

static void usX2Y_usbpcm_subs_startup(struct snd_usX2Y_substream *subs)
{
	struct usX2Ydev * usX2Y = subs->usX2Y;
	usX2Y->prepare_subs = subs;
	subs->urb[0]->start_frame = -1;
	smp_wmb();	// Make sure above modifications are seen by i_usX2Y_subs_startup()
	usX2Y_urbs_set_complete(usX2Y, i_usX2Y_usbpcm_subs_startup);
}

static int usX2Y_usbpcm_urbs_start(struct snd_usX2Y_substream *subs)
{
	int	p, u, err,
		stream = subs->pcm_substream->stream;
	struct usX2Ydev *usX2Y = subs->usX2Y;

	if (SNDRV_PCM_STREAM_CAPTURE == stream) {
		usX2Y->hwdep_pcm_shm->captured_iso_head = -1;
		usX2Y->hwdep_pcm_shm->captured_iso_frames = 0;
	}

	for (p = 0; 3 >= (stream + p); p += 2) {
		struct snd_usX2Y_substream *subs = usX2Y->subs[stream + p];
		if (subs != NULL) {
			if ((err = usX2Y_usbpcm_urbs_allocate(subs)) < 0)
				return err;
			subs->completed_urb = NULL;
		}
	}

	for (p = 0; p < 4; p++) {
		struct snd_usX2Y_substream *subs = usX2Y->subs[p];
		if (subs != NULL && atomic_read(&subs->state) >= state_PREPARED)
			goto start;
	}

 start:
	usX2Y_usbpcm_subs_startup(subs);
	for (u = 0; u < NRURBS; u++) {
		for (p = 0; 3 >= (stream + p); p += 2) {
			struct snd_usX2Y_substream *subs = usX2Y->subs[stream + p];
			if (subs != NULL) {
				struct urb *urb = subs->urb[u];
				if (usb_pipein(urb->pipe)) {
					unsigned long pack;
					if (0 == u)
						atomic_set(&subs->state, state_STARTING3);
					urb->dev = usX2Y->dev;
					urb->transfer_flags = URB_ISO_ASAP;
					for (pack = 0; pack < nr_of_packs(); pack++) {
						urb->iso_frame_desc[pack].offset = subs->maxpacksize * (pack + u * nr_of_packs());
						urb->iso_frame_desc[pack].length = subs->maxpacksize;
					}
					urb->transfer_buffer_length = subs->maxpacksize * nr_of_packs(); 
					if ((err = usb_submit_urb(urb, GFP_KERNEL)) < 0) {
						snd_printk (KERN_ERR "cannot usb_submit_urb() for urb %d, err = %d\n", u, err);
						err = -EPIPE;
						goto cleanup;
					}  else {
						snd_printdd("%i\n", urb->start_frame);
						if (u == 0)
							usX2Y->wait_iso_frame = urb->start_frame;
					}
					urb->transfer_flags = 0;
				} else {
					atomic_set(&subs->state, state_STARTING1);
					break;
				}			
			}
		}
	}
	err = 0;
	wait_event(usX2Y->prepare_wait_queue, NULL == usX2Y->prepare_subs);
	if (atomic_read(&subs->state) != state_PREPARED)
		err = -EPIPE;
		
 cleanup:
	if (err) {
		usX2Y_subs_startup_finish(usX2Y);	// Call it now
		usX2Y_clients_stop(usX2Y);		// something is completely wroong > stop evrything			
	}
	return err;
}

/*
 * prepare callback
 *
 * set format and initialize urbs
 */
static int snd_usX2Y_usbpcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usX2Y_substream *subs = runtime->private_data;
	struct usX2Ydev *usX2Y = subs->usX2Y;
	struct snd_usX2Y_substream *capsubs = subs->usX2Y->subs[SNDRV_PCM_STREAM_CAPTURE];
	int err = 0;
	snd_printdd("snd_usX2Y_pcm_prepare(%p)\n", substream);

	if (NULL == usX2Y->hwdep_pcm_shm) {
		if (NULL == (usX2Y->hwdep_pcm_shm = snd_malloc_pages(sizeof(struct snd_usX2Y_hwdep_pcm_shm), GFP_KERNEL)))
			return -ENOMEM;
		memset(usX2Y->hwdep_pcm_shm, 0, sizeof(struct snd_usX2Y_hwdep_pcm_shm));
	}

	mutex_lock(&usX2Y->prepare_mutex);
	usX2Y_subs_prepare(subs);
// Start hardware streams
// SyncStream first....
	if (atomic_read(&capsubs->state) < state_PREPARED) {
		if (usX2Y->format != runtime->format)
			if ((err = usX2Y_format_set(usX2Y, runtime->format)) < 0)
				goto up_prepare_mutex;
		if (usX2Y->rate != runtime->rate)
			if ((err = usX2Y_rate_set(usX2Y, runtime->rate)) < 0)
				goto up_prepare_mutex;
		snd_printdd("starting capture pipe for %s\n", subs == capsubs ?
			    "self" : "playpipe");
		if (0 > (err = usX2Y_usbpcm_urbs_start(capsubs)))
			goto up_prepare_mutex;
	}

	if (subs != capsubs) {
		usX2Y->hwdep_pcm_shm->playback_iso_start = -1;
		if (atomic_read(&subs->state) < state_PREPARED) {
			while (usX2Y_iso_frames_per_buffer(runtime, usX2Y) >
			       usX2Y->hwdep_pcm_shm->captured_iso_frames) {
				snd_printdd("Wait: iso_frames_per_buffer=%i,"
					    "captured_iso_frames=%i\n",
					    usX2Y_iso_frames_per_buffer(runtime, usX2Y),
					    usX2Y->hwdep_pcm_shm->captured_iso_frames);
				if (msleep_interruptible(10)) {
					err = -ERESTARTSYS;
					goto up_prepare_mutex;
				}
			} 
			if (0 > (err = usX2Y_usbpcm_urbs_start(subs)))
				goto up_prepare_mutex;
		}
		snd_printdd("Ready: iso_frames_per_buffer=%i,captured_iso_frames=%i\n",
			    usX2Y_iso_frames_per_buffer(runtime, usX2Y),
			    usX2Y->hwdep_pcm_shm->captured_iso_frames);
	} else
		usX2Y->hwdep_pcm_shm->capture_iso_start = -1;

 up_prepare_mutex:
	mutex_unlock(&usX2Y->prepare_mutex);
	return err;
}

static struct snd_pcm_hardware snd_usX2Y_4c =
{
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



static int snd_usX2Y_usbpcm_open(struct snd_pcm_substream *substream)
{
	struct snd_usX2Y_substream	*subs = ((struct snd_usX2Y_substream **)
					 snd_pcm_substream_chip(substream))[substream->stream];
	struct snd_pcm_runtime	*runtime = substream->runtime;

	if (!(subs->usX2Y->chip_status & USX2Y_STAT_CHIP_MMAP_PCM_URBS))
		return -EBUSY;

	runtime->hw = SNDRV_PCM_STREAM_PLAYBACK == substream->stream ? snd_usX2Y_2c :
		(subs->usX2Y->subs[3] ? snd_usX2Y_4c : snd_usX2Y_2c);
	runtime->private_data = subs;
	subs->pcm_substream = substream;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 1000, 200000);
	return 0;
}


static int snd_usX2Y_usbpcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_usX2Y_substream *subs = runtime->private_data;

	subs->pcm_substream = NULL;
	return 0;
}


static struct snd_pcm_ops snd_usX2Y_usbpcm_ops = 
{
	.open =		snd_usX2Y_usbpcm_open,
	.close =	snd_usX2Y_usbpcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_usX2Y_pcm_hw_params,
	.hw_free =	snd_usX2Y_usbpcm_hw_free,
	.prepare =	snd_usX2Y_usbpcm_prepare,
	.trigger =	snd_usX2Y_pcm_trigger,
	.pointer =	snd_usX2Y_pcm_pointer,
};


static int usX2Y_pcms_lock_check(struct snd_card *card)
{
	struct list_head *list;
	struct snd_device *dev;
	struct snd_pcm *pcm;
	int err = 0;
	list_for_each(list, &card->devices) {
		dev = snd_device(list);
		if (dev->type != SNDRV_DEV_PCM)
			continue;
		pcm = dev->device_data;
		mutex_lock(&pcm->open_mutex);
	}
	list_for_each(list, &card->devices) {
		int s;
		dev = snd_device(list);
		if (dev->type != SNDRV_DEV_PCM)
			continue;
		pcm = dev->device_data;
		for (s = 0; s < 2; ++s) {
			struct snd_pcm_substream *substream;
			substream = pcm->streams[s].substream;
			if (substream && SUBSTREAM_BUSY(substream))
				err = -EBUSY;
		}
	}
	return err;
}


static void usX2Y_pcms_unlock(struct snd_card *card)
{
	struct list_head *list;
	struct snd_device *dev;
	struct snd_pcm *pcm;
	list_for_each(list, &card->devices) {
		dev = snd_device(list);
		if (dev->type != SNDRV_DEV_PCM)
			continue;
		pcm = dev->device_data;
		mutex_unlock(&pcm->open_mutex);
	}
}


static int snd_usX2Y_hwdep_pcm_open(struct snd_hwdep *hw, struct file *file)
{
	// we need to be the first 
	struct snd_card *card = hw->card;
	int err = usX2Y_pcms_lock_check(card);
	if (0 == err)
		usX2Y(card)->chip_status |= USX2Y_STAT_CHIP_MMAP_PCM_URBS;
	usX2Y_pcms_unlock(card);
	return err;
}


static int snd_usX2Y_hwdep_pcm_release(struct snd_hwdep *hw, struct file *file)
{
	struct snd_card *card = hw->card;
	int err = usX2Y_pcms_lock_check(card);
	if (0 == err)
		usX2Y(hw->card)->chip_status &= ~USX2Y_STAT_CHIP_MMAP_PCM_URBS;
	usX2Y_pcms_unlock(card);
	return err;
}


static void snd_usX2Y_hwdep_pcm_vm_open(struct vm_area_struct *area)
{
}


static void snd_usX2Y_hwdep_pcm_vm_close(struct vm_area_struct *area)
{
}


static int snd_usX2Y_hwdep_pcm_vm_fault(struct vm_area_struct *area,
					struct vm_fault *vmf)
{
	unsigned long offset;
	void *vaddr;

	offset = vmf->pgoff << PAGE_SHIFT;
	vaddr = (char*)((struct usX2Ydev *)area->vm_private_data)->hwdep_pcm_shm + offset;
	vmf->page = virt_to_page(vaddr);
	get_page(vmf->page);
	return 0;
}


static const struct vm_operations_struct snd_usX2Y_hwdep_pcm_vm_ops = {
	.open = snd_usX2Y_hwdep_pcm_vm_open,
	.close = snd_usX2Y_hwdep_pcm_vm_close,
	.fault = snd_usX2Y_hwdep_pcm_vm_fault,
};


static int snd_usX2Y_hwdep_pcm_mmap(struct snd_hwdep * hw, struct file *filp, struct vm_area_struct *area)
{
	unsigned long	size = (unsigned long)(area->vm_end - area->vm_start);
	struct usX2Ydev	*usX2Y = hw->private_data;

	if (!(usX2Y->chip_status & USX2Y_STAT_CHIP_INIT))
		return -EBUSY;

	/* if userspace tries to mmap beyond end of our buffer, fail */ 
	if (size > PAGE_ALIGN(sizeof(struct snd_usX2Y_hwdep_pcm_shm))) {
		snd_printd("%lu > %lu\n", size, (unsigned long)sizeof(struct snd_usX2Y_hwdep_pcm_shm)); 
		return -EINVAL;
	}

	if (!usX2Y->hwdep_pcm_shm) {
		return -ENODEV;
	}
	area->vm_ops = &snd_usX2Y_hwdep_pcm_vm_ops;
	area->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	area->vm_private_data = hw->private_data;
	return 0;
}


static void snd_usX2Y_hwdep_pcm_private_free(struct snd_hwdep *hwdep)
{
	struct usX2Ydev *usX2Y = hwdep->private_data;
	if (NULL != usX2Y->hwdep_pcm_shm)
		snd_free_pages(usX2Y->hwdep_pcm_shm, sizeof(struct snd_usX2Y_hwdep_pcm_shm));
}


int usX2Y_hwdep_pcm_new(struct snd_card *card)
{
	int err;
	struct snd_hwdep *hw;
	struct snd_pcm *pcm;
	struct usb_device *dev = usX2Y(card)->dev;
	if (1 != nr_of_packs())
		return 0;

	if ((err = snd_hwdep_new(card, SND_USX2Y_USBPCM_ID, 1, &hw)) < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_USX2Y_PCM;
	hw->private_data = usX2Y(card);
	hw->private_free = snd_usX2Y_hwdep_pcm_private_free;
	hw->ops.open = snd_usX2Y_hwdep_pcm_open;
	hw->ops.release = snd_usX2Y_hwdep_pcm_release;
	hw->ops.mmap = snd_usX2Y_hwdep_pcm_mmap;
	hw->exclusive = 1;
	sprintf(hw->name, "/proc/bus/usb/%03d/%03d/hwdeppcm", dev->bus->busnum, dev->devnum);

	err = snd_pcm_new(card, NAME_ALLCAPS" hwdep Audio", 2, 1, 1, &pcm);
	if (err < 0) {
		return err;
	}
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_usX2Y_usbpcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_usX2Y_usbpcm_ops);

	pcm->private_data = usX2Y(card)->subs;
	pcm->info_flags = 0;

	sprintf(pcm->name, NAME_ALLCAPS" hwdep Audio");
	if (0 > (err = snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
						     SNDRV_DMA_TYPE_CONTINUOUS,
						     snd_dma_continuous_data(GFP_KERNEL),
						     64*1024, 128*1024)) ||
	    0 > (err = snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
	    					     SNDRV_DMA_TYPE_CONTINUOUS,
	    					     snd_dma_continuous_data(GFP_KERNEL),
						     64*1024, 128*1024))) {
		return err;
	}


	return 0;
}

#else

int usX2Y_hwdep_pcm_new(struct snd_card *card)
{
	return 0;
}

#endif
