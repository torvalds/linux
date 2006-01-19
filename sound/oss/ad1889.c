/*
 *  Copyright 2001-2004 Randolph Chung <tausq@debian.org>
 *
 *  Analog Devices 1889 PCI audio driver (AD1819 AC97-compatible codec)
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
 *   Notes:
 *   1. Only flat DMA is supported; s-g is not supported right now
 *
 *
<jsm> tausq: Anyway, to set up sample rates for D to A, you just use the sample rate on the codec. For A to D, you need to set the codec always to 48K (using the split sample rate feature on the codec) and then set the resampler on the AD1889 to the sample rate you want.
<jsm> Also, when changing the sample rate on the codec you need to power it down and re power it up for the change to take effect!
 *
 * $Id: ad1889.c,v 1.3 2002/10/19 21:31:44 grundler Exp $
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/ac97_codec.h>
#include <linux/sound.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#include "ad1889.h"

#define DBG(fmt, arg...) printk(fmt, ##arg)
#define DEVNAME "ad1889"

#define NR_HW_CH	4
#define DAC_RUNNING	1
#define ADC_RUNNING	2

#define UNDERRUN(dev)	(0)

#define AD1889_READW(dev,reg) readw(dev->regbase + reg)
#define AD1889_WRITEW(dev,reg,val) writew((val), dev->regbase + reg)
#define AD1889_READL(dev,reg) readl(dev->regbase + reg)
#define AD1889_WRITEL(dev,reg,val) writel((val), dev->regbase + reg)

//now 100ms
/* #define WAIT_10MS()	schedule_timeout(HZ/10) */
#define WAIT_10MS()	do { int __i; for (__i = 0; __i < 100; __i++) udelay(1000); } while(0)

/* currently only support a single device */
static ad1889_dev_t *ad1889_dev = NULL;

/************************* helper routines ***************************** */
static inline void ad1889_set_wav_rate(ad1889_dev_t *dev, int rate)
{
	struct ac97_codec *ac97_codec = dev->ac97_codec;

	DBG("Setting WAV rate to %d\n", rate);
	dev->state[AD_WAV_STATE].dmabuf.rate = rate;
	AD1889_WRITEW(dev, AD_DSWAS, rate);

	/* Cycle the DAC to enable the new rate */
	ac97_codec->codec_write(dev->ac97_codec, AC97_POWER_CONTROL, 0x0200);
	WAIT_10MS();
	ac97_codec->codec_write(dev->ac97_codec, AC97_POWER_CONTROL, 0);
}

static inline void ad1889_set_wav_fmt(ad1889_dev_t *dev, int fmt)
{
	u16 tmp;

	DBG("Setting WAV format to 0x%x\n", fmt);

	tmp = AD1889_READW(ad1889_dev, AD_DSWSMC);
	if (fmt & AFMT_S16_LE) {
		//tmp |= 0x0100; /* set WA16 */
		tmp |= 0x0300; /* set WA16 stereo */
	} else if (fmt & AFMT_U8) {
		tmp &= ~0x0100; /* clear WA16 */
	} 
	AD1889_WRITEW(ad1889_dev, AD_DSWSMC, tmp);
}

static inline void ad1889_set_adc_fmt(ad1889_dev_t *dev, int fmt)
{
	u16 tmp;

	DBG("Setting ADC format to 0x%x\n", fmt);

	tmp = AD1889_READW(ad1889_dev, AD_DSRAMC);
	if (fmt & AFMT_S16_LE) {
		tmp |= 0x0100; /* set WA16 */
	} else if (fmt & AFMT_U8) {
		tmp &= ~0x0100; /* clear WA16 */
	} 
	AD1889_WRITEW(ad1889_dev, AD_DSRAMC, tmp);
}

