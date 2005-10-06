/* Hewlett-Packard Harmony audio driver
 *
 *   This is a driver for the Harmony audio chipset found
 *   on the LASI ASIC of various early HP PA-RISC workstations.
 *
 *   Copyright (C) 2004, Kyle McMartin <kyle@{debian.org,parisc-linux.org}>
 *
 *     Based on the previous Harmony incarnations by,
 *       Copyright 2000 (c) Linuxcare Canada, Alex deVries
 *       Copyright 2000-2003 (c) Helge Deller
 *       Copyright 2001 (c) Matthieu Delahaye
 *       Copyright 2001 (c) Jean-Christophe Vaugeois
 *       Copyright 2003 (c) Laurent Canet
 *       Copyright 2004 (c) Stuart Brady
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License, version 2, as
 *   published by the Free Software Foundation.
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
 * Notes:
 *   - graveyard and silence buffers last for lifetime of
 *     the driver. playback and capture buffers are allocated
 *     per _open()/_close().
 * 
 * TODO:
 *
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>
#include <sound/info.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/parisc-device.h>

#include "harmony.h"

static struct parisc_device_id snd_harmony_devtable[] = {
	/* bushmaster / flounder */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007A }, 
	/* 712 / 715 */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007B }, 
	/* pace */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007E }, 
	/* outfield / coral II */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007F },
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, snd_harmony_devtable);

#define NAME "harmony"
#define PFX  NAME ": "

static unsigned int snd_harmony_rates[] = {
	5512, 6615, 8000, 9600,
	11025, 16000, 18900, 22050,
	27428, 32000, 33075, 37800,
	44100, 48000
};

static unsigned int rate_bits[14] = {
	HARMONY_SR_5KHZ, HARMONY_SR_6KHZ, HARMONY_SR_8KHZ,
	HARMONY_SR_9KHZ, HARMONY_SR_11KHZ, HARMONY_SR_16KHZ,
	HARMONY_SR_18KHZ, HARMONY_SR_22KHZ, HARMONY_SR_27KHZ,
	HARMONY_SR_32KHZ, HARMONY_SR_33KHZ, HARMONY_SR_37KHZ,
	HARMONY_SR_44KHZ, HARMONY_SR_48KHZ
};

static snd_pcm_hw_constraint_list_t hw_constraint_rates = {
	.count = ARRAY_SIZE(snd_harmony_rates),
	.list = snd_harmony_rates,
	.mask = 0,
};

inline unsigned long
harmony_read(harmony_t *h, unsigned r)
{
	return __raw_readl(h->iobase + r);
}

inline void
harmony_write(harmony_t *h, unsigned r, unsigned long v)
{
	__raw_writel(v, h->iobase + r);
}

static void
harmony_wait_for_control(harmony_t *h)
{
	while (harmony_read(h, HARMONY_CNTL) & HARMONY_CNTL_C) ;
}

inline void
harmony_reset(harmony_t *h)
{
	harmony_write(h, HARMONY_RESET, 1);
	mdelay(50);
	harmony_write(h, HARMONY_RESET, 0);
}

static void
harmony_disable_interrupts(harmony_t *h)
{
	u32 dstatus;
	harmony_wait_for_control(h);
	dstatus = harmony_read(h, HARMONY_DSTATUS);
	dstatus &= ~HARMONY_DSTATUS_IE;
	harmony_write(h, HARMONY_DSTATUS, dstatus);
}

static void
harmony_enable_interrupts(harmony_t *h)
{
	u32 dstatus;
	harmony_wait_for_control(h);
	dstatus = harmony_read(h, HARMONY_DSTATUS);
	dstatus |= HARMONY_DSTATUS_IE;
	harmony_write(h, HARMONY_DSTATUS, dstatus);
}

static void
harmony_mute(harmony_t *h)
{
	unsigned long flags;

	spin_lock_irqsave(&h->mixer_lock, flags);
	harmony_wait_for_control(h);
	harmony_write(h, HARMONY_GAINCTL, HARMONY_GAIN_SILENCE);
	spin_unlock_irqrestore(&h->mixer_lock, flags);
}

