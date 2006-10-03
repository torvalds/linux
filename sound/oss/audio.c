/*
 * sound/oss/audio.c
 *
 * Device file manager for /dev/audio
 */

/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 * Thomas Sailer   : moved several static variables into struct audio_operations
 *                   (which is grossly misnamed btw.) because they have the same
 *                   lifetime as the rest in there and dynamic allocation saves
 *                   12k or so
 * Thomas Sailer   : use more logical O_NONBLOCK semantics
 * Daniel Rodriksson: reworked the use of the device specific copy_user
 *                    still generic
 * Horst von Brand:  Add missing #include <linux/string.h>
 * Chris Rankin    : Update the module-usage counter for the coprocessor,
 *                   and decrement the counters again if we cannot open
 *                   the audio device.
 */

#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/kmod.h>

#include "sound_config.h"
#include "ulaw.h"
#include "coproc.h"

#define NEUTRAL8	0x80
#define NEUTRAL16	0x00


static int             dma_ioctl(int dev, unsigned int cmd, void __user *arg);

static int set_format(int dev, int fmt)
{
	if (fmt != AFMT_QUERY)
	{
		audio_devs[dev]->local_conversion = 0;

		if (!(audio_devs[dev]->format_mask & fmt))	/* Not supported */
		{
			if (fmt == AFMT_MU_LAW)
			{
				fmt = AFMT_U8;
				audio_devs[dev]->local_conversion = CNV_MU_LAW;
			}
			else
				fmt = AFMT_U8;	/* This is always supported */
		}
		audio_devs[dev]->audio_format = audio_devs[dev]->d->set_bits(dev, fmt);
		audio_devs[dev]->local_format = fmt;
	}
	else
		return audio_devs[dev]->local_format;

	if (audio_devs[dev]->local_conversion)
		return audio_devs[dev]->local_conversion;
	else 
		return audio_devs[dev]->local_format;
}

int audio_open(int dev, struct file *file)
{
	int ret;
	int bits;
	int dev_type = dev & 0x0f;
	int mode = translate_mode(file);
	const struct audio_driver *driver;
	const struct coproc_operations *coprocessor;

	dev = dev >> 4;

	if (dev_type == SND_DEV_DSP16)
		bits = 16;
	else
		bits = 8;

	if (dev < 0 || dev >= num_audiodevs)
		return -ENXIO;

	driver = audio_devs[dev]->d;

	if (!try_module_get(driver->owner))
		return -ENODEV;

	if ((ret = DMAbuf_open(dev, mode)) < 0)
		goto error_1;

	if ( (coprocessor = audio_devs[dev]->coproc) != NULL ) {
		if (!try_module_get(coprocessor->owner))
			goto error_2;

		if ((ret = coprocessor->open(coprocessor->devc, COPR_PCM)) < 0) {
			printk(KERN_WARNING "Sound: Can't access coprocessor device\n");
			goto error_3;
		}
	}
	
	audio_devs[dev]->local_conversion = 0;

	if (dev_type == SND_DEV_AUDIO)
		set_format(dev, AFMT_MU_LAW);
	else 
		set_format(dev, bits);

	audio_devs[dev]->audio_mode = AM_NONE;

	return 0;

	/*
	 * Clean-up stack: this is what needs (un)doing if
	 * we can't open the audio device ...
	 */
	error_3:
	module_put(coprocessor->owner);

	error_2:
	DMAbuf_release(dev, mode);

	error_1:
	module_put(driver->owner);

	return ret;
}

static void sync_output(int dev)
{
	int             p, i;
	int             l;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	if (dmap->fragment_size <= 0)
		return;
	dmap->flags |= DMA_POST;

	/* Align the write pointer with fragment boundaries */
	
	if ((l = dmap->user_counter % dmap->fragment_size) > 0)
	{
		int len;
		unsigned long offs = dmap->user_counter % dmap->bytes_in_use;

		len = dmap->fragment_size - l;
		memset(dmap->raw_buf + offs, dmap->neutral_byte, len);
		DMAbuf_move_wrpointer(dev, len);
	}
	
	/*
	 * Clean all unused buffer fragments.
	 */

	p = dmap->qtail;
	dmap->flags |= DMA_POST;

	for (i = dmap->qlen + 1; i < dmap->nbufs; i++)
	{
		p = (p + 1) % dmap->nbufs;
		if (((dmap->raw_buf + p * dmap->fragment_size) + dmap->fragment_size) >
			(dmap->raw_buf + dmap->buffsize))
				printk(KERN_ERR "audio: Buffer error 2\n");

		memset(dmap->raw_buf + p * dmap->fragment_size,
			dmap->neutral_byte,
			dmap->fragment_size);
	}

	dmap->flags |= DMA_DIRTY;
}