static void ad1889_start_wav(ad1889_state_t *state)
{
	unsigned long flags;
	struct dmabuf *dmabuf = &state->dmabuf;
	int cnt;
	u16 tmp;

	spin_lock_irqsave(&state->card->lock, flags);

	if (dmabuf->dma_len)	/* DMA already in flight */
		goto skip_dma;

	/* setup dma */
	cnt = dmabuf->wr_ptr - dmabuf->rd_ptr;
	if (cnt == 0)		/* done - don't need to do anything */
		goto skip_dma;

	/* If the wr_ptr has wrapped, only map to the end */
	if (cnt < 0)
		cnt = DMA_SIZE - dmabuf->rd_ptr;

	dmabuf->dma_handle = pci_map_single(ad1889_dev->pci,
					dmabuf->rawbuf + dmabuf->rd_ptr,
					cnt, PCI_DMA_TODEVICE);
	dmabuf->dma_len = cnt;
	dmabuf->ready = 1;

	DBG("Starting playback at 0x%p for %ld bytes\n", dmabuf->rawbuf +
	    dmabuf->rd_ptr, dmabuf->dma_len);

        /* load up the current register set */
	AD1889_WRITEL(ad1889_dev, AD_DMAWAVCC, cnt);
	AD1889_WRITEL(ad1889_dev, AD_DMAWAVICC, cnt);
	AD1889_WRITEL(ad1889_dev, AD_DMAWAVCA, dmabuf->dma_handle);

	/* TODO: for now we load the base registers with the same thing */
	AD1889_WRITEL(ad1889_dev, AD_DMAWAVBC, cnt);
	AD1889_WRITEL(ad1889_dev, AD_DMAWAVIBC, cnt);
	AD1889_WRITEL(ad1889_dev, AD_DMAWAVBA, dmabuf->dma_handle);

	/* and we're off to the races... */
	AD1889_WRITEL(ad1889_dev, AD_DMACHSS, 0x8);
	tmp = AD1889_READW(ad1889_dev, AD_DSWSMC);
	tmp |= 0x0400; /* set WAEN */
	AD1889_WRITEW(ad1889_dev, AD_DSWSMC, tmp);
	(void) AD1889_READW(ad1889_dev, AD_DSWSMC); /* flush posted PCI write */

	dmabuf->enable |= DAC_RUNNING;

skip_dma:
	spin_unlock_irqrestore(&state->card->lock, flags);
}


static void ad1889_stop_wav(ad1889_state_t *state)
{
	unsigned long flags;
	struct dmabuf *dmabuf = &state->dmabuf;

	spin_lock_irqsave(&state->card->lock, flags);

	if (dmabuf->enable & DAC_RUNNING) {
		u16 tmp;
		unsigned long cnt = dmabuf->dma_len;

		tmp = AD1889_READW(ad1889_dev, AD_DSWSMC);
		tmp &= ~0x0400; /* clear WAEN */
		AD1889_WRITEW(ad1889_dev, AD_DSWSMC, tmp);
		(void) AD1889_READW(ad1889_dev, AD_DSWSMC); /* flush posted PCI write */
		pci_unmap_single(ad1889_dev->pci, dmabuf->dma_handle, 
				cnt, PCI_DMA_TODEVICE);

		dmabuf->enable &= ~DAC_RUNNING;

		/* update dma pointers */
		dmabuf->rd_ptr += cnt;
		dmabuf->rd_ptr &= (DMA_SIZE - 1);

		dmabuf->dma_handle = 0;
		dmabuf->dma_len = 0;
		dmabuf->ready = 0;

		wake_up(&dmabuf->wait);
	}

	spin_unlock_irqrestore(&state->card->lock, flags);
}


#if 0
static void ad1889_startstop_adc(ad1889_state_t *state, int start)
{
	u16 tmp;
	unsigned long flags;

	spin_lock_irqsave(&state->card->lock, flags);
	
	tmp = AD1889_READW(ad1889_dev, AD_DSRAMC);
	if (start) {
		state->dmabuf.enable |= ADC_RUNNING;
		tmp |= 0x0004; /* set ADEN */
	} else {
		state->dmabuf.enable &= ~ADC_RUNNING;
		tmp &= ~0x0004; /* clear ADEN */
	}
	AD1889_WRITEW(ad1889_dev, AD_DSRAMC, tmp);

	spin_unlock_irqrestore(&state->card->lock, flags);
}
#endif

static ad1889_dev_t *ad1889_alloc_dev(struct pci_dev *pci)
{
	ad1889_dev_t *dev;
	struct dmabuf *dmabuf;
	int i;

	if ((dev = kmalloc(sizeof(ad1889_dev_t), GFP_KERNEL)) == NULL) 
		return NULL;
	memset(dev, 0, sizeof(ad1889_dev_t));
	spin_lock_init(&dev->lock);
	dev->pci = pci;

	for (i = 0; i < AD_MAX_STATES; i++) {
		dev->state[i].card = dev;
		init_MUTEX(&dev->state[i].sem);
		init_waitqueue_head(&dev->state[i].dmabuf.wait);
	}

	/* allocate dma buffer */

	for (i = 0; i < AD_MAX_STATES; i++) {
		dmabuf = &dev->state[i].dmabuf;
		dmabuf->rawbuf = kmalloc(DMA_SIZE, GFP_KERNEL|GFP_DMA);
		if (!dmabuf->rawbuf)
			goto err_free_dmabuf;
		dmabuf->rawbuf_size = DMA_SIZE;
		dmabuf->dma_handle = 0;
		dmabuf->rd_ptr = dmabuf->wr_ptr = dmabuf->dma_len = 0UL;
		dmabuf->ready = 0;
		dmabuf->rate = 48000;
	}
	return dev;

err_free_dmabuf:
	while (--i >= 0)
		kfree(dev->state[i].dmabuf.rawbuf);
	kfree(dev);
	return NULL;
}