static void
harmony_unmute(harmony_t *h)
{
	unsigned long flags;

	spin_lock_irqsave(&h->mixer_lock, flags);
	harmony_wait_for_control(h);
	harmony_write(h, HARMONY_GAINCTL, h->st.gain);
	spin_unlock_irqrestore(&h->mixer_lock, flags);
}

static void
harmony_set_control(harmony_t *h)
{
	u32 ctrl;
	unsigned long flags;

	spin_lock_irqsave(&h->lock, flags);

	ctrl = (HARMONY_CNTL_C      |
		(h->st.format << 6) |
		(h->st.stereo << 5) |
		(h->st.rate));

	harmony_wait_for_control(h);
	harmony_write(h, HARMONY_CNTL, ctrl);

	spin_unlock_irqrestore(&h->lock, flags);
}

static irqreturn_t
snd_harmony_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	u32 dstatus;
	harmony_t *h = dev;

	spin_lock(&h->lock);
	harmony_disable_interrupts(h);
	harmony_wait_for_control(h);
	dstatus = harmony_read(h, HARMONY_DSTATUS);
	spin_unlock(&h->lock);

	if (dstatus & HARMONY_DSTATUS_PN) {
		if (h->psubs) {
			spin_lock(&h->lock);
			h->pbuf.buf += h->pbuf.count; /* PAGE_SIZE */
			h->pbuf.buf %= h->pbuf.size; /* MAX_BUFS*PAGE_SIZE */

			harmony_write(h, HARMONY_PNXTADD, 
				      h->pbuf.addr + h->pbuf.buf);
			h->stats.play_intr++;
			spin_unlock(&h->lock);
                        snd_pcm_period_elapsed(h->psubs);
		} else {
			spin_lock(&h->lock);
			harmony_write(h, HARMONY_PNXTADD, h->sdma.addr);
			h->stats.silence_intr++;
			spin_unlock(&h->lock);
		}
	}

	if (dstatus & HARMONY_DSTATUS_RN) {
		if (h->csubs) {
			spin_lock(&h->lock);
			h->cbuf.buf += h->cbuf.count;
			h->cbuf.buf %= h->cbuf.size;

			harmony_write(h, HARMONY_RNXTADD,
				      h->cbuf.addr + h->cbuf.buf);
			h->stats.rec_intr++;
			spin_unlock(&h->lock);
                        snd_pcm_period_elapsed(h->csubs);
		} else {
			spin_lock(&h->lock);
			harmony_write(h, HARMONY_RNXTADD, h->gdma.addr);
			h->stats.graveyard_intr++;
			spin_unlock(&h->lock);
		}
	}

	spin_lock(&h->lock);
	harmony_enable_interrupts(h);
	spin_unlock(&h->lock);

	return IRQ_HANDLED;
}

static unsigned int 
snd_harmony_rate_bits(int rate)
{
	unsigned int i;
	
	for (i = 0; i < ARRAY_SIZE(snd_harmony_rates); i++)
		if (snd_harmony_rates[i] == rate)
			return rate_bits[i];

	return HARMONY_SR_44KHZ;
}

static snd_pcm_hardware_t snd_harmony_playback =
{
	.info =	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED | 
		 SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	.formats = (SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_MU_LAW |
		    SNDRV_PCM_FMTBIT_A_LAW),
	.rates = (SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_48000 |
		  SNDRV_PCM_RATE_KNOT),
	.rate_min = 5512,
	.rate_max = 48000,
	.channels_min =	1,
	.channels_max =	2,
	.buffer_bytes_max = MAX_BUF_SIZE,
	.period_bytes_min = BUF_SIZE,
	.period_bytes_max = BUF_SIZE,
	.periods_min = 1,
	.periods_max = MAX_BUFS,
	.fifo_size = 0,
};

static snd_pcm_hardware_t snd_harmony_capture =
{
        .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_MMAP_VALID |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER),
        .formats = (SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_MU_LAW |
                    SNDRV_PCM_FMTBIT_A_LAW),
        .rates = (SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_48000 |
		  SNDRV_PCM_RATE_KNOT),
        .rate_min = 5512,
        .rate_max = 48000,
        .channels_min = 1,
        .channels_max = 2,
        .buffer_bytes_max = MAX_BUF_SIZE,
        .period_bytes_min = BUF_SIZE,
        .period_bytes_max = BUF_SIZE,
        .periods_min = 1,
        .periods_max = MAX_BUFS,
        .fifo_size = 0,
};