void audio_release(int dev, struct file *file)
{
	const struct coproc_operations *coprocessor;
	int mode = translate_mode(file);

	dev = dev >> 4;

	/*
	 * We do this in DMAbuf_release(). Why are we doing it
	 * here? Why don't we test the file mode before setting
	 * both flags? DMAbuf_release() does.
	 * ...pester...pester...pester...
	 */
	audio_devs[dev]->dmap_out->closing = 1;
	audio_devs[dev]->dmap_in->closing = 1;

	/*
	 * We need to make sure we allocated the dmap_out buffer
	 * before we go mucking around with it in sync_output().
	 */
	if (mode & OPEN_WRITE)
		sync_output(dev);

	if ( (coprocessor = audio_devs[dev]->coproc) != NULL ) {
		coprocessor->close(coprocessor->devc, COPR_PCM);
		module_put(coprocessor->owner);
	}
	DMAbuf_release(dev, mode);

	module_put(audio_devs[dev]->d->owner);
}

static void translate_bytes(const unsigned char *table, unsigned char *buff, int n)
{
	unsigned long   i;

	if (n <= 0)
		return;

	for (i = 0; i < n; ++i)
		buff[i] = table[buff[i]];
}

int audio_write(int dev, struct file *file, const char __user *buf, int count)
{
	int c, p, l, buf_size, used, returned;
	int err;
	char *dma_buf;

	dev = dev >> 4;

	p = 0;
	c = count;
	
	if(count < 0)
		return -EINVAL;

	if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
		return -EPERM;

	if (audio_devs[dev]->flags & DMA_DUPLEX)
		audio_devs[dev]->audio_mode |= AM_WRITE;
	else
		audio_devs[dev]->audio_mode = AM_WRITE;

	if (!count)		/* Flush output */
	{
		  sync_output(dev);
		  return 0;
	}
	
	while (c)
	{
		if ((err = DMAbuf_getwrbuffer(dev, &dma_buf, &buf_size, !!(file->f_flags & O_NONBLOCK))) < 0)
		{
			    /* Handle nonblocking mode */
			if ((file->f_flags & O_NONBLOCK) && err == -EAGAIN)
				return p? p : -EAGAIN;	/* No more space. Return # of accepted bytes */
			return err;
		}
		l = c;

		if (l > buf_size)
			l = buf_size;

		returned = l;
		used = l;
		if (!audio_devs[dev]->d->copy_user)
		{
			if ((dma_buf + l) >
				(audio_devs[dev]->dmap_out->raw_buf + audio_devs[dev]->dmap_out->buffsize))
			{
				printk(KERN_ERR "audio: Buffer error 3 (%lx,%d), (%lx, %d)\n", (long) dma_buf, l, (long) audio_devs[dev]->dmap_out->raw_buf, (int) audio_devs[dev]->dmap_out->buffsize);
				return -EDOM;
			}
			if (dma_buf < audio_devs[dev]->dmap_out->raw_buf)
			{
				printk(KERN_ERR "audio: Buffer error 13 (%lx<%lx)\n", (long) dma_buf, (long) audio_devs[dev]->dmap_out->raw_buf);
				return -EDOM;
			}
			if(copy_from_user(dma_buf, &(buf)[p], l))
				return -EFAULT;
		} 
		else audio_devs[dev]->d->copy_user (dev,
						dma_buf, 0,
						buf, p,
						c, buf_size,
						&used, &returned,
						l);
		l = returned;

		if (audio_devs[dev]->local_conversion & CNV_MU_LAW)
		{
			translate_bytes(ulaw_dsp, (unsigned char *) dma_buf, l);
		}
		c -= used;
		p += used;
		DMAbuf_move_wrpointer(dev, l);

	}

	return count;
}

