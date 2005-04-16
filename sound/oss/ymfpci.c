/*
 *  Copyright 1999 Jaroslav Kysela <perex@suse.cz>
 *  Copyright 2000 Alan Cox <alan@redhat.com>
 *  Copyright 2001 Kai Germaschewski <kai@tp1.ruhr-uni-bochum.de>
 *  Copyright 2002 Pete Zaitcev <zaitcev@yahoo.com>
 *
 *  Yamaha YMF7xx driver.
 *
 *  This code is a result of high-speed collision
 *  between ymfpci.c of ALSA and cs46xx.c of Linux.
 *  -- Pete Zaitcev <zaitcev@yahoo.com>; 2000/09/18
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TODO:
 *  - Use P44Slot for 44.1 playback (beware of idle buzzing in P44Slot).
 *  - 96KHz playback for DVD - use pitch of 2.0.
 *  - Retain DMA buffer on close, do not wait the end of frame.
 *  - Resolve XXX tagged questions.
 *  - Cannot play 5133Hz.
 *  - 2001/01/07 Consider if we can remove voice_lock, like so:
 *     : Allocate/deallocate voices in open/close under semafore.
 *     : We access voices in interrupt, that only for pcms that open.
 *    voice_lock around playback_prepare closes interrupts for insane duration.
 *  - Revisit the way voice_alloc is done - too confusing, overcomplicated.
 *    Should support various channel types, however.
 *  - Remove prog_dmabuf from read/write, leave it in open.
 *  - 2001/01/07 Replace the OPL3 part of CONFIG_SOUND_YMFPCI_LEGACY code with
 *    native synthesizer through a playback slot.
 *  - 2001/11/29 ac97_save_state
 *    Talk to Kai to remove ac97_save_state before it's too late!
 *  - Second AC97
 *  - Restore S/PDIF - Toshibas have it.
 *
 * Kai used pci_alloc_consistent for DMA buffer, which sounds a little
 * unconventional. However, given how small our fragments can be,
 * a little uncached access is perhaps better than endless flushing.
 * On i386 and other I/O-coherent architectures pci_alloc_consistent
 * is entirely harmless.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/soundcard.h>
#include <linux/ac97_codec.h>
#include <linux/sound.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#ifdef CONFIG_SOUND_YMFPCI_LEGACY
# include "sound_config.h"
# include "mpu401.h"
#endif
#include "ymfpci.h"

/*
 * I do not believe in debug levels as I never can guess what
 * part of the code is going to be problematic in the future.
 * Don't forget to run your klogd with -c 8.
 *
 * Example (do not remove):
 * #define YMFDBG(fmt, arg...)  do{ printk(KERN_DEBUG fmt, ##arg); }while(0)
 */
#define YMFDBGW(fmt, arg...)  /* */	/* write counts */
#define YMFDBGI(fmt, arg...)  /* */	/* interrupts */
#define YMFDBGX(fmt, arg...)  /* */	/* ioctl */

static int ymf_playback_trigger(ymfpci_t *unit, struct ymf_pcm *ypcm, int cmd);
static void ymf_capture_trigger(ymfpci_t *unit, struct ymf_pcm *ypcm, int cmd);
static void ymfpci_voice_free(ymfpci_t *unit, ymfpci_voice_t *pvoice);
static int ymf_capture_alloc(struct ymf_unit *unit, int *pbank);
static int ymf_playback_prepare(struct ymf_state *state);
static int ymf_capture_prepare(struct ymf_state *state);
static struct ymf_state *ymf_state_alloc(ymfpci_t *unit);

static void ymfpci_aclink_reset(struct pci_dev * pci);
static void ymfpci_disable_dsp(ymfpci_t *unit);
static void ymfpci_download_image(ymfpci_t *codec);
static void ymf_memload(ymfpci_t *unit);

static DEFINE_SPINLOCK(ymf_devs_lock);
static LIST_HEAD(ymf_devs);

/*
 *  constants
 */