static int
snd_harmony_playback_trigger(snd_pcm_substream_t *ss, int cmd)
{
	harmony_t *h = snd_pcm_substream_chip(ss);
	unsigned long flags;

	if (h->st.capturing)
		return -EBUSY;

	spin_lock_irqsave(&h->lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		h->st.playing = 1;
		harmony_write(h, HARMONY_PNXTADD, h->pbuf.addr);
		harmony_write(h, HARMONY_RNXTADD, h->gdma.addr);
		harmony_unmute(h);
		harmony_enable_interrupts(h);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		h->st.playing = 0;
		harmony_mute(h);
		harmony_disable_interrupts(h);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	default:
		spin_unlock_irqrestore(&h->lock, flags);
		snd_BUG();
		return -EINVAL;
	}
	spin_unlock_irqrestore(&h->lock, flags);
	
	return 0;
}

static int
snd_harmony_capture_trigger(snd_pcm_substream_t *ss, int cmd)
{
        harmony_t *h = snd_pcm_substream_chip(ss);
	unsigned long flags;

	if (h->st.playing)
		return -EBUSY;

	spin_lock_irqsave(&h->lock, flags);
        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
		h->st.capturing = 1;
                harmony_write(h, HARMONY_PNXTADD, h->sdma.addr);
                harmony_write(h, HARMONY_RNXTADD, h->cbuf.addr);
		harmony_unmute(h);
                harmony_enable_interrupts(h);
		break;
        case SNDRV_PCM_TRIGGER_STOP:
		h->st.capturing = 0;
                harmony_mute(h);
                harmony_disable_interrupts(h);
		break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        case SNDRV_PCM_TRIGGER_SUSPEND:
	default:
		spin_unlock_irqrestore(&h->lock, flags);
		snd_BUG();
                return -EINVAL;
        }
	spin_unlock_irqrestore(&h->lock, flags);
		
        return 0;
}

static int
snd_harmony_set_data_format(harmony_t *h, int fmt, int force)
{
	int o = h->st.format;
	int n;

	switch(fmt) {
	case SNDRV_PCM_FORMAT_S16_BE:
		n = HARMONY_DF_16BIT_LINEAR;
		break;
	case SNDRV_PCM_FORMAT_A_LAW:
		n = HARMONY_DF_8BIT_ALAW;
		break;
	case SNDRV_PCM_FORMAT_MU_LAW:
		n = HARMONY_DF_8BIT_ULAW;
		break;
	default:
		n = HARMONY_DF_16BIT_LINEAR;
		break;
	}

	if (force || o != n) {
		snd_pcm_format_set_silence(fmt, h->sdma.area, SILENCE_BUFSZ / 
					   (snd_pcm_format_physical_width(fmt)
					    / 8));
	}

	return n;
}

static int
snd_harmony_playback_prepare(snd_pcm_substream_t *ss)
{
	harmony_t *h = snd_pcm_substream_chip(ss);
	snd_pcm_runtime_t *rt = ss->runtime;
	
	if (h->st.capturing)
		return -EBUSY;
	
	h->pbuf.size = snd_pcm_lib_buffer_bytes(ss);
	h->pbuf.count = snd_pcm_lib_period_bytes(ss);
	h->pbuf.buf = 0;
	h->st.playing = 0;

	h->st.rate = snd_harmony_rate_bits(rt->rate);
	h->st.format = snd_harmony_set_data_format(h, rt->format, 0);
	
	if (rt->channels == 2)
		h->st.stereo = HARMONY_SS_STEREO;
	else
		h->st.stereo = HARMONY_SS_MONO;

	harmony_set_control(h);

	h->pbuf.addr = rt->dma_addr;

	return 0;
}