int audio_read(int dev, struct file *file, char __user *buf, int count)
{
	int             c, p, l;
	char           *dmabuf;
	int             buf_no;

	dev = dev >> 4;
	p = 0;
	c = count;

	if (!(audio_devs[dev]->open_mode & OPEN_READ))
		return -EPERM;

	if ((audio_devs[dev]->audio_mode & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
		sync_output(dev);

	if (audio_devs[dev]->flags & DMA_DUPLEX)
		audio_devs[dev]->audio_mode |= AM_READ;
	else
		audio_devs[dev]->audio_mode = AM_READ;

	while(c)
	{
		if ((buf_no = DMAbuf_getrdbuffer(dev, &dmabuf, &l, !!(file->f_flags & O_NONBLOCK))) < 0)
		{
			/*
			 *	Nonblocking mode handling. Return current # of bytes
			 */

			if (p > 0) 		/* Avoid throwing away data */
				return p;	/* Return it instead */

			if ((file->f_flags & O_NONBLOCK) && buf_no == -EAGAIN)
				return -EAGAIN;

			return buf_no;
		}
		if (l > c)
			l = c;

		/*
		 * Insert any local processing here.
		 */

		if (audio_devs[dev]->local_conversion & CNV_MU_LAW)
		{
			translate_bytes(dsp_ulaw, (unsigned char *) dmabuf, l);
		}
		
		{
			char           *fixit = dmabuf;

			if(copy_to_user(&(buf)[p], fixit, l))
				return -EFAULT;
		};

		DMAbuf_rmchars(dev, buf_no, l);

		p += l;
		c -= l;
	}

	return count - c;
}

int audio_ioctl(int dev, struct file *file, unsigned int cmd, void __user *arg)
{
	int val, count;
	unsigned long flags;
	struct dma_buffparms *dmap;
	int __user *p = arg;

	dev = dev >> 4;

	if (_IOC_TYPE(cmd) == 'C')	{
		if (audio_devs[dev]->coproc)	/* Coprocessor ioctl */
			return audio_devs[dev]->coproc->ioctl(audio_devs[dev]->coproc->devc, cmd, arg, 0);
		/* else
		        printk(KERN_DEBUG"/dev/dsp%d: No coprocessor for this device\n", dev); */
		return -ENXIO;
	}
	else switch (cmd) 
	{
		case SNDCTL_DSP_SYNC:
			if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				return 0;
			if (audio_devs[dev]->dmap_out->fragment_size == 0)
				return 0;
			sync_output(dev);
			DMAbuf_sync(dev);
			DMAbuf_reset(dev);
			return 0;

		case SNDCTL_DSP_POST:
			if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				return 0;
			if (audio_devs[dev]->dmap_out->fragment_size == 0)
				return 0;
			audio_devs[dev]->dmap_out->flags |= DMA_POST | DMA_DIRTY;
			sync_output(dev);
			dma_ioctl(dev, SNDCTL_DSP_POST, NULL);
			return 0;

		case SNDCTL_DSP_RESET:
			audio_devs[dev]->audio_mode = AM_NONE;
			DMAbuf_reset(dev);
			return 0;

		case SNDCTL_DSP_GETFMTS:
			val = audio_devs[dev]->format_mask | AFMT_MU_LAW;
			break;
	
		case SNDCTL_DSP_SETFMT:
			if (get_user(val, p))
				return -EFAULT;
			val = set_format(dev, val);
			break;

		case SNDCTL_DSP_GETISPACE:
			if (!(audio_devs[dev]->open_mode & OPEN_READ))
				return 0;
  			if ((audio_devs[dev]->audio_mode & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
  				return -EBUSY;
			return dma_ioctl(dev, cmd, arg);

		case SNDCTL_DSP_GETOSPACE:
			if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				return -EPERM;
  			if ((audio_devs[dev]->audio_mode & AM_READ) && !(audio_devs[dev]->flags & DMA_DUPLEX))
  				return -EBUSY;
			return dma_ioctl(dev, cmd, arg);
		
		case SNDCTL_DSP_NONBLOCK:
			file->f_flags |= O_NONBLOCK;
			return 0;

		case SNDCTL_DSP_GETCAPS:
				val = 1 | DSP_CAP_MMAP;	/* Revision level of this ioctl() */
				if (audio_devs[dev]->flags & DMA_DUPLEX &&
					audio_devs[dev]->open_mode == OPEN_READWRITE)
					val |= DSP_CAP_DUPLEX;
				if (audio_devs[dev]->coproc)
					val |= DSP_CAP_COPROC;
				if (audio_devs[dev]->d->local_qlen)	/* Device has hidden buffers */
					val |= DSP_CAP_BATCH;
				if (audio_devs[dev]->d->trigger)	/* Supports SETTRIGGER */
					val |= DSP_CAP_TRIGGER;
				break;
			
		case SOUND_PCM_WRITE_RATE:
			if (get_user(val, p))
				return -EFAULT;
			val = audio_devs[dev]->d->set_speed(dev, val);
			break;

		case SOUND_PCM_READ_RATE:
			val = audio_devs[dev]->d->set_speed(dev, 0);
			break;
			
		case SNDCTL_DSP_STEREO:
			if (get_user(val, p))
				return -EFAULT;
			if (val > 1 || val < 0)
				return -EINVAL;
			val = audio_devs[dev]->d->set_channels(dev, val + 1) - 1;
			break;

		case SOUND_PCM_WRITE_CHANNELS:
			if (get_user(val, p))
				return -EFAULT;
			val = audio_devs[dev]->d->set_channels(dev, val);
			break;

		case SOUND_PCM_READ_CHANNELS:
			val = audio_devs[dev]->d->set_channels(dev, 0);
			break;
		
		case SOUND_PCM_READ_BITS:
			val = audio_devs[dev]->d->set_bits(dev, 0);
			break;

		case SNDCTL_DSP_SETDUPLEX:
			if (audio_devs[dev]->open_mode != OPEN_READWRITE)
				return -EPERM;
			return (audio_devs[dev]->flags & DMA_DUPLEX) ? 0 : -EIO;

		case SNDCTL_DSP_PROFILE:
			if (get_user(val, p))
				return -EFAULT;
			if (audio_devs[dev]->open_mode & OPEN_WRITE)
				audio_devs[dev]->dmap_out->applic_profile = val;
			if (audio_devs[dev]->open_mode & OPEN_READ)
				audio_devs[dev]->dmap_in->applic_profile = val;
			return 0;
		
		case SNDCTL_DSP_GETODELAY:
			dmap = audio_devs[dev]->dmap_out;
			if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				return -EINVAL;
			if (!(dmap->flags & DMA_ALLOC_DONE))
			{
				val=0;
				break;
			}
		
			spin_lock_irqsave(&dmap->lock,flags);
			/* Compute number of bytes that have been played */
			count = DMAbuf_get_buffer_pointer (dev, dmap, DMODE_OUTPUT);
			if (count < dmap->fragment_size && dmap->qhead != 0)
				count += dmap->bytes_in_use;	/* Pointer wrap not handled yet */
			count += dmap->byte_counter;
		
			/* Substract current count from the number of bytes written by app */
			count = dmap->user_counter - count;
			if (count < 0)
				count = 0;
			spin_unlock_irqrestore(&dmap->lock,flags);
			val = count;
			break;
		
		default:
			return dma_ioctl(dev, cmd, arg);
	}
	return put_user(val, p);
}

void audio_init_devices(void)
{
	/*
	 * NOTE! This routine could be called several times during boot.
	 */
}

void reorganize_buffers(int dev, struct dma_buffparms *dmap, int recording)
{
	/*
	 * This routine breaks the physical device buffers to logical ones.
	 */

	struct audio_operations *dsp_dev = audio_devs[dev];

	unsigned i, n;
	unsigned sr, nc, sz, bsz;

	sr = dsp_dev->d->set_speed(dev, 0);
	nc = dsp_dev->d->set_channels(dev, 0);
	sz = dsp_dev->d->set_bits(dev, 0);

	if (sz == 8)
		dmap->neutral_byte = NEUTRAL8;
	else
		dmap->neutral_byte = NEUTRAL16;

	if (sr < 1 || nc < 1 || sz < 1)
	{
/*		printk(KERN_DEBUG "Warning: Invalid PCM parameters[%d] sr=%d, nc=%d, sz=%d\n", dev, sr, nc, sz);*/
		sr = DSP_DEFAULT_SPEED;
		nc = 1;
		sz = 8;
	}
	
	sz = sr * nc * sz;

	sz /= 8;		/* #bits -> #bytes */
	dmap->data_rate = sz;

	if (!dmap->needs_reorg)
		return;
	dmap->needs_reorg = 0;

	if (dmap->fragment_size == 0)
	{	
		/* Compute the fragment size using the default algorithm */

		/*
		 * Compute a buffer size for time not exceeding 1 second.
		 * Usually this algorithm gives a buffer size for 0.5 to 1.0 seconds
		 * of sound (using the current speed, sample size and #channels).
		 */

		bsz = dmap->buffsize;
		while (bsz > sz)
			bsz /= 2;

		if (bsz == dmap->buffsize)
			bsz /= 2;	/* Needs at least 2 buffers */

		/*
		 *    Split the computed fragment to smaller parts. After 3.5a9
		 *      the default subdivision is 4 which should give better
		 *      results when recording.
		 */

		if (dmap->subdivision == 0)	/* Not already set */
		{
			dmap->subdivision = 4;	/* Init to the default value */

			if ((bsz / dmap->subdivision) > 4096)
				dmap->subdivision *= 2;
			if ((bsz / dmap->subdivision) < 4096)
				dmap->subdivision = 1;
		}
		bsz /= dmap->subdivision;

		if (bsz < 16)
			bsz = 16;	/* Just a sanity check */

		dmap->fragment_size = bsz;
	}
	else
	{
		/*
		 * The process has specified the buffer size with SNDCTL_DSP_SETFRAGMENT or
		 * the buffer size computation has already been done.
		 */
		if (dmap->fragment_size > (dmap->buffsize / 2))
			dmap->fragment_size = (dmap->buffsize / 2);
		bsz = dmap->fragment_size;
	}

	if (audio_devs[dev]->min_fragment)
		if (bsz < (1 << audio_devs[dev]->min_fragment))
			bsz = 1 << audio_devs[dev]->min_fragment;
	if (audio_devs[dev]->max_fragment)
		if (bsz > (1 << audio_devs[dev]->max_fragment))
			bsz = 1 << audio_devs[dev]->max_fragment;
	bsz &= ~0x07;		/* Force size which is multiple of 8 bytes */
#ifdef OS_DMA_ALIGN_CHECK
	OS_DMA_ALIGN_CHECK(bsz);
#endif

	n = dmap->buffsize / bsz;
	if (n > MAX_SUB_BUFFERS)
		n = MAX_SUB_BUFFERS;
	if (n > dmap->max_fragments)
		n = dmap->max_fragments;

	if (n < 2)
	{
		n = 2;
		bsz /= 2;
	}
	dmap->nbufs = n;
	dmap->bytes_in_use = n * bsz;
	dmap->fragment_size = bsz;
	dmap->max_byte_counter = (dmap->data_rate * 60 * 60) +
			dmap->bytes_in_use;	/* Approximately one hour */

	if (dmap->raw_buf)
	{
		memset(dmap->raw_buf, dmap->neutral_byte, dmap->bytes_in_use);
	}
	
	for (i = 0; i < dmap->nbufs; i++)
	{
		dmap->counts[i] = 0;
	}

	dmap->flags |= DMA_ALLOC_DONE | DMA_EMPTY;
}

static int dma_subdivide(int dev, struct dma_buffparms *dmap, int fact)
{
	if (fact == 0) 
	{
		fact = dmap->subdivision;
		if (fact == 0)
			fact = 1;
		return fact;
	}
	if (dmap->subdivision != 0 || dmap->fragment_size)	/* Too late to change */
		return -EINVAL;

	if (fact > MAX_REALTIME_FACTOR)
		return -EINVAL;

	if (fact != 1 && fact != 2 && fact != 4 && fact != 8 && fact != 16)
		return -EINVAL;

	dmap->subdivision = fact;
	return fact;
}

static int dma_set_fragment(int dev, struct dma_buffparms *dmap, int fact)
{
	int bytes, count;

	if (fact == 0)
		return -EIO;

	if (dmap->subdivision != 0 ||
	    dmap->fragment_size)	/* Too late to change */
		return -EINVAL;

	bytes = fact & 0xffff;
	count = (fact >> 16) & 0x7fff;

	if (count == 0)
		count = MAX_SUB_BUFFERS;
	else if (count < MAX_SUB_BUFFERS)
		count++;

	if (bytes < 4 || bytes > 17)	/* <16 || > 512k */
		return -EINVAL;

	if (count < 2)
		return -EINVAL;

	if (audio_devs[dev]->min_fragment > 0)
		if (bytes < audio_devs[dev]->min_fragment)
			bytes = audio_devs[dev]->min_fragment;

	if (audio_devs[dev]->max_fragment > 0)
		if (bytes > audio_devs[dev]->max_fragment)
			bytes = audio_devs[dev]->max_fragment;

#ifdef OS_DMA_MINBITS
	if (bytes < OS_DMA_MINBITS)
		bytes = OS_DMA_MINBITS;
#endif

	dmap->fragment_size = (1 << bytes);
	dmap->max_fragments = count;

	if (dmap->fragment_size > dmap->buffsize)
		dmap->fragment_size = dmap->buffsize;

	if (dmap->fragment_size == dmap->buffsize &&
	    audio_devs[dev]->flags & DMA_AUTOMODE)
		dmap->fragment_size /= 2;	/* Needs at least 2 buffers */

	dmap->subdivision = 1;	/* Disable SNDCTL_DSP_SUBDIVIDE */
	return bytes | ((count - 1) << 16);
}

static int dma_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	struct dma_buffparms *dmap_out = audio_devs[dev]->dmap_out;
	struct dma_buffparms *dmap_in = audio_devs[dev]->dmap_in;
	struct dma_buffparms *dmap;
	audio_buf_info info;
	count_info cinfo;
	int fact, ret, changed, bits, count, err;
	unsigned long flags;

	switch (cmd) 
	{
		case SNDCTL_DSP_SUBDIVIDE:
			ret = 0;
			if (get_user(fact, (int __user *)arg))
				return -EFAULT;
			if (audio_devs[dev]->open_mode & OPEN_WRITE)
				ret = dma_subdivide(dev, dmap_out, fact);
			if (ret < 0)
				return ret;
			if (audio_devs[dev]->open_mode != OPEN_WRITE ||
				(audio_devs[dev]->flags & DMA_DUPLEX &&
					audio_devs[dev]->open_mode & OPEN_READ))
				ret = dma_subdivide(dev, dmap_in, fact);
			if (ret < 0)
				return ret;
			break;

		case SNDCTL_DSP_GETISPACE:
		case SNDCTL_DSP_GETOSPACE:
			dmap = dmap_out;
			if (cmd == SNDCTL_DSP_GETISPACE && !(audio_devs[dev]->open_mode & OPEN_READ))
				return -EINVAL;
			if (cmd == SNDCTL_DSP_GETOSPACE && !(audio_devs[dev]->open_mode & OPEN_WRITE))
				return -EINVAL;
			if (cmd == SNDCTL_DSP_GETISPACE && audio_devs[dev]->flags & DMA_DUPLEX)
				dmap = dmap_in;
			if (dmap->mapping_flags & DMA_MAP_MAPPED)
				return -EINVAL;
			if (!(dmap->flags & DMA_ALLOC_DONE))
				reorganize_buffers(dev, dmap, (cmd == SNDCTL_DSP_GETISPACE));
			info.fragstotal = dmap->nbufs;
			if (cmd == SNDCTL_DSP_GETISPACE)
				info.fragments = dmap->qlen;
			else 
			{
				if (!DMAbuf_space_in_queue(dev))
					info.fragments = 0;
				else
				{
					info.fragments = DMAbuf_space_in_queue(dev);
					if (audio_devs[dev]->d->local_qlen) 
					{
						int tmp = audio_devs[dev]->d->local_qlen(dev);
						if (tmp && info.fragments)
							tmp--;	/*
								 * This buffer has been counted twice
								 */
						info.fragments -= tmp;
					}
				}
			}
			if (info.fragments < 0)
				info.fragments = 0;
			else if (info.fragments > dmap->nbufs)
				info.fragments = dmap->nbufs;

			info.fragsize = dmap->fragment_size;
			info.bytes = info.fragments * dmap->fragment_size;

			if (cmd == SNDCTL_DSP_GETISPACE && dmap->qlen)
				info.bytes -= dmap->counts[dmap->qhead];
			else 
			{
				info.fragments = info.bytes / dmap->fragment_size;
				info.bytes -= dmap->user_counter % dmap->fragment_size;
			}
			if (copy_to_user(arg, &info, sizeof(info)))
				return -EFAULT;
			return 0;

		case SNDCTL_DSP_SETTRIGGER:
			if (get_user(bits, (int __user *)arg))
				return -EFAULT;
			bits &= audio_devs[dev]->open_mode;
			if (audio_devs[dev]->d->trigger == NULL)
				return -EINVAL;
			if (!(audio_devs[dev]->flags & DMA_DUPLEX) && (bits & PCM_ENABLE_INPUT) &&
				(bits & PCM_ENABLE_OUTPUT))
				return -EINVAL;

			if (bits & PCM_ENABLE_INPUT)
			{
				spin_lock_irqsave(&dmap_in->lock,flags);
				changed = (audio_devs[dev]->enable_bits ^ bits) & PCM_ENABLE_INPUT;
				if (changed && audio_devs[dev]->go) 
				{
					reorganize_buffers(dev, dmap_in, 1);
					if ((err = audio_devs[dev]->d->prepare_for_input(dev,
						     dmap_in->fragment_size, dmap_in->nbufs)) < 0) {
						spin_unlock_irqrestore(&dmap_in->lock,flags);
						return -err;
					}
					dmap_in->dma_mode = DMODE_INPUT;
					audio_devs[dev]->enable_bits |= PCM_ENABLE_INPUT;
					DMAbuf_activate_recording(dev, dmap_in);
				} else
					audio_devs[dev]->enable_bits &= ~PCM_ENABLE_INPUT;
				spin_unlock_irqrestore(&dmap_in->lock,flags);
			}
			if (bits & PCM_ENABLE_OUTPUT)
			{
				spin_lock_irqsave(&dmap_out->lock,flags);
				changed = (audio_devs[dev]->enable_bits ^ bits) & PCM_ENABLE_OUTPUT;
				if (changed &&
				    (dmap_out->mapping_flags & DMA_MAP_MAPPED || dmap_out->qlen > 0) &&
				    audio_devs[dev]->go) 
				{
					if (!(dmap_out->flags & DMA_ALLOC_DONE))
						reorganize_buffers(dev, dmap_out, 0);
					dmap_out->dma_mode = DMODE_OUTPUT;
					audio_devs[dev]->enable_bits |= PCM_ENABLE_OUTPUT;
					dmap_out->counts[dmap_out->qhead] = dmap_out->fragment_size;
					DMAbuf_launch_output(dev, dmap_out);
				} else
					audio_devs[dev]->enable_bits &= ~PCM_ENABLE_OUTPUT;
				spin_unlock_irqrestore(&dmap_out->lock,flags);
			}
#if 0
			if (changed && audio_devs[dev]->d->trigger)
				audio_devs[dev]->d->trigger(dev, bits * audio_devs[dev]->go);
#endif				
			/* Falls through... */

		case SNDCTL_DSP_GETTRIGGER:
			ret = audio_devs[dev]->enable_bits;
			break;

		case SNDCTL_DSP_SETSYNCRO:
			if (!audio_devs[dev]->d->trigger)
				return -EINVAL;
			audio_devs[dev]->d->trigger(dev, 0);
			audio_devs[dev]->go = 0;
			return 0;

		case SNDCTL_DSP_GETIPTR:
			if (!(audio_devs[dev]->open_mode & OPEN_READ))
				return -EINVAL;
			spin_lock_irqsave(&dmap_in->lock,flags);
			cinfo.bytes = dmap_in->byte_counter;
			cinfo.ptr = DMAbuf_get_buffer_pointer(dev, dmap_in, DMODE_INPUT) & ~3;
			if (cinfo.ptr < dmap_in->fragment_size && dmap_in->qtail != 0)
				cinfo.bytes += dmap_in->bytes_in_use;	/* Pointer wrap not handled yet */
			cinfo.blocks = dmap_in->qlen;
			cinfo.bytes += cinfo.ptr;
			if (dmap_in->mapping_flags & DMA_MAP_MAPPED)
				dmap_in->qlen = 0;	/* Reset interrupt counter */
			spin_unlock_irqrestore(&dmap_in->lock,flags);
			if (copy_to_user(arg, &cinfo, sizeof(cinfo)))
				return -EFAULT;
			return 0;

		case SNDCTL_DSP_GETOPTR:
			if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				return -EINVAL;

			spin_lock_irqsave(&dmap_out->lock,flags);
			cinfo.bytes = dmap_out->byte_counter;
			cinfo.ptr = DMAbuf_get_buffer_pointer(dev, dmap_out, DMODE_OUTPUT) & ~3;
			if (cinfo.ptr < dmap_out->fragment_size && dmap_out->qhead != 0)
				cinfo.bytes += dmap_out->bytes_in_use;	/* Pointer wrap not handled yet */
			cinfo.blocks = dmap_out->qlen;
			cinfo.bytes += cinfo.ptr;
			if (dmap_out->mapping_flags & DMA_MAP_MAPPED)
				dmap_out->qlen = 0;	/* Reset interrupt counter */
			spin_unlock_irqrestore(&dmap_out->lock,flags);
			if (copy_to_user(arg, &cinfo, sizeof(cinfo)))
				return -EFAULT;
			return 0;

		case SNDCTL_DSP_GETODELAY:
			if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				return -EINVAL;
			if (!(dmap_out->flags & DMA_ALLOC_DONE))
			{
				ret=0;
				break;
			}
			spin_lock_irqsave(&dmap_out->lock,flags);
			/* Compute number of bytes that have been played */
			count = DMAbuf_get_buffer_pointer (dev, dmap_out, DMODE_OUTPUT);
			if (count < dmap_out->fragment_size && dmap_out->qhead != 0)
				count += dmap_out->bytes_in_use;	/* Pointer wrap not handled yet */
			count += dmap_out->byte_counter;
			/* Substract current count from the number of bytes written by app */
			count = dmap_out->user_counter - count;
			if (count < 0)
				count = 0;
			spin_unlock_irqrestore(&dmap_out->lock,flags);
			ret = count;
			break;

		case SNDCTL_DSP_POST:
			if (audio_devs[dev]->dmap_out->qlen > 0)
				if (!(audio_devs[dev]->dmap_out->flags & DMA_ACTIVE))
					DMAbuf_launch_output(dev, audio_devs[dev]->dmap_out);
			return 0;

		case SNDCTL_DSP_GETBLKSIZE:
			dmap = dmap_out;
			if (audio_devs[dev]->open_mode & OPEN_WRITE)
				reorganize_buffers(dev, dmap_out, (audio_devs[dev]->open_mode == OPEN_READ));
			if (audio_devs[dev]->open_mode == OPEN_READ ||
			    (audio_devs[dev]->flags & DMA_DUPLEX &&
			     audio_devs[dev]->open_mode & OPEN_READ))
				reorganize_buffers(dev, dmap_in, (audio_devs[dev]->open_mode == OPEN_READ));
			if (audio_devs[dev]->open_mode == OPEN_READ)
				dmap = dmap_in;
			ret = dmap->fragment_size;
			break;

		case SNDCTL_DSP_SETFRAGMENT:
			ret = 0;
			if (get_user(fact, (int __user *)arg))
				return -EFAULT;
			if (audio_devs[dev]->open_mode & OPEN_WRITE)
				ret = dma_set_fragment(dev, dmap_out, fact);
			if (ret < 0)
				return ret;
			if (audio_devs[dev]->open_mode == OPEN_READ ||
			    (audio_devs[dev]->flags & DMA_DUPLEX &&
			     audio_devs[dev]->open_mode & OPEN_READ))
				ret = dma_set_fragment(dev, dmap_in, fact);
			if (ret < 0)
				return ret;
			if (!arg) /* don't know what this is good for, but preserve old semantics */
				return 0;
			break;

		default:
			if (!audio_devs[dev]->d->ioctl)
				return -EINVAL;
			return audio_devs[dev]->d->ioctl(dev, cmd, arg);
	}
	return put_user(ret, (int __user *)arg);
}