static void ad1889_free_dev(ad1889_dev_t *dev)
{
	int j;
	struct dmabuf *dmabuf;

	if (dev == NULL) 
		return;

	if (dev->ac97_codec)
		ac97_release_codec(dev->ac97_codec);

	for (j = 0; j < AD_MAX_STATES; j++) {
		dmabuf = &dev->state[j].dmabuf;
		kfree(dmabuf->rawbuf);
	}

	kfree(dev);
}

static inline void ad1889_trigger_playback(ad1889_dev_t *dev)
{
#if 0
	u32 val;
	struct dmabuf *dmabuf = &dev->state[AD_WAV_STATE].dmabuf;
#endif

	ad1889_start_wav(&dev->state[AD_WAV_STATE]);
}

static int ad1889_read_proc (char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	char *out = page;
	int len, i;
	ad1889_dev_t *dev = data;
	ad1889_reg_t regs[] = {
		{ "WSMC", AD_DSWSMC, 16 },
		{ "RAMC", AD_DSRAMC, 16 },
		{ "WADA", AD_DSWADA, 16 },
		{ "SYDA", AD_DSSYDA, 16 },
		{ "WAS", AD_DSWAS, 16 },
		{ "RES", AD_DSRES, 16 },
		{ "CCS", AD_DSCCS, 16 },
		{ "ADCBA", AD_DMAADCBA, 32 },
		{ "ADCCA", AD_DMAADCCA, 32 },
		{ "ADCBC", AD_DMAADCBC, 32 },
		{ "ADCCC", AD_DMAADCCC, 32 },
		{ "ADCIBC", AD_DMAADCIBC, 32 },
		{ "ADCICC", AD_DMAADCICC, 32 },
		{ "ADCCTRL", AD_DMAADCCTRL, 16 },
		{ "WAVBA", AD_DMAWAVBA, 32 },
		{ "WAVCA", AD_DMAWAVCA, 32 },
		{ "WAVBC", AD_DMAWAVBC, 32 },
		{ "WAVCC", AD_DMAWAVCC, 32 },
		{ "WAVIBC", AD_DMAWAVIBC, 32 },
		{ "WAVICC", AD_DMAWAVICC, 32 },
		{ "WAVCTRL", AD_DMAWAVCTRL, 16 },
		{ "DISR", AD_DMADISR, 32 },
		{ "CHSS", AD_DMACHSS, 32 },
		{ "IPC", AD_GPIOIPC, 16 },
		{ "OP", AD_GPIOOP, 16 },
		{ "IP", AD_GPIOIP, 16 },
		{ "ACIC", AD_ACIC, 16 },
		{ "AC97_RESET", 0x100 + AC97_RESET, 16 },
		{ "AC97_MASTER_VOL_STEREO", 0x100 + AC97_MASTER_VOL_STEREO, 16 },
		{ "AC97_HEADPHONE_VOL", 0x100 + AC97_HEADPHONE_VOL, 16 },
		{ "AC97_MASTER_VOL_MONO", 0x100 + AC97_MASTER_VOL_MONO, 16 },
		{ "AC97_MASTER_TONE", 0x100 + AC97_MASTER_TONE, 16 },
		{ "AC97_PCBEEP_VOL", 0x100 + AC97_PCBEEP_VOL, 16 },
		{ "AC97_PHONE_VOL", 0x100 + AC97_PHONE_VOL, 16 },
		{ "AC97_MIC_VOL", 0x100 + AC97_MIC_VOL, 16 },
		{ "AC97_LINEIN_VOL", 0x100 + AC97_LINEIN_VOL, 16 },
		{ "AC97_CD_VOL", 0x100 + AC97_CD_VOL, 16 },
		{ "AC97_VIDEO_VOL", 0x100 + AC97_VIDEO_VOL, 16 },
		{ "AC97_AUX_VOL", 0x100 + AC97_AUX_VOL, 16 },
		{ "AC97_PCMOUT_VOL", 0x100 + AC97_PCMOUT_VOL, 16 },
		{ "AC97_RECORD_SELECT", 0x100 + AC97_RECORD_SELECT, 16 },
		{ "AC97_RECORD_GAIN", 0x100 + AC97_RECORD_GAIN, 16 },
		{ "AC97_RECORD_GAIN_MIC", 0x100 + AC97_RECORD_GAIN_MIC, 16 },
		{ "AC97_GENERAL_PURPOSE", 0x100 + AC97_GENERAL_PURPOSE, 16 },
		{ "AC97_3D_CONTROL", 0x100 + AC97_3D_CONTROL, 16 },
		{ "AC97_MODEM_RATE", 0x100 + AC97_MODEM_RATE, 16 },
		{ "AC97_POWER_CONTROL", 0x100 + AC97_POWER_CONTROL, 16 },
		{ NULL }
	};

	if (dev == NULL)
		return -ENODEV;

	for (i = 0; regs[i].name != 0; i++)
		out += sprintf(out, "%s: 0x%0*x\n", regs[i].name, 
			regs[i].width >> 2, 
			(regs[i].width == 16 
			 	? AD1889_READW(dev, regs[i].offset)
				: AD1889_READL(dev, regs[i].offset)));

	for (i = 0; i < AD_MAX_STATES; i++) {
		out += sprintf(out, "DMA status for %s:\n", 
			(i == AD_WAV_STATE ? "WAV" : "ADC")); 
		out += sprintf(out, "\t\t0x%p (IOVA: 0x%llu)\n",
			dev->state[i].dmabuf.rawbuf,
			(unsigned long long)dev->state[i].dmabuf.dma_handle);

		out += sprintf(out, "\tread ptr: offset %u\n", 
			(unsigned int)dev->state[i].dmabuf.rd_ptr);
		out += sprintf(out, "\twrite ptr: offset %u\n", 
			(unsigned int)dev->state[i].dmabuf.wr_ptr);
		out += sprintf(out, "\tdma len: offset %u\n", 
			(unsigned int)dev->state[i].dmabuf.dma_len);
	}

	len = out - page - off;
	if (len < count) {
		*eof = 1;
		if (len <= 0) return 0;
	} else {
		len = count;
	}
	*start = page + off;
	return len;
}