static int
snd_harmony_capture_prepare(snd_pcm_substream_t *ss)
{
        harmony_t *h = snd_pcm_substream_chip(ss);
        snd_pcm_runtime_t *rt = ss->runtime;

	if (h->st.playing)
		return -EBUSY;

        h->cbuf.size = snd_pcm_lib_buffer_bytes(ss);
        h->cbuf.count = snd_pcm_lib_period_bytes(ss);
        h->cbuf.buf = 0;
	h->st.capturing = 0;

        h->st.rate = snd_harmony_rate_bits(rt->rate);
        h->st.format = snd_harmony_set_data_format(h, rt->format, 0);

        if (rt->channels == 2)
                h->st.stereo = HARMONY_SS_STEREO;
        else
                h->st.stereo = HARMONY_SS_MONO;

        harmony_set_control(h);

        h->cbuf.addr = rt->dma_addr;

        return 0;
}

static snd_pcm_uframes_t 
snd_harmony_playback_pointer(snd_pcm_substream_t *ss)
{
	snd_pcm_runtime_t *rt = ss->runtime;
	harmony_t *h = snd_pcm_substream_chip(ss);
	unsigned long pcuradd;
	unsigned long played;

	if (!(h->st.playing) || (h->psubs == NULL)) 
		return 0;

	if ((h->pbuf.addr == 0) || (h->pbuf.size == 0))
		return 0;
	
	pcuradd = harmony_read(h, HARMONY_PCURADD);
	played = pcuradd - h->pbuf.addr;

#ifdef HARMONY_DEBUG
	printk(KERN_DEBUG PFX "playback_pointer is 0x%lx-0x%lx = %d bytes\n", 
	       pcuradd, h->pbuf.addr, played);	
#endif

	if (pcuradd > h->pbuf.addr + h->pbuf.size) {
		return 0;
	}

	return bytes_to_frames(rt, played);
}

static snd_pcm_uframes_t
snd_harmony_capture_pointer(snd_pcm_substream_t *ss)
{
        snd_pcm_runtime_t *rt = ss->runtime;
        harmony_t *h = snd_pcm_substream_chip(ss);
        unsigned long rcuradd;
        unsigned long caught;

        if (!(h->st.capturing) || (h->csubs == NULL))
                return 0;

        if ((h->cbuf.addr == 0) || (h->cbuf.size == 0))
                return 0;

        rcuradd = harmony_read(h, HARMONY_RCURADD);
        caught = rcuradd - h->cbuf.addr;

#ifdef HARMONY_DEBUG
        printk(KERN_DEBUG PFX "capture_pointer is 0x%lx-0x%lx = %d bytes\n",
               rcuradd, h->cbuf.addr, caught);
#endif

        if (rcuradd > h->cbuf.addr + h->cbuf.size) {
		return 0;
	}

        return bytes_to_frames(rt, caught);
}