static struct pci_device_id ymf_id_tbl[] = {
#define DEV(v, d, data) \
  { PCI_VENDOR_ID_##v, PCI_DEVICE_ID_##v##_##d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long)data }
	DEV (YAMAHA, 724,  "YMF724"),
	DEV (YAMAHA, 724F, "YMF724F"),
	DEV (YAMAHA, 740,  "YMF740"),
	DEV (YAMAHA, 740C, "YMF740C"),
	DEV (YAMAHA, 744,  "YMF744"),
	DEV (YAMAHA, 754,  "YMF754"),
#undef DEV
	{ }
};
MODULE_DEVICE_TABLE(pci, ymf_id_tbl);

/*
 *  common I/O routines
 */

static inline void ymfpci_writeb(ymfpci_t *codec, u32 offset, u8 val)
{
	writeb(val, codec->reg_area_virt + offset);
}

static inline u16 ymfpci_readw(ymfpci_t *codec, u32 offset)
{
	return readw(codec->reg_area_virt + offset);
}

static inline void ymfpci_writew(ymfpci_t *codec, u32 offset, u16 val)
{
	writew(val, codec->reg_area_virt + offset);
}

static inline u32 ymfpci_readl(ymfpci_t *codec, u32 offset)
{
	return readl(codec->reg_area_virt + offset);
}

static inline void ymfpci_writel(ymfpci_t *codec, u32 offset, u32 val)
{
	writel(val, codec->reg_area_virt + offset);
}

static int ymfpci_codec_ready(ymfpci_t *codec, int secondary, int sched)
{
	signed long end_time;
	u32 reg = secondary ? YDSXGR_SECSTATUSADR : YDSXGR_PRISTATUSADR;
	
	end_time = jiffies + 3 * (HZ / 4);
	do {
		if ((ymfpci_readw(codec, reg) & 0x8000) == 0)
			return 0;
		if (sched) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
	} while (end_time - (signed long)jiffies >= 0);
	printk(KERN_ERR "ymfpci_codec_ready: codec %i is not ready [0x%x]\n",
	    secondary, ymfpci_readw(codec, reg));
	return -EBUSY;
}

static void ymfpci_codec_write(struct ac97_codec *dev, u8 reg, u16 val)
{
	ymfpci_t *codec = dev->private_data;
	u32 cmd;

	spin_lock(&codec->ac97_lock);
	/* XXX Do make use of dev->id */
	ymfpci_codec_ready(codec, 0, 0);
	cmd = ((YDSXG_AC97WRITECMD | reg) << 16) | val;
	ymfpci_writel(codec, YDSXGR_AC97CMDDATA, cmd);
	spin_unlock(&codec->ac97_lock);
}

static u16 _ymfpci_codec_read(ymfpci_t *unit, u8 reg)
{
	int i;

	if (ymfpci_codec_ready(unit, 0, 0))
		return ~0;
	ymfpci_writew(unit, YDSXGR_AC97CMDADR, YDSXG_AC97READCMD | reg);
	if (ymfpci_codec_ready(unit, 0, 0))
		return ~0;
	if (unit->pci->device == PCI_DEVICE_ID_YAMAHA_744 && unit->rev < 2) {
		for (i = 0; i < 600; i++)
			ymfpci_readw(unit, YDSXGR_PRISTATUSDATA);
	}
	return ymfpci_readw(unit, YDSXGR_PRISTATUSDATA);
}

static u16 ymfpci_codec_read(struct ac97_codec *dev, u8 reg)
{
	ymfpci_t *unit = dev->private_data;
	u16 ret;
	
	spin_lock(&unit->ac97_lock);
	ret = _ymfpci_codec_read(unit, reg);
	spin_unlock(&unit->ac97_lock);
	
	return ret;
}

/*
 *  Misc routines
 */

/*
 * Calculate the actual sampling rate relatetively to the base clock (48kHz).
 */
static u32 ymfpci_calc_delta(u32 rate)
{
	switch (rate) {
	case 8000:	return 0x02aaab00;
	case 11025:	return 0x03accd00;
	case 16000:	return 0x05555500;
	case 22050:	return 0x07599a00;
	case 32000:	return 0x0aaaab00;
	case 44100:	return 0x0eb33300;
	default:	return ((rate << 16) / 48000) << 12;
	}
}

static u32 def_rate[8] = {
	100, 2000, 8000, 11025, 16000, 22050, 32000, 48000
};

static u32 ymfpci_calc_lpfK(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x00570000, 0x06AA0000, 0x18B20000, 0x20930000,
		0x2B9A0000, 0x35A10000, 0x3EAA0000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x40000000;	/* FIXME: What's the right value? */
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

static u32 ymfpci_calc_lpfQ(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x35280000, 0x34A70000, 0x32020000, 0x31770000,
		0x31390000, 0x31C90000, 0x33D00000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x370A0000;
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

static u32 ymf_calc_lend(u32 rate)
{
	return (rate * YMF_SAMPF) / 48000;
}

/*
 * We ever allow only a few formats, but let's be generic, for smaller surprise.
 */
static int ymf_pcm_format_width(int format)
{
	static int mask16 = AFMT_S16_LE|AFMT_S16_BE|AFMT_U16_LE|AFMT_U16_BE;

	if ((format & (format-1)) != 0) {
		printk(KERN_ERR "ymfpci: format 0x%x is not a power of 2\n", format);
		return 8;
	}

	if (format == AFMT_IMA_ADPCM) return 4;
	if ((format & mask16) != 0) return 16;
	return 8;
}

static void ymf_pcm_update_shift(struct ymf_pcm_format *f)
{
	f->shift = 0;
	if (f->voices == 2)
		f->shift++;
	if (ymf_pcm_format_width(f->format) == 16)
		f->shift++;
}

/* Are you sure 32K is not too much? See if mpg123 skips on loaded systems. */
#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/*
 * Allocate DMA buffer
 */
static int alloc_dmabuf(ymfpci_t *unit, struct ymf_dmabuf *dmabuf)
{
	void *rawbuf = NULL;
	dma_addr_t dma_addr;
	int order;
	struct page *map, *mapend;

	/* alloc as big a chunk as we can */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--) {
		rawbuf = pci_alloc_consistent(unit->pci, PAGE_SIZE << order, &dma_addr);
		if (rawbuf)
			break;
	}
	if (!rawbuf)
		return -ENOMEM;

#if 0
	printk(KERN_DEBUG "ymfpci: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf);
#endif

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->dma_addr = dma_addr;
	dmabuf->buforder = order;

	/* now mark the pages as reserved; otherwise remap_pfn_range doesn't do what we want */
	mapend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (map = virt_to_page(rawbuf); map <= mapend; map++)
		set_bit(PG_reserved, &map->flags);

	return 0;
}

/*
 * Free DMA buffer
 */
static void dealloc_dmabuf(ymfpci_t *unit, struct ymf_dmabuf *dmabuf)
{
	struct page *map, *mapend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		mapend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (map = virt_to_page(dmabuf->rawbuf); map <= mapend; map++)
			clear_bit(PG_reserved, &map->flags);

		pci_free_consistent(unit->pci, PAGE_SIZE << dmabuf->buforder,
		    dmabuf->rawbuf, dmabuf->dma_addr);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct ymf_state *state, int rec)
{
	struct ymf_dmabuf *dmabuf;
	int w_16;
	unsigned bufsize;
	unsigned long flags;
	int redzone, redfrags;
	int ret;

	w_16 = ymf_pcm_format_width(state->format.format) == 16;
	dmabuf = rec ? &state->rpcm.dmabuf : &state->wpcm.dmabuf;

	spin_lock_irqsave(&state->unit->reg_lock, flags);
	dmabuf->hwptr = dmabuf->swptr = 0;
	dmabuf->total_bytes = 0;
	dmabuf->count = 0;
	spin_unlock_irqrestore(&state->unit->reg_lock, flags);

	/* allocate DMA buffer if not allocated yet */
	if (!dmabuf->rawbuf)
		if ((ret = alloc_dmabuf(state->unit, dmabuf)))
			return ret;

	/*
	 * Create fake fragment sizes and numbers for OSS ioctls.
	 * Import what Doom might have set with SNDCTL_DSP_SETFRAGMENT.
	 */
	bufsize = PAGE_SIZE << dmabuf->buforder;
	/* By default we give 4 big buffers. */
	dmabuf->fragshift = (dmabuf->buforder + PAGE_SHIFT - 2);
	if (dmabuf->ossfragshift > 3 &&
	    dmabuf->ossfragshift < dmabuf->fragshift) {
		/* If OSS set smaller fragments, give more smaller buffers. */
		dmabuf->fragshift = dmabuf->ossfragshift;
	}
	dmabuf->fragsize = 1 << dmabuf->fragshift;

	dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

	if (dmabuf->ossmaxfrags >= 2) {
		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= state->format.shift;
		redzone *= 3;
		redfrags = (redzone + dmabuf->fragsize-1) >> dmabuf->fragshift;

		if (dmabuf->ossmaxfrags + redfrags < dmabuf->numfrag) {
			dmabuf->numfrag = dmabuf->ossmaxfrags + redfrags;
			dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;
		}
	}

	memset(dmabuf->rawbuf, w_16 ? 0 : 0x80, dmabuf->dmasize);

	/*
	 *	Now set up the ring 
	 */

	/* XXX   ret = rec? cap_pre(): pbk_pre();  */
	spin_lock_irqsave(&state->unit->voice_lock, flags);
	if (rec) {
		if ((ret = ymf_capture_prepare(state)) != 0) {
			spin_unlock_irqrestore(&state->unit->voice_lock, flags);
			return ret;
		}
	} else {
		if ((ret = ymf_playback_prepare(state)) != 0) {
			spin_unlock_irqrestore(&state->unit->voice_lock, flags);
			return ret;
		}
	}
	spin_unlock_irqrestore(&state->unit->voice_lock, flags);

	/* set the ready flag for the dma buffer (this comment is not stupid) */
	dmabuf->ready = 1;

#if 0
	printk(KERN_DEBUG "prog_dmabuf: rate %d format 0x%x,"
	    " numfrag %d fragsize %d dmasize %d\n",
	       state->format.rate, state->format.format, dmabuf->numfrag,
	       dmabuf->fragsize, dmabuf->dmasize);
#endif

	return 0;
}

static void ymf_start_dac(struct ymf_state *state)
{
	ymf_playback_trigger(state->unit, &state->wpcm, 1);
}

// static void ymf_start_adc(struct ymf_state *state)
// {
// 	ymf_capture_trigger(state->unit, &state->rpcm, 1);
// }

/*
 * Wait until output is drained.
 * This does not kill the hardware for the sake of ioctls.
 */
static void ymf_wait_dac(struct ymf_state *state)
{
	struct ymf_unit *unit = state->unit;
	struct ymf_pcm *ypcm = &state->wpcm;
	DECLARE_WAITQUEUE(waita, current);
	unsigned long flags;

	add_wait_queue(&ypcm->dmabuf.wait, &waita);

	spin_lock_irqsave(&unit->reg_lock, flags);
	if (ypcm->dmabuf.count != 0 && !ypcm->running) {
		ymf_playback_trigger(unit, ypcm, 1);
	}

#if 0
	if (file->f_flags & O_NONBLOCK) {
		/*
		 * XXX Our  mistake is to attach DMA buffer to state
		 * rather than to some per-device structure.
		 * Cannot skip waiting, can only make it shorter.
		 */
	}
#endif

	set_current_state(TASK_UNINTERRUPTIBLE);
	while (ypcm->running) {
		spin_unlock_irqrestore(&unit->reg_lock, flags);
		schedule();
		spin_lock_irqsave(&unit->reg_lock, flags);
		set_current_state(TASK_UNINTERRUPTIBLE);
	}
	spin_unlock_irqrestore(&unit->reg_lock, flags);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ypcm->dmabuf.wait, &waita);

	/*
	 * This function may take up to 4 seconds to reach this point
	 * (32K circular buffer, 8000 Hz). User notices.
	 */
}

/* Can just stop, without wait. Or can we? */
static void ymf_stop_adc(struct ymf_state *state)
{
	struct ymf_unit *unit = state->unit;
	unsigned long flags;

	spin_lock_irqsave(&unit->reg_lock, flags);
	ymf_capture_trigger(unit, &state->rpcm, 0);
	spin_unlock_irqrestore(&unit->reg_lock, flags);
}

/*
 *  Hardware start management
 */

static void ymfpci_hw_start(ymfpci_t *unit)
{
	unsigned long flags;

	spin_lock_irqsave(&unit->reg_lock, flags);
	if (unit->start_count++ == 0) {
		ymfpci_writel(unit, YDSXGR_MODE,
		    ymfpci_readl(unit, YDSXGR_MODE) | 3);
		unit->active_bank = ymfpci_readl(unit, YDSXGR_CTRLSELECT) & 1;
	}
	spin_unlock_irqrestore(&unit->reg_lock, flags);
}

static void ymfpci_hw_stop(ymfpci_t *unit)
{
	unsigned long flags;
	long timeout = 1000;

	spin_lock_irqsave(&unit->reg_lock, flags);
	if (--unit->start_count == 0) {
		ymfpci_writel(unit, YDSXGR_MODE,
		    ymfpci_readl(unit, YDSXGR_MODE) & ~3);
		while (timeout-- > 0) {
			if ((ymfpci_readl(unit, YDSXGR_STATUS) & 2) == 0)
				break;
		}
	}
	spin_unlock_irqrestore(&unit->reg_lock, flags);
}

/*
 *  Playback voice management
 */

static int voice_alloc(ymfpci_t *codec, ymfpci_voice_type_t type, int pair, ymfpci_voice_t *rvoice[])
{
	ymfpci_voice_t *voice, *voice2;
	int idx;

	for (idx = 0; idx < YDSXG_PLAYBACK_VOICES; idx += pair ? 2 : 1) {
		voice = &codec->voices[idx];
		voice2 = pair ? &codec->voices[idx+1] : NULL;
		if (voice->use || (voice2 && voice2->use))
			continue;
		voice->use = 1;
		if (voice2)
			voice2->use = 1;
		switch (type) {
		case YMFPCI_PCM:
			voice->pcm = 1;
			if (voice2)
				voice2->pcm = 1;
			break;
		case YMFPCI_SYNTH:
			voice->synth = 1;
			break;
		case YMFPCI_MIDI:
			voice->midi = 1;
			break;
		}
		ymfpci_hw_start(codec);
		rvoice[0] = voice;
		if (voice2) {
			ymfpci_hw_start(codec);
			rvoice[1] = voice2;
		}
		return 0;
	}
	return -EBUSY;	/* Your audio channel is open by someone else. */
}

static void ymfpci_voice_free(ymfpci_t *unit, ymfpci_voice_t *pvoice)
{
	ymfpci_hw_stop(unit);
	pvoice->use = pvoice->pcm = pvoice->synth = pvoice->midi = 0;
	pvoice->ypcm = NULL;
}

/*
 */

static void ymf_pcm_interrupt(ymfpci_t *codec, ymfpci_voice_t *voice)
{
	struct ymf_pcm *ypcm;
	int redzone;
	int pos, delta, swptr;
	int played, distance;
	struct ymf_state *state;
	struct ymf_dmabuf *dmabuf;
	char silence;

	if ((ypcm = voice->ypcm) == NULL) {
		return;
	}
	if ((state = ypcm->state) == NULL) {
		ypcm->running = 0;	// lock it
		return;
	}
	dmabuf = &ypcm->dmabuf;
	spin_lock(&codec->reg_lock);
	if (ypcm->running) {
		YMFDBGI("ymfpci: %d, intr bank %d count %d start 0x%x:%x\n",
		   voice->number, codec->active_bank, dmabuf->count,
		   le32_to_cpu(voice->bank[0].start),
		   le32_to_cpu(voice->bank[1].start));
		silence = (ymf_pcm_format_width(state->format.format) == 16) ?
		    0 : 0x80;
		/* We need actual left-hand-side redzone size here. */
		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= (state->format.shift + 1);
		swptr = dmabuf->swptr;

		pos = le32_to_cpu(voice->bank[codec->active_bank].start);
		pos <<= state->format.shift;
		if (pos < 0 || pos >= dmabuf->dmasize) {	/* ucode bug */
			printk(KERN_ERR "ymfpci%d: runaway voice %d: hwptr %d=>%d dmasize %d\n",
			    codec->dev_audio, voice->number,
			    dmabuf->hwptr, pos, dmabuf->dmasize);
			pos = 0;
		}
		if (pos < dmabuf->hwptr) {
			delta = dmabuf->dmasize - dmabuf->hwptr;
			memset(dmabuf->rawbuf + dmabuf->hwptr, silence, delta);
			delta += pos;
			memset(dmabuf->rawbuf, silence, pos);
		} else {
			delta = pos - dmabuf->hwptr;
			memset(dmabuf->rawbuf + dmabuf->hwptr, silence, delta);
		}
		dmabuf->hwptr = pos;

		if (dmabuf->count == 0) {
			printk(KERN_ERR "ymfpci%d: %d: strain: hwptr %d\n",
			    codec->dev_audio, voice->number, dmabuf->hwptr);
			ymf_playback_trigger(codec, ypcm, 0);
		}

		if (swptr <= pos) {
			distance = pos - swptr;
		} else {
			distance = dmabuf->dmasize - (swptr - pos);
		}
		if (distance < redzone) {
			/*
			 * hwptr inside redzone => DMA ran out of samples.
			 */
			if (delta < dmabuf->count) {
				/*
				 * Lost interrupt or other screwage.
				 */
				printk(KERN_ERR "ymfpci%d: %d: lost: delta %d"
				    " hwptr %d swptr %d distance %d count %d\n",
				    codec->dev_audio, voice->number, delta,
				    dmabuf->hwptr, swptr, distance, dmabuf->count);
			} else {
				/*
				 * Normal end of DMA.
				 */
				YMFDBGI("ymfpci%d: %d: done: delta %d"
				    " hwptr %d swptr %d distance %d count %d\n",
				    codec->dev_audio, voice->number, delta,
				    dmabuf->hwptr, swptr, distance, dmabuf->count);
			}
			played = dmabuf->count;
			if (ypcm->running) {
				ymf_playback_trigger(codec, ypcm, 0);
			}
		} else {
			/*
			 * hwptr is chipping away towards a remote swptr.
			 * Calculate other distance and apply it to count.
			 */
			if (swptr >= pos) {
				distance = swptr - pos;
			} else {
				distance = dmabuf->dmasize - (pos - swptr);
			}
			if (distance < dmabuf->count) {
				played = dmabuf->count - distance;
			} else {
				played = 0;
			}
		}

		dmabuf->total_bytes += played;
		dmabuf->count -= played;
		if (dmabuf->count < dmabuf->dmasize / 2) {
			wake_up(&dmabuf->wait);
		}
	}
	spin_unlock(&codec->reg_lock);
}

static void ymf_cap_interrupt(ymfpci_t *unit, struct ymf_capture *cap)
{
	struct ymf_pcm *ypcm;
	int redzone;
	struct ymf_state *state;
	struct ymf_dmabuf *dmabuf;
	int pos, delta;
	int cnt;

	if ((ypcm = cap->ypcm) == NULL) {
		return;
	}
	if ((state = ypcm->state) == NULL) {
		ypcm->running = 0;	// lock it
		return;
	}
	dmabuf = &ypcm->dmabuf;
	spin_lock(&unit->reg_lock);
	if (ypcm->running) {
		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= (state->format.shift + 1);

		pos = le32_to_cpu(cap->bank[unit->active_bank].start);
		// pos <<= state->format.shift;
		if (pos < 0 || pos >= dmabuf->dmasize) {	/* ucode bug */
			printk(KERN_ERR "ymfpci%d: runaway capture %d: hwptr %d=>%d dmasize %d\n",
			    unit->dev_audio, ypcm->capture_bank_number,
			    dmabuf->hwptr, pos, dmabuf->dmasize);
			pos = 0;
		}
		if (pos < dmabuf->hwptr) {
			delta = dmabuf->dmasize - dmabuf->hwptr;
			delta += pos;
		} else {
			delta = pos - dmabuf->hwptr;
		}
		dmabuf->hwptr = pos;

		cnt = dmabuf->count;
		cnt += delta;
		if (cnt + redzone > dmabuf->dmasize) {
			/* Overflow - bump swptr */
			dmabuf->count = dmabuf->dmasize - redzone;
			dmabuf->swptr = dmabuf->hwptr + redzone;
			if (dmabuf->swptr >= dmabuf->dmasize) {
				dmabuf->swptr -= dmabuf->dmasize;
			}
		} else {
			dmabuf->count = cnt;
		}

		dmabuf->total_bytes += delta;
		if (dmabuf->count) {		/* && is_sleeping  XXX */
			wake_up(&dmabuf->wait);
		}
	}
	spin_unlock(&unit->reg_lock);
}

static int ymf_playback_trigger(ymfpci_t *codec, struct ymf_pcm *ypcm, int cmd)
{

	if (ypcm->voices[0] == NULL) {
		return -EINVAL;
	}
	if (cmd != 0) {
		codec->ctrl_playback[ypcm->voices[0]->number + 1] =
		    cpu_to_le32(ypcm->voices[0]->bank_ba);
		if (ypcm->voices[1] != NULL)
			codec->ctrl_playback[ypcm->voices[1]->number + 1] =
			    cpu_to_le32(ypcm->voices[1]->bank_ba);
		ypcm->running = 1;
	} else {
		codec->ctrl_playback[ypcm->voices[0]->number + 1] = 0;
		if (ypcm->voices[1] != NULL)
			codec->ctrl_playback[ypcm->voices[1]->number + 1] = 0;
		ypcm->running = 0;
	}
	return 0;
}

static void ymf_capture_trigger(ymfpci_t *codec, struct ymf_pcm *ypcm, int cmd)
{
	u32 tmp;

	if (cmd != 0) {
		tmp = ymfpci_readl(codec, YDSXGR_MAPOFREC) | (1 << ypcm->capture_bank_number);
		ymfpci_writel(codec, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 1;
	} else {
		tmp = ymfpci_readl(codec, YDSXGR_MAPOFREC) & ~(1 << ypcm->capture_bank_number);
		ymfpci_writel(codec, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 0;
	}
}

static int ymfpci_pcm_voice_alloc(struct ymf_pcm *ypcm, int voices)
{
	struct ymf_unit *unit;
	int err;

	unit = ypcm->state->unit;
	if (ypcm->voices[1] != NULL && voices < 2) {
		ymfpci_voice_free(unit, ypcm->voices[1]);
		ypcm->voices[1] = NULL;
	}
	if (voices == 1 && ypcm->voices[0] != NULL)
		return 0;		/* already allocated */
	if (voices == 2 && ypcm->voices[0] != NULL && ypcm->voices[1] != NULL)
		return 0;		/* already allocated */
	if (voices > 1) {
		if (ypcm->voices[0] != NULL && ypcm->voices[1] == NULL) {
			ymfpci_voice_free(unit, ypcm->voices[0]);
			ypcm->voices[0] = NULL;
		}		
		if ((err = voice_alloc(unit, YMFPCI_PCM, 1, ypcm->voices)) < 0)
			return err;
		ypcm->voices[0]->ypcm = ypcm;
		ypcm->voices[1]->ypcm = ypcm;
	} else {
		if ((err = voice_alloc(unit, YMFPCI_PCM, 0, ypcm->voices)) < 0)
			return err;
		ypcm->voices[0]->ypcm = ypcm;
	}
	return 0;
}

static void ymf_pcm_init_voice(ymfpci_voice_t *voice, int stereo,
    int rate, int w_16, unsigned long addr, unsigned int end, int spdif)
{
	u32 format;
	u32 delta = ymfpci_calc_delta(rate);
	u32 lpfQ = ymfpci_calc_lpfQ(rate);
	u32 lpfK = ymfpci_calc_lpfK(rate);
	ymfpci_playback_bank_t *bank;
	int nbank;

	/*
	 * The gain is a floating point number. According to the manual,
	 * bit 31 indicates a sign bit, bit 30 indicates an integer part,
	 * and bits [29:15] indicate a decimal fraction part. Thus,
	 * for a gain of 1.0 the constant of 0x40000000 is loaded.
	 */
	unsigned default_gain = cpu_to_le32(0x40000000);

	format = (stereo ? 0x00010000 : 0) | (w_16 ? 0 : 0x80000000);
	if (stereo)
		end >>= 1;
	if (w_16)
		end >>= 1;
	for (nbank = 0; nbank < 2; nbank++) {
		bank = &voice->bank[nbank];
		bank->format = cpu_to_le32(format);
		bank->loop_default = 0;	/* 0-loops forever, otherwise count */
		bank->base = cpu_to_le32(addr);
		bank->loop_start = 0;
		bank->loop_end = cpu_to_le32(end);
		bank->loop_frac = 0;
		bank->eg_gain_end = default_gain;
		bank->lpfQ = cpu_to_le32(lpfQ);
		bank->status = 0;
		bank->num_of_frames = 0;
		bank->loop_count = 0;
		bank->start = 0;
		bank->start_frac = 0;
		bank->delta =
		bank->delta_end = cpu_to_le32(delta);
		bank->lpfK =
		bank->lpfK_end = cpu_to_le32(lpfK);
		bank->eg_gain = default_gain;
		bank->lpfD1 =
		bank->lpfD2 = 0;

		bank->left_gain = 
		bank->right_gain =
		bank->left_gain_end =
		bank->right_gain_end =
		bank->eff1_gain =
		bank->eff2_gain =
		bank->eff3_gain =
		bank->eff1_gain_end =
		bank->eff2_gain_end =
		bank->eff3_gain_end = 0;

		if (!stereo) {
			if (!spdif) {
				bank->left_gain = 
				bank->right_gain =
				bank->left_gain_end =
				bank->right_gain_end = default_gain;
			} else {
				bank->eff2_gain =
				bank->eff2_gain_end =
				bank->eff3_gain =
				bank->eff3_gain_end = default_gain;
			}
		} else {
			if (!spdif) {
				if ((voice->number & 1) == 0) {
					bank->left_gain =
					bank->left_gain_end = default_gain;
				} else {
					bank->format |= cpu_to_le32(1);
					bank->right_gain =
					bank->right_gain_end = default_gain;
				}
			} else {
				if ((voice->number & 1) == 0) {
					bank->eff2_gain =
					bank->eff2_gain_end = default_gain;
				} else {
					bank->format |= cpu_to_le32(1);
					bank->eff3_gain =
					bank->eff3_gain_end = default_gain;
				}
			}
		}
	}
}

/*
 * XXX Capture channel allocation is entirely fake at the moment.
 * We use only one channel and mark it busy as required.
 */
static int ymf_capture_alloc(struct ymf_unit *unit, int *pbank)
{
	struct ymf_capture *cap;
	int cbank;

	cbank = 1;		/* Only ADC slot is used for now. */
	cap = &unit->capture[cbank];
	if (cap->use)
		return -EBUSY;
	cap->use = 1;
	*pbank = cbank;
	return 0;
}

static int ymf_playback_prepare(struct ymf_state *state)
{
	struct ymf_pcm *ypcm = &state->wpcm;
	int err, nvoice;

	if ((err = ymfpci_pcm_voice_alloc(ypcm, state->format.voices)) < 0) {
		/* Somebody started 32 mpg123's in parallel? */
		printk(KERN_INFO "ymfpci%d: cannot allocate voice\n",
		    state->unit->dev_audio);
		return err;
	}

	for (nvoice = 0; nvoice < state->format.voices; nvoice++) {
		ymf_pcm_init_voice(ypcm->voices[nvoice],
		    state->format.voices == 2, state->format.rate,
		    ymf_pcm_format_width(state->format.format) == 16,
		    ypcm->dmabuf.dma_addr, ypcm->dmabuf.dmasize,
		    ypcm->spdif);
	}
	return 0;
}

static int ymf_capture_prepare(struct ymf_state *state)
{
	ymfpci_t *unit = state->unit;
	struct ymf_pcm *ypcm = &state->rpcm;
	ymfpci_capture_bank_t * bank;
	/* XXX This is confusing, gotta rename one of them banks... */
	int nbank;		/* flip-flop bank */
	int cbank;		/* input [super-]bank */
	struct ymf_capture *cap;
	u32 rate, format;

	if (ypcm->capture_bank_number == -1) {
		if (ymf_capture_alloc(unit, &cbank) != 0)
			return -EBUSY;

		ypcm->capture_bank_number = cbank;

		cap = &unit->capture[cbank];
		cap->bank = unit->bank_capture[cbank][0];
		cap->ypcm = ypcm;
		ymfpci_hw_start(unit);
	}

	// ypcm->frag_size = snd_pcm_lib_transfer_fragment(substream);
	// frag_size is replaced with nonfragged byte-aligned rolling buffer
	rate = ((48000 * 4096) / state->format.rate) - 1;
	format = 0;
	if (state->format.voices == 2)
		format |= 2;
	if (ymf_pcm_format_width(state->format.format) == 8)
		format |= 1;
	switch (ypcm->capture_bank_number) {
	case 0:
		ymfpci_writel(unit, YDSXGR_RECFORMAT, format);
		ymfpci_writel(unit, YDSXGR_RECSLOTSR, rate);
		break;
	case 1:
		ymfpci_writel(unit, YDSXGR_ADCFORMAT, format);
		ymfpci_writel(unit, YDSXGR_ADCSLOTSR, rate);
		break;
	}
	for (nbank = 0; nbank < 2; nbank++) {
		bank = unit->bank_capture[ypcm->capture_bank_number][nbank];
		bank->base = cpu_to_le32(ypcm->dmabuf.dma_addr);
		// bank->loop_end = ypcm->dmabuf.dmasize >> state->format.shift;
		bank->loop_end = cpu_to_le32(ypcm->dmabuf.dmasize);
		bank->start = 0;
		bank->num_of_loops = 0;
	}
#if 0 /* s/pdif */
	if (state->digital.dig_valid)
		/*state->digital.type == SND_PCM_DIG_AES_IEC958*/
		ymfpci_writew(codec, YDSXGR_SPDIFOUTSTATUS,
		    state->digital.dig_status[0] | (state->digital.dig_status[1] << 8));
#endif
	return 0;
}

static irqreturn_t ymf_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ymfpci_t *codec = dev_id;
	u32 status, nvoice, mode;
	struct ymf_voice *voice;
	struct ymf_capture *cap;

	status = ymfpci_readl(codec, YDSXGR_STATUS);
	if (status & 0x80000000) {
		codec->active_bank = ymfpci_readl(codec, YDSXGR_CTRLSELECT) & 1;
		spin_lock(&codec->voice_lock);
		for (nvoice = 0; nvoice < YDSXG_PLAYBACK_VOICES; nvoice++) {
			voice = &codec->voices[nvoice];
			if (voice->use)
				ymf_pcm_interrupt(codec, voice);
		}
		for (nvoice = 0; nvoice < YDSXG_CAPTURE_VOICES; nvoice++) {
			cap = &codec->capture[nvoice];
			if (cap->use)
				ymf_cap_interrupt(codec, cap);
		}
		spin_unlock(&codec->voice_lock);
		spin_lock(&codec->reg_lock);
		ymfpci_writel(codec, YDSXGR_STATUS, 0x80000000);
		mode = ymfpci_readl(codec, YDSXGR_MODE) | 2;
		ymfpci_writel(codec, YDSXGR_MODE, mode);
		spin_unlock(&codec->reg_lock);
	}

	status = ymfpci_readl(codec, YDSXGR_INTFLAG);
	if (status & 1) {
		/* timer handler */
		ymfpci_writel(codec, YDSXGR_INTFLAG, ~0);
	}
	return IRQ_HANDLED;
}

static void ymf_pcm_free_substream(struct ymf_pcm *ypcm)
{
	unsigned long flags;
	struct ymf_unit *unit;

	unit = ypcm->state->unit;

	if (ypcm->type == PLAYBACK_VOICE) {
		spin_lock_irqsave(&unit->voice_lock, flags);
		if (ypcm->voices[1])
			ymfpci_voice_free(unit, ypcm->voices[1]);
		if (ypcm->voices[0])
			ymfpci_voice_free(unit, ypcm->voices[0]);
		spin_unlock_irqrestore(&unit->voice_lock, flags);
	} else {
		if (ypcm->capture_bank_number != -1) {
			unit->capture[ypcm->capture_bank_number].use = 0;
			ypcm->capture_bank_number = -1;
			ymfpci_hw_stop(unit);
		}
	}
}

static struct ymf_state *ymf_state_alloc(ymfpci_t *unit)
{
	struct ymf_pcm *ypcm;
	struct ymf_state *state;

	if ((state = kmalloc(sizeof(struct ymf_state), GFP_KERNEL)) == NULL) {
		goto out0;
	}
	memset(state, 0, sizeof(struct ymf_state));

	ypcm = &state->wpcm;
	ypcm->state = state;
	ypcm->type = PLAYBACK_VOICE;
	ypcm->capture_bank_number = -1;
	init_waitqueue_head(&ypcm->dmabuf.wait);

	ypcm = &state->rpcm;
	ypcm->state = state;
	ypcm->type = CAPTURE_AC97;
	ypcm->capture_bank_number = -1;
	init_waitqueue_head(&ypcm->dmabuf.wait);

	state->unit = unit;

	state->format.format = AFMT_U8;
	state->format.rate = 8000;
	state->format.voices = 1;
	ymf_pcm_update_shift(&state->format);

	return state;

out0:
	return NULL;
}

/* AES/IEC958 channel status bits */
#define SND_PCM_AES0_PROFESSIONAL	(1<<0)	/* 0 = consumer, 1 = professional */
#define SND_PCM_AES0_NONAUDIO		(1<<1)	/* 0 = audio, 1 = non-audio */
#define SND_PCM_AES0_PRO_EMPHASIS	(7<<2)	/* mask - emphasis */
#define SND_PCM_AES0_PRO_EMPHASIS_NOTID	(0<<2)	/* emphasis not indicated */
#define SND_PCM_AES0_PRO_EMPHASIS_NONE	(1<<2)	/* none emphasis */
#define SND_PCM_AES0_PRO_EMPHASIS_5015	(3<<2)	/* 50/15us emphasis */
#define SND_PCM_AES0_PRO_EMPHASIS_CCITT	(7<<2)	/* CCITT J.17 emphasis */
#define SND_PCM_AES0_PRO_FREQ_UNLOCKED	(1<<5)	/* source sample frequency: 0 = locked, 1 = unlocked */
#define SND_PCM_AES0_PRO_FS		(3<<6)	/* mask - sample frequency */
#define SND_PCM_AES0_PRO_FS_NOTID	(0<<6)	/* fs not indicated */
#define SND_PCM_AES0_PRO_FS_44100	(1<<6)	/* 44.1kHz */
#define SND_PCM_AES0_PRO_FS_48000	(2<<6)	/* 48kHz */
#define SND_PCM_AES0_PRO_FS_32000	(3<<6)	/* 32kHz */
#define SND_PCM_AES0_CON_NOT_COPYRIGHT	(1<<2)	/* 0 = copyright, 1 = not copyright */
#define SND_PCM_AES0_CON_EMPHASIS	(7<<3)	/* mask - emphasis */
#define SND_PCM_AES0_CON_EMPHASIS_NONE	(0<<3)	/* none emphasis */
#define SND_PCM_AES0_CON_EMPHASIS_5015	(1<<3)	/* 50/15us emphasis */
#define SND_PCM_AES0_CON_MODE		(3<<6)	/* mask - mode */
#define SND_PCM_AES1_PRO_MODE		(15<<0)	/* mask - channel mode */
#define SND_PCM_AES1_PRO_MODE_NOTID	(0<<0)	/* not indicated */
#define SND_PCM_AES1_PRO_MODE_STEREOPHONIC (2<<0) /* stereophonic - ch A is left */
#define SND_PCM_AES1_PRO_MODE_SINGLE	(4<<0)	/* single channel */
#define SND_PCM_AES1_PRO_MODE_TWO	(8<<0)	/* two channels */
#define SND_PCM_AES1_PRO_MODE_PRIMARY	(12<<0)	/* primary/secondary */
#define SND_PCM_AES1_PRO_MODE_BYTE3	(15<<0)	/* vector to byte 3 */
#define SND_PCM_AES1_PRO_USERBITS	(15<<4)	/* mask - user bits */
#define SND_PCM_AES1_PRO_USERBITS_NOTID	(0<<4)	/* not indicated */
#define SND_PCM_AES1_PRO_USERBITS_192	(8<<4)	/* 192-bit structure */
#define SND_PCM_AES1_PRO_USERBITS_UDEF	(12<<4)	/* user defined application */
#define SND_PCM_AES1_CON_CATEGORY	0x7f
#define SND_PCM_AES1_CON_GENERAL	0x00
#define SND_PCM_AES1_CON_EXPERIMENTAL	0x40
#define SND_PCM_AES1_CON_SOLIDMEM_MASK	0x0f
#define SND_PCM_AES1_CON_SOLIDMEM_ID	0x08
#define SND_PCM_AES1_CON_BROADCAST1_MASK 0x07
#define SND_PCM_AES1_CON_BROADCAST1_ID	0x04
#define SND_PCM_AES1_CON_DIGDIGCONV_MASK 0x07
#define SND_PCM_AES1_CON_DIGDIGCONV_ID	0x02
#define SND_PCM_AES1_CON_ADC_COPYRIGHT_MASK 0x1f
#define SND_PCM_AES1_CON_ADC_COPYRIGHT_ID 0x06
#define SND_PCM_AES1_CON_ADC_MASK	0x1f
#define SND_PCM_AES1_CON_ADC_ID		0x16
#define SND_PCM_AES1_CON_BROADCAST2_MASK 0x0f
#define SND_PCM_AES1_CON_BROADCAST2_ID	0x0e
#define SND_PCM_AES1_CON_LASEROPT_MASK	0x07
#define SND_PCM_AES1_CON_LASEROPT_ID	0x01
#define SND_PCM_AES1_CON_MUSICAL_MASK	0x07
#define SND_PCM_AES1_CON_MUSICAL_ID	0x05
#define SND_PCM_AES1_CON_MAGNETIC_MASK	0x07
#define SND_PCM_AES1_CON_MAGNETIC_ID	0x03
#define SND_PCM_AES1_CON_IEC908_CD	(SND_PCM_AES1_CON_LASEROPT_ID|0x00)
#define SND_PCM_AES1_CON_NON_IEC908_CD	(SND_PCM_AES1_CON_LASEROPT_ID|0x08)
#define SND_PCM_AES1_CON_PCM_CODER	(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x00)
#define SND_PCM_AES1_CON_SAMPLER	(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x20)
#define SND_PCM_AES1_CON_MIXER		(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x10)
#define SND_PCM_AES1_CON_RATE_CONVERTER	(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x18)
#define SND_PCM_AES1_CON_SYNTHESIZER	(SND_PCM_AES1_CON_MUSICAL_ID|0x00)
#define SND_PCM_AES1_CON_MICROPHONE	(SND_PCM_AES1_CON_MUSICAL_ID|0x08)
#define SND_PCM_AES1_CON_DAT		(SND_PCM_AES1_CON_MAGNETIC_ID|0x00)
#define SND_PCM_AES1_CON_VCR		(SND_PCM_AES1_CON_MAGNETIC_ID|0x08)
#define SND_PCM_AES1_CON_ORIGINAL	(1<<7)	/* this bits depends on the category code */
#define SND_PCM_AES2_PRO_SBITS		(7<<0)	/* mask - sample bits */
#define SND_PCM_AES2_PRO_SBITS_20	(2<<0)	/* 20-bit - coordination */
#define SND_PCM_AES2_PRO_SBITS_24	(4<<0)	/* 24-bit - main audio */
#define SND_PCM_AES2_PRO_SBITS_UDEF	(6<<0)	/* user defined application */
#define SND_PCM_AES2_PRO_WORDLEN	(7<<3)	/* mask - source word length */
#define SND_PCM_AES2_PRO_WORDLEN_NOTID	(0<<3)	/* not indicated */
#define SND_PCM_AES2_PRO_WORDLEN_22_18	(2<<3)	/* 22-bit or 18-bit */
#define SND_PCM_AES2_PRO_WORDLEN_23_19	(4<<3)	/* 23-bit or 19-bit */
#define SND_PCM_AES2_PRO_WORDLEN_24_20	(5<<3)	/* 24-bit or 20-bit */
#define SND_PCM_AES2_PRO_WORDLEN_20_16	(6<<3)	/* 20-bit or 16-bit */
#define SND_PCM_AES2_CON_SOURCE		(15<<0)	/* mask - source number */
#define SND_PCM_AES2_CON_SOURCE_UNSPEC	(0<<0)	/* unspecified */
#define SND_PCM_AES2_CON_CHANNEL	(15<<4)	/* mask - channel number */
#define SND_PCM_AES2_CON_CHANNEL_UNSPEC	(0<<4)	/* unspecified */
#define SND_PCM_AES3_CON_FS		(15<<0)	/* mask - sample frequency */
#define SND_PCM_AES3_CON_FS_44100	(0<<0)	/* 44.1kHz */
#define SND_PCM_AES3_CON_FS_48000	(2<<0)	/* 48kHz */
#define SND_PCM_AES3_CON_FS_32000	(3<<0)	/* 32kHz */
#define SND_PCM_AES3_CON_CLOCK		(3<<4)	/* mask - clock accuracy */
#define SND_PCM_AES3_CON_CLOCK_1000PPM	(0<<4)	/* 1000 ppm */
#define SND_PCM_AES3_CON_CLOCK_50PPM	(1<<4)	/* 50 ppm */
#define SND_PCM_AES3_CON_CLOCK_VARIABLE	(2<<4)	/* variable pitch */

/*
 * User interface
 */

/*
 * in this loop, dmabuf.count signifies the amount of data that is
 * waiting to be copied to the user's buffer.  it is filled by the dma
 * machine and drained by this loop.
 */
static ssize_t
ymf_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->rpcm.dmabuf;
	struct ymf_unit *unit = state->unit;
	DECLARE_WAITQUEUE(waita, current);
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr;
	int cnt;			/* This many to go in this revolution */

	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
		return ret;
	ret = 0;

	add_wait_queue(&dmabuf->wait, &waita);
	set_current_state(TASK_INTERRUPTIBLE);
	while (count > 0) {
		spin_lock_irqsave(&unit->reg_lock, flags);
		if (unit->suspended) {
			spin_unlock_irqrestore(&unit->reg_lock, flags);
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			if (signal_pending(current)) {
				if (!ret) ret = -EAGAIN;
				break;
			}
			continue;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count < cnt)
			cnt = dmabuf->count;
		spin_unlock_irqrestore(&unit->reg_lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is empty, start the dma machine and wait for data to be
			   recorded */
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			if (!state->rpcm.running) {
				ymf_capture_trigger(state->unit, &state->rpcm, 1);
			}
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				break;
			}
			/* This isnt strictly right for the 810  but it'll do */
			tmo = (dmabuf->dmasize * HZ) / (state->format.rate * 2);
			tmo >>= state->format.shift;
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer overrun. And worse, there is
			   NOTHING we can do to prevent it. */
			tmo = schedule_timeout(tmo);
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			set_current_state(TASK_INTERRUPTIBLE);
			if (tmo == 0 && dmabuf->count == 0) {
				printk(KERN_ERR "ymfpci%d: recording schedule timeout, "
				    "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				    state->unit->dev_audio,
				    dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				    dmabuf->hwptr, dmabuf->swptr);
			}
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				break;
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			if (!ret) ret = -EFAULT;
			break;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&unit->reg_lock, flags);
		if (unit->suspended) {
			spin_unlock_irqrestore(&unit->reg_lock, flags);
			continue;
		}

		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		// spin_unlock_irqrestore(&unit->reg_lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		// spin_lock_irqsave(&unit->reg_lock, flags);
		if (!state->rpcm.running) {
			ymf_capture_trigger(unit, &state->rpcm, 1);
		}
		spin_unlock_irqrestore(&unit->reg_lock, flags);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &waita);

	return ret;
}

static ssize_t
ymf_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->wpcm.dmabuf;
	struct ymf_unit *unit = state->unit;
	DECLARE_WAITQUEUE(waita, current);
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr;
	int cnt;			/* This many to go in this revolution */
	int redzone;
	int delay;