/***************************** DMA interfaces ************************** */
#if 0
static inline unsigned long ad1889_get_dma_addr(ad1889_state_t *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 offset;

	if (!(dmabuf->enable & (DAC_RUNNING | ADC_RUNNING))) {
		printk(KERN_ERR DEVNAME ": get_dma_addr called without dma enabled\n");
		return 0;
	}
	
	if (dmabuf->enable & DAC_RUNNING)
		offset = le32_to_cpu(AD1889_READL(state->card, AD_DMAWAVBA));
	else
		offset = le32_to_cpu(AD1889_READL(state->card, AD_DMAADCBA));

	return (unsigned long)bus_to_virt((unsigned long)offset) - (unsigned long)dmabuf->rawbuf;
}

static void ad1889_update_ptr(ad1889_dev_t *dev, int wake)
{
	ad1889_state_t *state;
	struct dmabuf *dmabuf;
	unsigned long hwptr;
	int diff;

	/* check ADC first */
	state = &dev->adc_state;
	dmabuf = &state->dmabuf;
	if (dmabuf->enable & ADC_RUNNING) {
		hwptr = ad1889_get_dma_addr(state);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;

		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count += diff;
		if (dmabuf->count > dmabuf->dmasize)
			dmabuf->count = dmabuf->dmasize;

		if (dmabuf->mapped) {
			if (wake & dmabuf->count >= dmabuf->fragsize)
				wake_up(&dmabuf->wait);
		} else {
			if (wake & dmabuf->count > 0)
				wake_up(&dmabuf->wait);
		}
	}

	/* check DAC */
	state = &dev->wav_state;
	dmabuf = &state->dmabuf;
	if (dmabuf->enable & DAC_RUNNING) {
XXX

}
#endif

/************************* /dev/dsp interfaces ************************* */

static ssize_t ad1889_read(struct file *file, char __user *buffer, size_t count,
	loff_t *ppos)
{
	return 0;
}

static ssize_t ad1889_write(struct file *file, const char __user *buffer, size_t count,
	loff_t *ppos)
{
	ad1889_dev_t *dev = (ad1889_dev_t *)file->private_data;
	ad1889_state_t *state = &dev->state[AD_WAV_STATE];
	volatile struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	down(&state->sem);
#if 0
	if (dmabuf->mapped) {
		ret = -ENXIO;
		goto err1;
	}
#endif
	if (!access_ok(VERIFY_READ, buffer, count)) {
		ret = -EFAULT;
		goto err1;
	}

	add_wait_queue(&state->dmabuf.wait, &wait);

	/* start filling dma buffer.... */
	while (count > 0) {
		long rem;
		long cnt = count;
		unsigned long flags;

		for (;;) {
			long used_bytes;
			long timeout;	/* max time for DMA in jiffies */

			/* buffer is full if wr catches up to rd */
			spin_lock_irqsave(&state->card->lock, flags);
			used_bytes = dmabuf->wr_ptr - dmabuf->rd_ptr;
			timeout = (dmabuf->dma_len * HZ) / dmabuf->rate;
			spin_unlock_irqrestore(&state->card->lock, flags);

			/* adjust for buffer wrap around */
			used_bytes = (used_bytes + DMA_SIZE) & (DMA_SIZE - 1);

			/* If at least one page unused */
			if (used_bytes < (DMA_SIZE - 0x1000))
				break;

			/* dma buffer full */

			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				goto err2;
			}

			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(timeout + 1);
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				goto err2;
			}
		}

		/* watch out for wrapping around static buffer */
		spin_lock_irqsave(&state->card->lock, flags);
		rem = DMA_SIZE - dmabuf->wr_ptr;
		if (cnt > rem)
			cnt = rem;

		rem = dmabuf->wr_ptr;

		/* update dma pointers */
		dmabuf->wr_ptr += cnt;
		dmabuf->wr_ptr &= DMA_SIZE - 1;	/* wrap ptr if necessary */
		spin_unlock_irqrestore(&state->card->lock, flags);

		/* transfer unwrapped chunk */
		if (copy_from_user(dmabuf->rawbuf + rem, buffer, cnt)) {
			ret = -EFAULT;
			goto err2;
		}

		DBG("Writing 0x%lx bytes to +0x%lx\n", cnt, rem);

		/* update counters */
		count -= cnt;
		buffer += cnt;
		ret += cnt;

		/* we have something to play - go play it! */
		ad1889_trigger_playback(dev);
	}