static int 
snd_harmony_playback_open(snd_pcm_substream_t *ss)
{
	harmony_t *h = snd_pcm_substream_chip(ss);
	snd_pcm_runtime_t *rt = ss->runtime;
	int err;
	
	h->psubs = ss;
	rt->hw = snd_harmony_playback;
	snd_pcm_hw_constraint_list(rt, 0, SNDRV_PCM_HW_PARAM_RATE, 
				   &hw_constraint_rates);
	
	err = snd_pcm_hw_constraint_integer(rt, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	
	return 0;
}

static int
snd_harmony_capture_open(snd_pcm_substream_t *ss)
{
        harmony_t *h = snd_pcm_substream_chip(ss);
        snd_pcm_runtime_t *rt = ss->runtime;
        int err;

        h->csubs = ss;
        rt->hw = snd_harmony_capture;
        snd_pcm_hw_constraint_list(rt, 0, SNDRV_PCM_HW_PARAM_RATE,
                                   &hw_constraint_rates);

        err = snd_pcm_hw_constraint_integer(rt, SNDRV_PCM_HW_PARAM_PERIODS);
        if (err < 0)
                return err;

        return 0;
}

static int 
snd_harmony_playback_close(snd_pcm_substream_t *ss)
{
	harmony_t *h = snd_pcm_substream_chip(ss);
	h->psubs = NULL;
	return 0;
}

static int
snd_harmony_capture_close(snd_pcm_substream_t *ss)
{
        harmony_t *h = snd_pcm_substream_chip(ss);
        h->csubs = NULL;
        return 0;
}

static int 
snd_harmony_hw_params(snd_pcm_substream_t *ss,
		      snd_pcm_hw_params_t *hw)
{
	int err;
	harmony_t *h = snd_pcm_substream_chip(ss);
	
	err = snd_pcm_lib_malloc_pages(ss, params_buffer_bytes(hw));
	if (err > 0 && h->dma.type == SNDRV_DMA_TYPE_CONTINUOUS)
		ss->runtime->dma_addr = __pa(ss->runtime->dma_area);
	
	return err;
}

static int 
snd_harmony_hw_free(snd_pcm_substream_t *ss) 
{
	return snd_pcm_lib_free_pages(ss);
}

static snd_pcm_ops_t snd_harmony_playback_ops = {
	.open =	snd_harmony_playback_open,
	.close = snd_harmony_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_harmony_hw_params,
	.hw_free = snd_harmony_hw_free,
	.prepare = snd_harmony_playback_prepare,
	.trigger = snd_harmony_playback_trigger,
 	.pointer = snd_harmony_playback_pointer,
};

static snd_pcm_ops_t snd_harmony_capture_ops = {
        .open = snd_harmony_capture_open,
        .close = snd_harmony_capture_close,
        .ioctl = snd_pcm_lib_ioctl,
        .hw_params = snd_harmony_hw_params,
        .hw_free = snd_harmony_hw_free,
        .prepare = snd_harmony_capture_prepare,
        .trigger = snd_harmony_capture_trigger,
        .pointer = snd_harmony_capture_pointer,
};

static int 
snd_harmony_pcm_init(harmony_t *h)
{
	snd_pcm_t *pcm;
	int err;

	harmony_disable_interrupts(h);
	
   	err = snd_pcm_new(h->card, "harmony", 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, 
			&snd_harmony_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_harmony_capture_ops);

	pcm->private_data = h;
	pcm->info_flags = 0;
	strcpy(pcm->name, "harmony");
	h->pcm = pcm;

	h->psubs = NULL;
	h->csubs = NULL;
	
	/* initialize graveyard buffer */
	h->dma.type = SNDRV_DMA_TYPE_DEV;
	h->dma.dev = &h->dev->dev;
	err = snd_dma_alloc_pages(h->dma.type,
				  h->dma.dev,
				  BUF_SIZE*GRAVEYARD_BUFS,
				  &h->gdma);
	if (err < 0) {
		printk(KERN_ERR PFX "cannot allocate graveyard buffer!\n");
		return err;
	}
	
	/* initialize silence buffers */
	err = snd_dma_alloc_pages(h->dma.type,
				  h->dma.dev,
				  BUF_SIZE*SILENCE_BUFS,
				  &h->sdma);
	if (err < 0) {
		printk(KERN_ERR PFX "cannot allocate silence buffer!\n");
		return err;
	}

	/* pre-allocate space for DMA */
	err = snd_pcm_lib_preallocate_pages_for_all(pcm, h->dma.type,
						    h->dma.dev,
						    MAX_BUF_SIZE, 
						    MAX_BUF_SIZE);
	if (err < 0) {
		printk(KERN_ERR PFX "buffer allocation error: %d\n", err);
		return err;
	}

	h->st.format = snd_harmony_set_data_format(h,
		SNDRV_PCM_FORMAT_S16_BE, 1);

	return 0;
}

static void 
snd_harmony_set_new_gain(harmony_t *h)
{
 	harmony_wait_for_control(h);
	harmony_write(h, HARMONY_GAINCTL, h->st.gain);
}

static int 
snd_harmony_mixercontrol_info(snd_kcontrol_t *kc, 
			      snd_ctl_elem_info_t *uinfo)
{
	int mask = (kc->private_value >> 16) & 0xff;
	int left_shift = (kc->private_value) & 0xff;
	int right_shift = (kc->private_value >> 8) & 0xff;
	
	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : 
		       SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = left_shift == right_shift ? 1 : 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;

	return 0;
}

static int 
snd_harmony_volume_get(snd_kcontrol_t *kc, 
		       snd_ctl_elem_value_t *ucontrol)
{
	harmony_t *h = snd_kcontrol_chip(kc);
	int shift_left = (kc->private_value) & 0xff;
	int shift_right = (kc->private_value >> 8) & 0xff;
	int mask = (kc->private_value >> 16) & 0xff;
	int invert = (kc->private_value >> 24) & 0xff;
	int left, right;
	unsigned long flags;
	
	spin_lock_irqsave(&h->mixer_lock, flags);

	left = (h->st.gain >> shift_left) & mask;
	right = (h->st.gain >> shift_right) & mask;

	if (invert) {
		left = mask - left;
		right = mask - right;
	}
	ucontrol->value.integer.value[0] = left;
	ucontrol->value.integer.value[1] = right;

	spin_unlock_irqrestore(&h->mixer_lock, flags);

	return 0;
}  

static int 
snd_harmony_volume_put(snd_kcontrol_t *kc, 
		       snd_ctl_elem_value_t *ucontrol)
{
	harmony_t *h = snd_kcontrol_chip(kc);
	int shift_left = (kc->private_value) & 0xff;
	int shift_right = (kc->private_value >> 8) & 0xff;
	int mask = (kc->private_value >> 16) & 0xff;
	int invert = (kc->private_value >> 24) & 0xff;
	int left, right;
	int old_gain = h->st.gain;
	unsigned long flags;
	
	left = ucontrol->value.integer.value[0] & mask;
	right = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		left = mask - left;
		right = mask - right;
	}
	
	spin_lock_irqsave(&h->mixer_lock, flags);

	h->st.gain &= ~( (mask << shift_right) | (mask << shift_left) );
 	h->st.gain |=  ( (left << shift_left) | (right << shift_right) );
	snd_harmony_set_new_gain(h);

	spin_unlock_irqrestore(&h->mixer_lock, flags);
	
	return (old_gain - h->st.gain);
}