	YMFDBGW("ymf_write: count %d\n", count);

	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
		return ret;
	ret = 0;

	/*
	 * Alan's cs46xx works without a red zone - marvel of ingenuity.
	 * We are not so brilliant... Red zone does two things:
	 *  1. allows for safe start after a pause as we have no way
	 *     to know what the actual, relentlessly advancing, hwptr is.
	 *  2. makes computations in ymf_pcm_interrupt simpler.
	 */
	redzone = ymf_calc_lend(state->format.rate) << state->format.shift;
	redzone *= 3;	/* 2 redzone + 1 possible uncertainty reserve. */

	add_wait_queue(&dmabuf->wait, &waita);
	set_current_state(TASK_INTERRUPTIBLE);
	while (count > 0) {
		spin_lock_irqsave(&unit->reg_lock, flags);
		if (unit->suspended) {
			spin_unlock_irqrestore(&unit->reg_lock, flags);
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			if (signal_pending(current)) {
				if (!ret) ret = -EAGAIN;
				break;
			}
			continue;
		}
		if (dmabuf->count < 0) {
			printk(KERN_ERR
			   "ymf_write: count %d, was legal in cs46xx\n",
			    dmabuf->count);
			dmabuf->count = 0;
		}
		if (dmabuf->count == 0) {
			swptr = dmabuf->hwptr;
			if (state->wpcm.running) {
				/*
				 * Add uncertainty reserve.
				 */
				cnt = ymf_calc_lend(state->format.rate);
				cnt <<= state->format.shift;
				if ((swptr += cnt) >= dmabuf->dmasize) {
					swptr -= dmabuf->dmasize;
				}
			}
			dmabuf->swptr = swptr;
		} else {
			/*
			 * XXX This is not right if dmabuf->count is small -
			 * about 2*x frame size or less. We cannot count on
			 * on appending and not causing an artefact.
			 * Should use a variation of the count==0 case above.
			 */
			swptr = dmabuf->swptr;
		}
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize - redzone)
			cnt = (dmabuf->dmasize - redzone) - dmabuf->count;
		spin_unlock_irqrestore(&unit->reg_lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			YMFDBGW("ymf_write: full, count %d swptr %d\n",
			   dmabuf->count, dmabuf->swptr);
			/*
			 * buffer is full, start the dma machine and
			 * wait for data to be played
			 */
			spin_lock_irqsave(&unit->reg_lock, flags);
			if (!state->wpcm.running) {
				ymf_playback_trigger(unit, &state->wpcm, 1);
			}
			spin_unlock_irqrestore(&unit->reg_lock, flags);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				break;
			}
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf + swptr, buffer, cnt)) {
			if (!ret) ret = -EFAULT;
			break;
		}

		if ((swptr += cnt) >= dmabuf->dmasize) {
			swptr -= dmabuf->dmasize;
		}

		spin_lock_irqsave(&unit->reg_lock, flags);
		if (unit->suspended) {
			spin_unlock_irqrestore(&unit->reg_lock, flags);
			continue;
		}
		dmabuf->swptr = swptr;
		dmabuf->count += cnt;

		/*
		 * Start here is a bad idea - may cause startup click
		 * in /bin/play when dmabuf is not full yet.
		 * However, some broken applications do not make
		 * any use of SNDCTL_DSP_SYNC (Doom is the worst).
		 * One frame is about 5.3ms, Doom write size is 46ms.
		 */
		delay = state->format.rate / 20;	/* 50ms */
		delay <<= state->format.shift;
		if (dmabuf->count >= delay && !state->wpcm.running) {
			ymf_playback_trigger(unit, &state->wpcm, 1);
		}

		spin_unlock_irqrestore(&unit->reg_lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &waita);

	YMFDBGW("ymf_write: ret %d dmabuf.count %d\n", ret, dmabuf->count);
	return ret;
}