err2:
	remove_wait_queue(&state->dmabuf.wait, &wait);
err1:
	up(&state->sem);
	return ret;
}

static unsigned int ad1889_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
#if 0
	ad1889_dev_t *dev = (ad1889_dev_t *)file->private_data;
	ad1889_state_t *state = NULL;
	struct dmabuf *dmabuf;
	unsigned long flags;
	
	if (!(file->f_mode & (FMODE_READ | FMODE_WRITE)))
		return -EINVAL;

	if (file->f_mode & FMODE_WRITE) {
		state = &dev->state[AD_WAV_STATE];
		if (!state) return 0;
		dmabuf = &state->dmabuf;
		poll_wait(file, &dmabuf->wait, wait);
	}

	if (file->f_mode & FMODE_READ) {
		state = &dev->state[AD_ADC_STATE];
		if (!state) return 0;
		dmabuf = &state->dmabuf;
		poll_wait(file, &dmabuf->wait, wait);
	}

	spin_lock_irqsave(&dev->lock, flags);
	ad1889_update_ptr(dev, 0);

	if (file->f_mode & FMODE_WRITE) {
		state = &dev->state[WAV_STATE];
		dmabuf = &state->dmabuf;
		if (dmabuf->mapped) {
			if (dmabuf->count >= (int)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((int)dmabuf->dmasize >= dmabuf->count + 
				(int)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}

	if (file ->f_mode & FMODE_READ) {
		state = &dev->state[AD_ADC_STATE];
		dmabuf = &state->dmabuf;
		if (dmabuf->count >= (int)dmabuf->fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

#endif
	return mask;
}

static int ad1889_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static int ad1889_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	int val = 0;
	ad1889_dev_t *dev = (ad1889_dev_t *)file->private_data;
	struct dmabuf *dmabuf;
	audio_buf_info abinfo;
	int __user *p = (int __user *)arg;

	DBG("ad1889_ioctl cmd 0x%x arg %lu\n", cmd, arg);

	switch (cmd)
	{
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_RESET:
		break;

	case SNDCTL_DSP_SYNC:
		break;

	case SNDCTL_DSP_SPEED:
		/* set sampling rate */
		if (get_user(val, p))
			return -EFAULT;
		if (val > 5400 && val < 48000)
		{
			if (file->f_mode & FMODE_WRITE)
				AD1889_WRITEW(ad1889_dev, AD_DSWAS, val);
			if (file->f_mode & FMODE_READ)
				AD1889_WRITEW(ad1889_dev, AD_DSRES, val);
		}
		return 0;

	case SNDCTL_DSP_STEREO: /* undocumented? */
		if (get_user(val, p))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			val = AD1889_READW(ad1889_dev, AD_DSWSMC);
			if (val) {
				val |= 0x0200;  /* set WAST */
			} else {
				val &= ~0x0200; /* clear WAST */
			}
			AD1889_WRITEW(ad1889_dev, AD_DSWSMC, val);
		}
		if (file->f_mode & FMODE_WRITE) {
			val = AD1889_READW(ad1889_dev, AD_DSRAMC);
			if (val) {
				val |= 0x0002;  /* set ADST */
			} else {
				val &= ~0x0002; /* clear ADST */
			}
			AD1889_WRITEW(ad1889_dev, AD_DSRAMC, val);
		}

		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		return put_user(DMA_SIZE, p);

	case SNDCTL_DSP_GETFMTS:
		return put_user(AFMT_S16_LE|AFMT_U8, p);

	case SNDCTL_DSP_SETFMT:
		if (get_user(val, p))
			return -EFAULT;

		if (val == 0) {
			if (file->f_mode & FMODE_READ) 
				ad1889_set_adc_fmt(dev, val);

			if (file->f_mode & FMODE_WRITE) 
				ad1889_set_wav_fmt(dev, val);
		} else {
			val = AFMT_S16_LE | AFMT_U8;
		}

		return put_user(val, p);

	case SNDCTL_DSP_CHANNELS:
		break;

	case SNDCTL_DSP_POST:
		/* send all data to device */
		break;

	case SNDCTL_DSP_SUBDIVIDE:
		break;

	case SNDCTL_DSP_SETFRAGMENT:
		/* not supported; uses fixed fragment sizes */
		return put_user(DMA_SIZE, p);

	case SNDCTL_DSP_GETOSPACE:
	case SNDCTL_DSP_GETISPACE:
		/* space left in dma buffers */
		if (cmd == SNDCTL_DSP_GETOSPACE)
			dmabuf = &dev->state[AD_WAV_STATE].dmabuf;
		else
			dmabuf = &dev->state[AD_ADC_STATE].dmabuf;
		abinfo.fragments = 1;
		abinfo.fragstotal = 1;
		abinfo.fragsize = DMA_SIZE;
		abinfo.bytes = DMA_SIZE;
		return copy_to_user(p, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(0, p);

	case SNDCTL_DSP_GETTRIGGER:
	case SNDCTL_DSP_SETTRIGGER:
		break;

	case SNDCTL_DSP_GETIPTR:
	case SNDCTL_DSP_GETOPTR:
		break;

	case SNDCTL_DSP_SETDUPLEX:
		break;
	
	case SNDCTL_DSP_GETODELAY:
		break;

	case SOUND_PCM_READ_RATE:
		return put_user(AD1889_READW(ad1889_dev, AD_DSWAS), p);

	case SOUND_PCM_READ_CHANNELS:
	case SOUND_PCM_READ_BITS:
		break;

	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		break;

	default:
		break;
	}

	return -ENOTTY;
}

static int ad1889_open(struct inode *inode, struct file *file)
{
	/* check minor; only support /dev/dsp atm */
	if (iminor(inode) != 3)
		return -ENXIO;
	
	file->private_data = ad1889_dev;

	ad1889_set_wav_rate(ad1889_dev, 48000);
	ad1889_set_wav_fmt(ad1889_dev, AFMT_S16_LE);
	AD1889_WRITEW(ad1889_dev, AD_DSWADA, 0x0404); /* attenuation */
	return nonseekable_open(inode, file);
}

static int ad1889_release(struct inode *inode, struct file *file)
{
	/* if we have state free it here */
	return 0;
}

static struct file_operations ad1889_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= ad1889_read,
	.write		= ad1889_write,
	.poll		= ad1889_poll,
	.ioctl		= ad1889_ioctl,
	.mmap		= ad1889_mmap,
	.open		= ad1889_open,
	.release	= ad1889_release,
};

/************************* /dev/mixer interfaces ************************ */
static int ad1889_mixer_open(struct inode *inode, struct file *file)
{
	if (ad1889_dev->ac97_codec->dev_mixer != iminor(inode))
		return -ENODEV;

	file->private_data = ad1889_dev->ac97_codec;
	return 0;
}

static int ad1889_mixer_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int ad1889_mixer_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;
	return codec->mixer_ioctl(codec, cmd, arg);
}

static struct file_operations ad1889_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= ad1889_mixer_ioctl,
	.open		= ad1889_mixer_open,
	.release	= ad1889_mixer_release,
};