#define HARMONY_CONTROLS (sizeof(snd_harmony_controls)/ \
                          sizeof(snd_kcontrol_new_t))

#define HARMONY_VOLUME(xname, left_shift, right_shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,                \
  .info = snd_harmony_mixercontrol_info,                             \
  .get = snd_harmony_volume_get, .put = snd_harmony_volume_put,      \
  .private_value = ((left_shift) | ((right_shift) << 8) |            \
                   ((mask) << 16) | ((invert) << 24)) }

static snd_kcontrol_new_t snd_harmony_controls[] = {
	HARMONY_VOLUME("Playback Volume", HARMONY_GAIN_LO_SHIFT, 
		       HARMONY_GAIN_RO_SHIFT, HARMONY_GAIN_OUT, 1),
	HARMONY_VOLUME("Capture Volume", HARMONY_GAIN_LI_SHIFT,
		       HARMONY_GAIN_RI_SHIFT, HARMONY_GAIN_IN, 0),
};

static void __init 
snd_harmony_mixer_reset(harmony_t *h)
{
	harmony_mute(h);
	harmony_reset(h);
	h->st.gain = HARMONY_GAIN_DEFAULT;
	harmony_unmute(h);
}

static int __init 
snd_harmony_mixer_init(harmony_t *h)
{
	snd_card_t *card = h->card;
	int idx, err;

	snd_assert(h != NULL, return -EINVAL);
	strcpy(card->mixername, "Harmony Gain control interface");

	for (idx = 0; idx < HARMONY_CONTROLS; idx++) {
		err = snd_ctl_add(card, 
				  snd_ctl_new1(&snd_harmony_controls[idx], h));
		if (err < 0)
			return err;
	}
	
	snd_harmony_mixer_reset(h);

	return 0;
}

static int
snd_harmony_free(harmony_t *h)
{
        if (h->gdma.addr)
                snd_dma_free_pages(&h->gdma);
        if (h->sdma.addr)
                snd_dma_free_pages(&h->sdma);

	if (h->irq >= 0)
		free_irq(h->irq, h);

	if (h->iobase)
		iounmap(h->iobase);

	parisc_set_drvdata(h->dev, NULL);

	kfree(h);
	return 0;
}