static unsigned int ymf_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf;
	int redzone;
	unsigned long flags;
	unsigned int mask = 0;

	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &state->wpcm.dmabuf.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &state->rpcm.dmabuf.wait, wait);

	spin_lock_irqsave(&state->unit->reg_lock, flags);
	if (file->f_mode & FMODE_READ) {
		dmabuf = &state->rpcm.dmabuf;
		if (dmabuf->count >= (signed)dmabuf->fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= state->format.shift;
		redzone *= 3;

		dmabuf = &state->wpcm.dmabuf;
		if (dmabuf->mapped) {
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		} else {
			/*
			 * Don't select unless a full fragment is available.
			 * Otherwise artsd does GETOSPACE, sees 0, and loops.
			 */
			if (dmabuf->count + redzone + dmabuf->fragsize
			     <= dmabuf->dmasize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&state->unit->reg_lock, flags);

	return mask;
}

static int ymf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->wpcm.dmabuf;
	int ret;
	unsigned long size;

	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(state, 0)) != 0)
			return ret;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(state, 1)) != 0)
			return ret;
	} else 
		return -EINVAL;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		return -EINVAL;
	if (remap_pfn_range(vma, vma->vm_start,
			     virt_to_phys(dmabuf->rawbuf) >> PAGE_SHIFT,
			     size, vma->vm_page_prot))
		return -EAGAIN;
	dmabuf->mapped = 1;