/************************* AC97 interfaces ****************************** */
static void ad1889_codec_write(struct ac97_codec *ac97, u8 reg, u16 val)
{
	ad1889_dev_t *dev = ac97->private_data;

	//DBG("Writing 0x%x to 0x%lx\n", val, dev->regbase + 0x100 + reg);
	AD1889_WRITEW(dev, 0x100 + reg, val);
}

static u16 ad1889_codec_read(struct ac97_codec *ac97, u8 reg)
{
	ad1889_dev_t *dev = ac97->private_data;
	//DBG("Reading from 0x%lx\n", dev->regbase + 0x100 + reg);
	return AD1889_READW(dev, 0x100 + reg);
}	

static int ad1889_ac97_init(ad1889_dev_t *dev, int id)
{
	struct ac97_codec *ac97;
	u16 eid;

	if ((ac97 = ac97_alloc_codec()) == NULL) 
		return -ENOMEM;

	ac97->private_data = dev;
	ac97->id = id;

	ac97->codec_read = ad1889_codec_read;
	ac97->codec_write = ad1889_codec_write;

	if (ac97_probe_codec(ac97) == 0) {
		printk(DEVNAME ": ac97_probe_codec failed\n");
		goto out_free;
	}

	eid = ad1889_codec_read(ac97, AC97_EXTENDED_ID);
	if (eid == 0xffff) {
		printk(KERN_WARNING DEVNAME ": no codec attached?\n");
		goto out_free;
	}

	dev->ac97_features = eid;

	if ((ac97->dev_mixer = register_sound_mixer(&ad1889_mixer_fops, -1)) < 0) {
		printk(KERN_ERR DEVNAME ": cannot register mixer\n");
		goto out_free;
	}

	dev->ac97_codec = ac97;
	return 0;

out_free:
	ac97_release_codec(ac97);
	return -ENODEV;
}