static int
snd_harmony_dev_free(snd_device_t *dev)
{
	harmony_t *h = dev->device_data;
	return snd_harmony_free(h);
}

static int __devinit
snd_harmony_create(snd_card_t *card, 
		   struct parisc_device *padev, 
		   harmony_t **rchip)
{
	int err;
	harmony_t *h;
	static snd_device_ops_t ops = {
		.dev_free = snd_harmony_dev_free,
	};

	*rchip = NULL;

	h = kmalloc(sizeof(*h), GFP_KERNEL);
	if (h == NULL)
		return -ENOMEM;

	memset(&h->st, 0, sizeof(h->st));
	memset(&h->stats, 0, sizeof(h->stats));
	memset(&h->pbuf, 0, sizeof(h->pbuf));
	memset(&h->cbuf, 0, sizeof(h->cbuf));

	h->hpa = padev->hpa;
	h->card = card;
	h->dev = padev;
	h->irq = padev->irq;
	h->iobase = ioremap_nocache(padev->hpa, HARMONY_SIZE);
	if (h->iobase == NULL) {
		printk(KERN_ERR PFX "unable to remap hpa 0x%lx\n",
		       padev->hpa);
		err = -EBUSY;
		goto free_and_ret;
	}
		
	err = request_irq(h->irq, snd_harmony_interrupt, 0,
			  "harmony", h);
	if (err) {
		printk(KERN_ERR PFX "could not obtain interrupt %d",
		       h->irq);
		goto free_and_ret;
	}

	spin_lock_init(&h->mixer_lock);
	spin_lock_init(&h->lock);

        if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
                                  h, &ops)) < 0) {
                goto free_and_ret;
        }

	snd_card_set_dev(card, &padev->dev);

	*rchip = h;

	return 0;

free_and_ret:
	snd_harmony_free(h);
	return err;
}

static int __devinit
snd_harmony_probe(struct parisc_device *padev)
{
	int err;
	static int dev;
	snd_card_t *card;
	harmony_t *h;
	static int index = SNDRV_DEFAULT_IDX1;
	static char *id = SNDRV_DEFAULT_STR1;

	h = parisc_get_drvdata(padev);
	if (h != NULL) {
		return -ENODEV;
	}

	card = snd_card_new(index, id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	err = snd_harmony_create(card, padev, &h);
	if (err < 0) {
		goto free_and_ret;
	}

	err = snd_harmony_pcm_init(h);
	if (err < 0) {
		goto free_and_ret;
	}

	err = snd_harmony_mixer_init(h);
	if (err < 0) {
		goto free_and_ret;
	}

	strcpy(card->driver, "harmony");
	strcpy(card->shortname, "Harmony");
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, h->hpa, h->irq);

	err = snd_card_register(card);
	if (err < 0) {
		goto free_and_ret;
	}

	dev++;
	parisc_set_drvdata(padev, h);

	return 0;

free_and_ret:
	snd_card_free(card);
	return err;
}

static int __devexit
snd_harmony_remove(struct parisc_device *padev)
{
	harmony_t *h = parisc_get_drvdata(padev);
	snd_card_free(h->card);
	return 0;
}

static struct parisc_driver snd_harmony_driver = {
	.name = "harmony",
	.id_table = snd_harmony_devtable,
	.probe = snd_harmony_probe,
	.remove = snd_harmony_remove,
};

static int __init 
alsa_harmony_init(void)
{
	int err;

	err = register_parisc_driver(&snd_harmony_driver);
	if (err < 0) {
		printk(KERN_ERR PFX "device not found\n");
		return err;
	}

	return 0;
}

static void __exit
alsa_harmony_fini(void)
{
	int err;

	err = unregister_parisc_driver(&snd_harmony_driver);
	if (err < 0) {
		printk(KERN_ERR PFX "failed to unregister\n");
	}
	
	return;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyle McMartin <kyle@parisc-linux.org>");
MODULE_DESCRIPTION("Harmony sound driver");

module_init(alsa_harmony_init);
module_exit(alsa_harmony_fini);