/* P3 */ printk(KERN_INFO "ymfpci: using memory mapped sound, untested!\n");
	return 0;
}

static int ymf_ioctl(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned long arg)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int redzone;
	int val;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	switch (cmd) {
	case OSS_GETVERSION:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETVER) arg 0x%lx\n", cmd, arg);
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_RESET:
		YMFDBGX("ymf_ioctl: cmd 0x%x(RESET)\n", cmd);
		if (file->f_mode & FMODE_WRITE) {
			ymf_wait_dac(state);
			dmabuf = &state->wpcm.dmabuf;
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr;
			dmabuf->count = dmabuf->total_bytes = 0;
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
		if (file->f_mode & FMODE_READ) {
			ymf_stop_adc(state);
			dmabuf = &state->rpcm.dmabuf;
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr;
			dmabuf->count = dmabuf->total_bytes = 0;
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
		return 0;

	case SNDCTL_DSP_SYNC:
		YMFDBGX("ymf_ioctl: cmd 0x%x(SYNC)\n", cmd);
		if (file->f_mode & FMODE_WRITE) {
			dmabuf = &state->wpcm.dmabuf;
			if (file->f_flags & O_NONBLOCK) {
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				if (dmabuf->count != 0 && !state->wpcm.running) {
					ymf_start_dac(state);
				}
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			} else {
				ymf_wait_dac(state);
			}
		}
		/* XXX What does this do for reading? dmabuf->count=0; ? */
		return 0;

	case SNDCTL_DSP_SPEED: /* set smaple rate */
		if (get_user(val, p))
			return -EFAULT;
		YMFDBGX("ymf_ioctl: cmd 0x%x(SPEED) sp %d\n", cmd, val);
		if (val >= 8000 && val <= 48000) {
			if (file->f_mode & FMODE_WRITE) {
				ymf_wait_dac(state);
				dmabuf = &state->wpcm.dmabuf;
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				dmabuf->ready = 0;
				state->format.rate = val;
				ymf_pcm_update_shift(&state->format);
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			}
			if (file->f_mode & FMODE_READ) {
				ymf_stop_adc(state);
				dmabuf = &state->rpcm.dmabuf;
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				dmabuf->ready = 0;
				state->format.rate = val;
				ymf_pcm_update_shift(&state->format);
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			}
		}
		return put_user(state->format.rate, p);

	/*
	 * OSS manual does not mention SNDCTL_DSP_STEREO at all.
	 * All channels are mono and if you want stereo, you
	 * play into two channels with SNDCTL_DSP_CHANNELS.
	 * However, mpg123 calls it. I wonder, why Michael Hipp used it.
	 */
	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
		if (get_user(val, p))
			return -EFAULT;
		YMFDBGX("ymf_ioctl: cmd 0x%x(STEREO) st %d\n", cmd, val);
		if (file->f_mode & FMODE_WRITE) {
			ymf_wait_dac(state); 
			dmabuf = &state->wpcm.dmabuf;
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			state->format.voices = val ? 2 : 1;
			ymf_pcm_update_shift(&state->format);
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
		if (file->f_mode & FMODE_READ) {
			ymf_stop_adc(state);
			dmabuf = &state->rpcm.dmabuf;
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			state->format.voices = val ? 2 : 1;
			ymf_pcm_update_shift(&state->format);
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETBLK)\n", cmd);
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(state, 0)))
				return val;
			val = state->wpcm.dmabuf.fragsize;
			YMFDBGX("ymf_ioctl: GETBLK w %d\n", val);
			return put_user(val, p);
		}
		if (file->f_mode & FMODE_READ) {
			if ((val = prog_dmabuf(state, 1)))
				return val;
			val = state->rpcm.dmabuf.fragsize;
			YMFDBGX("ymf_ioctl: GETBLK r %d\n", val);
			return put_user(val, p);
		}
		return -EINVAL;

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETFMTS)\n", cmd);
		return put_user(AFMT_S16_LE|AFMT_U8, p);

	case SNDCTL_DSP_SETFMT: /* Select sample format */
		if (get_user(val, p))
			return -EFAULT;
		YMFDBGX("ymf_ioctl: cmd 0x%x(SETFMT) fmt %d\n", cmd, val);
		if (val == AFMT_S16_LE || val == AFMT_U8) {
			if (file->f_mode & FMODE_WRITE) {
				ymf_wait_dac(state);
				dmabuf = &state->wpcm.dmabuf;
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				dmabuf->ready = 0;
				state->format.format = val;
				ymf_pcm_update_shift(&state->format);
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			}
			if (file->f_mode & FMODE_READ) {
				ymf_stop_adc(state);
				dmabuf = &state->rpcm.dmabuf;
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				dmabuf->ready = 0;
				state->format.format = val;
				ymf_pcm_update_shift(&state->format);
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			}
		}
		return put_user(state->format.format, p);

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, p))
			return -EFAULT;
		YMFDBGX("ymf_ioctl: cmd 0x%x(CHAN) ch %d\n", cmd, val);
		if (val != 0) {
			if (file->f_mode & FMODE_WRITE) {
				ymf_wait_dac(state);
				if (val == 1 || val == 2) {
					spin_lock_irqsave(&state->unit->reg_lock, flags);
					dmabuf = &state->wpcm.dmabuf;
					dmabuf->ready = 0;
					state->format.voices = val;
					ymf_pcm_update_shift(&state->format);
					spin_unlock_irqrestore(&state->unit->reg_lock, flags);
				}
			}
			if (file->f_mode & FMODE_READ) {
				ymf_stop_adc(state);
				if (val == 1 || val == 2) {
					spin_lock_irqsave(&state->unit->reg_lock, flags);
					dmabuf = &state->rpcm.dmabuf;
					dmabuf->ready = 0;
					state->format.voices = val;
					ymf_pcm_update_shift(&state->format);
					spin_unlock_irqrestore(&state->unit->reg_lock, flags);
				}
			}
		}
		return put_user(state->format.voices, p);

	case SNDCTL_DSP_POST:
		YMFDBGX("ymf_ioctl: cmd 0x%x(POST)\n", cmd);
		/*
		 * Quoting OSS PG:
		 *    The ioctl SNDCTL_DSP_POST is a lightweight version of
		 *    SNDCTL_DSP_SYNC. It just tells to the driver that there
		 *    is likely to be a pause in the output. This makes it
		 *    possible for the device to handle the pause more
		 *    intelligently. This ioctl doesn't block the application.
		 *
		 * The paragraph above is a clumsy way to say "flush ioctl".
		 * This ioctl is used by mpg123.
		 */
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		if (state->wpcm.dmabuf.count != 0 && !state->wpcm.running) {
			ymf_start_dac(state);
		}
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, p))
			return -EFAULT;
		YMFDBGX("ymf_ioctl: cmd 0x%x(SETFRAG) fr 0x%04x:%04x(%d:%d)\n",
		    cmd,
		    (val >> 16) & 0xFFFF, val & 0xFFFF,
		    (val >> 16) & 0xFFFF, val & 0xFFFF);
		dmabuf = &state->wpcm.dmabuf;
		dmabuf->ossfragshift = val & 0xffff;
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		if (dmabuf->ossfragshift < 4)
			dmabuf->ossfragshift = 4;
		if (dmabuf->ossfragshift > 15)
			dmabuf->ossfragshift = 15;
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETOSPACE)\n", cmd);
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		dmabuf = &state->wpcm.dmabuf;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= state->format.shift;
		redzone *= 3;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->dmasize - dmabuf->count - redzone;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETISPACE)\n", cmd);
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		dmabuf = &state->rpcm.dmabuf;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 1)) != 0)
			return val;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_NONBLOCK:
		YMFDBGX("ymf_ioctl: cmd 0x%x(NONBLOCK)\n", cmd);
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETCAPS)\n", cmd);
		/* return put_user(DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP,
			    p); */
		return put_user(0, p);

	case SNDCTL_DSP_GETIPTR:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETIPTR)\n", cmd);
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		dmabuf = &state->rpcm.dmabuf;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		YMFDBGX("ymf_ioctl: GETIPTR ptr %d bytes %d\n",
		    cinfo.ptr, cinfo.bytes);
		return copy_to_user(argp, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETOPTR:
		YMFDBGX("ymf_ioctl: cmd 0x%x(GETOPTR)\n", cmd);
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		dmabuf = &state->wpcm.dmabuf;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		YMFDBGX("ymf_ioctl: GETOPTR ptr %d bytes %d\n",
		    cinfo.ptr, cinfo.bytes);
		return copy_to_user(argp, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_SETDUPLEX:
		YMFDBGX("ymf_ioctl: cmd 0x%x(SETDUPLEX)\n", cmd);
		return 0;		/* Always duplex */

	case SOUND_PCM_READ_RATE:
		YMFDBGX("ymf_ioctl: cmd 0x%x(READ_RATE)\n", cmd);
		return put_user(state->format.rate, p);

	case SOUND_PCM_READ_CHANNELS:
		YMFDBGX("ymf_ioctl: cmd 0x%x(READ_CH)\n", cmd);
		return put_user(state->format.voices, p);

	case SOUND_PCM_READ_BITS:
		YMFDBGX("ymf_ioctl: cmd 0x%x(READ_BITS)\n", cmd);
		return put_user(AFMT_S16_LE, p);

	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		YMFDBGX("ymf_ioctl: cmd 0x%x unsupported\n", cmd);
		return -ENOTTY;

	default:
		/*
		 * Some programs mix up audio devices and ioctls
		 * or perhaps they expect "universal" ioctls,
		 * for instance we get SNDCTL_TMR_CONTINUE here.
		 * (mpg123 -g 100 ends here too - to be fixed.)
		 */
		YMFDBGX("ymf_ioctl: cmd 0x%x unknown\n", cmd);
		break;
	}
	return -ENOTTY;
}