static int ad1889_aclink_reset(struct pci_dev * pcidev)
{
	u16 stat;
	int retry = 200;
	ad1889_dev_t *dev = pci_get_drvdata(pcidev);

	AD1889_WRITEW(dev, AD_DSCCS, 0x8000); /* turn on clock */
	AD1889_READW(dev, AD_DSCCS); 

	WAIT_10MS();

	stat = AD1889_READW(dev, AD_ACIC);
	stat |= 0x0002;				/* Reset Disable */
	AD1889_WRITEW(dev, AD_ACIC, stat);
	(void) AD1889_READW(dev, AD_ACIC);	/* flush posted write */

	udelay(10);

	stat = AD1889_READW(dev, AD_ACIC);
	stat |= 0x0001;				/* Interface Enable */
	AD1889_WRITEW(dev, AD_ACIC, stat);

	do {
		if (AD1889_READW(dev, AD_ACIC) & 0x8000)	/* Ready */
			break;
		WAIT_10MS();
		retry--;
	} while (retry > 0);

	if (!retry) {
		printk(KERN_ERR "ad1889_aclink_reset: codec is not ready [0x%x]\n",
			    AD1889_READW(dev, AD_ACIC));
		return -EBUSY;
	}

	/* TODO reset AC97 codec */
	/* TODO set wave/adc pci ctrl status */

	stat = AD1889_READW(dev, AD_ACIC);
	stat |= 0x0004;				/* Audio Stream Output Enable */
	AD1889_WRITEW(dev, AD_ACIC, stat);
	return 0;
}

/************************* PCI interfaces ****************************** */
/* PCI device table */
static struct pci_device_id ad1889_id_tbl[] = {
	{ PCI_VENDOR_ID_ANALOG_DEVICES, PCI_DEVICE_ID_AD1889JS, PCI_ANY_ID, 
	  PCI_ANY_ID, 0, 0, (unsigned long)DEVNAME },
	{ },
};
MODULE_DEVICE_TABLE(pci, ad1889_id_tbl);

static irqreturn_t ad1889_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 stat;
	ad1889_dev_t *dev = (ad1889_dev_t *)dev_id;

	stat = AD1889_READL(dev, AD_DMADISR);

	/* clear ISR */
	AD1889_WRITEL(dev, AD_DMADISR, stat);

	if (stat & 0x8) {		/* WAVI */
		DBG("WAV interrupt\n");
		dev->stats.wav_intrs++;
		if (dev->state[AD_WAV_STATE].dmabuf.ready) {
			ad1889_stop_wav(&dev->state[AD_WAV_STATE]);	/* clean up */
			ad1889_start_wav(&dev->state[AD_WAV_STATE]);	/* start new */
		}
	}

	if ((stat & 0x2) && dev->state[AD_ADC_STATE].dmabuf.ready) { /* ADCI */
		DBG("ADC interrupt\n");
		dev->stats.adc_intrs++;
	}
	if(stat)
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static void ad1889_initcfg(ad1889_dev_t *dev)
{
	u16 tmp16;
	u32 tmp32;

	/* make sure the interrupt bits are setup the way we want */
	tmp32 = AD1889_READL(dev, AD_DMAWAVCTRL);
	tmp32 &= ~0xff; /* flat dma, no sg, mask out the intr bits */
	tmp32 |= 0x6;  /* intr on count, loop */
	AD1889_WRITEL(dev, AD_DMAWAVCTRL, tmp32);

	/* unmute... */
	tmp16 = AD1889_READW(dev, AD_DSWADA);
	tmp16 &= ~0x8080;
	AD1889_WRITEW(dev, AD_DSWADA, tmp16);
}