/*
 * open(2)
 * We use upper part of the minor to distinguish between soundcards.
 * Channels are opened with a clone open.
 */
static int ymf_open(struct inode *inode, struct file *file)
{
	struct list_head *list;
	ymfpci_t *unit = NULL;
	int minor;
	struct ymf_state *state;
	int err;

	minor = iminor(inode);
	if ((minor & 0x0F) == 3) {	/* /dev/dspN */
		;
	} else {
		return -ENXIO;
	}

	unit = NULL;	/* gcc warns */
	spin_lock(&ymf_devs_lock);
	list_for_each(list, &ymf_devs) {
		unit = list_entry(list, ymfpci_t, ymf_devs);
		if (((unit->dev_audio ^ minor) & ~0x0F) == 0)
			break;
	}
	spin_unlock(&ymf_devs_lock);
	if (unit == NULL)
		return -ENODEV;

	down(&unit->open_sem);

	if ((state = ymf_state_alloc(unit)) == NULL) {
		up(&unit->open_sem);
		return -ENOMEM;
	}
	list_add_tail(&state->chain, &unit->states);

	file->private_data = state;

	/*
	 * ymf_read and ymf_write that we borrowed from cs46xx
	 * allocate buffers with prog_dmabuf(). We call prog_dmabuf
	 * here so that in case of DMA memory exhaustion open
	 * fails rather than write.
	 *
	 * XXX prog_dmabuf allocates voice. Should allocate explicitly, above.
	 */
	if (file->f_mode & FMODE_WRITE) {
		if (!state->wpcm.dmabuf.ready) {
			if ((err = prog_dmabuf(state, 0)) != 0) {
				goto out_nodma;
			}
		}
	}
	if (file->f_mode & FMODE_READ) {
		if (!state->rpcm.dmabuf.ready) {
			if ((err = prog_dmabuf(state, 1)) != 0) {
				goto out_nodma;
			}
		}
	}

#if 0 /* test if interrupts work */
	ymfpci_writew(unit, YDSXGR_TIMERCOUNT, 0xfffe);	/* ~ 680ms */
	ymfpci_writeb(unit, YDSXGR_TIMERCTRL,
	    (YDSXGR_TIMERCTRL_TEN|YDSXGR_TIMERCTRL_TIEN));
#endif
	up(&unit->open_sem);

	return nonseekable_open(inode, file);

out_nodma:
	/*
	 * XXX Broken custom: "goto out_xxx" in other place is
	 * a nestable exception, but here it is not nestable due to semaphore.
	 * XXX Doubtful technique of self-describing objects....
	 */
	dealloc_dmabuf(unit, &state->wpcm.dmabuf);
	dealloc_dmabuf(unit, &state->rpcm.dmabuf);
	ymf_pcm_free_substream(&state->wpcm);
	ymf_pcm_free_substream(&state->rpcm);

	list_del(&state->chain);
	kfree(state);

	up(&unit->open_sem);
	return err;
}

static int ymf_release(struct inode *inode, struct file *file)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	ymfpci_t *unit = state->unit;

#if 0 /* test if interrupts work */
	ymfpci_writeb(unit, YDSXGR_TIMERCTRL, 0);
#endif

	down(&unit->open_sem);

	/*
	 * XXX Solve the case of O_NONBLOCK close - don't deallocate here.
	 * Deallocate when unloading the driver and we can wait.
	 */
	ymf_wait_dac(state);
	ymf_stop_adc(state);		/* fortunately, it's immediate */
	dealloc_dmabuf(unit, &state->wpcm.dmabuf);
	dealloc_dmabuf(unit, &state->rpcm.dmabuf);
	ymf_pcm_free_substream(&state->wpcm);
	ymf_pcm_free_substream(&state->rpcm);

	list_del(&state->chain);
	file->private_data = NULL;	/* Can you tell I programmed Solaris */
	kfree(state);

	up(&unit->open_sem);

	return 0;
}

/*
 * Mixer operations are based on cs46xx.
 */
static int ymf_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct list_head *list;
	ymfpci_t *unit;
	int i;

	spin_lock(&ymf_devs_lock);
	list_for_each(list, &ymf_devs) {
		unit = list_entry(list, ymfpci_t, ymf_devs);
		for (i = 0; i < NR_AC97; i++) {
			if (unit->ac97_codec[i] != NULL &&
			    unit->ac97_codec[i]->dev_mixer == minor) {
				spin_unlock(&ymf_devs_lock);
				goto match;
			}
		}
	}
	spin_unlock(&ymf_devs_lock);
	return -ENODEV;

 match:
	file->private_data = unit->ac97_codec[i];

	return nonseekable_open(inode, file);
}

static int ymf_ioctl_mixdev(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

	return codec->mixer_ioctl(codec, cmd, arg);
}

static int ymf_release_mixdev(struct inode *inode, struct file *file)
{
	return 0;
}

static /*const*/ struct file_operations ymf_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= ymf_read,
	.write		= ymf_write,
	.poll		= ymf_poll,
	.ioctl		= ymf_ioctl,
	.mmap		= ymf_mmap,
	.open		= ymf_open,
	.release	= ymf_release,
};

static /*const*/ struct file_operations ymf_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= ymf_ioctl_mixdev,
	.open		= ymf_open_mixdev,
	.release	= ymf_release_mixdev,
};

/*
 */

static int ymf_suspend(struct pci_dev *pcidev, pm_message_t unused)
{
	struct ymf_unit *unit = pci_get_drvdata(pcidev);
	unsigned long flags;
	struct ymf_dmabuf *dmabuf;
	struct list_head *p;
	struct ymf_state *state;
	struct ac97_codec *codec;
	int i;

	spin_lock_irqsave(&unit->reg_lock, flags);

	unit->suspended = 1;

	for (i = 0; i < NR_AC97; i++) {
		if ((codec = unit->ac97_codec[i]) != NULL)
			ac97_save_state(codec);
	}

	list_for_each(p, &unit->states) {
		state = list_entry(p, struct ymf_state, chain);

		dmabuf = &state->wpcm.dmabuf;
		dmabuf->hwptr = dmabuf->swptr = 0;
		dmabuf->total_bytes = 0;
		dmabuf->count = 0;

		dmabuf = &state->rpcm.dmabuf;
		dmabuf->hwptr = dmabuf->swptr = 0;
		dmabuf->total_bytes = 0;
		dmabuf->count = 0;
	}

	ymfpci_writel(unit, YDSXGR_NATIVEDACOUTVOL, 0);
	ymfpci_disable_dsp(unit);

	spin_unlock_irqrestore(&unit->reg_lock, flags);
	
	return 0;
}

static int ymf_resume(struct pci_dev *pcidev)
{
	struct ymf_unit *unit = pci_get_drvdata(pcidev);
	unsigned long flags;
	struct list_head *p;
	struct ymf_state *state;
	struct ac97_codec *codec;
	int i;

	ymfpci_aclink_reset(unit->pci);
	ymfpci_codec_ready(unit, 0, 1);		/* prints diag if not ready. */

#ifdef CONFIG_SOUND_YMFPCI_LEGACY
	/* XXX At this time the legacy registers are probably deprogrammed. */
#endif

	ymfpci_download_image(unit);

	ymf_memload(unit);

	spin_lock_irqsave(&unit->reg_lock, flags);

	if (unit->start_count) {
		ymfpci_writel(unit, YDSXGR_MODE, 3);
		unit->active_bank = ymfpci_readl(unit, YDSXGR_CTRLSELECT) & 1;
	}

	for (i = 0; i < NR_AC97; i++) {
		if ((codec = unit->ac97_codec[i]) != NULL)
			ac97_restore_state(codec);
	}

	unit->suspended = 0;
	list_for_each(p, &unit->states) {
		state = list_entry(p, struct ymf_state, chain);
		wake_up(&state->wpcm.dmabuf.wait);
		wake_up(&state->rpcm.dmabuf.wait);
	}

	spin_unlock_irqrestore(&unit->reg_lock, flags);
	return 0;
}

/*
 *  initialization routines
 */

#ifdef CONFIG_SOUND_YMFPCI_LEGACY

static int ymfpci_setup_legacy(ymfpci_t *unit, struct pci_dev *pcidev)
{
	int v;
	int mpuio = -1, oplio = -1;

	switch (unit->iomidi) {
	case 0x330:
		mpuio = 0;
		break;
	case 0x300:
		mpuio = 1;
		break;
	case 0x332:
		mpuio = 2;
		break;
	case 0x334:
		mpuio = 3;
		break;
	default: ;
	}

	switch (unit->iosynth) {
	case 0x388:
		oplio = 0;
		break;
	case 0x398:
		oplio = 1;
		break;
	case 0x3a0:
		oplio = 2;
		break;
	case 0x3a8:
		oplio = 3;
		break;
	default: ;
	}

	if (mpuio >= 0 || oplio >= 0) {
		/* 0x0020: 1 - 10 bits of I/O address decoded, 0 - 16 bits. */
		v = 0x001e;
		pci_write_config_word(pcidev, PCIR_LEGCTRL, v);

		switch (pcidev->device) {
		case PCI_DEVICE_ID_YAMAHA_724:
		case PCI_DEVICE_ID_YAMAHA_740:
		case PCI_DEVICE_ID_YAMAHA_724F:
		case PCI_DEVICE_ID_YAMAHA_740C:
			v = 0x8800;
			if (mpuio >= 0) { v |= mpuio<<4; }
			if (oplio >= 0) { v |= oplio; }
			pci_write_config_word(pcidev, PCIR_ELEGCTRL, v);
			break;

		case PCI_DEVICE_ID_YAMAHA_744:
		case PCI_DEVICE_ID_YAMAHA_754:
			v = 0x8800;
			pci_write_config_word(pcidev, PCIR_ELEGCTRL, v);
			if (oplio >= 0) {
				pci_write_config_word(pcidev, PCIR_OPLADR, unit->iosynth);
			}
			if (mpuio >= 0) {
				pci_write_config_word(pcidev, PCIR_MPUADR, unit->iomidi);
			}
			break;

		default:
			printk(KERN_ERR "ymfpci: Unknown device ID: 0x%x\n",
			    pcidev->device);
			return -EINVAL;
		}
	}

	return 0;
}
#endif /* CONFIG_SOUND_YMFPCI_LEGACY */

static void ymfpci_aclink_reset(struct pci_dev * pci)
{
	u8 cmd;

	/*
	 * In the 744, 754 only 0x01 exists, 0x02 is undefined.
	 * It does not seem to hurt to trip both regardless of revision.
	 */
	pci_read_config_byte(pci, PCIR_DSXGCTRL, &cmd);
	pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd & 0xfc);
	pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd | 0x03);
	pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd & 0xfc);

	pci_write_config_word(pci, PCIR_DSXPWRCTRL1, 0);
	pci_write_config_word(pci, PCIR_DSXPWRCTRL2, 0);
}

static void ymfpci_enable_dsp(ymfpci_t *codec)
{
	ymfpci_writel(codec, YDSXGR_CONFIG, 0x00000001);
}

static void ymfpci_disable_dsp(ymfpci_t *codec)
{
	u32 val;
	int timeout = 1000;

	val = ymfpci_readl(codec, YDSXGR_CONFIG);
	if (val)
		ymfpci_writel(codec, YDSXGR_CONFIG, 0x00000000);
	while (timeout-- > 0) {
		val = ymfpci_readl(codec, YDSXGR_STATUS);
		if ((val & 0x00000002) == 0)
			break;
	}
}

#include "ymfpci_image.h"

static void ymfpci_download_image(ymfpci_t *codec)
{
	int i, ver_1e;
	u16 ctrl;

	ymfpci_writel(codec, YDSXGR_NATIVEDACOUTVOL, 0x00000000);
	ymfpci_disable_dsp(codec);
	ymfpci_writel(codec, YDSXGR_MODE, 0x00010000);
	ymfpci_writel(codec, YDSXGR_MODE, 0x00000000);
	ymfpci_writel(codec, YDSXGR_MAPOFREC, 0x00000000);
	ymfpci_writel(codec, YDSXGR_MAPOFEFFECT, 0x00000000);
	ymfpci_writel(codec, YDSXGR_PLAYCTRLBASE, 0x00000000);
	ymfpci_writel(codec, YDSXGR_RECCTRLBASE, 0x00000000);
	ymfpci_writel(codec, YDSXGR_EFFCTRLBASE, 0x00000000);
	ctrl = ymfpci_readw(codec, YDSXGR_GLOBALCTRL);
	ymfpci_writew(codec, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);

	/* setup DSP instruction code */
	for (i = 0; i < YDSXG_DSPLENGTH / 4; i++)
		ymfpci_writel(codec, YDSXGR_DSPINSTRAM + (i << 2), DspInst[i]);

	switch (codec->pci->device) {
	case PCI_DEVICE_ID_YAMAHA_724F:
	case PCI_DEVICE_ID_YAMAHA_740C:
	case PCI_DEVICE_ID_YAMAHA_744:
	case PCI_DEVICE_ID_YAMAHA_754:
		ver_1e = 1;
		break;
	default:
		ver_1e = 0;
	}

	if (ver_1e) {
		/* setup control instruction code */
		for (i = 0; i < YDSXG_CTRLLENGTH / 4; i++)
			ymfpci_writel(codec, YDSXGR_CTRLINSTRAM + (i << 2), CntrlInst1E[i]);
	} else {
		for (i = 0; i < YDSXG_CTRLLENGTH / 4; i++)
			ymfpci_writel(codec, YDSXGR_CTRLINSTRAM + (i << 2), CntrlInst[i]);
	}

	ymfpci_enable_dsp(codec);

	/* 0.02s sounds not too bad, we may do schedule_timeout() later. */
	mdelay(20); /* seems we need some delay after downloading image.. */
}

static int ymfpci_memalloc(ymfpci_t *codec)
{
	unsigned int playback_ctrl_size;
	unsigned int bank_size_playback;
	unsigned int bank_size_capture;
	unsigned int bank_size_effect;
	unsigned int size;
	unsigned int off;
	char *ptr;
	dma_addr_t pba;
	int voice, bank;

	playback_ctrl_size = 4 + 4 * YDSXG_PLAYBACK_VOICES;
	bank_size_playback = ymfpci_readl(codec, YDSXGR_PLAYCTRLSIZE) << 2;
	bank_size_capture = ymfpci_readl(codec, YDSXGR_RECCTRLSIZE) << 2;
	bank_size_effect = ymfpci_readl(codec, YDSXGR_EFFCTRLSIZE) << 2;
	codec->work_size = YDSXG_DEFAULT_WORK_SIZE;

	size = ((playback_ctrl_size + 0x00ff) & ~0x00ff) +
	    ((bank_size_playback * 2 * YDSXG_PLAYBACK_VOICES + 0xff) & ~0xff) +
	    ((bank_size_capture * 2 * YDSXG_CAPTURE_VOICES + 0xff) & ~0xff) +
	    ((bank_size_effect * 2 * YDSXG_EFFECT_VOICES + 0xff) & ~0xff) +
	    codec->work_size;

	ptr = pci_alloc_consistent(codec->pci, size + 0xff, &pba);
	if (ptr == NULL)
		return -ENOMEM;
	codec->dma_area_va = ptr;
	codec->dma_area_ba = pba;
	codec->dma_area_size = size + 0xff;

	off = (unsigned long)ptr & 0xff;
	if (off) {
		ptr += 0x100 - off;
		pba += 0x100 - off;
	}

	/*
	 * Hardware requires only ptr[playback_ctrl_size] zeroed,
	 * but in our judgement it is a wrong kind of savings, so clear it all.
	 */
	memset(ptr, 0, size);

	codec->ctrl_playback = (u32 *)ptr;
	codec->ctrl_playback_ba = pba;
	codec->ctrl_playback[0] = cpu_to_le32(YDSXG_PLAYBACK_VOICES);
	ptr += (playback_ctrl_size + 0x00ff) & ~0x00ff;
	pba += (playback_ctrl_size + 0x00ff) & ~0x00ff;

	off = 0;
	for (voice = 0; voice < YDSXG_PLAYBACK_VOICES; voice++) {
		codec->voices[voice].number = voice;
		codec->voices[voice].bank =
		    (ymfpci_playback_bank_t *) (ptr + off);
		codec->voices[voice].bank_ba = pba + off;
		off += 2 * bank_size_playback;		/* 2 banks */
	}
	off = (off + 0xff) & ~0xff;
	ptr += off;
	pba += off;

	off = 0;
	codec->bank_base_capture = pba;
	for (voice = 0; voice < YDSXG_CAPTURE_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			codec->bank_capture[voice][bank] =
			    (ymfpci_capture_bank_t *) (ptr + off);
			off += bank_size_capture;
		}
	off = (off + 0xff) & ~0xff;
	ptr += off;
	pba += off;

	off = 0;
	codec->bank_base_effect = pba;
	for (voice = 0; voice < YDSXG_EFFECT_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			codec->bank_effect[voice][bank] =
			    (ymfpci_effect_bank_t *) (ptr + off);
			off += bank_size_effect;
		}
	off = (off + 0xff) & ~0xff;
	ptr += off;
	pba += off;

	codec->work_base = pba;

	return 0;
}