static int __devinit ad1889_probe(struct pci_dev *pcidev, const struct pci_device_id *ent)
{
	int err;
	ad1889_dev_t *dev;
	unsigned long bar;
	struct proc_dir_entry *proc_root = NULL;

	if ((err = pci_enable_device(pcidev)) != 0) {
		printk(KERN_ERR DEVNAME ": pci_enable_device failed\n");
		return err;
	}

	pci_set_master(pcidev);
	if ((dev = ad1889_alloc_dev(pcidev)) == NULL) {
		printk(KERN_ERR DEVNAME ": cannot allocate memory for device\n");
		return -ENOMEM;
	}
	pci_set_drvdata(pcidev, dev);
	bar = pci_resource_start(pcidev, 0);
	
        if (!(pci_resource_flags(pcidev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR DEVNAME ": memory region not assigned\n");
		goto out1;
	}

	if (pci_request_region(pcidev, 0, DEVNAME)) {
		printk(KERN_ERR DEVNAME ": unable to request memory region\n");
		goto out1;
	}

	dev->regbase = ioremap_nocache(bar, AD_DSIOMEMSIZE);
	if (!dev->regbase) {
		printk(KERN_ERR DEVNAME ": unable to remap iomem\n");
		goto out2;
	}

	if (request_irq(pcidev->irq, ad1889_interrupt, SA_SHIRQ, DEVNAME, dev) != 0) {
		printk(KERN_ERR DEVNAME ": unable to request interrupt\n");
		goto out3;
	}

	printk(KERN_INFO DEVNAME ": %s at %p IRQ %d\n",
		(char *)ent->driver_data, dev->regbase, pcidev->irq);

	if (ad1889_aclink_reset(pcidev) != 0)
		goto out4;

	/* register /dev/dsp */
	if ((dev->dev_audio = register_sound_dsp(&ad1889_fops, -1)) < 0) {
		printk(KERN_ERR DEVNAME ": cannot register /dev/dsp\n");
		goto out4;
	}

	if ((err = ad1889_ac97_init(dev, 0)) != 0)
		goto out5;

	/* XXX: cleanups */
	if (((proc_root = proc_mkdir("driver/ad1889", NULL)) == NULL) ||
	    create_proc_read_entry("ac97", S_IFREG|S_IRUGO, proc_root, ac97_read_proc, dev->ac97_codec) == NULL ||
	    create_proc_read_entry("info", S_IFREG|S_IRUGO, proc_root, ad1889_read_proc, dev) == NULL) 
		goto out5;
	
	ad1889_initcfg(dev);

	//DBG(DEVNAME ": Driver initialization done!\n");

	ad1889_dev = dev;

	return 0;

out5:
	unregister_sound_dsp(dev->dev_audio);
out4:
	free_irq(pcidev->irq, dev);
out3:
	iounmap(dev->regbase);
out2:
	pci_release_region(pcidev, 0);
out1:
	ad1889_free_dev(dev);
	pci_set_drvdata(pcidev, NULL);

	return -ENODEV;
}

static void __devexit ad1889_remove(struct pci_dev *pcidev)
{
	ad1889_dev_t *dev = pci_get_drvdata(pcidev);

	if (dev == NULL) return;
	
	unregister_sound_mixer(dev->ac97_codec->dev_mixer);
	unregister_sound_dsp(dev->dev_audio);
	free_irq(pcidev->irq, dev);
	iounmap(dev->regbase);
	pci_release_region(pcidev, 0);

	/* any hw programming needed? */
	ad1889_free_dev(dev);
	pci_set_drvdata(pcidev, NULL);
}

MODULE_AUTHOR("Randolph Chung");
MODULE_DESCRIPTION("Analog Devices AD1889 PCI Audio");
MODULE_LICENSE("GPL");

static struct pci_driver ad1889_driver = {
	.name		= DEVNAME,
	.id_table	= ad1889_id_tbl,
	.probe		= ad1889_probe,
	.remove		= __devexit_p(ad1889_remove),
};

static int __init ad1889_init_module(void)
{
	return pci_register_driver(&ad1889_driver);
}

static void ad1889_exit_module(void)
{
	pci_unregister_driver(&ad1889_driver);
	return;
}

module_init(ad1889_init_module);
module_exit(ad1889_exit_module);