static void ymfpci_memfree(ymfpci_t *codec)
{
	ymfpci_writel(codec, YDSXGR_PLAYCTRLBASE, 0);
	ymfpci_writel(codec, YDSXGR_RECCTRLBASE, 0);
	ymfpci_writel(codec, YDSXGR_EFFCTRLBASE, 0);
	ymfpci_writel(codec, YDSXGR_WORKBASE, 0);
	ymfpci_writel(codec, YDSXGR_WORKSIZE, 0);
	pci_free_consistent(codec->pci,
	    codec->dma_area_size, codec->dma_area_va, codec->dma_area_ba);
}

static void ymf_memload(ymfpci_t *unit)
{

	ymfpci_writel(unit, YDSXGR_PLAYCTRLBASE, unit->ctrl_playback_ba);
	ymfpci_writel(unit, YDSXGR_RECCTRLBASE, unit->bank_base_capture);
	ymfpci_writel(unit, YDSXGR_EFFCTRLBASE, unit->bank_base_effect);
	ymfpci_writel(unit, YDSXGR_WORKBASE, unit->work_base);
	ymfpci_writel(unit, YDSXGR_WORKSIZE, unit->work_size >> 2);

	/* S/PDIF output initialization */
	ymfpci_writew(unit, YDSXGR_SPDIFOUTCTRL, 0);
	ymfpci_writew(unit, YDSXGR_SPDIFOUTSTATUS,
		SND_PCM_AES0_CON_EMPHASIS_NONE |
		(SND_PCM_AES1_CON_ORIGINAL << 8) |
		(SND_PCM_AES1_CON_PCM_CODER << 8));

	/* S/PDIF input initialization */
	ymfpci_writew(unit, YDSXGR_SPDIFINCTRL, 0);

	/* move this volume setup to mixer */
	ymfpci_writel(unit, YDSXGR_NATIVEDACOUTVOL, 0x3fff3fff);
	ymfpci_writel(unit, YDSXGR_BUF441OUTVOL, 0);
	ymfpci_writel(unit, YDSXGR_NATIVEADCINVOL, 0x3fff3fff);
	ymfpci_writel(unit, YDSXGR_NATIVEDACINVOL, 0x3fff3fff);
}

static int ymf_ac97_init(ymfpci_t *unit, int num_ac97)
{
	struct ac97_codec *codec;
	u16 eid;

	if ((codec = ac97_alloc_codec()) == NULL)
		return -ENOMEM;

	/* initialize some basic codec information, other fields will be filled
	   in ac97_probe_codec */
	codec->private_data = unit;
	codec->id = num_ac97;

	codec->codec_read = ymfpci_codec_read;
	codec->codec_write = ymfpci_codec_write;

	if (ac97_probe_codec(codec) == 0) {
		printk(KERN_ERR "ymfpci: ac97_probe_codec failed\n");
		goto out_kfree;
	}

	eid = ymfpci_codec_read(codec, AC97_EXTENDED_ID);
	if (eid==0xFFFF) {
		printk(KERN_WARNING "ymfpci: no codec attached ?\n");
		goto out_kfree;
	}

	unit->ac97_features = eid;

	if ((codec->dev_mixer = register_sound_mixer(&ymf_mixer_fops, -1)) < 0) {
		printk(KERN_ERR "ymfpci: couldn't register mixer!\n");
		goto out_kfree;
	}

	unit->ac97_codec[num_ac97] = codec;

	return 0;
 out_kfree:
	ac97_release_codec(codec);
	return -ENODEV;
}

#ifdef CONFIG_SOUND_YMFPCI_LEGACY
# ifdef MODULE
static int mpu_io;
static int synth_io;
module_param(mpu_io, int, 0);
module_param(synth_io, int, 0);
# else
static int mpu_io     = 0x330;
static int synth_io   = 0x388;
# endif
static int assigned;
#endif /* CONFIG_SOUND_YMFPCI_LEGACY */

static int __devinit ymf_probe_one(struct pci_dev *pcidev, const struct pci_device_id *ent)
{
	u16 ctrl;
	unsigned long base;
	ymfpci_t *codec;

	int err;

	if ((err = pci_enable_device(pcidev)) != 0) {
		printk(KERN_ERR "ymfpci: pci_enable_device failed\n");
		return err;
	}
	base = pci_resource_start(pcidev, 0);

	if ((codec = kmalloc(sizeof(ymfpci_t), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "ymfpci: no core\n");
		return -ENOMEM;
	}
	memset(codec, 0, sizeof(*codec));

	spin_lock_init(&codec->reg_lock);
	spin_lock_init(&codec->voice_lock);
	spin_lock_init(&codec->ac97_lock);
	init_MUTEX(&codec->open_sem);
	INIT_LIST_HEAD(&codec->states);
	codec->pci = pcidev;

	pci_read_config_byte(pcidev, PCI_REVISION_ID, &codec->rev);

	if (request_mem_region(base, 0x8000, "ymfpci") == NULL) {
		printk(KERN_ERR "ymfpci: unable to request mem region\n");
		goto out_free;
	}

	if ((codec->reg_area_virt = ioremap(base, 0x8000)) == NULL) {
		printk(KERN_ERR "ymfpci: unable to map registers\n");
		goto out_release_region;
	}

	pci_set_master(pcidev);

	printk(KERN_INFO "ymfpci: %s at 0x%lx IRQ %d\n",
	    (char *)ent->driver_data, base, pcidev->irq);

	ymfpci_aclink_reset(pcidev);
	if (ymfpci_codec_ready(codec, 0, 1) < 0)
		goto out_unmap;

#ifdef CONFIG_SOUND_YMFPCI_LEGACY
	if (assigned == 0) {
		codec->iomidi = mpu_io;
		codec->iosynth = synth_io;
		if (ymfpci_setup_legacy(codec, pcidev) < 0)
			goto out_unmap;
		assigned = 1;
	}
#endif

	ymfpci_download_image(codec);

	if (ymfpci_memalloc(codec) < 0)
		goto out_disable_dsp;
	ymf_memload(codec);

	if (request_irq(pcidev->irq, ymf_interrupt, SA_SHIRQ, "ymfpci", codec) != 0) {
		printk(KERN_ERR "ymfpci: unable to request IRQ %d\n",
		    pcidev->irq);
		goto out_memfree;
	}

	/* register /dev/dsp */
	if ((codec->dev_audio = register_sound_dsp(&ymf_fops, -1)) < 0) {
		printk(KERN_ERR "ymfpci: unable to register dsp\n");
		goto out_free_irq;
	}

	/*
	 * Poke just the primary for the moment.
	 */
	if ((err = ymf_ac97_init(codec, 0)) != 0)
		goto out_unregister_sound_dsp;

#ifdef CONFIG_SOUND_YMFPCI_LEGACY
	codec->opl3_data.name = "ymfpci";
	codec->mpu_data.name  = "ymfpci";

	codec->opl3_data.io_base = codec->iosynth;
	codec->opl3_data.irq     = -1;

	codec->mpu_data.io_base  = codec->iomidi;
	codec->mpu_data.irq      = -1;	/* May be different from our PCI IRQ. */

	if (codec->iomidi) {
		if (!probe_uart401(&codec->mpu_data, THIS_MODULE)) {
			codec->iomidi = 0;	/* XXX kludge */
		}
	}
#endif /* CONFIG_SOUND_YMFPCI_LEGACY */

	/* put it into driver list */
	spin_lock(&ymf_devs_lock);
	list_add_tail(&codec->ymf_devs, &ymf_devs);
	spin_unlock(&ymf_devs_lock);
	pci_set_drvdata(pcidev, codec);

	return 0;

 out_unregister_sound_dsp:
	unregister_sound_dsp(codec->dev_audio);
 out_free_irq:
	free_irq(pcidev->irq, codec);
 out_memfree:
	ymfpci_memfree(codec);
 out_disable_dsp:
	ymfpci_disable_dsp(codec);
	ctrl = ymfpci_readw(codec, YDSXGR_GLOBALCTRL);
	ymfpci_writew(codec, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);
	ymfpci_writel(codec, YDSXGR_STATUS, ~0);
 out_unmap:
	iounmap(codec->reg_area_virt);
 out_release_region:
	release_mem_region(pci_resource_start(pcidev, 0), 0x8000);
 out_free:
	if (codec->ac97_codec[0])
		ac97_release_codec(codec->ac97_codec[0]);
	return -ENODEV;
}

static void __devexit ymf_remove_one(struct pci_dev *pcidev)
{
	__u16 ctrl;
	ymfpci_t *codec = pci_get_drvdata(pcidev);

	/* remove from list of devices */
	spin_lock(&ymf_devs_lock);
	list_del(&codec->ymf_devs);
	spin_unlock(&ymf_devs_lock);

	unregister_sound_mixer(codec->ac97_codec[0]->dev_mixer);
	ac97_release_codec(codec->ac97_codec[0]);
	unregister_sound_dsp(codec->dev_audio);
	free_irq(pcidev->irq, codec);
	ymfpci_memfree(codec);
	ymfpci_writel(codec, YDSXGR_STATUS, ~0);
	ymfpci_disable_dsp(codec);
	ctrl = ymfpci_readw(codec, YDSXGR_GLOBALCTRL);
	ymfpci_writew(codec, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);
	iounmap(codec->reg_area_virt);
	release_mem_region(pci_resource_start(pcidev, 0), 0x8000);
#ifdef CONFIG_SOUND_YMFPCI_LEGACY
	if (codec->iomidi) {
		unload_uart401(&codec->mpu_data);
	}
#endif /* CONFIG_SOUND_YMFPCI_LEGACY */
}

MODULE_AUTHOR("Jaroslav Kysela");
MODULE_DESCRIPTION("Yamaha YMF7xx PCI Audio");
MODULE_LICENSE("GPL");

static struct pci_driver ymfpci_driver = {
	.name		= "ymfpci",
	.id_table	= ymf_id_tbl,
	.probe		= ymf_probe_one,
	.remove		= __devexit_p(ymf_remove_one),
	.suspend	= ymf_suspend,
	.resume		= ymf_resume
};

static int __init ymf_init_module(void)
{
	return pci_module_init(&ymfpci_driver);
}

static void __exit ymf_cleanup_module (void)
{
	pci_unregister_driver(&ymfpci_driver);
}

module_init(ymf_init_module);
module_exit(ymf_cleanup_module);
